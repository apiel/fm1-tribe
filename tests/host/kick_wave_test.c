/* SPDX-License-Identifier: GPL-3.0-only */
/* KickWave (kicks.c): the wave's output for a set of its pots must match the per-sample generator it replaced (the
 * table of cycle.c). The reference numbers are the level and the sum recorded before the table: the level within 0.002,
 * the sum within 3 % (the table smears the discontinuities of the saw and the square by a fraction of a sample). */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "kicks.h"

static int fails;

#define NS 22050
static float o[NS];

struct ref { const char *name; double rms, sum; };
static const struct ref REF[] = {
    { "default", 0.629619, -14477.656759 }, { "saw", 0.588556, -1217.950161 }, { "square", 0.656855, -15763.583950 },
    { "harm", 0.643007, -14047.686918 }, { "fold", 0.656530, 50141.515790 }, { "phase", 0.616225, -22135.554626 },
};

static void check(const char *name, double rms, double sum)
{
    int i;
    for (i = 0; i < (int)(sizeof REF / sizeof REF[0]); i++) {
        if (strcmp(REF[i].name, name))
            continue;
        if (fabs(rms - REF[i].rms) > 0.002 || fabs(sum - REF[i].sum) > 0.03 * fabs(REF[i].sum)) {
            printf("FAIL %s: rms %.6f (was %.6f), sum %.6f (was %.6f)\n", name, rms, REF[i].rms, sum, REF[i].sum);
            fails++;
        }
    }
}

static void run(const char *name, int wave, int h2, int h3, int skew, int fold, int phase)
{
    int inst = kick_alloc(0), i;
    double s = 0, a = 0;
    int n;
    kick_set(inst, 5, wave);
    kick_set(inst, 6, h2);
    kick_set(inst, 7, h3);
    kick_set(inst, 8, skew);
    kick_set(inst, 9, fold);
    kick_set(inst, 10, phase);
    kick_trigger(inst, 60, 1.0f);
    for (n = 0; n < NS; n += 64)
        kick_render(inst, o + n, NS - n < 64 ? NS - n : 64);
    for (i = 0; i < NS; i++) {
        s += (double)o[i] * (i + 1) * 1e-3;
        a += (double)o[i] * o[i];
    }
    printf("%-10s rms %.6f sum %.6f\n", name, sqrt(a / NS), s);
    check(name, sqrt(a / NS), s);
    kick_free(inst);
}

int main(void)
{
    kicks_init();
    run("default", 0, 64, 64, 50, 0, 0);
    run("saw", 50, 64, 64, 50, 0, 0);
    run("square", 100, 64, 64, 50, 0, 0);
    run("harm", 30, 100, 20, 30, 0, 0);
    run("fold", 70, 64, 64, 80, 60, 0);
    run("phase", 10, 64, 64, 40, 0, 50);
    if (fails) {
        printf("%d failures\n", fails);
        return 1;
    }
    printf("KickWave table: matches the per-sample wave\n");
    return 0;
}
