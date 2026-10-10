/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE audio engine: the ten tracks' sequencer, their engines, the mixer and the master.
 * engine_render() runs in the audio ISR and owns every engine; the main loop reaches it only
 * through the command queue (dk_*), and reads the playheads and meters it publishes. */
#include <string.h>
#include "tribe.h"
#include "../app/plat.h"
#include "../dsp/master.h"
#include "../dsp/fastmath.h"

dproj_t proj X0X_POOL;

volatile uint8_t dk_bound[NTRK];
volatile uint8_t dk_pos[NTRK], dk_clip[NTRK], dk_queued[NTRK], dk_muted[NTRK], dk_playing;
volatile uint16_t dk_peak[NTRK];
volatile uint8_t dk_hit[NTRK];

/* ------------------------------------------------------------ the queue --- */
enum { C_PLAY, C_BPM, C_SET, C_RELOAD, C_LAUNCH, C_MUTE, C_NOTE, C_MSET };
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
    proj.clip[t][dk_clip[t]].par[i] = (uint8_t)v;
    push(C_SET, t, i, v);
}
void dk_engine(int t, int eng)
{
    clip_set_engine(&proj.clip[t][dk_clip[t]], eng);      /* the steps stay */
    push(C_RELOAD, t, 0, 0);
}
void dk_launch(int t, int c, int now) { push(C_LAUNCH, t, c, now); }
void dk_mute(int t, int on) { push(C_MUTE, t, on, 0); }
void dk_note(int t, int note, int vel) { push(C_NOTE, t, note, vel); }
void dk_master_set(int i, int v) { push(C_MSET, 0, i, v); }

/* ------------------------------------------------------------ the state --- */
static master_t mst;
static float trk_buf[NTRK * 256];
static float spl = 11025.0f, acc;               /* samples a step lasts; samples to the next step */
static uint8_t playing_;
static uint8_t pos[NTRK], queued[NTRK], muted[NTRK];
static int32_t gate_left[NTRK];                 /* samples until the gate ends (0 = none) */
static uint32_t rnd = 0x1234567u;

static uint32_t rand32(void)
{
    rnd = rnd * 1664525u + 1013904223u;
    return rnd >> 8;
}

static void set_bpm(int x10)
{
    float bpm = (float)x10 * 0.1f;
    spl = 661500.0f / (bpm < 30.0f ? 30.0f : bpm > 300.0f ? 300.0f : bpm);
}

/* load a clip's engine and pots into the track */
static void load_clip(int t)
{
    const clip_t *c = &proj.clip[t][dk_clip[t]];
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

static void switch_clip(int t, int c)
{
    dk_clip[t] = (uint8_t)c;
    queued[t] = 0xFF;
    dk_queued[t] = 0xFF;
    load_clip(t);
}

static void launch(int t, int c, int now)
{
    if (c == dk_clip[t] && queued[t] == 0xFF)
        return;
    if (now || !playing_) {
        switch_clip(t, c);
        queued[t] = 0xFF;
        dk_queued[t] = 0xFF;
        return;
    }
    queued[t] = (uint8_t)c;
    dk_queued[t] = (uint8_t)c;
}

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
                for (t = 0; t < NTRK; t++)
                    pos[t] = 0;
            } else if (!c.a && playing_) {
                int t;
                playing_ = 0;
                for (t = 0; t < NTRK; t++) {
                    eng_panic(t);
                    gate_left[t] = 0;
                }
            }
            dk_playing = playing_;
            break;
        case C_BPM: set_bpm(c.a); break;
        case C_SET: eng_set(c.trk, c.a, c.b); break;
        case C_RELOAD: load_clip(c.trk); break;
        case C_LAUNCH: launch(c.trk, c.a, c.b); break;
        case C_MUTE:
            muted[c.trk] = (uint8_t)c.a;
            dk_muted[c.trk] = (uint8_t)c.a;
            if (c.a)
                eng_panic(c.trk);
            break;
        case C_NOTE:
            if (c.b > 0) {
                eng_note(c.trk, c.a, c.b, 0);
                dk_hit[c.trk] = (uint8_t)c.b;
            } else
                eng_panic(c.trk);
            break;
        case C_MSET: master_set(&mst, c.a, c.b); break;
        }
        q_r++;
    }
}

/* one step of every track */
static void step_all(void)
{
    int t;
    for (t = 0; t < NTRK; t++) {
        const clip_t *cl;
        const dstep_t *s;
        int p = pos[t], len, hit = 0;
        if (queued[t] != 0xFF) {                        /* a clip waits for the pattern's end */
            int len0 = LEN_OF(&proj.clip[t][dk_clip[t]]);
            if (p >= len0 || p == 0)
                switch_clip(t, queued[t]);
        }
        cl = &proj.clip[t][dk_clip[t]];
        len = LEN_OF(cl);
        if (p >= len)
            p = 0;
        dk_pos[t] = (uint8_t)p;
        s = &cl->st[p];
        if (step_on(cl, p) && eng_bound(t) && (s->prob >= 100 || (int)(rand32() % 100u) < s->prob)) {
            if (!muted[t]) {
                eng_note(t, s->note, s->vel, s->slide);
                hit = 1;
                dk_hit[t] = s->vel;
                if (eng_info(eng_bound(t))->kind == EK_SYNTH)
                    gate_left[t] = (int32_t)((float)(s->len ? s->len : 3) * spl * 0.25f);
            }
        }
        if (!hit && eng_slide_pending(t)) {
            eng_panic(t);
            gate_left[t] = 0;
        }
        pos[t] = (uint8_t)((p + 1) % len);
    }
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

static void mix(float *l, float *r, int n)
{
    int t, i;
    for (t = 0; t < NTRK; t++) {
        const float *b = trk_buf + t * 256;
        float g = (float)proj.set.level[t] * (1.0f / 127.0f), pk = 0.0f;
        if (muted[t])
            continue;
        for (i = 0; i < n; i++) {
            float v = b[i] * g;
            l[i] += v;
            if (fm_fabsf(v) > pk)
                pk = fm_fabsf(v);
        }
        {
            uint32_t qk = (uint32_t)(fm_minf(pk, 1.0f) * 32767.0f);
            if (qk > dk_peak[t])
                dk_peak[t] = (uint16_t)qk;
        }
    }
    memcpy(r, l, (size_t)n * sizeof(float));
}

void engine_init(void)
{
    int t;
    eng_init();
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
        dk_clip[t] = proj.set.cur[t] < NCLIP ? proj.set.cur[t] : 0;
        load_clip(t);
    }
}

static const float out_scale = 4194303.0f, out_lim = 2.0f;

void engine_render(int32_t *out_lr, uint32_t n)
{
    static float L[256], R[256];
    float vol = (float)plat_master() * (1.0f / 4096.0f);
    int32_t left, off = 0;
    uint32_t i;
    if (n > 256u)
        n = 256u;
    left = (int32_t)n;
    drain();
    /* the block is cut at every step and every gate end, so a note starts on its own sample */
    while (left > 0) {
        int32_t k;
        if (playing_ && acc <= 0.0f) {
            step_all();
            acc += spl;
        }
        k = next_event(left);
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
    memset(L, 0, sizeof L);
    mix(L, R, (int)n);
    master_process_st(&mst, L, R, (int)n, vol);
    for (i = 0; i < n; i++) {
        float l = L[i], r = R[i];
        l = l > out_lim ? out_lim : l < -out_lim ? -out_lim : l;
        r = r > out_lim ? out_lim : r < -out_lim ? -out_lim : r;
        out_lr[2u * i] = (int32_t)(l * out_scale);
        out_lr[2u * i + 1u] = (int32_t)(r * out_scale);
    }
}
