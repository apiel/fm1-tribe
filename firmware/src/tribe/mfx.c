/* SPDX-License-Identifier: GPL-3.0-only */
/* The master effects: the DJ filter (one knob, low-pass to the left of the middle, high-pass to the right, plus RESO; on the whole mix) and FX 1 / FX 2, the two effects the tracks send to. Mono, in place, a few operations a sample. */
#include "tribe.h"
#include "../dsp/fastmath.h"
#include "../app/plat.h"

const char *mx_name(int type)
{
    static const char *const N[NMX] = {"REVERB", "DELAY", "DRIVE", "DISTORT", "FUZZ", "WAVSHAPE", "CRUSH", "DECIM", "RING", "CHORUS", "FLANGER", "PHASER", "TREMOLO", "COMB"};
    return N[type < NMX ? type : 0];
}
const char *mx_pname(int type)
{
    static const char *const P[NMX] = {"SIZE", "FDBK", "DRIVE", "DRIVE", "FUZZ", "FOLD", "BITS", "RATE", "FREQ", "RATE", "RATE", "RATE", "RATE", "PITCH"};
    return P[type < NMX ? type : 0];
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
void dj_format(int v, char *buf)                     /* LP 0..100 left of the middle, HP right, OFF between */
{
    if (v < 61)
        puti_(puts_(buf, "LP"), 100 - v * 100 / 60);
    else if (v > 67)
        puti_(puts_(buf, "HP"), (v - 67) * 100 / 60);
    else
        puts_(buf, "OFF");
}
void mx_format(int type, int v, char *buf)
{
    if (type == MX_RING || type == MX_COMB)              /* the carrier, the comb's pitch: 40 Hz .. 2 kHz */
        puts_(puti_(buf, (int)(40.0f * fm_exp2f((float)v * (5.64f / 127.0f)) + 0.5f)), "Hz");
    else
        puts_(puti_(buf, v * 100 / 127), "%");
}

int dj_active(const uint8_t dj[2]) { return dj[0] < 61 || dj[0] > 67; }

void dj_run(mfxs_t *s, float *x, int n, const uint8_t v[2])
{
    int i;
    {
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
    }
}

/* a sine LFO: PH is its phase in turns (kept in the slot's state), INC the step a sample */
static float lfo(float *ph, float inc)
{
    *ph += inc;
    *ph -= fm_floorf(*ph);
    return fm_sin_turns(*ph);
}
/* a first-order allpass: flat level at every frequency, a phase shift set by A; Z is its one state */
static float allpass(float x, float a, float *z)
{
    float y = a * x + *z;
    *z = x - a * y;
    return y;
}

/* ---- CHORUS, FLANGER and COMB: a short line, 1280 samples (29 ms at 44.1 kHz) of 16-bit samples (2.5 KiB), that they share: only one of FX 1 / FX 2 can use it ---- */
#define MOD_N 1280
static int16_t mod_ring[MOD_N] X0X_POOL;
static int32_t mod_w;
static uint8_t mod_on;

static float mod_read(float d)                       /* d samples back from the write position, linear between two */
{
    float r = (float)mod_w - d, f, a, b;
    int32_t i;
    if (r < 0.0f)
        r += MOD_N;
    if (r >= MOD_N)
        r -= MOD_N;
    i = (int32_t)r;
    f = r - (float)i;
    a = (float)mod_ring[i];
    b = (float)mod_ring[i + 1 == MOD_N ? 0 : i + 1];
    return (a + (b - a) * f) * (1.0f / 8000.0f);
}
static void mod_put(float v)
{
    float y = v * 8000.0f;
    mod_ring[mod_w] = (int16_t)(y > 32000.0f ? 32000 : y < -32000.0f ? -32000 : (int32_t)y);
    if (++mod_w >= MOD_N)
        mod_w = 0;
}

/* a send effect's wet signal: the bus in, the processed bus out, before the return level. PARAM is the effect's own control */
static void mx_wet(int type, mfxs_t *s, float *x, int n, int par)
{
    int i;
    float p = (float)par * (1.0f / 127.0f);
    switch (type) {
    case MX_DRIVE: {                                     /* a soft clip, levelled so a signal of 0.4 comes out at 0.4 whatever the drive */
        float gain = 1.0f + p * 14.0f, ref = 0.4f * gain, norm = 0.4f * (1.0f + ref) / ref;
        for (i = 0; i < n; i++) {
            float y = x[i] * gain;
            x[i] = y / (1.0f + fm_fabsf(y)) * norm;
        }
        break;
    }
    case MX_DIST: {                                      /* a hard clip: flat tops, a lot more bite than the drive */
        float gain = 2.0f + p * 38.0f;
        for (i = 0; i < n; i++) {
            float y = x[i] * gain;
            x[i] = (y > 1.0f ? 1.0f : y < -1.0f ? -1.0f : y) * 0.45f;
        }
        break;
    }
    case MX_FUZZ: {                                      /* lopsided clipping (the two halves clip at different heights), fizzy; the offset that makes is taken out again */
        float gain = 4.0f + p * 60.0f;
        for (i = 0; i < n; i++) {
            float y = x[i] * gain;
            y = y > 0.8f ? 0.8f : y < -0.3f ? -0.3f : y;
            s->a += 0.002f * (y - s->a);                 /* its DC: a one-pole low-pass */
            x[i] = (y - s->a) * 0.6f;
        }
        break;
    }
    case MX_WAVE: {                                      /* a wavefolder: the wave bends back on itself instead of clipping */
        float k = 0.15f + p * 1.6f;
        for (i = 0; i < n; i++) {
            float t = x[i] * k;
            t -= fm_floorf(t);
            x[i] = fm_sin_turns(t) * 0.8f;
        }
        break;
    }
    case MX_CRUSH: {                                     /* fewer samples and fewer levels as PARAM goes up */
        int hold = 1 + (int)par * 14 / 127;
        float levels = 4.0f + (float)(127 - par) * (60.0f / 127.0f);
        for (i = 0; i < n; i++) {
            if (s->cnt-- <= 0) {
                s->cnt = hold - 1;
                s->hold = fm_floorf(x[i] * levels + 0.5f) * (1.0f / levels);
            }
            x[i] = s->hold;
        }
        break;
    }
    case MX_DECIM: {                                     /* a sample-rate reducer: hold every k-th sample, no bit loss */
        int hold = 1 + (int)par * 31 / 127;
        for (i = 0; i < n; i++) {
            if (s->cnt-- <= 0) {
                s->cnt = hold - 1;
                s->hold = x[i];
            }
            x[i] = s->hold;
        }
        break;
    }
    case MX_RING: {                                      /* a carrier through the signal: bells and metal */
        float inc = 40.0f * fm_exp2f((float)par * (5.64f / 127.0f)) * (1.0f / 44100.0f);
        for (i = 0; i < n; i++) {
            s->b += inc;
            s->b -= fm_floorf(s->b);
            x[i] *= fm_sin_turns(s->b) * 1.4f;
        }
        break;
    }
    case MX_CHORUS: {                                    /* a copy a few ms late, its time wandering (0.2 .. 5 Hz): the voice doubles and goes a little out of tune */
        float inc = 0.2f * fm_exp2f(p * 4.64f) * (1.0f / 44100.0f);
        for (i = 0; i < n; i++) {
            float o = mod_read(662.0f + 176.0f * lfo(&s->a, inc));
            mod_put(x[i]);
            x[i] = o;
        }
        break;
    }
    case MX_FLANGER: {                                   /* a fed-back copy 0.1 .. 5 ms late, swept (0.05 .. 2 Hz): the dry and the copy cancel at notches that move, a jet */
        float inc = 0.05f * fm_exp2f(p * 5.3f) * (1.0f / 44100.0f);
        for (i = 0; i < n; i++) {
            float o = mod_read(112.0f + 108.0f * lfo(&s->a, inc));
            mod_put(x[i] + o * 0.6f);
            x[i] = (x[i] + o) * 0.5f;
        }
        break;
    }
    case MX_PHASER: {                                    /* four allpass stages whose break point sweeps (0.1 .. 4 Hz): notches that move, a softer sweep than the flanger */
        float inc = 0.1f * fm_exp2f(p * 5.3f) * (1.0f / 44100.0f);
        for (i = 0; i < n; i++) {
            float a = 0.6f * lfo(&s->hold, inc), y = x[i];
            y = allpass(y, a, &s->a);
            y = allpass(y, a, &s->b);
            y = allpass(y, a, &s->c);
            y = allpass(y, a, &s->d);
            x[i] = (x[i] + y) * 0.5f;
        }
        break;
    }
    case MX_TREMOLO: {                                   /* the level swings with a sine (0.5 .. 12 Hz), all the way down */
        float inc = 0.5f * fm_exp2f(p * 4.58f) * (1.0f / 44100.0f);
        for (i = 0; i < n; i++)
            x[i] *= 0.5f + 0.5f * lfo(&s->a, inc);
        break;
    }
    case MX_COMB: {                                      /* a resonator: the input rings at a pitch, PARAM sets it (40 Hz .. 2 kHz) */
        float d = 44100.0f / (40.0f * fm_exp2f(p * 5.64f));
        for (i = 0; i < n; i++) {
            float y = x[i] + 0.7f * mod_read(d);
            mod_put(y);
            x[i] = y * 0.25f;
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

static void delay_rev(int type, float *x, int n, const uint8_t v[2], float spl)
{
    int i;
    if (type == MX_DELAY) {
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
    } else if (type == MX_REVERB) {
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

/* an effect that was switched off starts from silence next time */
void mx_idle(int type)
{
    if (type == MX_DELAY)
        dly_on = 0;
    else if (type == MX_REVERB)
        rev_on = 0;
    else if (MX_LINE(type) == LN_MOD)
        mod_on = 0;
}

void mx_run(int type, mfxs_t *s, float *x, int n, int amt, int par, float spl)
{
    if (type == MX_DELAY || type == MX_REVERB) {         /* AMT is their mix: the echo / the room's own return level */
        uint8_t v[2];
        v[0] = (uint8_t)amt;
        v[1] = (uint8_t)par;
        delay_rev(type, x, n, v, spl);
    } else {
        float g = (float)amt * (1.0f / 127.0f);
        int i;
        if (MX_LINE(type) == LN_MOD && !mod_on) {        /* a fresh start: no old copies */
            memset(mod_ring, 0, sizeof mod_ring);
            mod_w = 0;
            mod_on = 1;
        }
        mx_wet(type, s, x, n, par);
        for (i = 0; i < n; i++)
            x[i] *= g;
    }
}
