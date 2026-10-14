/* SPDX-License-Identifier: GPL-3.0-only */
/* The drawing toolkit of X0X's ui.c (text, rounded boxes, arcs, change-only blits), moved here
 * unchanged for TRIBE's screens (X0X, GPL-3.0, itself a fork of Felucca). Included by the unity
 * build after gfx.c. */
#pragma once
#include "../dsp/fastmath.h"

/* ============================================================ text helpers === */
static char *put_s(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}
static char *put_i(char *b, int v)
{
    char t[12];
    int n = 0, neg = v < 0;
    uint32_t u = (uint32_t)(neg ? -v : v);
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    if (neg)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static const char *const NOTE_N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static char *put_note(char *b, int n)
{
    b = put_s(b, NOTE_N[(n % 12 + 12) % 12]);
    return put_i(b, n / 12 - 1);
}
static char *put_hex(char *b, uint32_t v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--)
        *b++ = "0123456789ABCDEF"[(v >> (4 * i)) & 15u];
    *b = 0;
    return b;
}


/* ================================================================== drawing === */
#define HDR_H 20
#define MAIN_Y 20
#define BAND_H 69                     /* the main area: two bands of 69 (the knob row takes the 6 below) */
#define KNOB_Y 158
#define KNOB_H 81                     /* the last row (239) is the CPU bar */
#define CPU_Y 239

static uint32_t blit_hash[4];
static uint32_t cv_hash(void)
{
    uint32_t i, h = 2166136261u, n = cv_w * cv_h;
    for (i = 0; i < n; i++)
        h = (h ^ cv_px[i]) * 16777619u;
    return h ^ (cv_w << 16) ^ cv_h;
}
static void cv_commit(int region, uint32_t x, uint32_t y)
{
    uint32_t h = cv_hash();
    if (h != blit_hash[region]) {
        blit_hash[region] = h;
        cv_blit(x, y);
    }
}

/* the main area sends only the rectangle that changed: the panel has no tearing-effect line the
 * FM-1 can see, so a transfer the panel's refresh overtakes shows half old, half new. The whole
 * area is 69 KB, 18 ms at 30 MHz, longer than a refresh, so every playhead step sheared; the two
 * columns a step changes are ~3 ms. Rows and 8-pixel column blocks are hashed against the last
 * frame sent; the changed rectangle is packed to the front of the canvas (it is rebuilt every
 * frame) and sent as one transfer. blit_hash[region] == 0 (a reset) sends it all. */
#define MAIN_ROWS (2 * BAND_H)
#define MAIN_COLB (240 / 8)
static uint32_t main_rowh[MAIN_ROWS], main_colh[MAIN_COLB];
static void cv_commit_rect(int region, uint32_t x, uint32_t y)
{
    uint32_t rowh[MAIN_ROWS], colh[MAIN_COLB], r, c, r0 = MAIN_ROWS, r1 = 0, c0 = MAIN_COLB, c1 = 0, w, h, k;
    for (c = 0; c < MAIN_COLB; c++)
        colh[c] = 2166136261u;
    for (r = 0; r < MAIN_ROWS; r++) {
        const uint16_t *q = cv_px + r * 240u;
        uint32_t hr = 2166136261u;
        for (c = 0; c < MAIN_COLB; c++) {
            uint32_t hc = colh[c];
            for (k = 0; k < 8u; k++) {
                hr = (hr ^ q[k]) * 16777619u;
                hc = (hc ^ q[k]) * 16777619u;
            }
            colh[c] = hc;
            q += 8;
        }
        rowh[r] = hr;
    }
    if (!blit_hash[region]) {                         /* after a reset: all of it */
        r0 = c0 = 0;
        r1 = MAIN_ROWS - 1;
        c1 = MAIN_COLB - 1;
    } else {
        for (r = 0; r < MAIN_ROWS; r++)
            if (rowh[r] != main_rowh[r]) {
                if (r0 == MAIN_ROWS)
                    r0 = r;
                r1 = r;
            }
        for (c = 0; c < MAIN_COLB; c++)
            if (colh[c] != main_colh[c]) {
                if (c0 == MAIN_COLB)
                    c0 = c;
                c1 = c;
            }
        if (r0 == MAIN_ROWS || c0 == MAIN_COLB)
            return;                                     /* nothing changed */
    }
    blit_hash[region] = 1;
    for (r = 0; r < MAIN_ROWS; r++)
        main_rowh[r] = rowh[r];
    for (c = 0; c < MAIN_COLB; c++)
        main_colh[c] = colh[c];
    w = (c1 - c0 + 1u) * 8u;
    h = r1 - r0 + 1u;
    for (r = 0; r < h; r++) {                         /* pack: the destination never passes the source */
        const uint16_t *src = cv_px + (r0 + r) * 240u + c0 * 8u;
        uint16_t *dst = cv_px + r * w;
        for (k = 0; k < w; k++)
            dst[k] = src[k];
    }
    lcd_blit(x + c0 * 8u, y + r0, w, h, cv_px);
}

static uint16_t blend(uint16_t a, uint16_t b, int t)  /* a..b, t of 16 */
{
    int r = ((a >> 11) * (16 - t) + (b >> 11) * t) >> 4, g = (((a >> 5) & 63) * (16 - t) + ((b >> 5) & 63) * t) >> 4,
        bl = ((a & 31) * (16 - t) + (b & 31) * t) >> 4;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}
static uint16_t dim(uint16_t c, int t) { return blend(C_BLACK, c, t); }

static int32_t tw(const felucca_font_t *f, const char *s) { return text_w(f, s); }

/* a font's capital line: the first and last rows of 'H' (rounded half coverage), measured
 * once from the glyph bitmap, so text is centred on what the eye sees rather than on the
 * font's cell (which differs per face and size) */
typedef struct { const felucca_font_t *f; int8_t top, bot; } capm_t;
static capm_t capm[6];
static const capm_t *caps(const felucca_font_t *f)
{
    int i;
    for (i = 0; i < 6 && capm[i].f; i++)
        if (capm[i].f == f)
            return &capm[i];
    if (i == 6)
        i = 5;
    {
        uint32_t gi = ((uint32_t)'H' <= f->last ? 'H' : '0') - f->first, w = f->bw[gi], bpr = (w + 1u) / 2u, x, y;
        const uint8_t *gd = f->data + f->off[gi];
        int top = -1, bot = 0;
        for (y = 0; y < f->h; y++)
            for (x = 0; x < w; x++) {
                uint32_t a = gd[y * bpr + x / 2u];
                a = (x & 1u) ? (a & 15u) : (a >> 4);
                if (a >= 8u) {
                    if (top < 0)
                        top = (int)y;
                    bot = (int)y;
                }
            }
        capm[i].f = f;
        capm[i].top = (int8_t)(top < 0 ? 0 : top);
        capm[i].bot = (int8_t)bot;
    }
    return &capm[i];
}
/* parts of a line ("A 909 GR", "B 909 FL") with a real gap between them: a proportional face's
 * space is too narrow to separate them. A NULL ends the list. */
static int32_t segs(int32_t x, int32_t y, const felucca_font_t *f, uint16_t c, int32_t gap, const char *a,
                    const char *b, const char *d, const char *e)
{
    const char *p[4] = {a, b, d, e};
    int i;
    for (i = 0; i < 4 && p[i]; i++)
        x = cv_text(x, y, f, p[i], c) + gap;
    return x;
}

/* the y to draw f at so its capitals sit centred in [y0, y0 + h) */
static int32_t vc(const felucca_font_t *f, int32_t y0, int32_t h)
{
    const capm_t *m = caps(f);
    int32_t ch = m->bot - m->top + 1;
    return y0 + (h - ch + 1) / 2 - m->top;
}
/* the y to draw g at so its baseline matches f drawn at y */
static int32_t base_y(const felucca_font_t *f, int32_t y, const felucca_font_t *g)
{
    return y + caps(f)->bot - caps(g)->bot;
}
static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - tw(f, s) / 2, y, f, s, c);
}
static void text_r(int32_t rx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(rx - tw(f, s), y, f, s, c);
}

/* ---- anti-aliased shapes. Coverage is 0..16; a pixel is blended over what is there. ---- */
static void px_blend(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint16_t *q;
    uint32_t bg;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1 || !a)
        return;
    q = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    if (a >= 16u) {
        *q = swap16(c);
        return;
    }
    bg = swap16(*q);
    *q = swap16((uint32_t)(((((c >> 11) * a + (bg >> 11) * (16u - a)) >> 4) << 11) |
                           (((((c >> 5) & 63u) * a + ((bg >> 5) & 63u) * (16u - a)) >> 4) << 5) |
                           (((c & 31u) * a + (bg & 31u) * (16u - a)) >> 4)));
}

/* corner coverage of a radius-r quarter circle, pixel (x, y) of the r x r corner square,
 * 4 x 4 supersampled; and of its 1-px ring (for outlines). Built once. */
#define RMAX 8
static uint8_t corner_fill[RMAX + 1][RMAX][RMAX], corner_ring[RMAX + 1][RMAX][RMAX];
static void corners_init(void)
{
    int r, x, y, sx, sy;
    for (r = 1; r <= RMAX; r++)
        for (y = 0; y < r; y++)
            for (x = 0; x < r; x++) {
                int in = 0, ring = 0;
                for (sy = 0; sy < 4; sy++)
                    for (sx = 0; sx < 4; sx++) {
                        float dx = (float)r - ((float)x + (sx + 0.5f) / 4.0f);
                        float dy = (float)r - ((float)y + (sy + 0.5f) / 4.0f);
                        float d2 = dx * dx + dy * dy;
                        if (d2 <= (float)(r * r)) {
                            in++;
                            if (d2 >= (float)((r - 1) * (r - 1)))
                                ring++;
                        }
                    }
                corner_fill[r][y][x] = (uint8_t)in;
                corner_ring[r][y][x] = (uint8_t)ring;
            }
}

/* a filled rounded rectangle; r is clamped to fit */
static void rbox(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    int32_t i, j;
    if (w <= 0 || h <= 0)
        return;
    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;
    if (r > RMAX)
        r = RMAX;
    if (r < 1) {
        cv_rect(x, y, w, h, c);
        return;
    }
    cv_rect(x + r, y, w - 2 * r, h, c);
    cv_rect(x, y + r, r, h - 2 * r, c);
    cv_rect(x + w - r, y + r, r, h - 2 * r, c);
    for (j = 0; j < r; j++)
        for (i = 0; i < r; i++) {
            uint32_t a = corner_fill[r][j][i];
            px_blend(x + i, y + j, c, a);
            px_blend(x + w - 1 - i, y + j, c, a);
            px_blend(x + i, y + h - 1 - j, c, a);
            px_blend(x + w - 1 - i, y + h - 1 - j, c, a);
        }
}

/* a 1-px rounded outline */
static void rframe(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    int32_t i, j;
    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;
    if (r > RMAX)
        r = RMAX;
    if (r < 1)
        r = 1;
    cv_rect(x + r, y, w - 2 * r, 1, c);
    cv_rect(x + r, y + h - 1, w - 2 * r, 1, c);
    cv_rect(x, y + r, 1, h - 2 * r, c);
    cv_rect(x + w - 1, y + r, 1, h - 2 * r, c);
    for (j = 0; j < r; j++)
        for (i = 0; i < r; i++) {
            uint32_t a = corner_ring[r][j][i];
            px_blend(x + i, y + j, c, a);
            px_blend(x + w - 1 - i, y + j, c, a);
            px_blend(x + i, y + h - 1 - j, c, a);
            px_blend(x + w - 1 - i, y + h - 1 - j, c, a);
        }
}

/* the radius that suits a box of height h: soft, never round */
static int32_t rad_of(int32_t h) { return h >= 30 ? 6 : h >= 18 ? 4 : h >= 9 ? 3 : 2; }
static void box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { rbox(x, y, w, h, rad_of(h < w ? h : w), c); }
static void frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { rframe(x, y, w, h, rad_of(h < w ? h : w), c); }

/* an anti-aliased disc of radius r (float) at (cx, cy) */
static void disc(float cx, float cy, float r, uint16_t c)
{
    int32_t x0 = (int32_t)fm_floorf(cx - r - 1.0f), x1 = (int32_t)(cx + r + 1.0f);
    int32_t y0 = (int32_t)fm_floorf(cy - r - 1.0f), y1 = (int32_t)(cy + r + 1.0f), x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy);
            float a = fm_clampf(r - d + 0.5f, 0.0f, 1.0f);
            px_blend(x, y, c, (uint32_t)(a * 16.0f + 0.5f));
        }
}
static void dot(int32_t x, int32_t y, int32_t r, uint16_t c) { disc((float)x + 0.5f, (float)y + 0.5f, (float)r + 0.3f, c); }

/* atan2 in turns, 0..1 from +x going clockwise on screen (y down) */
static float turns(float y, float x)
{
    float ax = fm_fabsf(x), ay = fm_fabsf(y), a = ax < ay ? ax / (ay + 1e-9f) : ay / (ax + 1e-9f), s = a * a, r;
    r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;   /* atan on [0, 1] */
    if (ay > ax)
        r = 1.57079637f - r;
    if (x < 0.0f)
        r = FM_PI - r;
    if (y < 0.0f)
        r = FM_TWO_PI - r;
    return r * (1.0f / FM_TWO_PI);
}

/* a knob: a 270-degree track from 7:30 to 4:30, thickness th, anti-aliased, round ends; the
 * value filled from the start (or from the top, for a bipolar value) and a round handle */
static void arc(int32_t cx, int32_t cy, int32_t r, float v, uint16_t track, uint16_t fill, int bipolar)
{
    const float th = 3.4f, a0 = 0.375f, sweep = 0.75f;  /* start at 135 degrees, in turns */
    float ro = (float)r, ri = ro - th, rm = ro - th * 0.5f, fx = (float)cx + 0.5f, fy = (float)cy + 0.5f;
    float lo = bipolar ? (v < 0.5f ? v : 0.5f) : 0.0f, hi = bipolar ? (v < 0.5f ? 0.5f : v) : v;
    int32_t x, y;
    v = fm_clampf(v, 0.0f, 1.0f);
    for (y = -r - 1; y <= r + 1; y++)
        for (x = -r - 1; x <= r + 1; x++) {
            float dx = (float)x + 0.5f - 0.5f, dy = (float)y + 0.5f - 0.5f, d = fm_sqrtf(dx * dx + dy * dy), t, a;
            a = fm_clampf(ro + 0.5f - d, 0.0f, 1.0f) * fm_clampf(d - ri + 0.5f, 0.0f, 1.0f);
            if (a <= 0.0f)
                continue;
            t = turns(dy, dx) - a0;
            if (t < 0.0f)
                t += 1.0f;
            t /= sweep;
            if (t > 1.0f)
                continue;                              /* the gap at the bottom; the caps close it */
            px_blend(cx + x, cy + y, (t >= lo && t <= hi) ? fill : track, (uint32_t)(a * 16.0f + 0.5f));
        }
    {   /* round ends: the track's, then the fill's, then the handle */
        float e0 = (a0) * FM_TWO_PI, e1 = (a0 + sweep) * FM_TWO_PI, ev = (a0 + sweep * v) * FM_TWO_PI;
        float el = (a0 + sweep * lo) * FM_TWO_PI;
        disc(fx + fm_cosf(e0) * rm - 0.5f, fy + fm_sinf(e0) * rm - 0.5f, th * 0.5f, lo <= 0.0f && hi > 0.0f ? fill : track);
        disc(fx + fm_cosf(e1) * rm - 0.5f, fy + fm_sinf(e1) * rm - 0.5f, th * 0.5f, hi >= 1.0f ? fill : track);
        if (hi > lo)
            disc(fx + fm_cosf(el) * rm - 0.5f, fy + fm_sinf(el) * rm - 0.5f, th * 0.5f, fill);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, th * 0.5f + 1.3f, C_BLACK);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, th * 0.5f + 0.6f, C_WHITE);
    }
}

static void play_icon(int32_t x, int32_t y, uint16_t c)
{
    int32_t i, j;
    for (j = 0; j < 11; j++)                           /* a triangle, edges anti-aliased */
        for (i = 0; i < 9; i++) {
            float fy = (float)j + 0.5f - 5.5f, fx = (float)i + 0.5f;
            float edge = 8.0f - fx - fm_fabsf(fy) * 1.3f;
            float a = fm_clampf(edge * 0.7f + 0.5f, 0.0f, 1.0f) * fm_clampf(fx + 0.5f, 0.0f, 1.0f);
            px_blend(x + i, y + j, c, (uint32_t)(a * 16.0f + 0.5f));
        }
}

