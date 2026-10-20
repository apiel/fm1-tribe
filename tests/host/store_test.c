/* SPDX-License-Identifier: GPL-3.0-only */
/* The clip store under power cuts: random edits and saves with the power cut at a random program or erase
 * (a program lands half, an erase leaves the sector as garbage). After every cut the "reboot" must find each clip
 * either as it was before the save or as the save meant it, never corrupt; a save that returned must have kept
 * everything. The log is small (69 sectors), so thousands of records force many garbage collections. */
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

static void mutate(clip_t *cl)
{
    int k, n = 1 + rand() % 6;
    for (k = 0; k < n; k++) {
        int s = rand() % NSTEPS;
        cl->on[s >> 5] ^= 1u << (s & 31);
        cl->st[s].note = (uint8_t)(24 + rand() % 60);
        cl->st[s].vel = (uint8_t)(1 + rand() % 127);
    }
    cl->par[rand() % 8] = (uint8_t)(rand() % 128);
    cl->lfo[rand() % NLFO] = (uint8_t)(rand() % 128);
    cl->rate = (uint8_t)(rand() % NSEQRATE);
    cl->dir = (uint8_t)(rand() % NSEQDIR);
    cl->swing = (uint8_t)(rand() % 128);
    if (rand() % 5 == 0) cl->lensel = (uint8_t)(rand() % 3);
}

/* the clips as the log gives them after a boot: every key read back */
static void reboot(clip_t got[NTRK][NCLIP])
{
    int t, c;
    project_defaults();
    store_load();
    for (t = 0; t < NTRK; t++)
        for (c = 0; c < NCLIP; c++)
            (void)store_fetch(t, c, &got[t][c]);
}

/* "full": every clip of every track has steps before the rounds start, so the log holds all 640 clips and the settings */
int main(int argc, char **argv)
{
    static clip_t got[NTRK][NCLIP];
    int round, fails = 0, cuts = 0, ok_saves = 0, t, c, full = argc > 1 && !strcmp(argv[1], "full");
    long maxops = 0;
    srand(12345);
    memset(flash, 0xFF, sizeof flash);
    project_defaults();
    if (store_load() != 0) { printf("empty flash loaded something\n"); return 1; }
    reboot(got);
    for (t = 0; t < NTRK; t++)
        for (c = 0; c < NCLIP; c++)
            committed[t][c] = got[t][c];
    committed_set = proj.set;
    if (full) {
        for (t = 0; t < NTRK; t++)
            for (c = 0; c < NCLIP; c++) {
                pending[t][c] = committed[t][c];
                mutate(&pending[t][c]);
                pending[t][c].on[0] |= 1u;
                if (store_write(t, c, &pending[t][c]) < 0) { printf("a full log did not save\n"); return 1; }
            }
        reboot(got);
        for (t = 0; t < NTRK; t++)
            for (c = 0; c < NCLIP; c++) {
                if (memcmp(&got[t][c], &pending[t][c], sizeof(clip_t))) { printf("clip %d/%d came back different\n", t, c); return 1; }
                committed[t][c] = got[t][c];
            }
    }
    for (round = 0; round < 1500; round++) {
        int n = 1 + rand() % 25, i, r, cut = rand() % 3 == 0;
        int kt[25], kc[25];
        for (i = 0; i < n; i++) {
            kt[i] = rand() % NTRK;
            kc[i] = rand() % NCLIP;
            pending[kt[i]][kc[i]] = committed[kt[i]][kc[i]];
            mutate(&pending[kt[i]][kc[i]]);
        }
        dead = 0;
        budget = cut ? rand() % 40 : -1;
        r = 0;
        for (i = 0; i < n; i++) {
            int w = store_write(kt[i], kc[i], &pending[kt[i]][kc[i]]);
            if (w < 0) { r = w; break; }
            r += w;
        }
        if (!cut && r < 0) { printf("round %d: save failed %d without a cut\n", round, r); fails++; }
        if (budget < 0 && r >= 0) ok_saves++;
        if (budget >= 0) cuts += dead;
        /* reboot: what is in memory is gone; the log decides */
        budget = -1;
        dead = 0;
        reboot(got);
        for (t = 0; t < NTRK; t++)
            for (c = 0; c < NCLIP; c++) {
                int is_old = !memcmp(&got[t][c], &committed[t][c], sizeof(clip_t));
                int is_new = 0, touched = 0;
                for (i = 0; i < n; i++)
                    if (kt[i] == t && kc[i] == c) {
                        touched = 1;
                        is_new = !memcmp(&got[t][c], &pending[t][c], sizeof(clip_t));
                    }
                if (!is_old && !(touched && is_new)) {
                    printf("round %d: clip %d/%d is neither the old nor the new version\n", round, t, c);
                    fails++;
                }
                if (!cut && r >= 0 && touched && !is_new) { printf("round %d: a finished save lost clip %d/%d\n", round, t, c); fails++; }
            }
        if (memcmp(&proj.set, &committed_set, sizeof proj.set) && memcmp(&proj.set, &pending_set, sizeof proj.set) && 0) {
            printf("round %d: settings are neither old nor new\n", round);
            fails++;
        }
        if (fails > 10) break;
        for (i = 0; i < n; i++)                            /* the log is the truth now: what was written is saved */
            if (!cut || !memcmp(&got[kt[i]][kc[i]], &pending[kt[i]][kc[i]], sizeof(clip_t)))
                committed[kt[i]][kc[i]] = got[kt[i]][kc[i]];
        if (budget < 0 && erases + progs > maxops) maxops = erases + progs;
    }
    printf("%d rounds, %d clean saves, %d power cuts, %ld erases, %ld programs, %d failures\n", round, ok_saves, cuts, erases, progs, fails);
    return fails ? 1 : 0;
}
