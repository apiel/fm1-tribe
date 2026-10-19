/* SPDX-License-Identifier: GPL-3.0-only */
/* The two kick engines of zicBox's zicModKick (KickWave, PotKick; GPL-3.0), in C, float, no libm.
 * Pots are 0..127 like every X0X engine; kick_param(...)->def is the pot of the original default. */
#pragma once
#include "x0x_param.h"
void kicks_init(void);
int kick_alloc(int potkick);                 /* an instance index, -1 when none is free */
void kick_free(int inst);
int kick_nparams(int potkick);
const x0x_param_t *kick_param(int potkick, int i);
void kick_set(int inst, int i, int pot);
void kick_format(int potkick, int i, int pot, char *buf);   /* a pot in its unit: 52Hz, 350ms, 40% */
void kick_trigger(int inst, int note, float vel);
void kick_render(int inst, float *out, int n);   /* overwrites */
