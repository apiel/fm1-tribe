/* SPDX-License-Identifier: GPL-3.0-only */
/* SVF: see svf.h. */
#include "svf.h"
#include "fastmath.h"

void svf_init(svf_t *f)
{
    f->ic1 = 0.0f;
    f->ic2 = 0.0f;
    f->k = 1.0f;
    f->kg = 1.0f;
    f->a1 = 0.0f;
    f->a2 = 0.0f;
    f->a3 = 0.0f;
}

void svf_set(svf_t *f, float fc, float k, float sr)
{
    float g;
    if (fc < 20.0f)
        fc = 20.0f;
    if (fc > 0.45f * sr)
        fc = 0.45f * sr;
    g = fm_tanf(FM_PI * fc / sr);                  /* fc <= 0.45 sr keeps the argument inside (-pi/2, pi/2) */
    f->k = k;
    f->kg = fm_sqrtf(k);
    f->a1 = 1.0f / (1.0f + g * (g + k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}

void svf_tick(svf_t *f, float x, float *lp, float *bp, float *hp)
{
    float v3 = x - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    *lp = v2;
    *bp = v1 * f->kg;
    *hp = x - f->k * v1 - v2;
}
