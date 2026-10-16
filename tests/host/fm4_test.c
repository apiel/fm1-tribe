/* SPDX-License-Identifier: GPL-3.0-only */
/* FM4 (fm4.c, laid out on the Digitone's FM): finite and bounded at the corners of its controls, every algorithm
 * computes its operators in a valid order, carrier C plays its ratio, MIX picks X or Y, HARM changes C's wave, the
 * envelopes end at their end levels (trig) or hold until the key is let go (gate), the release ends in silence, and a
 * note over a sounding one (legato) keeps the envelopes going. Writes nothing. */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "fm4.h"

#define SR 44100
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static fm4_t b;
static float buf[SR * 3];

static void render(float *o, int n)
{
    int i;
    for (i = 0; i < n; i += 64)
        fm4_render(&b, o + i, n - i < 64 ? n - i : 64);
}
static double hz(const float *x, int n)                 /* from the rising zero crossings */
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
static double bright(const float *x, int n)             /* energy of the first difference over the energy */
{
    double d = 0, s = 1e-12;
    int i;
    for (i = 1; i < n; i++) {
        d += (double)(x[i] - x[i - 1]) * (x[i] - x[i - 1]);
        s += (double)x[i] * x[i];
    }
    return d / s;
}

/* C alone: algorithm 1, no modulator on C, X only (MIX 0), open filter, a held amp */
static void c_alone(void)
{
    fm4_init(&b);
    fm4_set(&b, FM4_ALG, 0);
    fm4_set(&b, FM4_LEVA, 0);
    fm4_set(&b, FM4_MIX, 0);
    fm4_set(&b, FM4_CUT, 127);
    fm4_set(&b, FM4_SUS, 127);
    fm4_set(&b, FM4_ATK, 0);
    fm4_set(&b, FM4_RATC, 2);                              /* ratio 3 */
}

int main(void)
{
    int alg, c, k, i, j, found;
    float peak = 0.0f;
    int bad = 0;
    double f, b0, b1, lv, r1;

    /* corners: every algorithm, the controls at their ends, feedback and resonance up */
    for (alg = 0; alg < 8; alg++)
        for (c = 0; c < 4; c++) {
            fm4_init(&b);
            for (k = 0; k < FM4_NPARAMS; k++)
                fm4_set(&b, k, ((c >> (k % 2)) & 1) ? 127 : 0);
            fm4_set(&b, FM4_ALG, alg);
            fm4_set(&b, FM4_FDBK, 127);
            fm4_set(&b, FM4_RESO, 127);
            fm4_set(&b, FM4_HARM, 127 * (c & 1));
            fm4_note_on(&b, 36, 0);
            render(buf, SR / 2);
            fm4_note_off(&b);
            render(buf + SR / 2, SR / 2);
            for (i = 0; i < SR; i++) {
                if (!isfinite(buf[i]))
                    bad++;
                if (fabsf(buf[i]) > peak)
                    peak = fabsf(buf[i]);
            }
        }
    CHECK(bad == 0, "%d non-finite samples", bad);
    CHECK(peak < 1.0f, "peak %g at the corners", peak);

    /* every algorithm: the operators are computed in an order where each comes after its modulators */
    for (alg = 0; alg < 8; alg++) {
        fm4_init(&b);
        fm4_set(&b, FM4_ALG, alg);
        for (j = 0; j < FM4_OPS; j++) {
            found = 0;
            for (k = 0; k < FM4_OPS; k++)
                found += b.ord[k] == j;
            CHECK(found == 1, "algorithm %d: operator %d not once in the order", alg + 1, j);
        }
    }

    /* C alone plays its ratio (3 x the note: A2 is 110 Hz, so 330 Hz) */
    c_alone();
    fm4_note_on(&b, 45, 0);
    render(buf, SR);
    f = hz(buf + SR / 4, SR / 2);
    CHECK(fabs(f - 330.0) < 3.0, "C at ratio 3: %.1f Hz, want 330", f);

    /* a modulator on C adds brightness (A > C in algorithm 1), with its index at 0 and at full */
    c_alone();
    fm4_set(&b, FM4_LEVA, 0);
    fm4_set(&b, FM4_DECA, 127);
    fm4_note_on(&b, 45, 0);
    render(buf, SR / 4);
    b0 = bright(buf + SR / 50, SR / 5);
    c_alone();
    fm4_set(&b, FM4_LEVA, 127);
    fm4_set(&b, FM4_DECA, 127);
    fm4_note_on(&b, 45, 0);
    render(buf, SR / 4);
    b1 = bright(buf + SR / 50, SR / 5);
    CHECK(b1 > b0 * 1.2, "a modulator adds nothing (%g vs %g)", b1, b0);

    /* MIX: 0 is X (C), 127 is Y (B1 and B2 in algorithm 1): the two sound different */
    c_alone();
    fm4_set(&b, FM4_LEVB, 127);
    fm4_note_on(&b, 45, 0);
    render(buf, SR / 2);
    fm4_set(&b, FM4_MIX, 127);
    render(buf + SR / 2, SR / 2);
    CHECK(rms(buf + SR / 4, SR / 4) > 0.01 && rms(buf + SR * 3 / 4, SR / 4) > 0.01, "MIX silences a side");
    CHECK(fabs(rms(buf + SR / 4, SR / 4) - rms(buf + SR * 3 / 4, SR / 4)) > 0.001, "MIX changes nothing");

    /* HARM below 64 gives C a saw-like wave: its output is not a sine any more */
    c_alone();
    fm4_note_on(&b, 45, 0);
    render(buf, SR / 2);
    c_alone();
    fm4_set(&b, FM4_HARM, 0);
    fm4_note_on(&b, 45, 0);
    render(buf + SR / 2, SR / 2);
    CHECK(memcmp(buf, buf + SR / 2, SR / 2 * sizeof(float)) != 0, "HARM changes nothing");

    /* trig envelope: runs out to its end level (A: 0.5) and holds there */
    fm4_init(&b);
    fm4_set(&b, FM4_TRIG, 1);
    fm4_set(&b, FM4_ATKA, 0);
    fm4_set(&b, FM4_DECA, 40);
    fm4_set(&b, FM4_ENDA, 64);
    fm4_note_on(&b, 45, 0);
    render(buf, SR);
    CHECK(fabs(b.oenv[0] - 64.0 / 127.0) < 0.02, "trig: A ends at %.3f, want %.3f", b.oenv[0], 64.0 / 127.0);

    /* gate envelope: holds at 1 while the key is down, then decays to its end level after the release */
    fm4_init(&b);
    fm4_set(&b, FM4_TRIG, 0);
    fm4_set(&b, FM4_ATKA, 0);
    fm4_set(&b, FM4_DECA, 40);
    fm4_set(&b, FM4_ENDA, 64);
    fm4_note_on(&b, 45, 0);
    render(buf, SR / 2);
    CHECK(b.oenv[0] > 0.98, "gate: A is %.3f while held, want 1", b.oenv[0]);
    fm4_note_off(&b);
    render(buf, SR);
    CHECK(fabs(b.oenv[0] - 64.0 / 127.0) < 0.02, "gate: A ends at %.3f after the release, want %.3f", b.oenv[0], 64.0 / 127.0);

    /* the release ends in silence */
    fm4_init(&b);
    fm4_set(&b, FM4_REL, 0);
    fm4_set(&b, FM4_SUS, 127);
    fm4_note_on(&b, 45, 0);
    render(buf, SR / 2);
    CHECK(rms(buf + SR / 4, SR / 4) > 0.05, "no sound while held (%g)", rms(buf + SR / 4, SR / 4));
    fm4_note_off(&b);
    render(buf, SR / 2);
    lv = rms(buf + SR / 4, SR / 4);
    CHECK(lv < 0.001, "the note does not end after the release (%g)", lv);

    /* legato: a note over a sounding one with the slide flag glides: the pitch moves, the envelopes do not restart */
    c_alone();
    fm4_set(&b, FM4_TRIG, 0);
    fm4_set(&b, FM4_ATKA, 0);
    fm4_note_on(&b, 45, 0);
    render(buf, SR / 2);
    r1 = b.oenv[0];
    fm4_note_on(&b, 57, 1);
    CHECK(fabs(b.oenv[0] - r1) < 1e-6, "legato restarted the envelope (%g, was %g)", b.oenv[0], r1);
    render(buf, SR * 2);
    f = hz(buf + SR, SR);
    CHECK(fabs(f - 660.0) < 4.0, "legato: %.1f Hz after the slide, want 660", f);
    CHECK(b.oenv[0] > 0.98, "legato: A dropped to %.3f", b.oenv[0]);

    /* a new note without the slide flag starts the envelopes from nothing */
    c_alone();
    fm4_set(&b, FM4_TRIG, 0);
    fm4_note_on(&b, 45, 0);
    render(buf, SR / 2);
    fm4_note_on(&b, 57, 0);
    CHECK(b.oenv[0] == 0.0f && b.oenv[1] == 0.0f, "a new note kept the envelopes (%g, %g)", b.oenv[0], b.oenv[1]);

    /* two instances do not touch each other */
    {
        static fm4_t b2;
        static float a[4000], o2[4000];
        fm4_init(&b);
        fm4_init(&b2);
        fm4_note_on(&b, 40, 0);
        fm4_render(&b, a, 4000);
        fm4_note_on(&b2, 52, 0);
        fm4_render(&b2, o2, 4000);
        fm4_init(&b);
        fm4_note_on(&b, 40, 0);
        render(buf, 4000);
        CHECK(memcmp(buf, a, sizeof a) == 0, "a second instance changed the first");
    }

    printf(fails ? "fm4: %d FAILED\n" : "fm4: ok\n", fails);
    return fails != 0;
}
