#ifndef ESPILON_UTILS_H
#define ESPILON_UTILS_H

#include <stdint.h>

/* Monotonic timestamp in milliseconds */
uint64_t now_ms(void);

/* Strip ANSI escape sequences from src into dst (null-terminated).
 * Returns the visual length of the stripped string.
 * dst must be at least maxlen bytes. */
int strip_ansi(const char *src, char *dst, int maxlen);

#endif /* ESPILON_UTILS_H */
