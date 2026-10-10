/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE project: defaults, clips. (Saving to flash comes with the storage layer.) */
#include <string.h>
#include "tribe.h"

void clip_set_engine(clip_t *c, int eng)
{
    int i, n = eng_nparams(eng);
    c->eng = (uint8_t)eng;
    memset(c->par, 0, sizeof c->par);
    for (i = 0; i < n; i++)
        c->par[i] = (uint8_t)eng_default(eng, i);
}

void clip_init(clip_t *c, int eng)
{
    int s;
    memset(c, 0, sizeof *c);
    for (s = 0; s < NSTEPS; s++) {
        c->st[s].note = 60;
        c->st[s].vel = 100;
        c->st[s].prob = 100;
        c->st[s].len = 3;
    }
    clip_set_engine(c, eng);
}

static int find(const char *n)
{
    int i;
    for (i = 0; i < eng_count(); i++) {
        const char *a = eng_info(i)->name, *b = n;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b)
            return i;
    }
    return 0;
}

static void put(clip_t *c, int s, int note) { c->on[s >> 5] |= 1u << (s & 31); c->st[s].note = (uint8_t)note; }

/* a starting point that makes noise: four-on-the-floor, backbeat, hats, a 303 line */
void project_defaults(void)
{
    static const char *const names[NTRK] = {"KICK W", "909 SD", "909 CH", "909 OH", "808 CP", "303", "KICK P",
                                            "808 BD", "909 RS", "NONE"};
    int t, c, s;
    memset(&proj, 0, sizeof proj);
    proj.set.magic = 0x54524942u;
    proj.set.bpm_x10 = 1250;
    proj.set.master_vol = 100;
    for (t = 0; t < NTRK; t++) {
        proj.set.level[t] = 100;
        proj.set.cur[t] = 0;
        for (c = 0; c < NCLIP; c++)
            clip_init(&proj.clip[t][c], find(names[t]));
    }
    for (s = 0; s < 16; s += 4)
        put(&proj.clip[0][0], s, 48);
    put(&proj.clip[1][0], 4, 60);
    put(&proj.clip[1][0], 12, 60);
    for (s = 2; s < 16; s += 4)
        put(&proj.clip[3][0], s, 60);
    for (s = 0; s < 16; s += 2)
        put(&proj.clip[2][0], s, 60);
    for (s = 0; s < 16; s++) {
        static const uint8_t line[16] = {36, 0, 36, 48, 0, 36, 39, 0, 36, 0, 43, 36, 0, 48, 0, 34};
        if (line[s]) {
            put(&proj.clip[5][0], s, line[s]);
            proj.clip[5][0].st[s].len = 3;
            if (s == 3 || s == 10)
                proj.clip[5][0].st[s].slide = 1;
            if (s == 6)
                proj.clip[5][0].st[s].vel = 120;
        }
    }
    proj.clip[6][0].lensel = 1;                      /* a 32-step kick fill on the second kick */
    put(&proj.clip[6][0], 22, 52);
    put(&proj.clip[6][0], 30, 55);
}
