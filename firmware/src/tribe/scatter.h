/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#define SC_SLOTS 16                    /* 0..7: the selected track, 8..15: the whole mix */
#define SC_RING 16384                  /* samples a loop effect can keep (0.37 s) */
typedef struct {
    uint8_t kind;
    int16_t *ring;
    int32_t slice, pos, t, cnt;
    uint8_t play;
    float ph, g, lp, a, hold;
} sc_t;
const char *sc_name(int slot);         /* <= 4 characters */
int sc_needs_ring(int slot);
void sc_start(sc_t *s, int slot, int16_t *ring, float spl);
void sc_run(sc_t *s, float *x, int n, float spl);
