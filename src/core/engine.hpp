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

public:
    HFTEngine(StrategyType& strategy) : strategy_(&strategy) {
        ITCHParser::init();
    }

    void set_raw_mode(bool raw) { raw_mode_ = raw; }

   
    FORCE_INLINE void process_message(const uint8_t* data) {
        if (!this || !strategy_) [[unlikely]] return;
        if (!ITCHParser::parse(data, current_order_)) return;

        strategy_->on_event(current_order_, manager_);

        if (current_order_.msg_type == 'A' || current_order_.msg_type == 'F' || 
            current_order_.msg_type == 'E' || current_order_.msg_type == 'C' || 
            current_order_.msg_type == 'X' || current_order_.msg_type == 'D' ||
            current_order_.msg_type == 'U' || current_order_.msg_type == 'R' ||
            current_order_.msg_type == 'S' || current_order_.msg_type == 'P' ||
            current_order_.msg_type == 'Q') {
            
            manager_.process_order(current_order_);
            
            OrderBook* book = manager_.get_book(current_order_.stock_locate);
            if (book) {
                strategy_->on_order_book_update(current_order_.stock_locate, *book, manager_);
                
                if (current_order_.msg_type == 'P' || current_order_.msg_type == 'E') {
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
