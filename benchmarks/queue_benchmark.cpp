#include "../src/core/queue.hpp"
#include "../src/core/timer.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <algorithm>
#include <cstddef>

using TestQueue = SPSCQueue<int, 1024>;

struct STATS {
	uint64_t min, max, p50, p99, p999;
	double avg;
};

STATS calculate_stats(std::vector<uint64_t>& data) {
	STATS s;

	std::sort(data.begin(),data.end());

	s.min = data[0];
	s.p50 = data[data.size() / 2];
	s.p99 = data[data.size() * 99 / 100];
	s.p999 = data[data.size() * 999 / 1000];
	s.max = data[data.size() - 1];

	uint64_t sum = 0;
	for (auto i : data) sum += i;
	s.avg = static_cast<double>(sum) / data.size();
	return s;

}


void benchmark_latency() {
	std::cout << "=== Latency Benchmark ===\n";


	TestQueue queue;
	uint64_t ITERATIONS = 100000000;

	std::vector<uint64_t> latencies;
	Timer timer;
	
	latencies.reserve(ITERATIONS);


	for (uint64_t i = 0; i < ITERATIONS; ++i) {
		int pushed = static_cast<int>(i);
		timer.start();

		
		queue.try_push(std::move(pushed));

		int popped;
		queue.try_pop(popped);

		uint64_t latency = timer.elapsed_ns();
		latencies.push_back(latency);
	}

	STATS s = calculate_stats(latencies);

	std::cout << "Messages: " << ITERATIONS << "\n";
	std::cout << "Min:      " << s.min << "ns\n";
	std::cout << "p50:      " << s.p50 << "ns\n";
	std::cout << "p99:      " << s.p99 << "ns\n";
	std::cout << "p999:     " << s.p999 << "ns\n";
	std::cout << "Max:      " << s.max << "ns\n";
	std::cout << "Average:  " << s.avg << "ns\n";

}


void benchmark_throughput() {
	std::cout << "\n=== Throughput Benchmark ===\n";

	TestQueue queue;
	uint64_t MESSAGES = 10000000;
	Timer timer;

	std::atomic<bool> producer_done{ false };
	std::atomic<size_t> messages_consumed{ 0 };

	std::thread producer([&]() {

		for (size_t i = 0; i < MESSAGES; ++i) {
			while (!queue.try_push(static_cast<int>(i))) {
				std::this_thread::yield();
			}
		}
		producer_done.store(true);
		});

	timer.start();

	std::thread consumer([&]() {
		int popped;
		while (messages_consumed.load() < MESSAGES) {
			if (queue.try_pop(popped)) {
				messages_consumed.fetch_add(1);
			}
			else if (producer_done.load()) {
				while (queue.try_pop(popped)) {
					messages_consumed.fetch_add(1);
				}
				break;
			}


		}
	});
	producer.join();
	consumer.join();

	uint64_t elapsed_ns = timer.elapsed_ns();
	double elapsed_s = static_cast<double>(elapsed_ns) / 1e9;
	double throughput = static_cast<double>(MESSAGES) / elapsed_s;

	std::cout << "Messages:   " << MESSAGES << "\n";
	std::cout << "Duration:   " << elapsed_s << "s\n";
	std::cout << "Throughput: " << (throughput / 1e6) << "M msgs/sec\n";
	std::cout << "Avg latency:" << (static_cast<double>(elapsed_ns) / static_cast<double>(MESSAGES)) << "ns/msg\n";
		
}

int main() {
	benchmark_latency();
	benchmark_throughput();

	// Batch benchmark
	std::cout << "\n=== Batch Throughput Benchmark ===\n";
	{
		using BatchQueue = SPSCQueue<int, 65536>;
		BatchQueue queue;
		constexpr size_t MESSAGES = 10000000;
		constexpr size_t BATCH_SIZE = 64;
		Timer timer;

		std::atomic<bool> producer_done{ false };
		std::atomic<size_t> messages_consumed{ 0 };

		std::thread producer([&]() {
			std::vector<int> batch(BATCH_SIZE);
			size_t sent = 0;
			while (sent < MESSAGES) {
				size_t to_send = std::min(BATCH_SIZE, MESSAGES - sent);
				for (size_t i = 0; i < to_send; i++) batch[i] = static_cast<int>(sent + i);
				size_t pushed = 0;
				while (pushed < to_send) {
					pushed += queue.try_push_batch(batch.begin() + pushed, batch.begin() + to_send);
				}
				sent += to_send;
			}
			producer_done.store(true);
		});

		timer.start();

		std::thread consumer([&]() {
			int buf[BATCH_SIZE];
			while (messages_consumed.load(std::memory_order_relaxed) < MESSAGES) {
				size_t got = queue.try_pop_batch(buf, BATCH_SIZE);
				if (got > 0) {
					messages_consumed.fetch_add(got, std::memory_order_relaxed);
				} else if (producer_done.load(std::memory_order_relaxed)) {
					got = queue.try_pop_batch(buf, BATCH_SIZE);
					messages_consumed.fetch_add(got, std::memory_order_relaxed);
					if (got == 0) break;
				}
			}
		});

		producer.join();
		consumer.join();

		uint64_t elapsed_ns = timer.elapsed_ns();
		double elapsed_s = static_cast<double>(elapsed_ns) / 1e9;
		double throughput = static_cast<double>(MESSAGES) / elapsed_s;

		std::cout << "Batch size: " << BATCH_SIZE << "\n";
		std::cout << "Messages:   " << MESSAGES << "\n";
		std::cout << "Duration:   " << elapsed_s << "s\n";
		std::cout << "Throughput: " << (throughput / 1e6) << "M msgs/sec\n";
		std::cout << "Avg latency:" << (static_cast<double>(elapsed_ns) / static_cast<double>(MESSAGES)) << "ns/msg\n";
	}
}