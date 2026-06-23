/* screenshot.c - grab the desktop into a 0x00RRGGBB buffer */
#include "app.h"
#include <stdlib.h>

uint32_t *screenshot_capture(int w, int h){
    uint32_t *out = (uint32_t*)malloc((size_t)w * h * 4);
    if (!out) return NULL;

    HDC screen = GetDC(NULL);
    HDC mem    = CreateCompatibleDC(screen);

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;          /* top-down */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp && bits){
        HGDIOBJ old = SelectObject(mem, bmp);
        BitBlt(mem, 0, 0, w, h, screen, 0, 0, SRCCOPY);
        memcpy(out, bits, (size_t)w * h * 4);
        SelectObject(mem, old);
        DeleteObject(bmp);
    } else {
        for (int i = 0; i < w*h; i++) out[i] = COL_BG;
    }
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    return out;
}
