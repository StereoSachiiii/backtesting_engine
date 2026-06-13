#pragma once

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <cmath>
#define __builtin_ctzll _tzcnt_u64
#define __builtin_clzll _lzcnt_u64
#define FORCE_INLINE __forceinline
#define FAST_SQRT(x) sqrt(x)
#define alloc_zeroed_aligned(size) VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
#define free_zeroed_aligned(ptr, size) VirtualFree(ptr, 0, MEM_RELEASE)
#pragma warning(disable: 4324)  // structure was padded due to alignment specifier
#pragma warning(disable: 4201)  // nonstandard extension used: nameless struct/union
#elif defined(__linux__)
#include <sys/mman.h>
#include <cstdlib>
#define FORCE_INLINE inline __attribute__((always_inline))
#define FAST_SQRT(x) __builtin_sqrt(x)
#define alloc_zeroed_aligned(size) ([] (size_t s) { \
    void* p = mmap(nullptr, s, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0); \
    if (p != MAP_FAILED) madvise(p, s, MADV_HUGEPAGE); \
    return (p == MAP_FAILED) ? nullptr : p; \
}(size))
#define free_zeroed_aligned(ptr, size) munmap(ptr, size)
#else
#include <cmath>
#define FORCE_INLINE inline
#define FAST_SQRT(x) std::sqrt(x)
#define alloc_zeroed_aligned(size) [] (size_t s) { void* p = std::aligned_alloc(64, s); if (p) std::memset(p, 0, s); return p; } (size)
#define free_zeroed_aligned(ptr, size) std::free(ptr)
#endif
