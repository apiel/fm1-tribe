/* SPDX-License-Identifier: GPL-3.0-only */
/* The master effects and the volume rule: an effect at its neutral value, or limited to tracks that are silent, leaves the
 * 303 track's audio bit-for-bit as it was; otherwise it changes it, and the output stays finite.
 *   mfx_test base FILE                    the plain track
 *   mfx_test dj CUTOFF RESO same|diff FILE   the DJ filter (settings.dj) on the mix
 *   mfx_test vol KNOBKIND (0 = volume, 1 = anything else) MASTERVOL same|quieter FILE
 *   mfx_test send SLOT TYPE AMT PAR SEND same|diff FILE   FX 1 / FX 2 (SLOT 0 / 1) set to TYPE (MX_*); the 303 track's send to it
 *   mfx_test comp J V same|diff FILE            a compressor pot (settings.comp[J]) set to V
 *   mfx_test trans SEMITONES same|diff FILE   the 303 track transposed live (ARP > TRSP)
 *   mfx_test lfo SHAPE RATE DEPTH TARGET SYNC RETRIG same|diff FILE   the 303 clip's LFO */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "tribe/tribe.h"
uint32_t plat_master(void) { return 4096; }
#include "app/plat.h"
int plat_log_read(uint32_t s, uint32_t o, void *d, uint32_t n) { (void)s; (void)o; (void)d; (void)n; return -1; }
int plat_log_erase(uint32_t s) { (void)s; return -1; }
int plat_log_prog(uint32_t s, uint32_t o, const void *d, uint32_t n) { (void)s; (void)o; (void)d; (void)n; return -1; }

enum { N = 300 };
static int g_trans;
static void run(float *out)
{
    static int32_t blk[512];
    int b, i, t;
    engine_init();
    for (t = 0; t < NTRK; t++)
        dk_mute(t, t != 5);
    if (g_trans)
        dk_transpose(5, g_trans);
    dk_play(1);
    for (b = 0; b < N; b++) {
        engine_render(blk, 256);
        for (i = 0; i < 256; i++)
            out[b * 256 + i] = (float)blk[2 * i] / 4194303.0f;
    }
}

int main(int argc, char **argv)
{
    static float a[N * 256], b[N * 256];
    FILE *f;
    int i, bad = 0;
    double d = 0, ra = 0, rb = 0;
    project_defaults();
    {   int t;                                   /* the sends are closed by default; the send test opens the 303 track's */
        for (t = 0; t < NTRK; t++)
            proj.set.send[0][t] = proj.set.send[1][t] = 0;
    }
    if (argc < 3)
        return 2;
    if (!strcmp(argv[1], "base")) {
        run(a);
        f = fopen(argv[2], "wb");
        fwrite(a, sizeof a[0], N * 256, f);
        fclose(f);
        return 0;
    }
    if (!strcmp(argv[1], "comp")) {
        proj.set.comp[atoi(argv[2]) % 6] = (uint8_t)atoi(argv[3]);
    } else if (!strcmp(argv[1], "trans")) {
        g_trans = atoi(argv[2]);
    } else if (!strcmp(argv[1], "send")) {
        int sl = atoi(argv[2]) & 1;
        proj.set.fxtype[sl ^ 1] = MX_DRIVE;          /* the other slot is out of the way (and not the same delay / reverb) */
        proj.set.fxtype[sl] = (uint8_t)atoi(argv[3]);
        proj.set.fxamt[sl] = (uint8_t)atoi(argv[4]);
        proj.set.fxpar[sl] = (uint8_t)atoi(argv[5]);
        proj.set.send[sl][5] = (uint8_t)atoi(argv[6]);
    } else if (!strcmp(argv[1], "lfo")) {
        clip_t *c = &act[5];
        c->lfo[LFO_SHAPE] = (uint8_t)atoi(argv[2]);
        c->lfo[LFO_RATE] = (uint8_t)atoi(argv[3]);
        c->lfo[LFO_DEPTH] = (uint8_t)atoi(argv[4]);
        c->lfo[LFO_TARGET] = (uint8_t)atoi(argv[5]);
        c->lfo[LFO_SYNC] = (uint8_t)atoi(argv[6]);
        c->lfo[LFO_RETRIG] = (uint8_t)atoi(argv[7]);
    } else if (!strcmp(argv[1], "vol")) {
        proj.set.knob[0].kind = (uint8_t)atoi(argv[2]);
        proj.set.master_vol = (uint8_t)atoi(argv[3]);
    } else if (!strcmp(argv[1], "dj")) {
        proj.set.dj[0] = (uint8_t)atoi(argv[2]);
        proj.set.dj[1] = (uint8_t)atoi(argv[3]);
    }
    f = fopen(argv[argc - 1], "rb");
    if (!f || fread(a, sizeof a[0], N * 256, f) != N * 256)
        return 2;
    fclose(f);
    run(b);
    for (i = 0; i < N * 256; i++) {
        if (!isfinite(b[i]) || fabsf(b[i]) > 2.01f)
            bad = 1;
        d += (double)(b[i] - a[i]) * (b[i] - a[i]);
        ra += (double)a[i] * a[i];
        rb += (double)b[i] * b[i];
    }
    printf("%s: diff %.5f rms %.4f -> %.4f\n", argv[1], sqrt(d / (N * 256)), sqrt(ra / (N * 256)), sqrt(rb / (N * 256)));
    if (bad) { printf("  not finite or out of range\n"); return 1; }
    if (!strcmp(argv[argc - 2], "same") && d != 0) { printf("  expected the same audio\n"); return 1; }
    if (!strcmp(argv[argc - 2], "diff") && d < 1e-6) { printf("  expected a change\n"); return 1; }
    if (!strcmp(argv[argc - 2], "quieter") && !(rb < ra * 0.9)) { printf("  expected it quieter\n"); return 1; }
    return 0;
}
