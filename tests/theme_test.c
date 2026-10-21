/* SPDX-License-Identifier: GPL-3.0-only */
/* The palettes and the real canvas/text renderer (src/gfx.c): GREY stays gray in every token and blend
 * (pitch-black background), every palette's text contrast, the alpha blend, the ramp cache, the fonts'
 * metrics and the ellipsis. Also: thin glyphs (3 / S) keep a middle ink run after 4-bit quantise; cv_blit
 * must not rewrite its expand buffer while a transfer is in flight, and a full graph strip uses CV_STRIP
 * rows per lcd_blit. Optional: a palette sheet (PPM). */
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#define __attribute__(x)
/* host stub: leave the blit source "in flight" until lcd_sync, as the panel DMA does */
static uint32_t lcd_nblit, lcd_live_n;
static const uint16_t *lcd_live;
static uint16_t lcd_snap[240 * 32];
static void lcd_sync(void)
{
    if (lcd_live) {
        assert(memcmp(lcd_live, lcd_snap, lcd_live_n * sizeof(uint16_t)) == 0);
        lcd_live = 0;
    }
}
static void lcd_power(uint32_t s) { (void)s; }   /* (MENU > SCREEN OFF: lcd.c) */
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    (void)x; (void)y;
    lcd_nblit++;
    lcd_sync();                                         /* next access waits, like lcd_window */
    lcd_live_n = w * h;
    assert(lcd_live_n && lcd_live_n <= sizeof lcd_snap / sizeof lcd_snap[0]);
    memcpy(lcd_snap, p, lcd_live_n * sizeof(uint16_t));
    lcd_live = p;                                       /* DMA still reading p */
}
#include "../firmware/src/gfx.c"

/* thin AA strokes (3, S) must keep a middle horizontal ink run after 4-bit quantise */
static int glyph_mid_run(const aafont_t *f, const char *s, uint16_t ink, uint16_t under)
{
    uint32_t x, y, ymin = 0xffffffffu, ymax = 0, best = 0, mid, row, run;
    cv_begin(24, f->h + 2u, under);
    cv_text_on(2, 1, f, s, ink, under);
    for (y = 0; y < cv_h; y++)
        for (x = 0; x < cv_w; x++)
            if (cv_peek(y * cv_w + x) != under) {
                if (y < ymin) ymin = y;
                if (y > ymax) ymax = y;
            }
    if (ymin > ymax)
        return 0;
    mid = (ymin + ymax) / 2u;
    for (row = mid > 0u ? mid - 1u : mid; row <= mid + 1u && row < cv_h; row++) {
        run = 0;
        for (x = 0; x < cv_w; x++) {
            if (cv_peek(row * cv_w + x) != under) {
                if (++run > best) best = run;
            } else
                run = 0;
        }
    }
    return best >= 2u;
}

#define SHEET_W 960u
#define SHEET_H (((NPALETTES + 3u) / 4u) * 124u)
static uint16_t sheet[SHEET_W * SHEET_H];
static unsigned channel(uint16_t c, unsigned n)
{ return n == 0 ? c >> 11 : n == 1 ? (c >> 5) & 63 : c & 31; }
static int gray(uint16_t c) { return channel(c, 0) == channel(c, 2) && channel(c, 1) == channel(c, 0) * 2u; }
static double luminance(uint16_t color)
{
    const double weight[] = {0.2126, 0.7152, 0.0722};
    double result = 0;
    for (unsigned c = 0; c < 3; c++) {
        double v = channel(color, c) / (c == 1 ? 63.0 : 31.0);
        result += weight[c] * (v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4));
    }
    return result;
}
static double contrast(uint16_t fg, uint16_t bg)
{
    double a = luminance(fg), b = luminance(bg);
    return a > b ? (a + 0.05) / (b + 0.05) : (b + 0.05) / (a + 0.05);
}

int main(int argc, char **argv)
{
    static const aag_t pg[1] = {{0, 48, 0, 0, 3, 1}};   /* one glyph 'A': 3 px, alpha 0, 8, 15 */
    static const uint8_t pd[] = {0x08, 0xf0};
    const aafont_t probe = {1, 1, 'A', 'A', 0, 0, pg, pd, 0, 0, 0};
    double worst = 100;
    assert(NPALETTES == 9u && !strcmp(UI_PALETTES[0].name, "GREY") && !strcmp(UI_PALETTES[7].name, "HI-CON") &&
           !strcmp(UI_PALETTES[8].name, "NIGHT") && UI_RETIRED_MONO_INDEX == 9u);
    /* (#50: NIGHT appended; GREY is the MONO of 1.0.1, id 0; MONO palette retired) */
    ux.style = ST_FLAT;
    palette_set(UI_GREY_INDEX);
    assert(T_BG == 0u && T_SURF == 0u);                 /* GREY: pitch black */
    for (unsigned p = 0; p < NPALETTES; p++) {
        uint16_t *tok = &ux.bg;
        palette_set(p);
        assert(ux.surf == ux.bg);                      /* SURF = BG in every palette (STYLE FLAT) */
        if (p == UI_GREY_INDEX)
            for (unsigned i = 0; i < 14; i++) assert(gray(tok[i]));      /* every token, derived ones too (RAISE, KEY the last) */
        /* text and the things read on the screen; the generator checks the same (tools/gen_ui_palettes.py) */
        struct { uint16_t fg, bg; double min; } c[] = {
            {T_TEXT, T_BG, 12.0}, {T_TEXT, T_SURF, 9.0}, {T_MID, T_BG, 5.0}, {T_MID, T_SURF, 4.0},
            {T_THEME, T_BG, 4.5}, {T_THEME, T_SURF, 4.5}, {T_ACCENT, T_BG, 4.5}, {T_ACCENT, T_SURF, 4.5},
            {T_INK, T_SEL, 4.5}, {T_INK, T_THEME, 4.5}, {T_DIM, T_BG, 2.2}, {T_LINE, T_BG, 1.25}, {T_REC, T_BG, 3.0},
            {T_INK, T_KEY, 4.5}, {T_KEY, T_BG, 3.0}, {T_KEY, T_SURF, 2.5}, {T_INK, T_DIM, 1.8}, {T_INK, T_ACCENT, 4.5},
            {T_TEXT, T_RAISE, 7.0}};
        double min = 100;
        for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
            double r = contrast(c[i].fg, c[i].bg);
            if (r < c[i].min) printf("%s: pairing %u contrast %.2f < %.2f\n", UI_PALETTES[p].name, i, r, c[i].min);
            assert(r >= c[i].min);
            if (r / c[i].min < min) min = r / c[i].min;
        }
        if (min < worst) worst = min;
        printf("%-7s text %.1f:1, labels %.1f:1, values %.1f:1, selection %.1f:1\n", UI_PALETTES[p].name,
               contrast(T_TEXT, T_BG), contrast(T_MID, T_BG), contrast(T_THEME, T_BG), contrast(T_INK, T_SEL));
        assert((p == 6u) == ux.light);                         /* PAPER is the light one */
        assert(T_REC == (p == UI_GREY_INDEX ? T_ACCENT : ux.light ? UI_REC_LIGHT : UI_REC_DARK));
        /* the blend: transparent, solid and an edge between the two, on two backgrounds */
        for (unsigned b = 0; b < 2; b++) {
            uint16_t under = b ? T_SEL : T_BG, ink = b ? T_INK : T_THEME;
            cv_begin(3, 1, under);
            cv_text_on(0, 0, &probe, "A", ink, under);
            assert(cv_peek(0) == under && cv_peek(2) == ink);
            {   /* 4-bit canvas: the edge snaps to a theme slot (may leave the under->ink channel box) */
                uint16_t mid = cv_peek(1);
                unsigned i, ok = 0;
                for (i = 0; i < CV_SENT; i++)
                    if (cv_tok[i] == mid) ok = 1;
                assert(ok);
                if (p == UI_GREY_INDEX) assert(gray(mid));
            }
        }
        assert(text_w(&AF_S, UI_PALETTES[p].name) <= 60);     /* the COLOR row's value */
        cv_begin(240, 124, T_BG);
        cv_text(4, 2, &AF_M, UI_PALETTES[p].name, T_TEXT);
        for (unsigned k = 0; k < 5; k++) cv_rect(150 + k * 16, 4, 12, 12, tok[k]);
        cv_text(4, 26, &AF_S, "LABEL", T_MID);
        cv_text(70, 22, &AF_M, "VALUE 127", T_THEME);
        cv_text(170, 22, &AF_M, "HOT", T_ACCENT);
        cv_text(4, 46, &AF_S, "INACTIVE", T_DIM);
        cv_rect(80, 44, 150, 17, T_SEL);
        cv_text_on(86, 45, &AF_S, "SELECTED ROW", T_INK, T_SEL);
        cv_rect(4, 66, 232, 1, T_LINE);
        cv_rect(4, 72, 120, 3, T_LINE);
        cv_rect(4, 72, 70, 3, T_SEL);
        cv_rect(74, 70, 1, 7, T_THEME);
        cv_text(136, 66, &AF_S, "REC", T_REC);
        cv_text(4, 82, &AF_L, "124", T_THEME);
        cv_text(80, 92, &AF_S, "Mixed case text", T_TEXT);
        cv_key_hint(80, 108, KC_OCTUP, "LOAD", 1, T_BG);          /* keycaps: available, unavailable */
        cv_key_hint(150, 108, KC_OCTDN, "BACK", 0, T_BG);
        for (unsigned y = 0; y < 124; y++)
            for (unsigned x = 0; x < 240; x++) {
                uint16_t v = cv_peek(y * 240 + x);
                if (p == UI_GREY_INDEX) assert(gray(v));          /* every blended pixel of GREY */
                sheet[(p / 4 * 124 + y) * 960 + p % 4 * 240 + x] = v;
            }
    }
    /* the ramp cache follows the palette (a stale ramp would blend toward the old background) */
    palette_set(1);
    cv_begin(3, 1, T_BG); cv_text(0, 0, &probe, "A", T_THEME);
    uint16_t green = cv_peek(1);
    palette_set(6);
    cv_begin(3, 1, T_BG); cv_text(0, 0, &probe, "A", T_THEME);
    assert(cv_peek(1) != green);
    /* 4-bit canvas: THEME on SURF must keep the middle bar of 3 / S (card values, preset engine names) */
    for (unsigned p = 0; p < NPALETTES; p++) {
        palette_set(p);
        assert(glyph_mid_run(&AF_M, "3", T_THEME, T_SURF));
        assert(glyph_mid_run(&AF_S, "S", T_THEME, T_SURF));
        assert(glyph_mid_run(&AF_S, "3", T_THEME, T_SURF));
    }
    /* expand buffer must not change while a blit is in flight; graph strip fewer transfers than 1 row each */
    {
        uint32_t want;
        palette_set(0);
        lcd_nblit = 0;
        cv_begin(240, 124, T_BG);
        cv_rect(0, 0, 240, 124, T_SURF);
        cv_text(8, 40, &AF_M, "PHAS", T_THEME);
        cv_blit(0, 0);
        lcd_sync();
        want = (124u + CV_UNIT - 1u) / CV_UNIT;
        assert(lcd_nblit == want);
        assert(lcd_nblit < 124u);
    }
    /* fonts: Terminus S 12 px (6x12), M 16 px (8x16 1x), L 32 px (8x16 2x); monospace digits; the ellipsis; L capitals only */
    assert(AF_S.h == 12 && AF_S.asc == 10 && AF_M.h == 16 && AF_M.asc == 12 && AF_L.h == 32 && AF_L.asc == 24);
    assert(text_w(&AF_M, "0000") == text_w(&AF_M, "1111") && text_w(&AF_S, "1.25") == text_w(&AF_S, "8.75"));
    assert(glyph(&AF_S, (uint8_t)ELLIPSIS) != glyph(&AF_S, '?') && glyph(&AF_M, (uint8_t)ELLIPSIS) != glyph(&AF_M, '?'));
    assert(text_w(&AF_L, "abc") == text_w(&AF_L, "ABC"));
    {
        char b[32];
        assert(!text_fit(b, sizeof b, "KICK", &AF_S, 60) && !strcmp(b, "KICK"));
        assert(text_fit(b, sizeof b, "A VERY LONG PRESET NAME", &AF_S, 60) && text_w(&AF_S, b) <= 60);
        assert(b[strlen(b) - 1] == ELLIPSIS && b[strlen(b) - 2] != ' ');
        assert(text_fit(b, sizeof b, "WWWWWWWW", &AF_M, 1) && b[0] == ELLIPSIS && !b[1]);
    }
    if (argc > 1) {
        FILE *f = fopen(argv[1], "wb"); assert(f);
        fprintf(f, "P6\n%u %u\n255\n", (unsigned)SHEET_W, (unsigned)SHEET_H);
        for (unsigned i = 0; i < SHEET_W * SHEET_H; i++) {
            fputc(channel(sheet[i], 0) * 255 / 31, f);
            fputc(channel(sheet[i], 1) * 255 / 63, f);
            fputc(channel(sheet[i], 2) * 255 / 31, f);
        }
        fclose(f);
    }
    printf("Palettes: GREY gray (pitch black), contrast (worst margin x%.2f), blending, ramp cache, font metrics and ellipsis passed.\n", worst);
    return 0;
}
