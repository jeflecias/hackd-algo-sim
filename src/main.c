/* main.c - DEADLOCK entry point: window, loop, state machine */
#include "app.h"
#include <string.h>
#include <stdlib.h>

App g_app;

static void update(double dt){
    App *a = &g_app;
    a->now_ms += dt;
    a->state_time += dt;
    term_update(a, dt);

    switch (a->state){
    case ST_GLITCH_INTRO:
    case ST_BOOT_INFECT:
    case ST_SKULL_REVEAL:
    case ST_WELCOME:
        intro_update(a, dt);
        break;
    case ST_TERMINAL:
        if (!a->busy_anim && a->now_ms >= a->scare_at)
            jumpscare_trigger(a);          /* deferred automatically while an anim runs */
        break;
    case ST_ANIM:
        anim_update(a, dt);
        a->scare_at += dt;                 /* freeze the jumpscare countdown */
        break;
    case ST_DATAEDIT:
        dataedit_update(a, dt);            /* freezes the countdown itself */
        break;
    case ST_JUMPSCARE:
        jumpscare_update(a, dt);
        break;
    case ST_FOURTHWALL:
        fourthwall_update(a, dt);
        break;
    case ST_WORLD:
        world_update(a, dt);
        break;
    case ST_GAMEOVER:
        gameover_update(a, dt);
        break;
    default: break;
    }

    /* ambient dread: sparse drips/whispers, and a fake "reading your files" scan */
    {
        static double ambient_at = 4500.0, scan_at = 11000.0;
        if (a->state == ST_TERMINAL || a->state == ST_ANIM || a->state == ST_DATAEDIT){
            if (a->now_ms >= ambient_at){
                audio_sfx((rng_next(&a->rng) & 1) ? SFX_DRIP : SFX_WHISPER, 0);
                ambient_at = a->now_ms + rng_range(&a->rng, 4000, 11000);
            }
        }
        if (a->state == ST_TERMINAL && !a->busy_anim && a->now_ms >= scan_at){
            static const char *FILES[] = {
                "/home/user/Documents/taxes.pdf", "/home/user/.ssh/id_rsa",
                "/home/user/Pictures/IMG_0666.jpg", "/home/user/.bash_history",
                "/etc/shadow", "/home/user/Desktop/diary.txt", "/home/user/wallet.dat" };
            term_print(&a->term, COL_DGREEN, "[scan] reading %s ... done",
                       FILES[rng_next(&a->rng) % 7]);
            audio_sfx(SFX_SCAN, 0);
            scan_at = a->now_ms + rng_range(&a->rng, 9000, 20000);
        }
    }

    /* drive the procedural dread bed by state */
    {
        double lvl = 0.18;
        switch (a->state){
        case ST_TERMINAL:     lvl = 0.15; break;
        case ST_ANIM:         lvl = 0.35; break;
        case ST_DATAEDIT:     lvl = 0.30; break;
        case ST_GLITCH_INTRO:
        case ST_BOOT_INFECT:  lvl = 0.42; break;
        case ST_SKULL_REVEAL: lvl = 0.72; break;
        case ST_JUMPSCARE:
            lvl = (a->scare.phase == 1)
                ? 0.45 + 0.50 * (1.0 - a->scare.time_left / 15000.0)  /* rises as timer drains */
                : 0.85;
            break;
        case ST_FOURTHWALL: lvl = 0.62; break;
        case ST_WORLD:      lvl = a->world.monster_on ? 0.88 : 0.50; break;
        case ST_GAMEOVER:   lvl = 0.92; break;
        default: lvl = 0.20; break;
        }
        audio_drone((float)lvl);
    }
}

static void render(void){
    App *a = &g_app;
    switch (a->state){
    case ST_GLITCH_INTRO:
    case ST_BOOT_INFECT:
    case ST_SKULL_REVEAL:
    case ST_WELCOME:
        intro_render(a);
        break;
    case ST_TERMINAL:
        term_render(a);
        break;
    case ST_ANIM:
        anim_render(a);
        break;
    case ST_DATAEDIT:
        dataedit_render(a);
        break;
    case ST_JUMPSCARE:
        jumpscare_render(a);
        break;
    case ST_FOURTHWALL:
        fourthwall_render(a);
        break;
    case ST_WORLD:
        world_render(a);
        break;
    case ST_GAMEOVER:
        gameover_render(a);
        break;
    default:
        fb_clear(&a->fb, COL_BG);
        break;
    }
    HDC dc = GetDC(a->hwnd);
    fb_present(&a->fb, dc);
    ReleaseDC(a->hwnd, dc);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp){
    App *a = &g_app;
    switch (msg){
    case WM_CHAR:
        if (wp >= 32 && wp < 127){
            if (a->state == ST_JUMPSCARE) jumpscare_key_char(a, (char)wp);
            else if (a->state == ST_TERMINAL) term_key_char(a, (char)wp);
            else if (a->state == ST_DATAEDIT) dataedit_key_char(a, (char)wp);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == 'Q' && (GetKeyState(VK_CONTROL) & 0x8000)){
            a->state = ST_QUIT; PostQuitMessage(0); return 0;   /* global panic quit */
        }
        if (a->state == ST_ANIM){
            if (wp == VK_ESCAPE) anim_exit(a);                  /* ESC -> back to shell */
            else anim_key(a, (int)wp);
            return 0;
        }
        if (a->state == ST_DATAEDIT){
            if (wp == VK_ESCAPE){ a->state = ST_TERMINAL; a->state_time = 0; } /* ESC -> shell */
            else dataedit_key_special(a, (int)wp);
            return 0;
        }
        /* ESC = panic-exit only from the shell; inside the world/cinematics you're
           trapped (Ctrl+Q above is the universal escape hatch) */
        if (wp == VK_ESCAPE && a->state == ST_TERMINAL){ a->state = ST_QUIT; PostQuitMessage(0); return 0; }
        if (a->state == ST_JUMPSCARE) jumpscare_key_special(a, (int)wp);
        else if (a->state == ST_TERMINAL) term_key_special(a, (int)wp);
        return 0;
    case WM_SETCURSOR:
        SetCursor(NULL);   /* hide cursor over our window */
        return TRUE;
    case WM_CLOSE:
        a->state = ST_QUIT; PostQuitMessage(0); return 0;
    case WM_DESTROY:
        PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show){
    (void)hPrev; (void)cmd; (void)show;
    App *a = &g_app;
    memset(a, 0, sizeof(*a));

    data_reset();
    a->rng = (uint64_t)now_ms() ^ (uint64_t)GetTickCount64() ^ 0xDEADBEEFCAFEull;

    /* become DPI-aware so SM_CXSCREEN reports TRUE physical pixels (no DWM
       upscaling/blur) and our framebuffer matches the monitor exactly */
    SetProcessDPIAware();

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    /* capture the desktop BEFORE we cover it */
    a->shot = screenshot_capture(sw, sh);

    WNDCLASSA wc; memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = "DeadlockWnd";
    wc.hCursor       = NULL;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&wc);

    a->hwnd = CreateWindowExA(
        WS_EX_TOPMOST,
        "DeadlockWnd", "deadlock",
        WS_POPUP,
        0, 0, sw, sh,
        NULL, NULL, hInst, NULL);
    if (!a->hwnd) return 1;

    if (!fb_create(&a->fb, sw, sh)) return 2;
    term_init(&a->term);
    audio_init();
    a->state = ST_GLITCH_INTRO;
    a->lives = 3;         /* lose-sequence lives; 0 -> corrupted exit */
    a->scare_at = 1e18;   /* not scheduled until the shell is reached */

    ShowWindow(a->hwnd, SW_SHOW);
    SetForegroundWindow(a->hwnd);
    SetFocus(a->hwnd);
    ShowCursor(FALSE);

    double last = now_ms();
    MSG m;
    for (;;){
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)){
            if (m.message == WM_QUIT) goto done;
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        if (a->state == ST_QUIT) break;

        double t = now_ms();
        double dt = t - last; last = t;
        if (dt > 100) dt = 100;          /* clamp after stalls */

        update(dt);
        render();
        audio_update();                   /* refill streaming sound buffers */

        Sleep(6);                         /* ~ up to 60-ish fps */
    }
done:
    audio_shutdown();
    ShowCursor(TRUE);
    fb_destroy(&a->fb);
    if (a->shot) free(a->shot);
    DestroyWindow(a->hwnd);
    return 0;
}
