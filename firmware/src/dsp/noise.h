/* SPDX-License-Identifier: GPL-3.0-only */
/* NOISE: a mono engine built around noise (docs/plans/2026-10-17-noise-engine.md). An oscillator morphs through
 * five anchors, square, noise, saw, noise, tri, and the noise floor is always there: at a noise anchor the
 * oscillator is gone and only the noise plays. The noise is white, pink or brown. Then a state-variable filter
 * (svf.h) with a choice of type, an overdrive, and an amp envelope that also opens the filter. The clip's LFO
 * modulates the pots, as for the other synths.
 *
 *  Morph    0 square, 1/4 noise, 1/2 saw, 3/4 noise, 1 tri (the waves fade through the noise)
 *  Noise    the noise floor: the level of the noise at every point of the morph
 *  NType    white, pink, brown                                  Tune   +-1.5 semitones
 *  FType    LP, HP, BP, Notch                                   Cutoff 40 Hz .. 12 kHz, exponential
 *  Reso     the filter's Q, 0.5 .. 24                            KeyTrk the cutoff follows the note (0 .. 1 semitone a semitone)
 *  Attack, Decay, Sustain, Release   the envelope; it opens the filter by EnvMod as well
 *  Drive    gain into the filter
 *
 * One voice: a note over a sounding one with the slide flag only changes the pitch. Every instance is
 * self-contained (no mutable static state). */
#pragma once
#include <stdint.h>
#include "x0x_param.h"
#include "svf.h"

#define NOISE_SR 44100.0f
#define NOISE_CTRL 8                     /* control rate: the morph, the envelope and the filter's coefficients */

enum {
    NOISE_MORPH, NOISE_NOISE, NOISE_NTYPE, NOISE_TUNE,                 /* EDIT, page 1 */
    NOISE_FTYPE, NOISE_CUTOFF, NOISE_RESO, NOISE_KEYTRK,               /* EDIT, page 2 */
    NOISE_ATTACK, NOISE_DECAY, NOISE_SUSTAIN, NOISE_RELEASE, NOISE_ENVMOD,   /* ENV */
    NOISE_DRIVE,                                                       /* FX */
    NOISE_NPARAMS
};
enum { NT_WHITE, NT_PINK, NT_BROWN };
enum { NF_LP, NF_HP, NF_BP, NF_NOTCH };

typedef struct noise {
    uint8_t pot[NOISE_NPARAMS];

    /* derived from the pots */
    float morph;                  /* 0 .. 4: the position over the four segments of the morph */
    float floor_;                 /* the noise floor, 0 .. 1 */
    int ntype, ftype;
    float tune_st, cut_st, reso_k, keytrk, env_st, drive_g;
    float att_step, dec_c, sustain, rel_c;

    /* voice */
    float inc, note_st;           /* the oscillator's phase step per sample, the note from A4 in semitones */
    float gsq, gsaw, gtri, gnoise;   /* the morph at this control step: each wave's gain, and the noise's */
    int gate, idle, stage;        /* stage: 0 attack, 1 decay / sustain, 2 release */
    int cc;
    float env;
    float ph;
    uint32_t rng;                 /* the noise's random state (xorshift32) */
    float pk0, pk1, pk2;          /* pink: the three poles */
    float brown;                  /* brown: the integrator */
    svf_t fl;
} noise_t;

void noise_init(noise_t *b);
void noise_note_on(noise_t *b, int note, int slide);
void noise_note_off(noise_t *b);
void noise_all_off(noise_t *b);
void noise_render(noise_t *b, float *out, int n);
int noise_nparams(void);
const x0x_param_t *noise_param(int i);
void noise_set(noise_t *b, int i, int value);
int noise_get(const noise_t *b, int i);
