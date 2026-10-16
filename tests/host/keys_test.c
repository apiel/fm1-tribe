/* SPDX-License-Identifier: GPL-3.0-only */
/* The keys on a synth track are monophonic and last-in: the sounding note is the one pressed last. Let go of a note
 * that is not the sounding one and nothing changes; let go of the sounding one and the note held before it sounds
 * again, glided to, with no new attack; the last one let go releases the track.
 *   keys_test                 checks the rules above on ACID (the pitch of each step, the release) */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include "tribe/tribe.h"
uint32_t plat_master(void) { return 4096; }
#include "app/plat.h"
int plat_log_read(uint32_t s, uint32_t o, void *d, uint32_t n) { (void)s; (void)o; (void)d; (void)n; return -1; }
int plat_log_erase(uint32_t s) { (void)s; return -1; }
int plat_log_prog(uint32_t s, uint32_t o, const void *d, uint32_t n) { (void)s; (void)o; (void)d; (void)n; return -1; }

#define SR 44100
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static float buf[NTRK * 256];
static const int TRK = 0;

/* renders n samples of the track into out (blocks of 256, as the engine does) */
static void render(float *out, int n)
{
    int i;
    for (i = 0; i < n; i += 256) {
        int k = n - i < 256 ? n - i : 256;
        eng_render_all(buf, 0, k);
        memcpy(out + i, buf + TRK * 256, (unsigned)k * sizeof(float));
    }
}
/* the frequency from the rising zero crossings */
static double hz(const float *x, int n)
{
    int i, c = 0;
    for (i = 1; i < n; i++)
        c += x[i - 1] < 0 && x[i] >= 0;
    return (double)c * SR / n;
}
static double rms(const float *x, int n)
{
    double s = 0;
    int i;
    for (i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / n);
}

static float out[SR];

/* renders a quarter second, then measures the next half second: the pitch and the level of the sound */
static double measure(double *level)
{
    render(out, SR / 4 + SR / 2);
    *level = rms(out + SR / 4, SR / 2);
    return hz(out + SR / 4, SR / 2);
}

int main(void)
{
    double f, lv;
    int acid = 25;                                   /* ACID in the table (engines.c) */
    eng_init();
    CHECK(eng_bind(TRK, acid) == 0, "ACID binds to a track");
    eng_set(TRK, 0, 127);                            /* Cutoff open, no resonance, no filter sweep: a plain saw to count */
    eng_set(TRK, 1, 0);
    eng_set(TRK, 5, 0);

    eng_key_on(TRK, 45, 100);                        /* A2 = 110 Hz */
    f = measure(&lv);
    CHECK(fabs(f - 110.0) < 3.0, "A2 pressed: %.1f Hz, want 110", f);

    eng_key_on(TRK, 57, 100);                        /* A3 = 220 Hz: the last pressed, so it sounds */
    f = measure(&lv);
    CHECK(fabs(f - 220.0) < 3.0, "A3 pressed: %.1f Hz, want 220", f);

    eng_key_on(TRK, 48, 100);                        /* C3 = 131 Hz: the last pressed */
    f = measure(&lv);
    CHECK(fabs(f - 130.8) < 3.0, "C3 pressed: %.1f Hz, want 131", f);

    eng_key_off(TRK, 57);                            /* A3 let go, not the sounding one: C3 goes on */
    f = measure(&lv);
    CHECK(fabs(f - 130.8) < 3.0 && lv > 0.05, "A3 let go under C3: %.1f Hz, level %g", f, lv);

    eng_key_off(TRK, 48);                            /* C3 let go, the sounding one: A2, still held, sounds again */
    f = measure(&lv);
    CHECK(fabs(f - 110.0) < 3.0 && lv > 0.05, "C3 let go, back to A2: %.1f Hz, level %g", f, lv);

    eng_key_off(TRK, 45);                            /* the last one let go: the track releases */
    f = measure(&lv);
    CHECK(lv < 0.001, "the last key let go releases the track (level %g)", lv);

    eng_key_on(TRK, 45, 100);                        /* and the track plays again, from the empty stack */
    f = measure(&lv);
    CHECK(fabs(f - 110.0) < 3.0 && lv > 0.05, "pressed again after the release: %.1f Hz, level %g", f, lv);

    printf(fails ? "keys: %d FAILED\n" : "keys: ok\n", fails);
    return fails != 0;
}
