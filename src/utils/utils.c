#include "utils/utils.h"

#include <string.h>
#include <time.h>
#include <ctype.h>

uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

int strip_ansi(const char *src, char *dst, int maxlen)
{
    int di = 0;
    for (int si = 0; src[si] && di < maxlen - 1; si++) {
        if ((unsigned char)src[si] == 0x1b && src[si + 1] == '[') {
            si += 2;
            while (src[si] && !isalpha((unsigned char)src[si]))
                si++;
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
    return di;
}
