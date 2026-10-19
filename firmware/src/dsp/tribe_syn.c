/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE: see tribe_syn.h. */
#include "../tribe/mem.h"
#include "tribe_syn.h"
#include "fastmath.h"
#include "cycle.h"

static const char *const ftype_names[] = { "LP", "HP", "BP", "Notch" };
static const char *const fx_names[] = { "Off", "Drive", "Crush", "Decim", "Ring" };

#define TSYN_OUT 0.3f                  /* level: a default note is about as loud as LEAD's (rms 0.17) */

static const x0x_param_t params[TS_NPARAMS] = {
    { "Cutoff", 127, 70, 0 },        /* 40 Hz .. 12 kHz */
    { "Reso", 127, 40, 0 },          /* Q 0.5 .. 24; the notch: Q 0.5 .. 4 */
    { "FType", 3, 0, ftype_names },
    { "KeyTrk", 127, 64, 0 },        /* the cutoff follows the note: 0 .. 1 semitone a semitone */
    { "Wave", 127, 0, 0 },           /* the cycle's morph: sine, triangle, saw, square */
    { "Skew", 127, 64, 0 },          /* where the cycle peaks: 5 .. 95 % */
    { "Harm2", 127, 64, 0 },         /* the second harmonic, bipolar: 64 is none */
    { "Harm3", 127, 64, 0 },         /* the third harmonic, bipolar: 64 is none */
    { "WvFold", 127, 0, 0 },         /* a sine fold of the cycle */
    { "Phase", 127, 0, 0 },          /* where the cycle starts: 0 .. 1 turn */
    { "Attack", 127, 20, 0 },        /* 1 ms .. 2 s, exponential */
    { "Decay", 127, 64, 0 },         /* 5 ms .. 2.5 s */
    { "Sustain", 127, 110, 0 },      /* the level a held note settles at */
    { "Release", 127, 50, 0 },       /* 5 ms .. 3 s */
    { "EnvMod", 127, 50, 0 },        /* the envelope opens the cutoff: 0 .. 60 semitones */
    { "EnvWav", 127, 0, 0 },         /* the envelope folds the cycle: 0 .. a full fold */
    { "FxTyp", 4, 0, fx_names },     /* one effect after the filter */
    { "FxPar", 127, 64, 0 },         /* its one parameter: drive gain, bit depth, hold, ring frequency */
};

int tsyn_nparams(void) { return TS_NPARAMS; }
const x0x_param_t *tsyn_param(int i) { return (i >= 0 && i < TS_NPARAMS) ? &params[i] : 0; }
int tsyn_get(const tsyn_t *b, int i) { return (i >= 0 && i < TS_NPARAMS) ? b->pot[i] : 0; }

static float decay_mul(float seconds, float step_s) { return fm_expf(-step_s / seconds); }

void tsyn_set(tsyn_t *b, int i, int v)
{
    float n;
    if (i < 0 || i >= TS_NPARAMS)
        return;
    if (v < 0)
        v = 0;
    if (v > params[i].max)
        v = params[i].max;
    b->pot[i] = (uint8_t)v;
    n = (float)v * (1.0f / 127.0f);
    if (i == TS_WAVE || i == TS_SKEW || i == TS_HARM2 || i == TS_HARM3 || i == TS_WVFOLD)
        b->tab_dirty = 1;
    switch (i) {
    case TS_WAVE: b->wave = n; break;
    case TS_SKEW: b->skew = 0.05f + 0.9f * n; break;
    case TS_HARM2: b->harm2 = v >= 64 ? (float)(v - 64) / 63.0f : (float)(v - 64) / 64.0f; break;
    case TS_HARM3: b->harm3 = v >= 64 ? (float)(v - 64) / 63.0f : (float)(v - 64) / 64.0f; break;
    case TS_WVFOLD: b->fold = n; break;
    case TS_PHASE: b->phase = n; break;
    case TS_FTYPE: b->ftype = v; break;
    case TS_CUTOFF: b->cut_st = -43.0f + 100.0f * n; break;
    case TS_RESO: b->reso_n = n; break;
    case TS_KEYTRK: b->keytrk = n; break;
    case TS_ATTACK: b->att_step = 1.0f / (0.001f * fm_exp2f(n * 11.0f) * TSYN_SR); break;
    case TS_DECAY: b->dec_c = decay_mul(0.005f * fm_exp2f(n * 9.0f), 1.0f / TSYN_SR); break;
    case TS_SUSTAIN: b->sustain = n; break;
    case TS_RELEASE: b->rel_c = decay_mul(0.005f * fm_exp2f(n * 9.2f), 1.0f / TSYN_SR); break;
    case TS_ENVMOD: b->env_st = 60.0f * n; break;
    case TS_ENVWAV: b->env_wav = n; break;
    case TS_FXTYP: b->fxtyp = v; break;
    case TS_FXPAR:                   /* one pot, read by the effect that is selected */
        b->fx_drive = 1.0f + 15.0f * n * n;
        b->crush_scale = v ? fm_exp2f(15.0f - 13.0f * n) : 0.0f;   /* 2^(bits - 1), bits 16 .. 3 */
        b->hold_n = 1.0f + 47.0f * n * n;
        b->ring_inc = 30.0f * fm_exp2f(n * 7.0f) / TSYN_SR;         /* 30 Hz .. 3.8 kHz */
        break;
    }
}

void tsyn_init(tsyn_t *b)
{
    int i;
    memset(b, 0, sizeof *b);
    b->idle = 1;
    b->hold_n = 1.0f;
    svf_init(&b->fl);
    for (i = 0; i < TS_NPARAMS; i++)
        tsyn_set(b, i, params[i].def);
}

void tsyn_note_on(tsyn_t *b, int note, int slide)
{
    int legato;
    if (note < 0)
        note = 0;
    if (note > 127)
        note = 127;
    legato = slide && b->gate && !b->idle;                 /* a slide over a held note: the envelope goes on */
    b->note_st = (float)(note - 69);
    b->inc = 440.0f * fm_exp2f(b->note_st * (1.0f / 12.0f)) / TSYN_SR;
    if (!legato) {
        b->stage = 0;                                      /* a new attack, from the level it is at */
        b->cc = 0;
    }
    b->gate = 1;
    b->idle = 0;
}

void tsyn_note_off(tsyn_t *b)
{
    b->gate = 0;
    b->stage = 2;
}
void tsyn_all_off(tsyn_t *b) { tsyn_note_off(b); }

/* one sample of the cycle: the shared generator (cycle.c) */
float tsyn_shape(float ph, float wave, float skew, float harm2, float harm3, float fold)
{
    cycle_p c = { wave, skew, harm2, harm3, fold };
    return cycle_shape(ph, &c);
}

/* the filter's coefficients: the cutoff with the envelope and the note, the damping from the resonance. The notch's Q stops
 * at 4, as NOISE's: a narrower notch is too thin to hear on a cycle */
static void control(tsyn_t *b)
{
    float cut = b->cut_st + b->env * b->env_st + b->keytrk * b->note_st, fc, k;
    if (cut > 100.0f)
        cut = 100.0f;
    fc = 440.0f * fm_exp2f(cut * (1.0f / 12.0f));
    k = b->ftype == TF_NOTCH ? 2.0f * fm_exp2f(-3.0f * b->reso_n) : 2.0f * fm_exp2f(-5.6f * b->reso_n);
    svf_set(&b->fl, fc, k, TSYN_SR);
}

void tsyn_render(tsyn_t *b, float *out, int n)
{
    int i;
    if (b->idle) {
        memset(out, 0, (unsigned)n * sizeof(float));
        return;
    }
    for (i = 0; i < n; i++) {
        float s, lp, bp, hp, y, e, fold;
        if (b->cc == 0) {
            control(b);
            b->cc = TSYN_CTRL;
        }
        b->cc--;

        if (b->env_wav > 0.0f) {                               /* the fold follows the envelope: the cycle is made per sample */
            fold = fm_clampf(b->fold + b->env * b->env_wav, 0.0f, 1.0f);
            s = tsyn_shape(b->ph + b->phase, b->wave, b->skew, b->harm2, b->harm3, fold);
        } else {
            if (b->tab_dirty) {
                cycle_p c = { b->wave, b->skew, b->harm2, b->harm3, b->fold };
                cycle_build(b->tab, &c);
                b->tab_dirty = 0;
            }
            s = cycle_read(b->tab, b->ph + b->phase - fm_floorf(b->ph + b->phase));
        }
        b->ph += b->inc;
        b->ph -= fm_floorf(b->ph);

        svf_tick(&b->fl, s, &lp, &bp, &hp);
        switch (b->ftype) {
        case TF_HP: y = hp; break;
        case TF_BP: y = bp; break;
        case TF_NOTCH: y = lp + hp; break;
        default: y = lp; break;
        }
        y = fm_tanhf(y);                                       /* a resonant peak is soft-limited, not cut */

        switch (b->fxtyp) {                                    /* the one effect, after the filter */
        case TX_DRIVE:
            y = fm_tanhf(y * b->fx_drive);
            break;
        case TX_CRUSH:
            if (b->crush_scale > 0.0f)
                y = fm_floorf(y * b->crush_scale + 0.5f) / b->crush_scale;
            break;
        case TX_DECIM:
            b->hold_c += 1.0f;
            if (b->hold_c >= b->hold_n) {
                b->hold_c -= b->hold_n;
                b->hold_v = y;
            }
            y = b->hold_v;
            break;
        case TX_RING:
            y *= 1.4142f * fm_sin_turns(b->ring_ph);
            b->ring_ph += b->ring_inc;
            b->ring_ph -= fm_floorf(b->ring_ph);
            break;
        default:
            break;
        }

        e = b->env;                                            /* the envelope: attack, decay to the sustain, release */
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

        out[i] = TSYN_OUT * e * y;
        if (b->stage == 2 && e < 1e-4f) {
            memset(out + i + 1, 0, (unsigned)(n - i - 1) * sizeof(float));
            b->idle = 1;
            b->env = 0.0f;
            return;
        }
    }
}
