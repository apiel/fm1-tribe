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

enum { V_SEQ, V_PARAM, V_HOME, V_SCATTER, V_GLO, V_ARP, V_REC, NVIEWS };
static const char *const VIEW_N[NVIEWS] = {"SEQ", "PARAM", "CLIPS", "SCATTER", "GLOBAL", "ARP", "REC"};
static const char *const GRP_N[NPG] = {"FX", "ENV", "LFO", "EDIT"};
static const uint8_t GRP_BTN[NPG] = {B_FX, B_ENV, B_LFO, B_EDIT};
#define NGLO 5
static const char *const GLO_N[NGLO] = {"TEMPO", "ENGINE", "LEVEL", "MASTER", "PROJECT"};

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
    uint8_t glo;                         /* GLO: the item */
    uint8_t mdjf, mcomp;                 /* the master knobs' values */
    uint32_t bprev, kprev, used;
    uint8_t held_note[NKEYS];            /* live note of a held key + 1 */
    uint8_t held_trk[NKEYS];
    char msg[24];
    uint32_t msg_until, frame;
    int8_t touched;                      /* the knob last turned, for its highlight */
    uint32_t touch_until;
} ui;

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

static int group_ok(int g) { return eng_pages(cur_clip()->eng, g) > 0; }
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
static void select_track(int t)
{
    ui.trk = (uint8_t)t;
    ui.edit = -1;
    ui.press = -1;
    if (ui.spage >= nsteps_page())
        ui.spage = 0;
    fix_group();
}

/* ------------------------------------------------------------------ input --- */
static int param_ix(int slot) { return eng_param_at(cur_clip()->eng, ui.grp, ui.page[ui.grp], slot); }

static void turn_param(int slot, int d)
{
    int i = param_ix(slot), v;
    const x0x_param_t *p;
    clip_t *c = cur_clip();
    if (i < 0)
        return;
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

static void turn_master(int which, int d)
{
    int v;
    char b[16];
    if (which == 0) {                                   /* SELECT: the DJ filter, 64 = off */
        v = ui.mdjf + d * 2;
        ui.mdjf = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        dk_master_set(13, ui.mdjf);
        say(ui.mdjf < 62 ? "LOW PASS " : ui.mdjf > 66 ? "HIGH PASS " : "FILTER OFF", 0);
        if (ui.mdjf < 62 || ui.mdjf > 66)
            put_i(ui.msg + (ui.mdjf < 62 ? 9 : 10), ui.mdjf < 62 ? ui.mdjf * 100 / 62 : (ui.mdjf - 66) * 100 / 61);
    } else {                                            /* PRESETS: the one-knob compressor */
        v = ui.mcomp + d * 2;
        ui.mcomp = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        dk_master_set(12, ui.mcomp);
        put_s(b, "COMP ");
        put_i(b + 5, ui.mcomp * 100 / 127);
        say(b, 0);
    }
}

static void write_step(int t, int s, int note, int vel)
{
    clip_t *c = &proj.clip[t][dk_clip[t]];
    c->on[s >> 5] |= 1u << (s & 31);
    c->st[s].note = (uint8_t)note;
    c->st[s].vel = (uint8_t)vel;
}

static void key_note(int k, int down)
{
    int note = 53 + k + 12 * ui.oct;
    if (note < 0)
        note = 0;
    if (note > 127)
        note = 127;
    if (down) {
        ui.held_note[k] = (uint8_t)(note + 1);
        ui.held_trk[k] = ui.trk;
        dk_note(ui.trk, note, 100);
        if (ui.view == V_REC && dk_playing) {
            clip_t *c = cur_clip();
            int p = dk_pos[ui.trk] % LEN_OF(c);
            write_step(ui.trk, p, note, 100);
        }
    } else if (ui.held_note[k]) {
        dk_note(ui.held_trk[k], ui.held_note[k] - 1, 0);
        ui.held_note[k] = 0;
    }
}

static void key_down(int k)
{
    int w = WHITE_OF[k], b = BLACK_OF[k];
    uint32_t now = plat_ms();
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
        dk_launch(ui.trk, w, dk_queued[ui.trk] == w);   /* again on the queued clip: now */
        break;
    case V_PARAM:
    case V_GLO:
        dk_note(ui.trk, 48 + w, 100);                   /* the white keys audition the track */
        ui.held_note[WHITE_K[w]] = (uint8_t)(48 + w + 1);
        ui.held_trk[WHITE_K[w]] = ui.trk;
        break;
    default:
        break;
    }
}

static void key_up(int k)
{
    int w = WHITE_OF[k];
    if (ui.held_note[k]) {
        dk_note(ui.held_trk[k], ui.held_note[k] - 1, 0);
        ui.held_note[k] = 0;
    }
    if (ui.view == V_SEQ && w >= 0 && ui.press == ui.spage * 16 + w) {   /* a short tap on an "on" step: off */
        step_flip(cur_clip(), ui.press);
        if (ui.edit == ui.press)
            ui.edit = -1;
        ui.press = -1;
    }
}

static void button_down(int b)
{
    int g;
    switch (b) {
    case B_PLAY:
        dk_play(!dk_playing);
        break;
    case B_SEQ:
        ui.view = V_SEQ;
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
            ui.page[g] = (uint8_t)((ui.page[g] + 1) % eng_pages(cur_clip()->eng, g));
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
        ui.view = ui.view == V_HOME ? V_SCATTER : V_HOME;
        break;
    case B_GLO:
        ui.view = V_GLO;
        break;
    case B_SAVE:
        say("SAVE: SOON", 0);
        break;
    case B_SEL:
        say("SEL: SOON", 0);
        break;
    case B_OCTDN: case B_OCTUP: {
        int d = b == B_OCTUP ? 1 : -1;
        if (ui.view == V_ARP || ui.view == V_REC) {
            ui.oct = (int8_t)(ui.oct + d < -2 ? -2 : ui.oct + d > 2 ? 2 : ui.oct + d);
        } else if (ui.view == V_PARAM) {
            int n = eng_pages(cur_clip()->eng, ui.grp);
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
        if (ui.view == V_GLO) {
            if (i == 0)
                ui.glo = (uint8_t)((ui.glo + NGLO + (d > 0 ? 1 : -1)) % NGLO);
            else if (i == 1) {
                clip_t *c = cur_clip();
                if (ui.glo == 0) {
                    int v = proj.set.bpm_x10 + d * 10;
                    dk_bpm(v < 300 ? 300 : v > 3000 ? 3000 : v);
                } else if (ui.glo == 1) {
                    int e = (c->eng + eng_count() + d) % eng_count();
                    dk_engine(ui.trk, e);
                    fix_group();
                } else if (ui.glo == 2) {
                    int v = proj.set.level[ui.trk] + d * 2;
                    proj.set.level[ui.trk] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
                }
            }
        } else if (ui.view == V_SEQ && ui.edit >= 0)
            turn_step(i, d);
        else
            turn_param(i, d);
    }
    m = (uint32_t)plat_enc(EN_SELECT);
    if ((int32_t)m)
        turn_master(0, (int32_t)m);
    m = (uint32_t)plat_enc(EN_PRESET);
    if ((int32_t)m)
        turn_master(1, (int32_t)m);
    (void)plat_enc(EN_ALGO);
}

/* ---------------------------------------------------------------- drawing --- */
static void put_num(char *b, int v) { put_i(b, v); }

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
        if (eng_pages(cur_clip()->eng, ui.grp) > 1) {
            char *q = put_s(b + 0, GRP_N[ui.grp]);
            *q++ = ' ';
            q = put_i(q, ui.page[ui.grp] + 1);
            *q++ = '/';
            put_i(q, eng_pages(cur_clip()->eng, ui.grp));
        }
        x = cv_text(x, vc(&FONT_B, 1, 17), &FONT_B, b, C_HI) + 6;
    } else
        x = cv_text(x, vc(&FONT_B, 1, 17), &FONT_B, VIEW_N[ui.view], C_HI) + 6;
    b[0] = 'C';
    put_i(b + 1, dk_clip[ui.trk] + 1);
    if (dk_queued[ui.trk] != 0xFF) {
        char *q = put_s(b + 0, "C");
        q = put_i(q, dk_clip[ui.trk] + 1);
        *q++ = '>';
        put_i(q, dk_queued[ui.trk] + 1);
    }
    cv_text(150, vc(&FONT_B, 1, 17), &FONT_B, b, C_WHITE);
    put_i(b, proj.set.bpm_x10 / 10);
    text_r(222, vc(&FONT_B, 1, 17), &FONT_B, b, C_AMB);
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
        b[0] = 'c';
        put_i(b + 1, dk_clip[t] + 1);
        text_c(x + 12, vc(&FONT_XS, 122, 11), &FONT_XS, b, dk_queued[t] != 0xFF ? C_AMB : C_DIM);
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
    for (i = 0; i < n; i++) {
        int g = eng_group(c->eng, i), k;
        const x0x_param_t *p;
        if (g != ui.grp)
            continue;
        p = eng_param(c->eng, i);
        k = row / 4 == ui.page[ui.grp];
        cv_text(10 + (row & 1 ? 120 : 0), 44 + (row / 2 % 2) * 0 + (row >> 1) * 15, &FONT_S, p->name, k ? C_WHITE : C_GRAY);
        eng_format(c->eng, i, c->par[i], v);
        text_r(110 + (row & 1 ? 120 : 0), 44 + (row >> 1) * 15, &FONT_S, v, k ? C_HI : C_DIM);
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

static void draw_text_screen(const char *a, const char *b2, uint16_t c)
{
    text_c(120, vc(&FONT_L, 20, 40), &FONT_B, a, c);
    text_c(120, vc(&FONT_S, 70, 20), &FONT_S, b2, C_GRAY);
}

static void draw_glo(void)
{
    int i;
    char v[24];
    const clip_t *c = cur_clip();
    for (i = 0; i < NGLO; i++) {
        int y = 6 + i * 22, on = ui.glo == i;
        if (on)
            box(0, y - 2, 240, 20, RGB(40, 40, 48));
        cv_text(10, vc(&FONT_B, y, 16), &FONT_B, GLO_N[i], on ? C_WHITE : C_GRAY);
        v[0] = 0;
        if (i == 0) {
            char *q = put_i(v, proj.set.bpm_x10 / 10);
            put_s(q, ".");
            put_i(q + 1, proj.set.bpm_x10 % 10);
        } else if (i == 1)
            put_s(v, eng_info(c->eng)->name);
        else if (i == 2)
            put_i(v, proj.set.level[ui.trk]);
        else if (i == 3)
            put_s(v, "KNOBS: SOON");
        else
            put_s(v, "SAVE LOAD: SOON");
        text_r(230, vc(&FONT_B, y, 16), &FONT_B, v, on ? C_HI : C_DIM);
    }
}

static void draw_main(void)
{
    char b[16];
    cv_begin(240, 2 * BAND_H, C_BLACK);
    switch (ui.view) {
    case V_SEQ: draw_seq(); break;
    case V_PARAM: draw_param(); break;
    case V_HOME: draw_home(); break;
    case V_SCATTER: draw_text_screen("SCATTER FX", "TAP A KEY (SOON)", C_HI); break;
    case V_GLO: draw_glo(); break;
    default:
        put_s(b, ui.view == V_REC ? "REC" : "ARP");
        draw_text_screen(b, "OCT -/+  KEYBOARD PLAYS THE TRACK", ui.view == V_REC ? RGB(255, 90, 80) : C_HI);
        put_s(b, "OCT ");
        put_i(b + 4, ui.oct);
        text_c(120, vc(&FONT_M, 100, 30), &FONT_M, b, C_WHITE);
        break;
    }
    cv_commit_rect(1, 0, MAIN_Y);
}

static void draw_knobs(void)
{
    int i;
    const clip_t *c = cur_clip();
    const tribe_eng_t *e = eng_info(c->eng);
    int editing = ui.view == V_SEQ && ui.edit >= 0;
    cv_begin(240, KNOB_H, C_BLACK);
    {   /* where you are: the tag and the page dots at the right of the rule */
        const char *tag = editing ? (e->kind == EK_SYNTH ? "STEP" : "STEP") : GRP_N[ui.grp];
        int np = editing ? 1 : eng_pages(c->eng, ui.grp), dots = np > 1;
        int32_t xd = 237 - (dots ? np * 5 : 0), xt = xd - (dots ? 5 : 0) - tw(&FONT_XS, tag);
        cv_rect(0, 5, xt - 5, 1, C_LINE);
        cv_text(xt, vc(&FONT_XS, 0, 11), &FONT_XS, tag, C_HI);
        for (i = 0; dots && i < np; i++)
            cv_rect(xd + i * 5, 4, 3, 3, i == ui.page[ui.grp] ? C_HI : C_LINE);
    }
    for (i = 0; i < 4; i++) {
        int cx = i * 60 + 30, touched = ui.touched == i && (int32_t)(ui.touch_until - plat_ms()) > 0;
        const char *lab = 0;
        char val[12];
        float u = 0.0f;
        int bip = 0;
        val[0] = 0;
        if (editing) {
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
            lab = eng_param(c->eng, pi)->name;
            eng_format(c->eng, pi, c->par[pi], val);
            u = (float)c->par[pi] / (float)(eng_param(c->eng, pi)->max ? eng_param(c->eng, pi)->max : 1);
        }
        if (i)
            cv_rect(i * 60, 14, 1, 60, C_LINE);
        text_c(cx, vc(&FONT_XS, 13, 10), &FONT_XS, lab, touched ? C_WHITE : C_GRAY);
        arc(cx, 42, 18, u, C_LINE, tcol(ui.trk), bip);
        text_c(cx, vc(&FONT_B, 62, 14), &FONT_B, val, touched ? C_WHITE : C_HI);
    }
    if (!editing && !eng_nparams(c->eng))
        text_c(120, vc(&FONT_S, 30, 20), &FONT_S, "NO ENGINE ON THIS CLIP", C_GRAY);
    cv_commit(2, 0, KNOB_Y);
}

/* ------------------------------------------------------------------ lights --- */
static void leds(void)
{
    uint32_t b = 0, bd = 0, k = 0, kd = 0;
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
    if (ui.view == V_HOME || ui.view == V_SCATTER) b |= 1u << B_HOME;
    if (ui.view == V_GLO) b |= 1u << B_GLO;
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
    plat_glow(0);
    plat_buttons_dim(bd & ~b);
    plat_keys_dim(kd & ~k);
    plat_leds(b, k);
}

/* ------------------------------------------------------------------- entry --- */
void ui_init(void)
{
    palette_set(0);
    corners_init();
    ui.trk = 0;
    ui.edit = -1;
    ui.press = -1;
    ui.mdjf = 64;
    ui.mcomp = 0;
    ui.grp = PG_EDIT;
    fix_group();
}

void ui_input_only(void) { input(); }

void ui_frame(void)
{
    int t;
    input();
    for (t = 0; t < NTRK; t++) {
        dk_peak[t] = (uint16_t)(dk_peak[t] - (dk_peak[t] >> 3));
        dk_hit[t] = (uint8_t)(dk_hit[t] - (dk_hit[t] >> 3) - (dk_hit[t] ? 1 : 0));
    }
    draw_header();
    draw_main();
    draw_knobs();
    leds();
    ui.frame++;
}

int ui_dirty(void) { return 0; }
