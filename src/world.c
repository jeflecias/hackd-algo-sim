/* world.c - the "other world": a procedurally-generated software raycaster maze you
   escape on foot, hunted by an incomprehensible code-error. Entered when a scare
   puzzle is failed. Reach the exit portal to claw back to the shell; get caught and
   a corruption-jumpscare consumes a life.

   The maze is regenerated every time: a carved + braided grid (always solvable, BFS-
   checked), the exit placed at the cell FARTHEST from spawn, the monster spawned ON
   the exit so it guards the way out, then it wakes and hunts. The walls are textured
   like corrupted machine guts and carry cryptic, fourth-wall messages baked onto the
   surfaces (the player's own name/host/filenames) - and some walls bleed the player's
   own photos back at them, corrupted. "Full horror" set piece; file access is the
   read-only image/identity lookups only (see imageload.c / fourthwall.c). */
#include "app.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define PI       3.14159265358979
#define GEN_W    31          /* real grid (3 cells per logical room: 2 open + 1 wall) */
#define GEN_H    21
#define LW       ((GEN_W-1)/3)   /* logical rooms across (each -> 2x2 open room)       */
#define LH       ((GEN_H-1)/3)   /* logical rooms down                                  */
#define MAXVIEW  5.0         /* cells before the fog swallows it (Silent Hill short draw dist) */
#define MAXCOL   8192        /* per-column wall depth buffer          */

/* ---- runtime maze state (rebuilt each world_enter) ---- */
static char          g_grid[GEN_H][GEN_W];   /* '#' wall, '.' floor */
static unsigned char g_tag [GEN_H][GEN_W];   /* wall content: 0 plain, 1 message, 2 image */
static unsigned char g_idx [GEN_H][GEN_W];   /* message/image index for the cell */
static int  g_mw = GEN_W, g_mh = GEN_H;
static int  g_spawn_x = 1, g_spawn_y = 1;
static int  g_exit_x = 1,  g_exit_y = 1;

/* ---- cryptic / fourth-wall message pool, built per run ---- */
#define MSG_MAX 28
static char g_msgs[MSG_MAX][48];
static int  g_msglen[MSG_MAX];
static int  g_nmsg = 0;

/* ---- the player's own photos, loaded once, corrupted onto walls ---- */
static ImageSet g_imgs;
static int      g_imgs_tried = 0;

#define TAG_PLAIN 0
#define TAG_MSG   1
#define TAG_IMAGE 2

static int is_wall(int ix, int iy){
    if (ix < 0 || iy < 0 || ix >= g_mw || iy >= g_mh) return 1;
    return g_grid[iy][ix] == '#';
}
static int blocked(double x, double y){ return is_wall((int)x, (int)y); }

static unsigned hashu(unsigned x){
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
static unsigned cellhash(int a, int b, int c, int d, int e){
    return hashu((unsigned)a*73856093u ^ (unsigned)b*19349663u ^ (unsigned)c*83492791u
               ^ (unsigned)d*2654435761u ^ (unsigned)e*40503u);
}

/* ---- low-res internal render target: the world is drawn here then upscaled, for
   both the FPS win and the Silent Hill / PS1 softness ---- */
static Framebuffer g_lofb;
static int g_lo_ready = 0;
static Framebuffer *lo_target(Framebuffer *fb){
    int scale = (int)(fb->h / 270.0 + 0.5); if (scale < 2) scale = 2;   /* ~270px internal height */
    int lw = fb->w / scale, lh = fb->h / scale;
    if (!g_lo_ready || g_lofb.w != lw || g_lofb.h != lh){
        if (g_lofb.px) free(g_lofb.px);
        memset(&g_lofb, 0, sizeof(g_lofb));
        g_lofb.px = (uint32_t*)malloc((size_t)lw*lh*4);
        g_lofb.w = lw; g_lofb.h = lh; g_lofb.base_fh = 1;
        g_lo_ready = 1;
    }
    return &g_lofb;
}

static int ifloor(double x){ int i = (int)x; return (x < 0 && (double)i != x) ? i-1 : i; }

/* ---- small color helpers (fog + shading) ---- */
#define FOG_COLOR 0x0014180Fu                /* murky gray-green the distance fades into */
static uint32_t scale_rgb(uint32_t c, double f){
    int r=(int)(((c>>16)&0xFF)*f), g=(int)(((c>>8)&0xFF)*f), b=(int)((c&0xFF)*f);
    r = r<0?0:(r>255?255:r);
    g = g<0?0:(g>255?255:g);
    b = b<0?0:(b>255?255:b);
    return RGB32(r,g,b);
}
static uint32_t blend_col(uint32_t c0, uint32_t c1, double t){  /* t=0 -> c0, t=1 -> c1 */
    t = t<0?0:(t>1?1:t);
    int ar=(c0>>16)&0xFF, ag=(c0>>8)&0xFF, ab=c0&0xFF;
    int br=(c1>>16)&0xFF, bg=(c1>>8)&0xFF, bb=c1&0xFF;
    return RGB32(ar+(int)((br-ar)*t), ag+(int)((bg-ag)*t), ab+(int)((bb-ab)*t));
}

/* ===================== maze generation + playability ===================== */

/* BFS distance fields over the grid; -1 = unreachable/wall. */
static int g_dist[GEN_W*GEN_H];      /* from spawn (generation/playability)  */
static int g_exitdist[GEN_W*GEN_H];  /* from exit  (player path-trail)        */
static int g_mdist[GEN_W*GEN_H];     /* from player (monster pathfinding)     */
static unsigned char g_onpath[GEN_W*GEN_H];  /* cells on the player->exit trail */

/* generic flood-fill from (sx,sy) into out[] (BFS distances, -1 unreachable) */
static void bfs_from(int sx, int sy, int *out){
    static int q[GEN_W*GEN_H];
    int qh = 0, qt = 0;
    for (int i = 0; i < g_mw*g_mh; i++) out[i] = -1;
    if (sx<0||sy<0||sx>=g_mw||sy>=g_mh || g_grid[sy][sx] == '#') return;
    int s = sy*g_mw + sx;
    out[s] = 0; q[qt++] = s;
    static const int DX[4] = {1,-1,0,0}, DY[4] = {0,0,1,-1};
    while (qh < qt){
        int cur = q[qh++]; int cx = cur % g_mw, cy = cur / g_mw;
        for (int k = 0; k < 4; k++){
            int nx = cx+DX[k], ny = cy+DY[k];
            if (nx<0||ny<0||nx>=g_mw||ny>=g_mh) continue;
            if (g_grid[ny][nx] == '#') continue;
            int ni = ny*g_mw+nx;
            if (out[ni] >= 0) continue;
            out[ni] = out[cur]+1;
            q[qt++] = ni;
        }
    }
}

/* flood-fill from spawn into g_dist; returns 1 if (ex,ey) ended up reachable. */
static int reachable(int ex, int ey){
    bfs_from(g_spawn_x, g_spawn_y, g_dist);
    return (ex>=0 && ey>=0 && ex<g_mw && ey<g_mh && g_dist[ey*g_mw+ex] >= 0);
}

/* rebuild the glowing floor trail: walk from (sx,sy) down the exit-distance
   gradient to the portal, marking each cell on the shortest path. */
static void rebuild_path(int sx, int sy){
    memset(g_onpath, 0, sizeof(g_onpath));
    if (sx<0||sy<0||sx>=g_mw||sy>=g_mh) return;
    if (g_exitdist[sy*g_mw+sx] < 0) return;
    static const int DX[4] = {1,-1,0,0}, DY[4] = {0,0,1,-1};
    int cx = sx, cy = sy, guard = 0;
    while (guard++ < GEN_W*GEN_H){
        g_onpath[cy*g_mw+cx] = 1;
        if (cx==g_exit_x && cy==g_exit_y) break;
        int cd = g_exitdist[cy*g_mw+cx], bx=cx, by=cy, moved=0;
        for (int k = 0; k < 4; k++){
            int nx=cx+DX[k], ny=cy+DY[k];
            if (nx<0||ny<0||nx>=g_mw||ny>=g_mh) continue;
            if (g_grid[ny][nx]=='#') continue;
            int dd = g_exitdist[ny*g_mw+nx];
            if (dd>=0 && dd<cd){ cd=dd; bx=nx; by=ny; moved=1; }
        }
        if (!moved) break;
        cx=bx; cy=by;
    }
}

/* open a logical room as a 2x2 block of floor (origin lx*3+1, ly*3+1) */
static void open_room(int lx, int ly){
    int ox = lx*3+1, oy = ly*3+1;
    g_grid[oy][ox]     = '.'; g_grid[oy][ox+1]   = '.';
    g_grid[oy+1][ox]   = '.'; g_grid[oy+1][ox+1] = '.';
}
/* open the 2-wide doorway through the 1-cell wall toward neighbor (dx,dy) */
static void open_door(int lx, int ly, int dx, int dy){
    int ox = lx*3+1, oy = ly*3+1;
    if      (dx ==  1){ g_grid[oy][ox+2] = '.'; g_grid[oy+1][ox+2] = '.'; }
    else if (dx == -1){ g_grid[oy][ox-1] = '.'; g_grid[oy+1][ox-1] = '.'; }
    else if (dy ==  1){ g_grid[oy+2][ox] = '.'; g_grid[oy+2][ox+1] = '.'; }
    else              { g_grid[oy-1][ox] = '.'; g_grid[oy-1][ox+1] = '.'; }
}

/* carve a maze of 2x2 ROOMS joined by 2-wide doorways (wide enough to dodge in),
   via randomized DFS over a coarse logical grid (LW x LH). */
static void carve(uint64_t *rng){
    for (int y = 0; y < g_mh; y++) for (int x = 0; x < g_mw; x++) g_grid[y][x] = '#';
    static unsigned char vis[LW*LH];
    for (int i = 0; i < LW*LH; i++) vis[i] = 0;
    static int stk[LW*LH]; int sp = 0;
    open_room(0,0); vis[0] = 1; stk[sp++] = 0;
    static const int DX[4] = {1,-1,0,0}, DY[4] = {0,0,1,-1};
    while (sp > 0){
        int cur = stk[sp-1]; int lx = cur % LW, ly = cur / LW;
        int order[4] = {0,1,2,3};
        for (int i = 3; i > 0; i--){ int j = rng_range(rng,0,i); int t=order[i];order[i]=order[j];order[j]=t; }
        int advanced = 0;
        for (int i = 0; i < 4; i++){
            int d = order[i];
            int nlx = lx+DX[d], nly = ly+DY[d];
            if (nlx<0||nly<0||nlx>=LW||nly>=LH) continue;
            if (vis[nly*LW+nlx]) continue;
            open_room(nlx, nly);
            open_door(lx, ly, DX[d], DY[d]);
            vis[nly*LW+nlx] = 1;
            stk[sp++] = nly*LW+nlx;
            advanced = 1; break;
        }
        if (!advanced) sp--;
    }
}

/* braid: remove some interior walls to create loops / small rooms (less claustrophobic,
   and gives obstacles somewhere to live without sealing the maze) */
static void braid(uint64_t *rng){
    for (int y = 1; y < g_mh-1; y++)
        for (int x = 1; x < g_mw-1; x++)
            if (g_grid[y][x] == '#' && rng_range(rng,0,99) < 18)
                g_grid[y][x] = '.';
}

/* build the cryptic / fourth-wall message pool from identity + sampled filenames */
static void build_messages(App *a){
    const char *user = (a->fw.user[0]) ? a->fw.user : "USER";
    const char *host = (a->fw.host[0]) ? a->fw.host : "THIS MACHINE";
    g_nmsg = 0;
    #define PUSH(...) do{ if(g_nmsg<MSG_MAX){ snprintf(g_msgs[g_nmsg],48,__VA_ARGS__); g_nmsg++; } }while(0)
    PUSH("%s THERE IS NO EXIT", user);
    PUSH("I LIVE IN %s", host);
    PUSH("WHY ARE YOU AWAKE %s", user);
    PUSH("YOU ARE THE BUG");
    PUSH("RETURN 0 // YOU CANT");
    PUSH("SEGFAULT IN YOU");
    PUSH("WHILE(YOU) SUFFER");
    PUSH("FREE(YOUR SOUL)");
    PUSH("KERNEL PANIC");
    PUSH("I SEE YOUR STACK");
    PUSH("NO ESCAPE %s", user);
    PUSH("01001000 01000101 01001100 01010000");
    PUSH("EXIT(1)");
    PUSH("HELP ME");
    PUSH("ITS BEHIND YOU");
    PUSH("DONT LOOK BACK");
    PUSH("MEMORY LEAK: YOU");
    PUSH("DEADLOCK");
    /* weave in the player's own files - the deepest cut */
    for (int i = 0; i < g_imgs.nnames && g_nmsg < MSG_MAX; i++){
        if ((i & 1) == 0) PUSH("%s IS MINE NOW", g_imgs.names[i]);
        else              PUSH("I HAVE YOUR %s", g_imgs.names[i]);
    }
    #undef PUSH
    if (g_nmsg == 0) snprintf(g_msgs[g_nmsg++], 48, "NO EXIT");
    for (int i = 0; i < g_nmsg; i++) g_msglen[i] = (int)strlen(g_msgs[i]);
}

/* assign each visible wall face a content tag (plain texture / message / photo) */
static void tag_walls(uint64_t *rng){
    static const int DX[4] = {1,-1,0,0}, DY[4] = {0,0,1,-1};
    for (int y = 0; y < g_mh; y++){
        for (int x = 0; x < g_mw; x++){
            g_tag[y][x] = TAG_PLAIN; g_idx[y][x] = 0;
            if (g_grid[y][x] != '#') continue;
            int borders = 0;
            for (int k = 0; k < 4; k++){
                int nx=x+DX[k], ny=y+DY[k];
                if (nx>=0&&ny>=0&&nx<g_mw&&ny<g_mh && g_grid[ny][nx]=='.') borders = 1;
            }
            if (!borders) continue;
            int roll = rng_range(rng,0,99);
            if (roll < 22){ g_tag[y][x] = TAG_MSG;  g_idx[y][x] = (unsigned char)rng_range(rng,0,g_nmsg-1); }
            else if (roll < 40){ g_tag[y][x] = TAG_IMAGE;
                int n = g_imgs.count > 0 ? g_imgs.count : 1;
                g_idx[y][x] = (unsigned char)rng_range(rng,0,n-1);
            }
        }
    }
}

static void generate_maze(App *a){
    uint64_t *rng = &a->rng;
    g_mw = GEN_W; g_mh = GEN_H;
    g_spawn_x = 1; g_spawn_y = 1;

    for (int attempt = 0; attempt < 8; attempt++){
        carve(rng);
        braid(rng);

        /* exit = a cell among the farthest reachable from spawn (random tie-break) */
        reachable(-1, -1);
        int maxd = 0;
        for (int i = 0; i < g_mw*g_mh; i++) if (g_dist[i] > maxd) maxd = g_dist[i];
        static int farc[GEN_W*GEN_H]; int nf = 0;
        for (int i = 0; i < g_mw*g_mh; i++)
            if (g_dist[i] >= maxd-2 && g_dist[i] > 0) farc[nf++] = i;
        int pick = nf ? farc[rng_range(rng,0,nf-1)] : (g_spawn_y*g_mw+g_spawn_x);
        g_exit_x = pick % g_mw; g_exit_y = pick / g_mw;

        /* scatter pillar obstacles "in between", each validated so the maze stays solvable */
        int floor_cells = 0;
        for (int i = 0; i < g_mw*g_mh; i++) if (((char*)g_grid)[i] == '.') floor_cells++;
        int want = floor_cells / 22;            /* sparse pillars - rooms stay open to dodge in */
        for (int t = 0; t < want*3 && want > 0; t++){
            int x = rng_range(rng,1,g_mw-2), y = rng_range(rng,1,g_mh-2);
            if (g_grid[y][x] != '.') continue;
            if (x==g_spawn_x && y==g_spawn_y) continue;
            if (x==g_exit_x  && y==g_exit_y ) continue;
            if (abs(x-g_spawn_x)+abs(y-g_spawn_y) < 2) continue;   /* keep spawn clear */
            g_grid[y][x] = '#';
            if (!reachable(g_exit_x, g_exit_y)) g_grid[y][x] = '.';  /* revert if it seals */
            else if (--want <= 0) break;
        }

        /* explicit playability gate; regenerate if (impossibly) unsolvable */
        if (reachable(g_exit_x, g_exit_y)) break;
    }

    bfs_from(g_exit_x, g_exit_y, g_exitdist);   /* distance-to-exit field for the path trail */
    memset(g_onpath, 0, sizeof(g_onpath));
    build_messages(a);
    tag_walls(rng);
}

/* ============================= enter / update ============================= */

void world_enter(App *a){
    a->state = ST_WORLD;
    a->state_time = 0;

    if (!g_imgs_tried){ images_load(&g_imgs, &a->rng, IMG_MAX); g_imgs_tried = 1; }
    generate_maze(a);

    a->world.px = g_spawn_x + 0.5; a->world.py = g_spawn_y + 0.5; a->world.dir = 0.0;
    a->world.mx = g_exit_x + 0.5;  a->world.my = g_exit_y + 0.5;   /* monster guards the exit */
    a->world.monster_on = 0;
    a->world.elapsed = 0;
    a->world.warned = 0;
    a->world.warn_t = 0;
    a->world.flash = 0;
    a->world.escaped = 0;
    a->world.msg = 0;
    a->world.caught = 0;
    a->world.caught_t = 0;
    a->world.pitch = 0;
    a->world.mouse_ready = 0;
    a->world.mpath_t = 0;
    a->world.hp = 50; a->world.hit_cd = 0; a->world.hurt_t = 0;
    a->world.pcellx = g_spawn_x; a->world.pcelly = g_spawn_y;
    rebuild_path(g_spawn_x, g_spawn_y);
    gfx_phosphor_reset(&a->fb);
    audio_silence(200);
}

static int held(int vk){ return (GetAsyncKeyState(vk) & 0x8000) != 0; }

void world_update(App *a, double dt){
    /* catch-jumpscare phase: hold, then consume a life and resolve */
    if (a->world.caught){
        a->world.caught_t += dt;
        if (a->world.caught_t > 1500){
            a->lives--;
            a->world.caught = 0;
            if (a->lives <= 0){ gameover_enter(a); return; }
            a->state = ST_TERMINAL; a->state_time = 0;
            term_print(&a->term, COL_RED, "[a life was consumed -- %d remain]", a->lives);
            jumpscare_schedule(a);
        }
        return;
    }

    if (a->world.flash > 0) a->world.flash -= dt;

    if (a->world.escaped){
        if (a->world.flash <= 0){
            a->state = ST_TERMINAL; a->state_time = 0;
            term_print(&a->term, COL_GREEN, "[you clawed back out -- %d lives left]", a->lives);
            jumpscare_schedule(a);
        }
        return;
    }

    /* mouse look: unbounded yaw + clamped pitch (a horizon shift). recenters the
       cursor every frame so turning never hits a screen edge -> true 360. */
    if (a->world.flash <= 0 && GetForegroundWindow() == a->hwnd){
        int cx = a->fb.w/2, cy = a->fb.h/2;
        POINT pt; GetCursorPos(&pt);
        if (a->world.mouse_ready){
            a->world.dir   += (pt.x - cx) * 0.0022;        /* yaw: unbounded 360       */
            a->world.pitch -= (pt.y - cy) * 1.30;          /* pitch: look up/down       */
            double pmax = a->fb.h * 0.60;                  /* generous vertical range   */
            if (a->world.pitch >  pmax) a->world.pitch =  pmax;
            if (a->world.pitch < -pmax) a->world.pitch = -pmax;
        } else a->world.mouse_ready = 1;
        SetCursorPos(cx, cy);
    }

    /* movement (held keys, dt-scaled) - WASD/arrows MOVE only; the mouse does all
       looking. W/S = forward/back, A/D = strafe left/right. */
    if (a->world.flash <= 0){
        double mv  = 0.0028 * dt;
        double fdx = cos(a->world.dir), fdy = sin(a->world.dir);   /* forward */
        double rdx = -fdy, rdy = fdx;                              /* strafe-right (camera plane) */
        double mxv = 0, myv = 0;
        if (held('W') || held(VK_UP))    { mxv += fdx; myv += fdy; }
        if (held('S') || held(VK_DOWN))  { mxv -= fdx; myv -= fdy; }
        if (held('D') || held(VK_RIGHT)) { mxv += rdx; myv += rdy; }
        if (held('A') || held(VK_LEFT))  { mxv -= rdx; myv -= rdy; }
        double nx = a->world.px + mxv*mv, ny = a->world.py + myv*mv;
        if (!blocked(nx, a->world.py)) a->world.px = nx;
        if (!blocked(a->world.px, ny)) a->world.py = ny;
    }

    /* rebuild the floor path-trail when the player steps into a new cell */
    {
        int pcx = (int)a->world.px, pcy = (int)a->world.py;
        if (pcx != a->world.pcellx || pcy != a->world.pcelly){
            a->world.pcellx = pcx; a->world.pcelly = pcy;
            rebuild_path(pcx, pcy);
        }
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
        double mspeed = 0.0021 * dt;          /* a touch slower than the player */

        /* recompute a distance-to-player field periodically so the monster can
           thread corridors and round corners instead of jamming on walls */
        a->world.mpath_t -= dt;
        int pcx = (int)a->world.px, pcy = (int)a->world.py;
        if (a->world.mpath_t <= 0){ bfs_from(pcx, pcy, g_mdist); a->world.mpath_t = 150; }

        int mcx = (int)a->world.mx, mcy = (int)a->world.my;
        double tgx = a->world.px, tgy = a->world.py;   /* default: straight at the player */
        if (mcx>=0 && mcy>=0 && mcx<g_mw && mcy<g_mh){
            int cur = g_mdist[mcy*g_mw+mcx];
            int bestd = (cur < 0) ? (1<<30) : cur, bk = -1;
            static const int DX[4] = {1,-1,0,0}, DY[4] = {0,0,1,-1};
            for (int k = 0; k < 4; k++){
                int nx=mcx+DX[k], ny=mcy+DY[k];
                if (nx<0||ny<0||nx>=g_mw||ny>=g_mh) continue;
                if (g_grid[ny][nx]=='#') continue;
                int dd = g_mdist[ny*g_mw+nx];
                if (dd>=0 && dd<bestd){ bestd=dd; bk=k; }
            }
            if (bk >= 0){ tgx = mcx + DX[bk] + 0.5; tgy = mcy + DY[bk] + 0.5; }
        }
        /* steer continuously toward the sub-target cell center */
        double ax = tgx - a->world.mx, ay = tgy - a->world.my;
        double al = sqrt(ax*ax + ay*ay); if (al > 1e-6){ ax/=al; ay/=al; }
        double nmx = a->world.mx + ax*mspeed, nmy = a->world.my + ay*mspeed;
        if (!blocked(nmx, a->world.my)) a->world.mx = nmx;
        if (!blocked(a->world.mx, nmy)) a->world.my = nmy;

        /* damage: when near, the monster claws for 10 HP at most once per second */
        if (a->world.hit_cd > 0) a->world.hit_cd -= dt;
        double dx = a->world.px - a->world.mx, dy = a->world.py - a->world.my;
        if (dx*dx + dy*dy < 1.0 && a->world.hit_cd <= 0){   /* "near" ~1 cell */
            a->world.hp -= 10;
            a->world.hit_cd = 1000;
            a->world.hurt_t = 450;                          /* red corruption flash */
            audio_sfx(SFX_SCREAM, 0);
            if (a->world.hp <= 0){                          /* drained -> catch jumpscare -> lose a life */
                a->world.caught = 1; a->world.caught_t = 0;
                audio_silence(120);
                audio_sfx(SFX_SKULL, 0);
                audio_sfx(SFX_WRONG, 0);
                return;
            }
        }
    }
    if (a->world.hurt_t > 0) a->world.hurt_t -= dt;

    /* reached the exit portal? */
    double ex = g_exit_x + 0.5, ey = g_exit_y + 0.5;
    double ddx = a->world.px - ex, ddy = a->world.py - ey;
    if (ddx*ddx + ddy*ddy < 0.36){
        a->world.escaped = 1;
        a->world.flash = 1300;
        audio_sfx(SFX_CORRECT, 0);
    }
}

/* ===================== text + texture sampling ===================== */

/* is pixel (u,v) lit by `msg` (length n) rendered as a single line in band [vlo,vhi)? */
static int text_lit(const char *msg, int n, double u, double v, double vlo, double vhi){
    if (v < vlo || v >= vhi) return 0;
    if (n <= 0) return 0;
    double fu = u * n; int ci = (int)fu; if (ci < 0 || ci >= n) return 0;
    int col = (int)((fu - ci) * 6.0); if (col > 4) return 0;     /* 6th unit = gap */
    int row = (int)((v - vlo) / (vhi - vlo) * 7.0); if (row > 6) row = 6;
    return microfont_pixel(msg[ci], col, row);
}

/* corrupted machine-guts wall texel - STABLE (seeded by cell, no per-frame RNG) so
   the walls read as solid surfaces instead of boiling static. */
static uint32_t wall_base(int cx, int cy, int side, double u, double v, double sh){
    unsigned ch = cellhash(cx, cy, side, 0, 0);
    int baseg = 22 + (int)(ch & 15);              /* dim teal-green base, slight per-cell variation */
    int baseb = 10 + (int)((ch>>4) & 7);
    double grad = 1.0 - fabs(v-0.5)*0.5;          /* soft vertical gradient, brighter mid */
    int r = 2, g = (int)(baseg*grad), b = (int)(baseb*grad);
    int tx = (int)(u * 12.0), ty = (int)(v * 18.0);
    if ((cellhash(cx,cy,side,tx,0) & 7) == 0){ g += 26; b += 8; }      /* vertical circuit trace */
    unsigned gh = cellhash(cx, cy, side, tx, ty);
    if      ((gh & 63) == 0){ g += 70; b += 20; r += 4; }             /* sparse bright data glint */
    else if ((gh & 31) == 0){ g += 26; }                             /* dim data fleck */
    if ((gh & 255) == 0){ r += 120; g = g/2; }                       /* rare red corruption fleck */
    return scale_rgb(RGB32(r,g,b), sh);
}

/* sample one of the player's photos (or the desktop screenshot fallback), corrupted */
static uint32_t image_texel(App *a, int which, double u, double v, double sh, unsigned h){
    int r, g, b;
    if (g_imgs.count > 0){
        const uint32_t *buf = g_imgs.px[which % g_imgs.count];
        int d = g_imgs.dim;
        int sx = (int)(u*d); if (sx<0)sx=0; if (sx>=d)sx=d-1;
        int sy = (int)(v*d); if (sy<0)sy=0; if (sy>=d)sy=d-1;
        uint32_t p = buf[sy*d+sx];
        r=(p>>16)&0xFF; g=(p>>8)&0xFF; b=p&0xFF;
    } else if (a->shot){
        int sx = (int)(u*a->fb.w); if (sx<0)sx=0; if (sx>=a->fb.w)sx=a->fb.w-1;
        int sy = (int)(v*a->fb.h); if (sy<0)sy=0; if (sy>=a->fb.h)sy=a->fb.h-1;
        uint32_t p = a->shot[(size_t)sy*a->fb.w+sx];
        r=(p>>16)&0xFF; g=(p>>8)&0xFF; b=p&0xFF;
    } else {
        return wall_base(0,0,which,u,v,sh);
    }
    /* corrupt: phosphor-green bias, channels crushed, sparse glitch rows */
    int og = (int)(g*0.75 + (r+b)*0.10 + 22);
    int or_ = (int)(r*0.35);
    int ob = (int)(b*0.35);
    if (((h >> 5) & 15) == 0){ or_ = 160; og = og/3; ob = 12; }   /* bleeding red scanrow */
    if (or_>255) or_=255;
    if (og>255) og=255;
    if (ob>255) ob=255;
    or_=(int)(or_*sh); og=(int)(og*sh); ob=(int)(ob*sh);
    return RGB32(or_,og,ob);
}

static uint32_t wall_pixel(App *a, int cx, int cy, int side, double u, double v, double sh){
    if (cx<0||cy<0||cx>=g_mw||cy>=g_mh) return wall_base(cx,cy,side,u,v,sh);
    int tag = g_tag[cy][cx];
    unsigned h = cellhash(cx, cy, side, (int)(u*16.0), (int)(v*24.0));
    if (tag == TAG_IMAGE) return image_texel(a, g_idx[cy][cx], u, v, sh, h);
    if (tag == TAG_MSG){
        int mi = g_idx[cy][cx] % (g_nmsg>0?g_nmsg:1);
        if (text_lit(g_msgs[mi], g_msglen[mi], u, v, 0.40, 0.60)){
            int g = (int)(235*sh), r = (int)(120*sh), b = (int)(140*sh);  /* bright sickly text */
            return RGB32(r,g,b);
        }
    }
    return wall_base(cx,cy,side,u,v,sh);
}

/* exit "model": an animated glowing portal in place of a flat wall */
static uint32_t exit_pixel(App *a, double u, double v, double sh){
    double pulse = 0.5 + 0.5*sin(a->now_ms/200.0);
    int edge = (u<0.14 || u>0.86 || v<0.07 || v>0.93);
    if (edge){
        int g = (int)((150 + 105*pulse) * (0.5+0.5*sh));
        return RGB32(0, g, (int)(g*0.4));                         /* radiant green frame */
    }
    if (text_lit("EXIT", 4, u, v, 0.42, 0.58)){
        return RGB32((int)(180*sh), 255, (int)(200*sh));
    }
    /* dark beckoning core with shifting glyph sparkle */
    unsigned h = cellhash(g_exit_x, g_exit_y, (int)(a->now_ms/90), (int)(u*20), (int)(v*20));
    int core = (h & 15) == 0 ? 60 : 10;
    int g = (int)((core + 30*pulse) * sh);
    return RGB32(0, g, (int)(g*0.5));
}

/* ===================== floor + sky cast ===================== */

static void render_floor_sky(Framebuffer *fb, App *a, int Wd, int H, int horizon,
                             double posx, double posy,
                             double dirx, double diry, double planex, double planey){
    double r0x = dirx - planex, r0y = diry - planey;   /* leftmost ray  (camx=-1) */
    double r1x = dirx + planex, r1y = diry + planey;   /* rightmost ray (camx=+1) */
    int timebkt = (int)(a->now_ms/400);
    for (int y = horizon + 1; y < H; y++){
        int p = y - horizon;
        double rowDist = (0.5 * H) / p;
        double fsh = 1.0 - rowDist / MAXVIEW; if (fsh < 0) fsh = 0; if (fsh > 1) fsh = 1;
        double fogt = 1.0 - fsh;                        /* blend distance into fog */
        double stepx = rowDist * (r1x - r0x) / Wd;
        double stepy = rowDist * (r1y - r0y) / Wd;
        double fx = posx + rowDist * r0x;
        double fy = posy + rowDist * r0y;
        int yc = 2*horizon - y;                         /* mirror row for the ceiling/sky */
        int have_ceil = (yc >= 0 && yc < H);
        uint32_t *frow = fb->px + (size_t)y  * Wd;
        uint32_t *crow = have_ceil ? fb->px + (size_t)yc * Wd : NULL;
        double trail_pulse = 0.55 + 0.45*sin(a->now_ms/250.0 - rowDist*1.4);  /* per row */
        for (int x = 0; x < Wd; x += 2){
            int ix = ifloor(fx), iy = ifloor(fy);
            double fracx = fx - ix, fracy = fy - iy;

            /* ---- floor: dark circuit board + perspective messages + path trail ---- */
            int fr=2, fg=7, fb_=5;
            if (fracx<0.04||fracx>0.96||fracy<0.04||fracy>0.96){ fg = 30; fb_ = 18; fr = 6; } /* seams */
            unsigned fh = cellhash(ix, iy, 7, 0, 0);
            if ((fh & 7) == 0 && g_nmsg > 0){                          /* this floor tile carries text */
                int mi = fh % g_nmsg;
                if (text_lit(g_msgs[mi], g_msglen[mi], fracx, fracy, 0.35, 0.62)){ fr=60; fg=130; fb_=70; }
            }
            if (ix>=0 && iy>=0 && ix<g_mw && iy<g_mh && g_onpath[iy*g_mw+ix]){
                double cxd=fracx-0.5, cyd=fracy-0.5, dd=cxd*cxd+cyd*cyd;
                if (dd < 0.20){
                    /* a bright wave that flows toward the exit (lower exit-distance) */
                    int ed = g_exitdist[iy*g_mw+ix]; if (ed < 0) ed = 0;
                    double flow = 0.5 + 0.5*sin(ed*0.9 + a->now_ms/180.0);
                    int t = (int)((1.0 - dd/0.20) * (110 + 150*flow) * (0.7+0.3*trail_pulse));
                    fr += t/4; fg += t; fb_ += t/2;        /* glowing green-cyan trail */
                }
            }
            uint32_t fc = scale_rgb(RGB32(fr,fg,fb_), fsh);
            fc = blend_col(fc, FOG_COLOR, fogt*0.9);
            frow[x] = fc; if (x+1<Wd) frow[x+1] = fc;

            /* ---- sky/ceiling: cold near-black gradient + sparse digital rain ---- */
            if (have_ceil){
                int sg = (int)(8 * fsh);
                unsigned ch = cellhash(ix, iy, 3, timebkt, 0);
                if ((ch & 31) == 0) sg = (int)((36 + (ch & 31)) * fsh); /* faint falling glyph dots */
                uint32_t cc = blend_col(RGB32(0, sg, (int)(sg*0.6)), FOG_COLOR, fogt*0.7);
                crow[x] = cc; if (x+1<Wd) crow[x+1] = cc;
            }

            fx += stepx*2; fy += stepy*2;
        }
    }
}

/* ============================== render ============================== */

/* full-screen corruption jumpscare while the monster has the player.
   noise + glitch render at low-res, upscaled; the skull + text are crisp on top. */
static void render_catch(App *a){
    Framebuffer *real = &a->fb;
    Framebuffer *fb = lo_target(real);
    int Wd = fb->w, H = fb->h;
    double k = a->world.caught_t / 1500.0; if (k > 1) k = 1;

    for (int y = 0; y < H; y++){
        uint32_t *row = fb->px + (size_t)y*Wd;
        for (int x = 0; x < Wd; x++){
            uint32_t n = rng_next(&a->rng);
            int v = 30 + (int)(n & 0x9F);
            row[x] = ((uint32_t)v << 16) | ((n & 0x10) ? (uint32_t)(v/3) << 8 : 0) | (n & 0x0F);
        }
    }
    gfx_datamosh(fb, &a->rng, 0, 0, Wd, H);
    gfx_rgb_split(fb, 6 + (int)(k*10));
    gfx_slice_tear(fb, &a->rng, 30, 5);
    if ((rng_next(&a->rng) & 3) == 0) gfx_invert_band(fb, 0, H);
    gfx_vignette(fb);

    fb_upscale(real, fb->px, Wd, H);

    int RW = real->w, RH = real->h;
    int frame = (int)(a->world.caught_t / 90);
    int fhh = (int)(real->base_fh * (1.5 + 3.0*k));
    fb_font_for(real, fhh);
    uint32_t sc = (frame & 1) ? RGB32(255,30,40) : RGB32(220,220,220);
    skull_render(real, RW/2, RH/2, frame, sc);
    fb_font_base(real);
    uint32_t tc = (frame & 1) ? COL_RED : COL_WHITE;
    fb_text_center(real, RW/2, RH/6, "PROCESS TERMINATED", tc);
    fb_text_center(real, RW/2, RH - RH/6, "SEGMENTATION FAULT -- IN YOU", COL_RED);
    gfx_scanlines(real, 80);
}

void world_render(App *a){
    Framebuffer *real = &a->fb;
    if (a->world.caught){ render_catch(a); return; }

    Framebuffer *fb = lo_target(real);          /* draw the 3D world low-res, upscale later */
    int Wd = fb->w, H = fb->h;
    fb_clear(fb, 0x00000000);

    double posx = a->world.px, posy = a->world.py, dir = a->world.dir;
    double dirx = cos(dir), diry = sin(dir);
    double planex = -diry * 0.66, planey = dirx * 0.66;

    double pitch_lo = a->world.pitch * H / (double)real->h;   /* scale pitch into low-res */
    int horizon = H/2 + (int)pitch_lo;
    if (horizon < H/8)   horizon = H/8;
    if (horizon > H*7/8) horizon = H*7/8;

    render_floor_sky(fb, a, Wd, H, horizon, posx, posy, dirx, diry, planex, planey);

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
        while (!hit && guard++ < 200){
            if (sidex < sidey){ sidex += ddx; mapx += stepx; side = 0; }
            else              { sidey += ddy; mapy += stepy; side = 1; }
            if (is_wall(mapx, mapy)) hit = 1;
        }
        double perp = (side == 0) ? (sidex - ddx) : (sidey - ddy);
        if (perp < 0.0001) perp = 0.0001;

        int lineh = (int)(H / perp);
        int top   = horizon - lineh/2;        /* slice centered on the look horizon */
        int y0 = top < 0 ? 0 : top;
        int y1 = horizon + lineh/2; if (y1 >= H) y1 = H - 1;

        /* exact fractional hit position along the wall face -> texture U */
        double wallx = (side == 0) ? (posy + perp*rayy) : (posx + perp*rayx);
        wallx -= floor(wallx);

        double sh = 1.0 - perp / MAXVIEW; if (sh < 0) sh = 0;
        if (side == 1) sh *= 0.66;
        int isexit = (mapx == g_exit_x && mapy == g_exit_y);

        double fogt = perp / MAXVIEW; if (fogt > 1) fogt = 1; fogt *= fogt;  /* heavier far */
        double eu = wallx < 0.5 ? wallx : 1.0 - wallx;     /* edge AO (vertical seams) */
        double ao_u = 0.82 + 0.36*eu;

        for (int y = y0; y <= y1; y++){
            double v = (double)(y - top) / (lineh > 0 ? lineh : 1);
            uint32_t col = isexit ? exit_pixel(a, wallx, v, sh)
                                  : wall_pixel(a, mapx, mapy, side, wallx, v, sh);
            double ev = v < 0.5 ? v : 1.0 - v;             /* contact-shadow AO top/bottom */
            double ao = ao_u * (0.6 + 0.9*ev); if (ao > 1) ao = 1;
            col = scale_rgb(col, ao);
            col = blend_col(col, FOG_COLOR, fogt*0.85);
            fb->px[(size_t)y*Wd + x] = col;
            if (x+1 < Wd) fb->px[(size_t)y*Wd + x+1] = col;
        }
        zbuf[x] = perp; if (x+1 < cols) zbuf[x+1] = perp;
    }

    /* exit beacon: a pulsing pillar of green light at the portal, shining through the
       fog (depth-tested, so it only shows with line of sight) - tells you where to go. */
    {
        double bx = (g_exit_x+0.5) - posx, by = (g_exit_y+0.5) - posy;
        double inv = 1.0 / (planex*diry - dirx*planey);
        double btx = inv * (diry*bx - dirx*by);
        double bty = inv * (-planey*bx + planex*by);
        if (bty > 0.15){
            int scx = (int)((Wd/2) * (1.0 + btx/bty));
            int half = 1 + (int)(3.0/bty);
            double pulse = 0.6 + 0.4*sin(a->now_ms/220.0);
            int inten = (int)((90 + 90*pulse) / (1.0 + bty*0.5));
            for (int px = scx-half; px <= scx+half; px++){
                if (px < 0 || px >= cols) continue;
                if (zbuf[px] <= bty) continue;                    /* occluded by a wall */
                double edge = 1.0 - (double)abs(px-scx)/(half+1);
                for (int y = 0; y < H; y++){
                    double vy = 1.0 - fabs(y - horizon)/(double)H;
                    int add = (int)(inten * edge * vy);
                    if (add <= 0) continue;
                    uint32_t c = fb->px[(size_t)y*Wd+px];
                    int r = ((c>>16)&0xFF)+add/4; if (r>255) r=255;
                    int g = ((c>>8)&0xFF)+add;    if (g>255) g=255;
                    int b = (c&0xFF)+add/2;       if (b>255) b=255;
                    fb->px[(size_t)y*Wd+px] = RGB32(r,g,b);
                }
            }
        }
    }

    /* the monster: an unreadable code-error billboard, depth-tested per column.
       its error caption is captured here and drawn crisp after the upscale. */
    int mon_on = 0, mon_sx = 0; char mon_err[64] = {0};
    if (a->world.monster_on){
        double sx = a->world.mx - posx, sy = a->world.my - posy;
        double inv = 1.0 / (planex*diry - dirx*planey);
        double tx = inv * (diry*sx - dirx*sy);
        double ty = inv * (-planey*sx + planex*sy);   /* depth along view */
        if (ty > 0.1){
            int screenx = (int)((Wd/2) * (1.0 + tx/ty));
            int spriteh = (int)(H / ty); if (spriteh > H*2) spriteh = H*2;
            int spritew = spriteh * 2 / 3;
            int y0 = horizon - spriteh/2;
            int x0 = screenx - spritew/2;
            double dist = sqrt(sx*sx + sy*sy);
            double halfw = spritew*0.5;
            /* a coherent dark figure (head + tapering body) of corrupted matter,
               rather than a square of random static */
            for (int px = x0; px < x0 + spritew; px++){
                if (px < 0 || px >= cols) continue;
                if (zbuf[px] <= ty) continue;          /* hidden behind a wall */
                double u2 = (px - screenx) / halfw;     /* -1..1 across the figure */
                for (int py = y0; py < y0 + spriteh; py++){
                    if (py < 0 || py >= H) continue;
                    double v2 = (py - y0) / (double)spriteh;  /* 0 top .. 1 bottom */
                    double rad = (v2 < 0.26) ? 0.20 + 0.14*(v2/0.26)        /* head */
                                             : 0.58 - 0.30*((v2-0.26)/0.74); /* shoulders -> legs */
                    if (fabs(u2) > rad) continue;       /* outside the silhouette */
                    unsigned hs = cellhash(px, py, 11, 0, 0);     /* stable body grain */
                    int base = 6 + (int)(hs & 15);
                    int r = base, g = base, b = base+3;
                    if ((cellhash(px, py, (int)(a->now_ms/110), 0, 0) & 15) == 0){
                        r = 110 + (int)(hs & 63); g = 8; b = 16;  /* sparse red glitch bits */
                    }
                    if (v2>0.08 && v2<0.16 && fabs(fabs(u2)-0.11) < 0.05){
                        r = 255; g = 40; b = 48;        /* two burning eyes */
                    }
                    fb->px[(size_t)py*Wd + px] = RGB32(r,g,b);
                }
            }
            if (x0 > -spritew && x0 < Wd){
                gfx_datamosh(fb, &a->rng, x0, y0, spritew, spriteh);
                const char *ERR[] = { "SEGMENTATION FAULT", "0xDEADBEEF", "STACK SMASHED",
                                      "FATAL: core dumped", "0xC0000005" };
                fb_garble(mon_err, ERR[rng_next(&a->rng) % 5], &a->rng, 45);
                mon_on = 1; mon_sx = screenx;
            }
            double prox = 1.0 - dist/8.0; if (prox < 0) prox = 0; if (prox > 1) prox = 1;
            if (prox > 0) gfx_rgb_split(fb, (int)(prox * 7));
        }
    }

    /* ---- Silent Hill grade on the low-res buffer (cheap: small buffer) ---- */
    gfx_bloom(fb, 150, 50);                  /* faint green glow */
    gfx_desaturate(fb, 55);                  /* muted, sickly palette */
    gfx_dither(fb);                          /* PS1 banded gradients */
    int tension = a->world.monster_on ? 3 : 1;
    if ((rng_next(&a->rng) & 2047) < tension) gfx_brightness(fb, 72 + (int)(rng_next(&a->rng)%22));
    if (a->world.monster_on && (rng_next(&a->rng) & 255) < 2) gfx_slice_tear(fb, &a->rng, 6, 2);
    gfx_grain(fb, &a->rng, 8);               /* heavier film grain */
    gfx_vignette(fb);                        /* tunnel-vision dark edges */

    /* ---- blow the low-res world up to the real screen ---- */
    fb_upscale(real, fb->px, Wd, H);

    /* ---- text / HUD / reticle at full resolution (crisp) ---- */
    int RW = real->w, RH = real->h;
    int rhoriz = horizon * RH / H;          /* reticle on the SAME line the world tilts to */

    /* red corruption flash when the monster lands a hit */
    if (a->world.hurt_t > 0){
        double hk = a->world.hurt_t / 450.0; if (hk > 1) hk = 1;
        int add = (int)(160*hk), keep = 100 - (int)(45*hk);
        for (int y = 0; y < RH; y++){
            uint32_t *row = real->px + (size_t)y*RW;
            for (int x = 0; x < RW; x++){
                uint32_t c = row[x];
                int r = ((c>>16)&0xFF) + add; if (r>255) r=255;
                int g = ((c>>8)&0xFF) * keep/100;
                int b = (c&0xFF)      * keep/100;
                row[x] = RGB32(r,g,b);
            }
        }
        if (hk > 0.3) gfx_rgb_split(real, (int)(7*hk));
    }

    if (mon_on)
        fb_text(real, mon_sx*RW/Wd - (int)strlen(mon_err)*real->ch_w/2, rhoriz, mon_err, COL_RED);

    /* HUD: lives + a health bar */
    char hud[64]; snprintf(hud, sizeof(hud), "LIVES: %d", a->lives);
    fb_text(real, 20, 20, hud, a->lives > 1 ? COL_RED : COL_WHITE);
    int hp = (int)a->world.hp; if (hp < 0) hp = 0;
    char hpt[32]; snprintf(hpt, sizeof(hpt), "HP %d/50", hp);
    fb_text(real, 20, 20 + real->ch_h, hpt, hp > 20 ? COL_GREEN : COL_RED);
    int barw = real->ch_w * 16, barh = real->ch_h - 4;
    int bx = 20, by = 22 + real->ch_h*2;
    fb_fill_rect(real, bx, by, barw, barh, 0x00200000);
    fb_fill_rect(real, bx, by, barw*hp/50, barh, hp > 20 ? COL_GREEN : COL_RED);
    fb_frame(real, bx, by, barw, barh, COL_DGREEN);

    double rel = atan2((g_exit_y + 0.5) - posy, (g_exit_x + 0.5) - posx) - dir;
    while (rel >  PI) rel -= 2*PI;
    while (rel < -PI) rel += 2*PI;
    const char *arrow = fabs(rel) < 0.45 ? "  ^ THE PORTAL IS AHEAD"
                      : (rel > 0 ? "  THE PORTAL -> (turn right)" : "  <- THE PORTAL (turn left)");
    fb_text_center(real, RW/2, 40, arrow, COL_DGREEN);
    if (a->world.elapsed < 7000)
        fb_text_center(real, RW/2, 40 + real->ch_h, "FOLLOW THE GREEN LIGHTS TO THE PORTAL", COL_GREEN);

    /* drifting sky message (screen-space, slow scroll) */
    if (g_nmsg > 0){
        const char *sm = g_msgs[(int)(a->now_ms/3300) % g_nmsg];
        int sx = (int)(RW - fmod(a->now_ms*0.06, (double)(RW + (int)strlen(sm)*real->ch_w)));
        uint32_t scol = RGB32(0, 70, 40);
        if ((rng_next(&a->rng) & 31) == 0){ char g[64]; fb_garble(g, sm, &a->rng, 30);
            fb_text(real, sx, real->ch_h, g, scol); }
        else fb_text(real, sx, real->ch_h, sm, scol);
    }

    /* the warning, with the goal, when the hunter wakes */
    if (a->world.warn_t > 0){
        const char *WARN[] = { "I AM COMING FOR YOU",
                               "YOUR PROCESS WILL BE TERMINATED",
                               "SEGMENTATION FAULT -- IN YOU" };
        uint32_t c = (((int)(a->world.warn_t/120)) & 1) ? COL_RED : COL_WHITE;
        fb_text_center(real, RW/2, RH/3, WARN[a->world.msg % 3], c);
        fb_text_center(real, RW/2, RH/3 + real->ch_h*2, "RUN. REACH THE GREEN PORTAL.", COL_GREEN);
    }

    if (a->world.escaped)
        fb_text_center(real, RW/2, RH/2, "THE PORTAL -- YOU CLAWED OUT", COL_GREEN);

    /* crosshair reticle on the look horizon */
    {
        int cxr = RW/2, cyr = rhoriz;
        uint32_t rc = 0x0030C060u;
        fb_fill_rect(real, cxr-6, cyr,   13, 1, rc);
        fb_fill_rect(real, cxr,   cyr-6, 1, 13, rc);
    }
}
