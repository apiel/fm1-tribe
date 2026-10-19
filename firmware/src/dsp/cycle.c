/* SPDX-License-Identifier: GPL-3.0-only */
/* CYCLE: see cycle.h. The generator is the KickWave's (kicks.c's wave_sample, before the table). */
#include "cycle.h"
#include "fastmath.h"

static inline float lerp(float a, float b, float t) { return a + t * (b - a); }

float cycle_shape(float ph, const cycle_p *c)
{
    float p = ph - fm_floorf(ph), sk = c->skew < 0.05f ? 0.05f : c->skew > 0.95f ? 0.95f : c->skew, w, s, sn, tri, saw, sq, sh = c->wave;
    w = p < sk ? 0.5f * (p / sk) : 0.5f + 0.5f * ((p - sk) / (1.0f - sk));
    sn = fm_sin_turns(w);
    tri = w < 0.5f ? 4.0f * w - 1.0f : 3.0f - 4.0f * w;
    saw = 1.0f - 2.0f * w;
    sq = w < 0.5f ? 1.0f : -1.0f;
    if (sh <= 0.3333f)
        s = lerp(sn, tri, sh / 0.3333f);
    else if (sh <= 0.6666f)
        s = lerp(tri, saw, (sh - 0.3333f) / 0.3333f);
    else
        s = lerp(saw, sq, (sh - 0.6666f) / 0.3333f);
    if (fm_fabsf(c->harm2) > 0.001f)
        s += c->harm2 * fm_sin_turns(w * 2.0f);
    if (fm_fabsf(c->harm3) > 0.001f)
        s += c->harm3 * fm_sin_turns(w * 3.0f);
    if (c->fold > 0.001f)
        s = fm_sinf(s * (1.0f + c->fold * 3.5f) * 1.57079632679f);
    return s < -1.0f ? -1.0f : s > 1.0f ? 1.0f : s;
}

void cycle_build(float *t, const cycle_p *c)
{
    int i;
    for (i = 0; i < CYCLE_N; i++)
        t[i] = cycle_shape((float)i * (1.0f / (float)CYCLE_N), c);
    t[CYCLE_N] = t[0];
}
