/* SPDX-License-Identifier: GPL-3.0-only */
/* ACID: see acid.h. A float port of zic_303.cpp (zicCycle 303). */
#include "../tribe/mem.h"
#include "acid.h"
#include "fastmath.h"

static const char *const type_names[] = { "Soft", "Hard", "Fold" };

static const x0x_param_t params[ACID_NPARAMS] = {
    { "Cutoff", 127, 64, 0 },        /* 40 Hz .. 12 kHz */
    { "Reso", 127, 64, 0 },          /* 0 .. 4.2 (self-oscillation) */
    { "Color", 127, 42, 0 },         /* tri, saw, square, distorted; 42 = saw */
    { "Accent", 127, 100, 0 },       /* an accented note: filter envelope x(1 + a/2), louder by the same */
    { "Decay", 127, 64, 0 },         /* time constant 10 ms * 2^(pot/16): 160 ms at 64 */
    { "EnvMod", 127, 64, 0 },        /* filter envelope depth, 0 .. 60 semitones */
    { "P.Dpth", 127, 64, 0 },        /* pitch envelope: 64 = none, +-24 semitones */
    { "Slide", 127, 74, 0 },         /* 5 .. 300 ms; 74 = 55 ms (the Cycles' glide) */
    { "Tune", 127, 64, 0 },          /* +-1.5 semitones */
    { "Drive", 127, 0, 0 },          /* gain into the filter, 1 .. 8; 0 = off */
    { "Dist", 127, 0, 0 },           /* the distortion after the filter, gain 1 .. 16; 0 = off */
    { "DrvTyp", 2, 0, type_names },  /* what the distortion does */
};

int acid_nparams(void) { return ACID_NPARAMS; }
const x0x_param_t *acid_param(int i) { return (i >= 0 && i < ACID_NPARAMS) ? &params[i] : 0; }
int acid_get(const acid_t *b, int i) { return (i >= 0 && i < ACID_NPARAMS) ? b->pot[i] : 0; }

/* 1 - exp(-1/n): the per-step gain of a one pole of n steps; exp(-1/n) is the decay multiplier */
static float decay_mul(float seconds, float step_s) { return fm_expf(-step_s / seconds); }

void acid_set(acid_t *b, int i, int v)
{
    float n;
    if (i < 0 || i >= ACID_NPARAMS)
        return;
    if (v < 0)
        v = 0;
    if (v > params[i].max)
        v = params[i].max;
    b->pot[i] = (uint8_t)v;
    n = (float)v * (1.0f / 127.0f);
    switch (i) {
    case ACID_CUTOFF: b->cut_st = -43.0f + 100.0f * n; break;
    case ACID_RESO: b->reso_k = 4.2f * n; break;
    case ACID_COLOR: b->color = n; break;
    case ACID_ACCENT: b->acc_amt = n; break;
    case ACID_DECAY: {
        float tau = 0.010f * fm_exp2f((float)v * 0.0625f);       /* 10 ms .. 2.4 s */
        b->amp_c = decay_mul(tau, 1.0f / ACID_SR);
        b->fenv_c = decay_mul(tau * 1.5f + 0.060f, (float)ACID_CTRL / ACID_SR);
        break;
    }
    case ACID_ENVMOD: b->env_st = 60.0f * n; break;
    case ACID_PDEPTH: b->pdepth_st = (float)(v - 64) * (24.0f / 64.0f); break;
    case ACID_SLIDE: b->glide_c = 1.0f - decay_mul(0.005f * fm_exp2f(n * 5.9f), (float)ACID_CTRL / ACID_SR); break;
    case ACID_TUNE: b->tune_st = (float)(v - 64) * (1.5f / 64.0f); break;
    case ACID_DRIVE: b->drive_g = 1.0f + 7.0f * n * n; break;
    case ACID_DIST:
        b->dist_g = 1.0f + 15.0f * n * n;
        b->dist_wet = v >= 16 ? 1.0f : (float)v * (1.0f / 16.0f);
        b->dist_norm = 1.0f / fm_tanhf(b->dist_g);
        break;
    case ACID_DISTTYPE: b->dist_type = v; break;
    }
}

void acid_init(acid_t *b)
{
    int i;
    memset(b, 0, sizeof *b);
    b->idle = 1;
    b->rel_c = decay_mul(0.004f, 1.0f / ACID_SR);
    b->penv_c = decay_mul(0.021f, (float)ACID_CTRL / ACID_SR);
    for (i = 0; i < ACID_NPARAMS; i++)
        acid_set(b, i, params[i].def);
}

void acid_note_on(acid_t *b, int note, int accent, int slide)
{
    int legato = slide && b->gate && !b->idle;
    if (note < 0)
        note = 0;
    if (note > 127)
        note = 127;
    b->target_st = (float)(note - 69) + b->tune_st;
    b->accent = accent != 0;
    if (!legato) {
        if (b->idle)
            b->s1 = b->s2 = b->s3 = b->s4 = 0.0f;
        b->cur_st = b->target_st;
        b->penv = b->pdepth_st;
        b->fenv = 1.0f;
        b->phase = 0.0f;
        b->cc = 0;                                  /* a new control step, without interpolation from the old pitch */
        b->snap = 1;
    }
    b->att_step = (1.0f - b->amp) * (1.0f / (0.002f * ACID_SR));   /* 2 ms attack, from where the envelope is */
    if (b->att_step < 1e-6f)
        b->att_step = 1e-6f;
    b->gate = 1;
    b->idle = 0;
}

void acid_note_off(acid_t *b) { b->gate = 0; }
void acid_all_off(acid_t *b) { b->gate = 0; }

/* polyBLEP residual of a unit (2-high) step at phase 0 */
static inline float blep(float t, float dt)
{
    float x;
    if (t < dt) {
        x = t / dt;
        return x + x - x * x - 1.0f;
    }
    if (t > 1.0f - dt) {
        x = (t - 1.0f) / dt;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

/* the waveform at phase ph (turns), increment dt, colour c in 0..1 */
static inline float osc(float ph, float dt, float c)
{
    float saw = 2.0f * ph - 1.0f - blep(ph, dt);
    float ph2 = ph + 0.5f, a, sq, p0, p1, t;
    if (ph2 >= 1.0f)
        ph2 -= 1.0f;
    sq = (ph < 0.5f ? 1.0f : -1.0f) + blep(ph, dt) - blep(ph2, dt);
    a = ph - 0.5f;
    if (c < 1.0f / 3.0f) {
        float tri = 1.0f - 4.0f * fm_fabsf(a);
        p0 = tri;
        p1 = saw;
        t = c * 3.0f;
    } else if (c < 2.0f / 3.0f) {
        p0 = saw;
        p1 = sq;
        t = (c - 1.0f / 3.0f) * 3.0f;
    } else {
        p0 = sq;
        p1 = fm_tanhf(2.0f * saw + sq);
        t = (c - 2.0f / 3.0f) * 3.0f;
    }
    if (t > 1.0f)
        t = 1.0f;
    return p0 + (p1 - p0) * t;
}

/* a control step: the envelopes advance, and the pitch, the filter and its coefficients are set for the
 * next ACID_CTRL samples (the pitch increment is interpolated from the last step) */
static void control(acid_t *b)
{
    float cur, fc, x, sn, cs, g, g2, k, d, cut;
    b->cur_st += (b->target_st - b->cur_st) * b->glide_c;
    b->penv *= b->penv_c;
    cur = b->cur_st + b->penv;
    b->inc0 = b->snap ? 0.0f : b->inc1;
    b->inc1 = 440.0f * fm_exp2f(cur * (1.0f / 12.0f)) * (1.0f / ACID_SR);
    if (b->inc1 > 0.45f)
        b->inc1 = 0.45f;
    if (b->snap) {
        b->inc0 = b->inc1;
        b->snap = 0;
    }
    b->fenv *= b->fenv_c;
    cut = b->cut_st + b->fenv * b->env_st * (b->accent ? 1.0f + 0.5f * b->acc_amt : 1.0f);
    if (cut > 60.0f)
        cut = 60.0f;
    fc = 440.0f * fm_exp2f(cut * (1.0f / 12.0f));
    x = fc * (0.5f / ACID_SR);                                    /* pi fc / fs, in turns of 2 pi */
    sn = fm_sin_turns(x);
    cs = fm_sin_turns(x + 0.25f);
    g = sn / (sn + cs);                                           /* g / (1 + g) = sin / (sin + cos) */
    g2 = g * g;
    k = b->reso_k;
    d = 1.0f + k * g2 * g2;
    b->G = g;
    b->G2 = g2;
    b->G3 = g2 * g;
    b->G4 = g2 * g2;
    b->kk = k;
    b->rdiv = 1.0f / d;
    b->gain = b->accent ? 1.0f + 0.5f * b->acc_amt : 1.0f;
}

void acid_render(acid_t *b, float *out, int n)
{
    int i;
    float inc, step;
    if (b->idle) {
        memset(out, 0, (unsigned)n * sizeof(float));
        return;
    }
    for (i = 0; i < n; i++) {
        float u, y4, x, xs, v, y, ym, G, omg, sg1, sg2, sg3, sg4, sum, amp;
        if (b->cc == 0) {
            control(b);
            b->cc = ACID_CTRL;
        }
        b->cc--;
        step = (float)(ACID_CTRL - b->cc) * (1.0f / (float)ACID_CTRL);
        inc = b->inc0 + (b->inc1 - b->inc0) * step;
        b->phase += inc;
        if (b->phase >= 1.0f)
            b->phase -= 1.0f;
        u = osc(b->phase, inc, b->color) * (0.8f * b->drive_g);

        G = b->G;
        omg = 1.0f - G;
        sg1 = omg * b->s1;
        sg2 = omg * b->s2;
        sg3 = omg * b->s3;
        sg4 = omg * b->s4;
        sum = b->G3 * sg1 + b->G2 * sg2 + G * sg3 + sg4;
        y4 = (b->G4 * u + sum) * b->rdiv;                         /* the feedback loop, solved exactly */
        x = u - b->kk * y4;
        xs = fm_tanhf(x);
        v = G * (xs - b->s1); y = v + b->s1; b->s1 = y + v;
        v = G * (y - b->s2);  y = v + b->s2; b->s2 = y + v;
        v = G * (y - b->s3);  y = v + b->s3; b->s3 = y + v;
        v = G * (y - b->s4);  y = v + b->s4; b->s4 = y + v;
        b->s1 = fm_clampf(b->s1, -8.0f, 8.0f);
        b->s2 = fm_clampf(b->s2, -8.0f, 8.0f);
        b->s3 = fm_clampf(b->s3, -8.0f, 8.0f);
        b->s4 = fm_clampf(b->s4, -8.0f, 8.0f);
        ym = fm_clampf(y + y * b->kk * 0.5f, -8.0f, 8.0f);        /* the resonance eats the bass: half of it back */

        amp = b->amp;
        if (b->att_step > 0.0f) {                                 /* attack, then the exponential decay */
            amp += b->att_step;
            if (amp >= 1.0f) {
                amp = 1.0f;
                b->att_step = 0.0f;
            }
        } else {
            amp *= b->gate ? b->amp_c : (b->amp_c < b->rel_c ? b->amp_c : b->rel_c);
        }
        b->amp = amp;
        ym = fm_tanhf(ym * 2.0f * b->gain);
        if (b->dist_wet > 0.0f) {                                 /* the distortion: a fade from the clean signal over the first pots */
            float w;
            switch (b->dist_type) {
            case 1: w = fm_clampf(ym * b->dist_g, -1.0f, 1.0f); break;
            case 2: w = fm_sinf(ym * b->dist_g * (FM_PI * 0.5f)); break;
            default: w = fm_tanhf(ym * b->dist_g) * b->dist_norm; break;
            }
            ym += (w - ym) * b->dist_wet;
        }
        out[i] = 0.5f * amp * ym;
        if (amp < 1e-4f && b->att_step == 0.0f && !b->gate) {
            memset(out + i + 1, 0, (unsigned)(n - i - 1) * sizeof(float));
            b->idle = 1;
            b->amp = 0.0f;
            return;
        }
    }
}
