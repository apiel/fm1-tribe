/* SPDX-License-Identifier: GPL-3.0-only */
/* KickWave and PotKick from zicBox (audio/engines/KickWave.h, PotKick.h), ported to C for the FM-1:
 * float only, fastmath.h instead of libm, the exponentials' coefficients computed when a pot moves
 * (not per sample), the pitch envelope kept as a tribeying exponential (pow(env, k) = exp(-k t)). */
#include <stdint.h>
#include "mem.h"
#include "kicks.h"
#include "../dsp/fastmath.h"

#define SR 44100.0f
#define NK 3                                  /* instances of each */
#define KICK_TRIM 0.25f                       /* zicBox's drive leaves the body at +-3: its output stage clips; ours keeps headroom */

/* ---- parameter tables: pot 0..127 -> min..max, linear or exponential ---- */
typedef struct { const char *name; float mn, mx, def; uint8_t expo; } kp_t;

static const kp_t KW[] = {                    /* KickWave, 14 */
    {"PchShp", 0, 100, 50, 0}, {"Freq", 30, 100, 52, 0}, {"Dur", 50, 1500, 350, 1}, {"FM", 0, 100, 35, 0},
    {"Drive", 0, 100, 35, 0}, {"Wave", 0, 100, 0, 0}, {"Harm2", -100, 100, 0, 0}, {"Harm3", -100, 100, 0, 0},
    {"Skew", 5, 95, 50, 0}, {"Fold", 0, 100, 0, 0}, {"Phase", 0, 100, 0, 0}, {"Reson", 0, 100, 0, 0},
    {"Crush", 0, 100, 0, 0}, {"Curve", 0, 100, 50, 0},
};
static const kp_t KP[] = {                    /* PotKick, 15 */
    {"Freq", 30, 100, 52, 0}, {"Dur", 50, 1500, 350, 1}, {"Click", 0, 100, 40, 0}, {"ClkDec", 1, 100, 10, 1},
    {"Morph", 0, 100, 0, 0}, {"SwpShp", 0, 100, 50, 0}, {"SwpDpt", 0, 100, 50, 0}, {"FMDpt", 0, 100, 25, 0},
    {"FMRat", 0.5f, 8, 1.5f, 0}, {"FMSnap", 2, 150, 25, 1}, {"Drive", 0, 100, 35, 0}, {"Wfold", 0, 100, 0, 0},
    {"Reson", 0, 100, 0, 0}, {"Crush", 0, 100, 0, 0}, {"Boost", 0, 100, 0, 0},
};
#define NKW ((int)(sizeof KW / sizeof KW[0]))
#define NKP ((int)(sizeof KP / sizeof KP[0]))

static x0x_param_t KWU[sizeof KW / sizeof KW[0]], KPU[sizeof KP / sizeof KP[0]];

static int names_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return !*a && !*b;
}

/* units of the pots, by name */
static char *put_u(char *b, int v)
{
    char t[12];
    int n = 0, neg = v < 0;
    if (neg) { *b++ = '-'; v = -v; }
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *b++ = t[--n];
    return b;
}
static float unmap(const kp_t *p, int pot)
{
    float u = (float)pot * (1.0f / 127.0f);
    if (p->mn < 0.0f)                              /* bipolar: pot 64 is exactly the middle */
        return (float)(pot - 64) / (pot >= 64 ? 63.0f : 64.0f) * p->mx;
    return p->expo ? p->mn * fm_powf(p->mx / p->mn, u) : p->mn + u * (p->mx - p->mn);
}
static int topot(const kp_t *p)
{
    float u = p->expo ? fm_logf(p->def / p->mn) / fm_logf(p->mx / p->mn) : (p->def - p->mn) / (p->mx - p->mn);
    int v = (int)(u * 127.0f + 0.5f);
    if (p->mn < 0.0f)
        v = p->def >= 0.0f ? 64 + (int)(p->def / p->mx * 63.0f + 0.5f) : 64 + (int)(p->def / p->mx * 64.0f - 0.5f);
    return v < 0 ? 0 : v > 127 ? 127 : v;
}

/* ---- the amplitude curves (EnvelopDrumAmp): seven shapes, morphed by Curve ---- */
static float shape_at(int k, float u)         /* u = what is left: 1 at the hit, 0 at the end */
{
    float x;
    switch (k) {
    case 0: x = u * u; return x * x * u;
    case 1: return u * u * u;
    case 2: return u;
    case 3: x = 1.0f - u; return 1.0f - x * x;
    case 4: return fm_sqrtf(u);
    case 5: return fm_sinf(u * 1.57079632679f);
    default: x = 1.0f - u; return 1.0f - x * x * x;
    }
}

typedef struct {
    uint8_t used, potkick;
    uint8_t pot[16];
    /* decoded */
    float freq, dur_samples, fm, drive, wave, h2, h3, skew, fold, phase, reson, crush, curve, pchshp;
    float click, clkdec, morph, swpshp, swpdpt, fmrat, fmsnap, wfold, boost;
    float c_mod, c_fm, c_click;               /* per-sample tribey coefficients */
    /* voice */
    float vel, pitch_mult, cph, mph, menv, fmenv, clickenv, comp, svf_lp, svf_bp, fold_buf;
    float crush_ph, crush_hold, boost_x, boost_y;
    uint32_t noise;
    int32_t n, total;
} kick_t;

static kick_t K[2][NK];

static void decode(kick_t *k)
{
    const kp_t *t = k->potkick ? KP : KW;
    float v[16];
    int i, np = k->potkick ? NKP : NKW;
    for (i = 0; i < np; i++)
        v[i] = unmap(&t[i], k->pot[i]);
    if (!k->potkick) {
        k->pchshp = v[0] * 0.01f; k->freq = v[1]; k->dur_samples = v[2] * 0.001f * SR; k->fm = v[3] * 0.01f;
        k->drive = v[4] * 0.01f; k->wave = v[5] * 0.01f; k->h2 = v[6] * 0.01f; k->h3 = v[7] * 0.01f;
        k->skew = v[8] * 0.01f; k->fold = v[9] * 0.01f; k->phase = v[10] * 0.01f; k->reson = v[11] * 0.01f;
        k->crush = v[12] * 0.01f; k->curve = v[13] * 0.01f;
        /* pitchEnv = menv^(1 + shape*5), menv tribeying with a 25 ms constant */
        k->c_mod = fm_expf(-(1.0f + k->pchshp * 5.0f) / (SR * 0.025f));
    } else {
        k->freq = v[0]; k->dur_samples = v[1] * 0.001f * SR; k->click = 0.0f;        /* no click: v[2], v[3] stay as hidden pots */
        k->clkdec = v[3] * 0.001f; k->morph = v[4] * 0.01f; k->swpshp = v[5] * 0.01f; k->swpdpt = v[6] * 0.01f;
        k->fm = v[7] * 0.01f; k->fmrat = v[8]; k->fmsnap = v[9] * 0.001f; k->drive = v[10] * 0.01f;
        k->wfold = v[11] * 0.01f; k->reson = v[12] * 0.01f; k->crush = v[13] * 0.01f; k->boost = v[14] * 0.01f;
        k->c_mod = fm_expf(-1.0f / (SR * (0.005f + k->swpdpt * 0.060f)));
        k->c_fm = fm_expf(-1.0f / (SR * k->fmsnap));
        k->c_click = fm_expf(-1.0f / (SR * (k->clkdec < 0.001f ? 0.001f : k->clkdec > 0.2f ? 0.2f : k->clkdec)));
    }
}

static int prepped;
static void prep(void)                        /* the descriptors, built once (project defaults need them before the engine runs) */
{
    int i;
    if (prepped)
        return;
    prepped = 1;
    for (i = 0; i < NKW; i++) { KWU[i].name = KW[i].name; KWU[i].max = 127; KWU[i].def = (uint8_t)topot(&KW[i]); KWU[i].names = 0; }
    for (i = 0; i < NKP; i++) { KPU[i].name = KP[i].name; KPU[i].max = 127; KPU[i].def = (uint8_t)topot(&KP[i]); KPU[i].names = 0; }
}

void kicks_init(void)
{
    prep();
    memset(K, 0, sizeof K);
}

int kick_alloc(int potkick)
{
    int i;
    prep();
    for (i = 0; i < NK; i++)
        if (!K[potkick][i].used) {
            kick_t *k = &K[potkick][i];
            int j, np = potkick ? NKP : NKW;
            memset(k, 0, sizeof *k);
            k->used = 1;
            k->potkick = (uint8_t)potkick;
            k->noise = 34567u;
            k->n = k->total = 0;
            for (j = 0; j < np; j++)
                k->pot[j] = (potkick ? KPU : KWU)[j].def;
            decode(k);
            return potkick * NK + i;
        }
    return -1;
}
void kick_free(int inst) { K[inst / NK][inst % NK].used = 0; K[inst / NK][inst % NK].n = K[inst / NK][inst % NK].total = 0; }
int kick_nparams(int potkick) { return potkick ? NKP : NKW; }
const x0x_param_t *kick_param(int potkick, int i) { prep(); return potkick ? &KPU[i] : &KWU[i]; }

void kick_set(int inst, int i, int pot)
{
    kick_t *k = &K[inst / NK][inst % NK];
    if (i < 0 || i >= (k->potkick ? NKP : NKW))
        return;
    k->pot[i] = (uint8_t)(pot < 0 ? 0 : pot > 127 ? 127 : pot);
    decode(k);
}

void kick_trigger(int inst, int note, float vel)
{
    kick_t *k = &K[inst / NK][inst % NK];
    k->vel = vel;
    k->pitch_mult = fm_exp2f((float)(note - 60) * (1.0f / 12.0f));
    k->svf_lp = k->svf_bp = 0.0f;
    k->cph = k->mph = 0.0f;
    k->menv = 1.0f;
    k->fmenv = 1.0f;
    k->clickenv = k->potkick ? 1.0f : 0.0f;
    k->comp = 0.0f;
    k->crush_ph = k->crush_hold = 0.0f;
    k->n = 0;
    k->total = (int32_t)k->dur_samples;
    if (k->total < 1)
        k->total = 1;
}

static inline float lerpf(float a, float b, float t) { return a + t * (b - a); }

static float amp_env(const kick_t *k)
{
    float t = (float)k->n / (float)k->total, u = 1.0f - t, pos = k->curve * 6.0f, f;
    int idx = (int)pos;
    if (idx > 5)
        idx = 5;
    f = pos - (float)idx;
    return lerpf(shape_at(idx, u), shape_at(idx + 1, u), f);
}

static inline float resonator(kick_t *k, float in, float hz, float amt)
{
    float f, q, hp, peak;
    if (amt < 0.001f)
        return in;
    hz = hz < 30.0f ? 30.0f : hz > 3500.0f ? 3500.0f : hz;
    f = 2.0f * fm_sinf(FM_PI * hz * (1.0f / SR));
    q = 1.0f - amt * 0.96f;
    k->svf_lp += f * k->svf_bp;
    hp = in - k->svf_lp - q * k->svf_bp;
    k->svf_bp += f * hp;
    peak = fm_tanhf((k->svf_lp + k->svf_bp * 1.8f) * (1.0f + amt * 2.5f));
    return lerpf(in, peak, fm_minf(amt * 1.25f, 1.0f));
}

/* zicBox applyDrive: a tanh push, scaled up again; the low end blends in */
static inline float drive_fx(float x, float amount)
{
    float g, sat, d;
    if (amount <= 0.0f)
        return x;
    g = 1.0f + amount * 5.0f;
    sat = fm_tanhf(x * g);
    d = sat * g;
    return amount < 0.3f ? x + (d - x) * (amount / 0.3f) : d;
}

/* zicBox applyCompression2 at intensity 0.65 */
static inline float glue(float x, float *env)
{
    const float ci = 0.4225f;                 /* 0.65^2 */
    float a = fm_fabsf(x), thr = 1.0f - ci * 0.7f, ratio = 1.0f + ci * 4.0f, out = x;
    *env = 0.999f * *env + 0.001f * a;
    if (*env > thr)
        out *= (thr + (*env - thr) / ratio) / *env;
    return out * (1.0f + ci * 0.4f);
}

/* ---------------------------------------------------------------- KickWave --- */
static float wave_sample(const kick_t *k, float ph)
{
    float p = ph + k->phase, sk = k->skew < 0.05f ? 0.05f : k->skew > 0.95f ? 0.95f : k->skew, w, s, sn, tri, saw, sq, sh = k->wave;
    p -= fm_floorf(p);
    w = p < sk ? 0.5f * (p / sk) : 0.5f + 0.5f * ((p - sk) / (1.0f - sk));
    sn = fm_sin_turns(w);
    tri = w < 0.5f ? 4.0f * w - 1.0f : 3.0f - 4.0f * w;
    saw = 1.0f - 2.0f * w;
    sq = w < 0.5f ? 1.0f : -1.0f;
    if (sh <= 0.3333f)
        s = lerpf(sn, tri, sh / 0.3333f);
    else if (sh <= 0.6666f)
        s = lerpf(tri, saw, (sh - 0.3333f) / 0.3333f);
    else
        s = lerpf(saw, sq, (sh - 0.6666f) / 0.3333f);
    if (fm_fabsf(k->h2) > 0.001f)
        s += k->h2 * fm_sin_turns(w * 2.0f);
    if (fm_fabsf(k->h3) > 0.001f)
        s += k->h3 * fm_sin_turns(w * 3.0f);
    if (k->fold > 0.001f)
        s = fm_sinf(s * (1.0f + k->fold * 3.5f) * 1.57079632679f);
    return s < -1.0f ? -1.0f : s > 1.0f ? 1.0f : s;
}

static void render_wave(kick_t *k, float *out, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        float env = k->n < k->total ? amp_env(k) : 0.0f, body = 0.0f, o;
        if (k->n < k->total)
            k->n++;
        if (env > 0.0001f) {
            float pe, root = k->freq * k->pitch_mult, cur, ms, sig;
            k->menv *= k->c_mod;                 /* already raised to 1 + shape*5 */
            pe = k->menv;
            cur = root * (1.0f + pe * 2.5f);
            k->mph += cur * 1.5f * (1.0f / SR);
            k->mph -= fm_floorf(k->mph);
            ms = fm_sin_turns(k->mph);
            k->cph += cur * (1.0f / SR) + ms * (k->fm * 0.75f * pe * 20.0f) * (1.0f / 2048.0f);
            k->cph -= fm_floorf(k->cph);
            sig = wave_sample(k, k->cph);
            sig = resonator(k, sig, cur, k->reson);
            body = sig * env;
        }
        o = body;
        if (k->drive > 0.0f)
            o = drive_fx(o, k->drive * 3.0f);
        o = glue(o, &k->comp);
        if (k->crush > 0.0f) {
            float dec = 1.0f + k->crush * 15.0f;
            k->crush_ph += 1.0f;
            if (k->crush_ph >= dec) {
                k->crush_ph -= dec;
                k->crush_hold = o;
            }
            o = k->crush_hold;
        }
        out[i] = o * k->vel * KICK_TRIM;
    }
}

/* ---------------------------------------------------------------- PotKick --- */
static float vco(float ph, float morph)
{
    float s = fm_sin_turns(ph), pos, t, saw, tri, oct, sq, np, fs;
    int idx;
    if (morph <= 0.0f)
        return s;
    saw = 2.0f * (ph - fm_floorf(ph + 0.5f));
    if (morph >= 1.0f)
        return fm_tanhf(saw * 2.5f);
    pos = morph * 7.0f;
    idx = (int)pos;
    t = pos - (float)idx;
    tri = 2.0f * fm_fabsf(saw) - 1.0f;
    oct = (s + 0.4f * fm_sin_turns(ph * 2.0f)) * 0.714f;
    sq = s > 0.0f ? 0.75f : -0.75f;
    np = (ph - fm_floorf(ph)) < 0.20f ? 0.75f : -0.25f;
    fs = fm_sinf(s * 2.5f);
    switch (idx) {
    case 0: return lerpf(s, tri, t);
    case 1: return lerpf(tri, oct, t);
    case 2: return lerpf(oct, saw, t);
    case 3: return lerpf(saw, sq, t);
    case 4: return lerpf(sq, np, t);
    case 5: return lerpf(np, fs, t);
    default: return lerpf(fs, fm_tanhf(saw * 2.5f), t > 1.0f ? 1.0f : t);
    }
}

static float shaped_pitch(float p, float sh)
{
    float t, sc, dip, sub;
    p = p < 0.0f ? 0.0f : p > 1.0f ? 1.0f : p;
    sc = p * p * (3.0f - 2.0f * p);
    if (sh < 0.20f)
        return lerpf(fm_sqrtf(p), p, sh * 5.0f);
    if (sh < 0.40f)
        return lerpf(p, p * p, (sh - 0.20f) * 5.0f);
    if (sh < 0.65f)
        return lerpf(p * p, sc, (sh - 0.40f) / 0.25f);
    dip = sc - 0.35f * fm_sin_pi(FM_PI * p) * (1.0f - p * p);
    if (sh < 0.85f)
        return lerpf(sc, dip, (sh - 0.65f) / 0.20f);
    t = (sh - 0.85f) / 0.15f;
    sub = p * p * p + 0.4f * fm_sqrtf(p) * (1.0f - p) * (1.0f - p);
    return lerpf(dip, sub, t);
}

static float gabber(kick_t *k, float sig, float amt)
{
    float fa, drv, biased, folded, sat, res, o, t;
    if (amt < 0.001f)
        return sig;
    fa = amt <= 0.70f ? amt : 0.70f + (amt - 0.70f) * 0.25f;
    drv = 1.0f + amt * 5.5f;
    biased = sig * drv + amt * 0.15f;
    folded = fm_sinf(biased * (1.0f + fa * 1.2f));
    sat = fm_tanhf(folded * 1.3f);
    res = lerpf(sig, sat, amt * 0.85f);
    o = fm_tanhf(res * 1.1f);
    if (amt > 0.70f) {
        t = (amt - 0.70f) / 0.30f;
        k->fold_buf += (1.0f - t * 0.18f) * (o - k->fold_buf);
        o = k->fold_buf;
    } else
        k->fold_buf = o;
    return o;
}

static void render_pot(kick_t *k, float *out, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        float env = k->n < k->total ? amp_env(k) : 0.0f, body = 0.0f, o;
        if (k->n < k->total)
            k->n++;
        if (env > 0.0001f) {
            float root = k->freq * k->pitch_mult, pm, rf, ms, sig;
            k->menv *= k->c_mod;
            pm = shaped_pitch(k->menv, k->swpshp);
            rf = root + pm * root * (k->swpdpt * 5.0f);
            k->fmenv *= k->c_fm;
            k->mph += rf * k->fmrat * (1.0f / SR);
            k->mph -= fm_floorf(k->mph);
            ms = fm_sin_turns(k->mph);
            k->cph += rf * (1.0f / SR) + ms * (k->fm * 0.75f * k->fmenv * 0.04f);
            k->cph -= fm_floorf(k->cph);
            sig = vco(k->cph, k->morph);
            sig = resonator(k, sig, rf, k->reson);
            body = sig * env;
        }
        o = body;
        if (k->wfold > 0.0f)
            o = gabber(k, o, k->wfold);
        if (k->drive > 0.0f)
            o = drive_fx(o, k->drive * 3.0f);
        if (k->boost > 0.0f) {
            float bf = 0.2f + 0.8f * k->boost, b = (1.0f - bf) * k->boost_y + bf * (o + k->boost_x) * 0.5f;
            k->boost_x = o;
            k->boost_y = b;
            o = b * (1.0f + k->boost * 2.0f);
        }
        o = glue(o, &k->comp);
        if (k->crush > 0.0f) {
            float dec = 1.0f + k->crush * 15.0f;
            k->crush_ph += 1.0f;
            if (k->crush_ph >= dec) {
                k->crush_ph -= dec;
                k->crush_hold = o;
            }
            o = k->crush_hold;
        }
        if (k->clickenv > 0.0001f) {
            k->clickenv *= k->c_click;
            k->noise = k->noise * 196314165u + 907633389u;
            o += ((float)(int32_t)k->noise / 2147483648.0f) * k->clickenv * k->click;
        }
        out[i] = o * k->vel * KICK_TRIM;
    }
}

void kick_render(int inst, float *out, int n)
{
    kick_t *k = &K[inst / NK][inst % NK];
    if (k->n >= k->total && k->clickenv < 0.0001f) {
        k->crush_hold = 0.0f;
        memset(out, 0, (unsigned)n * sizeof(float));      /* silent: costs nothing */
        return;
    }
    if (k->potkick)
        render_pot(k, out, n);
    else
        render_wave(k, out, n);
}

void kick_format(int potkick, int i, int pot, char *buf)
{
    const kp_t *t = potkick ? KP : KW;
    const char *n = t[i].name, *u = "%";
    float v = unmap(&t[i], pot);
    char *q = buf;
    if (names_eq(n, "Freq")) u = "Hz";
    else if (names_eq(n, "Dur") || names_eq(n, "ClkDec") || names_eq(n, "FMSnap")) u = "ms";
    else if (names_eq(n, "FMRat")) {
        int x4 = (int)(v * 4.0f + 0.5f);                 /* quarters */
        q = put_u(q, x4 / 4);
        if (x4 & 3) { *q++ = '.'; *q++ = (char)('0' + (x4 & 3) * 25 / 10); if (((x4 & 3) * 25) % 10) *q++ = (char)('0' + ((x4 & 3) * 25) % 10); }
        *q++ = 'x';
        *q = 0;
        return;
    }
    q = put_u(q, (int)(v < 0 ? v - 0.5f : v + 0.5f));
    while (*u) *q++ = *u++;
    *q = 0;
}
