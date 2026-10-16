/* SPDX-License-Identifier: GPL-3.0-only */
/* ACID: a monophonic acid bass, a float port of the zicCycle 303 (the Model:Cycles engine of the same
 * author, zic_303.cpp: ColdFire fixed point on the Elektron OS, here without the OS).
 *
 *  Color    waveform: triangle -> saw -> square -> distorted (tanh of 2 saw + square); saw and square polyBLEP
 *  Cutoff   40 Hz .. 12 kHz, exponential (100 semitones)
 *  Reso     4-pole ladder, up to self-oscillation (k = 4.2)
 *  EnvMod   depth of the filter envelope, which opens at each new note and closes over 1.5 x the note
 *           decay + 60 ms; an accented note opens it deeper and plays louder (Accent)
 *  Decay    length of the note (time constant 10 ms .. 2.4 s)
 *  Sustain  the level a held note settles at (70 % by default; 0 = it decays away, as the Cycles' do); the gate's end releases it
 *  P.Dpth   pitch envelope: centre = none, up = starts higher, down = lower (+-24 semitones, 21 ms)
 *  Slide    glide time of a slid note (5 .. 300 ms): the pitch glides, the oscillator and the filter keep
 *           running, the envelope opens again without a new attack
 *
 *  Drive    a saturation of the oscillator before the filter, its level kept: a squarer, grittier wave (0 = off, as the Cycles)
 *  Dist     a distortion after the filter, of the type DrvTyp: soft (tanh), hard (clip), fold (wavefold)
 *
 * The filter is the Cycles' one: four zero-delay-feedback one-pole stages with the feedback loop solved
 * exactly and a tanh on the loop input; its coefficients are refreshed every ACID_CTRL samples.
 * Every instance is self-contained (no mutable static state). */
#pragma once
#include <stdint.h>
#include "x0x_param.h"

#define ACID_SR 44100.0f
#define ACID_CTRL 8                       /* control rate: envelopes, pitch and the filter coefficients */

enum {
    ACID_CUTOFF, ACID_RESO, ACID_COLOR, ACID_ACCENT,       /* page 1 (EDIT) */
    ACID_DECAY, ACID_ENVMOD, ACID_PDEPTH, ACID_SLIDE,      /* page 2 (ENV) */
    ACID_TUNE,                                             /* page 3 (EDIT) */
    ACID_DRIVE, ACID_DIST, ACID_DISTTYPE,                  /* FX page */
    ACID_SUSTAIN,                                          /* ENV page, page 2 */
    ACID_NPARAMS
};

typedef struct acid {
    uint8_t pot[ACID_NPARAMS];

    /* derived from the pots */
    float cut_st;            /* cutoff, semitones from A4 */
    float reso_k;            /* ladder feedback k, 0 .. 4.2 */
    float color;             /* 0 .. 1 */
    float acc_amt;           /* accent amount 0 .. 1 */
    float env_st;            /* filter envelope depth, semitones */
    float pdepth_st;         /* pitch envelope start, semitones (signed) */
    float tune_st;           /* semitones */
    float amp_c;             /* amp envelope: per-sample multiplier */
    float fenv_c;            /* filter envelope: per control step */
    float glide_c;           /* glide: per control step */
    float drive_g;           /* gain into the saturator before the filter */
    float drive_norm;        /* 1 / tanh(drive_g): the saturator's output stays near the clean level */
    float dist_g, dist_wet, dist_norm;   /* the distortion after it: gain, how much of it (fades in over the first pots), 1 / tanh(gain) */
    int dist_type;           /* 0 soft, 1 hard, 2 fold */
    float sustain;           /* the level the decay settles at while the gate is held, 0 .. 1 */
    float rel_c;             /* the gate's release: per-sample multiplier */
    float penv_c;            /* pitch envelope: per control step (21 ms) */

    /* voice */
    int gate, idle, accent, snap;
    int cc;                  /* samples left in the control step */
    float target_st;         /* the note, semitones from A4 (plus Tune) */
    float cur_st, penv;      /* glided pitch; pitch envelope */
    float fenv;              /* filter envelope, 1 at a note */
    float amp, att_step;     /* amp envelope; its attack step per sample (0 once open) */
    float phase, inc0, inc1;
    float G, G2, G3, G4, kk, rdiv;   /* this control step's filter */
    float s1, s2, s3, s4;
    float gain;              /* output gain of the current note (accent) */
} acid_t;

void acid_init(acid_t *b);
void acid_note_on(acid_t *b, int note, int accent, int slide);
void acid_note_off(acid_t *b);
void acid_all_off(acid_t *b);
void acid_render(acid_t *b, float *out, int n);
int acid_nparams(void);
const x0x_param_t *acid_param(int i);
void acid_set(acid_t *b, int i, int value);
int acid_get(const acid_t *b, int i);
