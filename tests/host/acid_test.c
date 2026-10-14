/* SPDX-License-Identifier: GPL-3.0-only */
/* ACID (acid.c, the zicCycle 303 in float): it stays finite and bounded at the corners of its controls, plays
 * the pitch asked, decays, falls silent (idle), gets brighter with Cutoff and Color, plays accents louder,
 * glides on a slide, and two instances do not touch each other. Writes build/acid_demo.wav (an acid
 * line: accents and slides) for listening. */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "acid.h"

#define SR 44100
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void render(acid_t *b, float *o, int n)
{
    int i;
    for (i = 0; i < n; i += 64)
        acid_render(b, o + i, n - i < 64 ? n - i : 64);
}
static double rms(const float *x, int n)
{
    double s = 0;
    int i;
    for (i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / n);
}
static double hz(const float *x, int n)                  /* from the rising zero crossings */
{
    int i, c = 0;
    for (i = 1; i < n; i++)
        c += x[i - 1] < 0 && x[i] >= 0;
    return (double)c * SR / n;
}
static double bright(const float *x, int n)              /* energy of the first difference over the energy */
{
    double d = 0, s = 1e-12;
    int i;
    for (i = 1; i < n; i++) {
        d += (double)(x[i] - x[i - 1]) * (x[i] - x[i - 1]);
        s += (double)x[i] * x[i];
    }
    return d / s;
}
static void open_filter(acid_t *b)
{
    acid_set(b, ACID_CUTOFF, 127);
    acid_set(b, ACID_RESO, 0);
    acid_set(b, ACID_ENVMOD, 0);
    acid_set(b, ACID_DECAY, 127);
    acid_set(b, ACID_PDEPTH, 64);
}

static float buf[SR * 2];

int main(void)
{
    static acid_t a, b2;
    int i, c, k;
    double r1, r2, f;

    /* corners: finite, bounded */
    for (c = 0; c < 64; c++) {
        acid_init(&a);
        for (k = 0; k < ACID_NPARAMS; k++)
            acid_set(&a, k, ((c >> (k % 6)) & 1) ? 127 : 0);
        acid_note_on(&a, 24 + c, c & 1, 0);
        render(&a, buf, SR / 2);
        acid_note_on(&a, 60, 1, 1);
        render(&a, buf + SR / 2, SR / 2);
        for (i = 0; i < SR; i++)
            if (!isfinite(buf[i]) || fabsf(buf[i]) > 1.0f) {
                CHECK(0, "corner %d: sample %d = %g", c, i, buf[i]);
                break;
            }
    }

    /* pitch: A2 = 110 Hz and A3 = 220 Hz, a saw, filter open */
    acid_init(&a);
    open_filter(&a);
    acid_note_on(&a, 45, 0, 0);
    render(&a, buf, SR);
    f = hz(buf + 4410, SR - 4410);
    CHECK(fabs(f - 110.0) < 2.5, "A2 is %.1f Hz", f);
    acid_note_on(&a, 57, 0, 0);
    render(&a, buf, SR);
    f = hz(buf + 4410, SR - 4410);
    CHECK(fabs(f - 220.0) < 4.5, "A3 is %.1f Hz", f);

    /* decay: a short one is quieter after 300 ms than a long one */
    acid_init(&a);
    open_filter(&a);
    acid_set(&a, ACID_DECAY, 30);
    acid_note_on(&a, 45, 0, 0);
    render(&a, buf, SR);
    r1 = rms(buf + SR * 3 / 10, 4410);
    acid_init(&a);
    open_filter(&a);
    acid_set(&a, ACID_DECAY, 100);
    acid_note_on(&a, 45, 0, 0);
    render(&a, buf, SR);
    r2 = rms(buf + SR * 3 / 10, 4410);
    CHECK(r1 < r2 * 0.1, "decay 30 rms %g vs decay 100 rms %g after 300 ms", r1, r2);

    /* idle: the note ends, then exact silence */
    acid_init(&a);
    open_filter(&a);
    acid_set(&a, ACID_DECAY, 20);
    acid_note_on(&a, 45, 0, 0);
    render(&a, buf, SR / 10);
    acid_note_off(&a);
    render(&a, buf, SR);
    c = 0;
    for (i = SR / 2; i < SR; i++)
        c += buf[i] != 0.0f;
    CHECK(c == 0, "%d non-zero samples after the note died", c);
    acid_note_on(&a, 45, 0, 0);                                    /* and it plays again */
    render(&a, buf, 4410);
    CHECK(rms(buf, 4410) > 0.05, "no sound after idle");

    /* the gate: a held note stops soon after the gate ends */
    acid_init(&a);
    open_filter(&a);
    acid_note_on(&a, 45, 0, 0);
    render(&a, buf, SR / 4);
    acid_note_off(&a);
    render(&a, buf, SR / 2);
    CHECK(rms(buf + SR / 4 + 2000, 2000) < 0.01, "the released note rings on (%g)", rms(buf + SR / 4 + 2000, 2000));

    /* brighter with Cutoff, and with Color toward the distorted wave */
    acid_init(&a);
    acid_set(&a, ACID_ENVMOD, 0);
    acid_set(&a, ACID_RESO, 0);
    acid_set(&a, ACID_DECAY, 127);
    acid_set(&a, ACID_CUTOFF, 40);
    acid_note_on(&a, 45, 0, 0);
    render(&a, buf, SR / 2);
    r1 = bright(buf + 4410, SR / 2 - 4410);
    acid_set(&a, ACID_CUTOFF, 100);
    acid_note_on(&a, 45, 0, 0);
    render(&a, buf, SR / 2);
    r2 = bright(buf + 4410, SR / 2 - 4410);
    CHECK(r2 > r1 * 4.0, "cutoff 100 is not brighter than 40 (%g vs %g)", r2, r1);

    /* the filter envelope opens the filter at the start of a note */
    acid_init(&a);
    acid_set(&a, ACID_CUTOFF, 40);
    acid_set(&a, ACID_RESO, 0);
    acid_set(&a, ACID_ENVMOD, 127);
    acid_set(&a, ACID_DECAY, 80);
    acid_note_on(&a, 45, 0, 0);
    render(&a, buf, SR);
    r1 = bright(buf, 2000);
    r2 = bright(buf + SR * 8 / 10, 2000);
    CHECK(r1 > r2 * 3.0, "no filter envelope (%g at the start, %g later)", r1, r2);

    /* accent: louder */
    acid_init(&a);
    open_filter(&a);
    acid_set(&a, ACID_CUTOFF, 70);
    acid_set(&a, ACID_ENVMOD, 70);
    acid_note_on(&a, 45, 0, 0);
    render(&a, buf, 8000);
    r1 = rms(buf, 8000);
    acid_init(&a);
    open_filter(&a);
    acid_set(&a, ACID_CUTOFF, 70);
    acid_set(&a, ACID_ENVMOD, 70);
    acid_note_on(&a, 45, 1, 0);
    render(&a, buf, 8000);
    r2 = rms(buf, 8000);
    CHECK(r2 > r1 * 1.15, "accent %g vs plain %g", r2, r1);

    /* pitch envelope: starts above, then settles on the note */
    acid_init(&a);
    open_filter(&a);
    acid_set(&a, ACID_PDEPTH, 127);
    acid_note_on(&a, 45, 0, 0);
    render(&a, buf, SR);
    r1 = hz(buf, 1500);
    f = hz(buf + SR / 2, SR / 4);
    CHECK(r1 > f * 1.5 && fabs(f - 110.0) < 3.0, "pitch envelope: %.0f Hz at the start, %.1f Hz settled", r1, f);

    /* slide: glides to the new note, the oscillator keeps its phase (no restart: no step) */
    acid_init(&a);
    open_filter(&a);
    acid_note_on(&a, 45, 0, 0);
    render(&a, buf, 8000);
    acid_note_on(&a, 57, 0, 1);
    render(&a, buf, SR / 2);
    f = hz(buf + SR / 3, SR / 6);
    CHECK(fabs(f - 220.0) < 6.0, "slid note settles at %.1f Hz", f);
    CHECK(rms(buf + 100, 300) > 0.1, "the slide dropped out (rms %g)", rms(buf + 100, 300));

    /* sustain: a short decay settles on the level while the gate is held, and the gate ends it */
    acid_init(&a);
    open_filter(&a);
    acid_set(&a, ACID_DECAY, 30);
    acid_set(&a, ACID_SUSTAIN, 100);
    acid_note_on(&a, 45, 0, 0);
    render(&a, buf, SR);
    r1 = rms(buf + SR * 8 / 10, 4410);
    acid_init(&a);
    open_filter(&a);
    acid_set(&a, ACID_DECAY, 30);
    acid_note_on(&a, 45, 0, 0);
    render(&a, buf, SR);
    r2 = rms(buf + SR * 8 / 10, 4410);
    CHECK(r2 < 0.001 && r1 > 0.1, "sustain: rms %g held, %g with none", r1, r2);
    acid_note_off(&a);
    acid_init(&a);
    open_filter(&a);
    acid_set(&a, ACID_DECAY, 30);
    acid_set(&a, ACID_SUSTAIN, 100);
    acid_note_on(&a, 45, 0, 0);
    render(&a, buf, SR / 2);
    acid_note_off(&a);
    render(&a, buf, SR / 2);
    CHECK(rms(buf + SR / 4, 2000) < 0.001, "the sustained note does not end with the gate (%g)", rms(buf + SR / 4, 2000));

    /* Drive and Dist: off is the plain sound; on, a brighter, denser one, still bounded; each type works */
    {
        static float ref[8000], dd[8000];
        int ty;
        acid_init(&a);
        open_filter(&a);
        acid_set(&a, ACID_CUTOFF, 60);
        acid_note_on(&a, 45, 0, 0);
        render(&a, ref, 8000);
        for (ty = 0; ty < 3; ty++) {
            acid_init(&a);
            open_filter(&a);
            acid_set(&a, ACID_CUTOFF, 60);
            acid_set(&a, ACID_DIST, 127);
            acid_set(&a, ACID_DISTTYPE, ty);
            acid_note_on(&a, 45, 0, 0);
            render(&a, dd, 8000);
            for (i = 0; i < 8000; i++)
                if (!isfinite(dd[i]) || fabsf(dd[i]) > 0.5001f) {
                    CHECK(0, "dist type %d: sample %d = %g", ty, i, dd[i]);
                    break;
                }
            CHECK(bright(dd, 8000) > bright(ref, 8000) * 1.2 || ty == 2, "dist type %d is not brighter", ty);
            CHECK(memcmp(ref, dd, sizeof ref) != 0, "dist type %d changes nothing", ty);
        }
        acid_init(&a);
        open_filter(&a);
        acid_set(&a, ACID_CUTOFF, 60);
        acid_set(&a, ACID_DRIVE, 127);
        acid_note_on(&a, 45, 0, 0);
        render(&a, dd, 8000);
        CHECK(bright(dd, 8000) != bright(ref, 8000) && rms(dd, 8000) > 0.0, "drive changes nothing");
        acid_init(&a);
        open_filter(&a);
        acid_set(&a, ACID_CUTOFF, 60);
        acid_set(&a, ACID_DIST, 0);
        acid_set(&a, ACID_DRIVE, 0);
        acid_note_on(&a, 45, 0, 0);
        render(&a, dd, 8000);
        CHECK(memcmp(ref, dd, sizeof ref) == 0, "drive and dist at 0 are not bypassed");
    }

    /* two instances, same settings: the same samples; one note does not sound in the other */
    acid_init(&a);
    acid_init(&b2);
    acid_note_on(&a, 40, 1, 0);
    render(&a, buf, 4000);
    render(&b2, buf + 4000, 4000);
    CHECK(rms(buf + 4000, 4000) == 0.0, "an idle instance sounds");
    acid_note_on(&b2, 40, 1, 0);
    render(&b2, buf + 4000, 4000);
    CHECK(memcmp(buf, buf + 4000, 4000 * sizeof(float)) == 0, "the two instances differ");

    /* the demo: a line with accents and slides, 125 bpm sixteenths */
    {
        static const int N[16] = {36, 36, 48, 36, 39, 36, 51, 48, 36, 36, 46, 36, 43, 44, 36, 48};
        static const int ACC[16] = {1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0};
        static const int SL[16] = {0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1};
        int step = SR * 60 / 125 / 4, nn = step * 32, pos = 0, s;
        static float demo[SR * 8];
        FILE *w;
        acid_init(&a);
        acid_set(&a, ACID_CUTOFF, 36);
        acid_set(&a, ACID_RESO, 100);
        acid_set(&a, ACID_ENVMOD, 90);
        acid_set(&a, ACID_DECAY, 52);
        for (s = 0; s < 32; s++) {
            int q = s & 15, gate = step * (SL[(q + 1) & 15] ? 10 : 6) / 10;
            acid_note_on(&a, N[q], ACC[q], s > 0 && SL[q - 1 < 0 ? 15 : q - 1]);
            render(&a, demo + pos, gate);
            if (!SL[(q + 1) & 15])
                acid_note_off(&a);
            render(&a, demo + pos + gate, step - gate);
            pos += step;
        }
        (void)nn;
        mkdir("build", 0777);
        w = fopen("build/acid_demo.wav", "wb");
        if (w) {
            unsigned hdr[11] = {0x46464952, 36 + (unsigned)pos * 2, 0x45564157, 0x20746d66, 16, 0x10001, SR, SR * 2, 0x100002, 0x61746164, (unsigned)pos * 2};
            short sm;
            fwrite(hdr, 4, 11, w);
            for (i = 0; i < pos; i++) {
                sm = (short)(demo[i] * 28000.0f);
                fwrite(&sm, 2, 1, w);
            }
            fclose(w);
        }
    }
    printf(fails ? "acid: %d FAILED\n" : "acid: ok\n", fails);
    return fails != 0;
}
