#pragma once

#include <atomic>
#include <cstddef>

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <immintrin.h>
#elif defined(__aarch64__) || defined(__arm__)
#include <arm_acle.h>
#elif defined(_MSC_VER) && defined(_M_ARM64)
#include <intrin.h>
#endif

#if defined(_MSC_VER)
#define MLAB_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define MLAB_FORCE_INLINE inline __attribute__((always_inline))
#else
#define MLAB_FORCE_INLINE inline
#endif

#if defined(__GNUC__) || defined(__clang__)
#define MLAB_LIKELY(expression) (__builtin_expect(static_cast<bool>(expression), true) != 0)
#define MLAB_UNLIKELY(expression) (__builtin_expect(static_cast<bool>(expression), false) != 0)
#else
#define MLAB_LIKELY(expression) static_cast<bool>(expression)
#define MLAB_UNLIKELY(expression) static_cast<bool>(expression)
#endif

namespace mlab {

#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
inline constexpr std::size_t kCacheLine = 128;
#else
inline constexpr std::size_t kCacheLine = 64;
#endif

inline constexpr std::size_t kContendedAlignment = 128;

MLAB_FORCE_INLINE void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    _mm_pause();
#elif defined(__aarch64__) || defined(__arm__) || (defined(_MSC_VER) && defined(_M_ARM64))
    __yield();
#else
    std::atomic_signal_fence(std::memory_order_acq_rel);
#endif
}

}