/* SPDX-License-Identifier: GPL-3.0-only */
/* SVF: a state-variable filter (Andrew Simper's zero-delay form), shared by the sound engines. One filter per
 * voice. The coefficients come from a cutoff in Hz and a damping k = 1 / Q, set at a control rate (svf_set),
 * and each sample gives a low-pass, a band-pass and a high-pass (svf_tick). The band-pass is scaled by sqrt(k), so
 * its loudness on noise stays about the same whatever the resonance (its peak grows with Q, and a resonant band
 * sings instead of vanishing); a notch is low + high. The cutoff is clamped to 20 Hz .. 0.45 x the sample rate. */
#pragma once

typedef struct {
    float ic1, ic2;                   /* the two integrator states */
    float k, kg, a1, a2, a3;          /* damping, the band-pass gain sqrt(k), and the coefficients */
} svf_t;

void svf_init(svf_t *f);
void svf_set(svf_t *f, float fc_hz, float k, float sr);
void svf_tick(svf_t *f, float x, float *lp, float *bp, float *hp);
