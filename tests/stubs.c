/* stubs.c - minimal symbols so vmem.c/disk.c link in a console test */
#include "app.h"
#include <stdarg.h>

Datasets g_data;
App      g_app;

void term_print(Terminal *t, uint32_t c, const char *fmt, ...){ (void)t;(void)c;(void)fmt; }
void term_queue(Terminal *t, int d, uint32_t c, const char *fmt, ...){ (void)t;(void)d;(void)c;(void)fmt; }
