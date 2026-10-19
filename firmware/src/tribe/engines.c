/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE engine registry: every sound a track can have, behind one set of functions.
 *
 * The 909 and the 808 are kits: one shared instance, a track takes one voice of it (two tracks
 * cannot hold the same voice; the second one stays silent). The ACID, the LEAD and the kicks are instances,
 * a few of them, handed to tracks as they ask. Everything here runs in the audio ISR except the
 * descriptors (names, pages), which are constant. */
#include "mem.h"
#include "tribe.h"
#include "../dsp/drum909.h"
#include "../dsp/drum808.h"
#include "../dsp/acid.h"
#include "../dsp/fm4.h"
#include "../dsp/noise.h"
#include "../dsp/tribe_syn.h"
#include "../dsp/lead.h"
#include "../dsp/fastmath.h"
#include "kicks.h"

#ifndef POOLED
#define POOLED X0X_POOL
#endif

/* the table: none, the kicks, the 909 voices, the 808 voices, ACID (the zicCycle 303), LEAD */
#define E(n, k, f, v, c) { n, k, f, v, c }
static const tribe_eng_t ENG[] = {
    E("NONE", EK_DRUM, F_NONE, 0, 0),
    E("KICK W", EK_DRUM, F_KICKW, 0, 4),
    E("KICK P", EK_DRUM, F_KICKP, 0, 3),
    E("909 BD", EK_DRUM, F_909, DR_BD, 1), E("909 SD", EK_DRUM, F_909, DR_SD, 1),
    E("909 LT", EK_DRUM, F_909, DR_LT, 1), E("909 MT", EK_DRUM, F_909, DR_MT, 1),
    E("909 HT", EK_DRUM, F_909, DR_HT, 1), E("909 RS", EK_DRUM, F_909, DR_RS, 1),
    E("909 CP", EK_DRUM, F_909, DR_CP, 1), E("909 CH", EK_DRUM, F_909, DR_CH, 1),
    E("909 OH", EK_DRUM, F_909, DR_OH, 1), E("909 CR", EK_DRUM, F_909, DR_CR, 1),
    E("909 RD", EK_DRUM, F_909, DR_RD, 1),
    E("808 BD", EK_DRUM, F_808, D8_BD, 1), E("808 SD", EK_DRUM, F_808, D8_SD, 1),
    E("808 LT", EK_DRUM, F_808, D8_LT, 1), E("808 MT", EK_DRUM, F_808, D8_MT, 1),
    E("808 HT", EK_DRUM, F_808, D8_HT, 1), E("808 RS", EK_DRUM, F_808, D8_RS, 1),
    E("808 CP", EK_DRUM, F_808, D8_CP, 1), E("808 CB", EK_DRUM, F_808, D8_CB, 1),
    E("808 CY", EK_DRUM, F_808, D8_CY, 1), E("808 OH", EK_DRUM, F_808, D8_OH, 1),
    E("808 CH", EK_DRUM, F_808, D8_CH, 1),
    E("ACID", EK_SYNTH, F_ACID, 0, 4),
    E("LEAD", EK_SYNTH, F_LEAD, 0, 8),                     /* long legato leads: a unison of detuned oscillators */                     /* appended: projects store engines by index */
    E("FM4", EK_SYNTH, F_FM4, 0, 5),                       /* a mono 4-operator FM synth (Digitone-style operators) */
    E("NOISE", EK_SYNTH, F_NOISE, 0, 6),                   /* noise (white, pink, brown or dust) through a drive and a filter, crushed, with a comb that rings at the note */
    E("TRIBE", EK_SYNTH, F_TRIBE, 0, 5),                   /* a wavetable synth: a morphed, skewed, folded cycle through a filter, one effect from a list, the envelope on the filter and the fold */
};
#define NENG ((int)(sizeof ENG / sizeof ENG[0]))

int eng_count(void) { return NENG; }
const tribe_eng_t *eng_info(int id) { return &ENG[id >= 0 && id < NENG ? id : 0]; }

/* ---------------------------------------------------------- descriptors --- */
static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'a' && *a <= 'z' ? (char)(*a - 32) : *a, y = *b >= 'a' && *b <= 'z' ? (char)(*b - 32) : *b;
        if (x != y)
            return 0;
    }
    return !*a && !*b;
}
static int in(const char *n, const char *const *l)
{
    for (; *l; l++)
        if (ieq(n, *l))
            return 1;
    return 0;
}

int eng_nparams(int id)
{
    const tribe_eng_t *e = eng_info(id);
    int n = 0;
    switch (e->fam) {
    case F_909: n = drum909_nparams(e->voice); break;
    case F_808: n = drum808_nparams(e->voice); break;
    case F_ACID: n = acid_nparams(); break;
    case F_LEAD: n = lead_nparams(); break;
    case F_FM4: n = fm4_nparams(); break;
    case F_NOISE: n = noise_nparams(); break;
    case F_TRIBE: n = tsyn_nparams(); break;
    case F_KICKW: case F_KICKP: n = kick_nparams(e->fam == F_KICKP); break;
    }
    return n > NPAR ? NPAR : n;
}

const x0x_param_t *eng_param(int id, int i)
{
    const tribe_eng_t *e = eng_info(id);
    switch (e->fam) {
    case F_909: return drum909_param(e->voice, i);
    case F_808: return drum808_param(e->voice, i);
    case F_ACID: return acid_param(i);
    case F_LEAD: return lead_param(i);
    case F_FM4: return fm4_param(i);
    case F_NOISE: return noise_param(i);
    case F_TRIBE: return tsyn_param(i);
    case F_KICKW: case F_KICKP: return kick_param(e->fam == F_KICKP, i);
    }
    return 0;
}

/* which page a pot lives on: by its name, until an engine says otherwise */
int eng_group(int id, int i)
{
    static const char *const hide[] = {"Rev", "Dly", "Pan", "Accent", "Veloc", "Volume", "Level", "Click", "ClkDec", 0};
    static const char *const fx[] = {"Drive", "Dist", "DrvTyp", "Crush", "CrshRt", "Comb", "CombFb", "Fold", "Wfold", "Reson", "Boost", "Ring", "RingFq", "FxTyp", "FxPar", 0};
    static const char *const env[] = {"Attack", "Decay", "P.Dpth", "Pitch", "AccDec", "EnvMod", "EnvWav", "Slide", "Tail",
                                      "PchShp", "SwpShp", "SwpDpt", "Curve", "FMSnap", "Dur", "Sustain", "Release", 0};
    static const char *const lfo[] = {"LfoRt", "LfoDpt", "LfoShp", "LfoTgt", 0};
    const x0x_param_t *p = eng_param(id, i);
    const tribe_eng_t *e = eng_info(id);
    if (!p)
        return PG_HIDE;
    if (e->fam == F_FM4)                                   /* by place, as the Digitone's pages: FM, the envelopes, then the filter and overdrive */
        return i < FM4_ENV_START ? PG_EDIT : i < FM4_FX_START ? PG_ENV : PG_FX;
    if (in(p->name, hide) && !(e->fam == F_ACID && ieq(p->name, "Accent")))
        return PG_HIDE;
    if (in(p->name, fx))
        return PG_FX;
    if (in(p->name, env))
        return PG_ENV;
    if (in(p->name, lfo))
        return PG_LFO;
    return PG_EDIT;
}

int eng_default(int id, int i)
{
    const x0x_param_t *p = eng_param(id, i);
    return p ? p->def : 0;
}

int eng_param_at(int id, int group, int page, int slot)
{
    int n = eng_nparams(id), i, k = page * 4 + slot;
    for (i = 0; i < n; i++)
        if (eng_group(id, i) == group && k-- == 0)
            return i;
    return -1;
}

int eng_pages(int id, int group)
{
    int n = eng_nparams(id), i, c = 0;
    for (i = 0; i < n; i++)
        c += eng_group(id, i) == group;
    return (c + 3) / 4;
}

void eng_format(int id, int i, int value, char *buf)
{
    const x0x_param_t *p = eng_param(id, i);
    int k = 0;
    if (eng_info(id)->fam == F_KICKW || eng_info(id)->fam == F_KICKP) {
        kick_format(eng_info(id)->fam == F_KICKP, i, value, buf);
        return;
    }
    if (p && p->names && value >= 0 && value <= p->max) {
        const char *s = p->names[value];
        while (*s && k < 7)
            buf[k++] = *s++;
    } else {
        if (value >= 100)
            buf[k++] = (char)('0' + value / 100);
        if (value >= 10)
            buf[k++] = (char)('0' + value / 10 % 10);
        buf[k++] = (char)('0' + value % 10);
    }
    buf[k] = 0;
}

/* ------------------------------------------------------------ instances --- */
#define NACID 2
#define NLEAD 2
#define NFM4 4
#define NNOISE 2
#define NTSYN 2
#define NKICK 3
static drum909_t d909;
static drum808_t d808;
static acid_t bacid[NACID];
static lead_t blead[NLEAD];
static fm4_t bfm4[NFM4];
static noise_t bnoise[NNOISE];
static tsyn_t btsyn[NTSYN];
static float scratch_rev[256], scratch_dly[256];
static uint8_t bound_eng[NTRK];              /* engine id a track holds (0 = none) */
static int8_t inst[NTRK];                    /* instance index of an ACID / LEAD / kick, -1 */
static uint8_t inst_used[6][4];              /* [0] kicks, [1] ACIDs, [2] LEADs, [3] FM4s, [4] NOISEs, [5] TRIBEs */
static uint8_t voice_owner[2][12];           /* kits: which track + 1 has a voice */
static uint8_t base_tune[NTRK];
static int8_t tune_ix[NTRK];
static uint8_t slide_pending[NTRK];          /* the last synth note slides into the next */
static uint8_t gate_open[NTRK];

/* the keys held on a track, in the order pressed: a synth plays the last one, and when that one is let go it
 * goes back to the one before, legato (a slide, no new attack) */
#define NHELD 8
static struct { uint8_t note, vel; } held[NTRK][NHELD];
static uint8_t nheld[NTRK];

static int kit_of(int fam) { return fam == F_808; }

void eng_init(void)
{
    int t;
    drum909_init(&d909);
    drum808_init(&d808);
    for (t = 0; t < NACID; t++)
        acid_init(&bacid[t]);
    for (t = 0; t < NLEAD; t++)
        lead_init(&blead[t]);
    for (t = 0; t < NFM4; t++)
        fm4_init(&bfm4[t]);
    for (t = 0; t < NNOISE; t++)
        noise_init(&bnoise[t]);
    for (t = 0; t < NTSYN; t++)
        tsyn_init(&btsyn[t]);
    kicks_init();
    memset(bound_eng, 0, sizeof bound_eng);
    memset(inst_used, 0, sizeof inst_used);
    memset(voice_owner, 0, sizeof voice_owner);
    for (t = 0; t < NTRK; t++) {
        inst[t] = -1;
        tune_ix[t] = -1;
        nheld[t] = 0;
    }
}

void eng_unbind(int t)
{
    const tribe_eng_t *e = eng_info(bound_eng[t]);
    if (e->fam == F_909 || e->fam == F_808) {
        int k = kit_of(e->fam);
        if (voice_owner[k][e->voice] == t + 1) {
            voice_owner[k][e->voice] = 0;
            if (k)
                d808.vout[e->voice] = 0;
            else
                d909.vout[e->voice] = 0;
        }
    } else if (inst[t] >= 0) {
        if (e->fam == F_ACID) {
            acid_all_off(&bacid[inst[t]]);
            inst_used[1][inst[t]] = 0;
        } else if (e->fam == F_LEAD) {
            lead_all_off(&blead[inst[t]]);
            inst_used[2][inst[t]] = 0;
        } else if (e->fam == F_FM4) {
            fm4_all_off(&bfm4[inst[t]]);
            inst_used[3][inst[t]] = 0;
        } else if (e->fam == F_NOISE) {
            noise_all_off(&bnoise[inst[t]]);
            inst_used[4][inst[t]] = 0;
        } else if (e->fam == F_TRIBE) {
            tsyn_all_off(&btsyn[inst[t]]);
            inst_used[5][inst[t]] = 0;
        } else if (e->fam == F_KICKW || e->fam == F_KICKP) {
            kick_free(inst[t]);
            inst_used[0][inst[t]] = 0;
        }
    }
    inst[t] = -1;
    bound_eng[t] = 0;
    nheld[t] = 0;
    tune_ix[t] = -1;
    gate_open[t] = 0;
    slide_pending[t] = 0;
}

/* returns 0 when bound, -1 when the voice or the instances are taken */
int eng_bind(int t, int id)
{
    const tribe_eng_t *e = eng_info(id);
    int i, n;
    eng_unbind(t);
    switch (e->fam) {
    case F_909: case F_808: {
        int k = kit_of(e->fam);
        if (voice_owner[k][e->voice])
            return -1;
        voice_owner[k][e->voice] = (uint8_t)(t + 1);
        break;
    }
    case F_ACID:
        for (i = 0; i < NACID && inst_used[1][i]; i++) ;
        if (i == NACID)
            return -1;
        inst_used[1][i] = 1;
        inst[t] = (int8_t)i;
        break;
    case F_LEAD:
        for (i = 0; i < NLEAD && inst_used[2][i]; i++) ;
        if (i == NLEAD)
            return -1;
        inst_used[2][i] = 1;
        inst[t] = (int8_t)i;
        break;
    case F_FM4:
        for (i = 0; i < NFM4 && inst_used[3][i]; i++) ;
        if (i == NFM4)
            return -1;
        inst_used[3][i] = 1;
        inst[t] = (int8_t)i;
        break;
    case F_NOISE:
        for (i = 0; i < NNOISE && inst_used[4][i]; i++) ;
        if (i == NNOISE)
            return -1;
        inst_used[4][i] = 1;
        inst[t] = (int8_t)i;
        break;
    case F_TRIBE:
        for (i = 0; i < NTSYN && inst_used[5][i]; i++) ;
        if (i == NTSYN)
            return -1;
        inst_used[5][i] = 1;
        inst[t] = (int8_t)i;
        break;
    case F_KICKW: case F_KICKP:
        i = kick_alloc(e->fam == F_KICKP);
        if (i < 0)
            return -1;
        inst_used[0][i] = 1;
        inst[t] = (int8_t)i;
        break;
    default:
        return 0;
    }
    bound_eng[t] = (uint8_t)id;
    n = eng_nparams(id);
    for (i = 0; i < n; i++)
        if (e->fam != F_ACID && e->fam != F_LEAD && e->fam != F_NOISE && e->fam != F_TRIBE && eng_param(id, i) && ieq(eng_param(id, i)->name, "Tune"))
            tune_ix[t] = (int8_t)i;
    return 0;
}

int eng_bound(int t) { return bound_eng[t]; }
int eng_slide_pending(int t) { return slide_pending[t]; }

void eng_set(int t, int i, int v)
{
    const tribe_eng_t *e = eng_info(bound_eng[t]);
    if (i == tune_ix[t])
        base_tune[t] = (uint8_t)v;
    switch (e->fam) {
    case F_909: drum909_set(&d909, e->voice, i, v); break;
    case F_808: drum808_set(&d808, e->voice, i, v); break;
    case F_ACID: if (inst[t] >= 0) acid_set(&bacid[inst[t]], i, v); break;
    case F_LEAD: if (inst[t] >= 0) lead_set(&blead[inst[t]], i, v); break;
    case F_FM4: if (inst[t] >= 0) fm4_set(&bfm4[inst[t]], i, v); break;
    case F_NOISE: if (inst[t] >= 0) noise_set(&bnoise[inst[t]], i, v); break;
    case F_TRIBE: if (inst[t] >= 0) tsyn_set(&btsyn[inst[t]], i, v); break;
    case F_KICKW: case F_KICKP: if (inst[t] >= 0) kick_set(inst[t], i, v); break;
    }
}

/* the note's pitch on a kit voice: its TUNE pot moves 1.5 units a semitone from C4 */
static void kit_pitch(const tribe_eng_t *e, int t, int note)
{
    int v = base_tune[t] + (int)((float)(note - 60) * 1.5f);
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    if (e->fam == F_909)
        drum909_set(&d909, e->voice, tune_ix[t], v);
    else
        drum808_set(&d808, e->voice, tune_ix[t], v);
}

volatile uint32_t eng_notes[NTRK];

void eng_note(int t, int note, int vel, int slide)
{
    eng_notes[t]++;
    const tribe_eng_t *e = eng_info(bound_eng[t]);
    float v = (float)vel / 127.0f;
    switch (e->fam) {
    case F_909:
        if (voice_owner[0][e->voice] != t + 1)
            return;
        if (tune_ix[t] >= 0)
            kit_pitch(e, t, note);
        drum909_trigger(&d909, e->voice, v);
        break;
    case F_808:
        if (voice_owner[1][e->voice] != t + 1)
            return;
        if (tune_ix[t] >= 0)
            kit_pitch(e, t, note);
        drum808_trigger(&d808, e->voice, v);
        break;
    case F_ACID:
        if (inst[t] < 0)
            return;
        acid_note_on(&bacid[inst[t]], note, vel > 100, slide_pending[t]);
        slide_pending[t] = (uint8_t)(slide != 0);
        gate_open[t] = 1;
        break;
    case F_LEAD:
        if (inst[t] < 0)
            return;
        lead_note_on(&blead[inst[t]], note, slide_pending[t]);
        slide_pending[t] = (uint8_t)(slide != 0);
        gate_open[t] = 1;
        break;
    case F_FM4:
        if (inst[t] < 0)
            return;
        fm4_note_on(&bfm4[inst[t]], note, slide_pending[t]);
        slide_pending[t] = (uint8_t)(slide != 0);
        gate_open[t] = 1;
        break;
    case F_NOISE:
        if (inst[t] < 0)
            return;
        noise_note_on(&bnoise[inst[t]], note, slide_pending[t]);
        slide_pending[t] = (uint8_t)(slide != 0);
        gate_open[t] = 1;
        break;
    case F_TRIBE:
        if (inst[t] < 0)
            return;
        tsyn_note_on(&btsyn[inst[t]], note, slide_pending[t]);
        slide_pending[t] = (uint8_t)(slide != 0);
        gate_open[t] = 1;
        break;
    case F_KICKW: case F_KICKP:
        if (inst[t] >= 0)
            kick_trigger(inst[t], note, v);
        break;
    }
}

/* takes the key at i out of the stack, the ones after it move down */
static void held_drop(int t, int i)
{
    for (; i + 1 < nheld[t]; i++)
        held[t][i] = held[t][i + 1];
    nheld[t]--;
}

void eng_key_on(int t, int note, int vel)
{
    if (nheld[t] == NHELD)                         /* full: the oldest is forgotten */
        held_drop(t, 0);
    held[t][nheld[t]].note = (uint8_t)note;
    held[t][nheld[t]].vel = (uint8_t)vel;
    nheld[t]++;
    slide_pending[t] = 0;                          /* a key pressed: a new attack (LEAD still glides over a held note, always legato) */
    eng_note(t, note, vel, 0);
}

void eng_key_off(int t, int note)
{
    int i;
    for (i = nheld[t] - 1; i >= 0 && held[t][i].note != note; i--) ;
    if (i < 0)
        return;
    if (i == nheld[t] - 1 && nheld[t] > 1 && eng_info(bound_eng[t])->kind == EK_SYNTH) {
        /* the sounding note is let go and another is still held: glide back to it, no new attack */
        slide_pending[t] = 1;
        eng_note(t, held[t][nheld[t] - 2].note, held[t][nheld[t] - 2].vel, 1);
    }
    held_drop(t, i);
    if (nheld[t] == 0)
        eng_panic(t);
}

/* the synths: the note off of the instance a track holds */
static int is_synth(const tribe_eng_t *e) { return e->fam == F_ACID || e->fam == F_LEAD || e->fam == F_FM4 || e->fam == F_NOISE || e->fam == F_TRIBE; }
static void synth_note_off(const tribe_eng_t *e, int i)
{
    if (e->fam == F_ACID)
        acid_note_off(&bacid[i]);
    else if (e->fam == F_LEAD)
        lead_note_off(&blead[i]);
    else if (e->fam == F_NOISE)
        noise_note_off(&bnoise[i]);
    else if (e->fam == F_TRIBE)
        tsyn_note_off(&btsyn[i]);
    else
        fm4_note_off(&bfm4[i]);
}

/* the step's gate ends; a sliding synth keeps it open into the next note */
void eng_off(int t)
{
    const tribe_eng_t *e = eng_info(bound_eng[t]);
    if (is_synth(e) && inst[t] >= 0 && gate_open[t] && !slide_pending[t]) {
        synth_note_off(e, inst[t]);
        gate_open[t] = 0;
    }
}

void eng_panic(int t)
{
    const tribe_eng_t *e = eng_info(bound_eng[t]);
    slide_pending[t] = 0;
    if (is_synth(e) && inst[t] >= 0) {
        synth_note_off(e, inst[t]);
        gate_open[t] = 0;
    }
}

/* renders every bound track into buf[t * 256 + off ..]: n mono samples each (overwritten) */
void eng_render_all(float *buf, int off, int n)
{
    int t, k_used[2] = {0, 0};
    for (t = 0; t < NTRK; t++) {
        const tribe_eng_t *e = eng_info(bound_eng[t]);
        float *o = buf + t * 256 + off;
        memset(o, 0, (unsigned)n * sizeof(float));
        if (e->fam == F_909 || e->fam == F_808) {
            int k = kit_of(e->fam);
            k_used[k] = 1;
            if (k)
                d808.vout[e->voice] = o;
            else
                d909.vout[e->voice] = o;
        } else if (e->fam == F_ACID && inst[t] >= 0) {
            acid_render(&bacid[inst[t]], o, n);
        } else if (e->fam == F_LEAD && inst[t] >= 0) {
            lead_render(&blead[inst[t]], o, n);
        } else if (e->fam == F_FM4 && inst[t] >= 0) {
            fm4_render(&bfm4[inst[t]], o, n);
        } else if (e->fam == F_NOISE && inst[t] >= 0) {
            noise_render(&bnoise[inst[t]], o, n);
        } else if (e->fam == F_TRIBE && inst[t] >= 0) {
            tsyn_render(&btsyn[inst[t]], o, n);
        } else if ((e->fam == F_KICKW || e->fam == F_KICKP) && inst[t] >= 0) {
            kick_render(inst[t], o, n);
        }
    }
    if (k_used[0]) {
        memset(scratch_rev, 0, (unsigned)n * sizeof(float));
        memset(scratch_dly, 0, (unsigned)n * sizeof(float));
        {
            static float sink[256];
            memset(sink, 0, sizeof sink);
            drum909_render(&d909, sink, scratch_rev, scratch_dly, n);
        }
    }
    if (k_used[1]) {
        static float sink[256];
        memset(sink, 0, sizeof sink);
        memset(scratch_rev, 0, (unsigned)n * sizeof(float));
        memset(scratch_dly, 0, (unsigned)n * sizeof(float));
        drum808_render(&d808, sink, scratch_rev, scratch_dly, n);
    }
}
