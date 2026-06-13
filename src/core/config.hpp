#pragma once
#include <cstddef>

namespace config {
    // Pool sizing for S&P 500 universe (~500 symbols)
    // Peak live orders: ~2K-8K per symbol, 4M gives headroom for vol spikes
    inline constexpr size_t MAX_ORDERS       = 4'000'000;
    inline constexpr size_t MAX_PRICE_LEVELS = 500'000;

    // Order book geometry
    inline constexpr size_t PRICE_WINDOW     = 1'000'000;
    inline constexpr size_t INDEX_SIZE        = 1 << 20;  // 1M slots — ~50% load at 500K orders/book, cache-friendlier than 8M
    inline constexpr size_t INDEX_MASK        = INDEX_SIZE - 1;

    // OFI signal parameters
    inline constexpr double OFI_ALPHA         = 0.05;    // EMA decay for OFI signal (~20 event lookback)
    inline constexpr double ZSCORE_ALPHA      = 0.001;   // rolling stats decay for z-score normalization
}
