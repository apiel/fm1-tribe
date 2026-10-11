/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIBE's start-up screen: the name in big pixels, each letter in a track colour, ten squares (the tracks) that light up one after
 * another, and a line saying what it is. Drawn with the UI's canvas, so the same code runs on the FM-1 and in the emulator.
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

static void splash_static(void)
{
    static const char *const L[5] = {"T", "R", "I", "B", "E"};
    static const uint8_t COL[5] = {0, 2, 4, 6, 8};
    int32_t x, k = 3, adv[5], total = 0, i;
    const felucca_font_t *f = &FONT_M;
    for (i = 0; i < 5; i++) {                         /* the letters side by side, the whole word centred */
        adv[i] = text_w(f, L[i]) * k;
        total += adv[i];
    }
    x = (240 - total) / 2;
    lcd_fill(0, 0, 240, 240, C_BLACK);                 /* a clean screen first */
    for (i = 0; i < 5; i++) {
        splash_big(x, 48, f, L[i], TRK_COL[COL[i]], k);
        x += adv[i];
    }
    splash_squares(0);
    draw_text_box(0, 178, 240, &FONT_S, "10 TRACKS  ANY ENGINE", C_GRAY, 1);
    draw_text_box(0, 218, 240, &FONT_XS, "BASED ON X0X AND FELUCCA", C_DIM, 1);
}
