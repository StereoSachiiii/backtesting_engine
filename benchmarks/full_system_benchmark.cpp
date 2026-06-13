#include "core/engine.hpp"
#include <chrono>
#include <iostream>
#include <vector>
#include <random>

// Compiler barrier — prevents reordering/elimination across this point
static void compiler_barrier() {
    asm volatile("" ::: "memory");
}

// Volatile sink that accumulates real observable state every iteration.
// The compiler cannot prove any process_message call is dead because
// we read order-book state that depends on every prior mutation.
volatile uint64_t sink = 0;

int main() {
    struct NoOpStrategy : public Strategy {
        void on_order_book_update(uint16_t, const OrderBook&, const OrderBookManager&) {}
        void on_trade(uint16_t, double, uint32_t) {}
        void on_event(const Order&, const OrderBookManager&) {}
    };

    NoOpStrategy strategy;
    HFTEngine<NoOpStrategy> engine(strategy);

    const size_t iterations = 1'000'000;

    // Build a diverse message set with unique order refs so the order book
    // actually grows (adds, deletes, cancels) instead of hitting the same slot.
    std::vector<std::vector<uint8_t>> messages;
    messages.reserve(iterations);

    std::mt19937 rng(42);
    for (size_t i = 0; i < iterations; ++i) {
        std::vector<uint8_t> msg(36, 0);

        // Cycle through Add / Delete / Cancel to exercise real order book paths
        int kind = i % 4;
        if (kind == 0 || kind == 3) {
            msg[0] = 'A';  // Add
        } else if (kind == 1) {
            msg[0] = 'D';  // Delete
        } else {
            msg[0] = 'C';  // Cancel (partial)
        }

        msg[1] = 0x01; msg[2] = 0x23; // timestamp

        // Unique ref per add; deletes/cancels reference a previously-added order
        uint64_t ref;
        if (kind == 0 || kind == 3) {
            ref = 10000 + i;
        } else {
            // reference an earlier add (guaranteed to exist if kind 0/3 ran first)
            size_t target = (i / 4) * 4; // the most recent add index
            ref = 10000 + target;
        }
        memcpy(&msg[11], &ref, 8);

        msg[19] = 'B'; // buy side

        uint32_t qty = 100 + (rng() % 500);
        memcpy(&msg[20], &qty, 4);

        // Price within a tight window so the order book has real depth
        uint32_t price = 1000000 + (rng() % 50) * 100;
        memcpy(&msg[32], &price, 4);

        messages.push_back(std::move(msg));
    }

    std::cout << "Starting Full System Benchmark (Parser + OrderBook)...\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Escape: bid+ask+mid read every iteration\n\n";

    // Warm-up pass (not timed)
    for (size_t i = 0; i < 10000; ++i) {
        engine.process_message(messages[i].data());
        compiler_barrier();
    }

    auto start = std::chrono::high_resolution_clock::now();

    uint64_t checksum = 0;
    for (size_t i = 0; i < iterations; ++i) {
        engine.process_message(messages[i].data());

        // Read observable state every iteration.
        // The compiler cannot remove process_message because checksum
        // depends on the order book state it just mutated.
        OrderBook* book = engine.get_manager().get_book(0x0123);
        if (book) {
            checksum += book->best_bid() ^ book->best_ask();
        }
        compiler_barrier();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    // Publish checksum to volatile so the whole chain is live
    sink = checksum;

    double ns_per_msg = static_cast<double>(duration_ns) / iterations;
    std::cout << "Total time: " << (duration_ns / 1e6) << " ms\n";
    std::cout << "Latency: " << ns_per_msg << " ns/msg\n";
    std::cout << "Throughput: " << (1e9 / ns_per_msg / 1e6) << " M msgs/sec\n";
    std::cout << "Checksum: " << sink << " (non-zero = work was not optimized away)\n";

    return 0;
}
