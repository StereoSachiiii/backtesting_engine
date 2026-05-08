#pragma once
#include "../hierarchical_bitset.hpp"
#include "../config.hpp"
#include "price_level.hpp"
#include "order.hpp"
#include <cstdint>

/**
 * @brief Packed memory layout for OrderBook - Single allocation, cache-aligned.
 * 
 * Provides O(1) access to all order book 
 *  Hash table for order lookup (262K entries)
 *  Bid/ask price level arrays (1M entries each)
 *  Hierarchical bitsets  (O(1) bid/ask)
 */
struct alignas(4096) OrderBookMemory {
    static constexpr size_t INDEX_SIZE = config::INDEX_SIZE;
    static constexpr size_t PRICE_WINDOW = config::PRICE_WINDOW;

    struct IndexEntry {
        uint64_t ref = 0;
        Order* order = nullptr;
    };

    IndexEntry order_index[INDEX_SIZE];
    alignas(4096) char _pad0[4096]; // Guard page to catch index overflows
    PriceLevel* price_levels[2][PRICE_WINDOW]; // 0 = Bid, 1 = Ask/Sell
    alignas(4096) char _pad1[4096]; // Guard page to catch price level overflows
    HierarchicalBitset bits[2];               // 0 = Bid, 1 = Ask/Sell
};
