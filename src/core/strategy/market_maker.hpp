#pragma once
#include "strategy.hpp"
#include "virtual_matcher.hpp"
#include <iostream>
#include <iomanip>
#include <memory>
#include <cmath>
#include <vector>
#include <cstring>

class SimpleMarketMaker : public Strategy {
private:
    std::vector<std::string> symbols_;
    VirtualMatcher matcher_;

    static constexpr double MICROPRICE_W = 0.6;
    static constexpr double DEEP_IMB_W   = 0.15;
    static constexpr double OFI_W        = 15.0;
    static constexpr double TRADE_IMB_W  = 8.0;

    static constexpr int32_t MAX_POSITION   = 5000;
    static constexpr double  EDGE_LEAN_FRAC = 0.3;
    static constexpr double  STOP_LOSS      = -2000.0;

    struct PerSymbolState {
        double trade_imbalance_ema = 0.0;
        double last_mid = 0.0;
        uint64_t bid_price = 0;
        uint64_t ask_price = 0;
        bool active = false;
    };

    std::unique_ptr<std::array<PerSymbolState, 65536>> state_;

    static constexpr double TRADE_EMA_ALPHA = 0.02;

public:
    SimpleMarketMaker(const std::vector<std::string>& syms)
        : state_(std::make_unique<std::array<PerSymbolState, 65536>>()) {
        for (auto s : syms) {
            while (s.length() < 8) s += ' ';
            symbols_.push_back(s);
        }
        for (auto& s : *state_) s = {};
    }

    void on_event(const Order& event, const OrderBookManager& manager) {
        if (event.msg_type == 'R') [[unlikely]] {
            std::string sym(event.symbol, 8);
            for (const auto& target : symbols_) {
                if (sym == target) {
                    (*state_)[event.stock_locate].active = true;
                    break;
                }
            }
        }
        if ((*state_)[event.stock_locate].active)
            matcher_.on_message(event, pos_mgr_, manager);
    }

    void on_order_book_update(uint16_t locate, const OrderBook& book, const OrderBookManager& manager) {
        if (!(*state_)[locate].active) [[likely]] return;

        uint64_t bb = book.best_bid();
        uint64_t ba = book.best_ask();
        if (bb == 0 || ba == 0) return;
        double spread = (double)(ba - bb);
        if (spread <= 0) return;

        auto& ss = (*state_)[locate];
        double mid = (double)(bb + ba) / 2.0;
        double microprice = book.weighted_mid();
        double ofi_z = book.get_ofi_zscore();
        double deep_imb = book.get_book_imbalance(5);

        double edge = MICROPRICE_W * (microprice - mid)
                    + DEEP_IMB_W * deep_imb * spread
                    + OFI_W * ofi_z
                    + TRADE_IMB_W * ss.trade_imbalance_ema;

        int32_t position = (int32_t)pos_mgr_.get_position(locate).net_qty;

        bool allow_buy  = (position < MAX_POSITION);
        bool allow_sell = (position > -MAX_POSITION);

        if (edge > spread * EDGE_LEAN_FRAC) {
            if (position <= 0) allow_sell = false;
        } else if (edge < -spread * EDGE_LEAN_FRAC) {
            if (position >= 0) allow_buy = false;
        }

        if (position > 0) allow_sell = true;
        if (position < 0) allow_buy  = true;

        uint64_t quote_bid = allow_buy  ? bb : 0;
        uint64_t quote_ask = allow_sell ? ba : 0;

        if (quote_bid != ss.bid_price) {
            ss.bid_price = quote_bid;
            if (quote_bid > 0) {
                uint32_t ahead = book.qty_at_price('B', quote_bid);
                matcher_.place_order(locate, 'B', quote_bid, 100, ahead, pos_mgr_, book);
            }
        }

        if (quote_ask != ss.ask_price) {
            ss.ask_price = quote_ask;
            if (quote_ask > 0) {
                uint32_t ahead = book.qty_at_price('S', quote_ask);
                matcher_.place_order(locate, 'S', quote_ask, 100, ahead, pos_mgr_, book);
            }
        }

        static uint64_t update_count = 0;
        if (++update_count % 2000000 == 0) {
            auto pnl = get_pnl(manager);
            std::cout << std::fixed << std::setprecision(2)
                      << "[MM] Pos: " << position
                      << " | PnL: $" << pnl.total
                      << " (R: $" << pnl.realized << ")\n";
        }

        auto pnl = get_pnl(manager);
        if (pnl.total <= STOP_LOSS) {
            std::cout << "[Strategy] Stop Loss: $" << pnl.total << "\n";
            request_stop();
        }

        ss.last_mid = mid;
    }

    void on_trade(uint16_t locate, double price, uint32_t qty) {
        if (!(*state_)[locate].active) [[likely]] return;
        auto& ss = (*state_)[locate];
        if (ss.last_mid > 0) {
            double signed_qty = (price >= ss.last_mid) ? (double)qty : -(double)qty;
            ss.trade_imbalance_ema = TRADE_EMA_ALPHA * (signed_qty / 100.0) + (1.0 - TRADE_EMA_ALPHA) * ss.trade_imbalance_ema;
        }
        matcher_.on_trade(locate, (uint64_t)price, qty, pos_mgr_);
    }
};
