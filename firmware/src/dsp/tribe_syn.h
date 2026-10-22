/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE: a mono wavetable synth (docs/plans/2026-10-18-tribe-synth.md). The cycle is generated, not stored: a morph of
 * sine, triangle, saw and square (the KickWave's Wave), a Skew that moves the peak, a second harmonic and a sine fold.
 * Then a state-variable filter (svf.h) with a choice of type, one effect from a list (FxTyp) with one amount (FxPar),
 * and an amp envelope. The envelope also opens the filter (EnvMod) and folds the cycle (EnvWav). The clip's LFO
 * modulates any pot, as for the other synths.
 *
 *  Wave     the morph: sine, triangle, saw, square
 *  Skew     where the cycle peaks (5 .. 95 %)
 *  Harm2    the second harmonic, bipolar (64 is none)
 *  Harm3    the third harmonic, bipolar (64 is none)
 *  WvFold   a sine fold of the cycle: harmonics that grow with it
 *  Phase    where the cycle starts, 0 .. 1 turn
 *  FType    LP, HP, BP, Notch
 *  Cutoff   40 Hz .. 12 kHz, exponential
 *  Reso     the filter's Q: 0.5 .. 24 (LP, HP, BP), 0.5 .. 4 for the notch
 *  KeyTrk   the cutoff follows the note: 0 .. 1 semitone a semitone
 *  Attack, Decay, Sustain, Release   the envelope
 *  EnvMod   the envelope opens the cutoff, up to 60 semitones
 *  EnvWav   the envelope folds the cycle, up to a full fold
 *  FxTyp    Off, Drive, Crush, Decim, Ring, Dist, Fuzz, Fold, Trem, Phaser: one effect after the filter
 *  FxPar    the one parameter of that effect: drive gain, bit depth, hold, ring frequency, clip gain, fuzz gain,
 *           fold amount, tremolo rate, phaser rate
 *
 * One voice: a note over a sounding one with the slide flag only changes the note the filter follows. Every
 * instance is self-contained (no mutable static state). */
#pragma once
#include <stdint.h>
#include "x0x_param.h"
#include "svf.h"
#include "cycle.h"

#define TSYN_SR 44100.0f
#define TSYN_CTRL 8                    /* control rate: the filter's coefficients */

enum {
    TS_CUTOFF, TS_RESO, TS_FTYPE, TS_KEYTRK,                      /* EDIT, page 1: the filter */
    TS_WAVE, TS_SKEW, TS_HARM2, TS_HARM3,                         /* EDIT, page 2: the cycle */
    TS_WVFOLD, TS_PHASE,                                          /* EDIT, page 3: the cycle, continued */
    TS_ATTACK, TS_DECAY, TS_SUSTAIN, TS_RELEASE,                  /* ENV, page 1 */
    TS_ENVMOD, TS_ENVWAV,                                         /* ENV, page 2: the envelope's modulation */
    TS_FXTYP, TS_FXPAR,                                           /* FX */
    TS_NPARAMS
};
enum { TF_LP, TF_HP, TF_BP, TF_NOTCH };
enum { TX_OFF, TX_DRIVE, TX_CRUSH, TX_DECIM, TX_RING, TX_DIST, TX_FUZZ, TX_FOLD, TX_TREM, TX_PHASER, TX_NFX };

typedef struct tsyn {
    uint8_t pot[TS_NPARAMS];

    /* derived from the pots */
    int ftype, fxtyp;
    float wave, skew, harm2, harm3, fold, phase;   /* the cycle: wave 0 .. 1 (sine .. square), skew 0.05 .. 0.95, harm2 and harm3 -1 .. 1, fold 0 .. 1, phase 0 .. 1 turn */
    float tab[CYCLE_N + 1];          /* the cycle as a table, rebuilt when a shape pot moves (not while EnvWav folds it) */
    int tab_dirty;
    float cut_st, reso_n, keytrk, env_st, env_wav;
    float att_step, dec_c, sustain, rel_c;
    float fx_drive;                  /* drive: the gain into tanh */
    float crush_scale;               /* the levels per unit: 2^(bits - 1); 0 = off */
    float hold_n;                    /* decimation: samples held */
    float fx_gain, fx_fuzz, fx_fold; /* clip gain, fuzz gain, fold amount: from FxPar */
    float fx_inc;                    /* the phase step per sample of the effect that has one (ring, tremolo, phaser) */

    /* voice */
    float ph;                        /* the oscillator's phase, 0 .. 1 */
    float inc;                       /* its step per sample, from the note */
    float note_st;                   /* the note from A4 in semitones: the filter follows it by KeyTrk */
    int gate, idle, stage, cc;       /* stage: 0 attack, 1 decay / sustain, 2 release */
    float env;
    float fx_ph;                     /* that effect's phase, 0 .. 1 */
    float fx_dc;                     /* fuzz: its DC, taken out again */
    float ap[4];                     /* phaser: the four allpass states */
    float hold_c, hold_v;            /* decimation: the count and the held value */
    svf_t fl;
} tsyn_t;

void tsyn_init(tsyn_t *b);
void tsyn_note_on(tsyn_t *b, int note, int slide);
void tsyn_note_off(tsyn_t *b);
void tsyn_all_off(tsyn_t *b);
void tsyn_render(tsyn_t *b, float *out, int n);
int tsyn_nparams(void);
const x0x_param_t *tsyn_param(int i);
void tsyn_set(tsyn_t *b, int i, int value);
int tsyn_get(const tsyn_t *b, int i);
float tsyn_shape(float ph, float wave, float skew, float harm2, float harm3, float fold);   /* one sample of the cycle (for the tests) */
