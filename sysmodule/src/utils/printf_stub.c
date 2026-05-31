#include <printf/printf.h>
#include <stdarg.h>
#include <stddef.h>

/**
 * Use lightweight printf implementations to avoid increasing the binary size greatly.
 *
 * Currently we only use:
 *  - snprintf: in
 *  - vsnprintf: in Logger
 */

int __wrap_snprintf(char *s, const size_t n, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf_(s, n, fmt, args);
    va_end(args);
    return ret;
}

int __wrap_vsnprintf(char *s, const size_t n, const char *fmt, const va_list args) {
    return vsnprintf_(s, n, fmt, args);
}
