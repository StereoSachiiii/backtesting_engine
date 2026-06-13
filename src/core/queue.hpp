#pragma once
#include <cstddef>
#include<array>
#include<memory>
#include<atomic>
#include<utility>


#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) 
#endif

template<typename T,size_t capacity>
class SPSCQueue {
	//(n & (n - 1)) == 0 this is like modulo .. basically base 10 for us humans -> base 2 for computers 

	static_assert((capacity & (capacity - 1)) == 0, "capacity of the queue must be a power of 2");

	private:
		alignas(64) std::atomic<size_t> _tail{ 0 };
		alignas(64) std::atomic<size_t> _head{ 0 };

		/*std::unique_ptr<std::array<T, capacity>> _buffer;*/

		std::array<T, capacity> _buffer;
		

		static constexpr size_t MASK = capacity - 1 ;

	public:
		/*SPSCQueue() : _buffer(std::make_unique<std::array<T, capacity>>()){}*/

		SPSCQueue() : _buffer{} {};


		template<typename U>
		bool try_push(U&& item){
			size_t current_tail = _tail.load(std::memory_order_relaxed);
			size_t next_tail = (current_tail + 1) & MASK;

			size_t current_head = _head.load(std::memory_order_acquire);

			if (current_head == next_tail) {
				return false;
			}

			_buffer[current_tail] = std::forward<U>(item); 
			_tail.store(next_tail, std::memory_order_release);
			return true;
		}

		bool try_pop(T& item){
			size_t current_head = _head.load(std::memory_order_relaxed);
			size_t current_tail = _tail.load(std::memory_order_acquire);

			if (current_head == current_tail) {
				return false;
			}

			item = std::move(_buffer[current_head]);
			size_t next_head = (current_head + 1) & MASK;
			_head.store(next_head, std::memory_order_release);
			return true;
		}

		//copy
		SPSCQueue(const SPSCQueue&) = delete;

		//copy assignment blocked
		SPSCQueue& operator=(const SPSCQueue&) = delete;

		//move
		SPSCQueue(SPSCQueue&&) = delete;

		//move assignment blocked
		SPSCQueue& operator=(SPSCQueue&&) = delete;

		/**
		 * @brief Batch push up to N items with a single tail store.
		 * @return number of items actually pushed
		 */
		template<typename Iter>
		size_t try_push_batch(Iter begin, Iter end) {
			size_t current_tail = _tail.load(std::memory_order_relaxed);
			size_t current_head = _head.load(std::memory_order_acquire);
			size_t available = (current_head - current_tail - 1) & MASK;

			size_t count = 0;
			size_t t = current_tail;
			for (auto it = begin; it != end && count < available; ++it, ++count) {
				_buffer[t] = *it;
				t = (t + 1) & MASK;
			}

			if (count > 0) {
				_tail.store(t, std::memory_order_release);
			}
			return count;
		}

		/**
		 * @brief Batch pop up to max_count items with a single head store.
		 * @return number of items actually popped
		 */
		size_t try_pop_batch(T* out, size_t max_count) {
			size_t current_head = _head.load(std::memory_order_relaxed);
			size_t current_tail = _tail.load(std::memory_order_acquire);
			size_t available = (current_tail - current_head) & MASK;
			size_t to_pop = (available < max_count) ? available : max_count;

			size_t h = current_head;
			for (size_t i = 0; i < to_pop; ++i) {
				out[i] = std::move(_buffer[h]);
				h = (h + 1) & MASK;
			}

			if (to_pop > 0) {
				_head.store(h, std::memory_order_release);
			}
			return to_pop;
		}

		


};

template<typename T, size_t capacity>
class SPMCQueue {
    static_assert((capacity & (capacity - 1)) == 0, "capacity must be a power of 2");

private:
    alignas(64) std::atomic<size_t> _tail{ 0 };
    alignas(64) std::atomic<size_t> _head{ 0 };
    alignas(64) std::atomic<size_t> _free_idx{ 0 };
    
    std::array<T, capacity> _buffer;
    static constexpr size_t MASK = capacity - 1;

public:
    SPMCQueue() : _buffer{} {}

    template<typename U>
    bool try_push(U&& item) {
        size_t current_tail = _tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) & MASK;

        if (next_tail == (_free_idx.load(std::memory_order_acquire) & MASK)) {
            return false;
        }

        _buffer[current_tail] = std::forward<U>(item);
        _tail.store(next_tail, std::memory_order_release);
        return true;
    }

    bool try_pop(T& item) {
        size_t current_head = _head.load(std::memory_order_relaxed);
        while (true) {
            size_t current_tail = _tail.load(std::memory_order_acquire);
            if (current_head == current_tail) {
                return false;
            }

            size_t next_head = (current_head + 1) & MASK;
            if (_head.compare_exchange_weak(current_head, next_head,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
                item = std::move(_buffer[current_head]);
                _free_idx.fetch_add(1, std::memory_order_release);
                return true;
            }
            
        }
    }
	//block everything
    SPMCQueue(const SPMCQueue&) = delete;
    SPMCQueue& operator=(const SPMCQueue&) = delete;
    SPMCQueue(SPMCQueue&&) = delete;
    SPMCQueue& operator=(SPMCQueue&&) = delete;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
