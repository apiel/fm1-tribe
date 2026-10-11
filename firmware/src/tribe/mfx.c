/* SPDX-License-Identifier: GPL-3.0-only */
/* The master effects the knobs can be assigned to: FILTER (a DJ filter: one knob, low-pass to the left of the
 * middle, high-pass to the right, plus RESO), DRIVE, CRUSH. Mono, in place, a few operations a sample, and
 * silent (not even run) while their knob sits at its neutral value. Each one runs either on the whole mix or,
 * with its own state, on each track it is limited to. COMP is the master section's (dsp/master.c). */
#include "tribe.h"
#include "../dsp/fastmath.h"
#include "../app/plat.h"

const char *mfx_name(int fx)
{
    static const char *const N[NFX] = {"NONE", "FILTER", "DRIVE", "CRUSH", "RING", "DECIM", "VINYL", "COMP", "DELAY", "REVERB"};
    return N[fx < NFX ? fx : 0];
}
int mfx_nparams(int fx) { return fx == FX_NONE ? 0 : fx == FX_COMP ? 1 : 2; }
const char *mfx_pname(int fx, int p)
{
    static const char *const F[2] = {"DJ", "RESO"}, *const D[2] = {"AMT", "MIX"}, *const Y[2] = {"MIX", "FDBK"},
                            *const R[2] = {"MIX", "SIZE"}, *const G[2] = {"AMT", "FREQ"}, *const V[2] = {"AMT", "WEAR"};
    return fx == FX_FILTER ? F[p & 1] : fx == FX_COMP ? "AMT" : fx == FX_DELAY ? Y[p & 1] : fx == FX_REVERB ? R[p & 1]
           : fx == FX_RING ? G[p & 1] : fx == FX_VINYL ? V[p & 1] : D[p & 1];
}

static char *puts_(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}
static char *puti_(char *b, int v)
{
    char t[8];
    int n = 0;
    if (v < 0) {
        *b++ = '-';
        v = -v;
    }
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
void mfx_format(int fx, int p, int v, char *buf)
{
    if (fx == FX_FILTER && p == 0) {                     /* LP 0..100 left of the middle, HP right, OFF between */
        if (v < 61)
            puti_(puts_(buf, "LP"), 100 - v * 100 / 60);
        else if (v > 67)
            puti_(puts_(buf, "HP"), (v - 67) * 100 / 60);
        else
            puts_(buf, "OFF");
        return;
    }
    if (fx == FX_RING && p == 1) {                       /* the carrier: 40 Hz .. 2 kHz */
        puts_(puti_(buf, (int)(40.0f * fm_exp2f((float)v * (5.64f / 127.0f)) + 0.5f)), "Hz");
        return;
    }
    puts_(puti_(buf, v * 100 / 127), "%");
}

/* is it doing anything at these values? */
int mfx_active(int fx, const uint8_t v[2])
{
    switch (fx) {
    case FX_FILTER: return v[0] < 61 || v[0] > 67;
    case FX_DRIVE: case FX_CRUSH: return v[0] > 0 && v[1] > 0;
    case FX_RING: case FX_VINYL: case FX_DELAY: case FX_REVERB: return v[0] > 0;
    case FX_DECIM: return v[0] > 0 && v[1] > 0;
    default: return 0;
    }
}

void mfx_run(int fx, mfxs_t *s, float *x, int n, const uint8_t v[2])
{
    int i;
    switch (fx) {
    case FX_FILTER: {
        /* a state variable filter (trapezoidal), cutoff ramped across the block; a = lowpass state, b = bandpass state */
        float fc, g1, g0 = s->gp, k = 2.0f - 1.85f * (float)v[1] / 127.0f, step;
        int hp = v[0] > 67;
        if (!hp)
            fc = 80.0f * fm_exp2f((float)v[0] * (7.6f / 60.0f));         /* 0 -> 80 Hz ... 60 -> 18 kHz */
        else
            fc = 40.0f * fm_exp2f((float)(v[0] - 67) * (7.6f / 60.0f));   /* 68 -> 40 Hz ... 127 -> 8 kHz */
        if (fc > 14000.0f)
            fc = 14000.0f;
        g1 = fm_tanf(fc * (FM_PI / 44100.0f));
        if (g0 <= 0.0f)
            g0 = g1;
        step = (g1 - g0) / (float)n;
        for (i = 0; i < n; i++) {
            float g = g0 + step * (float)i, a1 = 1.0f / (1.0f + g * (g + k)), hpf, bp, lp;
            hpf = (x[i] - (g + k) * s->b - s->a) * a1;
            bp = g * hpf + s->b;
            lp = g * bp + s->a;
            s->b = g * hpf + bp;
            s->a = g * bp + lp;
            x[i] = hp ? hpf : lp;
        }
        s->gp = g1;
        break;
    }
    case FX_DRIVE: {
        /* a soft clip, levelled so that a signal of 0.4 comes out at 0.4 whatever the drive: more drive evens the level
         * out (quiet parts rise, peaks stay) instead of just getting louder */
        float gain = 1.0f + (float)v[0] * (14.0f / 127.0f), mix = (float)v[1] * (1.0f / 127.0f), ref = 0.4f * gain;
        float norm = 0.4f * (1.0f + ref) / ref;
        for (i = 0; i < n; i++) {
            float y = x[i] * gain;
            y = y / (1.0f + fm_fabsf(y)) * norm;
            x[i] += (y - x[i]) * mix;
        }
        break;
    }
    case FX_RING: {                                      /* the carrier goes through the signal: bells and metal. AMT blends it in */
        float inc = 40.0f * fm_exp2f((float)v[1] * (5.64f / 127.0f)) * (1.0f / 44100.0f), amt = (float)v[0] * (1.0f / 127.0f);
        for (i = 0; i < n; i++) {
            s->a += inc;
            s->a -= fm_floorf(s->a);
            x[i] += (x[i] * fm_sin_turns(s->a) * 1.4f - x[i]) * amt;
        }
        break;
    }
    case FX_DECIM: {                                     /* a sample-rate reducer: hold every k-th sample, no bit loss */
        int hold = 1 + (int)v[0] * 31 / 127;
        float mix = (float)v[1] * (1.0f / 127.0f);
        for (i = 0; i < n; i++) {
            if (s->cnt-- <= 0) {
                s->cnt = hold - 1;
                s->hold = x[i];
            }
            x[i] += (s->hold - x[i]) * mix;
        }
        break;
    }
    case FX_VINYL: {                                     /* an old record: band-limited, hiss, and pops that get denser with WEAR */
        float amt = (float)v[0] * (1.0f / 127.0f), wear = (float)v[1] * (1.0f / 127.0f), k = 0.9f - 0.7f * amt;
        uint32_t pop_p = (uint32_t)((0.00005f + wear * 0.002f) * amt * 4294967295.0f);
        if (!s->rng)
            s->rng = 0x9E3779B9u;
        for (i = 0; i < n; i++) {
            float y, hiss;
            s->rng = s->rng * 1664525u + 1013904223u;
            hiss = ((float)(int32_t)(s->rng) * (1.0f / 2147483648.0f)) * 0.006f * amt;
            s->a += k * (x[i] - s->a);                   /* low-pass */
            s->b += 0.008f * (s->a - s->b);              /* its slow part: taken out again, a high-pass at ~60 Hz */
            y = s->a - s->b;
            s->rng = s->rng * 1664525u + 1013904223u;
            if (s->rng < pop_p)                          /* a pop: a short click of random size and sign */
                s->c = (((float)(int32_t)(s->rng * 2654435761u)) * (1.0f / 2147483648.0f)) * (0.1f + 0.5f * amt);
            y += hiss + s->c;
            s->c *= 0.55f;
            x[i] += (y - x[i]) * (amt * 4.0f > 1.0f ? 1.0f : amt * 4.0f);
        }
        break;
    }
    case FX_CRUSH: {
        int hold = 1 + (int)v[0] * 14 / 127;
        float levels = 4.0f + (float)(127 - v[0]) * (60.0f / 127.0f), mix = (float)v[1] * (1.0f / 127.0f);
        for (i = 0; i < n; i++) {
            if (s->cnt-- <= 0) {
                s->cnt = hold - 1;
                s->hold = fm_floorf(x[i] * levels + 0.5f) * (1.0f / levels);
            }
            x[i] += (s->hold - x[i]) * mix;
        }
        break;
    }
    }
}

/* ---- DELAY and REVERB: the master effects the tracks send to (FX 1 and FX 2). One of each: the send bus in, the wet out. The delay keeps 16-bit samples (32 KiB); the reverb is four
 * damped combs into two allpasses (24 KiB of floats), small and dense rather than big. ---- */
#include "mem.h"
#define DLY_N 16384
static int16_t dly_ring[DLY_N] X0X_POOL;
static int32_t dly_w;
static float dly_lp;
static uint8_t dly_on, rev_on;

static const uint16_t COMB_N[4] = {1116, 1188, 1277, 1356}, AP_N[2] = {556, 441};
#define REV_TOTAL (1116 + 1188 + 1277 + 1356 + 556 + 441)
static float rev_buf[REV_TOTAL] X0X_POOL;
static int32_t rev_pos[6];
static float rev_damp[4];

void mfx_send_run(int fx, float *x, int n, const uint8_t v[2], float spl)
{
    int i;
    if (fx == FX_DELAY) {
        int32_t time = (int32_t)(spl * 3.0f);                    /* a dotted eighth */
        float mix = (float)v[0] * (1.0f / 127.0f), fb = (float)v[1] * (0.85f / 127.0f);
        if (!dly_on) {                                           /* a fresh start: no old echoes */
            memset(dly_ring, 0, sizeof dly_ring);
            dly_lp = 0.0f;
            dly_on = 1;
        }
        if (time > DLY_N - 2)
            time = (int32_t)(spl * 2.0f);                        /* slow tempo: a quarter step shorter... two steps */
        if (time > DLY_N - 2)
            time = DLY_N - 2;
        if (time < 64)
            time = 64;
        for (i = 0; i < n; i++) {
            int32_t r = dly_w - time;
            float d, in;
            if (r < 0)
                r += DLY_N;
            d = (float)dly_ring[r] * (1.0f / 8000.0f);
            dly_lp += 0.55f * (d - dly_lp);                      /* the repeats get darker */
            in = x[i] + dly_lp * fb;
            in = in / (1.0f + 0.25f * fm_fabsf(in));             /* and cannot run away */
            in *= 8000.0f;
            dly_ring[dly_w] = (int16_t)(in > 32000.0f ? 32000 : in < -32000.0f ? -32000 : (int32_t)in);
            if (++dly_w >= DLY_N)
                dly_w = 0;
            x[i] = dly_lp * mix;                                 /* the wet only: the engine adds it to the mix */
        }
    } else if (fx == FX_REVERB) {
        float mix = (float)v[0] * (1.0f / 127.0f), room = 0.70f + (float)v[1] * (0.27f / 127.0f);
        float *base = rev_buf;
        if (!rev_on) {
            memset(rev_buf, 0, sizeof rev_buf);
            memset(rev_pos, 0, sizeof rev_pos);
            memset(rev_damp, 0, sizeof rev_damp);
            rev_on = 1;
        }
        for (i = 0; i < n; i++) {
            float in = x[i] * 0.03f, acc = 0.0f, *b = base;
            int c;
            for (c = 0; c < 4; c++) {
                float o = b[rev_pos[c]];
                rev_damp[c] += 0.65f * (o - rev_damp[c]);        /* damping: the tail loses its highs */
                b[rev_pos[c]] = in + rev_damp[c] * room;
                if (++rev_pos[c] >= COMB_N[c])
                    rev_pos[c] = 0;
                acc += o;
                b += COMB_N[c];
            }
            for (c = 0; c < 2; c++) {                            /* allpass: smears the combs into a diffuse wash */
                float o = b[rev_pos[4 + c]], y = o - acc;
                b[rev_pos[4 + c]] = acc + o * 0.5f;
                acc = y;
                if (++rev_pos[4 + c] >= AP_N[c])
                    rev_pos[4 + c] = 0;
                b += AP_N[c];
            }
            x[i] = acc * mix * 2.2f;
        }
    }
}

/* a delay or reverb that was switched off starts from silence next time */
void mfx_send_idle(int fx)
{
    if (fx == FX_DELAY)
        dly_on = 0;
    else if (fx == FX_REVERB)
        rev_on = 0;
}
