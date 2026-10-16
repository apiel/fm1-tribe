/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE: ten mono tracks, any engine on any track, sixteen clips per track.
 *
 * The project is plain data (no pointers). The UI edits it in place from the main loop; the
 * audio ISR (engine_render) reads it and owns every engine's state. UI -> ISR traffic that is
 * not plain data (parameter changes, live notes, clip launches) goes through a command queue. */
#pragma once
#include <stdint.h>
#include "../dsp/x0x_param.h"

#define NTRK 10
#define NCLIP 16
#define NSTEPS 64
#define NPAR 24                         /* parameters stored per clip (an engine may have fewer) */

/* ------------------------------------------------------------- engines --- */
enum { EK_DRUM, EK_SYNTH };             /* what the step editor offers (note/vel/prob  or  note/vel/len/slide) */
enum { PG_FX, PG_ENV, PG_LFO, PG_EDIT, NPG, PG_HIDE = NPG };   /* parameter pages; HIDE: not shown */
enum { F_NONE, F_909, F_808, F_KICKW, F_KICKP, F_ACID, F_LEAD };         /* engine families */

typedef struct {
    const char *name;                   /* <= 8 characters */
    uint8_t kind, fam, voice, cost;     /* cost: rough CPU weight, 1 = a light drum voice */
} tribe_eng_t;

int eng_count(void);
const tribe_eng_t *eng_info(int id);     /* id 0 = none */
int eng_nparams(int id);
const x0x_param_t *eng_param(int id, int i);
int eng_group(int id, int i);           /* PG_* */
int eng_default(int id, int i);
int eng_pages(int id, int group);       /* pages of 4 in a group; 0 = the engine has none */
int eng_param_at(int id, int group, int page, int slot);   /* parameter index, -1 = none */
void eng_format(int id, int i, int value, char *buf);       /* a value in text, <= 7 chars */

/* ------------------------------------------------------------- project --- */
/* one LFO per clip, on one pot of the engine. Values are pots 0..127 except SHAPE (LFO_SHAPES), TARGET (a pot index) and the switches */
enum { LFO_SHAPE, LFO_RATE, LFO_DEPTH, LFO_TARGET, LFO_SYNC, LFO_RETRIG, NLFO };
enum { LS_SINE, LS_TRI, LS_SAW, LS_SQUARE, LS_RANDOM, LFO_SHAPES };
/* how fast a clip's steps go (a step is that many ticks of the engine's 1/96-note clock), and in which order */
enum { SR_16, SR_8, SR_32, SR_16T, SR_8T, SR_32T, NSEQRATE };
enum { SD_FWD, SD_REV, SD_PING, SD_RAND, NSEQDIR };

typedef struct {
    uint8_t note, vel, prob, len, slide;    /* prob 0..100; len: quarter steps; slide: > 0 glides into the next step */
} dstep_t;

typedef struct {
    uint32_t on[NSTEPS / 32];           /* bit s: step s plays */
    dstep_t st[NSTEPS];
    uint8_t eng;                        /* engine id */
    uint8_t lensel;                     /* 0, 1, 2 = 16, 32, 64 steps */
    uint8_t par[NPAR];                  /* the engine's pots */
    uint8_t lfo[NLFO];                  /* the clip's LFO: LFO_* below (depth 0 = off) */
    uint8_t rate, dir, swing;           /* the sequencer's config for this clip: SR_*, SD_*, swing 0..127 (all zero = 1/16, forward, straight) */
} clip_t;

/* the master knobs: MASTER (a pot), SELECT, PRESETS, ALGORITHM (encoders). Each one does one of the jobs below. The jobs are the settings: the GLO menu has a row for each, so a knob is only a shortcut. */
enum { KS_VOL, KS_DJ, KS_RESO, KS_FX1, KS_FX2, KS_BPM, NKS };
/* FX 1 and FX 2: two master effects every track can send to (SEL page). Each is one of these; AMT is its return level, PARAM its
 * own parameter (feedback, room size, drive, bit depth, ...). */
enum { MX_REVERB, MX_DELAY, MX_DRIVE, MX_DIST, MX_FUZZ, MX_WAVE, MX_CRUSH, MX_DECIM, MX_RING, NMX };
#define MX_STATEFUL(t) ((t) == MX_REVERB || (t) == MX_DELAY)     /* one buffer each: only one of FX 1 / FX 2 can be one of these */
#define NKNOB 4
#define PIX_LEVEL 100                   /* a parameter slot that is a track's LEVEL, not an engine pot */
typedef struct { uint8_t kind, rsv; } kslot_t;

typedef struct {
    uint32_t magic;
    uint16_t bpm_x10;
    uint8_t level[NTRK];                /* track level 0..127 */
    uint8_t cur[NTRK];                  /* the clip each track starts on */
    uint8_t master_vol, rsv[3];
    uint8_t arp_mode[NTRK], arp_rate[NTRK], arp_gate[NTRK], arp_oct[NTRK];   /* ARP: keys up down updn rand trsp; 1/16 1/8 1/4; % of the step; octaves */
    uint8_t fxtype[2];                  /* what FX 1 and FX 2 are: MX_* */
    uint8_t fxamt[2], fxpar[2];         /* their return level and their own parameter, 0..127 */
    uint8_t dj[2];                      /* the DJ filter: cutoff (64 = off, LP left, HP right) and resonance, 0..127 */
    uint8_t comp[6];                    /* the master compressor, always on: THRESH RATIO ATTACK RELEASE MAKEUP MIX (master.h pots) */
    uint8_t send[2][NTRK];              /* each track's send to the delay (0) and the reverb (1), 0..127 */
    kslot_t knob[NKNOB];
} dsettings_t;

typedef struct {
    dsettings_t set;
    clip_t clip[NTRK][NCLIP];
} dproj_t;

extern dproj_t proj;
#define LEN_OF(c) (16 << (c)->lensel)
static inline int step_on(const clip_t *c, int s) { return (int)(c->on[s >> 5] >> (s & 31)) & 1; }
static inline void step_flip(clip_t *c, int s) { c->on[s >> 5] ^= 1u << (s & 31); }

void project_defaults(void);
void clip_init(clip_t *c, int eng);     /* an empty clip with the engine's default pots */
void clip_set_engine(clip_t *c, int eng);   /* the engine and its default pots; the steps stay */

/* ------------------------------------------------------- the master effects --- */
const char *mx_name(int type);
const char *mx_pname(int type);                              /* its PARAM, in a word */
void mx_format(int type, int v, char *buf);                  /* PARAM in text, <= 7 chars */
void dj_format(int v, char *buf);                            /* the DJ filter: LP 40, OFF, HP 12 */
int dj_active(const uint8_t dj[2]);
typedef struct { float a, b, c, d, hold; int32_t cnt; float gp; uint32_t rng; } mfxs_t;
void dj_run(mfxs_t *s, float *x, int n, const uint8_t dj[2]);
/* FX 1 / FX 2: the send bus in, only the wet out (times AMT). s is the slot's state (unused by the reverb and the delay: one buffer each) */
void mx_run(int type, mfxs_t *s, float *x, int n, int amt, int par, float spl);
void mx_idle(int type);

/* -------------------------------------------------------------- engine --- */
void ui_say(const char *a, const char *b);
void engine_init(void);
void engine_load(uint32_t pct, uint32_t frames);         /* the platform: a block took pct % of its time */
extern volatile uint32_t dk_load_pct, dk_load_peak;      /* the audio block's cost, % of its time; the peak since the UI last cleared it */
void engine_render(int32_t *out_lr, uint32_t n);        /* ISR: n stereo frames, 24-bit in int32 */

/* main loop -> ISR (all queued) */
void dk_play(int on);
void dk_bpm(int bpm_x10);
void dk_set(int trk, int i, int value);                 /* a pot of the track's current clip */
void dk_engine(int trk, int eng);                       /* change the current clip's engine (pots reset to defaults) */
void dk_launch(int trk, int clip, int now);             /* queue a clip (switches at the end of the pattern); now: at once */
void dk_mute(int trk, int on);
void dk_note(int trk, int note, int vel);               /* a live note on (vel 0 = off) */
void dk_master_set(int i, int value);                   /* a master pot (master.h) */
void dk_transpose(int trk, int semitones);              /* the ARP page's TRSP mode: the track's sequence plays this many semitones up (or down) */
void dk_scatter(int trk, int slot, int on);             /* a scatter effect on (slots 0-7: the track, 8-15: the mix) or off */

/* ISR -> UI, read freely */
extern volatile uint8_t dk_pos[NTRK];                   /* each track's playhead */
extern volatile uint8_t dk_clip[NTRK], dk_queued[NTRK]; /* playing clip; queued clip or 0xFF */
extern volatile uint8_t dk_muted[NTRK], dk_playing;
extern volatile uint8_t dk_scat[NTRK], dk_scat_g;        /* the scatter slot (+1) each track plays; the mix's (slot - 7) */
extern volatile int8_t dk_trans[NTRK];                   /* each track's live transposition, semitones */
extern volatile uint8_t dk_arp_n[NTRK];                  /* notes each arpeggiator holds */
extern volatile uint32_t dk_gstep;
extern volatile uint8_t dk_tphase[NTRK];                 /* how far into its current step each track is, 0..255 (REC rounds with it) */
extern volatile uint8_t dk_next[NTRK];                   /* the step each track plays next */
extern volatile uint8_t dk_bound[NTRK];                  /* the engine each track really holds (differs from the clip's when its voice is taken) */
extern volatile uint16_t dk_peak[NTRK];                 /* Q15, decaying (UI) */
extern volatile uint8_t dk_hit[NTRK];                   /* last trigger's velocity, the UI lets it fall */

/* ------------------------------------------- ISR side (engines.c, engine.c) --- */
void eng_init(void);
int eng_bind(int trk, int id);          /* 0 = bound; -1 = the voice or every instance is taken */
void eng_unbind(int trk);
int eng_bound(int trk);
void eng_set(int trk, int i, int value);
void eng_note(int trk, int note, int vel, int slide);
void eng_key_on(int trk, int note, int vel);    /* a key pressed: the track plays it, the last one held */
void eng_key_off(int trk, int note);            /* a key let go: the held note before it, if it was the one sounding */
void eng_off(int trk);                  /* the step's gate ends (a sliding synth keeps it) */
void eng_panic(int trk);                /* release now */
int eng_slide_pending(int trk);
extern volatile uint32_t eng_notes[NTRK];                /* notes started on each track (the LFO restarts on them) */
void eng_render_all(float *buf, int off, int n);   /* buf[trk * 256 + off ...] */

/* ------------------------------------------------------------- the clip store --- */
int store_load(void);                   /* fills proj from the flash log; returns the records found (0: nothing saved yet) */
int store_save(int trk, int clip);      /* trk < 0: every changed clip + settings; else that clip. Records written, or < 0 */
void store_poll(void);                  /* call once a frame: finds changed clips, one a frame */
int store_clip_dirty(int trk, int clip);   /* does this clip differ from what was saved? */
int store_set_dirty(void);              /* the settings (GLO; not the MASTER knob's volume) */
int store_save_set(void);               /* only the settings: 1 saved, 0 unchanged, < 0 error */
int store_revert(int trk, int clip);    /* the clip back to its saved state: 1, or 0 when it was never saved */
int store_dirty(void);                  /* clips (and settings) changed since the last save */
