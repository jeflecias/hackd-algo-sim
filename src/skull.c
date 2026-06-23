/* skull.c - ASCII laughing skull, a few animation frames */
#include "app.h"
#include <string.h>

/* Each frame is an array of rows; frames differ in eyes/jaw to "laugh". */
static const char *SK0[] = {
"        .-=========-.        ",
"       (  .-------.  )       ",
"      /  /  _   _  \\  \\      ",
"     |  |  (o) (o)  |  |     ",
"     |  |    .^.    |  |     ",
"     |  |   '---'   |  |     ",
"      \\  \\ |||||||| /  /      ",
"       '. '-------' .'       ",
"      .-'-._______.-'-.      ",
"     /   HA   HA   HA   \\    ",
"    '----------------------' ",
0};

static const char *SK1[] = {
"        .-=========-.        ",
"       (  .-------.  )       ",
"      /  /  O   O  \\  \\      ",
"     |  |  (O) (O)  |  |     ",
"     |  |    .^.    |  |     ",
"     |  |   ' . '   |  |     ",
"      \\  \\ |_|_|_|_ /  /      ",
"       '. '-------' .'       ",
"      .-'-._______.-'-.      ",
"     /  HA  HA  HA  HA  \\    ",
"    '----------------------' ",
0};

static const char *SK2[] = {
"        .-=========-.        ",
"       (  .-------.  )       ",
"      /  /  _   _  \\  \\      ",
"     |  |  (X) (X)  |  |     ",
"     |  |    .^.    |  |     ",
"     |  |   '---'   |  |     ",
"      \\  \\ |/|/|/|/ /  /      ",
"       '. '-------' .'       ",
"      .-'-._______.-'-.      ",
"     /  AH AH AH AH AH  \\    ",
"    '----------------------' ",
0};

static const char **FRAMES[] = { SK0, SK1, SK2, SK1 };

int skull_height_px(Framebuffer *fb){ return 11 * fb->ch_h; }
int skull_width_px(Framebuffer *fb){ return 28 * fb->ch_w; }

void skull_render(Framebuffer *fb, int cx, int cy, int frame, uint32_t color){
    const char **F = FRAMES[((unsigned)frame) % 4];
    int rows = 0; while (F[rows]) rows++;
    int totalh = rows * fb->ch_h;
    int y = cy - totalh/2;
    for (int r = 0; r < rows; r++){
        int w = (int)strlen(F[r]) * fb->ch_w;
        int x = cx - w/2;
        fb_text(fb, x, y, F[r], color);
        y += fb->ch_h;
    }
}
