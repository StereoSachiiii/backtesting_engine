#pragma once
#include "config.hpp"
#include "orderbook.hpp"
#include "data/itch_file_parser.hpp"

#include <array>
#include <unordered_set>
#include <string>
#include <algorithm>
#include <iostream>

#include "utils/compiler.hpp"

class OrderBookManager {
private:
    std::unique_ptr<std::array<OrderBook*, 65536>> books_;
    std::unordered_set<std::string> whitelisted_symbols_;

    SingleThreadedObjectPool<Order, config::MAX_ORDERS> order_pool_;
    SingleThreadedObjectPool<PriceLevel, config::MAX_PRICE_LEVELS> level_pool_;
    uint64_t message_count_ = 0;

    // Dispatch table: one indirect call instead of switch
    using DispatchFn = void(*)(OrderBook*, const Order&);
    static inline DispatchFn dispatch_[256] = {};

    static void dispatch_add(OrderBook* b, const Order& o)     { b->apply_add(o.order_ref, o.side, o.price, o.shares, o.stock_locate, o.timestamp_ns); }
    static void dispatch_execute(OrderBook* b, const Order& o) { b->apply_execute(o.order_ref, o.shares, o.timestamp_ns); }
    static void dispatch_cancel(OrderBook* b, const Order& o)  { b->apply_cancel(o.order_ref, o.shares, o.timestamp_ns); }
    static void dispatch_delete(OrderBook* b, const Order& o)  { b->apply_delete(o.order_ref, o.timestamp_ns); }
    static void dispatch_replace(OrderBook* b, const Order& o) { b->apply_replace(o.order_ref, o.new_order_ref, o.shares, o.price, o.timestamp_ns); }
    static void dispatch_noop(OrderBook*, const Order&) {}

public:
    OrderBookManager() : books_(std::make_unique<std::array<OrderBook*, 65536>>()) {
        books_->fill(nullptr);
        if (dispatch_['A'] == nullptr) {
            dispatch_['A'] = dispatch_add;
            dispatch_['F'] = dispatch_add;
            dispatch_['E'] = dispatch_execute;
            dispatch_['C'] = dispatch_cancel;
            dispatch_['X'] = dispatch_cancel;
            dispatch_['D'] = dispatch_delete;
            dispatch_['U'] = dispatch_replace;
            dispatch_['R'] = dispatch_noop;
            dispatch_['S'] = dispatch_noop;
            dispatch_['P'] = dispatch_noop;
            dispatch_['Q'] = dispatch_noop;
        }
    }

    uint64_t get_message_count() const { return message_count_; }

    ~OrderBookManager() {
        if (!books_) return;
        for (OrderBook* book : *books_) {
            if (book) {
                book->~OrderBook();
                free_zeroed_aligned(book, sizeof(OrderBook));
            }
        }
    }

    void add_to_whitelist(std::string symbol) {
        if (whitelisted_symbols_.empty()) {
            ITCHParser::clear_interest();
        }
        // ITCH symbols are 8 bytes, space-padded
        while (symbol.length() < 8) symbol += ' ';
        whitelisted_symbols_.insert(symbol);
    }
    OrderBook* get_or_create_book(uint16_t locate, char market_category) {
        if (!books_) [[unlikely]] return nullptr;
        OrderBook* book = (*books_)[locate];
        if (!book) [[unlikely]] {
            book = static_cast<OrderBook*>(alloc_zeroed_aligned(sizeof(OrderBook)));
            if (book) {
                new (book) OrderBook(order_pool_, level_pool_);
                book->set_market_category(market_category);
                (*books_)[locate] = book;
            }
        }
        return book;
    }

    FORCE_INLINE OrderBook* process_order(const Order& order) {
        message_count_++;
        if (order.msg_type == 'R') [[unlikely]] {
            std::string sym(order.symbol, 8);
            std::cout << "[Engine] Discovered Ticker: " << sym << " (Locate: " << order.stock_locate << ")" << std::endl;
            if (whitelisted_symbols_.empty() || whitelisted_symbols_.count(sym)) {
                ITCHParser::set_interest(order.stock_locate, true);
                OrderBook* book = get_or_create_book(order.stock_locate, order.market_category);
                if (book) book->set_market_category(order.market_category);
            }
            return nullptr;
        }

        if (order.stock_locate >= 65536) return nullptr;
        OrderBook* book = get_or_create_book(order.stock_locate, order.market_category);
        if (!book) return nullptr;

        DispatchFn fn = dispatch_[static_cast<uint8_t>(order.msg_type)];
        if (fn) [[likely]] fn(book, order);
        return book;
    }

    Order* get_order(uint16_t locate, uint64_t ref) {
        OrderBook* book = (*books_)[locate];
        return book ? book->get_order(ref) : nullptr;
    }

    OrderBook* get_book(uint16_t locate) {
        return (*books_)[locate];
    }

    const OrderBook* get_book(uint16_t locate) const {
        return (*books_)[locate];
    }
};
