cmake_minimum_required(VERSION 3.15)
project(hft_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Performance Flags ----
if(MSVC)
    add_compile_options(/W4 /WX /Ot /Oi /O2 /GL /Gy /arch:AVX2 /Ob2 /fp:fast /GS- /GF /Oy)
    # Enable link-time optimizations and whole-program optimizations
    add_link_options(/LTCG /INCREMENTAL:NO /OPT:REF /OPT:ICF)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING)
else()
    # Distinguish 32-bit vs 64-bit for architecture-specific tuning
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ARCH_FLAGS -O3 -march=native -funroll-loops -fstrict-aliasing -fomit-frame-pointer -flto -fno-plt -fno-semantic-interposition -fdata-sections -ffunction-sections)
    else()
        # i386 / x86 (32-bit) tuning
        set(ARCH_FLAGS -O3 -march=i686 -funroll-loops -mfpmath=sse -msse4.2 -fomit-frame-pointer -flto -fno-plt)
    endif()
    add_compile_options(${ARCH_FLAGS})
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
    # Prefer gold or lld where available for faster linking and better LTO support
    find_program(LD_LD_GOLD gold)
    if(LD_LD_GOLD)
        add_link_options(-fuse-ld=gold -flto)
    else()
        add_link_options(-flto)
    endif()
endif()

option(USE_ASAN "Enable AddressSanitizer" OFF)
if(USE_ASAN)
    if(MSVC)
        add_compile_options(/fsanitize=address /Zi)
        add_link_options(/DEBUG)
    else()
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

# ---- PGO (Profile-Guided Optimization) ----
# PGO is intentionally opt-in. Leave options OFF by default.
option(PGO_GENERATE "Generate PGO profile data" OFF)
option(PGO_USE "Use PGO profile data" OFF)
if(NOT MSVC)
    if(PGO_GENERATE)
        add_compile_options(-fprofile-generate=${CMAKE_BINARY_DIR}/pgo)
        add_link_options(-fprofile-generate=${CMAKE_BINARY_DIR}/pgo)
    elseif(PGO_USE)
        add_compile_options(-fprofile-use=${CMAKE_BINARY_DIR}/pgo -fprofile-correction)
        add_link_options(-fprofile-use=${CMAKE_BINARY_DIR}/pgo)
    endif()
endif()

# Additional optimization ideas (research summary):
# - Use Link Time Code Generation (LTCG) / /GL + /LTCG on MSVC (already enabled).
# - Enable Whole Program Optimization and /Ob2 for aggressive inlining.
# - For GCC/Clang: consider -Ofast for more aggressive FP optimizations if acceptable.
# - Use `-march` tuned to CI/hardware and `-mtune` for target CPUs.
# - Enable `-fprofile-guided-optimization` only when you can collect realistic workloads.
# - Use `-ffunction-sections`/`-fdata-sections` + linker GC-sections to reduce code size.
# - Consider `-fno-exceptions`/`-fno-rtti` where not needed to reduce overhead.
# - For Windows, try `/Gw` (global data optimization) and `/GF` (string pooling).
# - Evaluate using `lld` on Linux for faster LTO linking.

# ---- Core Library ----
add_library(hft_core INTERFACE)
target_include_directories(hft_core INTERFACE 
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(UNIX)
    target_link_libraries(hft_core INTERFACE atomic pthread)
endif()

# ---- Main Executable ----
add_executable(hft_engine src/main.cpp)
target_link_libraries(hft_engine PRIVATE hft_core)

# ---- Benchmarks & Tests ----
function(add_hft_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE hft_core)
endfunction()

add_hft_test(orderbook_test tests/orderbook_test.cpp)
add_hft_test(bitset_test tests/bitset_test.cpp)
add_hft_test(strategy_test tests/strategy_test.cpp)
add_hft_test(full_system_benchmark benchmarks/full_system_benchmark.cpp)
add_hft_test(realistic_itch_benchmark benchmarks/realistic_itch_benchmark.cpp)
add_hft_test(queue_benchmark benchmarks/queue_benchmark.cpp)
add_hft_test(object_pool_test tests/object_pool_test.cpp)

