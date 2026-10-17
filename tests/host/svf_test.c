/* SPDX-License-Identifier: GPL-3.0-only */
/* SVF (svf.c), the shared filter: the low-pass passes DC and the high-pass blocks it, the band-pass peaks near unity at
 * its centre, the notch nulls at its centre, and the filter stays finite at the corners of its cutoff and damping. */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include "svf.h"

#define SR 44100.0f
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static double rms(const float *x, int n)
{
    double s = 0;
    int i;
    for (i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / n);
}

int main(void)
{
    static svf_t f;
    static float x[32768], lo[32768], ba[32768], hi[32768];
    float l, b, h;
    int i, c;
    double rin, rbp, rnotch, rlp, rhp;

    svf_init(&f);                                          /* DC: the low-pass is unity, the high-pass is nothing */
    svf_set(&f, 1000.0f, 0.5f, SR);
    for (i = 0; i < 4000; i++)
        svf_tick(&f, 1.0f, &l, &b, &h);
    CHECK(fabsf(l - 1.0f) < 1e-3f, "low-pass DC gain %f (want 1)", l);
    CHECK(fabsf(h) < 1e-3f, "high-pass DC gain %f (want 0)", h);

    for (c = 0; c < 2; c++) {                              /* a sine at the centre: the band-pass near 1, the notch near 0 */
        float fc = c ? 4000.0f : 1000.0f, k = c ? 0.2f : 0.5f;
        int n = 32768, settle = 16384;
        svf_init(&f);
        svf_set(&f, fc, k, SR);
        for (i = 0; i < n; i++) {
            x[i] = (float)sin(2.0 * 3.14159265358979 * fc * i / SR);
            svf_tick(&f, x[i], &lo[i], &ba[i], &hi[i]);
        }
        rin = rms(x + settle, n - settle);
        rbp = rms(ba + settle, n - settle);
        for (i = settle; i < n; i++)
            hi[i] = lo[i] + hi[i];                         /* the notch: low + high */
        rnotch = rms(hi + settle, n - settle);
        CHECK(fabs(rbp / rin - 1.0) < 0.05, "band-pass at its centre %.3f (want ~1), fc %.0f k %.2f", rbp / rin, fc, k);
        CHECK(rnotch / rin < 0.02, "notch at its centre %.4f (want ~0), fc %.0f k %.2f", rnotch / rin, fc, k);
    }

    svf_init(&f);                                          /* a sine below the cutoff passes the low-pass, not the high-pass */
    svf_set(&f, 2000.0f, 0.7f, SR);
    for (i = 0; i < 32768; i++) {
        x[i] = (float)sin(2.0 * 3.14159265358979 * 100.0 * i / SR);
        svf_tick(&f, x[i], &lo[i], &ba[i], &hi[i]);
    }
    rlp = rms(lo + 16384, 16384);
    rhp = rms(hi + 16384, 16384);
    rin = rms(x + 16384, 16384);
    CHECK(fabs(rlp / rin - 1.0) < 0.05, "low-pass below its cutoff %.3f (want ~1)", rlp / rin);
    CHECK(rhp / rin < 0.01, "high-pass below its cutoff %.4f (want ~0)", rhp / rin);

    {                                                      /* the corners: finite, and the cutoff clamp holds */
        uint32_t s = 12345u;
        float fcs[] = { -5.0f, 0.0f, 20.0f, 1000.0f, 19000.0f, 30000.0f };
        float ks[] = { 0.02f, 0.5f, 2.0f, 4.0f };
        int a, q;
        for (a = 0; a < 6; a++)
            for (q = 0; q < 4; q++) {
                svf_init(&f);
                svf_set(&f, fcs[a], ks[q], SR);
                for (i = 0; i < 4096; i++) {
                    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                    svf_tick(&f, (float)(int32_t)s * (1.0f / 2147483648.0f), &l, &b, &h);
                    if (!isfinite(l) || !isfinite(b) || !isfinite(h)) {
                        CHECK(0, "non-finite at fc %.0f k %.2f", fcs[a], ks[q]);
                        break;
                    }
                }
            }
    }

    printf(fails ? "svf: %d failure(s)\n" : "svf: all checks passed\n", fails);
    return fails != 0;
}
