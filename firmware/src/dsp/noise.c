/* SPDX-License-Identifier: GPL-3.0-only */
/* NOISE: see noise.h. */
#include "../tribe/mem.h"
#include "noise.h"
#include "fastmath.h"

static const char *const ntype_names[] = { "White", "Pink", "Brown" };
static const char *const ftype_names[] = { "LP", "HP", "BP", "Notch" };

/* the pink and brown levels: about the rms of white (0.58), so the colours sit at the same loudness */
#define PINK_G 0.333f                /* measured: pink rms 1.73 and brown rms 0.0575 on the raw filters, white 0.577 */
#define BROWN_G 10.0f

static const x0x_param_t params[NOISE_NPARAMS] = {
    { "Morph", 127, 0, 0 },          /* 0 square, 1/4 noise, 1/2 saw, 3/4 noise, 1 tri */
    { "Noise", 127, 32, 0 },         /* the noise floor: always there; at a noise anchor it is all there is */
    { "NType", 2, 0, ntype_names },
    { "Tune", 127, 64, 0 },          /* +-1.5 semitones */
    { "FType", 3, 0, ftype_names },
    { "Cutoff", 127, 70, 0 },        /* 40 Hz .. 12 kHz */
    { "Reso", 127, 40, 0 },          /* Q 0.5 .. 24 */
    { "KeyTrk", 127, 64, 0 },        /* the cutoff follows the note: 0 .. 1 semitone a semitone */
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

static inline float pitch_inc(float st)                      /* semitones from A4 to a phase step per sample */
{
    float base = 440.0f * fm_exp2f(st * (1.0f / 12.0f)) * (1.0f / NOISE_SR);
    return base > 0.4f ? 0.4f : base;
}

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
    case NOISE_MORPH: b->morph = (float)v * (4.0f / 127.0f); break;
    case NOISE_NOISE: b->floor_ = n; break;
    case NOISE_NTYPE: b->ntype = v; break;
    case NOISE_TUNE: b->tune_st = (float)(v - 64) * (1.5f / 64.0f); break;
    case NOISE_FTYPE: b->ftype = v; break;
    case NOISE_CUTOFF: b->cut_st = -43.0f + 100.0f * n; break;
    case NOISE_RESO: b->reso_k = 2.0f * fm_exp2f(-5.6f * n); break;
    case NOISE_KEYTRK: b->keytrk = n; break;
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
    legato = slide && b->gate && !b->idle;                 /* a slide over a held note: the pitch moves, the envelope goes on */
    b->note_st = (float)(note - 69);
    b->inc = pitch_inc((float)(note - 69) + b->tune_st);
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

/* the morph: the square fades out into the noise, the noise into the saw, the saw out into the noise, the noise into
 * the tri. The noise gets what the oscillator gives up, on top of the floor */
static void control(noise_t *b)
{
    float pos = b->morph, f, go, fc, cut;
    int seg = (int)pos;
    if (seg > 3)
        seg = 3;
    f = pos - (float)seg;
    b->gsq = b->gsaw = b->gtri = 0.0f;
    switch (seg) {
    case 0: b->gsq = 1.0f - f; break;
    case 1: b->gsaw = f; break;
    case 2: b->gsaw = 1.0f - f; break;
    default: b->gtri = f; break;
    }
    go = b->gsq + b->gsaw + b->gtri;
    b->gnoise = b->floor_ + (1.0f - b->floor_) * (1.0f - go);

    cut = b->cut_st + b->env * b->env_st + b->keytrk * b->note_st;
    if (cut > 100.0f)
        cut = 100.0f;
    fc = 440.0f * fm_exp2f(cut * (1.0f / 12.0f));
    svf_set(&b->fl, fc, b->reso_k, NOISE_SR);
}

void noise_render(noise_t *b, float *out, int n)
{
    int i;
    if (b->idle) {
        memset(out, 0, (unsigned)n * sizeof(float));
        return;
    }
    for (i = 0; i < n; i++) {
        float p, p2, dt = b->inc, saw, sq, tri, osc, w, nz, mix, u, lp, bp, hp, y, e;
        if (b->cc == 0) {
            control(b);
            b->cc = NOISE_CTRL;
        }
        b->cc--;

        p = b->ph + dt;
        if (p >= 1.0f)
            p -= 1.0f;
        b->ph = p;
        saw = 2.0f * p - 1.0f - blep(p, dt);
        p2 = p + 0.5f;
        if (p2 >= 1.0f)
            p2 -= 1.0f;
        sq = (p < 0.5f ? 1.0f : -1.0f) + blep(p, dt) - blep(p2, dt);
        tri = 2.0f * fm_fabsf(2.0f * p - 1.0f) - 1.0f;
        osc = b->gsq * sq + b->gsaw * saw + b->gtri * tri;

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
        default:
            nz = w;
            break;
        }
        mix = osc + b->gnoise * nz;

        u = fm_tanhf(mix * b->drive_g);                        /* the drive into the filter, bounded */
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

        out[i] = 0.5f * e * y;
        if (b->stage == 2 && e < 1e-4f) {
            memset(out + i + 1, 0, (unsigned)(n - i - 1) * sizeof(float));
            b->idle = 1;
            b->env = 0.0f;
            return;
        }
    }
}
