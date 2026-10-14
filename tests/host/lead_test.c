/* SPDX-License-Identifier: GPL-3.0-only */
/* LEAD (lead.c): finite and bounded at the corners of its controls, the pitch asked, a unison that beats,
 * legato (a note that arrives on a sounding one glides and does not start the envelope again; with Legato
 * off it does), sustain and release, idle silence, vibrato, and two instances that do not touch each other.
 * Writes build/lead_demo.wav (the starting project's two bars) for listening. */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "lead.h"

#define SR 44100
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void render(lead_t *b, float *o, int n)
{
    int i;
    for (i = 0; i < n; i += 64)
        lead_render(b, o + i, n - i < 64 ? n - i : 64);
}
static double rms(const float *x, int n)
{
    double s = 0;
    int i;
    for (i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / n);
}
static double hz(const float *x, int n)
{
    int i, c = 0;
    for (i = 1; i < n; i++)
        c += x[i - 1] < 0 && x[i] >= 0;
    return (double)c * SR / n;
}
static double beating(const float *x, int n)             /* variation of the level over 20 ms windows */
{
    int w, nw = n / 880;
    double s = 0, s2 = 0, m, v;
    for (w = 0; w < nw; w++) {
        double r = rms(x + w * 880, 880);
        s += r;
        s2 += r * r;
    }
    m = s / nw;
    v = s2 / nw - m * m;
    return sqrt(v > 0 ? v : 0) / (m + 1e-9);
}
static void plain(lead_t *b)                              /* one saw, the filter open: the oscillator alone */
{
    lead_init(b);
    lead_set(b, LEAD_UNISON, 0);
    lead_set(b, LEAD_SUB, 0);
    lead_set(b, LEAD_CUTOFF, 127);
    lead_set(b, LEAD_RESO, 0);
    lead_set(b, LEAD_ENVMOD, 0);
    lead_set(b, LEAD_ATTACK, 0);
    lead_set(b, LEAD_SUSTAIN, 127);
}

static float buf[SR * 3];

int main(void)
{
    static lead_t a, b2;
    int i, c, k;
    double r1, r2, f;

    for (c = 0; c < 64; c++) {                            /* corners: finite, bounded */
        lead_init(&a);
        for (k = 0; k < LEAD_NPARAMS; k++)
            lead_set(&a, k, ((c >> (k % 6)) & 1) ? 127 : 0);
        lead_note_on(&a, 24 + c, c & 1, 0);
        render(&a, buf, SR / 2);
        lead_note_on(&a, 60, 1, 1);
        render(&a, buf + SR / 2, SR / 2);
        for (i = 0; i < SR; i++)
            if (!isfinite(buf[i]) || fabsf(buf[i]) > 0.5001f) {
                CHECK(0, "corner %d: sample %d = %g", c, i, buf[i]);
                break;
            }
    }

    plain(&a);                                            /* pitch */
    lead_note_on(&a, 45, 0, 0);
    render(&a, buf, SR);
    f = hz(buf + 4410, SR - 4410);
    CHECK(fabs(f - 110.0) < 2.5, "A2 is %.1f Hz", f);
    lead_note_on(&a, 57, 0, 0);
    render(&a, buf, SR);
    f = hz(buf + 4410, SR - 4410);
    CHECK(fabs(f - 220.0) < 4.5, "A3 is %.1f Hz", f);

    plain(&a);                                            /* the unison beats; one oscillator does not */
    lead_note_on(&a, 45, 0, 0);
    render(&a, buf, SR * 2);
    r1 = beating(buf + SR / 2, SR);
    plain(&a);
    lead_set(&a, LEAD_UNISON, 6);
    lead_set(&a, LEAD_DETUNE, 70);
    lead_note_on(&a, 45, 0, 0);
    render(&a, buf, SR * 2);
    r2 = beating(buf + SR / 2, SR);
    CHECK(r2 > r1 * 3.0 + 0.02, "the unison does not beat (%g vs %g)", r2, r1);

    for (k = 0; k < 2; k++) {                             /* legato: On glides and keeps the level; Off starts the envelope again (it swells back from the sustain) */
        plain(&a);
        lead_set(&a, LEAD_LEGATO, k);
        lead_set(&a, LEAD_DECAY, 20);
        lead_set(&a, LEAD_SUSTAIN, 40);
        lead_set(&a, LEAD_RELEASE, 5);
        lead_set(&a, LEAD_SLIDE, 20);
        lead_note_on(&a, 45, 0, 0);
        render(&a, buf, SR);
        r1 = rms(buf + SR - 2000, 2000);
        lead_note_off(&a);                                /* the gate ends and the next note comes at once */
        lead_note_on(&a, 57, 0, 0);
        render(&a, buf, SR);
        r2 = rms(buf + 50, 400);                          /* the first 10 ms: the swell, if there is one */
        if (k) {
            CHECK(r2 < r1 * 1.25 && r2 > r1 * 0.75, "legato On: the level moves (%g after, %g before)", r2, r1);
            f = hz(buf + SR / 2, SR / 2);
            CHECK(fabs(f - 220.0) < 5.0, "legato On: settles at %.1f Hz", f);
        } else {
            CHECK(r2 > r1 * 1.6, "legato Off: no new attack (%g after, %g before)", r2, r1);
        }
    }

    plain(&a);                                            /* the slide flag glides with Legato off too */
    lead_set(&a, LEAD_LEGATO, 0);
    lead_set(&a, LEAD_DECAY, 20);
    lead_set(&a, LEAD_SUSTAIN, 40);
    lead_note_on(&a, 45, 0, 0);
    render(&a, buf, SR);
    r1 = rms(buf + SR - 2000, 2000);
    lead_note_on(&a, 57, 0, 1);
    render(&a, buf, SR / 2);
    CHECK(rms(buf + 50, 400) < r1 * 1.25, "slide: the envelope starts again (%g vs %g)", rms(buf + 50, 400), r1);

    plain(&a);                                            /* sustain and release */
    lead_set(&a, LEAD_DECAY, 20);
    lead_set(&a, LEAD_SUSTAIN, 90);
    lead_note_on(&a, 45, 0, 0);
    render(&a, buf, SR * 2);
    CHECK(rms(buf + SR * 3 / 2, 4410) > 0.1, "no sustain (%g)", rms(buf + SR * 3 / 2, 4410));
    lead_note_off(&a);
    render(&a, buf, SR * 2);
    c = 0;
    for (i = SR; i < SR * 2; i++)
        c += buf[i] != 0.0f;
    CHECK(c == 0, "%d non-zero samples after the release", c);
    lead_note_on(&a, 45, 0, 0);                           /* and it plays again from idle */
    render(&a, buf, 8000);
    CHECK(rms(buf + 2000, 6000) > 0.05, "no sound after idle");

    plain(&a);                                            /* sustain 0: the note dies away while held */
    lead_set(&a, LEAD_DECAY, 20);
    lead_set(&a, LEAD_SUSTAIN, 0);
    lead_note_on(&a, 45, 0, 0);
    render(&a, buf, SR * 2);
    CHECK(rms(buf + SR * 3 / 2, 4410) < 0.001, "sustain 0 still sounds (%g)", rms(buf + SR * 3 / 2, 4410));

    plain(&a);                                            /* vibrato changes the sound */
    lead_note_on(&a, 45, 0, 0);
    render(&a, buf, SR);
    plain(&b2);
    lead_set(&b2, LEAD_VIB, 127);
    lead_note_on(&b2, 45, 0, 0);
    render(&b2, buf + SR, SR);
    CHECK(memcmp(buf, buf + SR, SR * sizeof(float)) != 0, "vibrato changes nothing");

    {   /* ring: off is the plain sound (bit for bit); on, a different one, still bounded */
        static float ref[8000], rg[8000];
        plain(&a);
        lead_note_on(&a, 45, 0, 0);
        render(&a, ref, 8000);
        plain(&a);
        lead_set(&a, LEAD_RING, 0);
        lead_note_on(&a, 45, 0, 0);
        render(&a, rg, 8000);
        CHECK(memcmp(ref, rg, sizeof ref) == 0, "ring 0 is not bypassed");
        plain(&a);
        lead_set(&a, LEAD_RING, 127);
        lead_set(&a, LEAD_RINGFQ, 90);
        lead_note_on(&a, 45, 0, 0);
        render(&a, rg, 8000);
        CHECK(memcmp(ref, rg, sizeof ref) != 0 && rms(rg, 8000) > 0.01, "ring changes nothing (rms %g)", rms(rg, 8000));
        for (i = 0; i < 8000; i++)
            if (!isfinite(rg[i]) || fabsf(rg[i]) > 0.5001f) {
                CHECK(0, "ring: sample %d = %g", i, rg[i]);
                break;
            }
    }

    lead_init(&a);                                        /* two instances */
    lead_init(&b2);
    lead_note_on(&a, 40, 1, 0);
    render(&a, buf, 4000);
    render(&b2, buf + 4000, 4000);
    CHECK(rms(buf + 4000, 4000) == 0.0, "an idle instance sounds");
    lead_note_on(&b2, 40, 1, 0);
    render(&b2, buf + 4000, 4000);
    CHECK(memcmp(buf, buf + 4000, 4000 * sizeof(float)) == 0, "the two instances differ");

    {   /* the demo: the starting project's two bars (a slide step glides into the next note) */
        static const struct { int step, note, len, slide, vel; } L[] = {
            {0, 57, 31, 1, 100}, {8, 60, 31, 1, 100}, {16, 64, 31, 1, 100}, {20, 62, 15, 0, 120},
            {24, 60, 23, 1, 100}, {28, 59, 15, 0, 100},
        };
        int step = SR * 60 / 125 / 4, total = step * 32 * 2, pos = 0, n, prev_slide = 0;
        static float demo[SR * 12];
        FILE *w;
        lead_init(&a);
        while (pos < total) {
            int s = (pos / step) % 32, j, gate_end = -1;
            for (j = 0; j < 6; j++)
                if (L[j].step == s && pos % step == 0) {
                    lead_note_on(&a, L[j].note, L[j].vel > 100, prev_slide);
                    prev_slide = L[j].slide;
                    gate_end = pos + L[j].len * step / 4;
                }
            n = step;
            if (gate_end >= 0 && gate_end < pos + step)
                n = gate_end - pos;
            render(&a, demo + pos, n);
            if (gate_end >= 0 && gate_end <= pos + step && !prev_slide) {
                lead_note_off(&a);
                render(&a, demo + pos + n, step - n);
            } else if (n < step) {
                render(&a, demo + pos + n, step - n);
            }
            pos += step;
        }
        mkdir("build", 0777);
        w = fopen("build/lead_demo.wav", "wb");
        if (w) {
            unsigned hdr[11] = {0x46464952, 36 + (unsigned)pos * 2, 0x45564157, 0x20746d66, 16, 0x10001, SR, SR * 2, 0x100002, 0x61746164, (unsigned)pos * 2};
            short sm;
            fwrite(hdr, 4, 11, w);
            for (i = 0; i < pos; i++) {
                sm = (short)(demo[i] * 56000.0f);
                fwrite(&sm, 2, 1, w);
            }
            fclose(w);
        }
    }
    printf(fails ? "lead: %d FAILED\n" : "lead: ok\n", fails);
    return fails != 0;
}
