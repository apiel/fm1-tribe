/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE on the host: the whole app (UI, project, engine, sequencer, every DSP part) run against a
 * simulated FM-1 driven by a script. Audio goes to a WAV, the screen to PNGs, the lights to a
 * text dump. The same file, with TRIBE_WEB, is the browser emulator (web/emu/tribe_web.c).
 *
 *   tribe_host SCRIPT [OUTDIR]
 *
 * Script, one command per line (# comments):
 *   wait MS                      run the device for MS milliseconds
 *   press BTN | release BTN | tap BTN     (BTN: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+)
 *   key K down|up | tapkey K     (K: 0..26 = F3..G5, or w0..w15 white keys, b0..b10 black keys)
 *   holdkey K MS                 press, run MS, release
 *   turn ENC N                   N detents, signed (ENC: SELECT ALGO PRESET K1 K2 K3 K4)
 *   master N                     MASTER pot 0..4096
 *   wav FILE | wavstop           start / stop recording the output
 *   shot FILE.png                save the screen
 *   leds                         print the lit (#) and dim (o) buttons and keys
 *   expect WHAT VALUE            check state (playing view trk clip queued pos eng bound muted edit grp page spage bpm; stepon N / stepoff N): exit 1 on mismatch
 *   peakreset | expect peak_db_min/max VALUE    the output's peak since
 */
#define X0X_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
#endif
#define __attribute__(x)

/* ------------------------------------------------------------ screen --- */
static uint16_t fb[240 * 240];          /* RGB565, byte-swapped as the panel takes it (gfx.c) */
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sc = (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8));
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = sc;
}
static uint32_t blits;
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    blits++;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = px[j * w + i];
}
#include "../firmware/src/gfx.c"

/* ---------------------------------------------------------- platform --- */
#include "../firmware/src/app/plat.h"

static uint32_t now_ms, held_btn, held_keys, master = 2048;
static int32_t enc_acc[NE];
static uint32_t lit_btn, lit_keys, dim_keys, dim_btn;

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return held_btn; }
uint32_t plat_keys(void) { return held_keys; }
int32_t plat_enc(int role)
{
    int32_t v = enc_acc[role];
    enc_acc[role] = 0;
    return v;
}
uint32_t plat_master(void) { return master; }
int plat_battery(void) { return 3; }
void plat_leds(uint32_t b, uint32_t k)
{
    lit_btn = b;
    lit_keys = k;
}
void plat_glow(int on) { (void)on; }
void plat_buttons_dim(uint32_t b) { dim_btn = b; }
void plat_play_red(int red) { (void)red; }
void plat_keys_dim(uint32_t k) { dim_keys = k; }
int plat_midi_in(uint32_t *pkt) { (void)pkt; return 0; }
void plat_midi_out(uint32_t pkt) { (void)pkt; }

static uint8_t store[OBJ_NOBJ][PLAT_STORE_MAX];
static int store_len[OBJ_NOBJ];
static uint32_t store_writes;
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    int n = obj < OBJ_NOBJ ? store_len[obj] : 0;
    if (n <= 0)
        return -1;
    memcpy(dst, store[obj], (uint32_t)n > max ? max : (uint32_t)n);
    return n;
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    if (obj >= OBJ_NOBJ || len > PLAT_STORE_MAX)
        return -1;
    memcpy(store[obj], src, len);
    store_len[obj] = (int)len;
    store_writes++;
    return 0;
}
int plat_slot_zones(int k) { (void)k; return 0; }
const uint8_t *plat_slot(int k, int z, uint32_t *n, uint32_t *rate, char name[9]) { (void)k; (void)z; (void)n; (void)rate; (void)name; return 0; }
uint32_t plat_cpu_pct(void) { return 0; }
uint32_t plat_xruns(void) { return 0; }

/* -------------------------------------------------------------- app --- */
#include "../firmware/src/tribe/tribe.h"
#include "../firmware/src/tribe/draw.c"
#include "../firmware/src/tribe/ui.c"


/* ---------------------------------------------------------- outputs --- */
static void png_write(const char *path)
{
    /* RGB PNG with stored (uncompressed) deflate blocks: no zlib needed */
    static uint8_t raw[240 * (1 + 240 * 3)];
    FILE *f = fopen(path, "wb");
    uint32_t crc_t[256], i, j, k, n = 0, a = 1, b = 0, pos, len = sizeof raw;
    uint8_t hdr[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_t[i] = c;
    }
    for (j = 0; j < 240; j++) {
        raw[n++] = 0;
        for (i = 0; i < 240; i++) {
            uint16_t p = fb[j * 240 + i];
            p = (uint16_t)((p >> 8) | (p << 8));
            raw[n++] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
            raw[n++] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
            raw[n++] = (uint8_t)((p & 31) * 255 / 31);
        }
    }
    for (i = 0; i < len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
#define PUT32(v) do { uint8_t q_[4] = {(uint8_t)((v) >> 24), (uint8_t)((v) >> 16), (uint8_t)((v) >> 8), (uint8_t)(v)}; fwrite(q_, 1, 4, f); } while (0)
    fwrite(hdr, 1, 8, f);
    {
        uint8_t ih[17] = {'I', 'H', 'D', 'R', 0, 0, 0, 240, 0, 0, 0, 240, 8, 2, 0, 0, 0};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(13u);
        fwrite(ih, 1, 17, f);
        for (i = 0; i < 17; i++)
            c = crc_t[(c ^ ih[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    {
        uint32_t nblk = (len + 65534) / 65535, zlen = 2 + len + nblk * 5 + 4, c = 0xFFFFFFFFu;
        uint8_t *z = malloc(zlen + 4), *q = z;
        memcpy(q, "IDAT", 4);
        q += 4;
        *q++ = 0x78;
        *q++ = 0x01;
        for (pos = 0; pos < len;) {
            uint32_t m = len - pos > 65535 ? 65535 : len - pos;
            *q++ = pos + m >= len ? 1 : 0;
            *q++ = (uint8_t)m;
            *q++ = (uint8_t)(m >> 8);
            *q++ = (uint8_t)~m;
            *q++ = (uint8_t)(~m >> 8);
            memcpy(q, raw + pos, m);
            q += m;
            pos += m;
        }
        *q++ = (uint8_t)(b >> 8);
        *q++ = (uint8_t)b;
        *q++ = (uint8_t)(a >> 8);
        *q++ = (uint8_t)a;
        PUT32(zlen);
        fwrite(z, 1, zlen + 4, f);
        for (i = 0; i < zlen + 4; i++)
            c = crc_t[(c ^ z[i]) & 255] ^ (c >> 8);
        PUT32(~c);
        free(z);
    }
    {
        uint8_t ie[4] = {'I', 'E', 'N', 'D'};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(0u);
        fwrite(ie, 1, 4, f);
        for (i = 0; i < 4; i++)
            c = crc_t[(c ^ ie[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    fclose(f);
}

static FILE *wav;
static uint32_t wav_frames;
static float peak_out;
static void wav_open(const char *path)
{
    static const uint8_t h[44] = {0};
    wav = fopen(path, "wb");
    if (wav)
        fwrite(h, 1, 44, wav);
    wav_frames = 0;
}
static void wav_close(void)
{
    uint32_t bytes = wav_frames * 4, v;
    if (!wav)
        return;
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav);
    v = 36 + bytes;
    fwrite(&v, 4, 1, wav);
    fwrite("WAVEfmt ", 1, 8, wav);
    v = 16;
    fwrite(&v, 4, 1, wav);
    {
        uint16_t fmt[2] = {1, 2};
        uint32_t r[2] = {44100, 44100 * 4};
        uint16_t al[2] = {4, 16};
        fwrite(fmt, 2, 2, wav);
        fwrite(r, 4, 2, wav);
        fwrite(al, 2, 2, wav);
    }
    fwrite("data", 1, 4, wav);
    fwrite(&bytes, 4, 1, wav);
    fclose(wav);
    wav = 0;
}

/* --------------------------------------------------------- the device --- */
static double audio_due_ms;
static uint32_t ui_due;

static void boot(void)
{
    project_defaults();
    engine_init();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui_init();
}

static void run_ms(uint32_t ms)
{
    static int32_t blk[512];
    uint32_t i;
    for (i = 0; i < ms; i++) {
        now_ms++;
        while (audio_due_ms <= (double)now_ms) {      /* 256-frame half buffers, as the I2S DMA */
            uint32_t k;
            engine_render(blk, 256);
#ifdef TRIBE_WEB
            web_audio(blk, 256);                         /* the browser build: to the AudioWorklet */
#endif
            for (k = 0; k < 256; k++) {
                float l = (float)blk[2 * k] / 8388608.0f, r = (float)blk[2 * k + 1] / 8388608.0f;
                int16_t s[2];
                if (fabsf(l) > peak_out)
                    peak_out = fabsf(l);
                if (fabsf(r) > peak_out)
                    peak_out = fabsf(r);
                s[0] = (int16_t)(l * 32767.0f);
                s[1] = (int16_t)(r * 32767.0f);
                if (wav) {
                    fwrite(s, 2, 2, wav);
                    wav_frames++;
                }
            }
            audio_due_ms += 256.0 * 1000.0 / 44100.0;
        }
        if (now_ms >= ui_due) {
            ui_due = now_ms + 16;
            ui_frame();
        } else {
            ui_input_only();
        }
    }
}

#ifndef TRIBE_WEB
/* ---------------------------------------------------------- script --- */
static const char *const BTN_N[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                      "PLAY", "REC", "OCT-", "OCT+"};
static const char *const ENC_N[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};
static int find_name(const char *const *t, int n, const char *s)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(t[i], s))
            return i;
    return -1;
}
static int key_of(const char *s)
{
    if (s[0] == 'w')
        return WHITE_K[atoi(s + 1) & 15];
    if (s[0] == 'b')
        return BLACK_K[atoi(s + 1) % 11];
    return atoi(s);
}

static int expect(const char *what, const char *val, int line)
{
    long got = -1, want;
    int t = ui.trk;
    char op = 0;
    if (*val == '<' || *val == '>')
        op = *val++;
    want = atol(val);
    if (!strcmp(what, "playing")) got = dk_playing;
    else if (!strcmp(what, "view")) got = ui.view;
    else if (!strcmp(what, "trk")) got = ui.trk;
    else if (!strcmp(what, "clip")) got = dk_clip[t];
    else if (!strcmp(what, "queued")) got = dk_queued[t];
    else if (!strcmp(what, "pos")) got = dk_pos[t];
    else if (!strcmp(what, "eng")) got = proj.clip[t][dk_clip[t]].eng;
    else if (!strcmp(what, "bound")) got = dk_bound[t];
    else if (!strcmp(what, "muted")) got = dk_muted[t];
    else if (!strcmp(what, "edit")) got = ui.edit;
    else if (!strcmp(what, "grp")) got = ui.grp;
    else if (!strcmp(what, "page")) got = ui.page[ui.grp];
    else if (!strcmp(what, "spage")) got = ui.spage;
    else if (!strcmp(what, "bpm")) got = proj.set.bpm_x10;
    else if (!strcmp(what, "stepon")) got = step_on(&proj.clip[t][dk_clip[t]], atoi(val)) ? want : -1;     /* expect stepon N */
    else if (!strcmp(what, "stepoff")) got = step_on(&proj.clip[t][dk_clip[t]], atoi(val)) ? -1 : want;
    else if (!strcmp(what, "peak_db_min") || !strcmp(what, "peak_db_max")) {
        got = (long)(peak_out > 0.0f ? 20.0 * log10((double)peak_out) : -200.0);
        if (!strcmp(what, "peak_db_min")) { op = '>'; } else { op = '<'; }
    } else {
        fprintf(stderr, "line %d: unknown expect %s\n", line, what);
        return 1;
    }
    if ((op == '<' && got < want) || (op == '>' && got > want) || (!op && got == want))
        return 0;
    fprintf(stderr, "line %d: expect %s %s: got %ld\n", line, what, val, got);
    return 1;
}

int main(int argc, char **argv)
{
    FILE *sc;
    char buf[256], outdir[200] = ".", path[400];
    int line = 0, fails = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: tribe_host SCRIPT [OUTDIR]\n");
        return 2;
    }
    if (argc > 2)
        snprintf(outdir, sizeof outdir, "%s", argv[2]);
    sc = fopen(argv[1], "r");
    if (!sc) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    boot();
    while (fgets(buf, sizeof buf, sc)) {
        char *a[6] = {0}, *p = buf;
        int n = 0, i;
        line++;
        if (strchr(buf, '#'))
            *strchr(buf, '#') = 0;
        for (p = strtok(buf, " \t\r\n"); p && n < 6; p = strtok(0, " \t\r\n"))
            a[n++] = p;
        if (!n)
            continue;
        if (!strcmp(a[0], "wait"))
            run_ms((uint32_t)atoi(a[1]));
        else if (!strcmp(a[0], "press") || !strcmp(a[0], "release") || !strcmp(a[0], "tap")) {
            i = find_name(BTN_N, NB, a[1]);
            if (i < 0) { fprintf(stderr, "line %d: button %s\n", line, a[1]); fails++; continue; }
            if (a[0][0] != 'r') { held_btn |= 1u << i; }
            if (a[0][0] == 't') { run_ms(60); }
            if (a[0][0] != 'p') { held_btn &= ~(1u << i); run_ms(40); } else run_ms(20);
        } else if (!strcmp(a[0], "key")) {
            i = key_of(a[1]);
            if (!strcmp(a[2], "down")) held_keys |= 1u << i; else held_keys &= ~(1u << i);
            run_ms(20);
        } else if (!strcmp(a[0], "tapkey") || !strcmp(a[0], "holdkey")) {
            i = key_of(a[1]);
            held_keys |= 1u << i;
            run_ms(a[0][0] == 'h' ? (uint32_t)atoi(a[2]) : 60);
            held_keys &= ~(1u << i);
            run_ms(40);
        } else if (!strcmp(a[0], "turn")) {
            i = find_name(ENC_N, NE, a[1]);
            if (i < 0) { fprintf(stderr, "line %d: enc %s\n", line, a[1]); fails++; continue; }
            enc_acc[i] += atoi(a[2]);
            run_ms(40);
        } else if (!strcmp(a[0], "master"))
            master = (uint32_t)atoi(a[1]);
        else if (!strcmp(a[0], "wav")) {
            snprintf(path, sizeof path, "%s/%s", outdir, a[1]);
            wav_open(path);
        } else if (!strcmp(a[0], "wavstop"))
            wav_close();
        else if (!strcmp(a[0], "peakreset"))
            peak_out = 0.0f;
        else if (!strcmp(a[0], "shot")) {
            run_ms(40);
            snprintf(path, sizeof path, "%s/%s", outdir, a[1]);
            png_write(path);
        } else if (!strcmp(a[0], "leds")) {
            int k;
            printf("buttons:");
            for (k = 0; k < NB; k++)
                if ((lit_btn >> k) & 1u) printf(" %s=#", BTN_N[k]); else if ((dim_btn >> k) & 1u) printf(" %s=o", BTN_N[k]);
            printf("\nkeys   :");
            for (k = 0; k < NKEYS; k++)
                putchar((lit_keys >> k) & 1u ? '#' : (dim_keys >> k) & 1u ? 'o' : '.');
            putchar('\n');
        } else if (!strcmp(a[0], "expect"))
            fails += expect(a[1], a[2], line);
        else {
            fprintf(stderr, "line %d: unknown command %s\n", line, a[0]);
            fails++;
        }
    }
    wav_close();
    fclose(sc);
    if (fails)
        fprintf(stderr, "%d expectation(s) failed\n", fails);
    return fails ? 1 : 0;
}
#endif
