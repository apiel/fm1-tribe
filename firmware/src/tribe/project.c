/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE project: defaults, clips. (Saving to flash comes with the storage layer.) */
#include "mem.h"
#include "tribe.h"

void clip_set_engine(clip_t *c, int eng)
{
    int i, n = eng_nparams(eng);
    c->eng = (uint8_t)eng;
    memset(c->par, 0, sizeof c->par);
    for (i = 0; i < n; i++)
        c->par[i] = (uint8_t)eng_default(eng, i);
    c->lfo[LFO_TARGET] = 0;                          /* the LFO's target is a pot of the old engine */
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
    c->lfo[LFO_RATE] = 85;                           /* synced: 1 BAR (16 steps) */
    c->lfo[LFO_SYNC] = 1;
    c->lfo[LFO_RETRIG] = 1;
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

/* the clip a track starts on, when the log has none: an empty clip with the track's engine. Clip 0 of each track has
 * the starting steps: four-on-the-floor, backbeat, hats, an ACID line, a long legato lead, a 32-step kick fill */
void default_clip(int t, int c, clip_t *out)
{
    static const char *const names[NTRK] = {"KICK W", "909 SD", "909 CH", "909 OH", "808 CP", "ACID", "KICK P",
                                            "LEAD", "909 RS", "NONE"};
    int s;
    clip_init(out, find(names[t]));
    if (c != 0)
        return;
    switch (t) {
    case 0:
        for (s = 0; s < 16; s += 4)
            put(out, s, 48);
        break;
    case 1:
        put(out, 4, 60);
        put(out, 12, 60);
        break;
    case 2:
        for (s = 0; s < 16; s += 2)
            put(out, s, 60);
        break;
    case 3:
        for (s = 2; s < 16; s += 4)
            put(out, s, 60);
        break;
    case 5:
        {
            static const uint8_t line[16] = {36, 0, 36, 48, 0, 36, 39, 0, 36, 0, 43, 36, 0, 48, 0, 34};
            for (s = 0; s < 16; s++) {
                if (line[s]) {
                    put(out, s, line[s]);
                    out->st[s].len = 3;
                    if (s == 3 || s == 10)
                        out->st[s].slide = 1;
                    if (s == 6)
                        out->st[s].vel = 120;
                }
            }
        }
        break;
    case 6:
        out->lensel = 1;                             /* a 32-step kick fill on the second kick */
        put(out, 22, 52);
        put(out, 30, 55);
        break;
    case 7:
        {   /* two bars of long notes that glide into each other (a slide step glides into the next note) */
            static const struct { uint8_t step, note, len, slide, vel; } lead[] = {
                {0, 57, 31, 1, 100}, {8, 60, 31, 1, 100}, {16, 64, 31, 1, 100}, {20, 62, 15, 0, 120},
                {24, 60, 23, 1, 100}, {28, 59, 15, 0, 100},
            };
            unsigned i;
            out->lensel = 1;
            for (i = 0; i < sizeof lead / sizeof lead[0]; i++) {
                put(out, lead[i].step, lead[i].note);
                out->st[lead[i].step].len = lead[i].len;
                out->st[lead[i].step].slide = lead[i].slide;
                out->st[lead[i].step].vel = lead[i].vel;
            }
        }
        break;
    default:
        break;
    }
}

/* the settings and the starting clips (the log replaces both, where it has them) */
void project_defaults(void)
{
    int t;
    memset(&proj, 0, sizeof proj);
    proj.set.magic = 0x54524942u;
    proj.set.bpm_x10 = 1250;
    proj.set.master_vol = 127;                               /* the volume when MASTER is not the volume knob: full */
    proj.set.knob[0].kind = KS_VOL;                          /* MASTER: the volume */
    proj.set.knob[1].kind = KS_DJ;                           /* SELECT: the DJ filter */
    proj.set.knob[2].kind = KS_FX1;                          /* PRESETS: FX 1's amount */
    proj.set.knob[3].kind = KS_FX2;                          /* ALGORITHM: FX 2's amount */
    proj.set.fxtype[0] = MX_DELAY;                           /* FX 1: the echo, FX 2: the room; each track's send (SEL) opens them */
    proj.set.fxtype[1] = MX_REVERB;
    proj.set.fxamt[0] = proj.set.fxamt[1] = 100;
    proj.set.fxpar[0] = 50;                                  /* feedback */
    proj.set.fxpar[1] = 64;                                  /* size */
    proj.set.comp[0] = 95;                                   /* the compressor, a gentle bus glue: -12 dB, */
    proj.set.comp[1] = 2;                                    /* 2:1, */
    proj.set.comp[2] = 80;                                   /* 8 ms attack, */
    proj.set.comp[3] = 60;                                   /* 110 ms release, */
    proj.set.comp[4] = 12;                                   /* +2 dB makeup, */
    proj.set.comp[5] = 127;                                  /* all wet */
    proj.set.dj[0] = 64;                                     /* the DJ filter: the middle is off */
    proj.set.dj[1] = 40;
    for (t = 0; t < NTRK; t++) {
        proj.set.level[t] = 100;
        proj.set.arp_gate[t] = 80;
        proj.set.send[0][t] = proj.set.send[1][t] = 0;      /* the sends start closed: raise a track's send to put it in the echo / the room */
        proj.set.arp_oct[t] = 1;
        proj.set.cur[t] = 0;
        default_clip(t, 0, &act[t]);                    /* the clip it plays until the log says otherwise */
        dk_clip[t] = 0;
    }
}
