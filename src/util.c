/* util.c - rng + timing */
#include "app.h"

/* xorshift64* */
uint32_t rng_next(uint64_t *s){
    uint64_t x = *s;
    if (x == 0) x = 0x9E3779B97F4A7C15ull;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *s = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1Dull) >> 32);
}

int rng_range(uint64_t *s, int lo, int hi){
    if (hi <= lo) return lo;
    return lo + (int)(rng_next(s) % (uint32_t)(hi - lo + 1));
}

double now_ms(void){
    static LARGE_INTEGER freq;
    static int init = 0;
    LARGE_INTEGER c;
    if (!init){ QueryPerformanceFrequency(&freq); init = 1; }
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)freq.QuadPart;
}
