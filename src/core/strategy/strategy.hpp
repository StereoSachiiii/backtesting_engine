#pragma once
#include "../types/order.hpp"
#include "../order_book_manager.hpp"
#include "position_manager.hpp"
#include "pnl_tracker.hpp"

class Strategy {
protected:
    PositionManager pos_mgr_;
    bool stop_requested_ = false;
    
public:
    virtual ~Strategy() = default;
    
    const PositionManager& get_position_manager() const { return pos_mgr_; }
    
    PnLSummary get_pnl(const OrderBookManager& book_mgr) const {
        return PnLTracker::calculate_pnl(pos_mgr_, book_mgr);
    }

    bool should_stop() const { return stop_requested_; }
    void request_stop() { stop_requested_ = true; }
};

