/* SPDX-License-Identifier: GPL-3.0-only */
/* CYCLE: the KickWave's wavetable generator, shared by the kick and the TRIBE synth. One period of a morph of sine,
 * triangle, saw and square, with a skew (where the peak sits), a second and third harmonic and a sine fold.
 *
 * Computing a cycle costs a few sines per sample, so an engine builds a table of one period when a shape pot changes
 * (cycle_build) and reads it with interpolation while it plays (cycle_read). An engine that changes the shape while it
 * plays (an envelope on the fold) keeps calling cycle_shape per sample. */
#pragma once

#define CYCLE_N 256                       /* the table's points; the table holds CYCLE_N + 1 (the last repeats the first) */

typedef struct {
    float wave;                           /* the morph: 0 sine .. 1 square, 0 .. 1 */
    float skew;                           /* where the cycle peaks: 0.05 .. 0.95 */
    float harm2, harm3;                   /* the second and third harmonics, -1 .. 1 */
    float fold;                           /* the sine fold, 0 .. 1 */
} cycle_p;

/* one sample of the cycle at phase ph (any value: the fraction is taken) */
float cycle_shape(float ph, const cycle_p *c);

/* the table: one period, t[i] = cycle_shape(i / CYCLE_N). t has CYCLE_N + 1 entries */
void cycle_build(float *t, const cycle_p *c);

/* the table at phase ph, in [0, 1), linearly interpolated */
static inline float cycle_read(const float *t, float ph)
{
    float x = ph * (float)CYCLE_N, f;
    int i = (int)x;
    if (i >= CYCLE_N)
        i = CYCLE_N - 1;
    f = x - (float)i;
    return t[i] + f * (t[i + 1] - t[i]);
}
