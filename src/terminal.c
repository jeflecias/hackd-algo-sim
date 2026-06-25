/* terminal.c - software terminal: scrollback, input, typewriter queue, render */
#include "app.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#define MARGIN 24

static void add_line(Terminal *t, uint32_t color, const char *s){
    int idx;
    if (t->count < TERM_SCROLLBK){
        idx = (t->head + t->count) % TERM_SCROLLBK;
        t->count++;
    } else {
        idx = t->head;
        t->head = (t->head + 1) % TERM_SCROLLBK;
    }
    strncpy(t->lines[idx].text, s, TERM_MAXLINE - 1);
    t->lines[idx].text[TERM_MAXLINE - 1] = 0;
    t->lines[idx].color = color;
}

void term_init(Terminal *t){
    memset(t, 0, sizeof(*t));
    t->caret_on = 1;
}

void term_clear(Terminal *t){
    t->count = 0; t->head = 0;
    t->pend_head = t->pend_tail = 0;
    t->pend_timer = 0;
}

void term_print(Terminal *t, uint32_t color, const char *fmt, ...){
    char buf[TERM_MAXLINE];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    /* split on embedded newlines */
    char *p = buf, *nl;
    while ((nl = strchr(p, '\n')) != NULL){
        *nl = 0; add_line(t, color, p); p = nl + 1;
    }
    add_line(t, color, p);
}

void term_queue(Terminal *t, int delay_ms, uint32_t color, const char *fmt, ...){
    char buf[TERM_MAXLINE];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    int nxt = (t->pend_tail + 1) % TERM_PENDMAX;
    if (nxt == t->pend_head) return; /* full, drop */
    strncpy(t->pend[t->pend_tail].text, buf, TERM_MAXLINE - 1);
    t->pend[t->pend_tail].text[TERM_MAXLINE-1] = 0;
    t->pend[t->pend_tail].color = color;
    t->pend[t->pend_tail].delay_ms = delay_ms;
    t->pend_tail = nxt;
    if (t->pend_head == t->pend_tail) {} /* noop */
}

int term_busy(Terminal *t){
    return t->pend_head != t->pend_tail;
}

void term_update(App *a, double dt){
    Terminal *t = &a->term;
    /* drain pending queue with per-line delays */
    if (t->pend_head != t->pend_tail){
        t->pend_timer -= dt;
        int guard = 0;
        while (t->pend_head != t->pend_tail && t->pend_timer <= 0 && guard < 64){
            PendLine *pl = &t->pend[t->pend_head];
            add_line(t, pl->color, pl->text);
            if (pl->text[0]) audio_sfx(SFX_BOOT, 0);   /* data-chatter blip per line */
            t->pend_head = (t->pend_head + 1) % TERM_PENDMAX;
            if (t->pend_head != t->pend_tail)
                t->pend_timer += t->pend[t->pend_head].delay_ms;
            guard++;
        }
        a->busy_anim = (t->pend_head != t->pend_tail);
    } else {
        a->busy_anim = 0;
    }
    /* caret blink */
    t->caret_timer += dt;
    if (t->caret_timer > 450){ t->caret_timer = 0; t->caret_on = !t->caret_on; }
}

static const char *PROMPT = "root@kali:~# ";

/* a jagged crack walking outward from (x,y), with optional corruption bleed */
static void crack_from(App *a, uint64_t *r, int x, int y, double ang, int segs,
                       int len, uint32_t col, int bleed){
    Framebuffer *fb = &a->fb;
    for (int s = 0; s < segs; s++){
        ang += ((double)(rng_next(r) % 100) / 100.0 - 0.5) * 1.1;
        int nx = x + (int)(cos(ang) * (len / (double)segs));
        int ny = y + (int)(sin(ang) * (len / (double)segs));
        fb_line(fb, x, y, nx, ny, col);
        if (bleed && (rng_next(r) & 3) == 0)
            gfx_datamosh(fb, &a->rng, nx-8, ny-8, 16, 16);   /* dark void bleeds from the crack */
        x = nx; y = ny;
        if (x < 0 || x >= fb->w || y < 0 || y >= fb->h) break;
    }
}

/* The black background fractures as lives are lost: cracks branch in from the edges,
   glass-shards radiate from impact points, the parasite peeks through ever wider, and on
   the LAST life a hand claws out. Drawn BEFORE the text so command output stays readable
   (wreck the chrome, spare the data).  rot: 1 = a life lost, 2 = last life. */
static void draw_cracks_and_peek(App *a, int rot){
    if (rot < 1) return;
    Framebuffer *fb = &a->fb;

    /* seed stable per ~3s window so cracks don't strobe every frame */
    uint64_t r = (0x9E3779B97F4A7C15ull ^ (uint64_t)(a->now_ms / 3000.0)) | 1;
    uint32_t base = rot >= 2 ? 0x00C00018u : 0x00480010u;

    /* edge cracks (subtle at rot 1, dense at rot 2) */
    int ncracks = rot * 4;
    for (int i = 0; i < ncracks; i++){
        int edge = rng_range(&r, 0, 3), x, y;
        switch (edge){
            case 0:  x = rng_range(&r, 0, fb->w-1); y = 0;           break;
            case 1:  x = rng_range(&r, 0, fb->w-1); y = fb->h-1;     break;
            case 2:  x = 0;          y = rng_range(&r, 0, fb->h-1);  break;
            default: x = fb->w-1;    y = rng_range(&r, 0, fb->h-1);  break;
        }
        double ang = atan2(fb->h/2.0 - y, fb->w/2.0 - x);    /* drift toward center */
        crack_from(a, &r, x, y, ang, 4 + rot*2 + (int)(rng_next(&r)%4), 60 + rot*50, base, rot>=2);
    }

    /* impact-point shards: one (subtle) at rot 1, two (obvious) at rot 2 */
    int nimp = rot >= 2 ? 2 : 1;
    for (int m = 0; m < nimp; m++){
        int ix = rng_range(&r, fb->w/5, fb->w*4/5);
        int iy = rng_range(&r, fb->h/5, fb->h*4/5);
        red_glow(fb, ix, iy, 50 + rot*70, 20 + rot*40);      /* light bleeds through the break */
        int spokes = 5 + rot*3;
        for (int s = 0; s < spokes; s++){
            double ang = (2*3.14159265*s)/spokes + (rng_next(&r)%100)/300.0;
            crack_from(a, &r, ix, iy, ang, 3 + rot*2, 40 + rot*55, base, rot>=2);
        }
    }

    /* the parasite peeks through, more and more */
    int frame = (int)(a->now_ms / 140);
    if (rot == 1){
        /* subtle: a pair of eyes glinting deep behind a crack near an edge */
        int ex = (((int)(a->now_ms/3700))&1) ? fb->w/6 : fb->w*5/6;
        int ey = fb->h/4;
        red_glow(fb, ex, ey, 60, 30);
        fb_fill_rect(fb, ex-18, ey, 8, 6, 0x00C01018u);
        fb_fill_rect(fb, ex+10, ey, 8, 6, 0x00C01018u);
    } else {
        /* last life: a large pulsing skull behind the glass + a hand clawing out */
        double pulse = 0.5 + 0.5*sin(a->now_ms/120.0);
        red_glow(fb, fb->w/2, fb->h/2, fb->h/3, 30 + (int)(40*pulse));
        int v = 0x60 + (int)(0x9F*pulse);
        skull_render(fb, fb->w/2, fb->h/2, frame, RGB32(v, 0, v/6));
        /* the hand reaches out of the central break, in and out */
        double reach = 0.5 + 0.5*sin(a->now_ms/700.0);
        draw_reaching_hand(fb, fb->w/2, fb->h/2 + fb->h/12, reach, 0x00120406u);
        /* corner glimpses too, so it feels surrounded */
        int sw = skull_width_px(fb), sh = skull_height_px(fb);
        int slot = (int)(a->now_ms/2500)&3, cx, cy;
        switch (slot){
            case 0:  cx = sw/3;         cy = sh/2;         break;
            case 1:  cx = fb->w - sw/3; cy = sh/2;         break;
            case 2:  cx = sw/3;         cy = fb->h - sh/2; break;
            default: cx = fb->w - sw/3; cy = fb->h - sh/2; break;
        }
        skull_render(fb, cx, cy, frame, 0x00500010u);
    }
}

/* a tapered "bone": a limb segment that thins from w0 to w1 half-width, filled by stacking
   perpendicular spans (no polygon fill needed) and edged with a bright rim. The building
   block for the gaunt reaching hand's fingers and forearm. */
static void taper_limb(Framebuffer *fb, double x0, double y0, double x1, double y1,
                       double w0, double w1, uint32_t fill, uint32_t rim){
    double dx = x1-x0, dy = y1-y0;
    double len = sqrt(dx*dx + dy*dy); if (len < 1) len = 1;
    double nx = -dy/len, ny = dx/len;                        /* unit perpendicular */
    int steps = (int)len;
    for (int i = 0; i <= steps; i++){
        double t = (double)i/steps;
        double mx = x0 + dx*t, my = y0 + dy*t;
        double hw = w0 + (w1-w0)*t; if (hw < 0.5) hw = 0.5;
        fb_line(fb, (int)(mx-nx*hw), (int)(my-ny*hw), (int)(mx+nx*hw), (int)(my+ny*hw), fill);
    }
    fb_line(fb, (int)(x0-nx*w0), (int)(y0-ny*w0), (int)(x1-nx*w1), (int)(y1-ny*w1), rim);
    fb_line(fb, (int)(x0+nx*w0), (int)(y0+ny*w0), (int)(x1+nx*w1), (int)(y1+ny*w1), rim);
}

/* The thing behind the glass forces a gaunt hand through the break and gropes for a frame.
   A backlit silhouette (dark fill + red rim, seated in red_glow) rather than a blocky glove:
   a knuckled palm, five fanned fingers that curl when `reach` is low and splay into claws
   when it's high, and shards of broken screen radiating from the wrist. `reach` 0..1 drives
   both extension and curl (animate it to make the hand grasp in and out). */
void draw_reaching_hand(Framebuffer *fb, int cx, int cy, double reach, uint32_t color){
    if (reach < 0) reach = 0;
    if (reach > 1) reach = 1;
    int s = fb->h / 22; if (s < 8) s = 8;
    uint32_t rim = 0x00B00014u;                              /* bright red rim */
    const double PI = 3.14159265358979;
    double curl = 1.0 - reach;                               /* low reach -> fingers curl in */

    static unsigned tick = 0; tick++;                        /* self-contained time proxy */
    double tp = tick * 0.30;

    /* backlight: the break glows from behind so the hand reads as a silhouette */
    red_glow(fb, cx, cy - s*2, s*6, 24 + (int)(34*reach));

    /* broken-screen shards radiating from the wrist hole (stable per position, no strobe) */
    unsigned gseed = (unsigned)(cx*131 + cy*17 + 0x9E37u);
    for (int k = 0; k < 7; k++){
        gseed = gseed*1664525u + 1013904223u;
        double a = (2*PI*k)/7 + ((gseed>>9)&63)/255.0;
        int gl = s*2 + (int)((gseed>>4)&31)/8 * s;
        fb_line(fb, cx, cy + s, cx + (int)(cos(a)*gl), cy + s + (int)(sin(a)*gl), rim);
    }

    /* forearm rising into the palm (widens upward) */
    taper_limb(fb, cx, cy + s*3, cx, cy - s*0.4, s*1.5, s*2.1, color, rim);

    /* knuckled palm: stacked spans with a rounded profile + gnarled jitter */
    int ptop = cy - (int)(s*3.2), pbot = cy;
    unsigned pseed = gseed ^ 0xBEEF;
    for (int y = ptop; y <= pbot; y++){
        double t = (double)(y - ptop)/(pbot - ptop + 1);     /* 0 top .. 1 wrist */
        double hw = s*2.2 * (0.55 + 0.45*sin(t*PI*0.9 + 0.2));
        pseed = pseed*1103515245u + 12345u;
        hw += (((pseed>>10)&63)/63.0 - 0.5) * s*0.4;          /* gnarl the silhouette */
        if (hw < 1) hw = 1;
        fb_fill_rect(fb, cx - (int)hw, y, (int)(2*hw), 1, color);
        fb_fill_rect(fb, cx - (int)hw, y, 1, 1, rim);         /* ragged rim edges */
        fb_fill_rect(fb, cx + (int)hw, y, 1, 1, rim);
    }

    /* five fingers, fanned from the top of the palm; each has a knuckle bend */
    double rootY = ptop + s*0.2;
    /* index, middle, ring, little + thumb: splay angle, length multiplier */
    double splay[5] = { -0.62, -0.22, 0.18, 0.55, -1.15 };
    double lenm [5] = {  1.05,  1.30, 1.20, 0.85, 0.80 };
    double rootK[5] = { -1.5, -0.6, 0.6, 1.5, -2.1 };        /* root x offset in units of s */
    for (int i = 0; i < 5; i++){
        double rx = cx + rootK[i]*s;
        double ry = rootY + (i==4 ? s*1.6 : 0);              /* thumb roots lower on the side */
        double baseDir = -PI/2 + splay[i]*0.7;
        double L = s*(2.0 + 2.4*lenm[i]) + reach*s*3.2;
        double pl = L*0.55, dl = L*0.5;
        double bend = curl*0.95 + 0.12;                      /* curl the distal segment forward */
        if (i==4) bend += 0.3;
        double mx = rx + cos(baseDir)*pl;
        double my = ry + sin(baseDir)*pl;
        double ddir = baseDir + bend;
        double trem = sin(tp + i*1.7) * s*0.18;              /* faint twitch */
        double tx = mx + cos(ddir)*dl + trem;
        double ty = my + sin(ddir)*dl;
        taper_limb(fb, rx, ry, mx, my, s*0.85, s*0.62, color, rim);
        taper_limb(fb, mx, my, tx, ty, s*0.62, s*0.16, color, rim);
        fb_fill_rect(fb, (int)tx - 1, (int)ty - 1, 3, 3, rim);   /* claw tip */
    }
}

void term_render(App *a){
    Framebuffer *fb = &a->fb;
    Terminal *t = &a->term;
    fb_clear(fb, COL_BG);

    /* the shell rots as lives are consumed in the other world (0..3) */
    int rot = 3 - a->lives; if (rot < 0) rot = 0; if (rot > 3) rot = 3;
    draw_cracks_and_peek(a, rot);

    /* dread-ramp: in the final stretch before a scheduled scare, a faint skull surfaces
       from behind the glass and the glow swells. Drawn BEFORE the text so the data stays
       readable (wreck the chrome, spare the data). */
    {
        double tts = a->scare_at - a->now_ms;
        if (rot < 2 && tts >= 0 && tts < 3000.0){    /* last life already has a permanent skull */
            double k = 1.0 - tts/3000.0;                    /* 0 -> 1 as the scare nears */
            red_glow(fb, fb->w/2, fb->h/2, (int)(fb->h/4 * k), (int)(22*k));
            int v = (int)(0x18 + 0x40*k);
            skull_render(fb, fb->w/2, fb->h/2, (int)(a->now_ms/150), RGB32(v, 0, v/5));
        }
    }

    int ch = fb->ch_h;
    int status_h = ch + 10;
    int input_y  = fb->h - status_h - ch - 6;
    int vis_rows = (input_y - MARGIN) / ch;
    if (vis_rows < 1) vis_rows = 1;

    int start = t->count > vis_rows ? t->count - vis_rows : 0;
    int y = MARGIN;
    for (int i = start; i < t->count; i++){
        int idx = (t->head + i) % TERM_SCROLLBK;
        /* rarely corrupt a line's glyphs for a frame (the parasite bleeds through);
           the corruption spreads as lives are lost */
        if ((rng_next(&a->rng) & 1023) < 5 + rot*45){
            char g[TERM_MAXLINE];
            fb_garble(g, t->lines[idx].text, &a->rng, 18 + rot*16);
            fb_text(fb, MARGIN, y, g, COL_RED);
        } else {
            fb_text(fb, MARGIN, y, t->lines[idx].text, t->lines[idx].color);
        }
        y += ch;
    }

    /* input line */
    if (!a->busy_anim){
        char buf[TERM_MAXLINE];
        snprintf(buf, sizeof(buf), "%s%s", PROMPT, t->input);
        fb_text(fb, MARGIN, input_y, buf, COL_GREEN);
        if (t->caret_on){
            int cx = MARGIN + (int)strlen(buf) * fb->ch_w;
            fb_fill_rect(fb, cx, input_y, fb->ch_w, ch - 2, COL_GREEN);
        }
    } else {
        if (t->caret_on)
            fb_text(fb, MARGIN, input_y, "  ...processing...", COL_DGREEN);
    }

    /* status bar */
    int sy = fb->h - status_h + 4;
    fb_fill_rect(fb, 0, fb->h - status_h, fb->w, status_h, 0x00101810);
    char sb[256];
    snprintf(sb, sizeof(sb),
        " [ DEADLOCK v4.7 ]  modules:4-7 loaded  swapped-out:%d  lives:%d  type 'help'  |  ESC=panic-exit",
        a->kills, a->lives);
    fb_text(fb, 8, sy, sb, COL_DGREEN);
    /* live-intrusion HUD on the right */
    int cw = fb->ch_w;
    char ex[40]; snprintf(ex, sizeof(ex), "EXFIL %lu KB", (unsigned long)(a->now_ms * 0.131));
    fb_text(fb, fb->w - cw*30, sy, ex, COL_DGREEN);
    if (t->caret_on){
        fb_fill_rect(fb, fb->w - cw*14, sy+3, cw, cw, COL_RED);
        fb_text(fb, fb->w - cw*12, sy, "LIVE INTRUSION", COL_RED);
    }

    /* dread-ramp lifts the chrome decay slightly in the final approach (data stays drawn
       above this; these are signal-level effects) */
    double tts_fx = a->scare_at - a->now_ms;
    int ramp = (tts_fx >= 0 && tts_fx < RAMP_MS) ? (int)(3 * (1.0 - tts_fx/RAMP_MS)) : 0;

    /* occasional power flicker / darkness pulse */
    if ((rng_next(&a->rng) & 1023) < 4 + ramp*2) gfx_brightness(fb, 55 + (int)(rng_next(&a->rng)%30));
    /* lives-scaled decay: signal can't hold as the resident pushes through */
    if ((rot + ramp) >= 1 && (int)(rng_next(&a->rng) & 7) < rot + ramp) gfx_rgb_split(fb, rot + ramp);
    if (rot >= 2) gfx_slice_tear(fb, &a->rng, 10 + rot*8, rot);
    if (rot >= 2 && (int)(rng_next(&a->rng) & 31) < rot) gfx_datamosh(fb, &a->rng, 0, 0, fb->w, fb->h);
    /* analog grade: subtle film grain + a CRT vignette darkening the margins (centre, where
       the data lives, stays bright) + scanlines */
    gfx_grain(fb, &a->rng, 7);
    gfx_scanlines(fb, 88);
    gfx_vignette(fb);
}

/* ----------------- input handling ----------------- */

static void push_history(Terminal *t, const char *line){
    if (!line[0]) return;
    if (t->hist_count > 0 &&
        strcmp(t->hist[(t->hist_count-1)%32], line) == 0){ t->hist_pos = t->hist_count; return; }
    strncpy(t->hist[t->hist_count % 32], line, TERM_INPUTMAX-1);
    t->hist[t->hist_count % 32][TERM_INPUTMAX-1] = 0;
    t->hist_count++;
    t->hist_pos = t->hist_count;
}

void term_key_char(App *a, char c){
    Terminal *t = &a->term;
    if (a->busy_anim) return;
    if (c >= 32 && c < 127 && t->inlen < TERM_INPUTMAX - 1){
        t->input[t->inlen++] = c;
        t->input[t->inlen] = 0;
        audio_sfx(SFX_KEY, 0);
    }
}

void term_key_special(App *a, int vk){
    Terminal *t = &a->term;
    if (a->busy_anim) return;
    switch (vk){
    case VK_BACK:
        if (t->inlen > 0) t->input[--t->inlen] = 0;
        break;
    case VK_RETURN: {
        char line[TERM_INPUTMAX];
        strncpy(line, t->input, sizeof(line));
        line[sizeof(line)-1] = 0;
        /* echo the command at the prompt */
        term_print(t, COL_GREEN, "%s%s", PROMPT, line);
        push_history(t, line);
        t->input[0] = 0; t->inlen = 0;
        cmd_execute(a, line);
        break; }
    case VK_UP:
        if (t->hist_count > 0 && t->hist_pos > 0){
            t->hist_pos--;
            strncpy(t->input, t->hist[t->hist_pos % 32], TERM_INPUTMAX-1);
            t->inlen = (int)strlen(t->input);
        }
        break;
    case VK_DOWN:
        if (t->hist_pos < t->hist_count){
            t->hist_pos++;
            if (t->hist_pos == t->hist_count){ t->input[0]=0; t->inlen=0; }
            else { strncpy(t->input, t->hist[t->hist_pos % 32], TERM_INPUTMAX-1);
                   t->inlen = (int)strlen(t->input); }
        }
        break;
    }
}
