/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE, native: the device as the window sees it (desktop/tribe_core.c). One lock guards all of it:
 * the sound stream runs the device through tribe_pull, the window sends input and reads the screen. */
#ifndef TRIBE_CORE_H
#define TRIBE_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void tribe_init(const char *dir);                   /* load what was saved in DIR, boot, start the splash */
void tribe_save(const char *dir);                   /* write what changed since the last call */
void tribe_forget(const char *dir);                 /* delete what was saved in DIR (the factory reset) */
void tribe_pull(int16_t *out, uint32_t frames);     /* stereo, frames <= 4096: runs the device as far as needed */

void tribe_buttons(uint32_t mask);                  /* held: bit B_* (app/plat.h) */
void tribe_keys(uint32_t mask);                     /* held: bit k = key k (0 = F3) */
void tribe_turn(int role, int32_t detents);         /* EN_* (app/plat.h), + = clockwise */
void tribe_master(uint32_t v);                      /* MASTER pot, 0..4096 */

const uint16_t *tribe_screen(void);                 /* 240 x 240, RGB565, byte-swapped as the panel takes it */
uint32_t tribe_lit_buttons(void);
uint32_t tribe_dim_buttons(void);
uint32_t tribe_lit_keys(void);
uint32_t tribe_dim_keys(void);
uint32_t tribe_faint_keys(void);
int tribe_white_key(int i);                         /* the key of white key i (0..15) */
int tribe_black_key(int i);                         /* the key of black key i (0..10) */

#ifdef __cplusplus
}
#endif

#endif
