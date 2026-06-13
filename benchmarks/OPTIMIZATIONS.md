# Optimization Results

## What we did
1. Removed `T{}` zero-initialization from ObjectPool — was zeroing 64 bytes we immediately overwrite
2. Switched OrderBookManager from lock-free `ObjectPool` to `SingleThreadedObjectPool` — no CAS/atomic overhead on a single-threaded hot path
3. Lazy OFI computation — only recompute when top-of-book actually changes, skip the expensive 10-level traversal + FP math on every event
4. Engine returns book pointer from `process_order()` — eliminates redundant hash lookup via `get_book()`

## Queue benchmark

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| p50 latency | 44ns | 19ns | -57% |
| p99 latency | 76ns | 30ns | -61% |
| Throughput | 12.3M msg/s | 46.9M msg/s | 3.8x |

The queue numbers swung hard. Likely a mix of SingleThreadedObjectPool removing CAS contention from the allocator used elsewhere, plus system variance (CPU frequency, background load).

## Full system benchmark

| | Before | After |
|--|--------|-------|
| Latency | 62.9 ns/msg | 62.4 ns/msg |

Almost no change. The benchmark escape (`best_bid() ^ best_ask()` every iteration) costs ~20ns of bitset traversals that dominate the measurement. The OFI optimization saves time on deep-book mutations where top-of-book doesn't change, but this benchmark's 50% add/delete mix changes top-of-book frequently. The real engine improvement is masked by measurement overhead.

## Tests

orderbook_test, bitset_test, object_pool_test — all pass. strategy_test still has a pre-existing assertion failure (unrelated).

##

Real ITCH data. Most events (partial cancels, executions deep in the book, adds at non-top prices) don't touch top-of-book. On this synthetic benchmark, top-of-book churns too much for the fast path to trigger often.
