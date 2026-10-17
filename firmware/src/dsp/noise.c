/* SPDX-License-Identifier: GPL-3.0-only */
/* NOISE: see noise.h. */
#include "../tribe/mem.h"
#include "noise.h"
#include "fastmath.h"

static const char *const ntype_names[] = { "White", "Pink", "Brown", "Dust" };
static const char *const ftype_names[] = { "LP", "HP", "BP", "Notch" };
static const char *const mod_names[] = { "Off", "Sine", "Rand", "Drift", "Gate" };

/* the pink and brown levels: measured, so each colour has the rms of white (0.577) on the raw filters */
#define PINK_G 0.333f
#define BROWN_G 10.0f
/* dust: a click with probability 0.002 a sample (about 90 a second); this gain makes its rms about white's (0.577) */
#define DUST_G 13.0f
#define DUST_LIM 0.998f
#define NOISE_OUT 1.0f                /* level: the default matches LEAD's (rms 0.17 on a note); the soft limit keeps it bounded */
#define MOD_OCT 24.0f                 /* the cutoff's modulation at full depth: 24 semitones (2 octaves) each way */

static const x0x_param_t params[NOISE_NPARAMS] = {
    { "NType", 3, 0, ntype_names },
    { "FType", 3, 0, ftype_names },
    { "Cutoff", 127, 70, 0 },        /* 40 Hz .. 12 kHz */
    { "Reso", 127, 40, 0 },          /* Q 0.5 .. 24; the notch: Q 0.5 .. 4 */
    { "KeyTrk", 127, 64, 0 },        /* the cutoff follows the note: 0 .. 1 semitone a semitone */
    { "Mod", 4, 0, mod_names },      /* a built-in modulation: the cutoff, or the level */
    { "ModAmt", 127, 0, 0 },         /* its depth */
    { "ModRat", 127, 40, 0 },        /* its rate: 0.1 .. 51 Hz, exponential */
    { "Attack", 127, 20, 0 },        /* 1 ms .. 2 s, exponential */
    { "Decay", 127, 64, 0 },         /* 5 ms .. 2.5 s */
    { "Sustain", 127, 110, 0 },      /* the level a held note settles at */
    { "Release", 127, 50, 0 },       /* 5 ms .. 3 s */
    { "EnvMod", 127, 50, 0 },        /* filter envelope depth, 0 .. 60 semitones */
    { "Drive", 127, 0, 0 },          /* gain into the filter, 1 .. 8 */
};

int noise_nparams(void) { return NOISE_NPARAMS; }
const x0x_param_t *noise_param(int i) { return (i >= 0 && i < NOISE_NPARAMS) ? &params[i] : 0; }
int noise_get(const noise_t *b, int i) { return (i >= 0 && i < NOISE_NPARAMS) ? b->pot[i] : 0; }

static float decay_mul(float seconds, float step_s) { return fm_expf(-step_s / seconds); }

void noise_set(noise_t *b, int i, int v)
{
    float n;
    if (i < 0 || i >= NOISE_NPARAMS)
        return;
    if (v < 0)
        v = 0;
    if (v > params[i].max)
        v = params[i].max;
    b->pot[i] = (uint8_t)v;
    n = (float)v * (1.0f / 127.0f);
    switch (i) {
    case NOISE_NTYPE: b->ntype = v; break;
    case NOISE_FTYPE: b->ftype = v; break;
    case NOISE_CUTOFF: b->cut_st = -43.0f + 100.0f * n; break;
    case NOISE_RESO: b->reso_n = n; break;
    case NOISE_KEYTRK: b->keytrk = n; break;
    case NOISE_MOD: b->mod = v; break;
    case NOISE_MODAMT: b->mod_amt = n; break;
    case NOISE_MODRAT: b->mod_inc = 0.1f * fm_exp2f(n * 9.0f) * (float)NOISE_CTRL / NOISE_SR; break;
    case NOISE_ATTACK: b->att_step = 1.0f / (0.001f * fm_exp2f(n * 11.0f) * NOISE_SR); break;
    case NOISE_DECAY: b->dec_c = decay_mul(0.005f * fm_exp2f(n * 9.0f), 1.0f / NOISE_SR); break;
    case NOISE_SUSTAIN: b->sustain = n; break;
    case NOISE_RELEASE: b->rel_c = decay_mul(0.005f * fm_exp2f(n * 9.2f), 1.0f / NOISE_SR); break;
    case NOISE_ENVMOD: b->env_st = 60.0f * n; break;
    case NOISE_DRIVE: b->drive_g = 1.0f + 7.0f * n * n; break;
    }
}

void noise_init(noise_t *b)
{
    int i;
    memset(b, 0, sizeof *b);
    b->idle = 1;
    b->rng = 0x2545F491u;                                  /* any nonzero seed */
    b->mod_gain = 1.0f;
    svf_init(&b->fl);
    for (i = 0; i < NOISE_NPARAMS; i++)
        noise_set(b, i, params[i].def);
}

void noise_note_on(noise_t *b, int note, int slide)
{
    int legato;
    if (note < 0)
        note = 0;
    if (note > 127)
        note = 127;
    legato = slide && b->gate && !b->idle;                 /* a slide over a held note: the envelope goes on */
    b->note_st = (float)(note - 69);
    if (!legato) {
        b->stage = 0;                                      /* a new attack, from the level it is at */
        b->cc = 0;
    }
    b->gate = 1;
    b->idle = 0;
}

void noise_note_off(noise_t *b)
{
    b->gate = 0;
    b->stage = 2;
}
void noise_all_off(noise_t *b) { noise_note_off(b); }

/* the white noise: xorshift32, in [-1, 1) */
static inline float white(noise_t *b)
{
    uint32_t x = b->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    b->rng = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

/* the built-in modulation, at the control rate. Sine: the cutoff follows a sine. Rand: it takes a new random value at
 * each cycle (a stepped sample and hold). Drift: the same, but glides to each new value. Gate: the level pulses,
 * softly, so it is on and off with no click. The cutoff ones add to the cutoff in control(); the gate sets mod_gain */
static void modulate(noise_t *b)
{
    float target;
    b->mod_ph += b->mod_inc;
    if (b->mod_ph >= 1.0f) {
        b->mod_ph -= 1.0f;
        b->mod_hold = white(b);                                /* a new random value, for Rand and Drift */
    }
    switch (b->mod) {
    case NM_SINE:
        b->mod_v = fm_sin_turns(b->mod_ph);
        break;
    case NM_RAND:
        b->mod_v = b->mod_hold;
        break;
    case NM_DRIFT:
        target = b->mod_hold;
        b->mod_v += (target - b->mod_v) * 0.005f;               /* a glide of about 35 ms to each new value */
        break;
    case NM_GATE:
        b->mod_v = fm_clampf(6.0f * fm_sin_turns(b->mod_ph), -1.0f, 1.0f);
        b->mod_gain = 1.0f - b->mod_amt * (0.5f - 0.5f * b->mod_v);
        return;
    default:
        b->mod_v = 0.0f;
        break;
    }
    b->mod_gain = 1.0f;
}

/* the filter's coefficients: the cutoff with the envelope, the note and the modulation, the damping from the resonance.
 * The notch's Q stops at 4: a narrower notch is too thin to hear on noise, so its cutoff sweep would be silent */
static void control(noise_t *b)
{
    float cut, fc, k;
    if (b->mod)
        modulate(b);
    else
        b->mod_gain = 1.0f;
    cut = b->cut_st + b->env * b->env_st + b->keytrk * b->note_st;
    if (b->mod == NM_SINE || b->mod == NM_RAND || b->mod == NM_DRIFT)
        cut += b->mod_v * b->mod_amt * MOD_OCT;
    if (cut > 100.0f)
        cut = 100.0f;
    fc = 440.0f * fm_exp2f(cut * (1.0f / 12.0f));
    k = b->ftype == NF_NOTCH ? 2.0f * fm_exp2f(-3.0f * b->reso_n) : 2.0f * fm_exp2f(-5.6f * b->reso_n);
    svf_set(&b->fl, fc, k, NOISE_SR);
}

void noise_render(noise_t *b, float *out, int n)
{
    int i;
    if (b->idle) {
        memset(out, 0, (unsigned)n * sizeof(float));
        return;
    }
    for (i = 0; i < n; i++) {
        float w, nz, u, lp, bp, hp, y, e;
        if (b->cc == 0) {
            control(b);
            b->cc = NOISE_CTRL;
        }
        b->cc--;

        w = white(b);
        switch (b->ntype) {
        case NT_PINK:
            b->pk0 = 0.99765f * b->pk0 + w * 0.0990460f;
            b->pk1 = 0.96300f * b->pk1 + w * 0.2965164f;
            b->pk2 = 0.57000f * b->pk2 + w * 1.0526913f;
            nz = (b->pk0 + b->pk1 + b->pk2 + w * 0.1848f) * PINK_G;
            break;
        case NT_BROWN:
            b->brown = (b->brown + 0.02f * w) * (1.0f / 1.02f);
            nz = b->brown * BROWN_G;
            break;
        case NT_DUST:
            nz = fm_fabsf(w) > DUST_LIM ? (w > 0.0f ? DUST_G : -DUST_G) : 0.0f;
            break;
        default:
            nz = w;
            break;
        }

        u = fm_tanhf(nz * b->drive_g);                         /* the drive into the filter, bounded */
        svf_tick(&b->fl, u, &lp, &bp, &hp);
        switch (b->ftype) {
        case NF_HP: y = hp; break;
        case NF_BP: y = bp; break;
        case NF_NOTCH: y = lp + hp; break;
        default: y = lp; break;
        }
        y = fm_tanhf(y);                                       /* a resonant peak is soft-limited, not cut */

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

        out[i] = NOISE_OUT * e * y * b->mod_gain;
        if (b->stage == 2 && e < 1e-4f) {
            memset(out + i + 1, 0, (unsigned)(n - i - 1) * sizeof(float));
            b->idle = 1;
            b->env = 0.0f;
            return;
        }
    }
}
