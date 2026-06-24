/* imageload.c - pull a few real images from the player's Pictures/Downloads and
   decode them into small pixel buffers, so the maze walls can bleed the player's
   OWN photos back at them (corrupted). "Full horror, spare the data": this is
   strictly READ-ONLY - we list filenames and decode pixels for display, nothing
   is modified, moved, or sent anywhere. If no images are found the caller falls
   back to the desktop screenshot already captured at launch.

   Decoding uses OleLoadPicturePath -> IPicture::Render, the classic plain-C way to
   load JPG/PNG/BMP/GIF without GDI+ (a C++ API). Links: -lole32 -loleaut32. */
#include "app.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <ole2.h>
#include <olectl.h>

/* IID_IPicture, defined locally so we don't need to link -luuid */
static const GUID IID_IPicture_local =
    {0x7BF80980,0xBF32,0x101A,{0x8B,0xBB,0x00,0xAA,0x00,0x30,0x0C,0xAB}};

static int has_image_ext(const char *name){
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    char e[8]; int i = 0;
    for (const char *p = dot+1; *p && i < 7; p++) e[i++] = (char)tolower((unsigned char)*p);
    e[i] = 0;
    return !strcmp(e,"jpg") || !strcmp(e,"jpeg") || !strcmp(e,"png") ||
           !strcmp(e,"bmp") || !strcmp(e,"gif");
}

/* decode one image file into a freshly malloc'd dim*dim 0x00RRGGBB buffer. */
static uint32_t *decode_one(const char *path, int dim){
    wchar_t wpath[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH)) return NULL;

    IPicture *pic = NULL;
    HRESULT hr = OleLoadPicturePath(wpath, NULL, 0, 0, &IID_IPicture_local, (void**)&pic);
    if (FAILED(hr) || !pic) return NULL;

    long hmW = 0, hmH = 0;
    pic->lpVtbl->get_Width(pic, &hmW);
    pic->lpVtbl->get_Height(pic, &hmH);
    if (hmW <= 0 || hmH <= 0){ pic->lpVtbl->Release(pic); return NULL; }

    uint32_t *out = NULL;
    HDC screen = GetDC(NULL);
    HDC mem    = CreateCompatibleDC(screen);
    BITMAPINFO bi; memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = dim;
    bi.bmiHeader.biHeight      = -dim;            /* top-down */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (dib && bits){
        HGDIOBJ old = SelectObject(mem, dib);
        /* clear to near-black, then render the picture scaled to fill the square.
           source y starts at hmH and spans -hmH to flip into upright orientation. */
        RECT rc = {0,0,dim,dim};
        HBRUSH bk = CreateSolidBrush(RGB(4,8,5));
        FillRect(mem, &rc, bk);
        DeleteObject(bk);
        hr = pic->lpVtbl->Render(pic, mem, 0, 0, dim, dim, 0, hmH, hmW, -hmH, NULL);
        if (SUCCEEDED(hr)){
            out = (uint32_t*)malloc((size_t)dim * dim * 4);
            if (out) memcpy(out, bits, (size_t)dim * dim * 4);
        }
        SelectObject(mem, old);
        DeleteObject(dib);
    }
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    pic->lpVtbl->Release(pic);
    return out;
}

#define PATHBUF (MAX_PATH*2)

/* collect candidate image paths from one directory (top level only). */
static void scan_dir(const char *dir, char paths[][PATHBUF], char names[][64],
                     int *n, int cap){
    char pat[PATHBUF];
    snprintf(pat, sizeof(pat), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    int guard = 0;
    do {
        if (*n >= cap) break;
        if (guard++ > 8000) break;                       /* never trawl a pathological dir */
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!has_image_ext(fd.cFileName)) continue;
        snprintf(paths[*n], PATHBUF, "%s\\%s", dir, fd.cFileName);
        snprintf(names[*n], 64, "%s", fd.cFileName);
        (*n)++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

void images_load(ImageSet *set, uint64_t *rng, int max){
    memset(set, 0, sizeof(*set));
    set->dim = IMG_DIM;
    if (max > IMG_MAX) max = IMG_MAX;

    static int ole_ready = 0;
    if (!ole_ready){ OleInitialize(NULL); ole_ready = 1; }

    /* candidate paths from Pictures + Downloads (read-only enumeration) */
    enum { CAND_CAP = 512 };
    static char paths[CAND_CAP][PATHBUF];
    static char names[CAND_CAP][64];
    int ncand = 0;

    char home[MAX_PATH];
    DWORD hl = GetEnvironmentVariableA("USERPROFILE", home, sizeof(home));
    if (hl == 0 || hl >= sizeof(home)) return;           /* no home -> caller falls back */

    char dir[PATHBUF];
    snprintf(dir, sizeof(dir), "%s\\Pictures", home);  scan_dir(dir, paths, names, &ncand, CAND_CAP);
    snprintf(dir, sizeof(dir), "%s\\Downloads", home); scan_dir(dir, paths, names, &ncand, CAND_CAP);
    if (ncand == 0) return;

    /* random unique picks, decoding until we have `max` (or run out of candidates) */
    int tries = 0;
    while (set->count < max && tries < ncand * 3){
        tries++;
        int idx = rng_range(rng, 0, ncand - 1);
        if (!paths[idx][0]) continue;                    /* already taken */
        uint32_t *px = decode_one(paths[idx], set->dim);
        /* capture the name for cryptic messages whether or not decode succeeded */
        if (set->nnames < IMG_MAX){ snprintf(set->names[set->nnames], 64, "%s", names[idx]);
                                    set->nnames++; }
        paths[idx][0] = 0;                               /* consume this candidate */
        if (px){ set->px[set->count++] = px; }
    }
}

void images_free(ImageSet *set){
    for (int i = 0; i < set->count; i++){ free(set->px[i]); set->px[i] = NULL; }
    set->count = 0;
}
