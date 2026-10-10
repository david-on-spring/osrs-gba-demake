// Software-rendered UI layers: every screen cell owns a unique 4bpp tile, so panels and
// proportional text can be drawn per pixel into RAM and streamed to VRAM when dirty.
#include "game.h"

static EWRAM_BSS u32 text_tiles[600 * 8];
static EWRAM_BSS u32 panel_tiles[600 * 8];
Canvas cv_text = {text_tiles, 0, 255, 0};
Canvas cv_panel = {panel_tiles, 601, 255, 0};

void cv_init(void) {
    cv_clear(&cv_text);
    cv_clear(&cv_panel);
}

static inline void mark(Canvas *c, int y0, int y1) {
    int r0 = y0 >> 3, r1 = y1 >> 3;
    if (r0 < c->dmin) c->dmin = r0;
    if (r1 > c->dmax) c->dmax = r1;
}

void cv_clear(Canvas *c) {
    dma3_fill32(c->tiles, 0, 600 * 8);
    c->dmin = 0;
    c->dmax = 19;
}

// Clears tile rows r0..r1 (8 pixel lines each) - much cheaper than a full clear.
void cv_clear_rows(Canvas *c, int r0, int r1) {
    dma3_fill32(&c->tiles[r0 * 30 * 8], 0, (r1 - r0 + 1) * 30 * 8);
    if (r0 < c->dmin) c->dmin = r0;
    if (r1 > c->dmax) c->dmax = r1;
}

// Streams at most `budget` dirty tile rows to VRAM; the rest follow next frame.
void cv_flush(Canvas *c, int budget) {
    if (c->dmin > c->dmax) return;
    int r0 = c->dmin, r1 = c->dmax > 19 ? 19 : c->dmax;
    if (r1 - r0 + 1 > budget) r1 = r0 + budget - 1;
    dma3_copy32((void *)(VRAM + (c->vram_tile + r0 * 30) * 16), &c->tiles[r0 * 30 * 8], (r1 - r0 + 1) * 30 * 8);
    if (r1 >= c->dmax) {
        c->dmin = 255;
        c->dmax = 0;
    } else {
        c->dmin = r1 + 1;
    }
}

IWRAM_CODE void cv_pset(Canvas *c, int x, int y, int col) {
    if ((unsigned)x >= SCREEN_W || (unsigned)y >= SCREEN_H) return;
    u32 *row = &c->tiles[((y >> 3) * 30 + (x >> 3)) * 8 + (y & 7)];
    int sh = (x & 7) << 2;
    *row = (*row & ~(0xFu << sh)) | ((u32)col << sh);
    mark(c, y, y);
}

IWRAM_CODE void cv_fill(Canvas *c, int x, int y, int w, int h, int col) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;
    u32 full = (u32)col * 0x11111111u;
    int x1 = x + w;
    for (int yy = y; yy < y + h; yy++) {
        u32 *base = &c->tiles[(yy >> 3) * 30 * 8 + (yy & 7)];
        int xx = x;
        while (xx < x1) {
            int tx = xx >> 3;
            int s = xx & 7;
            int e = (x1 - (tx << 3)) > 8 ? 8 : x1 - (tx << 3);
            u32 *row = &base[tx * 8];
            if (s == 0 && e == 8) {
                *row = full;
            } else {
                u32 mask = (e == 8 ? 0xFFFFFFFFu : ((1u << (e * 4)) - 1)) & ~((1u << (s * 4)) - 1);
                *row = (*row & ~mask) | (full & mask);
            }
            xx = (tx + 1) << 3;
        }
    }
    mark(c, y, y + h - 1);
}

// ---------------------------------------------------------------------------------------
// Text
static int glyph(int ch) {
    if (ch < 32 || ch > 127) ch = '?';
    return ch - 32;
}

int text_width(const char *s) {
    int w = 0;
    for (; *s; s++) w += font_width[glyph((u8)*s)] + 1;
    return w ? w - 1 : 0;
}

// hot: every menu redraw draws hundreds of glyphs, so this runs from IWRAM as ARM code
// nibble masks: bit k of the index -> 0xF in nibble k
static const u32 nib_expand[256] = {
#define N1(k) (((k) & 1 ? 0xFu : 0) | ((k) & 2 ? 0xF0u : 0) | ((k) & 4 ? 0xF00u : 0) | ((k) & 8 ? 0xF000u : 0))
#define N2(k) (N1(k) | (N1((k) >> 4) << 16))
#define R4(k) N2(k), N2(k + 1), N2(k + 2), N2(k + 3)
#define R16(k) R4(k), R4(k + 4), R4(k + 8), R4(k + 12)
    R16(0), R16(16), R16(32), R16(48), R16(64), R16(80), R16(96), R16(112),
    R16(128), R16(144), R16(160), R16(176), R16(192), R16(208), R16(224), R16(240),
#undef R16
#undef R4
#undef N2
#undef N1
};

// hot: every menu redraw draws hundreds of glyphs, so this runs from IWRAM as ARM code and
// writes each glyph row with at most two masked word stores
IWRAM_CODE static int draw_glyphs(int x, int y, const char *s, int col, int spacing) {
    Canvas *c = &cv_text;
    u32 *tiles = c->tiles;
    if (y < 0 || y + 9 > SCREEN_H) {
        for (; *s; s++) {
            int g = glyph((u8)*s);
            for (int r = 0; r < 9; r++) {
                u8 bits = font_rows[g][r];
                for (int b = 0; bits; b++, bits >>= 1)
                    if (bits & 1) cv_pset(c, x + b, y + r, col);
            }
            x += font_width[g] + spacing;
        }
        return x;
    }
    u32 cw = (u32)col * 0x11111111u;
    for (; *s; s++) {
        int g = glyph((u8)*s);
        const u8 *rows = font_rows[g];
        if (x >= 0 && x < SCREEN_W) {
            int t = x >> 3, sh = (x & 7) << 2;
            for (int r = 0; r < 9; r++) {
                u32 m = nib_expand[rows[r]];
                if (!m) continue;
                int yy = y + r;
                u32 *line = &tiles[(yy >> 3) * 240 + (yy & 7) + t * 8];
                u32 lo = m << sh;
                line[0] = (line[0] & ~lo) | (cw & lo);
                if (sh && t < 29) {
                    u32 hi = m >> (32 - sh);
                    if (hi) line[8] = (line[8] & ~hi) | (cw & hi);
                }
            }
        }
        x += font_width[g] + spacing;
    }
    int r0 = y >> 3, r1 = (y + 8) >> 3;
    if (r0 < c->dmin) c->dmin = r0;
    if (r1 > c->dmax) c->dmax = r1;
    return x;
}

int text_draw(int x, int y, const char *s, int col) {
    draw_glyphs(x + 1, y + 1, s, TC_SHADOW, 1);
    return draw_glyphs(x, y, s, col, 1);
}
int text_draw_ns(int x, int y, const char *s, int col) { return draw_glyphs(x, y, s, col, 1); }
int text_right(int xr, int y, const char *s, int col) { return text_draw(xr - text_width(s), y, s, col); }
int text_center(int xc, int y, const char *s, int col) { return text_draw(xc - text_width(s) / 2, y, s, col); }

int text_spaced_width(const char *s, int sp) {
    int w = 0;
    for (; *s; s++) w += font_width[glyph((u8)*s)] + sp;
    return w ? w - sp : 0;
}

int text_spaced(int x, int y, const char *s, int col, int sp) {
    draw_glyphs(x + 1, y + 1, s, TC_SHADOW, sp);
    return draw_glyphs(x, y, s, col, sp);
}

// 2x scaled, outlined, two-tone gold title text
void text_big(int xc, int y, const char *s) {
    int w = 0;
    for (const char *p = s; *p; p++) w += (font_width[glyph((u8)*p)] + 1) * 2;
    int x0 = xc - w / 2;
    for (int pass = 0; pass < 3; pass++) {
        int x = x0;
        for (const char *p = s; *p; p++) {
            int g = glyph((u8)*p);
            for (int r = 0; r < 9; r++) {
                u8 bits = font_rows[g][r];
                for (int b = 0; bits; b++, bits >>= 1) {
                    if (!(bits & 1)) continue;
                    int px = x + b * 2, py = y + r * 2;
                    if (pass == 0) {
                        cv_fill(&cv_text, px - 1, py - 1, 4, 5, TC_SHADOW);
                    } else if (pass == 1) {
                        cv_fill(&cv_text, px + 1, py + 1, 2, 2, TC_SHADOW);
                    } else {
                        int col = r < 3 ? TC_YELLOW : r < 5 ? TC_GOLD : TC_ORANGE;
                        cv_fill(&cv_text, px, py, 2, 2, col);
                    }
                }
            }
            x += (font_width[g] + 1) * 2;
        }
    }
}

int text_wrap(const char *s, int width, char lines[][48], int maxlines) {
    int n = 0;
    while (*s && n < maxlines) {
        while (*s == ' ') s++;
        int len = 0, last_space = -1, w = 0;
        while (s[len]) {
            int cw = font_width[glyph((u8)s[len])] + 1;
            if (w + cw - 1 > width && len > 0) break;
            if (s[len] == ' ') last_space = len;
            w += cw;
            len++;
            if (len >= 47) break;
        }
        if (s[len] && last_space > 0) len = last_space;
        for (int i = 0; i < len; i++) lines[n][i] = s[i];
        lines[n][len] = 0;
        n++;
        s += len;
    }
    return n;
}

// ---------------------------------------------------------------------------------------
// Panel primitives (panel canvas)

// Frosted panel with chamfered top-left/bottom-right corners and bright corner brackets.
// Rows are written a tile word at a time from a per-row pattern that is only rebuilt when the
// row's span changes (the chamfers), so big panels stay cheap.
IWRAM_CODE void draw_panel(int x, int y, int w, int h, int ch) {
    Canvas *c = &cv_panel;
    int x1 = x + w - 1, y1 = y + h - 1;
    u32 pw[30], pm[30];
    int pl = -9999, pr = -9999, pe = -1, t0 = 0, t1 = -1;
    // body: tile rows wholly inside the panel (below the top chamfer, above the bottom one)
    // get their fully covered tiles written as eight plain words, no read-modify-write
    int by0 = y + ch + 1, by1 = y1 - ch - 1;                  // rows with the full span
    if (by0 < 0) by0 = 0;
    if (by1 > SCREEN_H - 1) by1 = SCREEN_H - 1;
    int band0 = (by0 + 7) >> 3, band1 = ((by1 + 1) >> 3) - 1;  // tile rows fully inside
    int ft0 = (x + 1 + 7) >> 3, ft1 = (x1 >> 3) - 1;          // tiles fully inside (edge columns excluded)
    if (ft0 < 0) ft0 = 0;
    if (ft1 > 29) ft1 = 29;
    u32 fill = (u32)PC_FILL * 0x11111111u;
    if (band0 <= band1 && ft0 <= ft1)
        for (int b = band0; b <= band1; b++) {
            u32 *t = &c->tiles[b * 240 + ft0 * 8];
            for (int k = ft0; k <= ft1; k++, t += 8)
                t[0] = t[1] = t[2] = t[3] = t[4] = t[5] = t[6] = t[7] = fill;
        }
    for (int yy = y; yy <= y1; yy++) {
        if ((unsigned)yy >= SCREEN_H) continue;
        int inband = band0 <= band1 && (yy >> 3) >= band0 && (yy >> 3) <= band1;
        int l = x + (ch - (yy - y) > 0 ? ch - (yy - y) : 0);
        int r = x1 - (ch - (y1 - yy) > 0 ? ch - (y1 - yy) : 0);
        int edge = yy == y || yy == y1;
        if (l != pl || r != pr || edge != pe) {
            pl = l, pr = r, pe = edge;
            int lc = l < 0 ? 0 : l, rc = r > SCREEN_W - 1 ? SCREEN_W - 1 : r;
            t0 = lc >> 3;
            t1 = rc >> 3;
            u32 full = (u32)(edge ? PC_EDGE_DIM : PC_FILL) * 0x11111111u;
            for (int t = t0; t <= t1; t++) {
                int s0 = t == t0 ? (lc & 7) : 0, e0 = t == t1 ? (rc & 7) + 1 : 8;
                u32 m = (e0 == 8 ? 0xFFFFFFFFu : ((1u << (e0 * 4)) - 1)) & ~((1u << (s0 * 4)) - 1);
                pw[t] = full & m;
                pm[t] = m;
            }
            if (!edge) {
                if (l >= 0 && l < SCREEN_W) {
                    int sh = (l & 7) << 2;
                    pw[l >> 3] = (pw[l >> 3] & ~(0xFu << sh)) | ((u32)PC_EDGE_DIM << sh);
                }
                if (r >= 0 && r < SCREEN_W) {
                    int sh = (r & 7) << 2;
                    pw[r >> 3] = (pw[r >> 3] & ~(0xFu << sh)) | ((u32)PC_EDGE_DIM << sh);
                }
            }
        }
        u32 *line = &c->tiles[(yy >> 3) * 240 + (yy & 7)];
        for (int t = t0; t <= t1; t++) {
            if (inband && t >= ft0 && t <= ft1) continue;      // done above
            line[t * 8] = (line[t * 8] & ~pm[t]) | pw[t];
        }
    }
    mark(c, y < 0 ? 0 : y, y1 >= SCREEN_H ? SCREEN_H - 1 : y1);
    // bright brackets: top-left diagonal + arms, bottom-right diagonal + arms
    for (int i = 0; i <= ch; i++) {
        cv_pset(c, x + ch - i, y + i, PC_EDGE);
        cv_pset(c, x1 - ch + i, y1 - i, PC_EDGE);
    }
    int arm = w / 4 < 18 ? w / 4 : 18;
    cv_fill(c, x + ch, y, arm, 1, PC_EDGE);
    cv_fill(c, x, y + ch, 1, arm / 2, PC_EDGE);
    cv_fill(c, x1 - ch - arm + 1, y1, arm, 1, PC_EDGE);
    cv_fill(c, x1, y1 - ch - arm / 2 + 1, 1, arm / 2, PC_EDGE);
}

void draw_panel_plain(int x, int y, int w, int h, int col) { cv_fill(&cv_panel, x, y, w, h, col); }

// Selection bar: bright left accent, colour fading to the right with a slanted end.
// Each row is five spans (solid / two dithers / a dither over transparency); a dither's pixel
// colour only depends on pixel parity, so every span is one repeated word written per tile.
IWRAM_CODE static void hl_span(u32 *line, int x0, int x1, u32 word, u32 pmask) {
    if (x0 < 0) x0 = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    while (x0 < x1) {
        int t = x0 >> 3, s0 = x0 & 7, e0 = x1 - (t << 3) > 8 ? 8 : x1 - (t << 3);
        u32 m = (e0 == 8 ? 0xFFFFFFFFu : ((1u << (e0 * 4)) - 1)) & ~((1u << (s0 * 4)) - 1) & pmask;
        line[t * 8] = (line[t * 8] & ~m) | (word & m);
        x0 = (t + 1) << 3;
    }
}

IWRAM_CODE void draw_highlight(int x, int y, int w, int h) {
    Canvas *c = &cv_panel;
    for (int r = 0; r < h; r++) {
        int yy = y + r;
        if ((unsigned)yy >= SCREEN_H) continue;
        int len = w - (h - 1 - r) / 2;
        int a = len * 5 / 10, b = len * 7 / 10, d = len * 85 / 100;
        u32 *line = &c->tiles[(yy >> 3) * 240 + (yy & 7)];
        // pixel px uses colour "odd" when (px - x + r) is odd
        int par = (r - x) & 1;
        u32 odd_n = par ? 0x0F0F0F0Fu : 0xF0F0F0F0u;   // nibbles of pixels where (px - x + r) is odd
        u32 even_n = ~odd_n;
        hl_span(line, x, x + 2, (u32)PC_EDGE * 0x11111111u, ~0u);
        hl_span(line, x + 2, x + a, (u32)PC_HL1 * 0x11111111u, ~0u);
        hl_span(line, x + a, x + b, ((u32)PC_HL1 * 0x11111111u & odd_n) | ((u32)PC_HL2 * 0x11111111u & even_n), ~0u);
        hl_span(line, x + b, x + d, ((u32)PC_HL2 * 0x11111111u & odd_n) | ((u32)PC_HL3 * 0x11111111u & even_n), ~0u);
        hl_span(line, x + d, x + len, (u32)PC_HL3 * 0x11111111u, odd_n);
    }
    mark(c, y < 0 ? 0 : y, y + h - 1 >= SCREEN_H ? SCREEN_H - 1 : y + h - 1);
}

// Slim parallelogram gauge.
void draw_bar(int x, int y, int w, int val, int max, int col, int dark) {
    Canvas *c = &cv_panel;
    if (max <= 0) max = 1;
    if (val < 0) val = 0;
    if (val > max) val = max;
    int fill = (w - 4) * val / max;
    if (val > 0 && fill == 0) fill = 1;
    for (int r = 0; r < 4; r++) {
        int off = 3 - r;
        cv_fill(c, x + off, y + r, w - 3, 1, PC_BAR_BG);
        if (fill) cv_fill(c, x + off + 1, y + r, fill, 1, r < 2 ? col : dark);
    }
}

void draw_button(int x, int y, char label) {
    if (label == 'S') {
        // SELECT: a pill with a tiny label
        for (int r = 0; r < 9; r++) {
            int inset = (r == 0 || r == 8) ? 2 : (r == 1 || r == 7) ? 1 : 0;
            cv_fill(&cv_panel, x + inset, y + r, 17 - inset * 2, 1, PC_EDGE_DIM);
        }
        text_draw_ns(x + 2, y + 1, "SEL", TC_WHITE);
        return;
    }
    // round button glyph: filled circle on the panel layer, letter on the text layer
    static const u8 circ[9] = {0x1C, 0x3E, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C};
    for (int r = 0; r < 9; r++)
        for (int b = 0; b < 7; b++)
            if (circ[r] & (1 << b)) cv_pset(&cv_panel, x + b, y + r, label == 'B' ? PC_RED : label == 'A' ? PC_GREEN_D : PC_EDGE_DIM);
    char s[2] = {label, 0};
    if (label == 'S') s[0] = '+';
    text_draw_ns(x + 2 - (font_width[glyph(s[0])] > 3), y + 1, s, TC_WHITE);
}

void draw_rule(int x, int y, int w) {
    cv_fill(&cv_panel, x, y, w, 1, PC_EDGE_DIM);
    cv_fill(&cv_panel, x, y, w / 5, 1, PC_EDGE);
}

void draw_cell(int x, int y, int w, int h, int sel) {
    Canvas *c = &cv_panel;
    cv_fill(c, x, y, w, h, PC_WELL);
    if (sel) {
        cv_fill(c, x, y, w, 1, PC_EDGE);
        cv_fill(c, x, y + h - 1, w, 1, PC_EDGE);
        cv_fill(c, x, y, 1, h, PC_EDGE);
        cv_fill(c, x + w - 1, y, 1, h, PC_EDGE);
        cv_fill(c, x + 1, y + 1, w - 2, h - 2, PC_HL3);
    } else {
        cv_pset(c, x, y, PC_EDGE_DIM);
        cv_pset(c, x + w - 1, y + h - 1, PC_EDGE_DIM);
    }
}
