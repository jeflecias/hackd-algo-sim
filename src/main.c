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
    case ST_JUMPSCARE:
        jumpscare_update(a, dt);
        break;
    default: break;
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
    case ST_JUMPSCARE:
        jumpscare_render(a);
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
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE){ a->state = ST_QUIT; PostQuitMessage(0); return 0; }
        if (wp == 'Q' && (GetKeyState(VK_CONTROL) & 0x8000)){
            a->state = ST_QUIT; PostQuitMessage(0); return 0;
        }
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
    a->state = ST_GLITCH_INTRO;
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

        Sleep(6);                         /* ~ up to 60-ish fps */
    }
done:
    ShowCursor(TRUE);
    fb_destroy(&a->fb);
    if (a->shot) free(a->shot);
    DestroyWindow(a->hwnd);
    return 0;
}
