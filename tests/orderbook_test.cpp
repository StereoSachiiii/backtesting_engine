#include "core/orderbook.hpp"
#include <iostream>
#include <cassert>

void test_orderbook_basic() {
    std::cout << "Starting test_orderbook_basic...\n";
    ObjectPool<Order, config::MAX_ORDERS> pool;
    ObjectPool<PriceLevel, config::MAX_PRICE_LEVELS> lvl_pool;
    OrderBook book(pool, lvl_pool);

    std::cout << "Adding Bid...\n";
    book.apply_add(1, 'B', 1000000, 100, 291, 0);
    assert(book.best_bid() == 1000000);
    assert(book.bid_qty() == 100);

    std::cout << "Adding Ask...\n";
    book.apply_add(2, 'S', 1000500, 50, 291, 0);
    assert(book.best_ask() == 1000500);
    assert(book.ask_qty() == 50);

    // Cancel partial
    book.apply_cancel(1, 40, 0);
    assert(book.bid_qty() == 60);

    // Delete ask
    book.apply_delete(2, 0);
    assert(book.best_ask() == 0);
    assert(book.ask_qty() == 0);

    std::cout << "test_orderbook_basic OK\n";
}

void test_orderbook_sorting() {
    ObjectPool<Order, config::MAX_ORDERS> pool;
    ObjectPool<PriceLevel, config::MAX_PRICE_LEVELS> lvl_pool;
    OrderBook book(pool, lvl_pool);

    book.apply_add(1, 'B', 1000000, 100, 291, 0);
    book.apply_add(2, 'B', 1000100, 100, 291, 0); // Higher price
    book.apply_add(3, 'B', 999900, 100, 291, 0);  // Lower price

    assert(book.best_bid() == 1000100);
    
    book.apply_delete(2, 0);
    assert(book.best_bid() == 1000000);

    book.apply_delete(1, 0);
    assert(book.best_bid() == 999900);

    book.apply_delete(3, 0);
    assert(book.best_bid() == 0);

    std::cout << "test_orderbook_sorting OK\n";
}

void test_orderbook_price_window() {
    std::cout << "Starting test_orderbook_price_window...\n";
    ObjectPool<Order, config::MAX_ORDERS> pool;
    ObjectPool<PriceLevel, config::MAX_PRICE_LEVELS> lvl_pool;
    OrderBook book(pool, lvl_pool);

    // Initial price sets the window
    book.apply_add(1, 'B', 1000000, 100, 1, 0);
    
    // Within window
    book.apply_add(2, 'B', 1499999, 100, 1, 0);
    assert(book.best_bid() == 1499999);

    // Outside window (1,000,000 + 500,000 - 1,000,000 = 500,000 is base_price_)
    // Price 2,000,000 is 1,500,000 from base_price_, which is > PRICE_WINDOW
    book.apply_add(3, 'B', 2000000, 100, 1, 0);
    assert(book.best_bid() == 1499999); // Should still be 1,499,999

    std::cout << "test_orderbook_price_window OK\n";
}

int main() {
    test_orderbook_basic();
    test_orderbook_sorting();
    test_orderbook_price_window();
    std::cout << "All OrderBook tests passed!\n";
    return 0;
}
