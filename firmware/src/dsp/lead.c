/* SPDX-License-Identifier: GPL-3.0-only */
/* LEAD: see lead.h. */
#include "../tribe/mem.h"
#include "lead.h"
#include "fastmath.h"

static const char *const unison_names[] = { "1", "2", "3", "4", "5", "6", "7" };
static const char *const legato_names[] = { "Off", "On" };
static const char *const type_names[] = { "Soft", "Hard", "Fold" };

static const x0x_param_t params[LEAD_NPARAMS] = {
    { "Cutoff", 127, 70, 0 },        /* 40 Hz .. 12 kHz */
    { "Reso", 127, 40, 0 },          /* 0 .. 4.2 */
    { "Color", 127, 0, 0 },          /* saw .. square */
    { "Detune", 127, 56, 0 },        /* 0 .. 60 cents (squared): 56 = 12 cents */
    { "Unison", 6, 4, unison_names },/* 1 .. 7 oscillators */
    { "Sub", 127, 40, 0 },           /* a sine an octave down */
    { "Tune", 127, 64, 0 },          /* +-1.5 semitones */
    { "Accent", 127, 64, 0 },
    { "Legato", 1, 1, legato_names },
    { "Vib", 127, 0, 0 },            /* 0 .. 1 semitone at 5.5 Hz */
    { "Attack", 127, 20, 0 },        /* 1 ms .. 2 s, exponential */
    { "Decay", 127, 64, 0 },         /* 5 ms .. 2.5 s */
    { "Sustain", 127, 110, 0 },      /* the level a held note settles at */
    { "Release", 127, 50, 0 },       /* 5 ms .. 3 s */
    { "EnvMod", 127, 50, 0 },        /* filter envelope depth, 0 .. 60 semitones */
    { "Slide", 127, 60, 0 },         /* glide, 5 ms .. 600 ms */
    { "Drive", 127, 0, 0 },          /* gain into the filter, 1 .. 8; 0 = off */
    { "Dist", 127, 0, 0 },           /* the distortion after the filter, gain 1 .. 16; 0 = off */
    { "Ring", 127, 0, 0 },           /* ring modulator amount; 0 = off */
    { "RingFq", 127, 64, 0 },        /* its sine: 0.5x .. 8x the note; 64 = 2x (an octave up) */
    { "DrvTyp", 2, 0, type_names },
};

int lead_nparams(void) { return LEAD_NPARAMS; }
const x0x_param_t *lead_param(int i) { return (i >= 0 && i < LEAD_NPARAMS) ? &params[i] : 0; }
int lead_get(const lead_t *b, int i) { return (i >= 0 && i < LEAD_NPARAMS) ? b->pot[i] : 0; }

static float decay_mul(float seconds, float step_s) { return fm_expf(-step_s / seconds); }

/* the unison's ratios: the oscillators spread evenly over +-detune cents around the note */
static void set_ratios(lead_t *b)
{
    int i, n = b->nvoices;
    for (i = 0; i < LEAD_VOICES; i++) {
        float x = n > 1 ? ((float)i / (float)(n - 1)) * 2.0f - 1.0f : 0.0f;
        b->ratio[i] = i < n ? fm_exp2f(x * b->detune_ct * (1.0f / 1200.0f)) : 1.0f;
    }
}

void lead_set(lead_t *b, int i, int v)
{
    float n;
    if (i < 0 || i >= LEAD_NPARAMS)
        return;
    if (v < 0)
        v = 0;
    if (v > params[i].max)
        v = params[i].max;
    b->pot[i] = (uint8_t)v;
    n = (float)v * (1.0f / 127.0f);
    switch (i) {
    case LEAD_CUTOFF: b->cut_st = -43.0f + 100.0f * n; break;
    case LEAD_RESO: b->reso_k = 4.2f * n; break;
    case LEAD_COLOR: b->color = n; break;
    case LEAD_DETUNE: b->detune_ct = 60.0f * n * n; set_ratios(b); break;
    case LEAD_UNISON: b->nvoices = v + 1; set_ratios(b); break;
    case LEAD_SUB: b->sub = n; break;
    case LEAD_TUNE: b->tune_st = (float)(v - 64) * (1.5f / 64.0f); break;
    case LEAD_ACCENT: b->acc_amt = n; break;
    case LEAD_LEGATO: b->legato = v; break;
    case LEAD_VIB: b->vib_st = n; break;
    case LEAD_ATTACK: b->att_step = 1.0f / (0.001f * fm_exp2f(n * 11.0f) * LEAD_SR); break;
    case LEAD_DECAY: b->dec_c = decay_mul(0.005f * fm_exp2f(n * 9.0f), 1.0f / LEAD_SR); break;
    case LEAD_SUSTAIN: b->sustain = n; break;
    case LEAD_RELEASE: b->rel_c = decay_mul(0.005f * fm_exp2f(n * 9.2f), 1.0f / LEAD_SR); break;
    case LEAD_ENVMOD: b->env_st = 60.0f * n; break;
    case LEAD_SLIDE: b->glide_c = 1.0f - decay_mul(0.005f * fm_exp2f(n * 6.9f), (float)LEAD_CTRL / LEAD_SR); break;
    case LEAD_DRIVE: b->drive_g = 1.0f + 7.0f * n * n; break;
    case LEAD_DIST:
        b->dist_g = 1.0f + 15.0f * n * n;
        b->dist_wet = v >= 16 ? 1.0f : (float)v * (1.0f / 16.0f);
        b->dist_norm = 1.0f / fm_tanhf(b->dist_g);
        break;
    case LEAD_DISTTYPE: b->dist_type = v; break;
    case LEAD_RING: b->ring_mix = n; break;
    case LEAD_RINGFQ: b->ring_ratio = 0.5f * fm_exp2f(n * 4.0f); break;
    }
}

void lead_init(lead_t *b)
{
    int i;
    memset(b, 0, sizeof *b);
    b->idle = 1;
    for (i = 0; i < LEAD_VOICES; i++) {                    /* free-running phases, spread: no two start alike */
        float p = (float)(i + 1) * 0.6180339f;
        b->ph[i] = p - fm_floorf(p);
    }
    for (i = 0; i < LEAD_NPARAMS; i++)
        lead_set(b, i, params[i].def);
}

void lead_note_on(lead_t *b, int note, int accent, int slide)
{
    int legato;
    if (note < 0)
        note = 0;
    if (note > 127)
        note = 127;
    legato = !b->idle && ((slide && b->gate) || (b->legato && b->env > 0.05f));
    b->target_st = (float)(note - 69) + b->tune_st;
    b->accent = accent != 0;
    if (!legato) {
        if (b->idle)
            b->s1 = b->s2 = b->s3 = b->s4 = 0.0f;
        b->cur_st = b->target_st;
        b->stage = 0;
        b->cc = 0;
        b->snap = 1;
    } else if (b->stage == 2) {
        b->stage = 1;                                      /* a note on a releasing one: the envelope carries on */
    }
    b->gate = 1;
    b->idle = 0;
}

void lead_note_off(lead_t *b)
{
    b->gate = 0;
    b->stage = 2;
}
void lead_all_off(lead_t *b) { lead_note_off(b); }

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

static void control(lead_t *b)
{
    float cur, fc, x, sn, cs, g, g2, cut, env_depth, base;
    b->cur_st += (b->target_st - b->cur_st) * b->glide_c;
    b->vib_ph += (float)LEAD_CTRL * (5.5f / LEAD_SR);
    if (b->vib_ph >= 1.0f)
        b->vib_ph -= 1.0f;
    cur = b->cur_st + b->vib_st * fm_sin_turns(b->vib_ph);
    base = 440.0f * fm_exp2f(cur * (1.0f / 12.0f)) * (1.0f / LEAD_SR);
    if (base > 0.4f)
        base = 0.4f;
    b->inc0 = b->snap ? base : b->inc1;
    b->inc1 = base;
    b->snap = 0;
    env_depth = b->env_st * (b->accent ? 1.0f + 0.5f * b->acc_amt : 1.0f);
    cut = b->cut_st + b->env * env_depth;
    if (cut > 60.0f)
        cut = 60.0f;
    fc = 440.0f * fm_exp2f(cut * (1.0f / 12.0f));
    x = fc * (0.5f / LEAD_SR);
    sn = fm_sin_turns(x);
    cs = fm_sin_turns(x + 0.25f);
    g = sn / (sn + cs);
    g2 = g * g;
    b->G = g;
    b->G2 = g2;
    b->G3 = g2 * g;
    b->G4 = g2 * g2;
    b->kk = b->reso_k;
    b->rdiv = 1.0f / (1.0f + b->reso_k * g2 * g2);
    b->gain = b->accent ? 1.0f + 0.4f * b->acc_amt : 1.0f;
}

void lead_render(lead_t *b, float *out, int n)
{
    int i, v, nv = b->nvoices;
    if (b->idle) {
        memset(out, 0, (unsigned)n * sizeof(float));
        return;
    }
    for (i = 0; i < n; i++) {
        float inc, step, mix = 0.0f, u, y4, x, xs, vv, y, ym, G, omg, sum, e, norm;
        if (b->cc == 0) {
            control(b);
            b->cc = LEAD_CTRL;
        }
        b->cc--;
        step = (float)(LEAD_CTRL - b->cc) * (1.0f / (float)LEAD_CTRL);
        inc = b->inc0 + (b->inc1 - b->inc0) * step;

        for (v = 0; v < nv; v++) {                              /* the unison */
            float dt = inc * b->ratio[v], p = b->ph[v] + dt, saw, w;
            if (p >= 1.0f)
                p -= 1.0f;
            b->ph[v] = p;
            saw = 2.0f * p - 1.0f - blep(p, dt);
            w = saw;
            if (b->color > 0.0f) {
                float p2 = p + 0.5f, sq;
                if (p2 >= 1.0f)
                    p2 -= 1.0f;
                sq = (p < 0.5f ? 1.0f : -1.0f) + blep(p, dt) - blep(p2, dt);
                w = saw + (sq - saw) * b->color;
            }
            mix += w;
        }
        norm = nv > 1 ? 1.0f / fm_sqrtf((float)nv) : 1.0f;
        mix *= norm;
        if (b->sub > 0.0f) {
            b->sub_ph += inc * 0.5f;
            if (b->sub_ph >= 1.0f)
                b->sub_ph -= 1.0f;
            mix += b->sub * fm_sin_turns(b->sub_ph);
        }
        u = mix * (0.5f * b->drive_g);

        G = b->G;
        omg = 1.0f - G;
        sum = b->G3 * (omg * b->s1) + b->G2 * (omg * b->s2) + G * (omg * b->s3) + omg * b->s4;
        y4 = (b->G4 * u + sum) * b->rdiv;
        x = u - b->kk * y4;
        xs = fm_tanhf(x);
        vv = G * (xs - b->s1); y = vv + b->s1; b->s1 = y + vv;
        vv = G * (y - b->s2);  y = vv + b->s2; b->s2 = y + vv;
        vv = G * (y - b->s3);  y = vv + b->s3; b->s3 = y + vv;
        vv = G * (y - b->s4);  y = vv + b->s4; b->s4 = y + vv;
        b->s1 = fm_clampf(b->s1, -8.0f, 8.0f);
        b->s2 = fm_clampf(b->s2, -8.0f, 8.0f);
        b->s3 = fm_clampf(b->s3, -8.0f, 8.0f);
        b->s4 = fm_clampf(b->s4, -8.0f, 8.0f);
        ym = fm_clampf(y + y * b->kk * 0.5f, -8.0f, 8.0f);

        e = b->env;                                             /* the envelope: attack, decay to the sustain, release */
        if (b->stage == 0) {
            e += b->att_step;
            if (e >= 1.0f) {
                e = 1.0f;
                b->stage = 1;
            }
        } else if (b->stage == 1) {
            e = b->sustain + (e - b->sustain) * b->dec_c;
        } else {
            e *= b->rel_c;
        }
        b->env = e;

        ym = fm_tanhf(ym * 2.0f * b->gain);
        if (b->dist_wet > 0.0f) {
            float w;
            switch (b->dist_type) {
            case 1: w = fm_clampf(ym * b->dist_g, -1.0f, 1.0f); break;
            case 2: w = fm_sinf(ym * b->dist_g * (FM_PI * 0.5f)); break;
            default: w = fm_tanhf(ym * b->dist_g) * b->dist_norm; break;
            }
            ym += (w - ym) * b->dist_wet;
        }
        if (b->ring_mix > 0.0f) {                               /* the ring modulator: dry to wet (the sound times a sine) */
            b->ring_ph += inc * b->ring_ratio;
            if (b->ring_ph >= 1.0f)
                b->ring_ph -= 1.0f;
            ym += (ym * fm_sin_turns(b->ring_ph) - ym) * b->ring_mix;
        }
        out[i] = 0.5f * e * ym;
        if (b->stage == 2 && e < 1e-4f) {
            memset(out + i + 1, 0, (unsigned)(n - i - 1) * sizeof(float));
            b->idle = 1;
            b->env = 0.0f;
            return;
        }
    }
}
