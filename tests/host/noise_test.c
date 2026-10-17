/* SPDX-License-Identifier: GPL-3.0-only */
/* NOISE (noise.c): finite and bounded at the corners of its controls, the three colours differ (crossings and level),
 * HP has more energy than LP on noise at a low cutoff, KeyTrk moves the cutoff with the note, a resonant band-pass
 * keeps its loudness and rings at the note (a pitched noise), the notch moves with its cutoff and its width follows the
 * resonance, a default note is about as loud as LEAD's, a slide keeps the envelope, the release ends in silence, and
 * two instances do not touch each other. */
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
/* a crude brightness: the rms of what a one-pole low-pass leaves out */
static double bright(const float *x, int n)
{
    double lp = 0, s = 0;
    int i;
    for (i = 0; i < n; i++) {
        lp = 0.9 * lp + 0.1 * x[i];
        s += (x[i] - lp) * (x[i] - lp);
    }
    return sqrt(s / n);
}

/* the spread of the level: the loudest over the quietest 50 ms window, so a modulation shows and a steady sound does not */
static double swing(const float *x, int n)
{
    int w, win = SR / 20, nw = n / win;
    double lo = 1e9, hi = 0;
    for (w = 0; w < nw; w++) {
        double r = rms(x + w * win, win);
        if (r < lo)
            lo = r;
        if (r > hi)
            hi = r;
    }
    return lo > 0 ? hi / lo : 0;
}

/* a held note on the given filter; the envelope open at once, the cutoff fixed (no LFO, no envelope, no key tracking) */
static void plain(noise_t *b, int ntype, int ftype, int cut, int reso)
{
    noise_init(b);
    noise_set(b, NOISE_NTYPE, ntype);
    noise_set(b, NOISE_FTYPE, ftype);
    noise_set(b, NOISE_CUTOFF, cut);
    noise_set(b, NOISE_RESO, reso);
    noise_set(b, NOISE_KEYTRK, 0);
    noise_set(b, NOISE_ENVMOD, 0);
    noise_set(b, NOISE_ATTACK, 0);
    noise_set(b, NOISE_SUSTAIN, 127);
    noise_set(b, NOISE_DRIVE, 0);
}

static float buf[SR * 3];

int main(void)
{
    static noise_t a, b2;
    int i, c, k;
    double r1, r2;

    for (c = 0; c < 64; c++) {                            /* corners: finite, bounded by the output level */
        noise_init(&a);
        for (k = 0; k < NOISE_NPARAMS; k++)
            noise_set(&a, k, ((c >> (k % 6)) & 1) ? 127 : 0);
        noise_set(&a, NOISE_NTYPE, c % 4);
        noise_set(&a, NOISE_FTYPE, (c / 3) % 4);
        noise_note_on(&a, 24 + c, 0);
        render(&a, buf, SR / 2);
        noise_note_on(&a, 60, 1);
        render(&a, buf + SR / 2, SR / 2);
        for (i = 0; i < SR; i++)
            if (!isfinite(buf[i]) || fabsf(buf[i]) > 1.0001f) {
                CHECK(0, "corner %d: sample %d = %f", c, i, buf[i]);
                break;
            }
    }

    {                                                     /* the colours: white crosses zero most, brown least; the levels are close */
        static noise_t cw[3];
        double zc[3], rm[3];
        int t;
        for (t = 0; t < 3; t++) {
            plain(&cw[t], t, NF_LP, 127, 0);
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

    plain(&a, NT_WHITE, NF_LP, 0, 0);                     /* the filter: HP passes more of the noise than LP at 40 Hz */
    noise_note_on(&a, 69, 0);
    render(&a, buf, SR);
    r1 = rms(buf + SR / 4, SR / 2);
    plain(&b2, NT_WHITE, NF_HP, 0, 0);
    noise_note_on(&b2, 69, 0);
    render(&b2, buf + SR, SR);
    r2 = rms(buf + SR + SR / 4, SR / 2);
    CHECK(r2 > 3.0 * r1, "HP %.4f should pass much more than LP %.4f at 40 Hz", r2, r1);

    plain(&a, NT_WHITE, NF_LP, 40, 0);                    /* KeyTrk: the cutoff follows the note, so a high note is brighter */
    noise_set(&a, NOISE_KEYTRK, 127);
    noise_note_on(&a, 36, 0);
    render(&a, buf, SR);
    r1 = rms(buf + SR / 4, SR / 2);
    noise_note_on(&a, 96, 0);
    render(&a, buf, SR);
    r2 = rms(buf + SR / 4, SR / 2);
    CHECK(r2 > 1.5 * r1, "KeyTrk: note 96 at %.4f should be brighter than note 36 at %.4f", r2, r1);

    plain(&a, NT_WHITE, NF_BP, 70, 0);                    /* BP: a resonance keeps the loudness, it does not vanish */
    noise_note_on(&a, 69, 0);
    render(&a, buf, SR);
    r1 = rms(buf + SR / 4, SR / 2);
    noise_set(&a, NOISE_RESO, 127);
    render(&a, buf, SR);
    r2 = rms(buf + SR / 4, SR / 2);
    CHECK(r2 > 0.5 * r1 && r2 < 2.0 * r1, "BP at full resonance %.4f against %.4f at none", r2, r1);

    plain(&a, NT_WHITE, NF_BP, 70, 127);                  /* the pitched noise: a resonant BP rings at its note */
    noise_set(&a, NOISE_KEYTRK, 127);
    noise_note_on(&a, 69, 0);
    render(&a, buf, SR);
    r1 = hz(buf + SR / 4, SR / 2);
    noise_note_on(&a, 81, 0);
    render(&a, buf, SR);
    r2 = hz(buf + SR / 4, SR / 2);
    CHECK(r1 > 0.0 && fabs(r2 / r1 - 2.0) < 0.3, "pitched BP: note 81 rings at %.0f Hz, note 69 at %.0f (want ~2x)", r2, r1);

    plain(&a, NT_WHITE, NF_NOTCH, 20, 0);                 /* the notch moves with its cutoff: low and high cutoffs differ */
    noise_note_on(&a, 69, 0);
    render(&a, buf, SR);
    r1 = bright(buf + SR / 4, SR / 2);
    noise_set(&a, NOISE_CUTOFF, 120);
    render(&a, buf, SR);
    r2 = bright(buf + SR / 4, SR / 2);
    CHECK(fabs(r1 - r2) > 0.1 * r1, "notch barely moves with its cutoff: %.4f at 20, %.4f at 120", r1, r2);

    plain(&a, NT_WHITE, NF_NOTCH, 120, 0);                /* the notch's width follows the resonance, up to Q 4 */
    noise_note_on(&a, 69, 0);
    render(&a, buf, SR);
    r1 = rms(buf + SR / 4, SR / 2);
    noise_set(&a, NOISE_RESO, 127);
    render(&a, buf, SR);
    r2 = rms(buf + SR / 4, SR / 2);
    CHECK(fabs(r1 - r2) > 0.05 * r1, "notch width does nothing: %.4f at no resonance, %.4f at full", r1, r2);

    plain(&a, NT_DUST, NF_LP, 127, 0);                    /* dust: sparse clicks, so a high peak against its rms */
    noise_note_on(&a, 69, 0);
    render(&a, buf, SR);
    {
        float pk = 0;
        for (i = SR / 4; i < SR; i++)
            if (fabsf(buf[i]) > pk)
                pk = fabsf(buf[i]);
        r1 = rms(buf + SR / 4, 3 * SR / 4);
        CHECK(pk > 5.0 * r1, "dust is not clicky: peak %.3f against rms %.3f", pk, r1);
    }

    plain(&a, NT_WHITE, NF_LP, 50, 0);                    /* Mod Sine: the cutoff wobbles, so the level swings over the window */
    noise_set(&a, NOISE_MOD, NM_OFF);
    noise_note_on(&a, 69, 0);
    render(&a, buf, SR);
    r1 = swing(buf + SR / 4, SR / 2);
    plain(&a, NT_WHITE, NF_LP, 50, 0);
    noise_set(&a, NOISE_MOD, NM_SINE);
    noise_set(&a, NOISE_MODAMT, 127);
    noise_set(&a, NOISE_MODRAT, 40);
    noise_note_on(&a, 69, 0);
    render(&a, buf, SR);
    r2 = swing(buf + SR / 4, SR / 2);
    CHECK(r2 > 1.5 && r2 > 1.3 * r1, "Sine modulation does nothing: swing %.2f against %.2f with it off", r2, r1);

    plain(&a, NT_WHITE, NF_LP, 127, 0);                   /* Mod Gate: the level pulses, so the swing is large */
    noise_set(&a, NOISE_MOD, NM_GATE);
    noise_set(&a, NOISE_MODAMT, 127);
    noise_set(&a, NOISE_MODRAT, 40);
    noise_note_on(&a, 69, 0);
    render(&a, buf, SR);
    r2 = swing(buf + SR / 4, SR / 2);
    CHECK(r2 > 2.0, "Gate does not pulse: swing %.2f", r2);

    plain(&a, NT_WHITE, NF_LP, 70, 40);                   /* the default level is about LEAD's: rms 0.17 on a note */
    noise_set(&a, NOISE_CUTOFF, 70);
    noise_set(&a, NOISE_RESO, 40);
    noise_set(&a, NOISE_KEYTRK, 64);
    noise_set(&a, NOISE_ENVMOD, 50);
    noise_set(&a, NOISE_ATTACK, 20);
    noise_set(&a, NOISE_DECAY, 64);
    noise_set(&a, NOISE_SUSTAIN, 110);
    noise_set(&a, NOISE_RELEASE, 50);
    noise_note_on(&a, 45, 0);
    render(&a, buf, SR);
    r1 = rms(buf + SR / 4, SR / 2);
    CHECK(r1 > 0.1 && r1 < 0.25, "default level %.3f (want about 0.17, LEAD's)", r1);

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
