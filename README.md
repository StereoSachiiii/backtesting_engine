hft-engine
-> high-performance c++23 nasdaq itch 5.0 processor

features
-> zero-allocation order book (only at startup)
-> constant-time price discovery
-> cache-optimized (64b alignment)
-> fast itch parsing

benchmarks
-> parsing: 2-4ns
-> full system: 30-50ns
-> throughput: 20m+ msg/s

build
-> cmake -B build
-> cmake --build build --config Release

test
-> ./build/Release/orderbook_test
-> ./build/Release/itch_parser_test
-> ./build/Release/full_system_benchmark

run
-> ./build/Release/hft_engine <file>


My issue tracker

1
assumption refs per book won't exceed 8M , so no need to rehash . 
but even if thats the case , log probe lengths to see if hash function for some reason producing weird clustering otherwise the whole point of mumur64 is assumed rather than verified even though it works for observability.
dont dynamically resize , just track on debug mode.. answers the question : are the probe lengths realistic under a real feed?. also something like a histogram. 

2
also bound probing both in insert and in erase

3
handle duplicate refs
these are the scenarios->
1. Cumulative Order Executions
2. Cumulative Order Modifications
3. Broken Trade Messages
4. Transport-Layer Retransmissions

4
static assert needed on config to just say that these are the assumptions made about values/ queue sizes, array sizes/ masks, hashes

5
take a day and write down all assumptions made during the design. otherwise choices look opaque AND most importantly readers can think theres a unhandled edge case that is assumed to be out of scope for the system


