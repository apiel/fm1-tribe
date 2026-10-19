/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE synth (tribe_syn.c): finite and bounded at the corners of its pots, the cycle's morph picks sine, triangle and square
 * (and the fold bends it), the pitch is the note, the filter types pass what they should, the cutoff and the envelope move
 * the filter, the effect selector applies only the effect it names (and FxPar is its one parameter), a slide keeps the
 * envelope, the release ends in silence, two instances do not touch each other, and a default note is about as loud as
 * LEAD's. */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "tribe_syn.h"
#include "cycle.h"

#define SR 44100
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void render(tsyn_t *b, float *o, int n)
{
    int i;
    for (i = 0; i < n; i += 64)
        tsyn_render(b, o + i, n - i < 64 ? n - i : 64);
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
static double diff(const float *x, const float *y, int n)
{
    double s = 0;
    int i;
    for (i = 0; i < n; i++)
        s += ((double)x[i] - y[i]) * ((double)x[i] - y[i]);
    return sqrt(s / n);
}
static int finite_bounded(const float *x, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (!isfinite(x[i]) || fabs(x[i]) > 1.0001f)
            return 0;
    return 1;
}

/* a fresh synth with a note held, the pots set from a list of (index, value) pairs */
static void make(tsyn_t *b, int note, const int *pv, int npv)
{
    int i;
    tsyn_init(b);
    for (i = 0; i + 1 < npv; i += 2)
        tsyn_set(b, pv[i], pv[i + 1]);
    tsyn_note_on(b, note, 0);
}

#define N (SR / 2)
static float buf[N], buf2[N];

int main(void)
{
    tsyn_t b, c;
    int i, k, v;
    static float o[N];

    /* the cycle: the morph, the skew and the fold, at known phases */
    CHECK(fabs(tsyn_shape(0.125f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f) - 0.7071f) < 0.01f, "sine at 45 degrees is 0.707");
    CHECK(tsyn_shape(0.25f, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f) > 0.99f, "the square is +1 in its first half");
    CHECK(tsyn_shape(0.75f, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f) < -0.99f, "the square is -1 in its second half");
    CHECK(fabs(tsyn_shape(0.25f, 0.3333f, 0.5f, 0.0f, 0.0f, 0.0f)) < 0.01f, "the triangle is 0 at a quarter turn");
    CHECK(fabs(tsyn_shape(0.125f, 0.0f, 0.5f, 0.0f, 0.0f, 1.0f) - tsyn_shape(0.125f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f)) > 0.05f, "the fold bends the sine");
    CHECK(fabs(tsyn_shape(0.3f, 0.5f, 0.2f, 0.0f, 0.0f, 0.0f) - tsyn_shape(0.3f, 0.5f, 0.2f, 0.0f, 0.0f, 0.0f)) == 0.0f, "the cycle is a pure function");

    CHECK(fabs(tsyn_shape(0.125f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f) - tsyn_shape(0.125f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f)) > 0.05f, "Harm3 changes the cycle");
    {   /* Phase starts the cycle later: the same note, another sample at the start */
        int pv0[] = { TS_WAVE, 0, TS_CUTOFF, 127, TS_RESO, 0, TS_KEYTRK, 0, TS_ENVMOD, 0, TS_SUSTAIN, 127, TS_ATTACK, 0 };
        int pv1[] = { TS_WAVE, 0, TS_CUTOFF, 127, TS_RESO, 0, TS_KEYTRK, 0, TS_ENVMOD, 0, TS_SUSTAIN, 127, TS_ATTACK, 0, TS_PHASE, 64 };
        make(&b, 45, pv0, sizeof pv0 / sizeof pv0[0]);
        render(&b, buf, N / 4);
        make(&c, 45, pv1, sizeof pv1 / sizeof pv1[0]);
        render(&c, buf2, N / 4);
        CHECK(diff(buf, buf2, N / 4) > 0.02, "Phase moves the start of the cycle");
    }

    /* the cycle table (cycle.c) reads the exact generator: close on the sine and the triangle, and within a sample's
     * smearing on the saw and the square, whose corners the table cannot hold */
    {
        static const float waves[] = { 0.0f, 0.5f, 0.9f, 1.0f };
        int wi;
        for (wi = 0; wi < 4; wi++) {
            cycle_p cp = { waves[wi], 0.5f, 0.2f, -0.1f, 0.3f };
            static float tab[CYCLE_N + 1];
            double e = 0, far = 0;
            int j;
            cycle_build(tab, &cp);
            for (j = 0; j < 4000; j++) {
                float ph = (float)j / 4000.0f;
                double d = fabs((double)cycle_read(tab, ph) - cycle_shape(ph, &cp));
                e += d;
                if (d > 0.1) far += 1.0 / 4000.0;
            }
            e /= 4000.0;
            CHECK(e < (wi < 2 ? 0.004 : 0.02), "the table reads the cycle (wave %.1f, mean error %.4f)", waves[wi], e);
            CHECK(far < 0.05, "the table is off by more than 0.1 at only the corners (wave %.1f, %.3f of the phases)", waves[wi], far);
        }
    }

    /* corners: every pot at its min and max, and a few random sets, on a few notes */
    for (k = 0; k < TS_NPARAMS; k++) {
        for (v = 0; v < 2; v++) {
            tsyn_init(&b);
            tsyn_set(&b, k, v ? 127 : 0);
            tsyn_note_on(&b, v ? 120 : 24, 0);
            render(&b, o, N);
            CHECK(finite_bounded(o, N), "pot %d at %d is not finite or bounded", k, v ? 127 : 0);
        }
    }
    srand(7);
    for (i = 0; i < 40; i++) {
        tsyn_init(&b);
        for (k = 0; k < TS_NPARAMS; k++)
            tsyn_set(&b, k, rand() % (tsyn_param(k)->max + 1));
        tsyn_note_on(&b, rand() % 128, 0);
        render(&b, o, N);
        CHECK(finite_bounded(o, N), "random set %d is not finite or bounded", i);
    }

    /* pitch: a sine at the note, a wide open low-pass, no effect */
    {
        int pv[] = { TS_WAVE, 0, TS_CUTOFF, 127, TS_RESO, 0, TS_KEYTRK, 0, TS_ENVMOD, 0, TS_SUSTAIN, 127, TS_ATTACK, 0 };
        make(&b, 69, pv, sizeof pv / sizeof pv[0]);
        render(&b, o, N);
        {
            double f = hz(o + N / 4, N - N / 4);
            CHECK(fabs(f - 440.0) < 440.0 * 0.01, "the sine plays A4 at 440 Hz (got %.1f)", f);
        }
    }

    /* the filter types on a 440 Hz sine with the cutoff at its floor (40 Hz): LP is quiet, HP and notch pass it */
    {
        double lp = 0, hp = 0, nt = 0;
        int pv[][2] = { {TS_FTYPE, 0}, {TS_CUTOFF, 0}, {TS_KEYTRK, 0}, {TS_RESO, 0}, {TS_WAVE, 0}, {TS_ENVMOD, 0}, {TS_SUSTAIN, 127}, {TS_ATTACK, 0} };
        tsyn_t t;
        int j;
        for (j = 0; j < 3; j++) {
            int ftype[] = { TF_LP, TF_HP, TF_NOTCH };
            double r;
            tsyn_init(&t);
            for (i = 0; i < 8; i++)
                tsyn_set(&t, pv[i][0], pv[i][1]);
            tsyn_set(&t, TS_FTYPE, ftype[j]);
            tsyn_note_on(&t, 69, 0);
            render(&t, o, N);
            r = rms(o + N / 2, N / 2);
            if (j == 0) lp = r;
            else if (j == 1) hp = r;
            else nt = r;
        }
        CHECK(hp > 5.0 * lp, "HP passes a 440 Hz sine that LP blocks (hp %.4f, lp %.4f)", hp, lp);
        CHECK(nt > 5.0 * lp, "the notch passes it too (notch %.4f, lp %.4f)", nt, lp);
    }

    /* the cutoff: a brighter saw has more energy than a dark one */
    {
        double dark, bright;
        int pvd[] = { TS_WAVE, 85, TS_CUTOFF, 0, TS_RESO, 0, TS_KEYTRK, 0, TS_ENVMOD, 0, TS_SUSTAIN, 127, TS_ATTACK, 0 };
        int pvb[] = { TS_WAVE, 85, TS_CUTOFF, 127, TS_RESO, 0, TS_KEYTRK, 0, TS_ENVMOD, 0, TS_SUSTAIN, 127, TS_ATTACK, 0 };
        make(&b, 45, pvd, sizeof pvd / sizeof pvd[0]);
        render(&b, o, N);
        dark = rms(o + N / 2, N / 2);
        make(&c, 45, pvb, sizeof pvb / sizeof pvb[0]);
        render(&c, o, N);
        bright = rms(o + N / 2, N / 2);
        CHECK(bright > 2.0 * dark, "the cutoff brightens the saw (dark %.4f, bright %.4f)", dark, bright);
    }

    /* the envelope opens the filter: with EnvMod the attack is brighter than without */
    {
        double none, mod;
        int pv0[] = { TS_WAVE, 85, TS_CUTOFF, 10, TS_RESO, 0, TS_KEYTRK, 0, TS_ENVMOD, 0, TS_ATTACK, 60, TS_DECAY, 100, TS_SUSTAIN, 0 };
        int pv1[] = { TS_WAVE, 85, TS_CUTOFF, 10, TS_RESO, 0, TS_KEYTRK, 0, TS_ENVMOD, 127, TS_ATTACK, 60, TS_DECAY, 100, TS_SUSTAIN, 0 };
        make(&b, 45, pv0, sizeof pv0 / sizeof pv0[0]);
        render(&b, o, N / 4);
        none = rms(o, N / 4);
        make(&c, 45, pv1, sizeof pv1 / sizeof pv1[0]);
        render(&c, o, N / 4);
        mod = rms(o, N / 4);
        CHECK(mod > 1.5 * none, "EnvMod opens the filter on the attack (none %.4f, mod %.4f)", none, mod);
    }

    /* the envelope folds the cycle: EnvWav changes the sound, and at 0 it changes nothing */
    {
        int pv0[] = { TS_WAVE, 0, TS_CUTOFF, 127, TS_RESO, 0, TS_KEYTRK, 0, TS_ENVMOD, 0, TS_ENVWAV, 0, TS_SUSTAIN, 127, TS_ATTACK, 0 };
        int pv1[] = { TS_WAVE, 0, TS_CUTOFF, 127, TS_RESO, 0, TS_KEYTRK, 0, TS_ENVMOD, 0, TS_ENVWAV, 127, TS_SUSTAIN, 127, TS_ATTACK, 0 };
        make(&b, 45, pv0, sizeof pv0 / sizeof pv0[0]);
        render(&b, buf, N / 4);
        make(&c, 45, pv1, sizeof pv1 / sizeof pv1[0]);
        render(&c, buf2, N / 4);
        CHECK(diff(buf, buf2, N / 4) > 0.02, "EnvWav folds the cycle as the envelope rises");
    }

    /* the effect selector: each effect changes the sound, Off changes nothing, and FxPar is what the effect reads */
    {
        int base[] = { TS_WAVE, 85, TS_CUTOFF, 110, TS_RESO, 0, TS_KEYTRK, 0, TS_ENVMOD, 0, TS_SUSTAIN, 127, TS_ATTACK, 0 };
        int nb = sizeof base / sizeof base[0];
        make(&b, 45, base, nb);
        render(&b, buf, N / 4);
        for (k = TX_DRIVE; k < TX_NFX; k++) {
            int pv[sizeof base / sizeof base[0] + 4];
            memcpy(pv, base, sizeof base);
            pv[nb] = TS_FXTYP;
            pv[nb + 1] = k;
            pv[nb + 2] = TS_FXPAR;
            pv[nb + 3] = 127;
            make(&c, 45, pv, nb + 4);
            render(&c, buf2, N / 4);
            CHECK(finite_bounded(buf2, N / 4), "effect %d is bounded", k);
            CHECK(diff(buf, buf2, N / 4) > 0.005, "effect %d changes the sound", k);
        }
        {   /* Off ignores FxPar */
            int pvA[sizeof base / sizeof base[0] + 4], pvB[sizeof base / sizeof base[0] + 4];
            memcpy(pvA, base, sizeof base);
            memcpy(pvB, base, sizeof base);
            pvA[nb] = TS_FXTYP; pvA[nb + 1] = TX_OFF; pvA[nb + 2] = TS_FXPAR; pvA[nb + 3] = 0;
            pvB[nb] = TS_FXTYP; pvB[nb + 1] = TX_OFF; pvB[nb + 2] = TS_FXPAR; pvB[nb + 3] = 127;
            make(&c, 45, pvA, nb + 4);
            render(&c, buf2, N / 4);
            make(&b, 45, pvB, nb + 4);
            render(&b, o, N / 4);
            CHECK(diff(buf2, o, N / 4) == 0.0, "Off ignores FxPar");
        }
        {   /* the crusher at FxPar 0 is off: the same as no effect */
            int pvA[sizeof base / sizeof base[0] + 4];
            memcpy(pvA, base, sizeof base);
            pvA[nb] = TS_FXTYP; pvA[nb + 1] = TX_CRUSH; pvA[nb + 2] = TS_FXPAR; pvA[nb + 3] = 0;
            make(&c, 45, pvA, nb + 4);
            render(&c, buf2, N / 4);
            CHECK(diff(buf, buf2, N / 4) == 0.0, "the crusher at FxPar 0 does nothing");
        }
    }

    /* a slide over a held note keeps the envelope; a fresh note re-attacks */
    {
        int pv[] = { TS_ATTACK, 0, TS_SUSTAIN, 127 };
        make(&b, 60, pv, sizeof pv / sizeof pv[0]);
        render(&b, o, N / 10);
        CHECK(b.stage == 1, "the envelope is past its attack after a tenth of a second");
        tsyn_note_on(&b, 64, 1);
        CHECK(b.stage == 1, "a slide over a held note keeps the envelope");
        tsyn_note_on(&b, 67, 0);
        CHECK(b.stage == 0, "a fresh note re-attacks");
    }

    /* the release ends in silence, and the instance goes idle */
    {
        int pv[] = { TS_RELEASE, 0, TS_SUSTAIN, 127 };
        make(&b, 60, pv, sizeof pv / sizeof pv[0]);
        render(&b, o, N / 10);
        tsyn_note_off(&b);
        render(&b, o, N);
        CHECK(b.idle, "the release ends and the voice goes idle");
        CHECK(fabs(o[N - 1]) == 0.0f, "the release ends in silence");
    }

    /* two instances do not touch each other */
    {
        tsyn_t x, y;
        float ox[N / 4], oy[N / 4];
        tsyn_init(&x);
        tsyn_init(&y);
        tsyn_note_on(&x, 60, 0);
        render(&x, ox, N / 4);
        render(&y, oy, N / 4);
        CHECK(rms(oy, N / 4) == 0.0, "an idle instance stays silent while another plays");
    }

    /* a default note is about as loud as LEAD's (rms 0.17) */
    {
        double r;
        tsyn_t d;
        tsyn_init(&d);
        tsyn_note_on(&d, 57, 0);
        render(&d, o, N);
        r = rms(o + N / 4, N / 2);
        CHECK(r > 0.12 && r < 0.24, "a default note is about LEAD's level (rms %.3f)", r);
    }

    if (fails) {
        printf("%d failures\n", fails);
        return 1;
    }
    printf("TRIBE synth: all checks passed\n");
    return 0;
}
