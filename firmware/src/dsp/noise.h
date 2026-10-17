/* SPDX-License-Identifier: GPL-3.0-only */
/* NOISE: a mono noise engine (docs/plans/2026-10-17-noise-engine.md). The noise is white, pink or brown, then a
 * state-variable filter (svf.h) with a choice of type, an overdrive, and an amp envelope that also opens the filter.
 * The note only moves the filter (KeyTrk): with a resonant band-pass that makes a pitched, tonal noise. The clip's LFO
 * modulates the pots, as for the other synths.
 *
 *  NType    white, pink, brown, dust (sparse clicks)
 *  FType    LP, HP, BP, Notch
 *  Cutoff   40 Hz .. 12 kHz, exponential
 *  Reso     the filter's Q: 0.5 .. 24 (LP, HP, BP), 0.5 .. 4 for the notch (a wider notch is audible on noise)
 *  KeyTrk   the cutoff follows the note: 0 .. 1 semitone a semitone
 *  Mod      a built-in modulation, with no second LFO: Sine (the cutoff wobbles), Rand (the cutoff jumps at random),
 *           Drift (the cutoff drifts at random), Gate (the level pulses). ModAmt is its depth (up to 2 octaves on the
 *           cutoff); ModRat its rate, 0.1 .. 51 Hz. The modulation is in floats, so it is not stepped by the pots.
 *  Attack, Decay, Sustain, Release   the envelope; it opens the filter by EnvMod as well
 *  Drive    gain into the filter
 *
 * One voice: a note over a sounding one with the slide flag only changes the note the filter follows. Every
 * instance is self-contained (no mutable static state). */
#pragma once
#include <stdint.h>
#include "x0x_param.h"
#include "svf.h"

#define NOISE_SR 44100.0f
#define NOISE_CTRL 8                     /* control rate: the envelope and the filter's coefficients */

enum {
    NOISE_NTYPE, NOISE_FTYPE, NOISE_CUTOFF, NOISE_RESO,                /* EDIT, page 1 */
    NOISE_KEYTRK, NOISE_MOD, NOISE_MODAMT, NOISE_MODRAT,               /* EDIT, page 2 */
    NOISE_ATTACK, NOISE_DECAY, NOISE_SUSTAIN, NOISE_RELEASE, NOISE_ENVMOD,   /* ENV */
    NOISE_DRIVE,                                                       /* FX */
    NOISE_NPARAMS
};
enum { NT_WHITE, NT_PINK, NT_BROWN, NT_DUST };
enum { NF_LP, NF_HP, NF_BP, NF_NOTCH };
enum { NM_OFF, NM_SINE, NM_RAND, NM_DRIFT, NM_GATE };

typedef struct noise {
    uint8_t pot[NOISE_NPARAMS];

    /* derived from the pots */
    int ntype, ftype;
    float cut_st, reso_n, keytrk, env_st, drive_g;   /* reso_n: the resonance as 0 .. 1 */
    float att_step, dec_c, sustain, rel_c;
    int mod;                      /* NM_* */
    float mod_amt, mod_inc;       /* the depth 0 .. 1, and the modulator's phase step per control step */
    float mod_ph, mod_v, mod_hold, mod_gain;   /* its phase, its value (-1 .. 1), the value it takes at a step, the level */

    /* voice */
    float note_st;                /* the note from A4 in semitones: the filter follows it by KeyTrk */
    int gate, idle, stage;        /* stage: 0 attack, 1 decay / sustain, 2 release */
    int cc;
    float env;
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
