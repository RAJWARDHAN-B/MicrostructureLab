#pragma once

#include <cstdio>
#include <cstdlib>

namespace mlab::detail {

[[noreturn]] inline void assertion_failed(const char* expression, const char* file, int line) noexcept {
    std::fprintf(stderr, "MicrostructureLab verification failed: %s (%s:%d)\n", expression, file, line);
    std::abort();
}

}

#define MLAB_VERIFY(expression)                                                                     \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            ::mlab::detail::assertion_failed(#expression, __FILE__, __LINE__);                        \
        }                                                                                          \
    } while (false)

#ifdef NDEBUG
#define MLAB_ASSERT(expression) static_cast<void>(0)
#else
#define MLAB_ASSERT(expression) MLAB_VERIFY(expression)
#endif