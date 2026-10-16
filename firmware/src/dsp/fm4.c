/* SPDX-License-Identifier: GPL-3.0-only */
/* FM4: see fm4.h. The algorithms (ALG_MOD, ALG_X, ALG_Y) are read from the Digitone manual's appendix A.3 (p. 90): the
 * bit k of a mask is operator k (0 C, 1 A, 2 B1, 3 B2). Where a diagram was hard to read, the routing is a best reading
 * and is marked so in the report. */
#include "../tribe/mem.h"
#include "fm4.h"
#include "fastmath.h"

/* ALG_MOD[a][j]: the operators that modulate operator j in algorithm a + 1 */
static const uint8_t ALG_MOD[8][FM4_OPS] = {
    {2, 0, 10, 0},    /* 1: A > C, B2 > B1, A > B1 */
    {2, 0, 8, 0},     /* 2: A > C, B2 > B1 */
    {2, 0, 8, 2},     /* 3: A > C, A > B2 > B1 */
    {2, 4, 8, 0},     /* 4: B2 > B1 > A > C */
    {8, 0, 2, 0},     /* 5: A > B1, B2 > C */
    {0, 0, 8, 2},     /* 6: A > B2 > B1 */
    {2, 0, 0, 0},     /* 7: A > C, B1 and B2 alone */
    {0, 0, 0, 0},     /* 8: all four in parallel */
};
static const uint8_t ALG_X[8] = {1, 1, 1, 1, 1, 1, 1, 3};     /* the carriers of output X */
static const uint8_t ALG_Y[8] = {4, 4, 4, 4, 4, 4, 12, 12};   /* the carriers of output Y */

/* the ratio tables: T16 for A and for B2, T8 for B1 (the B pair's two steps) */
static const float T16[16] = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
static const float T8[8] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 12.0f, 16.0f};

/* HARM: the partials of the four presets (the manual's p. 93: sine, saw, square, bell), morphed between */
static const float PRESET[4][FM4_PARTIALS] = {
    {1.0f, 0.0f, 0.0f, 0.0f},            /* sine */
    {1.0f, 0.5f, 0.33f, 0.25f},          /* saw */
    {1.0f, 0.0f, 0.33f, 0.0f},           /* square: the odd partials */
    {1.0f, 0.25f, 0.5f, 0.2f},           /* bell */
};

static const char *const alg_names[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
static const char *const c_names[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"};
static const char *const a_names[] = {"0.25", "0.5", "0.75", "1", "1.25", "1.5", "1.75", "2", "2.5", "3", "3.5", "4", "5", "6", "7", "8"};
static const char *const trig_names[] = {"Gate", "Trig"};

static const x0x_param_t params[FM4_NPARAMS] = {
    { "Alg", 7, 0, alg_names },
    { "Ratio C", 15, 0, c_names },           /* 1 .. 16, integers */
    { "Ratio A", 15, 3, a_names },           /* 0.25 .. 8, with the in-between values */
    { "Ratio B", 127, 39, 0 },               /* the B pair: B2 steps through 16 values, then B1 steps */
    { "Harm", 127, 64, 0 },                  /* 64 = a sine; below: C; above: A and B1 */
    { "Detune", 127, 0, 0 },                 /* A and B2 detuned, 0 .. 38 cents */
    { "Feedbk", 127, 0, 0 },                 /* operator A's self-modulation */
    { "Mix", 127, 64, 0 },                   /* X (0) .. Y (127) */
    { "Lev A", 127, 100, 0 },                /* A's modulation index (times its envelope) */
    { "Atk A", 127, 0, 0 },
    { "Dec A", 127, 60, 0 },
    { "End A", 127, 0, 0 },                  /* the level the decay ends at */
    { "Lev B", 127, 64, 0 },                 /* B1 and B2's modulation: a crossfade of the two, per the manual's graph */
    { "Atk B", 127, 0, 0 },
    { "Dec B", 127, 50, 0 },
    { "End B", 127, 0, 0 },
    { "Attack", 127, 0, 0 },                 /* the amp envelope, 1 ms .. 2 s */
    { "Decay", 127, 64, 0 },
    { "Sustain", 127, 100, 0 },
    { "Release", 127, 50, 0 },
    { "Cutoff", 127, 110, 0 },               /* 40 Hz .. 12 kHz */
    { "Reso", 127, 0, 0 },
    { "Drive", 127, 0, 0 },                  /* overdrive before the filter; 0 = off */
    { "Trig", 1, 0, trig_names },
};

int fm4_nparams(void) { return FM4_NPARAMS; }
const x0x_param_t *fm4_param(int i) { return (i >= 0 && i < FM4_NPARAMS) ? &params[i] : 0; }
int fm4_get(const fm4_t *b, int i) { return (i >= 0 && i < FM4_NPARAMS) ? b->pot[i] : 0; }

/* the multiplier of a one-pole decay that falls by e over `seconds` */
static float decay_mul(float seconds, float step_s) { return fm_expf(-step_s / seconds); }

/* the order the operators are computed in: each after the operators that modulate it (a cycle keeps the listed order) */
static void build_order(fm4_t *b)
{
    const uint8_t *mod = ALG_MOD[b->alg];
    int done[FM4_OPS] = {0, 0, 0, 0}, n = 0, pass, j, k, ok;
    for (pass = 0; pass < FM4_OPS; pass++)
        for (j = 0; j < FM4_OPS; j++) {
            if (done[j])
                continue;
            ok = 1;
            for (k = 0; k < FM4_OPS; k++)
                if (((mod[j] >> k) & 1u) && !done[k])
                    ok = 0;
            if (ok) {
                b->ord[n++] = (uint8_t)j;
                done[j] = 1;
            }
        }
    for (j = 0; j < FM4_OPS; j++)
        if (!done[j]) {
            b->ord[n++] = (uint8_t)j;
            done[j] = 1;
        }
}

/* HARM: the preset amplitudes, morphed by x in 0 .. 3 (sine, saw, square, bell), normalised to unit energy */
static void morph(float *w, float x)
{
    int a = (int)x, p;
    float f = x - (float)a, e = 0.0f;
    if (a >= 3) {
        a = 2;
        f = 1.0f;
    }
    for (p = 0; p < FM4_PARTIALS; p++) {
        w[p] = PRESET[a][p] * (1.0f - f) + PRESET[a + 1][p] * f;
        e += w[p] * w[p];
    }
    e = fm_sqrtf(e);
    for (p = 0; p < FM4_PARTIALS; p++)
        w[p] = e > 0.0f ? w[p] / e : 0.0f;
}

/* HARM: below 64, C takes the morph; above, A and B1 do; B2 stays a sine. */
static void set_harm(fm4_t *b, int v)
{
    int d = v - 64, j, p, last;
    float w[FM4_PARTIALS];
    for (j = 0; j < FM4_OPS; j++) {
        if (j == 0 ? d < 0 : (j <= 2 && d > 0))
            morph(w, d < 0 ? (float)(-d) * (3.0f / 64.0f) : (float)d * (3.0f / 63.0f));
        else
            morph(w, 0.0f);                                /* the sine */
        last = 0;
        for (p = 0; p < FM4_PARTIALS; p++) {
            b->part[j][p] = w[p];
            if (w[p] != 0.0f)
                last = p;
        }
        b->nparts[j] = (uint8_t)(last + 1);
    }
}

void fm4_set(fm4_t *b, int i, int v)
{
    float n;
    if (i < 0 || i >= FM4_NPARAMS)
        return;
    if (v < 0)
        v = 0;
    if (v > params[i].max)
        v = params[i].max;
    b->pot[i] = (uint8_t)v;
    n = (float)v * (1.0f / 127.0f);
    switch (i) {
    case FM4_ALG: b->alg = v; build_order(b); break;
    case FM4_RATC: b->ratio[0] = (float)(v + 1); break;
    case FM4_RATA: b->ratio[1] = T16[v]; break;
    case FM4_RATB: b->ratio[2] = T8[v >> 4]; b->ratio[3] = T16[v & 15]; break;
    case FM4_HARM: set_harm(b, v); break;
    case FM4_DTUN: b->dt_factor = fm_exp2f(v * 0.3f * (1.0f / 1200.0f)); break;
    case FM4_FDBK: b->fb = n * n; break;
    case FM4_MIX: b->mix = n; break;
    case FM4_LEVA: b->idx[1] = 1.5f * n * n; break;
    case FM4_LEVB: {                                      /* the manual's graph: B1 rises to 43, falls to 85, rises; B2 rises from 43 to 85 */
        float b1, b2;
        if (v <= 43)
            b1 = (float)v / 43.0f;
        else if (v <= 85)
            b1 = (85.0f - (float)v) / 42.0f;
        else
            b1 = ((float)v - 85.0f) / 42.0f;
        b2 = v < 43 ? 0.0f : v >= 85 ? 1.0f : ((float)v - 43.0f) / 42.0f;
        b->idx[2] = 1.5f * b1;
        b->idx[3] = 1.5f * b2;
        break;
    }
    case FM4_ATKA: b->atk_step[0] = 1.0f / (0.001f * fm_exp2f(n * 10.0f) * FM4_SR); break;
    case FM4_DECA: b->dec_c[0] = decay_mul(0.005f * fm_exp2f(n * 9.0f), 1.0f / FM4_SR); break;
    case FM4_ENDA: b->endl[0] = n; break;
    case FM4_ATKB: b->atk_step[1] = 1.0f / (0.001f * fm_exp2f(n * 10.0f) * FM4_SR); break;
    case FM4_DECB: b->dec_c[1] = decay_mul(0.005f * fm_exp2f(n * 9.0f), 1.0f / FM4_SR); break;
    case FM4_ENDB: b->endl[1] = n; break;
    case FM4_ATK: b->amp_att_step = 1.0f / (0.001f * fm_exp2f(n * 11.0f) * FM4_SR); break;
    case FM4_DEC: b->amp_dec_c = decay_mul(0.005f * fm_exp2f(n * 9.0f), 1.0f / FM4_SR); break;
    case FM4_SUS: b->sustain = n; break;
    case FM4_REL: b->rel_c = decay_mul(0.005f * fm_exp2f(n * 9.2f), 1.0f / FM4_SR); break;
    case FM4_CUT: b->cut_st = -43.0f + 100.0f * n; break;
    case FM4_RESO: b->reso_k = 2.0f - 1.9f * n; break;     /* 2 = no resonance, 0.1 = a strong peak */
    case FM4_DRIVE:
        b->drive_on = v > 0;
        b->drive_g = 1.0f + 7.0f * n * n;
        b->drive_norm = 1.0f / fm_tanhf(b->drive_g);
        break;
    case FM4_TRIG: b->trig = v; break;
    }
}

void fm4_init(fm4_t *b)
{
    int i;
    memset(b, 0, sizeof *b);
    b->idle = 1;
    b->idx[0] = 0.5f;                                     /* C's modulation index, fixed */
    b->dt_factor = 1.0f;
    for (i = 0; i < FM4_NPARAMS; i++)
        fm4_set(b, i, params[i].def);
}

void fm4_note_on(fm4_t *b, int note, int slide)
{
    int legato, j;
    if (note < 0)
        note = 0;
    if (note > 127)
        note = 127;
    legato = !b->idle && b->gate && slide;                 /* over a sounding note: the pitch changes, the envelopes go on */
    b->target_st = (float)(note - 69);
    if (!legato) {                                         /* a new note: the envelopes and the phases start again */
        b->ostage[0] = b->ostage[1] = 0;
        b->oenv[0] = b->oenv[1] = 0.0f;
        b->amp_stage = 0;
        for (j = 0; j < FM4_OPS; j++)
            b->ph[j] = 0.0f;
        if (b->idle) {
            b->ic1 = b->ic2 = 0.0f;
            for (j = 0; j < FM4_OPS; j++)
                b->s[j] = 0.0f;
        }
    }
    b->cc = 0;                                             /* the pitch and the filter are set on the next sample */
    b->gate = 1;
    b->idle = 0;
}

void fm4_note_off(fm4_t *b)
{
    b->gate = 0;
    b->amp_stage = 2;
}
void fm4_all_off(fm4_t *b) { fm4_note_off(b); }

/* a control step: the operators' increments and the filter's coefficients for the next FM4_CTRL samples */
static void control(fm4_t *b)
{
    float base, cut, fc, x, sn, cs, g, k, inc;
    base = 440.0f * fm_exp2f(b->target_st * (1.0f / 12.0f)) * (1.0f / FM4_SR);
    inc = base * b->ratio[0];
    b->inc[0] = inc > 0.45f ? 0.45f : inc;
    inc = base * b->ratio[1] * b->dt_factor;
    b->inc[1] = inc > 0.45f ? 0.45f : inc;
    inc = base * b->ratio[2];
    b->inc[2] = inc > 0.45f ? 0.45f : inc;
    inc = base * b->ratio[3] * b->dt_factor;
    b->inc[3] = inc > 0.45f ? 0.45f : inc;

    cut = b->cut_st > 60.0f ? 60.0f : b->cut_st;
    fc = 440.0f * fm_exp2f(cut * (1.0f / 12.0f));
    x = fc * (0.5f / FM4_SR);                              /* pi fc / fs, in turns of 2 pi */
    if (x > 0.2f)
        x = 0.2f;
    sn = fm_sin_turns(x);
    cs = fm_sin_turns(x + 0.25f);
    g = sn / cs;                                           /* tan(pi fc / fs) */
    k = b->reso_k;
    b->g = g;
    b->a1 = 1.0f / (1.0f + g * (g + k));
    b->a2 = g * b->a1;
    b->a3 = g * b->a2;
}

/* one operator's raw output at phase x (turns): its partials, the first nparts[j] of them */
static float partial(const fm4_t *b, int j, float x)
{
    float s = 0.0f;
    int p;
    for (p = 0; p < b->nparts[j]; p++)
        if (b->part[j][p] != 0.0f)
            s += b->part[j][p] * fm_sin_turns((float)(p + 1) * x);
    return s;
}

/* an envelope step: A (k = 0) or B (k = 1). Attack to 1, then a decay to the end level; for gate the decay waits for the
 * key to be let go, and holds at 1 meanwhile */
static void env_step(fm4_t *b, int k)
{
    float *e = &b->oenv[k];
    if (b->ostage[k] == 0) {
        *e += b->atk_step[k];
        if (*e >= 1.0f) {
            *e = 1.0f;
            b->ostage[k] = 1;
        }
        return;
    }
    if (b->ostage[k] == 1) {
        if (b->trig)
            *e = b->endl[k] + (*e - b->endl[k]) * b->dec_c[k];
        else if (!b->gate)
            b->ostage[k] = 2;
        return;
    }
    *e = b->endl[k] + (*e - b->endl[k]) * b->dec_c[k];
}

void fm4_render(fm4_t *b, float *out, int n)
{
    int i, k, j, m;
    const uint8_t *mod = ALG_MOD[b->alg];
    unsigned xm = ALG_X[b->alg], ym = ALG_Y[b->alg];
    if (b->idle) {
        memset(out, 0, (unsigned)n * sizeof(float));
        return;
    }
    for (i = 0; i < n; i++) {
        float a[FM4_OPS], xs = 0.0f, ys = 0.0f, pm, y, u, v3, v1, v2, lp, amp;
        if (b->cc == 0) {
            control(b);
            b->cc = FM4_CTRL;
        }
        b->cc--;

        env_step(b, 0);
        env_step(b, 1);
        a[0] = 1.0f;                                       /* C: no envelope */
        a[1] = b->oenv[0];
        a[2] = b->oenv[1];
        a[3] = b->oenv[1];

        for (k = 0; k < FM4_OPS; k++) {                    /* in the order that makes each modulator ready first */
            j = b->ord[k];
            pm = 0.0f;
            for (m = 0; m < FM4_OPS; m++)
                if ((mod[j] >> m) & 1u)
                    pm += b->idx[m] * a[m] * b->s[m];
            if (j == 1)
                pm += b->fb * b->s[1];                     /* A's feedback: its own last output */
            b->ph[j] += b->inc[j];
            if (b->ph[j] >= 1.0f)
                b->ph[j] -= 1.0f;
            b->s[j] = partial(b, j, b->ph[j] + pm);
            y = b->s[j] * a[j];
            if ((xm >> j) & 1u)
                xs += y;
            if ((ym >> j) & 1u)
                ys += y;
        }

        u = 0.5f * ((1.0f - b->mix) * xs + b->mix * ys);
        if (b->drive_on)
            u = fm_tanhf(u * b->drive_g) * b->drive_norm;

        /* the state-variable low-pass (zero-delay), two integrators */
        v3 = u - b->ic2;
        v1 = b->a1 * b->ic1 + b->a2 * v3;
        v2 = b->ic2 + b->a2 * b->ic1 + b->a3 * v3;
        b->ic1 = fm_clampf(2.0f * v1 - b->ic1, -8.0f, 8.0f);
        b->ic2 = fm_clampf(2.0f * v2 - b->ic2, -8.0f, 8.0f);
        lp = v2;

        /* the amp envelope: attack, decay to the sustain, release */
        amp = b->amp;
        if (b->amp_stage == 0) {
            amp += b->amp_att_step;
            if (amp >= 1.0f) {
                amp = 1.0f;
                b->amp_stage = 1;
            }
        } else if (b->amp_stage == 1) {
            amp = b->sustain + (amp - b->sustain) * b->amp_dec_c;
        } else {
            amp *= b->rel_c;
        }
        b->amp = amp;

        out[i] = 0.5f * amp * fm_tanhf(lp);
        if (b->amp_stage == 2 && amp < 1e-4f) {
            memset(out + i + 1, 0, (unsigned)(n - i - 1) * sizeof(float));
            b->idle = 1;
            b->amp = 0.0f;
            return;
        }
    }
}
