/* jumpscare.c - random skull interrupt + 30s easy OS puzzle, then resume */
#include "app.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

#define SKULL_PHASE_MS  1600.0
#define RESULT_PHASE_MS 1700.0
#define PUZZLE_MS       15000.0      /* 15s solve window */

static int    s_stung = 0;   /* has the silence->skull sting fired for this scare? */
static double s_hb    = 0;   /* heartbeat timer (ms) - accelerates as time runs out */

/* normalize: keep [0-9a-z], lowercase, and DROP 'p' so process answers typed the way
   the prompt shows them work: "P1 P2 P3"->"123", "P2"->"2" (canonical answers are digits
   or y/n, none contain 'p', so this is safe). */
static void normalize(char *out, const char *in){
    int j = 0;
    for (int i = 0; in[i] && j < 62; i++){
        char c = (char)tolower((unsigned char)in[i]);
        if (c == 'p') continue;
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')) out[j++] = c;
    }
    out[j] = 0;
}

void jumpscare_schedule(App *a){
    /* random 30-60s between scares */
    a->scare_at = a->now_ms + rng_range(&a->rng, 30000, 60000);
    a->scare_pending = 0;
    a->scare_ramped = 0;        /* arm the dread-ramp whisper for the next cycle */
}

/* ---- puzzle generators: fill question/hint and canonical answer ---- */
static void gen_puzzle(App *a){
    uint64_t *r = &a->rng;
    int kind = rng_range(r, 0, 6);
    char ans[64];
    switch (kind){
    case 0: { /* FCFS order by arrival of 3 procs */
        int at[3]; int id[3] = {1,2,3};
        at[0]=rng_range(r,0,2); at[1]=rng_range(r,3,5); at[2]=rng_range(r,6,8);
        /* shuffle display order */
        int o[3]={0,1,2};
        for (int i=2;i>0;i--){int k=rng_range(r,0,i);int tmp=o[i];o[i]=o[k];o[k]=tmp;}
        snprintf(a->scare.question, 256,
            "FCFS: P%d(AT%d)  P%d(AT%d)  P%d(AT%d).  Type the run order (e.g. 231 or P2 P3 P1):",
            id[o[0]],at[o[0]], id[o[1]],at[o[1]], id[o[2]],at[o[2]]);
        /* answer = ids sorted by arrival = 1 2 3 */
        snprintf(ans,64,"123");
        strcpy(a->scare.hint,"earliest arrival first; type the 3 process numbers in order");
        break; }
    case 1: { /* SJF: which runs first */
        int b1=rng_range(r,4,9), b2=rng_range(r,1,3), b3=rng_range(r,5,9);
        snprintf(a->scare.question,256,
            "SJF (all arrive at 0): P1 burst %d, P2 burst %d, P3 burst %d. Which runs FIRST?",
            b1,b2,b3);
        int mn=b1,mi=1; if(b2<mn){mn=b2;mi=2;} if(b3<mn){mn=b3;mi=3;}
        snprintf(ans,64,"%d",mi);
        strcpy(a->scare.hint,"shortest burst first - answer the process number, e.g. 2 (or P2)");
        break; }
    case 2: { /* paging page number */
        int ps=4, la=rng_range(r,5,15);
        snprintf(a->scare.question,256,
            "Paging: page size = %d.  Logical address = %d.  Page NUMBER = ?", ps, la);
        snprintf(ans,64,"%d", la/ps);
        strcpy(a->scare.hint,"page = LA / page_size");
        break; }
    case 3: { /* paging offset */
        int ps=4, la=rng_range(r,5,15);
        snprintf(a->scare.question,256,
            "Paging: page size = %d.  Logical address = %d.  OFFSET = ?", ps, la);
        snprintf(ans,64,"%d", la%ps);
        strcpy(a->scare.hint,"offset = LA mod page_size");
        break; }
    case 4: { /* SSTF nearest */
        int h=rng_range(r,40,60);
        int q[3]; q[0]=h+rng_range(r,1,5); q[1]=h-rng_range(r,6,12); q[2]=h+rng_range(r,13,20);
        snprintf(a->scare.question,256,
            "SSTF: head at %d, requests {%d, %d, %d}. Which track is served FIRST?",
            h,q[0],q[1],q[2]);
        int best=q[0],bd=q[0]>h?q[0]-h:h-q[0];
        for(int i=1;i<3;i++){int d=q[i]>h?q[i]-h:h-q[i]; if(d<bd){bd=d;best=q[i];}}
        snprintf(ans,64,"%d",best);
        strcpy(a->scare.hint,"closest track to the head");
        break; }
    case 5: { /* disk total movement, 2 hops */
        int h=rng_range(r,30,60), x=rng_range(r,10,90), y=rng_range(r,10,90);
        int tot=(x>h?x-h:h-x)+(x>y?x-y:y-x);
        snprintf(a->scare.question,256,
            "Disk FCFS: head %d -> %d -> %d.  TOTAL head movement (tracks) = ?", h,x,y);
        snprintf(ans,64,"%d",tot);
        strcpy(a->scare.hint,"|h-x| + |x-y|");
        break; }
    default: { /* FIFO fault y/n */
        snprintf(a->scare.question,256,
            "FIFO, 3 frames empty. Refs so far: 1 2 3 1.  Is reference #4 (the '1') a page fault? (y/n)");
        snprintf(ans,64,"n");
        strcpy(a->scare.hint,"1 is already in a frame");
        break; }
    }
    normalize(a->scare.answer, ans);
}

void jumpscare_trigger(App *a){
    a->state = ST_JUMPSCARE;
    a->state_time = 0;
    a->scare.phase = 0;
    a->scare.phase_time = 0;
    a->scare.time_left = PUZZLE_MS;
    a->scare.input[0] = 0; a->scare.inlen = 0;
    a->scare.result = 0;
    gen_puzzle(a);
    /* sudden silence, THEN the sting (fired in update once the quiet lands) */
    audio_silence(280);
    gfx_phosphor_reset(&a->fb);
    s_stung = 0;
    s_hb = 0;
}

void jumpscare_update(App *a, double dt){
    a->scare.phase_time += dt;
    if (a->scare.phase == 0){
        if (!s_stung && a->scare.phase_time > 280){ audio_sfx(SFX_SKULL, 0); s_stung = 1; }
        if (a->scare.phase_time > SKULL_PHASE_MS){ a->scare.phase = 1; a->scare.phase_time = 0; }
    } else if (a->scare.phase == 1){
        a->scare.time_left -= dt;
        /* heartbeat that quickens as the clock runs out (≈900ms early -> ≈250ms at zero) */
        s_hb -= dt;
        if (s_hb <= 0){
            audio_sfx(SFX_HEART, 0);
            double frac = a->scare.time_left / PUZZLE_MS; if (frac < 0) frac = 0;
            s_hb = 250.0 + 650.0 * frac;
        }
        if (a->scare.time_left <= 0){
            a->scare.time_left = 0;
            a->scare.result = 0;
            a->kills += 1;
            strcpy(a->scare.res1, "TOO SLOW.");
            snprintf(a->scare.res2, 64, "%s", story_fail_line(a));
            a->scare.phase = 2; a->scare.phase_time = 0;
            audio_sfx(SFX_WRONG, 0);
        }
    } else { /* phase 2: result */
        if (a->scare.phase_time > RESULT_PHASE_MS){
            if (a->scare.result){
                a->state = ST_TERMINAL;
                a->state_time = 0;
                jumpscare_schedule(a);
                term_print(&a->term, COL_GREEN, "[survived the skull -- continue]");
            } else {
                /* failure drags you out of the machine and into the other world */
                fourthwall_enter(a);
            }
        }
    }
}

static void submit_answer(App *a){
    char norm[64];
    normalize(norm, a->scare.input);
    int ok = 0;
    const char *exp = a->scare.answer;
    if (exp[0] && !exp[1] && (exp[0]<'0'||exp[0]>'9')){
        /* single-letter (y/n) answer: match first letter */
        ok = (norm[0] == exp[0]);
    } else {
        ok = (strcmp(norm, exp) == 0);
    }
    a->scare.result = ok;
    if (ok){ a->kills += 0; strcpy(a->scare.res1,"CORRECT."); strcpy(a->scare.res2,"access restored.");
             audio_sfx(SFX_CORRECT, 0); }
    else   { a->kills += 1; strcpy(a->scare.res1,"WRONG.");
             snprintf(a->scare.res2,64,"answer was: %s", a->scare.answer);
             audio_sfx(SFX_WRONG, 0); }
    a->scare.phase = 2; a->scare.phase_time = 0;
}

void jumpscare_key_char(App *a, char c){
    if (a->scare.phase != 1) return;
    if (c >= 32 && c < 127 && a->scare.inlen < 62){
        a->scare.input[a->scare.inlen++] = c;
        a->scare.input[a->scare.inlen] = 0;
    }
}

void jumpscare_key_special(App *a, int vk){
    if (a->scare.phase != 1) return;
    if (vk == VK_BACK && a->scare.inlen > 0) a->scare.input[--a->scare.inlen] = 0;
    else if (vk == VK_RETURN) submit_answer(a);
}

void jumpscare_render(App *a){
    Framebuffer *fb = &a->fb;
    fb_clear(fb, COL_BG);

    if (a->scare.phase == 0){
        int frame = (int)(a->scare.phase_time / 110);
        uint32_t col = (frame & 1) ? COL_RED : COL_WHITE;
        skull_render(fb, fb->w/2, fb->h/2 - fb->ch_h*2, frame, col);
        const char *m = "!!  I N T E R R U P T  !!";
        fb_text(fb, fb->w/2 - (int)strlen(m)*fb->ch_w/2, fb->h/2 + skull_height_px(fb)/2, m, COL_RED);
        if ((rng_next(&a->rng) & 1)) gfx_slice_tear(fb, &a->rng, 40, 4);
        gfx_rgb_split(fb, 4 + (rng_next(&a->rng)%6));
        gfx_datamosh(fb, &a->rng, 0, 0, fb->w, fb->h);            /* heavy corruption */
        if ((rng_next(&a->rng) & 7) == 0) gfx_invert_band(fb, 0, fb->h); /* full-frame invert flash */
        gfx_phosphor(fb, 70);                                    /* searing CRT trails */
        gfx_scanlines(fb, 80);
        gfx_vignette(fb);
        return;
    }

    if (a->scare.phase == 1){
        /* small skull top corner */
        skull_render(fb, fb->w - skull_width_px(fb)/2 - 20, skull_height_px(fb)/2 + 20,
                     (int)(a->scare.phase_time/150), COL_RED);
        int y = fb->h/3;
        const char *t1 = "SOLVE OR YIELD";
        fb_text(fb, fb->w/2 - (int)strlen(t1)*fb->ch_w/2, y - fb->ch_h*2, t1, COL_RED);
        fb_text(fb, 60, y, a->scare.question, COL_GREEN);
        char hint[160]; snprintf(hint,160,"(hint: %s)", a->scare.hint);
        fb_text(fb, 60, y + fb->ch_h, hint, COL_DGREEN);

        char line[128]; snprintf(line,128,"answer> %s", a->scare.input);
        fb_text(fb, 60, y + fb->ch_h*3, line, COL_CYAN);
        if (((int)(a->scare.phase_time/400)) & 1){
            int cx = 60 + (int)strlen(line)*fb->ch_w;
            fb_fill_rect(fb, cx, y + fb->ch_h*3, fb->ch_w, fb->ch_h-2, COL_CYAN);
        }

        /* countdown bar */
        double frac = a->scare.time_left / PUZZLE_MS;
        if (frac < 0) frac = 0;
        int barw = fb->w - 120;
        int secs = (int)(a->scare.time_left/1000.0 + 0.999);
        fb_fill_rect(fb, 60, y + fb->ch_h*5, barw, fb->ch_h, 0x00200000);
        fb_fill_rect(fb, 60, y + fb->ch_h*5, (int)(barw*frac), fb->ch_h, COL_RED);
        char tc[32]; snprintf(tc,32,"%2d s", secs);
        int lowblink = (frac < 0.34) && (((int)(a->scare.phase_time/150)) & 1);
        fb_text(fb, 60, y + fb->ch_h*6 + 4, tc,
                lowblink ? COL_WHITE : (frac < 0.34 ? COL_RED : COL_AMBER));

        /* urgency: a pulsing red border that thickens + beats faster as time drains */
        double urg = 1.0 - frac;
        double beat = 0.5 + 0.5*sin(a->scare.phase_time / (frac > 0.5 ? 260.0 : 130.0));
        int bt = (int)(5 + urg*20 + beat*urg*18);
        uint32_t rc = RGB32((int)(110 + 120*beat*urg), 0, 0);
        fb_fill_rect(fb, 0, 0, fb->w, bt, rc);
        fb_fill_rect(fb, 0, fb->h-bt, fb->w, bt, rc);
        fb_fill_rect(fb, 0, 0, bt, fb->h, rc);
        fb_fill_rect(fb, fb->w-bt, 0, bt, fb->h, rc);
        if (frac < 0.34){
            gfx_rgb_split(fb, 2 + (int)(urg*6));
            if ((rng_next(&a->rng) & 7) == 0) gfx_slice_tear(fb, &a->rng, 12, 2);
        }

        gfx_scanlines(fb, 86);
        return;
    }

    /* phase 2: result flash */
    uint32_t c = a->scare.result ? COL_GREEN : COL_RED;
    if (((int)(a->scare.phase_time/130)) & 1) c = COL_WHITE;
    fb_text(fb, fb->w/2 - (int)strlen(a->scare.res1)*fb->ch_w/2, fb->h/2 - fb->ch_h, a->scare.res1, c);
    fb_text(fb, fb->w/2 - (int)strlen(a->scare.res2)*fb->ch_w/2, fb->h/2 + fb->ch_h, a->scare.res2, COL_DGREEN);
    if (!a->scare.result && (rng_next(&a->rng)&1)) gfx_slice_tear(fb, &a->rng, 30, 3);
    gfx_scanlines(fb, 84);
    gfx_vignette(fb);
}
