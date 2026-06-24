/* terminal.c - software terminal: scrollback, input, typewriter queue, render */
#include "app.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

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

void term_render(App *a){
    Framebuffer *fb = &a->fb;
    Terminal *t = &a->term;
    fb_clear(fb, COL_BG);

    /* the shell rots as lives are consumed in the other world (0..3) */
    int rot = 3 - a->lives; if (rot < 0) rot = 0; if (rot > 3) rot = 3;

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
