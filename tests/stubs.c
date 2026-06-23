/* stubs.c - minimal symbols so vmem.c/disk.c link in a console test */
#include "app.h"
#include <stdarg.h>

Datasets g_data;
App      g_app;

void term_print(Terminal *t, uint32_t c, const char *fmt, ...){ (void)t;(void)c;(void)fmt; }
void term_queue(Terminal *t, int d, uint32_t c, const char *fmt, ...){ (void)t;(void)d;(void)c;(void)fmt; }

/* visualizer stubs so vmem.c / disk.c link in the console test */
void anim_begin(App *a, AnimKind k, const char *t, const char *s){ (void)a;(void)k;(void)t;(void)s; }
void anim_trace(App *a, double c, uint32_t col, const char *f, ...){ (void)a;(void)c;(void)col;(void)f; }
void anim_summary(App *a, uint32_t col, const char *f, ...){ (void)a;(void)col;(void)f; }
