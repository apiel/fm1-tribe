/* SPDX-License-Identifier: GPL-3.0-only */
/* NOISE (noise.c): finite and bounded at the corners of its controls, the square and the tri anchors play their pitch,
 * the noise anchor is loud and unpitched, the noise floor is on a wave anchor too, the filter types differ (HP on noise
 * has more energy than LP at a low cutoff), the three colours differ in their crossings and loudness, a slide keeps the
 * envelope, the release ends in silence, and two instances do not touch each other. */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "noise.h"

#define SR 44100
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void render(noise_t *b, float *o, int n)
{
    int i;
    for (i = 0; i < n; i += 64)
        noise_render(b, o + i, n - i < 64 ? n - i : 64);
}
static double rms(const float *x, int n)
{
    double s = 0;
    int i;
    for (i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / n);
}
static double hz(const float *x, int n)                   /* up-crossings per second */
{
    int i, c = 0;
    for (i = 1; i < n; i++)
        c += x[i - 1] < 0 && x[i] >= 0;
    return (double)c * SR / n;
}
static double crossings(const float *x, int n)            /* all sign changes per second, for the colours */
{
    int i, c = 0;
    for (i = 1; i < n; i++)
        c += (x[i - 1] < 0) != (x[i] < 0);
    return (double)c * SR / n;
}

/* the open filter, no envelope, a held note: the oscillator or the noise alone */
static void plain(noise_t *b, int morph, int noisefloor)
{
    noise_init(b);
    noise_set(b, NOISE_MORPH, morph);
    noise_set(b, NOISE_NOISE, noisefloor);
    noise_set(b, NOISE_CUTOFF, 127);
    noise_set(b, NOISE_RESO, 0);
    noise_set(b, NOISE_KEYTRK, 0);
    noise_set(b, NOISE_ENVMOD, 0);
    noise_set(b, NOISE_ATTACK, 0);
    noise_set(b, NOISE_SUSTAIN, 127);
    noise_set(b, NOISE_DRIVE, 0);
    noise_set(b, NOISE_TUNE, 64);
}

static float buf[SR * 3];

int main(void)
{
    static noise_t a, b2;
    int i, c, k;
    double r1, r2;

    for (c = 0; c < 64; c++) {                            /* corners: finite, bounded (the output is 0.5 x a soft limit) */
        noise_init(&a);
        for (k = 0; k < NOISE_NPARAMS; k++)
            noise_set(&a, k, ((c >> (k % 6)) & 1) ? 127 : 0);
        noise_set(&a, NOISE_NTYPE, c % 3);
        noise_set(&a, NOISE_FTYPE, (c / 3) % 4);
        noise_note_on(&a, 24 + c, 0);
        render(&a, buf, SR / 2);
        noise_note_on(&a, 60, 1);
        render(&a, buf + SR / 2, SR / 2);
        for (i = 0; i < SR; i++)
            if (!isfinite(buf[i]) || fabsf(buf[i]) > 0.5001f) {
                CHECK(0, "corner %d: sample %d = %f", c, i, buf[i]);
                break;
            }
    }

    plain(&a, 0, 0);                                      /* the square anchor, no noise: the pitch asked (A4) */
    noise_note_on(&a, 69, 0);
    render(&a, buf, SR);
    r1 = hz(buf + SR / 4, SR / 2);
    CHECK(fabs(r1 - 440.0) < 440.0 * 0.02, "square anchor pitch %.1f Hz (want 440)", r1);

    plain(&a, 127, 0);                                    /* the tri anchor, no noise: the pitch asked */
    noise_note_on(&a, 69, 0);
    render(&a, buf, SR);
    r1 = hz(buf + SR / 4, SR / 2);
    CHECK(fabs(r1 - 440.0) < 440.0 * 0.02, "tri anchor pitch %.1f Hz (want 440)", r1);
    CHECK(rms(buf + SR / 4, SR / 2) > 0.2, "tri anchor level %.3f", rms(buf + SR / 4, SR / 2));

    plain(&a, 32, 0);                                     /* the first noise anchor, noise floor 0: the noise is all there is */
    noise_note_on(&a, 69, 0);
    render(&a, buf, SR);
    r1 = rms(buf + SR / 4, SR / 2);
    r2 = crossings(buf + SR / 4, SR / 2);
    CHECK(r1 > 0.1, "noise anchor is silent: rms %.3f", r1);
    CHECK(r2 > 3000.0, "noise anchor is pitched: %.0f crossings/s", r2);

    {                                                     /* the morph law: each anchor's waves and the noise, by the gains the engine computes */
        static const struct { int morph, floor; float sq, saw, tri, nz; } want[] = {
            { 0, 0, 1.0f, 0.0f, 0.0f, 0.0f },              /* square: the noise floor off */
            { 0, 127, 1.0f, 0.0f, 0.0f, 1.0f },            /* square with the floor on full: the noise is there too */
            { 32, 0, 0.0f, 0.0f, 0.0f, 1.0f },             /* the first noise anchor: the noise is all there is */
            { 64, 0, 0.0f, 1.0f, 0.0f, 0.0f },             /* saw */
            { 95, 0, 0.0f, 0.0f, 0.0f, 1.0f },             /* the second noise anchor (3/4 of the morph: 95.25) */
            { 127, 0, 0.0f, 0.0f, 1.0f, 0.0f },            /* tri */
            { 127, 64, 0.0f, 0.0f, 1.0f, 0.5f },          /* tri with the floor at half: the noise sits under the wave */
        };
        int w;
        for (w = 0; w < (int)(sizeof want / sizeof want[0]); w++) {
            float sq, saw, tri, nz;
            plain(&a, want[w].morph, want[w].floor);
            noise_note_on(&a, 69, 0);
            render(&a, buf, 64);
            sq = a.gsq; saw = a.gsaw; tri = a.gtri; nz = a.gnoise;
            CHECK(fabsf(sq - want[w].sq) < 0.02f && fabsf(saw - want[w].saw) < 0.02f &&
                  fabsf(tri - want[w].tri) < 0.02f && fabsf(nz - want[w].nz) < 0.02f,
                  "morph %d floor %d: square %.2f saw %.2f tri %.2f noise %.2f (want %.2f %.2f %.2f %.2f)",
                  want[w].morph, want[w].floor, sq, saw, tri, nz,
                  want[w].sq, want[w].saw, want[w].tri, want[w].nz);
        }
    }

    plain(&a, 0, 0);                                      /* the filter: HP passes more of the noise than LP at 40 Hz */
    noise_set(&a, NOISE_MORPH, 32);
    noise_set(&a, NOISE_NOISE, 127);
    noise_set(&a, NOISE_CUTOFF, 0);
    noise_set(&a, NOISE_FTYPE, NF_LP);
    noise_note_on(&a, 69, 0);
    render(&a, buf, SR);
    r1 = rms(buf + SR / 4, SR / 2);
    plain(&b2, 32, 127);
    noise_set(&b2, NOISE_CUTOFF, 0);
    noise_set(&b2, NOISE_FTYPE, NF_HP);
    noise_note_on(&b2, 69, 0);
    render(&b2, buf + SR, SR);
    r2 = rms(buf + SR + SR / 4, SR / 2);
    CHECK(r2 > 3.0 * r1, "HP %.4f should pass much more than LP %.4f at 40 Hz", r2, r1);

    {                                                     /* the colours: white crosses zero most, brown least; the levels are close */
        static noise_t cw[3];
        double zc[3], rm[3];
        int t;
        for (t = 0; t < 3; t++) {
            noise_init(&cw[t]);
            noise_set(&cw[t], NOISE_MORPH, 32);
            noise_set(&cw[t], NOISE_NOISE, 127);
            noise_set(&cw[t], NOISE_CUTOFF, 127);
            noise_set(&cw[t], NOISE_RESO, 0);
            noise_set(&cw[t], NOISE_SUSTAIN, 127);
            noise_set(&cw[t], NOISE_ATTACK, 0);
            noise_set(&cw[t], NOISE_NTYPE, t);
            noise_note_on(&cw[t], 69, 0);
            render(&cw[t], buf, SR);
            zc[t] = crossings(buf + SR / 4, SR / 2);
            rm[t] = rms(buf + SR / 4, SR / 2);
        }
        CHECK(zc[NT_WHITE] > zc[NT_PINK] && zc[NT_PINK] > zc[NT_BROWN],
              "colours not ordered: white %.0f pink %.0f brown %.0f crossings/s", zc[0], zc[1], zc[2]);
        CHECK(rm[NT_PINK] > 0.7 * rm[NT_WHITE] && rm[NT_PINK] < 1.4 * rm[NT_WHITE],
              "pink level %.3f against white %.3f", rm[NT_PINK], rm[NT_WHITE]);
        CHECK(rm[NT_BROWN] > 0.7 * rm[NT_WHITE] && rm[NT_BROWN] < 1.4 * rm[NT_WHITE],
              "brown level %.3f against white %.3f", rm[NT_BROWN], rm[NT_WHITE]);
    }

    noise_init(&a);                                       /* a slide over a held note keeps the envelope; a new key restarts it */
    noise_set(&a, NOISE_ATTACK, 0);
    noise_set(&a, NOISE_SUSTAIN, 127);
    noise_note_on(&a, 60, 0);
    render(&a, buf, SR / 10);
    noise_note_on(&a, 64, 1);
    CHECK(a.stage == 1, "a slide restarted the envelope (stage %d)", a.stage);
    noise_note_on(&a, 67, 0);
    CHECK(a.stage == 0, "a new key did not restart the envelope (stage %d)", a.stage);

    noise_init(&a);                                       /* the release ends in silence */
    noise_set(&a, NOISE_RELEASE, 0);
    noise_set(&a, NOISE_ATTACK, 0);
    noise_set(&a, NOISE_SUSTAIN, 127);
    noise_note_on(&a, 60, 0);
    render(&a, buf, SR / 10);
    noise_note_off(&a);
    render(&a, buf, SR);
    CHECK(a.idle, "the release did not end in silence");
    for (i = SR / 2; i < SR; i++)
        if (buf[i] != 0.0f) {
            CHECK(0, "sound after the release at sample %d", i);
            break;
        }

    noise_init(&a);                                       /* two instances: one sounding, the other silent */
    noise_init(&b2);
    noise_note_on(&a, 60, 0);
    render(&a, buf, SR / 4);
    render(&b2, buf + SR / 4, SR / 4);
    CHECK(b2.idle, "a second instance woke up");
    for (i = SR / 4; i < SR / 2; i++)
        if (buf[i] != 0.0f) {
            CHECK(0, "a silent instance made sound at sample %d", i);
            break;
        }

    printf(fails ? "noise: %d failure(s)\n" : "noise: all checks passed\n", fails);
    return fails != 0;
}
