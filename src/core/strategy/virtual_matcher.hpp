#pragma once
#include "../types/order.hpp"
#include "../orderbook.hpp"
#include "../types/strategy_types.hpp"
#include <vector>
#include <iostream>
#include <algorithm>
#include <memory>
#include <array>

class VirtualMatcher {
private:
    //  unique_ptr to keep it off the stack but cache problem
    std::unique_ptr<std::array<StockOrders, 65536>> orders_;

public:
    VirtualMatcher() : orders_(std::make_unique<std::array<StockOrders, 65536>>()) {
        //  all orders are initialized to inactive
        for (auto& stock : *orders_) {
            stock.bid.active = false;
            stock.ask.active = false;
        }
    }

    void place_order(uint16_t locate, uint8_t side, uint64_t price, uint32_t qty, uint32_t total_ahead, 
                     PositionManager& pos_mgr, const OrderBook& book) {
        auto& stock = (*orders_)[locate];
        
        // --- Aggressive (Marketable) Fill Simulation ---
        if (side == 'B') {
            uint64_t best_ask = book.best_ask();
            if (best_ask > 0 && price >= best_ask) {
                // Immediate fill at best ask
                pos_mgr.on_fill(locate, (int64_t)qty, (double)best_ask, true);
                stock.bid.active = false;
                std::cout << "[Matcher] AGGRESSIVE FILL BUY: " << qty << " @ " << (best_ask/10000.0) << "\n";
                return;
            }
            stock.bid = {price, qty, total_ahead, true};
        } else {
            uint64_t best_bid = book.best_bid();
            if (best_bid > 0 && price <= best_bid) {
                // Immediate fill at best bid
                pos_mgr.on_fill(locate, (int64_t)qty, (double)best_bid, false);
                stock.ask.active = false;
                std::cout << "[Matcher] AGGRESSIVE FILL SELL: " << qty << " @ " << (best_bid/10000.0) << "\n";
                return;
            }
            stock.ask = {price, qty, total_ahead, true};
        }
    }

    void on_message(const Order& event, PositionManager& pos_mgr, const OrderBookManager& manager) {
        if (event.msg_type != 'E' && event.msg_type != 'C' && event.msg_type != 'X' && event.msg_type != 'D' && event.msg_type != 'U') [[likely]] return;

        auto& stock = (*orders_)[event.stock_locate];
        if (!stock.bid.active && !stock.ask.active) [[likely]] return;

        const Order* original = const_cast<OrderBookManager&>(manager).get_order(event.stock_locate, event.order_ref);
        if (!original) [[unlikely]] return;

        uint64_t price = original->price;
        uint8_t side = original->side;

        VirtualOrder& vo = (side == 'B') ? stock.bid : stock.ask;
        if (!vo.active || vo.price != price) [[likely]] return;

        if (event.msg_type == 'E' || event.msg_type == 'C') {
            if (vo.volume_ahead >= event.shares) [[likely]] {
                vo.volume_ahead -= event.shares;
            } else {
                uint32_t fill_qty = (std::min)(vo.qty, event.shares - vo.volume_ahead);
                vo.volume_ahead = 0;
                
                if (fill_qty > 0) {
                    bool is_buy = (side == 'B');
                    pos_mgr.on_fill(event.stock_locate, (int64_t)fill_qty, (double)vo.price, is_buy);
                    std::cout << "[Matcher] FILL " << (is_buy ? "BUY" : "SELL") << ": " << fill_qty << " @ " << (vo.price / 10000.0) << "\n";
                    vo.qty -= fill_qty;
                    if (vo.qty == 0) vo.active = false;
                }
            }
        } else if (event.msg_type == 'X' || event.msg_type == 'D' || event.msg_type == 'U') {
            uint32_t shares_to_remove = (event.msg_type == 'D' || event.msg_type == 'U') ? original->shares : event.shares;
            if (vo.volume_ahead > 0) {
                vo.volume_ahead = (shares_to_remove >= vo.volume_ahead) ? 0 : vo.volume_ahead - shares_to_remove;
            }
        }
    }

    void on_trade(uint16_t locate, uint64_t price, uint32_t qty, PositionManager& pos_mgr) {
        auto& stock = (*orders_)[locate];
        
        // Handle Bid Side
        if (stock.bid.active && stock.bid.price == price) {
            if (stock.bid.volume_ahead >= qty) {
                stock.bid.volume_ahead -= qty;
            } else {
                uint32_t fill_qty = (std::min)(stock.bid.qty, qty - stock.bid.volume_ahead);
                stock.bid.volume_ahead = 0;
                if (fill_qty > 0) {
                    pos_mgr.on_fill(locate, (int64_t)fill_qty, (double)price, true);
                    std::cout << "[Matcher] TRADE FILL BUY: " << fill_qty << " @ " << (price / 10000.0) << "\n";
                    stock.bid.qty -= fill_qty;
                    if (stock.bid.qty == 0) stock.bid.active = false;
                }
            }
        }

        // Handle Ask Side
        if (stock.ask.active && stock.ask.price == price) {
            if (stock.ask.volume_ahead >= qty) {
                stock.ask.volume_ahead -= qty;
            } else {
                uint32_t fill_qty = (std::min)(stock.ask.qty, qty - stock.ask.volume_ahead);
                stock.ask.volume_ahead = 0;
                if (fill_qty > 0) {
                    pos_mgr.on_fill(locate, (int64_t)fill_qty, (double)price, false);
                    std::cout << "[Matcher] TRADE FILL SELL: " << fill_qty << " @ " << (price / 10000.0) << "\n";
                    stock.ask.qty -= fill_qty;
                    if (stock.ask.qty == 0) stock.ask.active = false;
                }
            }
        }
    }
};
