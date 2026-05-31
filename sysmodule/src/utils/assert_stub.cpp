#include "logger.h"

/**
 * Replace newlib's __assert_func to avoid it pulling in fiprintf & its callees (>10KB).
 * Logs via our own Logger and calls std::terminate(), which invokes the
 * handler registered with std::set_terminate() in main.
 */
extern "C" [[noreturn]] void __wrap___assert_func(const char *file, const int line,
                                                  const char *func, const char *expr) {
    Logger::error("ASSERT %s:%d (%s): %s", file, line, func ? func : "?", expr ? expr : "?");
    std::terminate();
}
