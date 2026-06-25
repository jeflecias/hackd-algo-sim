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

/* soft additive red glow (light bleeding through a break behind the screen). Red-biased:
   strength at the centre, falling to 0 at the rim. Shared by the cracked terminal, the
   skull's eye sockets, and the reaching hand's backlight. */
void red_glow(Framebuffer *fb, int cx, int cy, int rad, int strength){
    if (rad <= 0) return;
    int x0 = cx-rad < 0 ? 0 : cx-rad, x1 = cx+rad >= fb->w ? fb->w-1 : cx+rad;
    int y0 = cy-rad < 0 ? 0 : cy-rad, y1 = cy+rad >= fb->h ? fb->h-1 : cy+rad;
    int r2 = rad*rad;
    for (int y = y0; y <= y1; y++){
        uint32_t *row = fb->px + (size_t)y*fb->w;
        int dy = y-cy;
        for (int x = x0; x <= x1; x++){
            int dx = x-cx, d2 = dx*dx+dy*dy;
            if (d2 > r2) continue;
            int f = strength * (r2 - d2) / r2;               /* bright centre -> 0 at rim */
            uint32_t p = row[x];
            int rr = ((p>>16)&0xFF) + f;        if (rr>255) rr=255;
            int gg = ((p>>8)&0xFF)  + f/8;      if (gg>255) gg=255;
            int bb = (p&0xFF)       + f/12;     if (bb>255) bb=255;
            row[x] = RGB32(rr,gg,bb);
        }
    }
}

void fb_blit_shot(Framebuffer *fb, const uint32_t *shot){
    memcpy(fb->px, shot, (size_t)fb->w * fb->h * 4);
}

/* nearest-neighbor scale the full-res desktop shot into rect (dx,dy,dw,dh), clipped */
void fb_blit_shot_rect(Framebuffer *fb, const uint32_t *shot, int dx, int dy, int dw, int dh){
    if (!shot || dw <= 0 || dh <= 0) return;
    int x0 = dx < 0 ? 0 : dx, y0 = dy < 0 ? 0 : dy;
    int x1 = dx + dw; if (x1 > fb->w) x1 = fb->w;
    int y1 = dy + dh; if (y1 > fb->h) y1 = fb->h;
    for (int y = y0; y < y1; y++){
        int sy = (int)((double)(y - dy) / dh * fb->h);
        if (sy < 0) sy = 0; else if (sy >= fb->h) sy = fb->h - 1;
        const uint32_t *srow = shot + (size_t)sy * fb->w;
        uint32_t *drow = fb->px + (size_t)y * fb->w;
        for (int x = x0; x < x1; x++){
            int sx = (int)((double)(x - dx) / dw * fb->w);
            if (sx < 0) sx = 0; else if (sx >= fb->w) sx = fb->w - 1;
            drow[x] = srow[sx];
        }
    }
}

void fb_hline(Framebuffer *fb, int x, int y, int w, uint32_t c){ fb_fill_rect(fb,x,y,w,1,c); }
void fb_vline(Framebuffer *fb, int x, int y, int h, uint32_t c){ fb_fill_rect(fb,x,y,1,h,c); }

/* Bresenham line, clipped to the framebuffer (per-pixel bounds test) */
void fb_line(Framebuffer *fb, int x0, int y0, int x1, int y1, uint32_t c){
    int dx = x1 - x0; if (dx < 0) dx = -dx;
    int dy = y1 - y0; if (dy < 0) dy = -dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;){
        if (x0 >= 0 && x0 < fb->w && y0 >= 0 && y0 < fb->h)
            fb->px[(size_t)y0 * fb->w + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = err << 1;
        if (e2 > -dy){ err -= dy; x0 += sx; }
        if (e2 <  dx){ err += dx; y0 += sy; }
    }
}

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

/* nearest-neighbor upscale of a low-res src (sw x sh) into dst (fixed-point step).
   used to render the 3D world at a low internal resolution, then blow it up. */
void fb_upscale(Framebuffer *dst, const uint32_t *src, int sw, int sh){
    int dw = dst->w, dh = dst->h;
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    uint32_t dsx = ((uint32_t)sw << 16) / dw;
    uint32_t dsy = ((uint32_t)sh << 16) / dh;
    uint32_t syf = 0;
    for (int y = 0; y < dh; y++){
        int sy = syf >> 16;
        if (sy >= sh) sy = sh-1;
        syf += dsy;
        const uint32_t *srow = src + (size_t)sy*sw;
        uint32_t *drow = dst->px + (size_t)y*dw;
        uint32_t sxf = 0;
        for (int x = 0; x < dw; x++){
            int sx = sxf >> 16; sxf += dsx;
            drow[x] = srow[sx];
        }
    }
}

/* pull each pixel toward its luma by pct% (SH1-style desaturated grade) */
void gfx_desaturate(Framebuffer *fb, int pct){
    if (pct <= 0) return;
    if (pct > 100) pct = 100;
    int n = fb->w * fb->h;
    uint32_t *p = fb->px;
    for (int i = 0; i < n; i++){
        uint32_t c = p[i];
        int r=(c>>16)&0xFF, g=(c>>8)&0xFF, b=c&0xFF;
        int y = (r*54 + g*183 + b*19) >> 8;        /* luma approx */
        r += (y-r)*pct/100; g += (y-g)*pct/100; b += (y-b)*pct/100;
        p[i] = RGB32(r,g,b);
    }
}

/* ordered 4x4 Bayer dither - the PS1 banded-gradient texture */
void gfx_dither(Framebuffer *fb){
    static const int B[4][4] = {{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    for (int y = 0; y < fb->h; y++){
        uint32_t *row = fb->px + (size_t)y*fb->w;
        const int *brow = B[y & 3];
        for (int x = 0; x < fb->w; x++){
            int d = brow[x & 3] - 8;               /* -8..7 */
            uint32_t c = row[x];
            int r=((c>>16)&0xFF)+d, g=((c>>8)&0xFF)+d, b=(c&0xFF)+d;
            r = r<0?0:(r>255?255:r);
            g = g<0?0:(g>255?255:g);
            b = b<0?0:(b>255?255:b);
            row[x] = RGB32(r,g,b);
        }
    }
}

/* subtle film grain: nudge a staggered subset of pixels by +/-amp each frame */
void gfx_grain(Framebuffer *fb, uint64_t *rng, int amp){
    if (amp <= 0) return;
    int n = fb->w * fb->h;
    uint32_t *p = fb->px;
    int start = (int)(rng_next(rng) % 3);
    for (int i = start; i < n; i += 3){
        int d = (int)(rng_next(rng) % (2*amp+1)) - amp;
        uint32_t c = p[i];
        int r = ((c>>16)&0xFF)+d, g = ((c>>8)&0xFF)+d, b = (c&0xFF)+d;
        r = r<0?0:(r>255?255:r);
        g = g<0?0:(g>255?255:g);
        b = b<0?0:(b>255?255:b);
        p[i] = RGB32(r,g,b);
    }
}

/* bloom: downsample bright pixels to half-res, separable box-blur, add back.
   the one heavier post pass; scratch buffers are malloc'd once and reused. */
void gfx_bloom(Framebuffer *fb, int threshold, int intensity){
    static uint32_t *ba = NULL, *bb = NULL; static int bw = 0, bh = 0;
    int hw = fb->w/2, hh = fb->h/2;
    if (hw < 2 || hh < 2) return;
    if (bw != hw || bh != hh){
        free(ba); free(bb);
        ba = (uint32_t*)malloc((size_t)hw*hh*4);
        bb = (uint32_t*)malloc((size_t)hw*hh*4);
        bw = hw; bh = hh;
    }
    if (!ba || !bb){ bw = bh = 0; return; }

    for (int y = 0; y < hh; y++){
        uint32_t *src = fb->px + (size_t)(y*2)*fb->w;
        uint32_t *d = ba + (size_t)y*hw;
        for (int x = 0; x < hw; x++){
            uint32_t c = src[x*2];
            int r=(c>>16)&0xFF, g=(c>>8)&0xFF, b=c&0xFF;
            int mx = r>g?r:g; if (b>mx) mx=b;
            d[x] = (mx > threshold) ? c : 0u;
        }
    }
    int R = 2;
    for (int y = 0; y < hh; y++){                          /* horizontal ba->bb */
        uint32_t *s = ba + (size_t)y*hw, *o = bb + (size_t)y*hw;
        for (int x = 0; x < hw; x++){
            int rs=0,gs=0,bs=0,cnt=0;
            for (int k=-R;k<=R;k++){ int xx=x+k; if(xx<0||xx>=hw)continue;
                uint32_t c=s[xx]; rs+=(c>>16)&0xFF; gs+=(c>>8)&0xFF; bs+=c&0xFF; cnt++; }
            o[x] = RGB32(rs/cnt, gs/cnt, bs/cnt);
        }
    }
    for (int x = 0; x < hw; x++){                          /* vertical bb->ba */
        for (int y = 0; y < hh; y++){
            int rs=0,gs=0,bs=0,cnt=0;
            for (int k=-R;k<=R;k++){ int yy=y+k; if(yy<0||yy>=hh)continue;
                uint32_t c=bb[(size_t)yy*hw+x]; rs+=(c>>16)&0xFF; gs+=(c>>8)&0xFF; bs+=c&0xFF; cnt++; }
            ba[(size_t)y*hw+x] = RGB32(rs/cnt, gs/cnt, bs/cnt);
        }
    }
    for (int y = 0; y < fb->h; y++){                       /* add back, upscaled */
        uint32_t *d = fb->px + (size_t)y*fb->w;
        uint32_t *s = ba + (size_t)(y/2)*hw;
        for (int x = 0; x < fb->w; x++){
            uint32_t bl = s[x/2];
            int r = ((d[x]>>16)&0xFF) + (((bl>>16)&0xFF)*intensity/100);
            int g = ((d[x]>>8)&0xFF)  + (((bl>>8)&0xFF)*intensity/100);
            int b = (d[x]&0xFF)       + ((bl&0xFF)*intensity/100);
            r = r>255?255:r; g = g>255?255:g; b = b>255?255:b;
            d[x] = RGB32(r,g,b);
        }
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
