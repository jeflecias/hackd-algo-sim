/* skull.c - a procedurally drawn dead skull: a process that deadlocked, was never reaped,
   and was paged out to disk. Hollow, cracked cranium with backlit, red-glowing eye sockets.
   Built from gfx primitives (fb_fill_rect, fb_line, fb_frame) + an additive red_glow so the
   sockets bleed light from within, like a thing pressed against the back of the glass.

   The public API and bounding box are unchanged: skull_width_px/height_px still report
   28*ch_w x 11*ch_h, so every existing call site (centre-piece, corner glimpses, font-scaled
   world-catch growth, faint margin watermarks) positions and scales exactly as before -- only
   the art inside the box is new. `color` tints the bone; rim + eye-glow brightness scale with
   how bright `color` is, so faint watermark tints stay faint and bright reveals pop. `frame`
   drives the eye-glow pulse and a small jaw chatter so frame-cycling callers still animate. */
#include "app.h"
#include <string.h>
#include <math.h>

#ifndef SK_PI
#define SK_PI 3.14159265358979
#endif

int skull_height_px(Framebuffer *fb){ return 11 * fb->ch_h; }
int skull_width_px(Framebuffer *fb){ return 28 * fb->ch_w; }

/* solid axis-aligned ellipse via horizontal spans (clipped by fb_fill_rect) */
static void fill_ellipse(Framebuffer *fb, int cx, int cy, int rx, int ry, uint32_t c){
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    for (int dy = -ry; dy <= ry; dy++){
        double t = (double)dy / ry;
        double w = 1.0 - t*t; if (w < 0) w = 0;
        int dx = (int)(rx * sqrt(w));
        fb_fill_rect(fb, cx - dx, cy + dy, 2*dx + 1, 1, c);
    }
}

/* ellipse outline as a fan of short line segments (rim light) */
static void ellipse_rim(Framebuffer *fb, int cx, int cy, int rx, int ry, uint32_t c){
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    int N = 40, px = cx + rx, py = cy;
    for (int i = 1; i <= N; i++){
        double a = 2.0*SK_PI*i/N;
        int x = cx + (int)(rx*cos(a));
        int y = cy + (int)(ry*sin(a));
        fb_line(fb, px, py, x, y, c);
        px = x; py = y;
    }
}

/* a short jagged fracture walking from (x,y) in direction ang (stable per call) */
static void hairline(Framebuffer *fb, unsigned seed, int x, int y, double ang, int segs, int len, uint32_t c){
    for (int s = 0; s < segs; s++){
        seed = seed*1664525u + 1013904223u;
        ang += (((seed >> 8) & 255) / 255.0 - 0.5) * 1.0;
        int nx = x + (int)(cos(ang) * (len / (double)segs));
        int ny = y + (int)(sin(ang) * (len / (double)segs));
        fb_line(fb, x, y, nx, ny, c);
        x = nx; y = ny;
    }
}

void skull_render(Framebuffer *fb, int cx, int cy, int frame, uint32_t color){
    int W = skull_width_px(fb), H = skull_height_px(fb);
    frame = ((unsigned)frame) & 0x3FFFFFFF;

    /* brightness of the requested tint drives rim + glow intensity, so a faint 0x00351515
       watermark stays faint while a COL_RED / white reveal blazes */
    int cr = (color>>16)&0xFF, cg = (color>>8)&0xFF, cb = color&0xFF;
    int lum = cr; if (cg > lum) lum = cg; if (cb > lum) lum = cb;
    double bf = lum / 255.0;

    uint32_t bone = color;                                   /* dark bone fill */
    uint32_t rim  = RGB32((int)(120 + 110*bf), (int)(14*bf), (int)(20*bf));  /* red-lit edge */

    double pulse = 0.5 + 0.5*sin(frame * 0.20);
    int jaw_open = (int)(H * 0.03 * (0.5 + 0.5*sin(frame * 0.16)));

    /* ---- proportions inside the WxH box ---- */
    int crx = (int)(W*0.34), cry = (int)(H*0.30);
    int cyc = cy - (int)(H*0.12);                            /* cranium centre */
    int mouth_y = cy + (int)(H*0.20) + jaw_open;             /* mouth line */

    /* ---- cranium + upper face (one rounded mass) ---- */
    fill_ellipse(fb, cx, cyc, crx, cry, bone);
    /* cheekbones flare the face a touch wider just under the eyes */
    fill_ellipse(fb, cx, cy - (int)(H*0.02), (int)(W*0.30), (int)(H*0.16), bone);

    /* ---- mandible: a rounded trapezoid hanging below the mouth gap ---- */
    {
        int jt = mouth_y + (int)(H*0.02);
        int jb = cy + (int)(H*0.40) + jaw_open;
        int htop = (int)(W*0.22), hbot = (int)(W*0.14);
        for (int y = jt; y <= jb; y++){
            double f = (jb > jt) ? (double)(y - jt)/(jb - jt) : 0;
            int hw = htop + (int)((hbot - htop)*f);
            fb_fill_rect(fb, cx - hw, y, 2*hw + 1, 1, bone);
        }
    }

    /* ---- rim light on the silhouette ---- */
    ellipse_rim(fb, cx, cyc, crx, cry, rim);

    /* ---- temporal fractures (it has been on disk a long time) ---- */
    if (bf > 0.15){
        hairline(fb, 0x51u ^ (unsigned)W, cx - (int)(crx*0.5), cyc - (int)(cry*0.5),
                 -2.3, 5, (int)(W*0.22), rim);
        hairline(fb, 0xC3u ^ (unsigned)H, cx + (int)(crx*0.3), cyc - (int)(cry*0.7),
                 -1.0, 4, (int)(W*0.16), rim);
    }

    /* ---- eye sockets: deep hollows, backlit so they glow red from within ---- */
    int ex = (int)(W*0.17), ey = cyc + (int)(cry*0.18);
    int erx = (int)(W*0.13), ery = (int)(H*0.10);
    uint32_t hollow = RGB32((int)(12*bf), 0, (int)(4*bf));
    fill_ellipse(fb, cx - ex, ey, erx, ery, hollow);
    fill_ellipse(fb, cx + ex, ey, erx, ery, hollow);
    int gstr = (int)((26 + 42*pulse) * bf);
    if (gstr > 0){
        int grad = erx + (int)(erx*0.6);
        red_glow(fb, cx - ex, ey, grad, gstr);
        red_glow(fb, cx + ex, ey, grad, gstr);
    }
    ellipse_rim(fb, cx - ex, ey, erx, ery, rim);
    ellipse_rim(fb, cx + ex, ey, erx, ery, rim);

    /* ---- nasal cavity: inverted triangle ---- */
    {
        int nx = cx, ny = ey + (int)(ery*1.4);
        int nb = ny + (int)(H*0.08);
        int nhw = (int)(W*0.05);
        fb_line(fb, nx - nhw, ny, nx, nb, hollow);
        fb_line(fb, nx + nhw, ny, nx, nb, hollow);
        fb_line(fb, nx - nhw, ny, nx + nhw, ny, hollow);
        for (int y = ny; y <= nb; y++){
            double f = (double)(y - ny)/(nb - ny + 1);
            int hw = (int)(nhw*(1.0 - f));
            fb_fill_rect(fb, nx - hw, y, 2*hw + 1, 1, hollow);
        }
    }

    /* ---- mouth + teeth: a dark gap bridged by vertical bone bars ---- */
    {
        int mw = (int)(W*0.20);
        int mh = (int)(H*0.06) + jaw_open;
        fb_fill_rect(fb, cx - mw, mouth_y, 2*mw, mh, hollow);
        int nteeth = 7, tw = (2*mw) / nteeth;
        if (tw < 1) tw = 1;
        for (int i = 0; i <= nteeth; i++){
            int tx = cx - mw + i*tw;
            fb_fill_rect(fb, tx, mouth_y, (tw > 2 ? tw/2 : 1), mh, bone);
        }
    }
}
