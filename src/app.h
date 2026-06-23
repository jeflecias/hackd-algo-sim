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
typedef struct {
    HDC      memdc;
    HBITMAP  dib;
    uint32_t *px;   /* w*h pixels, row-major, top-down */
    int      w, h;
    HFONT    font;  /* monospace */
    int      ch_w, ch_h; /* character cell metrics */
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
    ST_JUMPSCARE,
    ST_QUIT
} AppState;

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
/* glitch primitives operate on current fb contents */
void gfx_scanlines(Framebuffer *fb, int strength);
void gfx_rgb_split(Framebuffer *fb, int dx);
void gfx_slice_tear(Framebuffer *fb, uint64_t *rng, int amount, int bands);
void gfx_noise_blocks(Framebuffer *fb, uint64_t *rng, int count, int maxsz);
void gfx_invert_band(Framebuffer *fb, int y, int h);
void gfx_vignette(Framebuffer *fb);

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

/* ---- modules: each fills the terminal with a step animation + result ---- */
void sched_run(App *a, const char *args);
void mem_run(App *a, const char *args);
void vmem_run(App *a, const char *args);
void disk_run(App *a, const char *args);
void data_run(App *a, const char *args);   /* edit datasets */
void selftest_run(App *a);

#endif
