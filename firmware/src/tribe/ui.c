/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE user interface: ten tracks on the black keys, steps and clips on the white ones.
 *
 *   black keys 1-10   select a track          black key 11   MUTE (hold it, tap a track)
 *   white keys        SEQ: steps (tap = toggle, hold an "on" step = edit, a new step edits at once)
 *                     HOME: clips   SCATTER: effects   ARP / REC: the keyboard continues up
 *   FX ENV LFO EDIT   the engine's parameter pages (dim = it has some, lit = shown, again = next page)
 *   SEQ               the sequencer   HOME  clips (again: scatter)   GLO  global   ARP / REC keys
 *   KNOB 1-4          the four values on the strip   SELECT / PRESETS  master filter / compressor
 * Included by the unity build after gfx.c, draw.c and project.c. */
#include "tribe.h"
#include "../app/plat.h"
#include "scatter.h"

enum { V_SEQ, V_PARAM, V_HOME, V_GLO, V_ARP, V_REC, V_COMMON, NVIEWS };
static const char *const VIEW_N[NVIEWS] = {"SEQ", "PARAM", "CLIPS", "GLO", "ARP", "REC", "MIX"};
static const char *const ARP_N[6] = {"KEYS", "UP", "DOWN", "UPDN", "RAND", "TRSP"};
#define ARP_ON(t) (proj.set.arp_mode[t] >= 1 && proj.set.arp_mode[t] <= 4)   /* an arpeggio mode (not KEYS, not TRSP) */
#define ARP_TRSP 5
static const char *const ARP_R[3] = {"1/16", "1/8", "1/4"};
static const char *const GRP_N[NPG] = {"FX", "ENV", "LFO", "EDIT"};
static const uint8_t GRP_BTN[NPG] = {B_FX, B_ENV, B_LFO, B_EDIT};
enum { G_TEMPO, G_VOL, G_DJ, G_RESO, G_FX1, G_FX1A, G_FX1P, G_FX2, G_FX2A, G_FX2P, G_K0, G_K1, G_K2, G_K3, G_COMP0,
       G_COMP5 = G_COMP0 + 5, G_CLIPS, G_CPU, G_BAT, NGLO };
static const char *const GLO_N[NGLO] = {"TEMPO", "VOLUME", "DJ FILTER", "DJ RESO", "FX1 TYPE", "FX1 AMOUNT", "FX1 PARAM", "FX2 TYPE",
                                         "FX2 AMOUNT", "FX2 PARAM", "MASTER KNOB", "SELECT KNOB", "PRESETS KNOB", "ALGO KNOB", "COMP THRESH",
                                         "COMP RATIO", "COMP ATTACK", "COMP RELEASE", "COMP MAKEUP", "COMP MIX", "CLIPS", "CPU", "BATTERY"};
#define GLO_COMP0 G_COMP0              /* the compressor rows: GLO_COMP0 .. +5 (settings.comp[]) */

static const uint16_t TRK_COL[NTRK] = {
    RGB(255, 96, 80), RGB(255, 150, 60), RGB(255, 210, 70), RGB(170, 230, 80), RGB(70, 220, 130),
    RGB(60, 210, 220), RGB(80, 160, 255), RGB(150, 120, 255), RGB(220, 100, 240), RGB(255, 110, 170)};

/* the keyboard: key 0 = F3 ... 26 = G5; the black ones are the track row */
static const int8_t WHITE_OF[NKEYS] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6, 7, -1, 8, -1, 9, -1, 10, 11, -1, 12, -1, 13, 14, -1, 15};
static const int8_t BLACK_OF[NKEYS] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, 4, -1, -1, 5, -1, 6, -1, 7, -1, -1, 8, -1, 9, -1, -1, 10, -1};
static const uint8_t WHITE_K[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

static struct {
    uint8_t view, trk, grp, page[NPG];
    uint8_t spage;                       /* the sequencer's page of 16 steps */
    int8_t edit;                         /* the step the knobs edit, -1 none */
    int8_t press;                        /* a tap on an "on" step waiting to turn it off, -1 none */
    uint32_t press_t;
    int8_t oct;                          /* the keyboard's octave offset, -2..2 */
    uint8_t glo, gtop;                   /* GLO: the item, and the first row on screen */
    int16_t pot_ref;                     /* MASTER as a control: where the pot was when it was last used */
    uint8_t pot_armed;
    uint32_t bprev, kprev, used;
    uint8_t cfg, seq_hold;               /* SEQ held: the sequencer's config page, until it is let go */
    uint32_t seq_t0;
    uint8_t save_mode;                   /* SAVE was pressed: the track keys / GLO pick what to save, SAVE again saves it all, anything else leaves */
    uint8_t held_note[NKEYS];            /* live note of a held key + 1 */
    uint8_t held_trk[NKEYS];
    uint8_t held_scat[NKEYS];            /* scatter slot of a held key + 1 */
    uint8_t rec_step[NKEYS];             /* REC: the step a held key wrote + 1 */
    uint32_t rec_t0[NKEYS];
    char msg[24];
    uint32_t msg_until, frame;
    int8_t touched;                      /* the knob last turned, for its highlight */
    uint32_t touch_until;
} ui;

static int tw_len(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void say(const char *a, const char *b)
{
    char *q = put_s(ui.msg, a);
    if (b)
        put_s(q, b);
    ui.msg_until = plat_ms() + 1300u;
}

static clip_t *cur_clip(void) { return &proj.clip[ui.trk][dk_clip[ui.trk]]; }
static int clip_has(const clip_t *c) { return c->on[0] | c->on[1]; }
static int nsteps_page(void) { return LEN_OF(cur_clip()) / 16; }
static uint16_t tcol(int t) { return TRK_COL[t % NTRK]; }

/* the pages of a group: the engine's pots (the track's level is on the SEL page, and on CLIPS) */
#define PIX_LFO 110                                    /* the LFO's six controls: PIX_LFO + LFO_* */
static int grp_count(int g)
{
    int n = eng_nparams(cur_clip()->eng), i, c = 0;
    if (g == PG_LFO)
        return eng_info(cur_clip()->eng)->kind == EK_SYNTH ? NLFO : 0;     /* the LFO is for synths; a drum hit is too short for one */
    for (i = 0; i < n; i++)
        c += eng_group(cur_clip()->eng, i) == g;
    return c;
}
static int npages(int g) { return (grp_count(g) + 3) / 4; }
static int group_ok(int g) { return grp_count(g) > 0; }
static void fix_group(void)
{
    int g;
    if (group_ok(ui.grp))
        return;
    for (g = NPG - 1; g >= 0; g--)               /* the first group the engine has (EDIT last resort) */
        if (group_ok(g))
            ui.grp = (uint8_t)g;
    for (g = 0; g < NPG; g++)
        if (group_ok(g)) {
            ui.grp = (uint8_t)g;
            return;
        }
}
/* each track keeps the parameter group (FX, ENV, LFO, EDIT) and the page it was left on; SEL and SEQ stay as they are */
static uint8_t trk_grp[NTRK];
static uint8_t trk_page[NTRK][NPG];
static void select_track(int t)
{
    int g;
    if (t != ui.trk) {
        trk_grp[ui.trk] = ui.grp;
        for (g = 0; g < NPG; g++)
            trk_page[ui.trk][g] = ui.page[g];
        ui.trk = (uint8_t)t;
        ui.grp = trk_grp[t];
        for (g = 0; g < NPG; g++)
            ui.page[g] = trk_page[t][g];
        for (g = 0; g < NPG; g++)                    /* the engine may have fewer pages than the one it was left on */
            if (ui.page[g] >= npages(g))
                ui.page[g] = 0;
    }
    ui.edit = -1;
    ui.press = -1;
    if (ui.spage >= nsteps_page())
        ui.spage = 0;
    fix_group();
}

/* ---- the LFO's controls ---- */
static const char *const LFO_L[NLFO] = {"Shape", "Rate", "Depth", "Target", "Sync", "Retrig"};
static const char *const LFO_SHAPE_N[LFO_SHAPES] = {"SINE", "TRI", "SAW", "SQR", "RND"};
static const char *const LFO_DIV_N[10] = {"1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1BAR", "2BAR", "4BAR", "8BAR"};   /* the octaves of the synced rate */
static void lfo_text(const clip_t *c, int k, char *b)
{
    int v = c->lfo[k];
    switch (k) {
    case LFO_SHAPE: put_s(b, LFO_SHAPE_N[v < LFO_SHAPES ? v : 0]); break;
    case LFO_RATE:
        if (c->lfo[LFO_SYNC])                                          /* the nearest division: the rate between two is still felt */
            put_s(b, LFO_DIV_N[(v * 9 + 63) / 127]);
        else {
            float hz = LFO_HZ(v);
            if (hz < 1.0f) {                                           /* 0.02 .. 0.99 Hz */
                int c2 = (int)(hz * 100.0f + 0.5f);
                b = put_s(b, "0.");
                if (c2 < 10)
                    b = put_s(b, "0");
                b = put_i(b, c2);
            } else if (hz < 10.0f) {                                   /* 1.0 .. 9.9 Hz */
                int t10 = (int)(hz * 10.0f + 0.5f);
                b = put_i(put_s(put_i(b, t10 / 10), "."), t10 % 10);
            } else
                b = put_i(b, (int)(hz + 0.5f));
            put_s(b, "Hz");
        }
        break;
    case LFO_DEPTH: put_s(put_i(b, v * 100 / 127), "%"); break;
    case LFO_TARGET: put_s(b, v < eng_nparams(c->eng) ? eng_param(c->eng, v)->name : "-"); break;
    default: put_s(b, v ? "ON" : "OFF"); break;
    }
}
static float lfo_u(const clip_t *c, int k)
{
    int v = c->lfo[k];
    switch (k) {
    case LFO_SHAPE: return (float)v / (float)(LFO_SHAPES - 1);
    case LFO_TARGET: return eng_nparams(c->eng) > 1 ? (float)v / (float)(eng_nparams(c->eng) - 1) : 0.0f;
    case LFO_SYNC: case LFO_RETRIG: return v ? 1.0f : 0.0f;
    default: return (float)v / 127.0f;
    }
}
static void lfo_turn(int k, int d)
{
    clip_t *c = cur_clip();
    int v = c->lfo[k];
    switch (k) {
    case LFO_SHAPE: v += d > 0 ? 1 : -1; v = v < 0 ? 0 : v >= LFO_SHAPES ? LFO_SHAPES - 1 : v; break;
    case LFO_TARGET: v += d > 0 ? 1 : -1; v = v < 0 ? 0 : v >= eng_nparams(c->eng) ? eng_nparams(c->eng) - 1 : v; break;
    case LFO_SYNC: case LFO_RETRIG: v = d > 0; break;
    case LFO_RATE: v += d; v = v < 0 ? 0 : v > 127 ? 127 : v; break;        /* one step a click: the rate is continuous, so fine steps */
    default: v += d * 2; v = v < 0 ? 0 : v > 127 ? 127 : v; break;
    }
    c->lfo[k] = (uint8_t)v;
}

/* ---- the sequencer's config (SEQ held): length, rate, direction, swing of the clip on the track ---- */
static const char *const SEQ_L[4] = {"Length", "Rate", "Dir", "Swing"};
static const char *const SR_N[NSEQRATE] = {"1/16", "1/8", "1/32", "1/16T", "1/8T", "1/32T"};
static const char *const SD_N[NSEQDIR] = {"FWD", "REV", "PING", "RAND"};
static void seqcfg_text(const clip_t *c, int i, char *b)
{
    switch (i) {
    case 0: put_s(put_i(b, LEN_OF(c)), " STEPS"); break;
    case 1: put_s(b, SR_N[c->rate < NSEQRATE ? c->rate : 0]); break;
    case 2: put_s(b, SD_N[c->dir < NSEQDIR ? c->dir : 0]); break;
    default: put_s(put_i(b, c->swing * 100 / 127), "%"); break;
    }
}
static float seqcfg_u(const clip_t *c, int i)
{
    switch (i) {
    case 0: return (float)c->lensel / 2.0f;
    case 1: return (float)c->rate / (float)(NSEQRATE - 1);
    case 2: return (float)c->dir / (float)(NSEQDIR - 1);
    default: return (float)c->swing / 127.0f;
    }
}
static void seqcfg_turn(int i, int d)
{
    clip_t *c = cur_clip();
    int v, s = d > 0 ? 1 : -1;
    switch (i) {
    case 0:
        v = c->lensel + s;
        c->lensel = (uint8_t)(v < 0 ? 0 : v > 2 ? 2 : v);
        if (ui.spage >= LEN_OF(c) / 16)
            ui.spage = 0;
        ui.edit = -1;
        break;
    case 1: v = c->rate + s; c->rate = (uint8_t)(v < 0 ? 0 : v >= NSEQRATE ? NSEQRATE - 1 : v); break;
    case 2: v = c->dir + s; c->dir = (uint8_t)(v < 0 ? 0 : v >= NSEQDIR ? NSEQDIR - 1 : v); break;
    default: v = c->swing + d * 4; c->swing = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v); break;
    }
    ui.touched = (int8_t)i;
    ui.touch_until = plat_ms() + 900u;
}

/* ------------------------------------------------------------------ input --- */
static int param_ix(int slot)
{
    int k = ui.page[ui.grp] * 4 + slot;
    if (ui.view == V_HOME)                              /* CLIPS: KNOB 1 is the track's level */
        return slot == 0 ? PIX_LEVEL : -1;
    if (ui.grp == PG_LFO)
        return k < NLFO ? PIX_LFO + k : -1;
    return eng_param_at(cur_clip()->eng, ui.grp, k / 4, k % 4);
}

static void turn_param(int slot, int d)
{
    int i = param_ix(slot), v;
    const x0x_param_t *p;
    clip_t *c = cur_clip();
    if (i < 0)
        return;
    if (i >= PIX_LFO && i < PIX_LFO + NLFO) {
        lfo_turn(i - PIX_LFO, d);
        ui.touched = (int8_t)slot;
        ui.touch_until = plat_ms() + 900u;
        return;
    }
    if (i == PIX_LEVEL) {
        v = proj.set.level[ui.trk] + d * 2;
        proj.set.level[ui.trk] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        ui.touched = (int8_t)slot;
        ui.touch_until = plat_ms() + 900u;
        return;
    }
    p = eng_param(c->eng, i);
    v = c->par[i] + d * (p->max >= 100 ? 2 : 1);
    v = v < 0 ? 0 : v > p->max ? p->max : v;
    if (v != c->par[i])
        dk_set(ui.trk, i, v);
    ui.touched = (int8_t)slot;
    ui.touch_until = plat_ms() + 900u;
}

static void turn_step(int slot, int d)
{
    clip_t *c = cur_clip();
    dstep_t *s = &c->st[ui.edit];
    int synth = eng_info(c->eng)->kind == EK_SYNTH, v;
    switch (slot) {
    case 0:
        v = s->note + d;
        s->note = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        if (!synth && !dk_playing)
            dk_note(ui.trk, s->note, s->vel);            /* hear the pitch */
        break;
    case 1:
        v = s->vel + d * 2;
        s->vel = (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v);
        break;
    case 2:
        if (synth) {
            v = (s->len ? s->len : 3) + d;
            s->len = (uint8_t)(v < 1 ? 1 : v > 32 ? 32 : v);
        } else {
            v = s->prob + d * 5;
            s->prob = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
        }
        break;
    default:
        if (synth) {
            v = s->slide + d;
            s->slide = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v);
        }
        break;
    }
    ui.touched = (int8_t)slot;
    ui.touch_until = plat_ms() + 900u;
}

/* ---------------------------------------------------------------- the master knobs --- */
#define NASSIGN NKS
static const char *const ASSIGN_N[NKS] = {"VOLUME", "DJ FILTER", "DJ RESO", "FX1 AMOUNT", "FX2 AMOUNT", "BPM"};
static const char *const KNOB_N[NKNOB] = {"MASTER", "SELECT", "PRESETS", "ALGO"};

static int assign_of(const kslot_t *k) { return k->kind < NKS ? k->kind : 0; }
static void assign_text(int slot, char *b) { put_s(b, ASSIGN_N[assign_of(&proj.set.knob[slot])]); }
/* the next / previous thing a knob can do */
static void assign_step(int slot, int dir)
{
    kslot_t *k = &proj.set.knob[slot];
    k->kind = (uint8_t)((assign_of(k) + NASSIGN + (dir > 0 ? 1 : -1)) % NASSIGN);
    ui.pot_ref = -1;                                       /* MASTER: re-arm, so the new assignment does not jump */
}
/* the settings byte a job is (the GLO rows are the same bytes: a knob is a shortcut to a row) */
static uint8_t *kind_ptr(int kind)
{
    switch (kind) {
    case KS_VOL: return &proj.set.master_vol;
    case KS_DJ: return &proj.set.dj[0];
    case KS_RESO: return &proj.set.dj[1];
    case KS_FX1: return &proj.set.fxamt[0];
    case KS_FX2: return &proj.set.fxamt[1];
    default: return 0;
    }
}
static uint8_t *glo_ptr(int row)
{
    switch (row) {
    case G_VOL: return &proj.set.master_vol;
    case G_DJ: return &proj.set.dj[0];
    case G_RESO: return &proj.set.dj[1];
    case G_FX1A: return &proj.set.fxamt[0];
    case G_FX1P: return &proj.set.fxpar[0];
    case G_FX2A: return &proj.set.fxamt[1];
    case G_FX2P: return &proj.set.fxpar[1];
    default: return 0;
    }
}
static void bpm_text(char *b)
{
    char *q = put_i(b, proj.set.bpm_x10 / 10);
    put_s(q, ".");
    put_i(q + 1, proj.set.bpm_x10 % 10);
}
static void tempo_set(int x10) { dk_bpm(x10 < 300 ? 300 : x10 > 3000 ? 3000 : x10); }

/* FX 1 / FX 2: the next type. The reverb and the delay have one buffer each, so the other slot's is skipped */
static void fxtype_step(int i, int d)
{
    int v = proj.set.fxtype[i] < NMX ? proj.set.fxtype[i] : 0, other = proj.set.fxtype[i ^ 1], n;
    for (n = 0; n < NMX; n++) {
        v = (v + NMX + (d > 0 ? 1 : -1)) % NMX;
        if (!(MX_STATEFUL(v) && v == other))
            break;
    }
    proj.set.fxtype[i] = (uint8_t)v;
}

static int knob_value(int slot)
{
    int kind = assign_of(&proj.set.knob[slot]);
    const uint8_t *p = kind_ptr(kind);
    return p ? *p : kind == KS_BPM ? (proj.set.bpm_x10 - 300) * 127 / 2700 : 0;
}
static void knob_text(int slot, char *b)             /* the value, in its own unit */
{
    int kind = assign_of(&proj.set.knob[slot]);
    if (kind == KS_DJ)
        dj_format(knob_value(slot), b);
    else if (kind == KS_BPM)
        bpm_text(b);
    else
        put_s(put_i(b, knob_value(slot) * 100 / 127), "%");
}

static void knob_say(int slot)
{
    char b[24], *q;
    assign_text(slot, b);
    q = b + (int)tw_len(b);
    *q++ = ' ';
    knob_text(slot, q);
    say(b, 0);
}

/* set a knob's value (0..127), and say it */
static int knob_put(int slot, int v)                /* 0..127 into what the knob does; 0 if it does nothing */
{
    int kind = assign_of(&proj.set.knob[slot]);
    uint8_t *p = kind_ptr(kind);
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    if (p)
        *p = (uint8_t)v;
    else if (kind == KS_BPM)
        tempo_set(300 + v * 2700 / 127);                  /* the pot: 30 .. 300 */
    else
        return 0;
    return 1;
}
static void knob_set(int slot, int v)
{
    if (knob_put(slot, v))
        knob_say(slot);
}

static void knob_turn(int slot, int d)
{
    int kind = assign_of(&proj.set.knob[slot]);
    if (slot == 0 && kind == KS_VOL)
        return;
    if (kind == KS_BPM) {                                   /* an encoder: a whole BPM a detent */
        tempo_set(proj.set.bpm_x10 + d * 10);
        knob_say(slot);
    } else
        knob_set(slot, knob_value(slot) + d * 2);
}

/* MASTER is a pot: absolute. As the volume the engine reads it itself; as anything else its position is the
 * value, but only once it has been moved (so a new assignment, or power on, does not make the value jump) */
static void master_pot(void)
{
    int pos = (int)(fm_sqrtf((float)plat_master() * 256.0f) * (1.0f / 8.0f));
    pos = pos > 127 ? 127 : pos;
    if (ui.pot_ref < 0 || proj.set.knob[0].kind == KS_VOL) {
        ui.pot_ref = (int16_t)pos;
        ui.pot_armed = 0;
        return;
    }
    if (!ui.pot_armed && (pos - ui.pot_ref > 2 || ui.pot_ref - pos > 2))
        ui.pot_armed = 1;
    if (ui.pot_armed && pos != ui.pot_ref) {
        ui.pot_ref = (int16_t)pos;
        knob_set(0, pos);
    }
}

static void write_step(int t, int s, int note, int vel)
{
    clip_t *c = &proj.clip[t][dk_clip[t]];
    if (!step_on(c, s)) {                             /* a fresh step: plain defaults, not what was left in it */
        c->st[s].prob = 100;
        c->st[s].len = 3;
        c->st[s].slide = 0;
    }
    c->on[s >> 5] |= 1u << (s & 31);
    c->st[s].note = (uint8_t)note;
    c->st[s].vel = (uint8_t)vel;
}

/* REC: a synth note lasts as long as the key was held */
static void rec_release(int k)
{
    clip_t *c;
    if (!ui.rec_step[k])
        return;
    c = &proj.clip[ui.held_trk[k]][dk_clip[ui.held_trk[k]]];
    if (eng_info(c->eng)->kind == EK_SYNTH) {
        uint32_t step_ms = 150000u / (proj.set.bpm_x10 ? proj.set.bpm_x10 : 1200u), q;
        q = (plat_ms() - ui.rec_t0[k]) * 4u / step_ms;
        c->st[ui.rec_step[k] - 1].len = (uint8_t)(q < 1 ? 1 : q > 32 ? 32 : q);
    }
    ui.rec_step[k] = 0;
}

static void key_note(int k, int down)
{
    int note = 53 + k + 12 * ui.oct;
    if (note < 0)
        note = 0;
    if (note > 127)
        note = 127;
    if (down && ui.view == V_ARP && proj.set.arp_mode[ui.trk] == ARP_TRSP) {     /* TRSP: the key is a transposition from middle C, held until the next */
        dk_transpose(ui.trk, note - 60);
        if (!dk_playing) {                                                      /* stopped: hear it on the track */
            ui.held_note[k] = (uint8_t)(note + 1);
            ui.held_trk[k] = ui.trk;
            dk_note(ui.trk, note, 100);
        }
        return;
    }
    if (down) {
        int fwd = 0;
        ui.held_note[k] = (uint8_t)(note + 1);
        ui.held_trk[k] = ui.trk;
        ui.rec_step[k] = 0;
        if (ui.view == V_REC && dk_playing) {         /* quantised to the nearest step: the first half of a step rounds back,
                                                       * the second half forward (and that note sounds when its step comes) */
            int len = LEN_OF(cur_clip()), p = dk_pos[ui.trk] % len;
            fwd = dk_tphase[ui.trk] >= 128;
            if (fwd)
                p = dk_next[ui.trk] % len;                 /* the step the clip plays next, in its own direction */
            write_step(ui.trk, p, note, 100);
            ui.rec_step[k] = (uint8_t)(p + 1);
            ui.rec_t0[k] = plat_ms();
        }
        if (!fwd)
            dk_note(ui.trk, note, 100);
    } else if (ui.held_note[k]) {
        dk_note(ui.held_trk[k], ui.held_note[k] - 1, 0);
        ui.held_note[k] = 0;
    }
}

static void save_say(int n, const char *what)
{
    char buf[24];
    if (n < 0)
        say("SAVE FAILED", 0);
    else if (!n)
        say("UNCHANGED", 0);
    else
        say(put_s(buf, what), 0);
}

/* does track t's clip on screen differ from what was saved? */
static int trk_dirty(int t) { return store_clip_dirty(t, dk_clip[t]); }

static void key_down(int k)
{
    int w = WHITE_OF[k], b = BLACK_OF[k];
    uint32_t now = plat_ms();
    if (ui.save_mode) {                                 /* a track key saves that track's clip on screen; any other key leaves */
        if (b >= 0 && b < NTRK && trk_dirty(b))
            save_say(store_save(b, dk_clip[b]), "CLIP SAVED");
        ui.save_mode = 0;
        return;
    }
    if (ui.view == V_ARP || ui.view == V_REC) {
        key_note(k, 1);
        return;
    }
    if (b >= 0) {
        if (b == 10)
            return;                                     /* the mute key: it acts with a track */
        if (ui.kprev & (1u << BLACK_K[10])) {           /* MUTE + track */
            dk_mute(b, !dk_muted[b]);
            say(dk_muted[b] ? "UNMUTE T" : "MUTE T", 0);
            put_i(ui.msg + (dk_muted[b] ? 8 : 6), b + 1);
            ui.used |= 1u << 31;
            return;
        }
        select_track(b);
        return;
    }
    if (w < 0)
        return;
    switch (ui.view) {
    case V_SEQ: {
        int s = ui.spage * 16 + w;
        clip_t *c = cur_clip();
        if (s >= LEN_OF(c))
            return;
        if (!step_on(c, s)) {
            step_flip(c, s);
            ui.edit = (int8_t)s;
            ui.press = -1;
            if (eng_info(c->eng)->kind == EK_DRUM && !dk_playing)
                dk_note(ui.trk, c->st[s].note, c->st[s].vel);
        } else {
            ui.press = (int8_t)s;
            ui.press_t = now;
        }
        break;
    }
    case V_HOME:
        if (w != dk_clip[ui.trk] && store_clip_dirty(ui.trk, w))   /* loading a clip that was changed and not saved: it comes back as it was */
            store_revert(ui.trk, w);
        dk_launch(ui.trk, w, dk_queued[ui.trk] == w);   /* again on the queued clip: now */
        break;
    case V_PARAM:
    case V_GLO:
    case V_COMMON:                                       /* SEL too: the keys play the track, or scatter, as on the other sound pages */
        if (dk_playing) {                                /* playing: the second row is the scatter FX, not notes */
            ui.held_scat[k] = (uint8_t)(w + 1);
            ui.held_trk[k] = ui.trk;
            dk_scatter(ui.trk, w, 1);
        } else {                                        /* stopped: audition the track */
            dk_note(ui.trk, 48 + w, 100);
            ui.held_note[k] = (uint8_t)(48 + w + 1);
            ui.held_trk[k] = ui.trk;
        }
        break;
    default:
        break;
    }
}

static void key_up(int k)
{
    int w = WHITE_OF[k];
    if (ui.held_note[k]) {
        rec_release(k);
        dk_note(ui.held_trk[k], ui.held_note[k] - 1, 0);
        ui.held_note[k] = 0;
    }
    if (ui.held_scat[k]) {
        dk_scatter(ui.held_trk[k], ui.held_scat[k] - 1, 0);
        ui.held_scat[k] = 0;
    }
    if (ui.view == V_SEQ && w >= 0 && ui.press == ui.spage * 16 + w) {   /* a short tap on an "on" step: off */
        step_flip(cur_clip(), ui.press);
        if (ui.edit == ui.press)
            ui.edit = -1;
        ui.press = -1;
    }
}

static void save_all(void)
{
    char b[24];
    int n = store_save(-1, 0);
    if (n < 0) {
        char *q = put_s(b, "SAVE FAILED ");
        put_i(q, n);
        say(b, 0);
    } else if (!n)
        say("NOTHING TO SAVE", 0);
    else {
        char *q = put_i(b, n);
        put_s(q, n == 1 ? " CLIP SAVED" : " CLIPS SAVED");
        say(b, 0);
    }
}

static void button_down(int b)
{
    int g;
    if (ui.save_mode) {                                 /* SAVE: GLO saves the settings, SAVE again everything, any other button leaves */
        if (b == B_GLO) {
            int n = store_save_set();
            save_say(n, "GLO SAVED");
            ui.save_mode = 0;
            return;
        }
        ui.save_mode = 0;
        if (b == B_SAVE) {
            save_all();
            return;
        }
    } else if (b == B_SAVE) {
        if (store_dirty() || store_set_dirty())
            ui.save_mode = 1;
        else
            say("NOTHING TO SAVE", 0);
        return;
    }
    switch (b) {
    case B_PLAY:
        dk_play(!dk_playing);
        break;
    case B_SEQ:
        ui.view = V_SEQ;
        ui.seq_hold = 1;                                      /* kept down: the config page opens */
        ui.seq_t0 = plat_ms();
        break;
    case B_FX: case B_ENV: case B_LFO: case B_EDIT:
        for (g = 0; g < NPG; g++)
            if (GRP_BTN[g] == b)
                break;
        if (!group_ok(g)) {
            say("NO ", GRP_N[g]);
            break;
        }
        if (ui.view == V_PARAM && ui.grp == g)
            ui.page[g] = (uint8_t)((ui.page[g] + 1) % npages(g));
        ui.view = V_PARAM;
        ui.grp = (uint8_t)g;
        break;
    case B_ARP:
        ui.view = ui.view == V_ARP ? V_SEQ : V_ARP;
        break;
    case B_REC:
        ui.view = ui.view == V_REC ? V_SEQ : V_REC;
        break;
    case B_HOME:
        ui.view = V_HOME;
        break;
    case B_GLO:
        ui.view = V_GLO;
        break;
    case B_SAVE:
        break;
    case B_SEL:
        ui.view = V_COMMON;                                           /* SEL: what every engine has: volume, the two sends, engine */
        break;
    case B_OCTDN: case B_OCTUP: {
        int d = b == B_OCTUP ? 1 : -1;
        if (ui.view == V_ARP || ui.view == V_REC) {
            ui.oct = (int8_t)(ui.oct + d < -2 ? -2 : ui.oct + d > 2 ? 2 : ui.oct + d);
        } else if (ui.view == V_PARAM) {
            int n = npages(ui.grp);
            ui.page[ui.grp] = (uint8_t)((ui.page[ui.grp] + n + d) % n);
        } else {
            int n = nsteps_page();
            ui.spage = (uint8_t)((ui.spage + n + d) % n);
            ui.edit = -1;
        }
        break;
    }
    }
}

static void input(void)
{
    uint32_t b = plat_buttons(), k = plat_keys(), bd = b & ~ui.bprev, kd = k & ~ui.kprev, ku = ui.kprev & ~k, m;
    int i;
    if ((ui.view != V_ARP && ui.view != V_REC) || 1) {          /* releases first, so a note ends before the next begins */
        for (i = 0; i < NKEYS; i++)
            if ((ku >> i) & 1u)
                key_up(i);
    }
    for (i = 0; i < NB; i++)
        if ((bd >> i) & 1u)
            button_down(i);
    if (ui.seq_hold) {                                      /* SEQ kept down for a moment: its config page, until it is let go */
        if (!((b >> B_SEQ) & 1u))
            ui.seq_hold = ui.cfg = 0;
        else if (!ui.cfg && plat_ms() - ui.seq_t0 > 400u && ui.view == V_SEQ) {
            ui.cfg = 1;
            ui.edit = -1;
        }
    }
    for (i = 0; i < NKEYS; i++)
        if ((kd >> i) & 1u)
            key_down(i);
    ui.bprev = b;
    ui.kprev = k;
    if (ui.press >= 0 && (ui.kprev >> WHITE_K[ui.press & 15]) & 1u && plat_ms() - ui.press_t > 350u) {
        ui.edit = ui.press;                                      /* held: edit it, and it stays on */
        ui.press = -1;
    }
    for (i = 0; i < 4; i++) {
        int d = plat_enc(EN_K1 + i);
        if (!d)
            continue;
        if (ui.cfg) {
            seqcfg_turn(i, d);
            continue;
        }
        if (ui.view == V_GLO) {
            if (i == 0)
                ui.glo = (uint8_t)((ui.glo + NGLO + (d > 0 ? 1 : -1)) % NGLO);
            else if (i == 1) {
                if (ui.glo == G_TEMPO) {
                    tempo_set(proj.set.bpm_x10 + d * 10);
                } else if (ui.glo == G_VOL && proj.set.knob[0].kind == KS_VOL) {
                    say("VOLUME = MASTER KNOB", 0);          /* the pot is the volume: this row only counts when it is not */
                } else if (glo_ptr(ui.glo)) {                   /* a number: the volume, the DJ filter, a send effect's amount or parameter */
                    uint8_t *gp = glo_ptr(ui.glo);
                    int v = *gp + d * 2;
                    *gp = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
                } else if (ui.glo == G_FX1 || ui.glo == G_FX2) {  /* what FX 1 / FX 2 is */
                    fxtype_step(ui.glo == G_FX2, d);
                } else if (ui.glo >= G_K0 && ui.glo <= G_K3) {  /* what a knob does */
                    assign_step(ui.glo - G_K0, d);
                } else if (ui.glo >= GLO_COMP0 && ui.glo < GLO_COMP0 + 6) {      /* the master compressor */
                    int j = ui.glo - GLO_COMP0, v = proj.set.comp[j] + (j == 1 ? (d > 0 ? 1 : -1) : d * 2);
                    proj.set.comp[j] = (uint8_t)(v < 0 ? 0 : v > (j == 1 ? 9 : 127) ? (j == 1 ? 9 : 127) : v);
                }
            }
        } else if (ui.view == V_COMMON) {
            int t = ui.trk, v;
            if (i == 3) {                                  /* the engine of the clip on this track */
                int e = (cur_clip()->eng + eng_count() + (d > 0 ? 1 : -1)) % eng_count();
                dk_engine(t, e);
                fix_group();
            } else if (i == 0) {
                v = proj.set.level[t] + d * 2;
                proj.set.level[t] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
            } else {
                uint8_t *sd = &proj.set.send[i - 1][t];
                v = *sd + d * 2;
                *sd = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
            }
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 900u;
        } else if (ui.view == V_ARP) {
            int t = ui.trk, v;
            if (i > 0 && !ARP_ON(t))
                continue;                              /* KEYS: rate, gate and octaves belong to the arpeggiator */
            if (i == 0) {
                v = proj.set.arp_mode[t] + d;
                if (proj.set.arp_mode[t] == ARP_TRSP)
                    dk_transpose(t, 0);                         /* leaving TRSP: back to the clip's own pitch */
                proj.set.arp_mode[t] = (uint8_t)(v < 0 ? 0 : v > ARP_TRSP ? ARP_TRSP : v);
            } else if (i == 1) {
                v = proj.set.arp_rate[t] + d;
                proj.set.arp_rate[t] = (uint8_t)(v < 0 ? 0 : v > 2 ? 2 : v);
            } else if (i == 2) {
                v = proj.set.arp_gate[t] + d * 5;
                proj.set.arp_gate[t] = (uint8_t)(v < 10 ? 10 : v > 100 ? 100 : v);
            } else {
                v = proj.set.arp_oct[t] + d;
                proj.set.arp_oct[t] = (uint8_t)(v < 1 ? 1 : v > 3 ? 3 : v);
            }
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 900u;
        } else if (ui.view == V_SEQ && ui.edit >= 0)
            turn_step(i, d);
        else if (ui.view == V_SEQ)
            continue;                                      /* SEQ: the knobs are the step's; with no step picked they do nothing */
        else
            turn_param(i, d);
    }
    m = (uint32_t)plat_enc(EN_SELECT);                 /* the master knobs: each does what it is assigned to */
    if ((int32_t)m)
        knob_turn(1, (int32_t)m);
    m = (uint32_t)plat_enc(EN_PRESET);
    if ((int32_t)m)
        knob_turn(2, (int32_t)m);
    m = (uint32_t)plat_enc(EN_ALGO);
    if ((int32_t)m)
        knob_turn(3, (int32_t)m);
    master_pot();
}

/* ---------------------------------------------------------------- drawing --- */
static void put_num(char *b, int v) { put_i(b, v); }

/* the CPU load as a bar along the very bottom of the screen (1 row, its own tiny region: 0.5 KB).
 * It is sampled every 8th frame and changes only when the load moved a few percent, so it is
 * re-sent about once a second at most, not every frame; it does not have to be exact.
 * Fill = smoothed load, tick = the peak of the last ~2 s. */
static uint8_t cpu_fill, cpu_peak, cpu_ema;
static void cpu_sample(void)
{
    cpu_ema = (uint8_t)((cpu_ema * 3u + (dk_load_pct > 100u ? 100u : dk_load_pct)) >> 2);
    if ((ui.frame & 7u) != 0)
        return;
    if (cpu_ema > cpu_fill + 3 || cpu_ema + 3 < cpu_fill)
        cpu_fill = cpu_ema;
    if (dk_load_peak > cpu_peak + 3u || dk_load_peak + 3u < cpu_peak)
        cpu_peak = (uint8_t)(dk_load_peak > 100u ? 100u : dk_load_peak);
}
static void draw_cpu_bar(void)
{
    int32_t f = cpu_fill * 240 / 100, k = cpu_peak * 238 / 100;
    cv_begin(240, 1, C_BLACK);
    cv_rect(0, 0, f, 1, cpu_fill > 80 ? RGB(230, 60, 55) : C_DIM);   /* quiet; red over 80% */
    if (cpu_peak > cpu_fill)
        cv_rect(k, 0, 2, 1, C_GRAY);
    cv_commit(3, 0, CPU_Y);
}

static void draw_header(void)
{
    char b[24];
    int32_t x, w;
    const tribe_eng_t *e = eng_info(cur_clip()->eng);
    cv_begin(240, HDR_H, C_BLACK);
    cv_rect(0, HDR_H - 1, 240, 1, C_LINE);
    if (ui.msg_until && (int32_t)(ui.msg_until - plat_ms()) > 0) {
        box(0, 0, 240, HDR_H - 1, C_HI);
        text_c(120, vc(&FONT_B, 0, HDR_H - 1), &FONT_B, ui.msg, C_BLACK);
        cv_commit(0, 0, 0);
        return;
    }
    ui.msg_until = 0;
    b[0] = 'T';
    put_i(b + 1, ui.trk + 1);
    put_s(b + (ui.trk > 8 ? 3 : 2), " ");
    put_s(b + (ui.trk > 8 ? 4 : 3), e->name);
    w = tw(&FONT_B, b) + 10;
    box(0, 1, w, 17, tcol(ui.trk));
    cv_text(5, vc(&FONT_B, 1, 17), &FONT_B, b, C_BLACK);
    x = w + 6;
    if (dk_bound[ui.trk] != cur_clip()->eng)
        x = cv_text(x, vc(&FONT_B, 1, 17), &FONT_B, "BUSY", RGB(255, 90, 80)) + 6;
    else if (ui.view == V_PARAM) {
        put_s(b, GRP_N[ui.grp]);
        x = cv_text(x, vc(&FONT_B, 1, 17), &FONT_B, b, C_HI) + 6;
    } else if (ui.view == V_ARP)
        x = cv_text(x, vc(&FONT_B, 1, 17), &FONT_B, proj.set.arp_mode[ui.trk] == ARP_TRSP ? "TRSP" : proj.set.arp_mode[ui.trk] ? "ARP" : "KEYS", C_HI) + 6;
    else
        x = cv_text(x, vc(&FONT_B, 1, 17), &FONT_B, VIEW_N[ui.view], C_HI) + 6;
    {   /* the clip, in words; a clip waiting for the pattern's end shows where it goes */
        char *q = put_s(b, "CLIP ");
        int32_t cx;
        q = put_i(q, dk_clip[ui.trk] + 1);
        if (dk_queued[ui.trk] != 0xFF) {
            *q++ = '>';
            put_i(q, dk_queued[ui.trk] + 1);
        }
        cx = cv_text(136, vc(&FONT_B, 1, 17), &FONT_B, b, dk_queued[ui.trk] != 0xFF ? C_AMB : C_WHITE);
        if (dk_queued[ui.trk] == 0xFF) {
            put_i(b, proj.set.bpm_x10 / 10);
            text_r(218, vc(&FONT_B, 1, 17), &FONT_B, b, C_AMB);
        }
        (void)cx;
    }
    if (dk_playing)
        play_icon(226, 5, C_HI);
    else
        cv_rect(226, 5, 9, 9, C_DIM);
    cv_commit(0, 0, 0);
}

static void draw_seq(void)
{
    clip_t *c = cur_clip();
    int i, t, len = LEN_OF(c), pg = ui.spage, phead = dk_pos[ui.trk];
    uint16_t col = tcol(ui.trk);
    char b[24];
    /* the page of 16 steps: bar height = velocity */
    for (i = 0; i < 16; i++) {
        int s = pg * 16 + i, x = i * 15, on = s < len && step_on(c, s);
        uint16_t bg = s < len ? ((i & 3) == 0 ? RGB(40, 40, 48) : C_LINE) : C_BLACK;
        box(x, 4, 14, 38, bg);
        if (on) {
            int h = 6 + c->st[s].vel * 32 / 127;
            cv_rect(x + 1, 41 - h, 12, h, ui.edit == s ? C_WHITE : col);
        }
        if (dk_playing && phead == s)
            cv_rect(x, 45, 14, 3, C_WHITE);
        if (ui.edit == s)
            frame(x, 4, 14, 38, C_WHITE);
    }
    b[0] = 'P';
    put_i(b + 1, pg + 1);
    cv_text(0, vc(&FONT_XS, 50, 12), &FONT_XS, "PAGE", C_GRAY);
    cv_text(30, vc(&FONT_XS, 50, 12), &FONT_XS, b + 1, C_WHITE);
    put_s(b, "/");
    put_i(b + 1, len / 16);
    cv_text(38, vc(&FONT_XS, 50, 12), &FONT_XS, b, C_GRAY);
    cv_text(70, vc(&FONT_XS, 50, 12), &FONT_XS, "LEN", C_GRAY);
    put_i(b, len);
    cv_text(92, vc(&FONT_XS, 50, 12), &FONT_XS, b, C_WHITE);
    if (dk_trans[ui.trk]) {                              /* a live transposition from the ARP page is easy to forget */
        char *q2 = put_s(b, "TRSP ");
        if (dk_trans[ui.trk] > 0)
            *q2++ = '+';
        put_i(q2, dk_trans[ui.trk]);
        text_r(238, vc(&FONT_XS, 50, 12), &FONT_XS, b, C_AMB);
    }
    if (ui.edit >= 0) {
        put_note(b, c->st[ui.edit].note);
        cv_text(130, vc(&FONT_XS, 50, 12), &FONT_XS, "STEP", C_GRAY);
        put_i(b, ui.edit + 1);
        cv_text(160, vc(&FONT_XS, 50, 12), &FONT_XS, b, C_WHITE);
    }
    /* the ten tracks: meter, number, clip, muted */
    cv_rect(0, 68, 240, 1, C_LINE);
    for (t = 0; t < NTRK; t++) {
        int x = t * 24, h = (int)(dk_peak[t] * 38u / 32768u), hit = dk_hit[t];
        uint16_t tc = tcol(t);
        if (h > 38)
            h = 38;
        if (t == ui.trk)
            box(x + 1, 72, 22, 62, RGB(30, 30, 38));
        cv_rect(x + 9, 108 - h, 6, h, dk_muted[t] ? C_DIM : tc);
        if (hit)
            dot(x + 12, 76, 2, tc);
        put_i(b, t + 1);
        text_c(x + 12, vc(&FONT_B, 112, 12), &FONT_B, b, t == ui.trk ? C_WHITE : C_GRAY);
        put_i(b, dk_clip[t] + 1);
        box(x + 5, 124, 14, 11, dk_queued[t] != 0xFF ? C_AMB : C_LINE);
        text_c(x + 12, vc(&FONT_XS, 124, 11), &FONT_XS, b, dk_queued[t] != 0xFF ? C_BLACK : C_WHITE);
        if (dk_muted[t]) {
            cv_line(x + 4, 80, x + 20, 104, RGB(255, 90, 80));
            cv_line(x + 20, 80, x + 4, 104, RGB(255, 90, 80));
        }
    }
}

static void draw_param(void)
{
    const clip_t *c = cur_clip();
    int n = eng_nparams(c->eng), i, row = 0;
    char v[8];
    uint16_t col = tcol(ui.trk);
    text_c(120, vc(&FONT_L, 2, 40), &FONT_B, eng_info(c->eng)->name, col);
    if (ui.grp == PG_LFO) {                             /* the clip's LFO: six controls */
        int k;
        char lv[12];
        for (k = 0; k < NLFO; k++) {
            int on = k / 4 == ui.page[ui.grp];
            lfo_text(c, k, lv);
            cv_text(10 + (k & 1 ? 120 : 0), 44 + (k >> 1) * 15, &FONT_S, LFO_L[k], on ? C_WHITE : C_GRAY);
            text_r(110 + (k & 1 ? 120 : 0), 44 + (k >> 1) * 15, &FONT_S, lv, on ? C_HI : C_GRAY);
        }
        return;
    }
    for (i = 0; i < n; i++) {
        int g = eng_group(c->eng, i), slot;
        const x0x_param_t *p;
        if (g != ui.grp)
            continue;
        p = eng_param(c->eng, i);
        if (row / 4 != ui.page[ui.grp]) {                /* one page of four at a time, as the knobs: the page dots say where the others are */
            row++;
            continue;
        }
        slot = row & 3;
        cv_text(10 + (slot & 1 ? 120 : 0), 36 + (slot >> 1) * 15, &FONT_S, p->name, C_WHITE);   /* 36: clear of the scatter row (102) */
        eng_format(c->eng, i, c->par[i], v);
        text_r(110 + (slot & 1 ? 120 : 0), 36 + (slot >> 1) * 15, &FONT_S, v, C_HI);
        row++;
    }
}

static void draw_home(void)
{
    int t, cl;
    char b[4];
    for (t = 0; t < NTRK; t++) {
        int y = 3 + t * 13;
        uint16_t tc = tcol(t);
        put_i(b, t + 1);
        text_r(20, vc(&FONT_XS, y, 11), &FONT_XS, b, t == ui.trk ? C_WHITE : C_GRAY);
        for (cl = 0; cl < NCLIP; cl++) {
            int x = 26 + cl * 13, now = dk_clip[t] == cl, q = dk_queued[t] == cl, has = clip_has(&proj.clip[t][cl]);
            cv_rect(x, y, 12, 11, t == ui.trk ? RGB(40, 40, 48) : C_LINE);
            if (now)
                cv_rect(x, y, 12, 11, dk_muted[t] ? C_DIM : tc);
            else if (q && (ui.frame & 16u))
                cv_rect(x, y, 12, 11, C_AMB);
            else if (has)
                cv_rect(x + 4, y + 4, 4, 3, C_GRAY);
        }
        if (t == ui.trk)
            cv_rect(22, y + 5, 2, 1, tc);
    }
}

/* the sixteen scatter keys under the parameters: eight on the track, eight on the mix (live while playing) */
static void draw_scatter(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        int row = i >> 3, col = i & 7, on = i < 8 ? dk_scat[ui.trk] == i + 1 : dk_scat_g == i - 7;
        uint16_t c = i < 8 ? tcol(ui.trk) : C_AMB;
        int x = col * 30, y = 102 + row * 18, w = 29, h = 16;
        int lm = !(i & 3);                              /* the 1st, 5th, 9th, 13th: a lighter ground, like their full LEDs */
        box(x, y, w, h, on ? c : dk_playing ? (lm ? RGB(78, 78, 90) : RGB(40, 40, 48)) : lm ? blend(C_LINE, C_DIM, 8) : C_LINE);
        text_c(x + w / 2, vc(&FONT_XS, y, h), &FONT_XS, sc_name(i), on ? C_BLACK : (dk_playing ? C_WHITE : C_GRAY));
    }
}

/* the COMMON page: every track as a column. The wide bar is the live level (a meter, after the volume and the mute); the white tick across it is the
 * volume; the two thin bars beside it are the sends. The selected track sits on a lighter ground. */
static void draw_common(void)
{
    int t;
    char b[4], fxl[2][16];
    static const uint16_t CS[2] = {RGB(255, 184, 70), RGB(100, 200, 255)};
    for (t = 0; t < NTRK; t++) {
        int x = t * 24 + 1, sel = t == ui.trk, h, lv, y;
        uint16_t tc = tcol(t);
        if (sel)
            box(x - 1, 16, 24, 118, RGB(30, 30, 38));
        cv_rect(x + 2, 20, 11, 88, RGB(18, 18, 22));                      /* the meter's trough */
        h = (int)(dk_peak[t] * 88u / 32768u);
        h = h > 88 ? 88 : h;
        cv_rect(x + 2, 108 - h, 11, h, dk_muted[t] ? C_DIM : tc);
        lv = proj.set.level[t] * 88 / 127;
        y = 108 - lv;
        cv_rect(x, y - 1, 15, 3, C_WHITE);                                /* the volume */
        h = proj.set.send[0][t] * 88 / 127;
        cv_rect(x + 16, 108 - h, 2, h, sel ? CS[0] : dim(CS[0], 8));
        h = proj.set.send[1][t] * 88 / 127;
        cv_rect(x + 19, 108 - h, 2, h, sel ? CS[1] : dim(CS[1], 8));
        put_i(b, t + 1);
        text_c(x + 10, vc(&FONT_B, 112, 14), &FONT_B, b, sel ? C_WHITE : C_GRAY);
    }
    put_s(put_s(fxl[0], "FX1 "), mx_name(proj.set.fxtype[0]));
    put_s(put_s(fxl[1], "FX2 "), mx_name(proj.set.fxtype[1]));
    cv_rect(2, 6, 6, 4, C_WHITE);
    cv_text(10, vc(&FONT_XS, 2, 12), &FONT_XS, "VOLUME", C_GRAY);
    cv_rect(58, 6, 3, 4, CS[0]);
    cv_text(64, vc(&FONT_XS, 2, 12), &FONT_XS, fxl[0], C_GRAY);
    cv_rect(150, 6, 3, 4, CS[1]);
    cv_text(156, vc(&FONT_XS, 2, 12), &FONT_XS, fxl[1], C_GRAY);
}

static void draw_text_screen(const char *a, const char *b2, uint16_t c)
{
    text_c(120, vc(&FONT_L, 20, 40), &FONT_B, a, c);
    text_c(120, vc(&FONT_S, 70, 20), &FONT_S, b2, C_GRAY);
}

/* a compressor pot in its unit (the master's maps: THRESH -48..0 dB, ATTACK 0.1..100 ms, RELEASE 10..1500 ms, MAKEUP 0..24 dB) */
static void comp_text(int j, char *b)
{
    static const char *const RATIO[10] = {"1:1", "1.5:1", "2:1", "3:1", "4:1", "6:1", "8:1", "12:1", "20:1", "INF"};
    int p = proj.set.comp[j];
    float ms;
    char *q;
    switch (j) {
    case 0: put_s(put_i(b, -48 + p * 48 / 127), "dB"); break;
    case 1: put_s(b, RATIO[p < 10 ? p : 9]); break;
    case 2: case 3:
        ms = j == 2 ? 0.1f * fm_powf(1000.0f, (float)p / 127.0f) : 10.0f * fm_powf(150.0f, (float)p / 127.0f);
        if (ms < 10.0f)
            q = put_i(put_s(put_i(b, (int)ms), "."), (int)(ms * 10.0f) % 10);
        else
            q = put_i(b, (int)(ms + 0.5f));
        put_s(q, "ms");
        break;
    case 4: put_s(put_i(b, p * 24 / 127), "dB"); break;
    default: put_s(put_i(b, p * 100 / 127), "%"); break;
    }
}

/* a GLO row's value, in words */
static void glo_text(int i, char *v)
{
    v[0] = 0;
    if (i == G_TEMPO)
        bpm_text(v);
    else if (i == G_VOL && proj.set.knob[0].kind == KS_VOL)
        put_s(v, "MASTER KNOB");
    else if (i == G_DJ)
        dj_format(proj.set.dj[0], v);
    else if (i == G_FX1 || i == G_FX2)
        put_s(v, mx_name(proj.set.fxtype[i == G_FX2]));
    else if (i == G_FX1P || i == G_FX2P)
        mx_format(proj.set.fxtype[i == G_FX2P], *glo_ptr(i), v);
    else if (glo_ptr(i))
        put_s(put_i(v, *glo_ptr(i) * 100 / 127), "%");
    else if (i >= G_K0 && i <= G_K3)
        assign_text(i - G_K0, v);
    else if (i >= GLO_COMP0 && i < GLO_COMP0 + 6)
        comp_text(i - GLO_COMP0, v);
    else if (i == G_CLIPS)
        put_s(put_i(v, store_dirty()), " UNSAVED");
    else if (i == G_BAT) {
        int bat = plat_battery();
        put_s(v, bat == 4 ? "USB POWER" : bat < 0 ? "?" : bat == 3 ? "FULL" : bat == 2 ? "2/3" : bat == 1 ? "1/3" : "LOW");
    } else {
        char *q = put_i(v, (int)dk_load_pct);
        q = put_s(q, "% PEAK ");
        q = put_i(q, (int)dk_load_peak);
        put_s(q, "%");
    }
}

static void draw_glo(void)
{
    int i, rows = 7;
    char v[24], lab[24];
    if (ui.glo < ui.gtop)
        ui.gtop = ui.glo;
    if (ui.glo >= ui.gtop + rows)
        ui.gtop = (uint8_t)(ui.glo - rows + 1);
    for (i = ui.gtop; i < ui.gtop + rows && i < NGLO; i++) {
        int y = 3 + (i - ui.gtop) * 19, on = ui.glo == i;
        if (on)
            box(0, y - 1, 236, 18, RGB(40, 40, 48));
        if (i == G_FX1P || i == G_FX2P)                     /* its parameter's own name: FX1 FDBK, FX2 SIZE, ... */
            put_s(put_s(put_s(lab, "FX"), i == G_FX1P ? "1 " : "2 "), mx_pname(proj.set.fxtype[i == G_FX2P]));
        else
            put_s(lab, GLO_N[i]);
        cv_text(10, vc(&FONT_B, y, 16), &FONT_B, lab, on ? C_WHITE : C_GRAY);
        glo_text(i, v);
        text_r(230, vc(&FONT_B, y, 16), &FONT_B, v, on ? C_HI : C_GRAY);
    }
    if (NGLO > rows) {                                  /* where the list is: a thin bar at the right edge */
        int h = rows * 133 / NGLO, y0 = 2 + ui.gtop * (133 - h) / (NGLO - rows);
        cv_rect(238, 2, 1, 133, C_LINE);
        cv_rect(237, y0, 3, h, C_GRAY);
    }
}

/* the keyboard as the FM-1 has it, small: sixteen white keys, eleven black ones above (after which white key each sits),
 * the keys you hold filled in the track's colour */
static void draw_keyboard(int x0, int y0, int latched, int root)
{
    static const uint8_t AFTER[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};
    int i;
    uint16_t tc = tcol(ui.trk);
    for (i = 0; i < 16; i++) {
        int held = ui.held_note[WHITE_K[i]] != 0 || WHITE_K[i] == latched;
        box(x0 + i * 15, y0, 14, 56, held ? tc : RGB(215, 215, 220));
        if (WHITE_K[i] == root)
            dot(x0 + i * 15 + 7, y0 + 48, 3, held ? C_BLACK : RGB(120, 120, 130));
    }
    for (i = 0; i < 11; i++) {
        int held = ui.held_note[BLACK_K[i]] != 0 || BLACK_K[i] == latched;
        box(x0 + (AFTER[i] + 1) * 15 - 5, y0 - 2, 9, 32, held ? tc : RGB(40, 42, 50));
        if (!held)
            frame(x0 + (AFTER[i] + 1) * 15 - 5, y0 - 2, 9, 32, RGB(90, 92, 100));
    }
}

static void draw_seqcfg(void)
{
    const clip_t *c = cur_clip();
    int i;
    char v[16];
    for (i = 0; i < 4; i++) {
        int y = 4 + i * 28;
        seqcfg_text(c, i, v);
        box(0, y, 240, 26, ui.touched == i && (int32_t)(ui.touch_until - plat_ms()) > 0 ? RGB(40, 40, 48) : RGB(24, 24, 30));
        cv_text(10, vc(&FONT_S, y, 26), &FONT_S, SEQ_L[i], C_GRAY);
        text_r(230, vc(&FONT_M, y, 26), &FONT_M, v, C_HI);
    }
    text_c(120, vc(&FONT_XS, 120, 14), &FONT_XS, "LET GO OF SEQ TO GO BACK", C_GRAY);
}

static void draw_main(void)
{
    char b[16];
    cv_begin(240, 2 * BAND_H, C_BLACK);
    switch (ui.view) {
    case V_SEQ: if (ui.cfg) draw_seqcfg(); else draw_seq(); break;
    case V_PARAM: draw_param(); draw_scatter(); break;
    case V_HOME: draw_home(); break;
    case V_GLO: draw_glo(); break;
    case V_COMMON: draw_common(); break;
    default: {                                          /* ARP / KEYS / REC: the keyboard plays the track */
        int lo = 53 + 12 * ui.oct, steady = ui.view == V_ARP && ARP_ON(ui.trk) && !dk_playing;
        char *q;
        int trsp = ui.view == V_ARP && proj.set.arp_mode[ui.trk] == ARP_TRSP;
        put_s(b, ui.view == V_REC ? "REC" : trsp ? "TRANSPOSE" : proj.set.arp_mode[ui.trk] ? "ARP" : "KEYS");
        text_c(120, vc(&FONT_B, 4, 20), &FONT_B, b, ui.view == V_REC ? RGB(255, 90, 80) : C_HI);
        draw_keyboard(0, 34, trsp ? 60 + dk_trans[ui.trk] - lo : -1, trsp ? 60 - lo : -1);
        if (trsp) {                                                      /* the live transposition, in semitones */
            q = put_s(b, "TRSP ");
            if (dk_trans[ui.trk] > 0)
                *q++ = '+';
            put_i(q, dk_trans[ui.trk]);
            text_c(120, vc(&FONT_M, 98, 26), &FONT_M, b, dk_trans[ui.trk] ? C_AMB : C_GRAY);
        } else {
            q = put_s(b, "OCT ");
            if (ui.oct > 0)
                *q++ = '+';
            put_i(q, ui.oct);
            text_c(120, vc(&FONT_M, 98, 26), &FONT_M, b, ui.oct ? C_WHITE : C_GRAY);
        }
        if (trsp)
            text_c(120, vc(&FONT_XS, 124, 12), &FONT_XS, "MIDDLE C (THE DOT) = NO TRANSPOSITION", C_GRAY);
        else if (steady)
            text_c(120, vc(&FONT_XS, 124, 12), &FONT_XS, "THE ARPEGGIATOR RUNS WITH PLAY", C_AMB);
        else {
            char r[16], *p2 = put_note(r, lo);
            p2 = put_s(p2, " - ");
            put_note(p2, lo + 26);
            text_c(120, vc(&FONT_XS, 124, 12), &FONT_XS, r, C_GRAY);
        }
        break;
    }
    }
    if (ui.save_mode) {                                  /* the SAVE pick stays up until something is picked */
        rbox(6, 26, 228, 86, 8, C_HI);                   /* one solid green panel with rounded corners */
        text_c(120, vc(&FONT_B, 34, 20), &FONT_B, "SAVE", C_BLACK);
        text_c(120, vc(&FONT_S, 58, 16), &FONT_S, "PICK A TRACK OR GLO", C_BLACK);
        text_c(120, vc(&FONT_S, 80, 16), &FONT_S, "SAVE AGAIN = SAVE ALL", C_BLACK);
        text_c(120, vc(&FONT_XS, 98, 12), &FONT_XS, "ANY OTHER KEY CANCELS", RGB(10, 70, 40));
    }
    cv_commit_rect(1, 0, MAIN_Y);

}

static void draw_knobs(void)
{
    int i;
    const clip_t *c = cur_clip();
    const tribe_eng_t *e = eng_info(c->eng);
    int editing = ui.view == V_SEQ && ui.edit >= 0, arpv = ui.view == V_ARP, cov = ui.view == V_COMMON, scfg = ui.cfg;
    cv_begin(240, KNOB_H, C_BLACK);
    {   /* where you are: the tag and the page dots at the right of the rule */
        const char *tag = ui.view == V_GLO ? "GLO" : ui.view == V_REC ? "REC" : scfg ? "SEQUENCER" : ui.view == V_SEQ && !editing ? "TAP A STEP" : cov ? "COMMON" : arpv ? "ARP" : editing ? "STEP" : ui.view == V_HOME ? "MIX" : GRP_N[ui.grp];
        int np = editing || arpv || cov || ui.view == V_SEQ || ui.view == V_HOME || ui.view == V_GLO || ui.view == V_REC ? 1 : npages(ui.grp), dots = np > 1;
        int32_t xd = 237 - (dots ? np * 5 : 0), xt = xd - (dots ? 5 : 0) - tw(&FONT_XS, tag);
        {   /* the battery, at the left end of the rule: three bars; amber at one, red and blinking when it is empty; USB: plugged in */
            int bat = plat_battery(), bars = bat == 4 ? 3 : bat < 0 ? 0 : bat;
            uint16_t bc = bat == 4 ? C_HI : bat < 0 ? C_GRAY : bars >= 2 ? C_HI : bars == 1 ? C_AMB : ((ui.frame >> 4) & 1u) ? RGB(255, 90, 80) : C_GRAY;
            cv_rect(2, 1, 18, 1, C_GRAY); cv_rect(2, 9, 18, 1, C_GRAY); cv_rect(2, 1, 1, 9, C_GRAY); cv_rect(19, 1, 1, 9, C_GRAY);
            cv_rect(20, 4, 2, 3, C_GRAY);
            for (i = 0; i < bars; i++)
                cv_rect(4 + i * 5, 3, 4, 5, bc);
            if (bat == 4)
                cv_text(26, vc(&FONT_XS, 0, 11), &FONT_XS, "USB", C_GRAY);
            cv_rect(bat == 4 ? 50 : 28, 5, xt - 5 - (bat == 4 ? 50 : 28), 1, C_LINE);
        }
        cv_text(xt, vc(&FONT_XS, 0, 11), &FONT_XS, tag, C_HI);
        for (i = 0; dots && i < np; i++)
            cv_rect(xd + i * 5, 4, 3, 3, i == ui.page[ui.grp] ? C_HI : C_LINE);
    }
    for (i = 0; i < 4; i++) {
        int cx = i * 60 + 30, touched = ui.touched == i && (int32_t)(ui.touch_until - plat_ms()) > 0;
        const char *lab = 0;
        char val[24];
        float u = 0.0f;
        int bip = 0;
        val[0] = 0;
        if (ui.view == V_REC)
            continue;                                      /* REC: the keyboard is the instrument, no knobs to show */
        if (ui.view == V_GLO) {                            /* GLO: KNOB 1 scrolls the list, KNOB 2 edits the row; the others are unused */
            if (i == 0) {
                lab = "Scroll";
                put_s(put_i(put_s(put_i(val, ui.glo + 1), "/"), NGLO), "");
                u = (float)ui.glo / (float)(NGLO - 1);
            } else if (i == 1) {
                lab = "Edit";
                glo_text(ui.glo, val);
                u = 0.5f;
            } else
                continue;
            goto draw_one;
        }
        if (scfg) {
            lab = SEQ_L[i];
            seqcfg_text(c, i, val);
            u = seqcfg_u(c, i);
        } else if (ui.view == V_SEQ && !editing) {                 /* the step's controls, waiting for a step */
            static const char *const DW[4] = {"NOTE", "VEL", "PROB", ""}, *const SW[4] = {"NOTE", "VEL", "LEN", "SLIDE"};
            lab = (e->kind == EK_SYNTH ? SW : DW)[i];
            if (!lab[0])
                continue;
            put_s(val, "-");
            u = 0.0f;
        } else if (cov) {
            static const char *const CN[4] = {"Volume", "Send 1", "Send 2", "Engine"};
            int t = ui.trk;
            lab = CN[i];
            if (i == 3) { put_s(val, eng_info(c->eng)->name); u = (float)c->eng / (float)(eng_count() - 1); }
            else if (i == 0) { put_s(put_i(val, proj.set.level[t] * 100 / 127), "%"); u = (float)proj.set.level[t] / 127.0f; }
            else { put_s(put_i(val, proj.set.send[i - 1][t] * 100 / 127), "%"); u = (float)proj.set.send[i - 1][t] / 127.0f; }
        } else if (arpv) {
            static const char *const AN[4] = {"MODE", "RATE", "GATE", "OCT"};
            int t = ui.trk;
            lab = AN[i];
            if (i == 0) { put_s(val, ARP_N[proj.set.arp_mode[t]]); u = (float)proj.set.arp_mode[t] / (float)ARP_TRSP; }
            else if (i == 1) { put_s(val, ARP_R[proj.set.arp_rate[t]]); u = (float)proj.set.arp_rate[t] / 2.0f; }
            else if (i == 2) { char *q = put_i(val, proj.set.arp_gate[t]); put_s(q, "%"); u = (float)proj.set.arp_gate[t] / 100.0f; }
            else { put_i(val, proj.set.arp_oct[t]); u = (float)proj.set.arp_oct[t] / 3.0f; }
        } else if (editing) {
            static const char *const DN[4] = {"NOTE", "VEL", "PROB", ""};
            static const char *const SN[4] = {"NOTE", "VEL", "LEN", "SLIDE"};
            const dstep_t *s = &c->st[ui.edit];
            lab = (e->kind == EK_SYNTH ? SN : DN)[i];
            if (!lab[0])
                continue;
            if (i == 0) { put_note(val, s->note); u = (float)s->note / 127.0f; }
            else if (i == 1) { put_i(val, s->vel); u = (float)s->vel / 127.0f; }
            else if (e->kind == EK_SYNTH && i == 2) { put_i(val, s->len ? s->len : 3); u = (float)(s->len ? s->len : 3) / 32.0f; }
            else if (i == 2) { put_i(val, s->prob); u = (float)s->prob / 100.0f; }
            else { put_i(val, s->slide); u = (float)s->slide / 16.0f; }
        } else {
            int pi = param_ix(i);
            if (pi < 0)
                continue;
            if (pi >= PIX_LFO && pi < PIX_LFO + NLFO) {
                lab = LFO_L[pi - PIX_LFO];
                lfo_text(c, pi - PIX_LFO, val);
                u = lfo_u(c, pi - PIX_LFO);
                goto draw_one;
            }
            if (pi == PIX_LEVEL) {
                lab = "Level";
                put_i(val, proj.set.level[ui.trk]);
                u = (float)proj.set.level[ui.trk] / 127.0f;
                goto draw_one;
            }
            lab = eng_param(c->eng, pi)->name;
            eng_format(c->eng, pi, c->par[pi], val);
            u = (float)c->par[pi] / (float)(eng_param(c->eng, pi)->max ? eng_param(c->eng, pi)->max : 1);
        }
    draw_one:
        {
        int grey = (arpv && i > 0 && !ARP_ON(ui.trk)) || (ui.view == V_SEQ && !editing && !scfg);       /* KEYS: the three arpeggiator controls do nothing */
        if (i)
            cv_rect(i * 60, 14, 1, 60, C_LINE);
        text_c(cx, vc(&FONT_XS, 13, 10), &FONT_XS, lab, grey ? C_LINE : touched ? C_WHITE : C_GRAY);
        arc(cx, 42, 18, u, C_LINE, grey ? C_DIM : tcol(ui.trk), bip);
        {
            const felucca_font_t *vf = tw(&FONT_B, val) > 58 ? &FONT_XS : &FONT_B;     /* a long name in the smaller face */
            text_c(cx, vc(vf, 62, 14), vf, val, grey ? C_DIM : touched ? C_WHITE : C_HI);
        }
        }
    }
    if (!editing && !arpv && !cov && ui.view != V_HOME && ui.view != V_SEQ && !eng_nparams(c->eng) && ui.grp != PG_EDIT)
        text_c(120, vc(&FONT_S, 30, 20), &FONT_S, "NO ENGINE ON THIS CLIP", C_GRAY);
    cv_commit(2, 0, KNOB_Y);
}

/* ------------------------------------------------------------------ lights --- */
static void leds(void)
{
    uint32_t b = 0, bd = 0, k = 0, kd = 0, kf = 0;
    int g, t, i, blink = (ui.frame >> 4) & 1;
    const clip_t *c = cur_clip();
    for (g = 0; g < NPG; g++) {
        if (ui.view == V_PARAM && ui.grp == g && group_ok(g))
            b |= 1u << GRP_BTN[g];
        else if (group_ok(g))
            bd |= 1u << GRP_BTN[g];
    }
    if (dk_playing)
        b |= 1u << B_PLAY;
    if (ui.view == V_SEQ) b |= 1u << B_SEQ;
    if (ui.view == V_HOME) b |= 1u << B_HOME;
    if (ui.view == V_GLO) b |= 1u << B_GLO;
    if (ui.view == V_COMMON)
        b |= 1u << B_SEL;
    else
        bd |= 1u << B_SEL;                               /* SEL: the common page (volume, sends, engine); dim = it is there */
    if (ui.view == V_ARP) b |= 1u << B_ARP;
    if (ui.view == V_REC) b |= 1u << B_REC;
    plat_play_red(ui.view == V_REC);
    if (ui.view == V_ARP || ui.view == V_REC) {
        for (i = 0; i < NKEYS; i++)
            if (ui.held_note[i])
                k |= 1u << i;
    } else {
        for (t = 0; t < NTRK; t++) {
            uint32_t m = 1u << BLACK_K[t];
            if (t == ui.trk && !(dk_muted[t] && blink))
                k |= m;
            else if (!dk_muted[t] && clip_has(&proj.clip[t][dk_clip[t]]))
                kd |= m;
        }
        if (ui.kprev & (1u << BLACK_K[10]))
            k |= 1u << BLACK_K[10];
        else
            kd |= 1u << BLACK_K[10];
        if (ui.view == V_SEQ) {
            int len = LEN_OF(c);
            for (i = 0; i < 16; i++) {
                int s = ui.spage * 16 + i;
                uint32_t m = 1u << WHITE_K[i];
                if (s >= len)
                    continue;
                if (step_on(c, s))
                    k |= m;
                if (dk_playing && dk_pos[ui.trk] == s) {
                    if (step_on(c, s))
                        k &= ~m;
                    else
                        kd |= m;
                }
            }
        } else if ((ui.view == V_PARAM || ui.view == V_GLO || ui.view == V_COMMON) && dk_playing) {
            for (i = 0; i < 16; i++) {
                uint32_t m = 1u << WHITE_K[i];
                if (i < 8 ? dk_scat[ui.trk] == i + 1 : dk_scat_g == i - 7)
                    k |= m;
                else if (!(i & 3))
                    k |= m;                                 /* the 1st, 5th, 9th, 13th: full, as landmarks among the faint keys */
                else
                    kf |= m;
            }
        } else if (ui.view == V_HOME) {
            for (i = 0; i < NCLIP; i++) {
                uint32_t m = 1u << WHITE_K[i];
                if (dk_clip[ui.trk] == i || (dk_queued[ui.trk] == i && blink))
                    k |= m;
                else if (clip_has(&proj.clip[ui.trk][i]))
                    kd |= m;
            }
        }
    }
    if (ui.view == V_ARP || ui.view == V_REC) {          /* the octave of the keyboard: both dim = usable; the one you moved to is full */
        bd |= (1u << B_OCTDN) | (1u << B_OCTUP);
        if (ui.oct > 0)
            b |= 1u << B_OCTUP;
        else if (ui.oct < 0)
            b |= 1u << B_OCTDN;
    } else if ((ui.view == V_SEQ && nsteps_page() > 1) || (ui.view == V_PARAM && npages(ui.grp) > 1))
        bd |= (1u << B_OCTDN) | (1u << B_OCTUP);          /* OCT- / OCT+ page through the steps or the parameters */
    if (store_dirty())
        bd |= 1u << B_SAVE;                               /* SAVE glows: something is not saved */
    if (ui.save_mode) {          /* SAVE: only what can be saved blinks (the tracks whose clip changed, GLO); SAVE at half = everything */
        b = bd = 0;
        k = kd = kf = 0;
        bd |= 1u << B_SAVE;
        if (blink && store_set_dirty())
            b |= 1u << B_GLO;
        for (t = 0; t < NTRK; t++)
            if (blink && trk_dirty(t))
                k |= 1u << BLACK_K[t];
    }
    plat_glow(0);
    plat_buttons_dim(bd & ~b);
    plat_keys_dim(kd & ~k);
    plat_keys_faint(kf & ~k & ~kd);
    plat_leds(b, k);
}

/* ------------------------------------------------------------------- entry --- */
void ui_init(void)
{
    int t;
    palette_set(0);
    corners_init();
    ui.trk = 0;
    for (t = 0; t < NTRK; t++)
        trk_grp[t] = PG_EDIT;
    ui.edit = -1;
    ui.press = -1;
    ui.pot_ref = -1;
    {   /* what the MASTER knob does is not saved: it starts where the pot is */
        int pos = (int)(fm_sqrtf((float)plat_master() * 256.0f) * (1.0f / 8.0f));
        knob_put(0, pos > 127 ? 127 : pos);
    }
    ui.grp = PG_EDIT;
    fix_group();
}

void ui_input_only(void) { input(); }

void ui_frame(void)
{
    int t;
    input();
    store_poll();
    if ((ui.frame & 127u) == 127u)
        dk_load_peak = 0;                                 /* the CPU peak is of the last ~2 s */
    for (t = 0; t < NTRK; t++) {
        dk_peak[t] = (uint16_t)(dk_peak[t] - (dk_peak[t] >> 3));
        dk_hit[t] = (uint8_t)(dk_hit[t] - (dk_hit[t] >> 3) - (dk_hit[t] ? 1 : 0));
    }
    cpu_sample();
    draw_header();
    draw_main();
    draw_knobs();
    draw_cpu_bar();
    leds();
    ui.frame++;
}

int ui_dirty(void) { return 0; }
void ui_say(const char *a, const char *b) { say(a, b); }
