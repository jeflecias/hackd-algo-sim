/* app.h - shared types and globals for DEADLOCK */
#ifndef APP_H
#define APP_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

/* ---- colors: stored as 0x00RRGGBB for the 32-bit DIB pixel buffer ---- */
#define RGB32(r,g,b) (((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|((uint32_t)(b)))
#define COL_BG      0x00050805u   /* near-black with green tint */
#define COL_GREEN   0x0000FF66u   /* phosphor green             */
#define COL_DGREEN  0x0000A040u   /* dim green                  */
#define COL_RED     0x00FF0033u   /* blood red                  */
#define COL_AMBER   0x00FFB000u
#define COL_GRAY    0x00708070u
#define COL_WHITE   0x00D8FFD8u
#define COL_CYAN    0x0000FFEEu

/* convert our 0xRRGGBB to a Windows COLORREF (0xBBGGRR) */
static inline COLORREF to_cref(uint32_t c){
    return (COLORREF)(((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF));
}

/* ---- framebuffer: a 32-bit top-down DIB section shared by GDI + our code ---- */
#define FONT_MIN_PX  11
#define FB_FONTCACHE 12

typedef struct {
    HDC      memdc;
    HBITMAP  dib;
    uint32_t *px;   /* w*h pixels, row-major, top-down */
    int      w, h;
    HFONT    font;  /* base monospace */
    int      ch_w, ch_h;   /* CURRENT active character cell metrics */
    int      bch_w, bch_h; /* base font metrics */
    int      base_fh;      /* base font pixel height */
    /* on-demand font cache for true zoom (variable text size) */
    HFONT    fcache[FB_FONTCACHE];
    int      fpx[FB_FONTCACHE], fcw[FB_FONTCACHE], fch[FB_FONTCACHE];
    int      nfont;
    uint32_t *prev;        /* retained previous frame for phosphor persistence */
} Framebuffer;

/* ---- terminal model ---- */
#define TERM_MAXLINE  512
#define TERM_SCROLLBK 4000
#define TERM_INPUTMAX 256

typedef struct {
    char     text[TERM_MAXLINE];
    uint32_t color;
} TermLine;

/* queued output for typewriter / step-by-step reveal */
typedef struct {
    char     text[TERM_MAXLINE];
    uint32_t color;
    int      delay_ms;   /* delay BEFORE this line appears */
} PendLine;

#define TERM_PENDMAX 2048

typedef struct {
    TermLine lines[TERM_SCROLLBK];
    int      count;            /* total lines ever (ring) */
    int      head;             /* ring start index        */

    char     input[TERM_INPUTMAX];
    int      inlen;

    PendLine pend[TERM_PENDMAX];
    int      pend_head, pend_tail;   /* ring */
    double   pend_timer;             /* ms remaining until next pend line */

    int      caret_on;
    double   caret_timer;

    /* command history */
    char     hist[32][TERM_INPUTMAX];
    int      hist_count, hist_pos;
} Terminal;

/* ---- application state machine ---- */
typedef enum {
    ST_GLITCH_INTRO = 0,
    ST_BOOT_INFECT,
    ST_SKULL_REVEAL,
    ST_WELCOME,
    ST_TERMINAL,
    ST_ANIM,
    ST_DATAEDIT,
    ST_JUMPSCARE,
    ST_FOURTHWALL,   /* lose: real desktop + identity reveal, then zoom out of the monitor */
    ST_WORLD,        /* lose: 3D corrupted maze you must escape on foot */
    ST_GAMEOVER,     /* lives exhausted: fully corrupted message, then exit */
    ST_QUIT
} AppState;

/* ---- algorithm visualizer (breakout state) ---- */
typedef enum { AV_SCHED, AV_MEM, AV_VMEM, AV_DISK, AV_CALC } AnimKind;

#define ANIM_TRACE_MAX 256
#define ANIM_TRACE_W   80

typedef struct {
    AnimKind kind;
    char     title[64];
    char     subtitle[96];

    /* playback */
    int      phase;        /* 0 intro, 1 sim, 2 result(rest) */
    double   phase_time;
    double   clock;        /* fractional sim cursor (units or step index) */
    double   clock_max;    /* sim end */
    int      paused;
    int      step_req;     /* +1 / -1 single-step request from keys */
    double   flash;        /* ms left of a red event-flash */
    double   glitch;       /* ms left of a glitch jolt */

    /* kernel-trace ring (lines revealed over time) */
    char     trace[ANIM_TRACE_MAX][ANIM_TRACE_W];
    uint32_t trace_col[ANIM_TRACE_MAX];
    double   trace_at[ANIM_TRACE_MAX];   /* clock value at which line appears */
    int      ntrace;

    /* compact summary pushed to the shell on exit */
    char     summary[8][TERM_MAXLINE];
    uint32_t summary_col[8];
    int      nsummary;

    /* ---- CPU scheduling ---- */
    struct {
        int n, id[16], at[16], bt[16], pr[16];
        int nseg, seg_pid[512], seg_a[512], seg_b[512];
        int comp[16], tat[16], wait[16], resp[16];
        int makespan;
    } sched;

    /* ---- page replacement ---- */
    struct {
        int n, ref[64], frames;
        int snap[64][10];   /* frame contents after step i (-1 empty) */
        int hit[64];        /* 1 hit, 0 fault */
        int victim[64];     /* page evicted at step i, -1 none */
        int faults;
    } vmem;

    /* ---- disk scheduling ---- */
    struct {
        int start, dmin, dmax, n, req[32];
        int np, path[64], isreq[64];
        int total;
    } disk;

    /* ---- memory ---- */
    struct {
        int mode, nreg, reg[16];
        int nstep, step_job[64], step_region[64], step_frag[64];
        char step_jid[64];
        int totfrag, used;
        /* paging */
        int paging, page_size, nconv;
        int la[8], pg[8], doff[8], fr[8], pa[8];
    } mem;

    /* ---- calc board (swap/eat) ---- */
    struct {
        char line[8][96];
        int  nline;
        char result[96];
    } calc;
} Anim;

typedef struct {
    HWND        hwnd;
    Framebuffer fb;
    Terminal    term;
    AppState    state;
    double      state_time;   /* ms elapsed in current state */
    double      now_ms;
    uint64_t    rng;

    /* screenshot of the user's desktop, captured at launch */
    uint32_t   *shot;         /* w*h, may be NULL */

    /* jumpscare scheduling */
    double      scare_at;     /* ms (app clock) of next jumpscare */
    int         scare_pending;/* queued because an animation was running */
    int         kills;        /* cosmetic counter */
    int         lives;        /* lose-sequence lives (start 3); 0 -> ST_GAMEOVER + exit */

    /* jumpscare puzzle runtime */
    struct {
        char     question[256];
        char     hint[128];
        char     answer[64];  /* canonical normalized expected answer */
        char     input[64];   /* what the user typed */
        int      inlen;
        double   time_left;   /* ms */
        double   phase_time;  /* ms within current phase */
        int      phase;       /* 0=skull,1=puzzle,2=result */
        int      result;      /* 1 pass 0 fail */
        char     res1[64], res2[64];
    } scare;

    int         busy_anim;    /* true while pend queue is draining an animation */

    Anim        anim;         /* active algorithm visualizer (ST_ANIM) */

    /* interactive data editor (ST_DATAEDIT) */
    struct {
        int      tab;         /* 0 sched, 1 mem, 2 vmem, 3 disk */
        int      field;       /* selected field index within tab */
        int      typing;      /* mid-keystroke entry into inbuf */
        char     inbuf[16];
        double   glitch;      /* ms of write-glitch left after a commit */
        double   t;           /* local clock for caret/skull */
    } edit;

    /* fourth-wall reveal + zoom-out (ST_FOURTHWALL) and corrupted exit (ST_GAMEOVER) */
    struct {
        int      phase;
        double   t;           /* ms within current phase */
        char     user[64];    /* GetUserNameA          */
        char     host[64];    /* GetComputerNameA      */
        char     when[16];    /* "HH:MM" local time    */
    } fw;

    /* 3D escape world (ST_WORLD) */
    struct {
        double   px, py, dir; /* player position (cell units) + heading (radians) */
        double   mx, my;      /* monster position (cell units) */
        int      monster_on;  /* monster active (spawns after the warning) */
        double   elapsed;     /* ms since entering the world */
        int      warned;      /* "I AM COMING FOR YOU" banner has fired */
        double   warn_t;      /* ms left to show the warning banner overlay */
        double   flash;       /* ms left of an escape/catch flash */
        int      escaped;     /* set when the exit is reached */
        int      msg;         /* which warning variant is showing */
    } world;
} App;

extern App g_app;

/* ---- editable datasets (default to the textbook examples) ---- */
typedef struct { int n; int id[16], arrival[16], burst[16], prio[16]; int quantum; } SchedData;
typedef struct { int total; int nreg; int reg[16]; int njob; int job[16]; char jid[16];
                 int page_size; } MemData;
typedef struct { int n; int ref[64]; int frames; } VmemData;
typedef struct { int start; int n; int req[32]; int dirdown; int dmin, dmax; } DiskData;
typedef struct { SchedData sched; MemData mem; VmemData vmem; DiskData disk; } Datasets;

extern Datasets g_data;
void data_reset(void);

/* ---- util.c ---- */
uint32_t rng_next(uint64_t *s);
int      rng_range(uint64_t *s, int lo, int hi); /* inclusive */
double   now_ms(void);

/* ---- gfx.c ---- */
int  fb_create(Framebuffer *fb, int w, int h);
void fb_destroy(Framebuffer *fb);
void fb_clear(Framebuffer *fb, uint32_t c);
void fb_present(Framebuffer *fb, HDC dst);
void fb_fill_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t c);
void fb_text(Framebuffer *fb, int x, int y, const char *s, uint32_t c);
void fb_blit_shot(Framebuffer *fb, const uint32_t *shot);
void fb_blit_shot_rect(Framebuffer *fb, const uint32_t *shot, int dx, int dy, int dw, int dh);
void fb_frame(Framebuffer *fb, int x, int y, int w, int h, uint32_t c);  /* outline */
void fb_box(Framebuffer *fb, int x, int y, int w, int h, uint32_t c);    /* TUI frame + corner brackets */
void fb_text_center(Framebuffer *fb, int cx, int y, const char *s, uint32_t c);
void fb_hline(Framebuffer *fb, int x, int y, int w, uint32_t c);
void fb_vline(Framebuffer *fb, int x, int y, int h, uint32_t c);
/* variable-size text (true zoom): select a cached font sized px high, restore with base */
int  fb_font_for(Framebuffer *fb, int px);  /* selects font, updates ch_w/ch_h, returns ch_h */
void fb_font_base(Framebuffer *fb);         /* restore base font + metrics */
void fb_text_sm(Framebuffer *fb, int x, int y, const char *s, uint32_t c); /* one-off small label */
/* glitch primitives operate on current fb contents */
void gfx_scanlines(Framebuffer *fb, int strength);
void gfx_rgb_split(Framebuffer *fb, int dx);
void gfx_slice_tear(Framebuffer *fb, uint64_t *rng, int amount, int bands);
void gfx_noise_blocks(Framebuffer *fb, uint64_t *rng, int count, int maxsz);
void gfx_invert_band(Framebuffer *fb, int y, int h);
void gfx_vignette(Framebuffer *fb);
/* horror-hacker aesthetic primitives */
void gfx_phosphor(Framebuffer *fb, int fade);                 /* CRT persistence trail (fade 0..100) */
void gfx_phosphor_reset(Framebuffer *fb);                     /* drop the trail buffer (on state change) */
void gfx_datamosh(Framebuffer *fb, uint64_t *rng, int x, int y, int w, int h);  /* corruption bloom in a rect */
void gfx_jitter(Framebuffer *fb, uint64_t *rng, int amp);     /* horizontal signal jitter */
void gfx_brightness(Framebuffer *fb, int pct);               /* global dim (flicker / darkness) */
void gfx_hexdump(Framebuffer *fb, uint64_t *rng, int x, int y, int w, int h);   /* decorative fake memory dump */
void fb_garble(char *dst, const char *src, uint64_t *rng, int pct);  /* glitch-corrupt a string */

/* ---- terminal.c ---- */
void term_init(Terminal *t);
void term_print(Terminal *t, uint32_t color, const char *fmt, ...);
void term_queue(Terminal *t, int delay_ms, uint32_t color, const char *fmt, ...);
void term_clear(Terminal *t);
void term_update(App *a, double dt);     /* drains pend queue, blinks caret */
void term_render(App *a);
void term_key_char(App *a, char c);
void term_key_special(App *a, int vk);   /* VK_BACK, VK_RETURN, arrows */
int  term_busy(Terminal *t);

/* command processing */
void cmd_execute(App *a, const char *line);

/* ---- intro.c ---- */
void intro_update(App *a, double dt);
void intro_render(App *a);

/* ---- skull.c ---- */
void skull_render(Framebuffer *fb, int cx, int cy, int frame, uint32_t color);
int  skull_width_px(Framebuffer *fb);
int  skull_height_px(Framebuffer *fb);

/* ---- jumpscare.c ---- */
void jumpscare_schedule(App *a);
void jumpscare_trigger(App *a);
void jumpscare_update(App *a, double dt);
void jumpscare_render(App *a);
void jumpscare_key_char(App *a, char c);
void jumpscare_key_special(App *a, int vk);

/* ---- screenshot.c ---- */
uint32_t *screenshot_capture(int w, int h);

/* ---- fourthwall.c (lose: desktop/identity reveal, zoom-out, corrupted exit) ---- */
void fourthwall_enter(App *a);
void fourthwall_update(App *a, double dt);
void fourthwall_render(App *a);
void gameover_enter(App *a);
void gameover_update(App *a, double dt);
void gameover_render(App *a);

/* ---- world.c (lose: 3D raycast escape maze + code-error monster) ---- */
void world_enter(App *a);
void world_update(App *a, double dt);
void world_render(App *a);

/* ---- dataedit.c (interactive data editor, ST_DATAEDIT) ---- */
void dataedit_open(App *a);
void dataedit_update(App *a, double dt);
void dataedit_render(App *a);
void dataedit_key_char(App *a, char c);
void dataedit_key_special(App *a, int vk);

/* ---- audio.c (procedural synth: tones/noise/drone, no asset files) ---- */
enum {
    SFX_KEY=0, SFX_BOOT, SFX_PAGEFAULT, SFX_HIT, SFX_SEEK, SFX_SWITCH,
    SFX_ALLOC, SFX_NOFIT, SFX_SKULL, SFX_CORRECT, SFX_WRONG, SFX_GLITCH, SFX_DECRYPT,
    SFX_BREATH, SFX_DRIP, SFX_WHISPER, SFX_SCAN
};
void audio_init(void);
void audio_update(void);     /* call once per frame to refill stream buffers */
void audio_shutdown(void);
void audio_sfx(int id, float param);   /* param: 0..1 (e.g. pitch for SEEK) */
void audio_drone(float tension);       /* 0..1 sustained dread bed */
void audio_silence(float ms);          /* hard-mute everything for ms (sudden-silence sting) */

/* ---- anim.c (algorithm visualizer) ---- */
void anim_begin(App *a, AnimKind kind, const char *title, const char *subtitle);
void anim_trace(App *a, double at_clock, uint32_t col, const char *fmt, ...);
void anim_summary(App *a, uint32_t col, const char *fmt, ...);
void anim_update(App *a, double dt);
void anim_render(App *a);
void anim_key(App *a, int vk);
void anim_exit(App *a);

/* ---- modules: each fills the terminal with a step animation + result ---- */
void sched_run(App *a, const char *args);
void mem_run(App *a, const char *args);
void vmem_run(App *a, const char *args);
void disk_run(App *a, const char *args);
void data_run(App *a, const char *args);   /* edit datasets */
void selftest_run(App *a);

#endif
