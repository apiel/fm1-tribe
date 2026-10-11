/* SPDX-License-Identifier: GPL-3.0-only */
/* The sequencer's clock: a clip's rate (how long a step lasts), direction and swing, measured on the first track
 * at 125 BPM (a 16th = 5292 samples). One case a process:  seq_test RATE DIR SWING   */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "tribe/tribe.h"
#include "app/plat.h"
uint32_t plat_master(void) { return 4096; }
int plat_log_read(uint32_t s, uint32_t o, void *d, uint32_t n) { (void)s; (void)o; (void)d; (void)n; return -1; }
int plat_log_erase(uint32_t s) { (void)s; return -1; }
int plat_log_prog(uint32_t s, uint32_t o, const void *d, uint32_t n) { (void)s; (void)o; (void)d; (void)n; return -1; }

int main(int argc, char **argv)
{
    static int32_t blk[512];
    static int posn[4000], at[4000];
    int rate = atoi(argv[1]), dir = atoi(argv[2]), swing = atoi(argv[3]), b, n = 0, i, fails = 0, last = -1;
    static const int TICKS[6] = {6, 12, 3, 4, 8, 2};
    double sum = 0, odd = 0, even = 0;
    int nodd = 0, neven = 0, up = 0, down = 0, maxp = 0, minp = 99;
    project_defaults();
    proj.clip[0][0].rate = (uint8_t)rate;
    proj.clip[0][0].dir = (uint8_t)dir;
    proj.clip[0][0].swing = (uint8_t)swing;
    engine_init();
    dk_play(1);
    for (b = 0; b < 44100 * 12 / 256; b++) {
        engine_render(blk, 256);
        if (dk_pos[0] != last || b == 0) {
            if (n < 4000) { posn[n] = dk_pos[0]; at[n] = b * 256; n++; }
            last = dk_pos[0];
        }
    }
    for (i = 2; i < n; i++) {
        double iv = at[i] - at[i - 1];
        sum += iv;
        if (i & 1) { odd += iv; nodd++; } else { even += iv; neven++; }
        if (dir == SD_FWD && posn[i] != (posn[i - 1] + 1) % 16) { printf("forward: %d then %d\n", posn[i - 1], posn[i]); fails++; }
        if (dir == SD_REV && posn[i] != (posn[i - 1] + 15) % 16) { printf("reverse: %d then %d\n", posn[i - 1], posn[i]); fails++; }
        if (dir == SD_PING) {
            int d = posn[i] - posn[i - 1];
            if (d != 1 && d != -1) { printf("ping-pong: %d then %d\n", posn[i - 1], posn[i]); fails++; }
            up += d == 1;
            down += d == -1;
        }
        if (posn[i] > maxp) maxp = posn[i];
        if (posn[i] < minp) minp = posn[i];
    }
    if (dir == SD_REV && posn[0] != 15) { printf("reverse starts on %d\\n", posn[0]); fails++; }
    if (dir == SD_PING && (!up || !down)) { printf("ping-pong never bounced\n"); fails++; }
    if (dir == SD_RAND && (maxp - minp < 8 || maxp > 15)) { printf("random: range %d..%d\n", minp, maxp); fails++; }
    {
        double want = TICKS[rate] * 882.0, mean = sum / (n - 2);
        if (dir != SD_RAND && fabs(mean - want) > 120.0) {      /* (random can land on the same step twice: not seen as a step) */ printf("rate %d: mean step %.0f samples, wanted %.0f\n", rate, mean, want); fails++; }
        if (swing == 127) {
            int off = (127 * TICKS[rate] / 3 + 63) / 127;
            double wo = (TICKS[rate] - off) * 882.0, we = (TICKS[rate] + off) * 882.0;
            /* intervals[i] = from step i-1 to step i: the odd steps arrive late (i odd is the second of a pair here) */
            if (fabs(odd / nodd - we) > 400.0 || fabs(even / neven - wo) > 400.0) {
                printf("swing: odd %.0f even %.0f, wanted %.0f / %.0f\n", odd / nodd, even / neven, we, wo);
                fails++;
            }
        }
    }
    printf("rate %d dir %d swing %3d: %d steps, mean %.0f samples %s\n", rate, dir, swing, n, sum / (n - 2), fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
