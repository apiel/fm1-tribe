/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE's start-up screen, in the look of a free party flyer: hazard tape top and bottom, the name big and solid in one acid colour,
 * then the ten squares (the tracks, each in its own colour) that light up one after another, and a line saying what it is.
 * Drawn with the UI's canvas, so the same code runs on the FM-1 and in the emulator.
 * Included by the unity build after draw.c and ui.c. */

/* one string, magnified k times (nearest neighbour: big square pixels, the look of the font), at (x, y) */
static void splash_big(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c, int k)
{
    int32_t w = text_w(f, s), h = f->h, ox, oy;
    uint16_t *dst;
    cv_begin((uint32_t)w, (uint32_t)h, C_BLACK);
    cv_text(0, 0, f, s, c);
    dst = cv_px + (uint32_t)(w * h);                    /* the magnified picture after the small one, in the same canvas */
    for (oy = 0; oy < h * k; oy++)
        for (ox = 0; ox < w * k; ox++)
            dst[oy * w * k + ox] = cv_px[(oy / k) * w + ox / k];
    lcd_blit((uint32_t)x, (uint32_t)y, (uint32_t)(w * k), (uint32_t)(h * k), dst);
    lcd_sync();
}

/* the ten squares: the first `lit` filled in their track colour, the rest an outline */
static void splash_squares(int lit)
{
    int t;
    cv_begin(240, 18, C_BLACK);
    for (t = 0; t < NTRK; t++) {
        int x = 13 + t * 22;
        if (t < lit)
            cv_rect(x, 1, 16, 16, TRK_COL[t]);
        else {
            cv_rect(x, 1, 16, 1, C_DIM);
            cv_rect(x, 16, 16, 1, C_DIM);
            cv_rect(x, 1, 1, 16, C_DIM);
            cv_rect(x + 15, 1, 1, 16, C_DIM);
        }
    }
    cv_blit(0, 150);
    lcd_sync();
}

#define SP_ACID RGB(200, 255, 0)

/* a band of hazard tape, 240 x 9, at y */
static void splash_tape(int32_t y)
{
    int32_t x, r;
    cv_begin(240, 9, C_BLACK);
    for (r = 0; r < 9; r++)
        for (x = 0; x < 240; x++)
            if (((x + r) & 15) < 8)
                cv_px[r * 240 + x] = swap16(SP_ACID);   /* the canvas holds byte-swapped pixels */
    cv_blit(0, (uint32_t)y);
    lcd_sync();
}

/* TRIBE, three times the font and a pixel heavier, in one colour */
static void splash_title(int32_t y)
{
    const felucca_font_t *f = &FONT_M;
    int32_t w = text_w(f, "TRIBE"), h = f->h, k = 3, W = w * k, H = h * k, Wc = W + 1, Hc = H + 1, x0 = (240 - Wc) / 2, i, j;
    uint16_t *mag, *comp;
    cv_begin((uint32_t)w, (uint32_t)h, C_BLACK);
    cv_text(0, 0, f, "TRIBE", C_WHITE);
    mag = cv_px + w * h;
    comp = mag + W * H;
    for (j = 0; j < H; j++)
        for (i = 0; i < W; i++)
            mag[j * W + i] = cv_px[(j / k) * w + i / k];
    for (j = 0; j < Hc; j++)
        for (i = 0; i < Wc; i++) {                        /* each pixel also lights its right and lower neighbours: strokes a pixel heavier */
            int on = (j < H && i < W && mag[j * W + i]) || (j < H && i > 0 && i <= W && mag[j * W + i - 1]) ||
                     (j > 0 && j <= H && i < W && mag[(j - 1) * W + i]) || (j > 0 && j <= H && i > 0 && i <= W && mag[(j - 1) * W + i - 1]);
            comp[j * Wc + i] = on ? swap16(SP_ACID) : 0;
        }
    lcd_blit((uint32_t)x0, (uint32_t)y, (uint32_t)Wc, (uint32_t)Hc, comp);
    lcd_sync();
}

static void splash_static(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);                 /* a clean screen first */
    splash_tape(0);
    splash_tape(231);
    splash_title(36);
    draw_text_box(0, 98, 240, &FONT_S, "S O U N D   S Y S T E M", C_WHITE, 1);
    splash_squares(0);
    draw_text_box(0, 178, 240, &FONT_S, "10 TRACKS  ANY ENGINE", SP_ACID, 1);
    draw_text_box(0, 214, 240, &FONT_XS, "BASED ON X0X AND FELUCCA", C_GRAY, 1);
}
