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

/* soft additive red glow (the thing behind the screen, light bleeding through a break) */
static void red_glow(Framebuffer *fb, int cx, int cy, int rad, int strength){
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
            int f = strength * (r2 - d2) / r2;               /* bright center -> 0 at rim */
            uint32_t p = row[x];
            int rr = ((p>>16)&0xFF) + f;        if (rr>255) rr=255;
            int gg = ((p>>8)&0xFF)  + f/8;      if (gg>255) gg=255;
            int bb = (p&0xFF)       + f/12;     if (bb>255) bb=255;
            row[x] = RGB32(rr,gg,bb);
        }
    }
}

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

/* a procedural silhouette hand clawing out through the broken screen. Built from solid
   blocks with a bright red rim so it reads against the glow behind it. `reach` 0..1
   extends the fingers (animate it to make the hand grasp in and out). */
void draw_reaching_hand(Framebuffer *fb, int cx, int cy, double reach, uint32_t color){
    if (reach < 0) reach = 0;
    if (reach > 1) reach = 1;
    int s = fb->h / 22; if (s < 8) s = 8;
    uint32_t rim = 0x00B00014u;                              /* bright red rim */

    int palmw = s*5, palmh = s*5;
    int px = cx - palmw/2, py = cy;
    /* wrist */
    fb_fill_rect(fb, cx - s*2, py+palmh - s, s*4, s*4, color);
    fb_frame   (fb, cx - s*2, py+palmh - s, s*4, s*4, rim);
    /* palm */
    fb_fill_rect(fb, px, py, palmw, palmh, color);
    fb_frame   (fb, px, py, palmw, palmh, rim);
    /* four fingers reaching up (out of the screen) */
    int reachpx = (int)(s*5 * reach);
    int fw = s, gap = s/2;
    int fx = px + gap;
    int lengths[4] = { s*4 + s, s*4 + s*2, s*4 + s*2, s*4 + s };  /* middle two longest */
    for (int i = 0; i < 4; i++){
        int flen = lengths[i] + reachpx;
        int fy = py - flen;
        fb_fill_rect(fb, fx, fy, fw, flen + s, color);      /* overlaps into the palm */
        fb_frame   (fb, fx, fy, fw, flen, rim);
        fb_fill_rect(fb, fx, fy - s/2, fw, s/2, rim);       /* claw tip */
        fx += fw + gap;
    }
    /* thumb, off the side */
    int tx = px - s*2, ty = py + s;
    fb_fill_rect(fb, tx, ty, s*2 + s/2, s*2, color);
    fb_frame   (fb, tx, ty, s*2 + s/2, s*2, rim);
    fb_fill_rect(fb, tx - s/2, ty, s/2, s*2, rim);          /* thumb claw */
}

void term_render(App *a){
    Framebuffer *fb = &a->fb;
    Terminal *t = &a->term;
    fb_clear(fb, COL_BG);

    /* the shell rots as lives are consumed in the other world (0..3) */
    int rot = 3 - a->lives; if (rot < 0) rot = 0; if (rot > 3) rot = 3;
    draw_cracks_and_peek(a, rot);

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
        " [ DEADLOCK v6.6.6 ]  modules:4-7 loaded  victims:%d  lives:%d  type 'help'  |  ESC=panic-exit",
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

    /* occasional power flicker / darkness pulse */
    if ((rng_next(&a->rng) & 1023) < 4) gfx_brightness(fb, 55 + (int)(rng_next(&a->rng)%30));
    /* lives-scaled decay: signal can't hold as the parasite wins */
    if (rot >= 1 && (int)(rng_next(&a->rng) & 7) < rot) gfx_rgb_split(fb, rot);
    if (rot >= 2) gfx_slice_tear(fb, &a->rng, 10 + rot*8, rot);
    if (rot >= 2 && (int)(rng_next(&a->rng) & 31) < rot) gfx_datamosh(fb, &a->rng, 0, 0, fb->w, fb->h);
    /* subtle scanlines for CRT feel */
    gfx_scanlines(fb, 88);
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
