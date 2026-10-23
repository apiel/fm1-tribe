/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE, native: the host simulator (host/tribe_host.c) for a window and a sound card (desktop/tribe_desktop.cpp,
 * SFML; built by desktop/build.sh). The sound card drives the device: tribe_pull() runs it as far as the samples
 * it is asked for, as the browser build does (web/emu/tribe_web.c). Saved objects and the clip log are files
 * in a data directory, written when they change. */
#define TRIBE_DESKTOP 1
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tribe_core.h"

static void audio_out(const int32_t *blk, uint32_t n);
#include "../host/tribe_host.c"

#define RING 4096u                       /* output ring, stereo float, filled by run_ms, drained by tribe_pull */
static float ring[RING * 2];
static uint32_t ring_w, ring_r;
static void audio_out(const int32_t *blk, uint32_t n)
{
    uint32_t k;
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_w++ % RING) * 2u;
        ring[i] = (float)blk[2 * k] / 8388608.0f;
        ring[i + 1] = (float)blk[2 * k + 1] / 8388608.0f;
    }
}

static int16_t clip16(float v)
{
    if (v >= 1.0f)
        return 32767;
    if (v <= -1.0f)
        return -32768;
    return (int16_t)(v * 32767.0f);
}

void tribe_pull(int16_t *out, uint32_t frames)
{
    uint32_t k;
    while (ring_w - ring_r < frames)
        run_ms(1);
    for (k = 0; k < frames; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out[2 * k] = clip16(ring[i]);
        out[2 * k + 1] = clip16(ring[i + 1]);
    }
}

/* ------------------------------------------------------ saved data --- */
#define LOG_FILE "clips.log"
static uint32_t saved_writes, saved_log;

static void obj_path(char *out, size_t n, const char *dir, uint32_t o)
{
    snprintf(out, n, "%s/obj%u.bin", dir, (unsigned)o);
}

static void log_path(char *out, size_t n, const char *dir)
{
    snprintf(out, n, "%s/%s", dir, LOG_FILE);
}

void tribe_init(const char *dir)
{
    char path[512];
    uint32_t o;
    FILE *f;
    memset(logmem, 0xFF, sizeof logmem);
    for (o = 0; o < OBJ_NOBJ; o++) {
        obj_path(path, sizeof path, dir, o);
        f = fopen(path, "rb");
        if (!f)
            continue;
        store_len[o] = (int)fread(store[o], 1, PLAT_STORE_MAX, f);
        fclose(f);
    }
    log_path(path, sizeof path, dir);
    flash_file(path, 0);
    saved_writes = store_writes;
    saved_log = log_writes;
    batt_level = 4;                                  /* a computer's USB power: no battery to show */
    boot();
    splash_start();
}

void tribe_save(const char *dir)
{
    char path[512];
    uint32_t o;
    FILE *f;
    if (store_writes != saved_writes) {
        for (o = 0; o < OBJ_NOBJ; o++) {
            if (store_len[o] <= 0)
                continue;
            obj_path(path, sizeof path, dir, o);
            f = fopen(path, "wb");
            if (f) {
                fwrite(store[o], 1, (size_t)store_len[o], f);
                fclose(f);
            }
        }
        saved_writes = store_writes;
    }
    if (log_writes != saved_log) {
        log_path(path, sizeof path, dir);
        flash_file(path, 1);
        saved_log = log_writes;
    }
}

void tribe_forget(const char *dir)
{
    char path[512];
    uint32_t o;
    for (o = 0; o < OBJ_NOBJ; o++) {
        obj_path(path, sizeof path, dir, o);
        remove(path);
    }
    log_path(path, sizeof path, dir);
    remove(path);
}

/* ------------------------------------------------------- the panel --- */
void tribe_buttons(uint32_t mask) { held_btn = mask; }
void tribe_keys(uint32_t mask) { held_keys = mask; }
void tribe_turn(int role, int32_t detents)
{
    if (role >= 0 && role < NE)
        enc_acc[role] += detents;
}
void tribe_master(uint32_t v) { master = v > 4096u ? 4096u : v; }

const uint16_t *tribe_screen(void) { return fb; }
uint32_t tribe_lit_buttons(void) { return lit_btn; }
uint32_t tribe_dim_buttons(void) { return dim_btn & ~lit_btn; }
uint32_t tribe_lit_keys(void) { return lit_keys; }
uint32_t tribe_dim_keys(void) { return dim_keys & ~lit_keys; }
uint32_t tribe_faint_keys(void) { return faint_keys & ~(lit_keys | dim_keys); }
int tribe_white_key(int i) { return WHITE_K[i & 15]; }
int tribe_black_key(int i) { return BLACK_K[i % 11]; }
