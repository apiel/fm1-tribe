/* SPDX-License-Identifier: GPL-3.0-only */
/* The clip store under power cuts: random edits and saves with the power cut at a random program or erase
 * (a program lands half, an erase leaves the sector as garbage). After every cut the "reboot" must find each clip
 * either as it was before the save or as the save meant it, never corrupt; a save that returned must have kept
 * everything. The log is small (48 sectors), so thousands of records force many garbage collections. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "tribe/tribe.h"
#include "app/plat.h"
uint32_t plat_master(void) { return 4096; }

static uint8_t flash[PLAT_LOG_SECTORS][4096];
static long budget = -1;             /* operations left before the power goes; -1 = never */
static int dead;
static long erases, progs;

int plat_log_read(uint32_t s, uint32_t off, void *dst, uint32_t n)
{
    if (s >= PLAT_LOG_SECTORS || off + n > 4096u) return -1;
    memcpy(dst, flash[s] + off, n);
    return 0;
}
int plat_log_erase(uint32_t s)
{
    if (dead) return -1;
    if (budget == 0) {                   /* cut: a half-erased sector reads as garbage */
        uint32_t i;
        for (i = 0; i < 4096; i++) flash[s][i] = (uint8_t)rand();
        dead = 1;
        return -1;
    }
    if (budget > 0) budget--;
    memset(flash[s], 0xFF, 4096);
    erases++;
    return 0;
}
int plat_log_prog(uint32_t s, uint32_t off, const void *src, uint32_t n)
{
    uint32_t i, m = n;
    if (dead) return -1;
    if (budget == 0) { m = n / 2; dead = 1; }
    else if (budget > 0) budget--;
    for (i = 0; i < m; i++) flash[s][off + i] &= ((const uint8_t *)src)[i];
    progs++;
    return dead ? -1 : 0;
}

static clip_t committed[NTRK][NCLIP], pending[NTRK][NCLIP];
static dsettings_t committed_set, pending_set;

static void mutate(int t, int c)
{
    clip_t *cl = &proj.clip[t][c];
    int k, n = 1 + rand() % 6;
    for (k = 0; k < n; k++) {
        int s = rand() % NSTEPS;
        cl->on[s >> 5] ^= 1u << (s & 31);
        cl->st[s].note = (uint8_t)(24 + rand() % 60);
        cl->st[s].vel = (uint8_t)(1 + rand() % 127);
    }
    cl->par[rand() % 8] = (uint8_t)(rand() % 128);
    if (rand() % 5 == 0) cl->lensel = (uint8_t)(rand() % 3);
}

int main(void)
{
    int round, fails = 0, cuts = 0, ok_saves = 0, t, c;
    long maxops = 0;
    srand(12345);
    memset(flash, 0xFF, sizeof flash);
    project_defaults();
    if (store_load() != 0) { printf("empty flash loaded something\n"); return 1; }
    memcpy(committed, proj.clip, sizeof committed);
    committed_set = proj.set;
    for (round = 0; round < 1500; round++) {
        int n = 1 + rand() % 25, i, r, cut = rand() % 3 == 0;
        for (i = 0; i < n; i++) mutate(rand() % NTRK, rand() % NCLIP);
        if (rand() % 4 == 0) proj.set.bpm_x10 = (uint16_t)(600 + rand() % 1200);
        memcpy(pending, proj.clip, sizeof pending);
        pending_set = proj.set;
        dead = 0;
        budget = cut ? rand() % 40 : -1;
        r = store_save(-1, 0);
        if (!cut && r < 0) { printf("round %d: save failed %d without a cut\n", round, r); fails++; }
        if (budget < 0 && r >= 0) ok_saves++;
        if (budget >= 0) cuts += dead;
        /* reboot: what is in memory is gone; the log decides */
        budget = -1;
        dead = 0;
        project_defaults();
        store_load();
        for (t = 0; t < NTRK; t++)
            for (c = 0; c < NCLIP; c++) {
                int is_old = !memcmp(&proj.clip[t][c], &committed[t][c], sizeof(clip_t));
                int is_new = !memcmp(&proj.clip[t][c], &pending[t][c], sizeof(clip_t));
                int is_def = 0;
                if (!is_old && !is_new && !is_def) {
                    printf("round %d: clip %d/%d is neither the old nor the new version\n", round, t, c);
                    fails++;
                }
                if (!cut && r >= 0 && !is_new) { printf("round %d: a finished save lost clip %d/%d\n", round, t, c); fails++; }
            }
        if (memcmp(&proj.set, &committed_set, sizeof proj.set) && memcmp(&proj.set, &pending_set, sizeof proj.set)) {
            printf("round %d: settings are neither old nor new\n", round);
            fails++;
        }
        if (fails > 10) break;
        memcpy(committed, proj.clip, sizeof committed);   /* the log is the truth now */
        committed_set = proj.set;
        if (budget < 0 && erases + progs > maxops) maxops = erases + progs;
    }
    printf("%d rounds, %d clean saves, %d power cuts, %ld erases, %ld programs, %d failures\n", round, ok_saves, cuts, erases, progs, fails);
    return fails ? 1 : 0;
}
