/* gfx.c - shared 32-bit DIB framebuffer, text via GDI, glitch primitives */
#include "app.h"
#include <string.h>

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
    return 1;
}

void fb_destroy(Framebuffer *fb){
    if (fb->font) DeleteObject(fb->font);
    if (fb->dib)  DeleteObject(fb->dib);
    if (fb->memdc)DeleteDC(fb->memdc);
    memset(fb, 0, sizeof(*fb));
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
            if (xr < 0) xr = 0; if (xb >= fb->w) xb = fb->w-1;
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
