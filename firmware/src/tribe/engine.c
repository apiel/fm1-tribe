/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE audio engine: the ten tracks' sequencer, their engines, the mixer and the master.
 * engine_render() runs in the audio ISR and owns every engine; the main loop reaches it only
 * through the command queue (dk_*), and reads the playheads and meters it publishes. */
#include "mem.h"
#include "tribe.h"
#include "scatter.h"
#include "../app/plat.h"
#include "../dsp/master.h"
#include "../dsp/fastmath.h"

dproj_t proj X0X_POOL;

volatile uint8_t dk_tphase[NTRK], dk_next[NTRK];
volatile uint8_t dk_bound[NTRK], dk_scat[NTRK], dk_scat_g;
volatile uint8_t dk_arp_n[NTRK];
volatile int8_t dk_trans[NTRK];
volatile uint32_t dk_gstep;
volatile uint8_t dk_pos[NTRK], dk_clip[NTRK], dk_queued[NTRK], dk_muted[NTRK], dk_playing;
clip_t act[NTRK];                       /* the clip each track is playing (the UI edits it) */
clip_t clip_prep[NTRK][NCLIP_BUF] X0X_POOL;   /* copies the UI prepared from the log: a launch takes one of them */
volatile uint32_t dk_pat_n[NTRK];
volatile uint16_t dk_peak[NTRK];
volatile uint8_t dk_hit[NTRK];

/* ------------------------------------------------------------ the queue --- */
enum { C_PLAY, C_BPM, C_SET, C_RELOAD, C_LAUNCH, C_MUTE, C_NOTE, C_MSET, C_SCAT, C_TRANS };
typedef struct { uint8_t type, trk; int16_t a, b; } cmd_t;
#define QN 128u
static cmd_t q[QN];
static volatile uint32_t q_w, q_r;

static void push(uint8_t type, int trk, int a, int b)
{
    uint32_t w = q_w;
    cmd_t *c = &q[w % QN];
    while (w - q_r >= QN)                        /* full: the ISR will make room (main loop only) */
        ;
    c->type = type;
    c->trk = (uint8_t)trk;
    c->a = (int16_t)a;
    c->b = (int16_t)b;
    __asm__ volatile("" ::: "memory");
    q_w = w + 1u;
}

void dk_play(int on) { push(C_PLAY, 0, on, 0); }
void dk_bpm(int x10) { proj.set.bpm_x10 = (uint16_t)x10; push(C_BPM, 0, x10, 0); }
void dk_set(int t, int i, int v)
{
    act[t].par[i] = (uint8_t)v;
    push(C_SET, t, i, v);
}
void dk_engine(int t, int eng)
{
    clip_set_engine(&act[t], eng);      /* the steps stay */
    push(C_RELOAD, t, 0, 0);
}
void dk_launch(int t, int c, int buf, int now) { push(C_LAUNCH, t, c, (now ? 1 : 0) | (buf << 1)); }
void dk_mute(int t, int on) { push(C_MUTE, t, on, 0); }
void dk_note(int t, int note, int vel) { push(C_NOTE, t, note, vel); }
void dk_master_set(int i, int v) { push(C_MSET, 0, i, v); }
void dk_scatter(int trk, int slot, int on) { push(C_SCAT, trk, slot, on); }
void dk_transpose(int trk, int semitones) { push(C_TRANS, trk, semitones, 0); }

/* ------------------------------------------------------------ the state --- */
static master_t mst;
static float trk_buf[NTRK * 256];
static float spl = 11025.0f, tick_spl = 1837.5f, acc;   /* samples a 16th lasts, a tick (1/96 note) lasts; samples to the next tick */
#define TICKS_16 6
static const uint8_t RATE_TICKS[NSEQRATE] = {6, 12, 3, 4, 8, 2};   /* ticks a step lasts at 1/16, 1/8, 1/32, 1/16T, 1/8T, 1/32T */
static uint32_t gtick, next_tick[NTRK], last_tick[NTRK];
static uint8_t cur_int[NTRK], cnt[NTRK], nxt[NTRK];
static int8_t pdir[NTRK];
static uint8_t playing_;
static uint8_t pos[NTRK], queued[NTRK], qbuf[NTRK], muted[NTRK];
static int32_t gate_left[NTRK];                 /* samples until the gate ends (0 = none) */
static uint32_t rnd = 0x1234567u;

/* scatter: one effect per track and one on the whole mix; two rings for the loop effects on tracks, one for the mix */
#define NTRING 2
static int16_t sc_ring[NTRING + 1][SC_RING] X0X_POOL;
static uint8_t ring_used[NTRING + 1];
static sc_t sc_t_[NTRK], sc_g;
static mfxs_t dj_mix, fx_sl[2];                      /* effect state: the DJ filter on the mix; FX 1 and FX 2 */
static int8_t sc_ring_of[NTRK];                /* track's ring index, -1 */
static int8_t sc_g_ring;

/* the arpeggiator: held notes per track (kept sorted), the step counter, the walk */
#define ARP_MAX 8
static uint8_t arp_notes[NTRK][ARP_MAX], arp_n[NTRK];
static uint32_t arp_idx[NTRK];

static uint32_t rand32(void)
{
    rnd = rnd * 1664525u + 1013904223u;
    return rnd >> 8;
}

static void set_bpm(int x10)
{
    float bpm = (float)x10 * 0.1f;
    spl = 661500.0f / (bpm < 30.0f ? 30.0f : bpm > 300.0f ? 300.0f : bpm);
    tick_spl = spl * (1.0f / (float)TICKS_16);
}

/* load a clip's engine and pots into the track */
static void load_clip(int t)
{
    const clip_t *c = &act[t];
    int i, n;
    if (eng_bound(t) != c->eng)
        eng_bind(t, c->eng);
    else
        eng_panic(t);
    n = eng_nparams(eng_bound(t));
    for (i = 0; i < n; i++)
        eng_set(t, i, c->par[i]);
    dk_bound[t] = (uint8_t)eng_bound(t);
    gate_left[t] = 0;
}

static void switch_clip(int t, int c, int buf)
{
    memcpy(&act[t], &clip_prep[t][buf], sizeof(clip_t));
    dk_clip[t] = (uint8_t)c;
    queued[t] = 0xFF;
    dk_queued[t] = 0xFF;
    load_clip(t);
}

static void launch(int t, int c, int now, int buf)
{
    if (c == dk_clip[t]) {                          /* the clip that plays: a queued change to another one is dropped, its edits stay */
        queued[t] = 0xFF;
        dk_queued[t] = 0xFF;
        return;
    }
    if (now || !playing_) {
        switch_clip(t, c, buf);
        return;
    }
    queued[t] = (uint8_t)c;
    qbuf[t] = (uint8_t)buf;
    dk_queued[t] = (uint8_t)c;
}

static void arp_add(int t, int note)
{
    int i, n = arp_n[t];
    for (i = 0; i < n; i++)
        if (arp_notes[t][i] == note)
            return;
    if (n >= ARP_MAX)
        return;
    for (i = n; i > 0 && arp_notes[t][i - 1] > note; i--)    /* sorted */
        arp_notes[t][i] = arp_notes[t][i - 1];
    arp_notes[t][i] = (uint8_t)note;
    dk_arp_n[t] = arp_n[t] = (uint8_t)(n + 1);
}

static void arp_del(int t, int note)
{
    int i, n = arp_n[t];
    for (i = 0; i < n; i++)
        if (arp_notes[t][i] == note) {
            for (; i + 1 < n; i++)
                arp_notes[t][i] = arp_notes[t][i + 1];
            dk_arp_n[t] = arp_n[t] = (uint8_t)(n - 1);
            if (n == 1)
                arp_idx[t] = 0;
            return;
        }
}

/* one arpeggio note: mode 1 up, 2 down, 3 up-down, 4 random; over arp_oct octaves */
static void arp_play(int t, int div)
{
    int n = arp_n[t], oct = proj.set.arp_oct[t] < 1 ? 1 : proj.set.arp_oct[t], total = n * oct, i, note;
    uint32_t idx = arp_idx[t]++;
    switch (proj.set.arp_mode[t]) {
    case 2: i = total - 1 - (int)(idx % (uint32_t)total); break;
    case 3: {
        int period = total > 1 ? 2 * total - 2 : 1, k = (int)(idx % (uint32_t)period);
        i = k < total ? k : period - k;
        break;
    }
    case 4: i = (int)(rand32() % (uint32_t)total); break;
    default: i = (int)(idx % (uint32_t)total); break;
    }
    note = arp_notes[t][i % n] + 12 * (i / n);
    note = note > 127 ? 127 : note;
    eng_note(t, note, 100, 0);
    dk_hit[t] = 100;
    if (eng_info(eng_bound(t))->kind == EK_SYNTH) {
        int32_t g = (int32_t)(spl * (float)div * (float)(proj.set.arp_gate[t] < 10 ? 10 : proj.set.arp_gate[t]) * 0.01f);
        gate_left[t] = g < 1 ? 1 : g;
    }
}

static void scat_free(int slot_scope_trk)    /* stop what a scope plays and give its ring back */
{
    if (slot_scope_trk >= 0) {
        int t = slot_scope_trk;
        if (sc_ring_of[t] >= 0)
            ring_used[sc_ring_of[t]] = 0;
        sc_ring_of[t] = -1;
        dk_scat[t] = 0;
    } else {
        if (sc_g_ring >= 0)
            ring_used[sc_g_ring] = 0;
        sc_g_ring = -1;
        dk_scat_g = 0;
    }
}

static void scat_set(int t, int slot, int on)
{
    int scope = slot < 8 ? t : -1, r = -1, i;
    if (!on) {
        if (scope >= 0 ? dk_scat[t] == slot + 1 : dk_scat_g == slot - 7)
            scat_free(scope);
        return;
    }
    scat_free(scope);
    if (sc_needs_ring(slot)) {
        if (scope < 0)
            r = NTRING;
        else
            for (i = 0; i < NTRING; i++)
                if (!ring_used[i]) {
                    r = i;
                    break;
                }
        if (r < 0)
            return;                               /* all the track rings are busy: this one waits for its turn */
        ring_used[r] = 1;
    }
    if (scope >= 0) {
        sc_ring_of[t] = (int8_t)r;
        sc_start(&sc_t_[t], slot, r >= 0 ? sc_ring[r] : 0, spl);
        dk_scat[t] = (uint8_t)(slot + 1);
    } else {
        sc_g_ring = (int8_t)r;
        sc_start(&sc_g, slot, r >= 0 ? sc_ring[r] : 0, spl);
        dk_scat_g = (uint8_t)(slot - 7);
    }
}

static void pattern_start(int t);

static void drain(void)
{
    while (q_r != q_w) {
        const cmd_t c = q[q_r % QN];
        switch (c.type) {
        case C_PLAY:
            if (c.a && !playing_) {
                int t;
                playing_ = 1;
                acc = 0.0f;
                gtick = 0;
                for (t = 0; t < NTRK; t++) {
                    pos[t] = 0;
                    arp_idx[t] = 0;
                    next_tick[t] = last_tick[t] = 0;
                    cur_int[t] = TICKS_16;
                    pattern_start(t);
                }
                dk_gstep = 0;
            } else if (!c.a && playing_) {
                int t;
                playing_ = 0;
                for (t = 0; t < NTRK; t++) {
                    eng_panic(t);
                    gate_left[t] = 0;
                    dk_arp_n[t] = arp_n[t] = 0;
                    arp_idx[t] = 0;
                }
            }
            dk_playing = playing_;
            break;
        case C_BPM: set_bpm(c.a); break;
        case C_SET: eng_set(c.trk, c.a, c.b); break;
        case C_RELOAD: load_clip(c.trk); break;
        case C_LAUNCH: launch(c.trk, c.a, c.b & 1, (c.b >> 1) & 3); break;
        case C_MUTE:
            muted[c.trk] = (uint8_t)c.a;
            dk_muted[c.trk] = (uint8_t)c.a;
            if (c.a)
                eng_panic(c.trk);
            break;
        case C_NOTE:
            if (playing_ && proj.set.arp_mode[c.trk] >= 1 && proj.set.arp_mode[c.trk] <= 4) {   /* the arpeggiator holds it; its clock plays it */
                if (c.b > 0)
                    arp_add(c.trk, c.a);
                else
                    arp_del(c.trk, c.a);
            } else if (c.b > 0) {
                eng_key_on(c.trk, c.a, c.b);
                dk_hit[c.trk] = (uint8_t)c.b;
            } else
                eng_key_off(c.trk, c.a);
            break;
        case C_SCAT: scat_set(c.trk, c.a, c.b); break;
        case C_TRANS: dk_trans[c.trk] = (int8_t)(c.a < -48 ? -48 : c.a > 48 ? 48 : c.a); break;
        case C_MSET: master_set(&mst, c.a, c.b); break;
        }
        q_r++;
    }
}

/* a pattern begins: the first step to play (by direction), the step counter at zero */
static void pattern_start(int t)
{
    const clip_t *cl = &act[t];
    int len = LEN_OF(cl);
    cnt[t] = 0;
    pdir[t] = 1;
    nxt[t] = (uint8_t)(cl->dir == SD_REV ? len - 1 : cl->dir == SD_RAND ? (int)(rand32() % (uint32_t)len) : 0);
    dk_next[t] = nxt[t];
}

/* the step after p, in the clip's direction */
static int next_step(int t, int p, int len, int dir)
{
    switch (dir) {
    case SD_REV: return (p + len - 1) % len;
    case SD_RAND: return (int)(rand32() % (uint32_t)len);
    case SD_PING:
        if (len < 2)
            return 0;
        p += pdir[t];
        if (p >= len) {
            p = len - 2;
            pdir[t] = -1;
        } else if (p < 0) {
            p = 1;
            pdir[t] = 1;
        }
        return p;
    default: return (p + 1) % len;
    }
}

/* one step of one track, when its own clock says so */
static void step_track(int t)
{
    const clip_t *cl;
    const dstep_t *s;
    int p, len, hit = 0, period, off, par;
    if (cnt[t] == 0) {
        dk_pat_n[t]++;                                  /* the UI looks at each pattern start (song mode queues the next clip then) */
        if (queued[t] != 0xFF) {                        /* a clip waits for the pattern's end */
            switch_clip(t, queued[t], qbuf[t]);
            pattern_start(t);
        }
    }
    cl = &act[t];
    len = LEN_OF(cl);
    p = nxt[t];
    if (p >= len)
        p = 0;
    dk_pos[t] = (uint8_t)p;
    period = RATE_TICKS[cl->rate < NSEQRATE ? cl->rate : 0];
    s = &cl->st[p];
    if (step_on(cl, p) && eng_bound(t) && (s->prob >= 100 || (int)(rand32() % 100u) < s->prob)) {
        if (!muted[t]) {
            int nn = (int)s->note + dk_trans[t];                 /* the keyboard's transposition (ARP > TRSP) */
            eng_note(t, nn < 0 ? 0 : nn > 127 ? 127 : nn, s->vel, s->slide);
            hit = 1;
            dk_hit[t] = s->vel;
            if (eng_info(eng_bound(t))->kind == EK_SYNTH)     /* a gate is a share of this clip's own step */
                gate_left[t] = (int32_t)((float)(s->len ? s->len : 3) * (float)period * tick_spl * 0.25f);
        }
    }
    if (!hit && eng_slide_pending(t)) {
        eng_panic(t);
        gate_left[t] = 0;
    }
    par = cnt[t] & 1;
    if (++cnt[t] >= len)
        cnt[t] = 0;
    nxt[t] = (uint8_t)next_step(t, p, len, cl->dir);
    dk_next[t] = nxt[t];
    /* swing: the second step of each pair arrives late (up to a third of a step), the pair keeps its length */
    off = ((int)cl->swing * period / 3 + 63) / 127;
    cur_int[t] = (uint8_t)(par ? period - off : period + off);
    last_tick[t] = gtick;
    next_tick[t] += cur_int[t];
}

/* the arpeggiators run on the 16th grid whatever a clip's rate */
static void arp_tick(void)
{
    int t;
    for (t = 0; t < NTRK; t++)
        if (proj.set.arp_mode[t] >= 1 && proj.set.arp_mode[t] <= 4 && arp_n[t] && !muted[t]) {
            int div = 1 << (proj.set.arp_rate[t] > 2 ? 2 : proj.set.arp_rate[t]);
            if (dk_gstep % (uint32_t)div == 0)
                arp_play(t, div);
        }
    dk_gstep++;
}

/* one tick (a 96th note) of the clock */
static void tick_all(void)
{
    int t;
    for (t = 0; t < NTRK; t++)
        while (gtick >= next_tick[t])
            step_track(t);
    if (gtick % TICKS_16 == 0)
        arp_tick();
    gtick++;
}

static int32_t next_event(int32_t want)
{
    int32_t k = want, t;
    if (playing_) {
        int32_t until = (int32_t)acc;
        if (acc > (float)until)
            until++;
        if (until < 1)
            until = 1;
        if (until < k)
            k = until;
    }
    for (t = 0; t < NTRK; t++)
        if (gate_left[t] > 0 && gate_left[t] < k)
            k = gate_left[t];
    return k < 1 ? 1 : k;
}

/* ---- the LFOs: one a clip, moving one pot of the engine around the value the clip holds. Run once a block
 * (172 times a second): slow modulation (wobble, sweeps), not audio rate. ---- */
static float lfo_ph[NTRK], lfo_hold[NTRK];
static float lfo_rp[NTRK];                         /* the rate pot the LFO runs at: it glides toward the clip's */
static int8_t lfo_mode[NTRK];                      /* the sync flag the rate was last set in (-1: none): a change of mode snaps */
static int16_t lfo_sent[NTRK], lfo_tgt[NTRK];      /* the last value sent to the engine (-1: none), and the pot it was for */
static uint32_t lfo_notes[NTRK];
#define LFO_CHUNK 32                               /* the LFO runs every 32 samples: 1378 times a second */
#define LFO_GLIDE (0.03f * 44100.0f)               /* the rate's glide, a time constant of 30 ms, in samples */

static float lfo_shape(int shape, float ph, float *hold)
{
    switch (shape) {
    case LS_TRI: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    case LS_SAW: return 1.0f - 2.0f * ph;
    case LS_SQUARE: return ph < 0.5f ? 1.0f : -1.0f;
    case LS_RANDOM: return *hold;
    default: return fm_sin_turns(ph);
    }
}

static void lfo_update(int n)
{
    int t;
    for (t = 0; t < NTRK; t++) {
        const clip_t *c = &act[t];
        int eng = eng_bound(t), tgt = c->lfo[LFO_TARGET], np = eng_nparams(eng), depth = c->lfo[LFO_DEPTH];
        if (lfo_sent[t] >= 0 && (!depth || tgt != lfo_tgt[t] || !eng)) {      /* switched off, or aimed elsewhere: the pot goes home */
            if (lfo_tgt[t] < eng_nparams(eng) && eng)
                eng_set(t, lfo_tgt[t], c->par[lfo_tgt[t]]);
            lfo_sent[t] = -1;
        }
        if (!depth || !eng || tgt >= np || eng_info(eng)->kind != EK_SYNTH) {
            lfo_mode[t] = -1;                                                  /* the LFO is for synths; off, its rate starts over */
            continue;
        }
        {
            float period, max = (float)eng_param(eng, tgt)->max, v, eff, target = (float)c->lfo[LFO_RATE];
            int sync = c->lfo[LFO_SYNC] ? 1 : 0, shape = c->lfo[LFO_SHAPE], e;
            if (c->lfo[LFO_RETRIG] && eng_notes[t] != lfo_notes[t]) {
                lfo_ph[t] = 0.0f;                                              /* each note starts the wave again */
                lfo_hold[t] = (float)(rand32() & 0xFFFFu) * (2.0f / 65535.0f) - 1.0f;    /* (and a random one takes a new level) */
            }
            lfo_notes[t] = eng_notes[t];
            if (lfo_mode[t] != sync) {                                         /* switched on, or sync changed: the rate snaps */
                lfo_rp[t] = target;
                lfo_mode[t] = (int8_t)sync;
            } else
                lfo_rp[t] += (target - lfo_rp[t]) * (1.0f - fm_expf(-(float)n / LFO_GLIDE));   /* a rate change glides */
            period = sync ? LFO_STEPS(lfo_rp[t]) * spl : 44100.0f / LFO_HZ(lfo_rp[t]);
            lfo_ph[t] += (float)n / period;
            if (lfo_ph[t] >= 1.0f) {
                lfo_ph[t] -= fm_floorf(lfo_ph[t]);
                lfo_hold[t] = (float)(rand32() & 0xFFFFu) * (2.0f / 65535.0f) - 1.0f;      /* a new random level */
            }
            v = lfo_shape(shape, lfo_ph[t], &lfo_hold[t]);
            eff = (float)c->par[tgt] + v * (float)depth * (1.0f / 127.0f) * max * 0.5f;
            e = (int)(eff + 0.5f);
            e = e < 0 ? 0 : e > (int)max ? (int)max : e;
            if (e != lfo_sent[t] || lfo_tgt[t] != tgt) {
                eng_set(t, tgt, e);
                lfo_sent[t] = (int16_t)e;
                lfo_tgt[t] = (int16_t)tgt;
            }
        }
    }
}

static float bus[2][256];                           /* the sends: FX 1 and FX 2 */

/* FX 1 and FX 2 each hold a type; the reverb and the delay have one buffer each, so if both slots pick the same one the second is off */
static int slot_type(int i) { return proj.set.fxtype[i] < NMX ? proj.set.fxtype[i] : 0; }
static int slot_on(int i)
{
    if (!proj.set.fxamt[i])
        return 0;
    return !(i == 1 && MX_STATEFUL(slot_type(1)) && slot_type(1) == slot_type(0) && proj.set.fxamt[0]);
}

static void mix(float *l, float *r, int n)
{
    int t, i, k;
    int send_on[2], dj_on = dj_active(proj.set.dj);
    send_on[0] = slot_on(0);
    send_on[1] = slot_on(1);
    memset(bus, 0, sizeof bus);
    for (t = 0; t < NTRK; t++) {
        float *b = trk_buf + t * 256;
        float g = (float)proj.set.level[t] * (1.0f / 127.0f), pk = 0.0f;
        float s0 = send_on[0] ? (float)proj.set.send[0][t] * (1.0f / 127.0f) : 0.0f;
        float s1 = send_on[1] ? (float)proj.set.send[1][t] * (1.0f / 127.0f) : 0.0f;
        if (muted[t])
            continue;
        if (dk_scat[t])
            sc_run(&sc_t_[t], b, n, spl);
        for (i = 0; i < n; i++) {
            float v = b[i] * g;
            l[i] += v;
            bus[0][i] += v * s0;
            bus[1][i] += v * s1;
            if (fm_fabsf(v) > pk)
                pk = fm_fabsf(v);
        }
        {
            uint32_t qk = (uint32_t)(fm_minf(pk, 1.0f) * 32767.0f);
            if (qk > dk_peak[t])
                dk_peak[t] = (uint16_t)qk;
        }
    }
    for (k = 0; k < 2; k++) {                               /* FX 1, FX 2: the bus through the effect, back into the mix */
        if (send_on[k]) {
            float *w = bus[k];
            mx_run(slot_type(k), &fx_sl[k], w, n, proj.set.fxamt[k], proj.set.fxpar[k], spl);
            for (i = 0; i < n; i++)
                l[i] += w[i];
        } else if (!(send_on[k ^ 1] && slot_type(k ^ 1) == slot_type(k)))   /* (not while the other slot is running the same buffer) */
            mx_idle(slot_type(k));
    }
    if (dk_scat_g)
        sc_run(&sc_g, l, n, spl);                 /* the mix's scatter */
    if (dj_on)                                    /* the DJ filter, on the whole mix */
        dj_run(&dj_mix, l, n, proj.set.dj);
    memcpy(r, l, (unsigned)n * sizeof(float));
}

void engine_init(void)
{
    int t;
    eng_init();
    for (t = 0; t < NTRK; t++) {
        sc_ring_of[t] = -1;
        lfo_sent[t] = -1;
        lfo_mode[t] = -1;
    }
    sc_g_ring = -1;
    master_init(&mst);
    {
        int i;
        for (i = 0; i < master_nparams(); i++)          /* every pot to its power-on value */
            master_set(&mst, i, master_param(i)->def);
    }
    set_bpm(proj.set.bpm_x10 ? proj.set.bpm_x10 : 1200);
    for (t = 0; t < NTRK; t++) {
        queued[t] = 0xFF;
        dk_queued[t] = 0xFF;
        load_clip(t);                                   /* the clip store_load() put in act[] */
    }
}

static const float out_scale = 4194303.0f, out_lim = 2.0f;

void engine_render(int32_t *out_lr, uint32_t n)
{
    static float L[256], R[256];
    float vol;
    if (proj.set.knob[0].kind == KS_VOL)             /* MASTER is the volume: the pot, as it always was */
        vol = (float)plat_master() * (1.0f / 4096.0f);
    else {                                           /* else the volume is a number (GLO, or another knob) */
        float m = (float)proj.set.master_vol * (1.0f / 127.0f);
        vol = m * m;
    }
    int32_t left, off = 0;
    uint32_t i;
    if (n > 256u)
        n = 256u;
    left = (int32_t)n;
    drain();
    /* the block is cut at every step and every gate end, so a note starts on its own sample; and at 32 samples, so the LFO runs */
    while (left > 0) {
        int32_t k;
        if (playing_ && acc <= 0.0f) {
            tick_all();
            acc += tick_spl;
        }
        k = next_event(left);
        if (k > LFO_CHUNK)
            k = LFO_CHUNK;
        lfo_update((int)k);
        eng_render_all(trk_buf, off, k);
        if (playing_) {
            int t;
            acc -= (float)k;
            for (t = 0; t < NTRK; t++)
                if (gate_left[t] > 0) {
                    gate_left[t] -= k;
                    if (gate_left[t] <= 0) {
                        gate_left[t] = 0;
                        eng_off(t);
                    }
                }
        }
        off += k;
        left -= k;
    }
    {                                               /* how far into its step each track is (REC rounds with it) */
        int t;
        for (t = 0; t < NTRK; t++) {
            float el = (float)gtick - (float)last_tick[t] - acc / tick_spl, ph = el / (float)(cur_int[t] ? cur_int[t] : 1);
            dk_tphase[t] = (uint8_t)(playing_ ? (ph < 0.0f ? 0.0f : ph > 1.0f ? 255.0f : ph * 255.0f) : 0.0f);
        }
    }
    memset(L, 0, sizeof L);
    mix(L, R, (int)n);
    {   /* the master compressor: always on, its pots are the settings' (GLO); applied when one changes */
        static uint8_t last[6] = {255, 255, 255, 255, 255, 255};
        static const uint8_t ID[6] = {MST_THRESH, MST_RATIO, MST_ATTACK, MST_RELEASE, MST_MAKEUP, MST_MIX};
        int j;
        for (j = 0; j < 6; j++)
            if (proj.set.comp[j] != last[j]) {
                last[j] = proj.set.comp[j];
                master_set(&mst, ID[j], last[j]);
            }
    }
    master_process_st(&mst, L, R, (int)n, vol);
    for (i = 0; i < n; i++) {
        float l = L[i], r = R[i];
        l = l > out_lim ? out_lim : l < -out_lim ? -out_lim : l;
        r = r > out_lim ? out_lim : r < -out_lim ? -out_lim : r;
        out_lr[2u * i] = (int32_t)(l * out_scale);
        out_lr[2u * i + 1u] = (int32_t)(r * out_scale);
    }
}

/* the platform reports each block's cost (device: from the ISR); kept for the UI and, later, a guard */
volatile uint32_t dk_load_pct, dk_load_peak;
void engine_load(uint32_t pct, uint32_t frames)
{
    (void)frames;
    dk_load_pct = pct;
    if (pct > dk_load_peak)
        dk_load_peak = pct;
}
