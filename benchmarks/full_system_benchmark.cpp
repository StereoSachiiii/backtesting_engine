#include "core/engine.hpp"
#include <chrono>
#include <iostream>
#include <vector>
#include <random>


volatile int sink;
void DoNotOptimize(OrderBookManager& manager) {
    sink += static_cast<int>(manager.get_or_create_book(291, 'P')->bid_qty());
}

int main() {
    struct NoOpStrategy : public Strategy {
        void on_order_book_update(uint16_t, const OrderBook&, const OrderBookManager&) {}
        void on_trade(uint16_t, double, uint32_t) {}
        void on_event(const Order&, const OrderBookManager&) {}

    };

    NoOpStrategy strategy;
    HFTEngine<NoOpStrategy> engine(strategy);

    const size_t iterations = 1'000'000;

    
    std::vector<std::vector<uint8_t>> messages;
    for (size_t i = 0; i < 1000; ++i) {
        std::vector<uint8_t> msg(36, 0);
        msg[0] = 'A'; 
        msg[1] = 0x01; msg[2] = 0x23; 
    
        uint64_t ref = 10000 + i;
        memcpy(&msg[11], &ref, 8);
        msg[19] = 'B'; 
        uint32_t qty = 100;
        memcpy(&msg[20], &qty, 4); 
        uint32_t price = 1000000 + (i % 100) * 100;
        memcpy(&msg[32], &price, 4); 
        messages.push_back(msg);
    }

    std::cout << "Starting Full System Benchmark (Parser + OrderBook)...\n";
    std::cout << "Iterations: " << iterations << "\n";

    for (size_t i = 0; i < 10000; ++i) {
        engine.process_message(messages[i % 1000].data());
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < iterations; ++i) {
        engine.process_message(messages[i % 1000].data());
        if (i % 1000 == 0) DoNotOptimize(engine.get_manager());
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    double ns_per_msg = static_cast<double>(duration_ns) / iterations;
    std::cout << "Total time: " << (duration_ns / 1e6) << " ms\n";
    std::cout << "Latency: " << ns_per_msg << " ns/msg\n";
    std::cout << "Throughput: " << (1e9 / ns_per_msg / 1e6) << " M msgs/sec\n";

    return 0;
}
