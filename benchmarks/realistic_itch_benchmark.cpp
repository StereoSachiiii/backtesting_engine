#include "core/data/itch_file_parser.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <cstdint>
#include <random>

volatile int sink;
void DoNotOptimize(const Order& order) {
    sink += *reinterpret_cast<const volatile int*>(&order);
}

int main() {
    ITCHParser::init();

    const size_t num_messages = 10'000'000;
    
    std::cout << "=== SYNTHETIC TEST (L1 Cache, Perfect Branch Prediction) ===\n";
    {
        alignas(64) uint8_t msg[40] = { 0 };
        msg[0] = 'A';  
        for (int i = 0; i < 40; i++) {
            msg[i] = uint8_t(i);
        }

        Order order;
        for (size_t i = 0; i < 100000; i++) {
            ITCHParser::parse(msg, order);
        }

        auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < num_messages; i++) {
            ITCHParser::parse(msg, order);
        }
        auto end = std::chrono::steady_clock::now();
        DoNotOptimize(order);

        auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        double ns_per_msg = static_cast<double>(duration_ns) / static_cast<double>(num_messages);
        double throughput_m = 1e9 / ns_per_msg / 1e6;

        std::cout << "Per message: " << ns_per_msg << " ns\n";
        std::cout << "Throughput: " << throughput_m << " M msgs/sec\n";
        std::cout << "[UNREALISTIC - L1 cache hit 100%, branch prediction 100%]\n\n";
    }

    
    std::cout << "=== REALISTIC TEST (Diverse Data, L3 Cache) ===\n";
    {
        std::vector<uint8_t> large_buffer(40 * num_messages);
        std::mt19937 rng(12345);
        std::uniform_int_distribution<uint32_t> dist(0, 255);

        
        for (size_t i = 0; i < num_messages; i++) {
            size_t offset = i * 40;
            large_buffer[offset] = (rng() % 2 == 0) ? 'A' : 'D';  
            for (int j = 1; j < 40; j++) {
                large_buffer[offset + j] = static_cast<uint8_t>(dist(rng));
            }
        }

        Order order;
        for (size_t i = 0; i < 100000; i++) {
            size_t offset = (i % num_messages) * 40;
            ITCHParser::parse(&large_buffer[offset], order);
        }

        uint64_t checksum = 0;
        auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < num_messages; i++) {
            ITCHParser::parse(&large_buffer[i * 40], order);
            checksum += order.order_ref;
            checksum ^= (order.price << 1);
        }
        auto end = std::chrono::steady_clock::now();
        DoNotOptimize(order);

        auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        double ns_per_msg = static_cast<double>(duration_ns) / static_cast<double>(num_messages);
        double throughput_m = 1e9 / ns_per_msg / 1e6;

        std::cout << "Per message: " << ns_per_msg << " ns\n";
        std::cout << "Throughput: " << throughput_m << " M msgs/sec\n";
        std::cout << "Data size: " << (40 * num_messages / 1e9) << " GB\n";
        std::cout << "[MORE REALISTIC - Random data, branch prediction misses]\n\n";
        std::cout << "Checksum: " << checksum << "\n\n";
    }

    
    std::cout << "=== WITH ORDER BOOK OPERATIONS (Simulated) ===\n";
    {
        std::vector<uint8_t> large_buffer(40 * num_messages);
        std::mt19937 rng(12345);
        std::uniform_int_distribution<uint32_t> dist(0, 255);

        for (size_t i = 0; i < num_messages; i++) {
            size_t offset = i * 40;
            large_buffer[offset] = (rng() % 2 == 0) ? 'A' : 'D';
            for (int j = 1; j < 40; j++) {
                large_buffer[offset + j] = static_cast<uint8_t>(dist(rng));
            }
        }

        Order order;
        std::vector<Order> order_book(10000);  

        for (size_t i = 0; i < 100000; i++) {
            size_t offset = (i % num_messages) * 40;
            ITCHParser::parse(&large_buffer[offset], order);
            
            order_book[order.order_ref % 10000] = order;
        }

        uint64_t checksum = 0;
        auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < num_messages; i++) {
            ITCHParser::parse(&large_buffer[i * 40], order);
            
            order_book[order.order_ref % 10000] = order;
            checksum += order.order_ref;
            checksum ^= (order.price << 1);
        }
        auto end = std::chrono::steady_clock::now();
        DoNotOptimize(order);

        auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        double ns_per_msg = static_cast<double>(duration_ns) / static_cast<double>(num_messages);
        double throughput_m = 1e9 / ns_per_msg / 1e6;

        std::cout << "Per message: " << ns_per_msg << " ns\n";
        std::cout << "Throughput: " << throughput_m << " M msgs/sec\n";
        std::cout << "Checksum: " << checksum << "\n";
    }

    return 0;
}
