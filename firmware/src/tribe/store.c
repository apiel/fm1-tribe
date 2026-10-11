/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE's clip store: there is no project file, the clips are the data.
 *
 * The flash holds a log of 512-byte records, eight to a 4 KiB sector: one per saved clip (and one for the
 * settings), each with a sequence number and a CRC. The newest valid record of a clip wins. A save writes
 * only the clips that changed since they were last saved or loaded, each into a free slot, payload first and
 * the header (which makes it valid) last, so a power cut leaves the older copy of that clip in charge.
 * When the current sector is full the next one is an erased spare; an erased spare is always kept in
 * hand: taking the last one first moves the live records of the least-used sector into it and erases that
 * sector (garbage collection). The device's RAM tables below are rebuilt by scanning the log at boot. */
#include "tribe.h"
#include "../app/plat.h"
#include "mem.h"

#define SLOT_SZ 512u
#define PER_SEC 8
#define NSEC PLAT_LOG_SECTORS
#define NSLOT (NSEC * PER_SEC)
#define NKEY (NTRK * NCLIP + 1)            /* 0..NKEY-2: a clip (trk * NCLIP + clip); NKEY-1: the settings */
#define KEY_SET (NKEY - 1)
#define REC_MAGIC 0x31434C54u              /* "TLC1" */
#define HDR_SZ 32u
#define REC_VER 1

typedef struct {
    uint32_t magic, seq, crc;
    uint8_t type, trk, clip, ver;
    uint16_t len, rsv;
    uint32_t pad[3];
} rec_hdr_t;

static uint8_t slot_used[NSLOT], slot_key[NSLOT];
static uint32_t slot_seq[NSLOT];
static uint8_t sec_used[NSEC];
static uint32_t best_seq[NKEY];
static int16_t best_slot[NKEY];
static uint32_t saved_crc[NKEY], seq_next;
static int cur_sec = -1, cur_idx;
static uint8_t rec_buf[SLOT_SZ];
static uint8_t dirty_bit[NKEY];
static int dirty_n, poll_i;

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
static const void *key_data(int key, uint32_t *len)
{
    if (key == KEY_SET) {
        *len = sizeof proj.set;
        return &proj.set;
    }
    *len = sizeof(clip_t);
    return &proj.clip[key / NCLIP][key % NCLIP];
}
static uint32_t key_crc(int key)
{
    uint32_t n;
    const void *d = key_data(key, &n);
    return crc32(d, n);
}

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
    return h->type == 1 && h->len == sizeof proj.set ? KEY_SET : -1;
}

static int is_live(int s)
{
    int k = slot_key[s];
    return slot_used[s] && k != 0xFF && best_slot[k] == s && best_seq[k] == slot_seq[s];
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
    slot_key[s] = (uint8_t)key;
    slot_seq[s] = seq;
    if (key != 0xFF && (best_seq[key] == 0 || seq >= best_seq[key])) {
        best_seq[key] = seq;
        best_slot[key] = (int16_t)s;
    }
}

static int gc(void);
static int make_spare_dead(void);

/* rebuild every table from the log */
static void scan(void)
{
    int s, k;
    memset(slot_used, 0, sizeof slot_used);
    memset(slot_key, 0xFF, sizeof slot_key);
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
    /* the sector to write in: the fullest one that still has room, else none (the next write takes a spare) */
    cur_sec = -1;
    cur_idx = 0;
    for (s = 0; s < NSEC; s++)
        if (sec_used[s] > 0 && sec_used[s] < PER_SEC && (cur_sec < 0 || sec_used[s] > sec_used[cur_sec]))
            cur_sec = s;
    if (cur_sec >= 0) {                              /* its first blank slot (they fill in order) */
        for (cur_idx = 0; cur_idx < PER_SEC && slot_used[cur_sec * PER_SEC + cur_idx]; cur_idx++)
            ;
    }
    {   /* no erased sector left (a collection was cut short): finish it, or erase what holds nothing live */
        int e = 0;
        for (s = 0; s < NSEC; s++)
            e += s != cur_sec && sec_used[s] == 0;
        if (!e) {
            if (cur_sec >= 0 && cur_idx < PER_SEC)
                (void)gc();
            else
                (void)make_spare_dead();
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

/* make room: the live records of the least-used sector move into the new current sector, and it is erased */
static int gc(void)
{
    int v = -1, vl = 99, s, i;
    for (s = 0; s < NSEC; s++)
        if (s != cur_sec && sec_used[s] > 0) {
            int l = live_in(s);
            if (l < vl) {
                vl = l;
                v = s;
            }
        }
    if (v < 0 || vl > PER_SEC - 1 || cur_sec < 0)
        return -1;                                   /* everything in the log is live: nothing to free */
    for (i = 0; i < PER_SEC; i++) {
        int src = v * PER_SEC + i;
        rec_hdr_t h;
        int rc;
        if (!is_live(src))
            continue;
        if (check_slot(src, &h) < 0)
            continue;
        {
            uint8_t keep[SLOT_SZ - HDR_SZ];
            memcpy(keep, rec_buf + HDR_SZ, h.len);
            rc = write_slot(cur_sec * PER_SEC + cur_idx, &h, keep);
        }
        if (rc)
            return rc;
        cur_idx++;
    }
    if (plat_log_erase((uint32_t)v))
        return -3;
    for (i = 0; i < PER_SEC; i++) {
        slot_used[v * PER_SEC + i] = 0;
        slot_key[v * PER_SEC + i] = 0xFF;
        slot_seq[v * PER_SEC + i] = 0;
    }
    sec_used[v] = 0;
    return 0;
}

/* an erased sector to have in hand when none is: a sector holding nothing live (garbage, a torn record, a
 * half-erased sector) is simply erased */
static int make_spare_dead(void)
{
    int s;
    for (s = 0; s < NSEC; s++)
        if (s != cur_sec && sec_used[s] > 0 && live_in(s) == 0) {
            int i;
            if (plat_log_erase((uint32_t)s))
                return -3;
            for (i = 0; i < PER_SEC; i++) {
                slot_used[s * PER_SEC + i] = 0;
                slot_key[s * PER_SEC + i] = 0xFF;
                slot_seq[s * PER_SEC + i] = 0;
            }
            sec_used[s] = 0;
            return 0;
        }
    return -2;
}

/* the next free slot, taking a spare sector (and freeing another) when the current one is full */
static int next_slot(int *slot)
{
    if (cur_sec >= 0 && cur_idx < PER_SEC) {
        *slot = cur_sec * PER_SEC + cur_idx;
        return 0;
    }
    {
        int s, e = 0, spare = -1, rc;
        cur_sec = -1;
        for (s = 0; s < NSEC; s++)
            if (sec_used[s] == 0) {
                e++;
                if (spare < 0)
                    spare = s;
            }
        if (spare < 0) {
            if ((rc = make_spare_dead()) != 0)
                return rc;
            for (s = 0; s < NSEC; s++)
                if (sec_used[s] == 0) {
                    spare = s;
                    break;
                }
            e = 1;
            if (spare < 0)
                return -2;
        }
        cur_sec = spare;
        cur_idx = 0;
        if (e == 1 && (rc = gc()) != 0)              /* that was the last one: free another before going on */
            return rc;
        *slot = cur_sec * PER_SEC + cur_idx;
        return 0;
    }
}

static int put(int key)
{
    rec_hdr_t h;
    uint32_t len;
    const void *d = key_data(key, &len);
    int s, rc;
    if ((rc = next_slot(&s)) != 0)
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
    h.rsv = 0;
    if ((rc = write_slot(s, &h, d)) != 0) {
        cur_idx++;                                   /* a slot that failed is not tried again */
        return rc;
    }
    cur_idx++;
    saved_crc[key] = h.crc;
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

int store_load(void)
{
    int k, n = 0;
    scan();
    for (k = 0; k < NKEY; k++) {
        rec_hdr_t h;
        if (best_slot[k] < 0)
            continue;
        if (check_slot(best_slot[k], &h) < 0)
            continue;
        if (k == KEY_SET) {
            memcpy(&proj.set, rec_buf + HDR_SZ, sizeof proj.set);
        } else {
            memset(&proj.clip[k / NCLIP][k % NCLIP], 0, sizeof(clip_t));
            memcpy(&proj.clip[k / NCLIP][k % NCLIP], rec_buf + HDR_SZ, h.len);
            fix_clip(&proj.clip[k / NCLIP][k % NCLIP]);
        }
        n++;
    }
    for (k = 0; k < NKEY; k++) {                     /* what is in memory now is, by definition, saved */
        saved_crc[k] = key_crc(k);
        dirty_bit[k] = 0;
    }
    dirty_n = 0;
    poll_i = 0;
    return n;
}

/* trk < 0: every changed clip and the settings; else that one clip (if it changed). Returns the records
 * written, or a negative error (-1 the log is full of live records, -2 no spare sector, else a flash error) */
int store_save(int trk, int clip)
{
    int k, n = 0, rc;
    if (trk >= 0) {
        k = trk * NCLIP + clip;
        if (key_crc(k) == saved_crc[k])
            return 0;
        if ((rc = put(k)) != 0)
            return rc;
        dirty_bit[k] = 0;
        return 1;
    }
    for (k = 0; k < NKEY; k++)
        if (key_crc(k) != saved_crc[k]) {
            if ((rc = put(k)) != 0)
                return rc;
            dirty_bit[k] = 0;
            n++;
        }
    dirty_n = 0;
    for (k = 0; k < NKEY; k++)
        dirty_n += dirty_bit[k];
    return n;
}

/* four clips a frame (the whole set in about 0.7 s): does each differ from what was saved? (the SAVE light) */
void store_poll(void)
{
    int n;
    for (n = 0; n < 4; n++) {
        int k = poll_i;
        uint8_t d = key_crc(k) != saved_crc[k];
        if (d != dirty_bit[k]) {
            dirty_bit[k] = d;
            dirty_n += d ? 1 : -1;
        }
        poll_i = k + 1 < NKEY ? k + 1 : 0;
    }
}
int store_dirty(void) { return dirty_n; }
