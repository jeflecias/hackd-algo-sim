/* gfx.c - shared 32-bit DIB framebuffer, text via GDI, glitch primitives */
#include "app.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int fb_create(Framebuffer *fb, int w, int h){
    memset(fb, 0, sizeof(*fb));
    fb->w = w; fb->h = h;

    HDC screen = GetDC(NULL);
    fb->memdc = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    if (!fb->memdc) return 0;

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;          /* top-down */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    fb->dib = CreateDIBSection(fb->memdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!fb->dib || !bits){ DeleteDC(fb->memdc); return 0; }
    fb->px = (uint32_t*)bits;
    SelectObject(fb->memdc, fb->dib);

    /* monospace font sized to screen height */
    int fh = h / 34;            /* ~34 text rows on screen */
    if (fh < 12) fh = 12;
    fb->font = CreateFontA(fh, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    if (!fb->font)
        fb->font = CreateFontA(fh, 0, 0, 0, FW_NORMAL, 0,0,0, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Lucida Console");
    SelectObject(fb->memdc, fb->font);
    SetBkMode(fb->memdc, TRANSPARENT);

    TEXTMETRICA tm;
    GetTextMetricsA(fb->memdc, &tm);
    fb->ch_w = tm.tmAveCharWidth;
    fb->ch_h = tm.tmHeight + 2;
    fb->bch_w = fb->ch_w;
    fb->bch_h = fb->ch_h;
    fb->base_fh = fh;
    fb->nfont = 0;
    return 1;
}

void fb_destroy(Framebuffer *fb){
    for (int i = 0; i < fb->nfont; i++) if (fb->fcache[i]) DeleteObject(fb->fcache[i]);
    if (fb->prev) free(fb->prev);
    if (fb->font) DeleteObject(fb->font);
    if (fb->dib)  DeleteObject(fb->dib);
    if (fb->memdc)DeleteDC(fb->memdc);
    memset(fb, 0, sizeof(*fb));
}

/* select (creating/caching as needed) a monospace font 'px' pixels high.
   updates the framebuffer's active ch_w/ch_h and returns ch_h. */
int fb_font_for(Framebuffer *fb, int px){
    if (px < FONT_MIN_PX) px = FONT_MIN_PX;
    if (px >= fb->base_fh){ fb_font_base(fb); return fb->ch_h; }
    for (int i = 0; i < fb->nfont; i++){
        if (fb->fpx[i] == px){
            SelectObject(fb->memdc, fb->fcache[i]);
            fb->ch_w = fb->fcw[i]; fb->ch_h = fb->fch[i];
            return fb->ch_h;
        }
    }
    if (fb->nfont >= FB_FONTCACHE){ fb_font_base(fb); return fb->ch_h; }
    HFONT f = CreateFontA(px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    if (!f){ fb_font_base(fb); return fb->ch_h; }
    SelectObject(fb->memdc, f);
    TEXTMETRICA tm; GetTextMetricsA(fb->memdc, &tm);
    int i = fb->nfont++;
    fb->fcache[i] = f; fb->fpx[i] = px;
    fb->fcw[i] = tm.tmAveCharWidth; fb->fch[i] = tm.tmHeight + 2;
    fb->ch_w = fb->fcw[i]; fb->ch_h = fb->fch[i];
    return fb->ch_h;
}

void fb_font_base(Framebuffer *fb){
    SelectObject(fb->memdc, fb->font);
    fb->ch_w = fb->bch_w; fb->ch_h = fb->bch_h;
}

void fb_text_sm(Framebuffer *fb, int x, int y, const char *s, uint32_t c){
    fb_font_for(fb, fb->base_fh * 72 / 100);
    fb_text(fb, x, y, s, c);
    fb_font_base(fb);
}

void fb_clear(Framebuffer *fb, uint32_t c){
    int n = fb->w * fb->h;
    uint32_t *p = fb->px;
    for (int i = 0; i < n; i++) p[i] = c;
}

void fb_present(Framebuffer *fb, HDC dst){
    BitBlt(dst, 0, 0, fb->w, fb->h, fb->memdc, 0, 0, SRCCOPY);
}

void fb_fill_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t c){
    if (x < 0){ w += x; x = 0; }
    if (y < 0){ h += y; y = 0; }
    if (x + w > fb->w) w = fb->w - x;
    if (y + h > fb->h) h = fb->h - y;
    for (int j = 0; j < h; j++){
        uint32_t *row = fb->px + (size_t)(y + j) * fb->w + x;
        for (int i = 0; i < w; i++) row[i] = c;
    }
}

void fb_text(Framebuffer *fb, int x, int y, const char *s, uint32_t c){
    SetTextColor(fb->memdc, to_cref(c));
    TextOutA(fb->memdc, x, y, s, (int)strlen(s));
}

void fb_blit_shot(Framebuffer *fb, const uint32_t *shot){
    memcpy(fb->px, shot, (size_t)fb->w * fb->h * 4);
}

void fb_hline(Framebuffer *fb, int x, int y, int w, uint32_t c){ fb_fill_rect(fb,x,y,w,1,c); }
void fb_vline(Framebuffer *fb, int x, int y, int h, uint32_t c){ fb_fill_rect(fb,x,y,1,h,c); }

void fb_frame(Framebuffer *fb, int x, int y, int w, int h, uint32_t c){
    fb_hline(fb, x, y, w, c);
    fb_hline(fb, x, y+h-1, w, c);
    fb_vline(fb, x, y, h, c);
    fb_vline(fb, x+w-1, y, h, c);
}

void fb_text_center(Framebuffer *fb, int cx, int y, const char *s, uint32_t c){
    int x = cx - (int)strlen(s) * fb->ch_w / 2;
    fb_text(fb, x, y, s, c);
}

/* TUI-style frame with chunky corner brackets (looks like an exploit-tool panel) */
void fb_box(Framebuffer *fb, int x, int y, int w, int h, uint32_t c){
    if (w < 4 || h < 4) return;
    fb_frame(fb, x, y, w, h, c);
    int t = 2, L = 9;
    fb_fill_rect(fb, x,       y,       L, t, c); fb_fill_rect(fb, x,       y,       t, L, c);
    fb_fill_rect(fb, x+w-L,   y,       L, t, c); fb_fill_rect(fb, x+w-t,   y,       t, L, c);
    fb_fill_rect(fb, x,       y+h-t,   L, t, c); fb_fill_rect(fb, x,       y+h-L,   t, L, c);
    fb_fill_rect(fb, x+w-L,   y+h-t,   L, t, c); fb_fill_rect(fb, x+w-t,   y+h-L,   t, L, c);
}

/* ----------------- glitch primitives ----------------- */

void gfx_scanlines(Framebuffer *fb, int strength){
    for (int y = 0; y < fb->h; y += 2){
        uint32_t *row = fb->px + (size_t)y * fb->w;
        for (int x = 0; x < fb->w; x++){
            uint32_t p = row[x];
            int r = (p>>16)&0xFF, g = (p>>8)&0xFF, b = p&0xFF;
            r = r*strength/100; g = g*strength/100; b = b*strength/100;
            row[x] = RGB32(r,g,b);
        }
    }
}

void gfx_rgb_split(Framebuffer *fb, int dx){
    if (dx == 0) return;
    for (int y = 0; y < fb->h; y++){
        uint32_t *row = fb->px + (size_t)y * fb->w;
        for (int x = fb->w - 1; x >= 0; x--){
            int xr = x - dx, xb = x + dx;
            if (xr < 0) xr = 0;
            if (xb >= fb->w) xb = fb->w-1;
            uint32_t r = (row[xr]>>16)&0xFF;
            uint32_t g = (row[x]>>8)&0xFF;
            uint32_t b = row[xb]&0xFF;
            row[x] = (r<<16)|(g<<8)|b;
        }
    }
}

void gfx_slice_tear(Framebuffer *fb, uint64_t *rng, int amount, int bands){
    for (int i = 0; i < bands; i++){
        int y  = rng_range(rng, 0, fb->h - 1);
        int bh = rng_range(rng, 4, 40);
        int sh = rng_range(rng, -amount, amount);
        if (sh == 0) continue;
        for (int j = 0; j < bh && y + j < fb->h; j++){
            uint32_t *row = fb->px + (size_t)(y + j) * fb->w;
            if (sh > 0){
                for (int x = fb->w - 1; x >= sh; x--) row[x] = row[x - sh];
            } else {
                int s = -sh;
                for (int x = 0; x < fb->w - s; x++) row[x] = row[x + s];
            }
        }
    }
}

void gfx_noise_blocks(Framebuffer *fb, uint64_t *rng, int count, int maxsz){
    static const uint32_t pal[] = { COL_GREEN, COL_RED, 0x00FFFFFF, 0x00000000, COL_CYAN };
    for (int i = 0; i < count; i++){
        int w = rng_range(rng, 2, maxsz);
        int h = rng_range(rng, 1, maxsz/3 + 1);
        int x = rng_range(rng, 0, fb->w - 1);
        int y = rng_range(rng, 0, fb->h - 1);
        uint32_t c = pal[rng_range(rng, 0, 4)];
        fb_fill_rect(fb, x, y, w, h, c);
    }
}

void gfx_invert_band(Framebuffer *fb, int y, int h){
    if (y < 0){ h += y; y = 0; }
    if (y + h > fb->h) h = fb->h - y;
    for (int j = 0; j < h; j++){
        uint32_t *row = fb->px + (size_t)(y + j) * fb->w;
        for (int x = 0; x < fb->w; x++) row[x] = ~row[x] & 0x00FFFFFF;
    }
}

/* CRT phosphor persistence: bright pixels leave a decaying trail.
   Keeps a retained copy of the composited frame and blends toward it. */
void gfx_phosphor(Framebuffer *fb, int fade){
    int n = fb->w * fb->h;
    if (!fb->prev){
        fb->prev = (uint32_t*)malloc((size_t)n * 4);
        if (!fb->prev) return;
        memcpy(fb->prev, fb->px, (size_t)n * 4);
        return;
    }
    uint32_t *px = fb->px, *pv = fb->prev;
    for (int i = 0; i < n; i++){
        uint32_t cu = px[i], pr = pv[i];
        int r = (cu>>16)&0xFF, g = (cu>>8)&0xFF, b = cu&0xFF;
        int pr_r = ((pr>>16)&0xFF)*fade/100;
        int pr_g = ((pr>>8)&0xFF)*fade/100;
        int pr_b = (pr&0xFF)*fade/100;
        if (pr_r > r) r = pr_r;
        if (pr_g > g) g = pr_g;
        if (pr_b > b) b = pr_b;
        uint32_t o = RGB32(r,g,b);
        px[i] = o; pv[i] = o;
    }
}

/* localized corruption: horizontal block-displacement confined to a rect */
void gfx_datamosh(Framebuffer *fb, uint64_t *rng, int x, int y, int w, int h){
    if (x < 0){ w += x; x = 0; }
    if (y < 0){ h += y; y = 0; }
    if (x + w > fb->w) w = fb->w - x;
    if (y + h > fb->h) h = fb->h - y;
    if (w < 2 || h < 2) return;
    for (int b = 0; b < 4; b++){
        int by = y + rng_range(rng, 0, h - 1);
        int bh = rng_range(rng, 2, h/3 + 1);
        int sh = rng_range(rng, -14, 14);
        if (sh == 0) continue;
        for (int j = 0; j < bh && by + j < y + h; j++){
            uint32_t *row = fb->px + (size_t)(by + j) * fb->w;
            if (sh > 0){ for (int i = x+w-1; i >= x+sh; i--) row[i] = row[i-sh]; }
            else       { int s = -sh; for (int i = x; i < x+w-s; i++) row[i] = row[i+s]; }
        }
    }
}

/* global brightness multiply (for flicker / darkness pulses) */
void gfx_brightness(Framebuffer *fb, int pct){
    if (pct >= 100) return;
    if (pct < 0) pct = 0;
    int n = fb->w * fb->h;
    uint32_t *p = fb->px;
    for (int i = 0; i < n; i++){
        uint32_t c = p[i];
        int r = ((c>>16)&0xFF)*pct/100, g = ((c>>8)&0xFF)*pct/100, b = (c&0xFF)*pct/100;
        p[i] = RGB32(r,g,b);
    }
}

/* drop the phosphor trail buffer so the next gfx_phosphor reseeds cleanly
   (call when switching to a state that uses trails, to avoid cross-state ghosting) */
void gfx_phosphor_reset(Framebuffer *fb){
    if (fb->prev){ free(fb->prev); fb->prev = NULL; }
}

/* copy src->dst, randomly corrupting non-space glyphs (Zalgo-lite glitch text) */
void fb_garble(char *dst, const char *src, uint64_t *rng, int pct){
    static const char G[] = "#@%&$!?*<>/\\|01010";
    int i = 0;
    for (; src[i] && i < TERM_MAXLINE-1; i++){
        if (src[i] != ' ' && (int)(rng_next(rng)%100) < pct)
            dst[i] = G[rng_next(rng) % (int)(sizeof(G)-1)];
        else
            dst[i] = src[i];
    }
    dst[i] = 0;
}

/* whole-frame horizontal jitter (signal can't hold lock) */
void gfx_jitter(Framebuffer *fb, uint64_t *rng, int amp){
    int dx = rng_range(rng, -amp, amp);
    if (dx == 0) return;
    for (int y = 0; y < fb->h; y++){
        uint32_t *row = fb->px + (size_t)y * fb->w;
        if (dx > 0){ for (int i = fb->w-1; i >= dx; i--) row[i] = row[i-dx]; }
        else       { int s = -dx; for (int i = 0; i < fb->w-s; i++) row[i] = row[i+s]; }
    }
}

/* decorative fake hex memory dump to fill dead margins */
void gfx_hexdump(Framebuffer *fb, uint64_t *rng, int x, int y, int w, int h){
    int lh = fb->base_fh * 62 / 100;
    fb_font_for(fb, lh);
    int rh = fb->ch_h;
    uint32_t addr = 0x7FFE0000u ^ (rng_next(rng) & 0xFFFF0u);
    int bytes = (w / (fb->ch_w*3)) - 4; if (bytes > 12) bytes = 12; if (bytes < 4) bytes = 4;
    for (int yy = y; yy + rh <= y + h; yy += rh){
        char line[160]; int p = 0;
        p += snprintf(line+p, sizeof(line)-p, "%08X:", addr);
        for (int i = 0; i < bytes && p < (int)sizeof(line)-4; i++)
            p += snprintf(line+p, sizeof(line)-p, " %02X", (unsigned)(rng_next(rng) & 0xFF));
        fb_text(fb, x, yy, line, 0x00184a24);
        addr += bytes;
    }
    fb_font_base(fb);
}

void gfx_vignette(Framebuffer *fb){
    int cx = fb->w/2, cy = fb->h/2;
    int maxd = cx*cx + cy*cy;
    for (int y = 0; y < fb->h; y++){
        uint32_t *row = fb->px + (size_t)y * fb->w;
        int dy = y - cy;
        for (int x = 0; x < fb->w; x++){
            int dx = x - cx;
            int d = dx*dx + dy*dy;
            int f = 100 - (d * 70 / maxd);   /* center 100% -> edge 30% */
            uint32_t p = row[x];
            int r = ((p>>16)&0xFF)*f/100;
            int g = ((p>>8)&0xFF)*f/100;
            int b = (p&0xFF)*f/100;
            row[x] = RGB32(r,g,b);
        }
    }
}
