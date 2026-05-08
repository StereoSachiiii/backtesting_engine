#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <array>

#include "../types/strategy_types.hpp"

class PositionManager {
private:
    std::unique_ptr<std::array<Position, 65536>> positions_;
    std::vector<uint16_t> active_locates_;
    std::array<bool, 65536> is_active_;

public:
    PositionManager() 
        : positions_(std::make_unique<std::array<Position, 65536>>()) {
        is_active_.fill(false);
        for(auto& p : *positions_) p = {};
    }

    void on_fill(uint16_t locate, int64_t qty, double price, bool is_buy) {
        if (!is_active_[locate]) {
            is_active_[locate] = true;
            active_locates_.push_back(locate);
        }
        (*positions_)[locate].update_on_fill(qty, price, is_buy);
    }

    const Position& get_position(uint16_t locate) const {
        return (*positions_)[locate];
    }

    struct ActivePosition {
        uint16_t locate;
        const Position& pos;
    };

    std::vector<ActivePosition> get_all_active_positions() const {
        std::vector<ActivePosition> result;
        result.reserve(active_locates_.size());
        for (uint16_t loc : active_locates_) {
            result.push_back({loc, (*positions_)[loc]});
        }
        return result;
    }
};
