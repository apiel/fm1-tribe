/* SPDX-License-Identifier: GPL-3.0-only */
/* LEAD: a monophonic lead for long, gliding, legato notes (the thick, screaming leads of tekno and hardtek):
 * up to seven detuned oscillators (unison), a sub oscillator, a resonant 4-pole ladder filter that an ADSR
 * opens, vibrato, an always-legato mode, and a Drive and distortion stage. The filter and the FX stage are
 * ACID's (acid.c), which is the zicCycle 303 filter.
 *
 *  Cutoff   40 Hz .. 12 kHz, exponential (100 semitones)        Reso    the ladder, up to self-oscillation
 *  Color    saw -> square (polyBLEP)                             Detune  the unison spread, 0 .. 60 cents
 *  Unison   1 .. 7 oscillators, spread evenly either side of the note
 *  Sub      a sine an octave down                                Tune    +-1.5 semitones
 *  Accent   an accented note (velocity > 100): the filter opens deeper and the note is louder
 *  Legato   On: a note that arrives while the last one still sounds glides to the new pitch and does not
 *           start the envelope again, with or without the sequencer's slide flag. Off: only a slide does.
 *  Vib      vibrato depth (5.5 Hz), 0 .. 1 semitone
 *  Attack, Decay, Sustain, Release   the envelope; it opens the filter by EnvMod as well as the level
 *  EnvMod   filter envelope depth, 0 .. 60 semitones            Slide   glide time, 5 ms .. 600 ms
 *  Drive    gain into the filter                                 Dist, DrvTyp   the distortion after it
 *
 * Every instance is self-contained (no mutable static state). */
#pragma once
#include <stdint.h>
#include "x0x_param.h"

#define LEAD_SR 44100.0f
#define LEAD_CTRL 8                       /* control rate: envelope, pitch and the filter coefficients */
#define LEAD_VOICES 7

enum {
    LEAD_CUTOFF, LEAD_RESO, LEAD_COLOR, LEAD_DETUNE,           /* EDIT, page 1 */
    LEAD_UNISON, LEAD_SUB, LEAD_TUNE, LEAD_ACCENT,             /* page 2 */
    LEAD_LEGATO, LEAD_VIB,                                     /* page 3 */
    LEAD_ATTACK, LEAD_DECAY, LEAD_SUSTAIN, LEAD_RELEASE,       /* ENV, page 1 */
    LEAD_ENVMOD, LEAD_SLIDE,                                   /* page 2 */
    LEAD_DRIVE, LEAD_DIST, LEAD_DISTTYPE,                      /* FX */
    LEAD_NPARAMS
};

typedef struct lead {
    uint8_t pot[LEAD_NPARAMS];

    /* derived from the pots */
    float cut_st, reso_k, color, detune_ct, sub, tune_st, acc_amt, vib_st, env_st;
    int nvoices, legato;
    float ratio[LEAD_VOICES];     /* each oscillator's frequency ratio to the note */
    float att_step;               /* attack: envelope step per sample */
    float dec_c, sustain, rel_c;  /* decay (per sample), the level it settles at, release (per sample) */
    float glide_c;                /* per control step */
    float drive_g, dist_g, dist_wet, dist_norm;
    int dist_type;

    /* voice */
    int gate, idle, accent, snap;
    int stage;                    /* 0 attack, 1 decay / sustain, 2 release */
    int cc;
    float target_st, cur_st;
    float env;
    float vib_ph;
    float inc0, inc1, sub_ph;
    float ph[LEAD_VOICES];
    float G, G2, G3, G4, kk, rdiv;
    float s1, s2, s3, s4;
    float gain;
} lead_t;

void lead_init(lead_t *b);
void lead_note_on(lead_t *b, int note, int accent, int slide);
void lead_note_off(lead_t *b);
void lead_all_off(lead_t *b);
void lead_render(lead_t *b, float *out, int n);
int lead_nparams(void);
const x0x_param_t *lead_param(int i);
void lead_set(lead_t *b, int i, int value);
int lead_get(const lead_t *b, int i);
