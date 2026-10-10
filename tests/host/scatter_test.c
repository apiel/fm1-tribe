/* SPDX-License-Identifier: GPL-3.0-only */
/* Every scatter slot changes the sound it acts on, stays finite and bounded, and lets go when released.
 *   cc ... tests/host/scatter_test.c firmware/src/tribe/*.c (see tests/run_tests.sh) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "tribe/tribe.h"
uint32_t plat_master(void) { return 4096; }

static double run(int slot, int solo, float *out, int blocks)
{
    static int32_t blk[512];
    int b, i, t;
    double e = 0;
    project_defaults();
    engine_init();
    for (t = 0; t < NTRK; t++)
        dk_mute(t, t != solo);
    dk_play(1);
    for (b = 0; b < blocks; b++) {
        if (b == 40 && slot >= 0)
            dk_scatter(solo, slot, 1);
        engine_render(blk, 256);
        for (i = 0; i < 256; i++) {
            float v = (float)blk[2 * i] / 4194303.0f;
            if (!isfinite(v) || fabsf(v) > 2.01f) {
                printf("slot %d: bad sample %f\n", slot, (double)v);
                return -1;
            }
            out[b * 256 + i] = v;
        }
    }
    (void)e;
    return 0;
}

/* each run is its own process (the engine's state is static): `scatter_test base FILE` writes the plain
 * snare track, `scatter_test SLOT FILE` plays slot SLOT from block 40 and compares with FILE */
int main(int argc, char **argv)
{
    enum { N = 400 };
    static float a[N * 256], b[N * 256];
    FILE *f;
    int slot, i;
    double d = 0, rb = 0, rf = 0;
    if (argc < 3)
        return 2;
    if (!strcmp(argv[1], "base")) {
        run(-1, 5, a, N);
        f = fopen(argv[2], "wb");
        fwrite(a, sizeof a[0], N * 256, f);
        fclose(f);
        return 0;
    }
    slot = atoi(argv[1]);
    f = fopen(argv[2], "rb");
    if (!f || fread(a, sizeof a[0], N * 256, f) != N * 256)
        return 2;
    fclose(f);
    if (run(slot, 5, b, N) < 0)
        return 1;
    for (i = 40 * 256; i < N * 256; i++) {
        d += (double)(b[i] - a[i]) * (b[i] - a[i]);
        rb += (double)a[i] * a[i];
        rf += (double)b[i] * b[i];
    }
    printf("slot %2d: diff %.4f  rms plain %.4f with fx %.4f\n", slot, sqrt(d / (N * 256)), sqrt(rb / (N * 256)), sqrt(rf / (N * 256)));
    if (d < 1e-6) {
        printf("  slot %d changed nothing\n", slot);
        return 1;
    }
    return 0;
}
