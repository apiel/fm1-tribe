/* SPDX-License-Identifier: GPL-3.0-only */
/* Scatter FX: momentary effects played from the second row of keys. Sixteen slots: the first eight
 * act on the selected track, the last eight on the whole mix. Designed for the FM-1, not copied from
 * zicBox's Scatter: mono, one effect per scope at a time, no reverb, no big buffers. The loop
 * effects keep one slice of audio as 16-bit samples (32 KiB); everything else is a few multiplies a
 * sample. The slice lengths follow the tempo (a step is a 16th). */
#include "tribe.h"
#include "scatter.h"
#include "../dsp/fastmath.h"

enum { SK_NONE, SK_REP, SK_REV, SK_GATE, SK_CRUSH, SK_DIST, SK_RING, SK_LPS, SK_HPS };
typedef struct { uint8_t kind, arg; const char *name; } scdef_t;
/* arg: loops = slice length in quarter steps */
static const scdef_t SC[SC_SLOTS] = {
    {SK_REP, 2, "1/32"}, {SK_REP, 4, "1/16"}, {SK_REP, 8, "1/8"}, {SK_REV, 4, "REV"},
    {SK_GATE, 0, "GATE"}, {SK_CRUSH, 0, "CRSH"}, {SK_DIST, 0, "DIST"}, {SK_RING, 0, "RING"},
    {SK_REP, 2, "1/32"}, {SK_REP, 4, "1/16"}, {SK_REP, 8, "1/8"}, {SK_REV, 8, "REV"},
    {SK_GATE, 0, "GATE"}, {SK_CRUSH, 0, "CRSH"}, {SK_LPS, 0, "LPF"}, {SK_HPS, 0, "HPF"},
};
const char *sc_name(int slot) { return SC[slot & 15].name; }
int sc_needs_ring(int slot) { return SC[slot & 15].kind == SK_REP || SC[slot & 15].kind == SK_REV; }

#define SC_SCALE 8000.0f

void sc_start(sc_t *s, int slot, int16_t *ring, float spl)
{
    const scdef_t *d = &SC[slot & 15];
    int32_t n = (int32_t)(spl * (float)d->arg * 0.25f);
    s->kind = d->kind;
    s->ring = ring;
    s->slice = n < 64 ? 64 : n > SC_RING ? SC_RING : n;
    s->pos = 0;
    s->play = 0;
    s->ph = 0.0f;
    s->g = 1.0f;
    s->lp = 0.0f;
    s->t = 0;
    s->hold = 0.0f;
    s->cnt = 0;
    s->a = s->kind == SK_LPS ? 0.9f : 0.004f;
}

static inline int16_t to16(float x)
{
    x *= SC_SCALE;
    return (int16_t)(x > 32000.0f ? 32000 : x < -32000.0f ? -32000 : (int32_t)x);
}

/* in place, mono; spl = samples a step lasts */
void sc_run(sc_t *s, float *x, int n, float spl)
{
    int i;
    switch (s->kind) {
    case SK_REP: case SK_REV:
        for (i = 0; i < n; i++) {
            if (!s->play) {                         /* the first slice passes while it is kept */
                s->ring[s->pos++] = to16(x[i]);
                if (s->pos >= s->slice) {
                    s->play = 1;
                    s->pos = s->kind == SK_REV ? s->slice - 1 : 0;
                }
            } else if (s->kind == SK_REP) {
                x[i] = (float)s->ring[s->pos] * (1.0f / SC_SCALE);
                if (++s->pos >= s->slice)
                    s->pos = 0;
            } else {
                x[i] = (float)s->ring[s->pos] * (1.0f / SC_SCALE);
                if (--s->pos < 0)
                    s->pos = s->slice - 1;
            }
        }
        break;
    case SK_GATE: {
        int32_t per = (int32_t)spl;
        for (i = 0; i < n; i++) {
            float tgt = (s->t % per) < per / 2 ? 1.0f : 0.0f;
            s->g += (tgt - s->g) * 0.02f;
            s->t++;
            x[i] *= s->g;
        }
        break;
    }
    case SK_CRUSH:
        for (i = 0; i < n; i++) {
            if (s->cnt-- <= 0) {
                s->cnt = 5;
                s->hold = fm_floorf(x[i] * 12.0f + 0.5f) * (1.0f / 12.0f);
            }
            x[i] = s->hold;
        }
        break;
    case SK_DIST:
        for (i = 0; i < n; i++) {
            float y = x[i] * 5.0f;
            x[i] = y / (1.0f + fm_fabsf(y)) * 1.3f;
        }
        break;
    case SK_RING:
        for (i = 0; i < n; i++) {
            s->ph += 190.0f * (1.0f / 44100.0f);
            s->ph -= fm_floorf(s->ph);
            x[i] *= fm_sin_turns(s->ph);
        }
        break;
    case SK_LPS:                                    /* the low-pass closes over ~1 s and stays there */
        for (i = 0; i < n; i++) {
            s->lp += s->a * (x[i] - s->lp);
            x[i] = s->lp;
            if (s->a > 0.012f)
                s->a *= 0.99985f;
        }
        break;
    case SK_HPS:                                    /* the high-pass opens over ~1 s */
        for (i = 0; i < n; i++) {
            s->lp += s->a * (x[i] - s->lp);
            x[i] -= s->lp;
            if (s->a < 0.4f)
                s->a *= 1.00015f;
        }
        break;
    }
}
