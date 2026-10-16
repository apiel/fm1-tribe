/* SPDX-License-Identifier: GPL-3.0-only */
/* FM4: a mono 4-operator FM synth built on the layout of the Elektron Digitone's FM (its user manual, SYN1, SYN2,
 * FLTR, AMP and the FM appendix): the operators C, A, B1 and B2; eight algorithms that feed two outputs X and Y,
 * mixed with MIX; HARM (additive waveforms) and DTUN (detune of A and B2); FDBK; two envelopes, A and B (B drives
 * B1 and B2), each an attack and decay that ends at an end level; TRIG chooses gate or trig; then a low-pass
 * filter, an overdrive and an amp envelope. The LFO is the clip's LFO, as for the other synths.
 *
 * Operators: 0 = C (a carrier or a modulator at a fixed depth, no envelope), 1 = A, 2 = B1, 3 = B2 (A and B2 are
 * detuned by DTUN; HARM positive reaches A and B1, negative reaches C). A and B1, B2 are scaled by their envelope.
 *
 * One voice: a new note restarts the operators, the envelopes and the phases; a note over a sounding one with the
 * sequencer's slide flag (or a key over a held key) only changes the pitch. Every instance is self-contained. */
#pragma once
#include <stdint.h>
#include "x0x_param.h"

#define FM4_SR 44100.0f
#define FM4_CTRL 8                       /* control rate: the operators' pitch and the filter's coefficients */
#define FM4_OPS 4                        /* 0 C, 1 A, 2 B1, 3 B2 */
#define FM4_PARTIALS 4                   /* the additive waveform: up to four partials */

enum {
    FM4_ALG, FM4_RATC, FM4_RATA, FM4_RATB, FM4_HARM, FM4_DTUN, FM4_FDBK, FM4_MIX,     /* EDIT: the FM */
    FM4_LEVA, FM4_ATKA, FM4_DECA, FM4_ENDA, FM4_LEVB, FM4_ATKB, FM4_DECB, FM4_ENDB,   /* ENV: the operator envelopes */
    FM4_ATK, FM4_DEC, FM4_SUS, FM4_REL,                                              /* ENV: the amp envelope */
    FM4_CUT, FM4_RESO, FM4_DRIVE, FM4_TRIG,                                          /* FX: filter, overdrive, gate/trig */
    FM4_NPARAMS
};
#define FM4_ENV_START FM4_LEVA           /* the pages of the groups, by place (engines.c) */
#define FM4_FX_START FM4_CUT

typedef struct fm4 {
    uint8_t pot[FM4_NPARAMS];

    /* derived from the pots */
    int alg;
    uint8_t ord[FM4_OPS];            /* the order the operators are computed in: modulators first */
    float ratio[FM4_OPS];            /* each operator's ratio to the note (A and B2 before DTUN) */
    float dt_factor;                 /* DTUN: the factor on A and B2 */
    float idx[FM4_OPS];              /* modulation index: C fixed, A = LEV A, B1 and B2 from LEV B (times the envelope) */
    float part[FM4_OPS][FM4_PARTIALS];   /* HARM: the partials' amplitudes per operator */
    uint8_t nparts[FM4_OPS];         /* how many partials of each operator are used */
    float fb, mix;                   /* FDBK (operator A's own output), MIX: 0 = X, 1 = Y */
    float endl[2], atk_step[2], dec_c[2];    /* the envelopes A and B: end level, attack step, decay per sample */
    float amp_att_step, amp_dec_c, sustain, rel_c;   /* the amp envelope */
    int trig;                        /* 0 gate: the envelope holds while the key is down; 1 trig: it runs out by itself */
    float drive_g, drive_norm;
    int drive_on;
    float cut_st, reso_k;

    /* voice */
    int gate, idle, amp_stage, cc;
    int ostage[2];                   /* 0 attack, 1 decay (or hold, for gate), 2 release (gate) */
    float target_st, amp;
    float inc[FM4_OPS], ph[FM4_OPS], s[FM4_OPS];   /* the increment, the phase in turns, the last raw output */
    float oenv[2];                   /* the envelopes A and B now */
    float g, a1, a2, a3;             /* the filter (a state-variable low-pass), this control step */
    float ic1, ic2;                  /* its integrator states */
} fm4_t;

void fm4_init(fm4_t *b);
void fm4_note_on(fm4_t *b, int note, int slide);
void fm4_note_off(fm4_t *b);
void fm4_all_off(fm4_t *b);
void fm4_render(fm4_t *b, float *out, int n);
int fm4_nparams(void);
const x0x_param_t *fm4_param(int i);
void fm4_set(fm4_t *b, int i, int value);
int fm4_get(const fm4_t *b, int i);
