#pragma once
#include "order_book_manager.hpp"
#include "types/order.hpp"
#include "data/itch_file_parser.hpp"
#include "data/itch_file_reader.hpp"
#include "strategy/strategy.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include "utils/compiler.hpp"

template<typename StrategyType>
class HFTEngine {
private:
    OrderBookManager manager_;
    Order current_order_;
    StrategyType* strategy_;

    // Bitmap: bit N set if char N is a processable message type
    // Covers: A F E C X D U R S P Q
    static constexpr bool is_processable(char c) {
        return c=='A'||c=='F'||c=='E'||c=='C'||c=='X'||c=='D'||c=='U'||c=='R'||c=='S'||c=='P'||c=='Q';
    }
    static constexpr bool is_trade(char c) { return c=='P'||c=='E'; }

    // 256-bit lookup: processable_map_[type] is branch-free
    static constexpr auto make_map() {
        std::array<uint8_t, 256> m{};
        for (int i = 0; i < 256; i++) m[i] = is_processable(static_cast<char>(i)) ? 1 : 0;
        return m;
    }
    static constexpr auto make_trade_map() {
        std::array<uint8_t, 256> m{};
        for (int i = 0; i < 256; i++) m[i] = is_trade(static_cast<char>(i)) ? 1 : 0;
        return m;
    }
    static constexpr auto processable_map_ = make_map();
    static constexpr auto trade_map_ = make_trade_map();

public:
    HFTEngine(StrategyType& strategy) : strategy_(&strategy) {
        ITCHParser::init();
    }

    void set_raw_mode(bool raw) { raw_mode_ = raw; }

   
    FORCE_INLINE void process_message(const uint8_t* data) {
        if (!strategy_) [[unlikely]] return;
        if (!ITCHParser::parse(data, current_order_)) return;

        strategy_->on_event(current_order_, manager_);

        uint8_t type = static_cast<uint8_t>(current_order_.msg_type);
        if (processable_map_[type]) {
            OrderBook* book = manager_.process_order(current_order_);
            if (book) {
                strategy_->on_order_book_update(current_order_.stock_locate, *book, manager_);
                
                if (trade_map_[type]) {
                    strategy_->on_trade(current_order_.stock_locate, static_cast<double>(current_order_.price), current_order_.shares);
                }
            }
        }
    }

    void on_trade(uint16_t locate [[maybe_unused]], double price [[maybe_unused]], uint32_t qty [[maybe_unused]]) {
    }    

    void run_file(const std::string& path) {
        ITCHReader reader;
        reader.set_raw_mode(raw_mode_);
        if (!reader.open(path.c_str())) {
            std::cerr << "Failed to open ITCH file: " << path << "\n";
            return;
        }

        std::vector<uint8_t> buffer(8192);
        size_t msg_len = 0;
        uint64_t msg_count = 0;
        
        std::cout << "[Engine] File Size: " << reader.get_size() << " bytes\n";
        auto start = std::chrono::steady_clock::now();
        
        while (reader.read_next(buffer.data(), msg_len)) {
            process_message(buffer.data());
            
            if (strategy_->should_stop()) [[unlikely]] {
                std::cout << "[Engine] Stop requested by strategy. Terminating early...\n";
                break;
            }

            if (++msg_count == 10000000) {
                std::cout << "[Engine] Starting high-volume ingestion...\n";
            }
            if (msg_count % 25000000 == 0) [[unlikely]] {
                std::cout << "[Engine] Processed " << (msg_count / 1000000) << "M messages...\n";
            }
        }
        auto end = std::chrono::steady_clock::now();
        std::cout << "[Engine] Final Offset: " << reader.get_offset() << " bytes\n";
    }

    OrderBookManager& get_manager() { return manager_; }

    bool raw_mode_ = false;
};
