cmake_minimum_required(VERSION 3.15)
project(hft_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Performance Flags ----
if(MSVC)
    add_compile_options(/W4 /WX /Ot /Oi /O2 /GL /Gy /arch:AVX2)
    add_link_options(/LTCG /INCREMENTAL:NO /OPT:REF /OPT:ICF)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING)
else()
    add_compile_options(-O3 -march=native -flto -fno-plt -fno-semantic-interposition)
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
    add_link_options(-flto -fuse-ld=gold)
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
add_hft_test(object_pool_test tests/object_pool_test.cpp)
