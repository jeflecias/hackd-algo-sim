/* world.c - the "other world": a tiny software raycaster you escape on foot,
   hunted by an incomprehensible code-error. Entered when a scare puzzle is failed.
   Reach the green door to claw back to the shell; get caught and you lose a life. */
#include "app.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define PI       3.14159265358979
#define MAP_W    16
#define MAP_H    9
#define MAXVIEW  6.0         /* cells before the dark swallows the walls */
#define MAXCOL   8192        /* per-column wall depth buffer (>= any monitor width) */

/* '#'=wall '.'=floor  S=spawn  E=exit(green door)  M=monster spawn.
   Every row is exactly MAP_W (16) chars; interior pillars are isolated 1-cell
   stubs so the floor stays fully connected (S can always reach E and M). */
static const char *MAP[MAP_H] = {
    "################",
    "#S....#.......E#",
    "#..............#",
    "#...#.....#....#",
    "#..............#",
    "#....#.....#...#",
    "#..............#",
    "#..#......M....#",
    "################",
};

#define EXIT_X 14
#define EXIT_Y 1

static int is_wall(int ix, int iy){
    if (ix < 0 || iy < 0 || ix >= MAP_W || iy >= MAP_H) return 1;
    return MAP[iy][ix] == '#';
}
static int blocked(double x, double y){ return is_wall((int)x, (int)y); }

void world_enter(App *a){
    a->state = ST_WORLD;
    a->state_time = 0;
    a->world.px = 1.5; a->world.py = 1.5; a->world.dir = 0.0;  /* spawn at S, facing +x */
    a->world.mx = 10.5; a->world.my = 7.5;                     /* monster waits at M */
    a->world.monster_on = 0;
    a->world.elapsed = 0;
    a->world.warned = 0;
    a->world.warn_t = 0;
    a->world.flash = 0;
    a->world.escaped = 0;
    a->world.msg = 0;
    gfx_phosphor_reset(&a->fb);
    audio_silence(200);
}

static int held(int vk){ return (GetAsyncKeyState(vk) & 0x8000) != 0; }

void world_update(App *a, double dt){
    if (a->world.flash > 0) a->world.flash -= dt;

    /* finish an escape flash -> back to the (more broken) shell */
    if (a->world.escaped){
        if (a->world.flash <= 0){
            a->state = ST_TERMINAL; a->state_time = 0;
            term_print(&a->term, COL_GREEN, "[you clawed back out -- %d lives left]", a->lives);
            jumpscare_schedule(a);
        }
        return;
    }

    /* movement (held keys, dt-scaled) */
    if (a->world.flash <= 0){
        double mv  = 0.0028 * dt;
        double rot = 0.0026 * dt;
        double dx = cos(a->world.dir), dy = sin(a->world.dir);
        if (held('W') || held(VK_UP)){
            double nx = a->world.px + dx*mv, ny = a->world.py + dy*mv;
            if (!blocked(nx, a->world.py)) a->world.px = nx;
            if (!blocked(a->world.px, ny)) a->world.py = ny;
        }
        if (held('S') || held(VK_DOWN)){
            double nx = a->world.px - dx*mv, ny = a->world.py - dy*mv;
            if (!blocked(nx, a->world.py)) a->world.px = nx;
            if (!blocked(a->world.px, ny)) a->world.py = ny;
        }
        if (held('A') || held(VK_LEFT))  a->world.dir -= rot;
        if (held('D') || held(VK_RIGHT)) a->world.dir += rot;
    }

    /* the hunter wakes after 10s */
    a->world.elapsed += dt;
    if (!a->world.warned && a->world.elapsed > 10000){
        a->world.warned = 1;
        a->world.monster_on = 1;
        a->world.warn_t = 3800;
        a->world.msg = rng_range(&a->rng, 0, 2);
        audio_sfx(SFX_SKULL, 0);
        audio_silence(150);
    }
    if (a->world.warn_t > 0) a->world.warn_t -= dt;

    if (a->world.monster_on){
        double mspeed = 0.0020 * dt;          /* a touch slower than the player */
        double dx = a->world.px - a->world.mx, dy = a->world.py - a->world.my;
        double sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
        if (fabs(dx) >= fabs(dy)){
            double nmx = a->world.mx + sx*mspeed;
            if (!blocked(nmx, a->world.my)) a->world.mx = nmx;
            else { double nmy = a->world.my + sy*mspeed; if (!blocked(a->world.mx, nmy)) a->world.my = nmy; }
        } else {
            double nmy = a->world.my + sy*mspeed;
            if (!blocked(a->world.mx, nmy)) a->world.my = nmy;
            else { double nmx = a->world.mx + sx*mspeed; if (!blocked(nmx, a->world.my)) a->world.mx = nmx; }
        }
        double cd = dx*dx + dy*dy;
        if (cd < 0.36){                        /* caught (dist < 0.6) */
            a->lives--;
            audio_sfx(SFX_WRONG, 0);
            if (a->lives <= 0){ gameover_enter(a); return; }
            a->state = ST_TERMINAL; a->state_time = 0;
            term_print(&a->term, COL_RED, "[a life was consumed -- %d remain]", a->lives);
            jumpscare_schedule(a);
            return;
        }
    }

    /* reached the green door? */
    double ex = EXIT_X + 0.5, ey = EXIT_Y + 0.5;
    double ddx = a->world.px - ex, ddy = a->world.py - ey;
    if (ddx*ddx + ddy*ddy < 0.36){
        a->world.escaped = 1;
        a->world.flash = 1300;
        audio_sfx(SFX_CORRECT, 0);
    }
}

void world_render(App *a){
    Framebuffer *fb = &a->fb;
    int Wd = fb->w, H = fb->h;
    fb_clear(fb, 0x00000000);

    double posx = a->world.px, posy = a->world.py, dir = a->world.dir;
    double dirx = cos(dir), diry = sin(dir);
    double planex = -diry * 0.66, planey = dirx * 0.66;

    static double zbuf[MAXCOL];
    int cols = Wd < MAXCOL ? Wd : MAXCOL;

    for (int x = 0; x < cols; x += 2){
        double camx = 2.0 * x / Wd - 1.0;
        double rayx = dirx + planex * camx;
        double rayy = diry + planey * camx;
        int mapx = (int)posx, mapy = (int)posy;
        double ddx = (rayx == 0) ? 1e30 : fabs(1.0 / rayx);
        double ddy = (rayy == 0) ? 1e30 : fabs(1.0 / rayy);
        int stepx, stepy; double sidex, sidey;
        if (rayx < 0){ stepx = -1; sidex = (posx - mapx) * ddx; }
        else         { stepx =  1; sidex = (mapx + 1.0 - posx) * ddx; }
        if (rayy < 0){ stepy = -1; sidey = (posy - mapy) * ddy; }
        else         { stepy =  1; sidey = (mapy + 1.0 - posy) * ddy; }

        int side = 0, hit = 0, guard = 0;
        while (!hit && guard++ < 80){
            if (sidex < sidey){ sidex += ddx; mapx += stepx; side = 0; }
            else              { sidey += ddy; mapy += stepy; side = 1; }
            if (is_wall(mapx, mapy)) hit = 1;
        }
        double perp = (side == 0) ? (sidex - ddx) : (sidey - ddy);
        if (perp < 0.0001) perp = 0.0001;

        int lineh = (int)(H / perp);
        int y0 = H/2 - lineh/2; if (y0 < 0) y0 = 0;
        int y1 = H/2 + lineh/2; if (y1 >= H) y1 = H - 1;

        double sh = 1.0 - perp / MAXVIEW; if (sh < 0) sh = 0;
        if (side == 1) sh *= 0.62;                       /* darker faces read as depth */
        int isexit = (mapx == EXIT_X && mapy == EXIT_Y);
        uint32_t base = isexit ? COL_GREEN : 0x0000C040u;/* exit door glows green */
        int rr = (int)(((base>>16)&0xFF) * sh);
        int gg = (int)(((base>>8)&0xFF) * sh);
        int bb = (int)((base&0xFF) * sh);
        uint32_t col = RGB32(rr, gg, bb);

        for (int y = y0; y <= y1; y++){
            fb->px[(size_t)y*Wd + x] = col;
            if (x+1 < Wd) fb->px[(size_t)y*Wd + x+1] = col;
        }
        zbuf[x] = perp; if (x+1 < cols) zbuf[x+1] = perp;
    }

    /* the monster: an unreadable code-error billboard, depth-tested per column */
    if (a->world.monster_on){
        double sx = a->world.mx - posx, sy = a->world.my - posy;
        double inv = 1.0 / (planex*diry - dirx*planey);
        double tx = inv * (diry*sx - dirx*sy);
        double ty = inv * (-planey*sx + planex*sy);   /* depth along view */
        if (ty > 0.1){
            int screenx = (int)((Wd/2) * (1.0 + tx/ty));
            int spriteh = (int)(H / ty); if (spriteh > H*2) spriteh = H*2;
            int spritew = spriteh * 2 / 3;
            int y0 = H/2 - spriteh/2;
            int x0 = screenx - spritew/2;
            double dist = sqrt(sx*sx + sy*sy);
            for (int px = x0; px < x0 + spritew; px++){
                if (px < 0 || px >= cols) continue;
                if (zbuf[px] <= ty) continue;          /* hidden behind a wall */
                for (int py = y0; py < y0 + spriteh; py++){
                    if (py < 0 || py >= H) continue;
                    uint32_t n = rng_next(&a->rng);
                    int v = 40 + (int)(n & 0x7F);
                    uint32_t c = ((uint32_t)v << 16) | ((n & 0x10) ? (uint32_t)(v/4) << 8 : 0) | (n & 0x07);
                    fb->px[(size_t)py*Wd + px] = c;
                }
            }
            if (x0 > -spritew && x0 < Wd){
                gfx_datamosh(fb, &a->rng, x0, y0, spritew, spriteh);
                const char *ERR[] = { "SEGMENTATION FAULT", "0xDEADBEEF", "STACK SMASHED",
                                      "FATAL: core dumped", "0xC0000005" };
                char g[64];
                fb_garble(g, ERR[rng_next(&a->rng) % 5], &a->rng, 45);
                fb_text(fb, screenx - (int)strlen(g)*fb->ch_w/2, H/2, g, COL_RED);
            }
            double prox = 1.0 - dist/8.0; if (prox < 0) prox = 0; if (prox > 1) prox = 1;
            if (prox > 0) gfx_rgb_split(fb, (int)(prox * 7));
        }
    }

    /* HUD: lives + a compass to the door */
    char hud[64]; snprintf(hud, sizeof(hud), "LIVES: %d", a->lives);
    fb_text(fb, 20, 20, hud, a->lives > 1 ? COL_RED : COL_WHITE);

    double rel = atan2((EXIT_Y + 0.5) - posy, (EXIT_X + 0.5) - posx) - dir;
    while (rel >  PI) rel -= 2*PI;
    while (rel < -PI) rel += 2*PI;
    const char *arrow = fabs(rel) < 0.45 ? "  ^ THE DOOR IS AHEAD"
                      : (rel > 0 ? "  THE DOOR -> (turn right)" : "  <- THE DOOR (turn left)");
    fb_text_center(fb, Wd/2, 40, arrow, COL_DGREEN);

    /* the warning, with the goal, when the hunter wakes */
    if (a->world.warn_t > 0){
        const char *WARN[] = { "I AM COMING FOR YOU",
                               "YOUR PROCESS WILL BE TERMINATED",
                               "SEGMENTATION FAULT -- IN YOU" };
        uint32_t c = (((int)(a->world.warn_t/120)) & 1) ? COL_RED : COL_WHITE;
        fb_text_center(fb, Wd/2, H/3, WARN[a->world.msg % 3], c);
        fb_text_center(fb, Wd/2, H/3 + fb->ch_h*2, "RUN. REACH THE GREEN DOOR.", COL_GREEN);
        gfx_slice_tear(fb, &a->rng, 30, 3);
    }

    if (a->world.escaped)
        fb_text_center(fb, Wd/2, H/2, "THE DOOR -- YOU CLAWED OUT", COL_GREEN);

    gfx_scanlines(fb, 84);
    gfx_vignette(fb);
}
