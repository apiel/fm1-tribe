/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE's clip store: there is no project file, the clips are the data.
 *
 * The flash holds a log of 396-byte records, ten to a 4 KiB sector: one per saved clip (and one for the
 * settings), each with a sequence number and a CRC. The newest valid record of a clip wins. A save writes
 * only the clips that changed since they were last saved or loaded, each into a free slot, payload first and
 * the header (which makes it valid) last, so a power cut leaves the older copy of that clip in charge.
 * A sector is filled in order. Before a write the log keeps two sectors' worth of free slots: when it has less, the
 * sector with the fewest live records is cleaned (its live records move to free slots elsewhere, then it is erased).
 * If every sector is full of live records the write is refused (-1) and the log is left as it was.
 * The device's RAM tables below are rebuilt by scanning the log at boot. */
#include "tribe.h"
#include "../app/plat.h"
#include "mem.h"

#define HDR_SZ 32u
#define SLOT_SZ (HDR_SZ + (uint32_t)sizeof(clip_t))   /* a header and a clip: 396 bytes, ten to a sector */
#define PER_SEC ((int)(4096u / SLOT_SZ))
#define NSEC PLAT_LOG_SECTORS
#define NSLOT (NSEC * PER_SEC)
#define NKEY (NTRK * NCLIP + 1)            /* 0..NKEY-2: a clip (trk * NCLIP + clip); NKEY-1: the settings */
#define KEY_SET (NKEY - 1)
#define NO_KEY 0xFFFFu                     /* a slot with no record */
#define RESERVE (2 * PER_SEC)              /* free slots kept for cleaning: two sectors' worth */
#define REC_MAGIC 0x31434C54u              /* "TLC1" */
#define SET_OLD_LEN __builtin_offsetof(dsettings_t, song)   /* the settings before song mode: a record of this length is a prefix of today's */
#define REC_VER 4                          /* 4: 64 clips a track, 396-byte records. Only this version is read here: 1 to 3 are the old log (legacy_load) */

/* the log of version 3: 512-byte records, eight to a sector, 48 sectors, 16 clips a track */
#define LEG_SLOT_SZ 512u
#define LEG_PER_SEC 8
#define LEG_NSEC 48
#define LEG_CLIPS 16
#define LEG_NKEY (NTRK * LEG_CLIPS + 1)
#define LEG_KEY_SET (LEG_NKEY - 1)

typedef struct {
    uint32_t magic, seq, crc;
    uint8_t type, trk, clip, ver;
    uint16_t len, rsv;
    uint32_t pad[3];
} rec_hdr_t;

/* the tables are rebuilt from the log at boot. They sit in the pool: the RAM is for the clip being played */
static uint8_t slot_used[NSLOT] X0X_POOL;
static uint16_t slot_key[NSLOT] X0X_POOL;   /* the key of the record in each slot, NO_KEY when none (a key is up to 640) */
static uint32_t slot_seq[NSLOT] X0X_POOL;
static uint8_t sec_used[NSEC];
static uint32_t best_seq[NKEY] X0X_POOL;
static int16_t best_slot[NKEY] X0X_POOL;
static uint32_t saved_crc[NKEY] X0X_POOL;   /* what each key holds in the log (or its starting clip): a clip differs when its crc does */
static uint8_t has_bits[(NKEY + 7) / 8];   /* the clips with steps, for the clip grid and the song */
static uint32_t seq_next;
static uint8_t rec_buf[SLOT_SZ];

static uint32_t crc32(const void *p, uint32_t n)
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

/* what a key holds now */
/* the one setting the MASTER knob drives (a pot is a position, not a value to keep): dst's copy of it comes from src */
static void copy_master(dsettings_t *dst, const dsettings_t *src)
{
    switch (dst->knob[0].kind) {
    case KS_VOL: dst->master_vol = src->master_vol; break;
    case KS_DJ: dst->dj[0] = src->dj[0]; break;
    case KS_RESO: dst->dj[1] = src->dj[1]; break;
    case KS_FX1: dst->fxamt[0] = src->fxamt[0]; break;
    case KS_FX2: dst->fxamt[1] = src->fxamt[1]; break;
    case KS_BPM: dst->bpm_x10 = src->bpm_x10; break;
    }
}

/* the settings as they are saved: whatever the MASTER knob is set to do is not saved (it is read from the pot at start-up) */
static const void *set_image(uint32_t *len)
{
    static dsettings_t img;
    static const dsettings_t none;
    img = proj.set;
    copy_master(&img, &none);
    *len = sizeof img;
    return &img;
}
static uint32_t set_crc(void)
{
    uint32_t n;
    const void *d = set_image(&n);
    return crc32(d, n);
}
static uint32_t clip_crc(const clip_t *c) { return crc32(c, sizeof *c); }

static void has_set(int key, int v)
{
    if (v)
        has_bits[key >> 3] |= (uint8_t)(1u << (key & 7));
    else
        has_bits[key >> 3] &= (uint8_t)~(1u << (key & 7));
}
static int has_get(int key) { return (has_bits[key >> 3] >> (key & 7)) & 1; }

static int slot_sec(int s) { return s / PER_SEC; }
static int read_slot(int s, uint32_t off, void *dst, uint32_t n)
{
    return plat_log_read((uint32_t)slot_sec(s), (uint32_t)(s % PER_SEC) * SLOT_SZ + off, dst, n);
}

/* a header is erased flash: nothing was ever written to the slot */
static int hdr_blank(const rec_hdr_t *h)
{
    return h->magic == 0xFFFFFFFFu && h->seq == 0xFFFFFFFFu && h->crc == 0xFFFFFFFFu;
}

/* a record's key, and whether its payload is intact; the payload is left in rec_buf + HDR_SZ */
static int check_slot(int s, rec_hdr_t *h)
{
    if (read_slot(s, 0, h, sizeof *h) || h->magic != REC_MAGIC || h->len > SLOT_SZ - HDR_SZ || h->ver != REC_VER)
        return -1;
    if (read_slot(s, HDR_SZ, rec_buf + HDR_SZ, h->len) || crc32(rec_buf + HDR_SZ, h->len) != h->crc)
        return -1;
    if (h->type == 0)   /* a clip saved before the LFO was a little shorter: it loads, the LFO off */
        return h->trk < NTRK && h->clip < NCLIP && h->len >= 356 && h->len <= sizeof(clip_t) ? h->trk * NCLIP + h->clip : -1;
    return h->type == 1 && (h->len == sizeof proj.set || h->len == SET_OLD_LEN) ? KEY_SET : -1;
}

static int is_live(int s)
{
    int k = slot_key[s];
    return slot_used[s] && k != NO_KEY && best_slot[k] == s && best_seq[k] == slot_seq[s];
}
static int any_live(void)
{
    int s;
    for (s = 0; s < NSLOT; s++)
        if (is_live(s))
            return 1;
    return 0;
}
static int live_in(int sec)
{
    int i, n = 0;
    for (i = 0; i < PER_SEC; i++)
        n += is_live(sec * PER_SEC + i);
    return n;
}

static void note_slot(int s, int key, uint32_t seq)
{
    slot_used[s] = 1;
    slot_key[s] = (uint16_t)key;
    slot_seq[s] = seq;
    if (key != NO_KEY && (best_seq[key] == 0 || seq >= best_seq[key])) {
        best_seq[key] = seq;
        best_slot[key] = (int16_t)s;
    }
}

/* rebuild every table from the log */
static void scan(void)
{
    int s, k;
    memset(slot_used, 0, sizeof slot_used);
    for (s = 0; s < NSLOT; s++)
        slot_key[s] = NO_KEY;
    memset(slot_seq, 0, sizeof slot_seq);
    memset(sec_used, 0, sizeof sec_used);
    memset(best_seq, 0, sizeof best_seq);
    for (k = 0; k < NKEY; k++)
        best_slot[k] = -1;
    seq_next = 1;
    for (s = 0; s < NSLOT; s++) {
        rec_hdr_t h;
        int key;
        if (read_slot(s, 0, &h, sizeof h))
            continue;
        if (hdr_blank(&h)) {
            uint32_t i;                              /* a payload may have been written before the power went */
            int blank = !read_slot(s, HDR_SZ, rec_buf + HDR_SZ, SLOT_SZ - HDR_SZ);
            for (i = HDR_SZ; blank && i < SLOT_SZ; i++)
                blank = rec_buf[i] == 0xFF;
            if (blank)
                continue;
            sec_used[slot_sec(s)]++;
            slot_used[s] = 1;                        /* half a record: garbage, never reused until its sector is erased */
            continue;
        }
        sec_used[slot_sec(s)]++;
        key = check_slot(s, &h);
        slot_used[s] = 1;
        if (key >= 0) {
            note_slot(s, key, h.seq);
            if (h.seq >= seq_next)
                seq_next = h.seq + 1u;
        }
    }
}

static int write_slot(int s, const rec_hdr_t *h, const void *payload)
{
    int sec = slot_sec(s), rc;
    uint32_t off = (uint32_t)(s % PER_SEC) * SLOT_SZ;
    rec_hdr_t chk;
    if (!slot_used[s]) {                             /* from here the slot is not blank, whatever happens */
        slot_used[s] = 1;
        sec_used[sec]++;
    }
    if ((rc = plat_log_prog((uint32_t)sec, off + HDR_SZ, payload, h->len)))
        return rc;
    if ((rc = plat_log_prog((uint32_t)sec, off, h, HDR_SZ)))     /* the commit: the header, last */
        return rc;
    if (read_slot(s, 0, &chk, sizeof chk) || chk.crc != h->crc || chk.seq != h->seq)   /* a part that lost it must not report SAVED */
        return -7;
    if (read_slot(s, HDR_SZ, rec_buf + HDR_SZ, h->len) || crc32(rec_buf + HDR_SZ, h->len) != h->crc)
        return -7;
    note_slot(s, h->type == 1 ? KEY_SET : h->trk * NCLIP + h->clip, h->seq);
    return 0;
}

/* a slot for a record: the free tail of a sector that is partly written, else the first slot of an erased one. Records
 * fill a sector in order, so its free slots are always its tail. Never in sector `not`, the one being cleaned. */
static int alloc_slot(int not, int *slot)
{
    int s;
    for (s = 0; s < NSEC; s++)
        if (s != not && sec_used[s] > 0 && sec_used[s] < PER_SEC) {
            *slot = s * PER_SEC + sec_used[s];
            return 0;
        }
    for (s = 0; s < NSEC; s++)
        if (s != not && sec_used[s] == 0) {
            *slot = s * PER_SEC;
            return 0;
        }
    return -2;
}

static int total_free(void)
{
    int s, f = 0;
    for (s = 0; s < NSEC; s++)
        f += PER_SEC - sec_used[s];
    return f;
}

/* clean one sector: the one with the fewest live records among those with garbage. Its live records move to free slots
 * elsewhere, then it is erased. Returns 0, or -1 when no sector has garbage to gain (every one is full of live records). A copy keeps its
 * sequence number, so a cut between the copies and the erase leaves both, and the newer copy wins at boot. */
static int gc_one(void)
{
    int v = -1, vl = PER_SEC, s, i;
    for (s = 0; s < NSEC; s++)                       /* only a sector with garbage in it gains anything by cleaning */
        if (sec_used[s] > 0 && live_in(s) < sec_used[s] && live_in(s) < vl) {
            vl = live_in(s);
            v = s;
        }
    if (v < 0)
        return -1;
    for (i = 0; i < PER_SEC; i++) {
        int src = v * PER_SEC + i, dst, rc;
        rec_hdr_t h;
        uint8_t keep[SLOT_SZ - HDR_SZ];
        if (!is_live(src))
            continue;
        if (check_slot(src, &h) < 0)                 /* a live record that does not read back: the sector is kept */
            return -3;
        memcpy(keep, rec_buf + HDR_SZ, h.len);
        if ((rc = alloc_slot(v, &dst)) != 0)
            return rc;
        if ((rc = write_slot(dst, &h, keep)) != 0)
            return rc;
    }
    if (plat_log_erase((uint32_t)v))
        return -3;
    for (i = 0; i < PER_SEC; i++) {
        slot_used[v * PER_SEC + i] = 0;
        slot_key[v * PER_SEC + i] = NO_KEY;
        slot_seq[v * PER_SEC + i] = 0;
    }
    sec_used[v] = 0;
    return 0;
}

/* one record of a clip or the settings. First the room is made (RESERVE free slots: with at most 641 live records the
 * least-used sector has at most nine, and cleaning it always finds room for them), then the record goes in a free slot.
 * -1: the log is full of live records, nothing is written */
/* one record: a clip (has: it has steps, kept in the header for the clip grid) or the settings. First the room is made
 * (RESERVE free slots: with at most 641 live records the least-used sector has at most nine, and cleaning it always
 * finds room for them), then the record goes in a free slot. -1: the log is full of live records, nothing is written */
static int put_rec(int key, const void *d, uint32_t len, int has)
{
    rec_hdr_t h;
    int s, rc;
    while (total_free() <= RESERVE)
        if ((rc = gc_one()) != 0)
            return rc;
    if ((rc = alloc_slot(-1, &s)) != 0)
        return rc;
    memset(&h, 0xFF, sizeof h);
    h.magic = REC_MAGIC;
    h.seq = seq_next++;
    h.crc = crc32(d, len);
    h.type = key == KEY_SET ? 1 : 0;
    h.trk = key == KEY_SET ? 0 : (uint8_t)(key / NCLIP);
    h.clip = key == KEY_SET ? 0 : (uint8_t)(key % NCLIP);
    h.ver = REC_VER;
    h.len = (uint16_t)len;
    h.rsv = has ? 1 : 0;
    if ((rc = write_slot(s, &h, d)) != 0)
        return rc;
    saved_crc[key] = h.crc;
    has_set(key, has);
    return 0;
}

/* ---- the interface ---- */
static void fix_clip(clip_t *c)
{
    if (c->eng >= eng_count())
        c->eng = 0;
    if (c->lensel > 2)
        c->lensel = 0;
}

/* LEAD before version 3 had Accent (pot 7), Legato (8) and Vib (9) between Tune and Attack: the pots after them
 * move up three, Unison is 1..3 now, and an LFO on one of the three removed pots goes to Cutoff */
#define LEAD_ENG 26                         /* LEAD's place in the engine table (engines.c) */
static void lead_v2(clip_t *c)
{
    static const uint8_t old[18] = {0, 1, 2, 3, 4, 5, 6, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    uint8_t p[NPAR];
    int i;
    memset(p, 0, sizeof p);
    for (i = 0; i < 18; i++)
        p[i] = c->par[old[i]];
    if (p[4] > 2)
        p[4] = 2;
    memcpy(c->par, p, sizeof p);
    if (c->lfo[LFO_TARGET] >= 7 && c->lfo[LFO_TARGET] <= 9)
        c->lfo[LFO_TARGET] = 0;
    else if (c->lfo[LFO_TARGET] >= 10)
        c->lfo[LFO_TARGET] -= 3;
}

/* a clip out of rec_buf. Version 1 numbered the engines with the 303 in the table (25), so there ACID was 26 and
 * LEAD 27: the 303's clips become ACID (their pots are ACID's defaults), the others move down one */
static void unpack_clip(clip_t *c, const rec_hdr_t *h)
{
    memset(c, 0, sizeof *c);
    memcpy(c, rec_buf + HDR_SZ, h->len);
    if (h->ver == 1) {
        if (c->eng == 25)
            clip_set_engine(c, 25);
        else if (c->eng > 25)
            c->eng--;
    }
    if (h->ver <= 2 && c->eng == LEAD_ENG)
        lead_v2(c);
    fix_clip(c);
}

/* ---- the log of version 3 ----
 * Its records are 512 bytes apart, so today's scan cannot read them. When today's log holds no record at all, the
 * old one is read here (its clips into leg[], its settings into proj.set), the old sectors are erased, and the clips
 * and the settings are written in today's layout. */
static clip_t leg[NTRK][LEG_CLIPS] X0X_POOL;
static uint8_t leg_ok[NTRK][LEG_CLIPS];

static int read_legacy(int s, rec_hdr_t *h)
{
    uint32_t sec = (uint32_t)(s / LEG_PER_SEC), off = (uint32_t)(s % LEG_PER_SEC) * LEG_SLOT_SZ;
    if (plat_log_read(sec, off, h, sizeof *h) || h->magic != REC_MAGIC || h->ver < 1 || h->ver > 3 || h->len > LEG_SLOT_SZ - HDR_SZ)
        return -1;
    if (plat_log_read(sec, off + HDR_SZ, rec_buf + HDR_SZ, h->len) || crc32(rec_buf + HDR_SZ, h->len) != h->crc)
        return -1;
    if (h->type == 1)
        return h->len == sizeof proj.set || h->len == SET_OLD_LEN ? LEG_KEY_SET : -1;
    return h->trk < NTRK && h->clip < LEG_CLIPS && h->len >= 356 && h->len <= sizeof(clip_t) ? h->trk * LEG_CLIPS + h->clip : -1;
}

/* the newest record of each old clip (into leg[]) and of the settings (into proj.set); the number that came over */
static int legacy_load(void)
{
    static uint32_t seq[LEG_NKEY];
    static int16_t slot[LEG_NKEY];
    int s, k, n = 0;
    for (k = 0; k < LEG_NKEY; k++) {
        slot[k] = -1;
        seq[k] = 0;
    }
    for (s = 0; s < LEG_NSEC * LEG_PER_SEC; s++) {
        rec_hdr_t h;
        int key = read_legacy(s, &h);
        if (key >= 0 && (slot[key] < 0 || h.seq >= seq[key])) {
            slot[key] = (int16_t)s;
            seq[key] = h.seq;
        }
    }
    memset(leg_ok, 0, sizeof leg_ok);
    for (k = 0; k < LEG_NKEY; k++) {
        rec_hdr_t h;
        if (slot[k] < 0 || read_legacy(slot[k], &h) < 0)
            continue;
        if (k == LEG_KEY_SET) {
            dsettings_t old = proj.set;
            memcpy(&proj.set, rec_buf + HDR_SZ, h.len);
            copy_master(&proj.set, &old);
        } else {
            unpack_clip(&leg[h.trk][h.clip], &h);
            leg_ok[h.trk][h.clip] = 1;
        }
        n++;
    }
    return n;
}

/* the old sectors are erased (all of them: what came over is in RAM until it is written below), then the clips and
 * the settings are written in today's layout */
static void legacy_migrate(void)
{
    int s, t, c;
    for (s = 0; s < NSEC; s++)
        if (sec_used[s])
            (void)plat_log_erase((uint32_t)s);
    scan();
    for (t = 0; t < NTRK; t++)
        for (c = 0; c < LEG_CLIPS; c++)
            if (leg_ok[t][c])
                (void)put_rec(t * NCLIP + c, &leg[t][c], sizeof(clip_t), clip_has(&leg[t][c]));
    {
        uint32_t n;
        const void *d = set_image(&n);
        (void)put_rec(KEY_SET, d, n, 0);
    }
}

/* after the scan: each key's saved crc and steps flag from its newest record, or from its starting clip when it has
 * none; the settings from their record. Then the clip being played on each track is loaded */
static void finish_load(void)
{
    int k;
    for (k = 0; k < NKEY; k++) {
        rec_hdr_t h;
        if (k == KEY_SET) {
            if (best_slot[k] >= 0 && check_slot(best_slot[k], &h) >= 0) {
                dsettings_t old = proj.set;
                memcpy(&proj.set, rec_buf + HDR_SZ, h.len);     /* an older record: the fields it does not have keep their defaults */
                copy_master(&proj.set, &old);
                saved_crc[k] = h.crc;
            } else
                saved_crc[k] = set_crc();
        } else if (best_slot[k] >= 0 && read_slot(best_slot[k], 0, &h, sizeof h) == 0) {
            saved_crc[k] = h.crc;
            has_set(k, h.rsv & 1);
        } else {
            clip_t d;
            default_clip(k / NCLIP, k % NCLIP, &d);
            saved_crc[k] = clip_crc(&d);
            has_set(k, clip_has(&d));
        }
    }
}

/* the log decides what the tracks start with: the clip each track was on at the last save (clip 0 without one) */
int store_load(void)
{
    int n = 0, t;
    scan();
    if (!any_live()) {                                /* nothing in today's layout: maybe the log of version 3 */
        n = legacy_load();
        if (n > 0) {
            legacy_migrate();
            scan();
        }
    }
    finish_load();
    for (t = 0; t < NTRK; t++) {
        int c = proj.set.cur[t] < NCLIP ? proj.set.cur[t] : 0;
        dk_clip[t] = (uint8_t)c;
        (void)store_fetch(t, c, &act[t]);
    }
    return n;
}

/* the clip as it is saved: the log's newest copy, or the track's starting clip. Returns 1 when the log has it */
int store_fetch(int trk, int clip, clip_t *out)
{
    int k = trk * NCLIP + clip;
    rec_hdr_t h;
    if (best_slot[k] >= 0 && check_slot(best_slot[k], &h) == k) {
        unpack_clip(out, &h);
        return 1;
    }
    default_clip(trk, clip, out);
    return 0;
}

int store_has(int trk, int clip) { return has_get(trk * NCLIP + clip); }

/* a clip is written when it differs from what is saved (returns 1), or not (0); < 0 is a flash error or a full log */
int store_write(int trk, int clip, const clip_t *c)
{
    int k = trk * NCLIP + clip;
    if (clip_crc(c) == saved_crc[k])
        return 0;
    if (put_rec(k, c, sizeof *c, clip_has(c)) != 0)
        return -1;
    return 1;
}

/* the clip a track is playing differs from what is saved (the others are never changed: a clip is left as it is) */
int store_clip_dirty(int trk, int clip)
{
    return clip == dk_clip[trk] && clip_crc(&act[trk]) != saved_crc[trk * NCLIP + clip];
}

int store_save_track(int trk)
{
    int c = dk_clip[trk];
    if (!store_clip_dirty(trk, c))
        return 0;
    return store_write(trk, c, &act[trk]);
}

int store_set_dirty(void) { return set_crc() != saved_crc[KEY_SET]; }

/* the settings (GLO) with the clip each track is on: the one the next start loads */
int store_save_set(void)
{
    uint32_t n;
    const void *d;
    int t;
    for (t = 0; t < NTRK; t++)
        proj.set.cur[t] = dk_clip[t];
    if (!store_set_dirty())
        return 0;
    d = set_image(&n);
    if (put_rec(KEY_SET, d, n, 0) != 0)
        return -1;
    return 1;
}

/* every track's clip that changed, and the settings: returns the records written, or < 0 */
int store_save_all(void)
{
    int t, n = 0, r;
    for (t = 0; t < NTRK; t++) {
        if ((r = store_save_track(t)) < 0)
            return r;
        n += r;
    }
    if ((r = store_save_set()) < 0)
        return r;
    return n + r;
}

/* what is unsaved: the clips being played that changed, and the settings */
int store_dirty(void)
{
    int t, n = store_set_dirty();
    for (t = 0; t < NTRK; t++)
        n += store_clip_dirty(t, dk_clip[t]);
    return n;
}
