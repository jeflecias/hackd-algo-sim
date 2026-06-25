/* skull.c - the laughing skull, rendered from real ASCII art (closed_mouth.txt / open_mouth.txt)
   baked into two frames below. Cycling closed <-> open drops the jaw -> it laughs at you. The
   glyphs are drawn with the GDI monospace font (fb_text, TRANSPARENT background) so the negative
   space -- eye sockets, the gap around the bone -- stays empty and the additive red_glow bleeds
   through the sockets like something backlit pressing against the glass.

   The public API is unchanged: skull_render(fb,cx,cy,frame,color) draws centred at (cx,cy), and
   skull_width_px/height_px report the box so every call site (centre-piece reveal, corner glimpses,
   faint margin watermarks) positions correctly. The skull's SIZE still tracks the active font --
   a caller that selects a bigger font before calling (see world.c's catch screen) gets a bigger
   skull -- but the art is rendered at a fraction of that font so 51 rows fit a sane footprint.
   `color` tints the bone directly (faint watermark tints stay faint, COL_RED/white reveals blaze);
   `frame` drives the laugh cadence and the eye-glow pulse. */
#include "app.h"
#include <math.h>

/* ---- baked ASCII frames (generated from closed_mouth.txt / open_mouth.txt) ---- */
static const char *SK_CLOSED[] = {
    "                       uuuuuuuuuuuuuuuuuuuuu.",
    "                   .u$$$$$$$$$$$$$$$$$$$$$$$$$$W.",
    "                 u$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$Wu.",
    "               $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$i",
    "              $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "         `    $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "           .i$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$i",
    "           $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$W",
    "          .$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$W",
    "         .$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$i",
    "         #$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$.",
    "         W$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "$u       #$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$~",
    "$#      `\"$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "$i        $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "$$        #$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "$$         $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "#$.        $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$#",
    " $$      $iW$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$!",
    " $$i      $$$$$$$#\"\" `\"\"\"#$$$$$$$$$$$$$$$$$#\"\"\"\"\"\"#$$$$$$$$$$$$$$$W",
    " #$$W    `$$$#\"            \"       !$$$$$`           `\"#$$$$$$$$$$#",
    "  $$$     ``                 ! !iuW$$$$$                 #$$$$$$$#",
    "  #$$    $u                  $   $$$$$$$                  $$$$$$$~",
    "   \"#    #$$i.               #   $$$$$$$.                 `$$$$$$",
    "          $$$$$i.                \"\"\"#$$$$i.               .$$$$#",
    "          $$$$$$$$!         .   `    $$$$$$$$$i           $$$$$",
    "          `$$$$$  $iWW   .uW`        #$$$$$$$$$W.       .$$$$$$#",
    "            \"#$$$$$$$$$$$$#`          $$$$$$$$$$$iWiuuuW$$$$$$$$W",
    "               !#\"\"    \"\"             `$$$$$$$##$$$$$$$$$$$$$$$$",
    "          i$$$$    .                   !$$$$$$ .$$$$$$$$$$$$$$$#",
    "         $$$$$$$$$$`                    $$$$$$$$$Wi$$$$$$#\"#$$`",
    "         #$$$$$$$$$W.                   $$$$$$$$$$$#   ``",
    "          `$$$$##$$$$!       i$u.  $. .i$$$$$$$$$#\"\"",
    "             \"     `#W       $$$$$$$$$$$$$$$$$$$`      u$#",
    "                            W$$$$$$$$$$$$$$$$$$      $$$$W",
    "                            $$`!$$$##$$$$``$$$$      $$$$!",
    "                           i$\" $$$$  $$#\"`  \"\"\"     W$$$$",
    "                                                   W$$$$!",
    "                      uW$$  uu  uu.  $$$  $$$Wu#   $$$$$$",
    "                     ~$$$$iu$$iu$$$uW$$! $$$$$$i .W$$$$$$",
    "             ..  !   \"#$$$$$$$$$$##$$$$$$$$$$$$$$$$$$$$#\"",
    "             $$W  $     \"#$$$$$$$iW$$$$$$$$$$$$$$$$$$$$$W",
    "             $#`   `       \"\"#$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "                              !$$$$$$$$$$$$$$$$$$$$$#`",
    "                              $$$$$$$$$$$$$$$$$$$$$$!",
    "                            $$$$$$$$$$$$$$$$$$$$$$$`",
    "                             $$$$$$$$$$$$$$$$$$$$\"",
};

static const char *SK_OPEN[] = {
    "                       uuuuuuuuuuuuuuuuuuuuu.",
    "                   .u$$$$$$$$$$$$$$$$$$$$$$$$$$W.",
    "                 u$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$Wu.",
    "               $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$i",
    "              $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "         `    $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "           .i$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$i",
    "           $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$W",
    "          .$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$W",
    "         .$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$i",
    "         #$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$.",
    "         W$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "$u       #$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$~",
    "$#      `\"$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "$i        $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "$$        #$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "$$         $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "#$.        $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$#",
    " $$      $iW$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$!",
    " $$i      $$$$$$$#\"\" `\"\"\"#$$$$$$$$$$$$$$$$$#\"\"\"\"\"\"#$$$$$$$$$$$$$$$W",
    " #$$W    `$$$#\"            \"       !$$$$$`           `\"#$$$$$$$$$$#",
    "  $$$     ``                 ! !iuW$$$$$                 #$$$$$$$#",
    "  #$$    $u                  $   $$$$$$$                  $$$$$$$~",
    "   \"#    #$$i.               #   $$$$$$$.                 `$$$$$$",
    "          $$$$$i.                \"\"\"#$$$$i.               .$$$$#",
    "          $$$$$$$$!         .   `    $$$$$$$$$i           $$$$$",
    "          `$$$$$  $iWW   .uW`        #$$$$$$$$$W.       .$$$$$$#",
    "            \"#$$$$$$$$$$$$#`          $$$$$$$$$$$iWiuuuW$$$$$$$$W",
    "               !#\"\"    \"\"             `$$$$$$$##$$$$$$$$$$$$$$$$",
    "          i$$$$    .                   !$$$$$$ .$$$$$$$$$$$$$$$#",
    "         $$$$$$$$$$`                    $$$$$$$$$Wi$$$$$$#\"#$$`",
    "         #$$$$$$$$$W.                   $$$$$$$$$$$#   ``",
    "          `$$$$##$$$$!       i$u.  $. .i$$$$$$$$$#\"\"",
    "             \"     `#W       $$$$$$$$$$$$$$$$$$$`      ",
    "                            W$$$$$$$$$$$$$$$$$$      ",
    "                            $$`!$$$##$$$$``$$$$      ",
    "                           i$\" $$$$  $$#\"`  \"\"\"   ",
    "                                                       u$#",
    "                                                     $$$$W",
    "                                                     $$$$!",
    "                                                    W$$$$",
    "                                                    W$$$$",
    "                                                   W$$$$!",
    "                      uW$$  uu  uu.  $$$  $$$Wu#   $$$$$$",
    "                     ~$$$$iu$$iu$$$uW$$! $$$$$$i .W$$$$$$",
    "             ..  !   \"#$$$$$$$$$$##$$$$$$$$$$$$$$$$$$$$#\"",
    "             $$W  $     \"#$$$$$$$iW$$$$$$$$$$$$$$$$$$$$$W",
    "             $#`   `       \"\"#$$$$$$$$$$$$$$$$$$$$$$$$$$$",
    "                              !$$$$$$$$$$$$$$$$$$$$$#`",
    "                              $$$$$$$$$$$$$$$$$$$$$$!",
    "                            $$$$$$$$$$$$$$$$$$$$$$$`",
    "                             $$$$$$$$$$$$$$$$$$$$\"",
};

#define SK_CLOSED_ROWS ((int)(sizeof(SK_CLOSED)/sizeof(SK_CLOSED[0])))   /* 47 */
#define SK_OPEN_ROWS   ((int)(sizeof(SK_OPEN)/sizeof(SK_OPEN[0])))       /* 51 */
#define SK_ROWS  SK_OPEN_ROWS    /* tallest frame; the box reserves this so the cranium stays put */
#define SK_COLS  67              /* widest line across both frames */

/* art font height for the current active line height: the 51-row skull is squeezed toward the old
   11-row footprint, then floored at the GDI minimum. Bigger active font -> bigger skull. Tunable. */
static int sk_art_fpx(int active_ch_h){
    int px = active_ch_h * 11 / SK_ROWS;
    if (px < FONT_MIN_PX) px = FONT_MIN_PX;
    return px;
}

/* select the art font, leaving the active font untouched on return -- captures whatever HFONT/metrics
   were live (base for most callers, a zoom font for world.c's catch screen). Returns the px chosen. */
static HFONT sk_push_font(Framebuffer *fb, int *save_cw, int *save_ch){
    HFONT prev = (HFONT)GetCurrentObject(fb->memdc, OBJ_FONT);
    *save_cw = fb->ch_w; *save_ch = fb->ch_h;
    fb_font_for(fb, sk_art_fpx(*save_ch));
    return prev;
}
static void sk_pop_font(Framebuffer *fb, HFONT prev, int save_cw, int save_ch){
    SelectObject(fb->memdc, prev);
    fb->ch_w = save_cw; fb->ch_h = save_ch;
}

int skull_height_px(Framebuffer *fb){
    int cw, ch; HFONT prev = sk_push_font(fb, &cw, &ch);
    int h = SK_ROWS * fb->ch_h;
    sk_pop_font(fb, prev, cw, ch);
    return h;
}
int skull_width_px(Framebuffer *fb){
    int cw, ch; HFONT prev = sk_push_font(fb, &cw, &ch);
    int w = SK_COLS * fb->ch_w;
    sk_pop_font(fb, prev, cw, ch);
    return w;
}

void skull_render(Framebuffer *fb, int cx, int cy, int frame, uint32_t color){
    frame = ((unsigned)frame) & 0x3FFFFFFF;

    int cw, ch; HFONT prev = sk_push_font(fb, &cw, &ch);
    int acw = fb->ch_w, ach = fb->ch_h;          /* art cell metrics */
    int W = SK_COLS * acw, H = SK_ROWS * ach;
    int x0 = cx - W/2, y0 = cy - H/2;

    /* laugh cadence: callers step `frame` every ~90-170ms, so toggling on bit 2 chatters ~2x/sec */
    int open = (frame >> 2) & 1;
    const char **F = open ? SK_OPEN : SK_CLOSED;
    int nrows = open ? SK_OPEN_ROWS : SK_CLOSED_ROWS;

    /* ---- the bone: literal ASCII glyphs, tinted by `color` (transparent bg keeps sockets empty) ---- */
    for (int r = 0; r < nrows; r++)
        fb_text(fb, x0, y0 + r * ach, F[r], color);

    /* ---- backlit eye sockets: additive red glow bleeding through the empty socket pixels ----
       fractions of the rendered box, so the glow tracks any font size */
    int cr = (color>>16)&0xFF, cg = (color>>8)&0xFF, cb = color&0xFF;
    int lum = cr; if (cg > lum) lum = cg; if (cb > lum) lum = cb;
    double bf = lum / 255.0;
    double pulse = 0.5 + 0.5*sin(frame * 0.20);
    int gstr = (int)((26 + 42*pulse) * bf);
    if (gstr > 0){
        int ey  = y0 + (int)(H * 0.47);
        int exl = x0 + (int)(W * 0.33);
        int exr = x0 + (int)(W * 0.70);
        int grad = (int)(W * 0.12);
        red_glow(fb, exl, ey, grad, gstr);
        red_glow(fb, exr, ey, grad, gstr);
    }

    sk_pop_font(fb, prev, cw, ch);
}
