#pragma once
#include "../types/order.hpp"
#include <cstdint>
#include <cstring>
#include <array>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#include <stdlib.h>

#include "../utils/compiler.hpp"

#if defined(_MSC_VER)
#define LIKELY(x) (x)
#else
#define LIKELY(x) __builtin_expect(!!(x), 1)
#endif



FORCE_INLINE uint16_t read_be16(const uint8_t* p) {
    uint16_t v;
    memcpy(&v, p, 2);
#if defined(_MSC_VER)
    return _byteswap_ushort(v);
#else
    return __builtin_bswap16(v);
#endif
}

FORCE_INLINE uint32_t read_be32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
#if defined(_MSC_VER)
    return _byteswap_ulong(v);
#else
    return __builtin_bswap32(v);
#endif
}

FORCE_INLINE uint64_t read_be64(const uint8_t* p) {
    uint64_t v;
    memcpy(&v, p, 8);
#if defined(_MSC_VER)
    return _byteswap_uint64(v);
#else
    return __builtin_bswap64(v);
#endif
}

// 6-byte timestamp manual)
FORCE_INLINE uint64_t read_be48(const uint8_t* p) {
    return (uint64_t(p[0]) << 40) |
        (uint64_t(p[1]) << 32) |
        (uint64_t(p[2]) << 24) |
        (uint64_t(p[3]) << 16) |
        (uint64_t(p[4]) << 8) |
        uint64_t(p[5]);
}


FORCE_INLINE void parse_add_order(const uint8_t* data, Order& order) {
    order.msg_type = 'A';
    order.stock_locate = read_be16(&data[1]);
    order.timestamp_ns = read_be48(&data[5]);
    order.order_ref = read_be64(&data[11]);
    order.side = data[19];
    order.shares = read_be32(&data[20]);
    // Stock is at data[24], but we dropped it from Order struct
    order.price = read_be32(&data[32]);
}

FORCE_INLINE void parse_add_order_mpid(const uint8_t* data, Order& order) {
    order.msg_type = 'F';
    order.stock_locate = read_be16(&data[1]);
    order.timestamp_ns = read_be48(&data[5]);
    order.order_ref = read_be64(&data[11]);
    order.side = data[19];
    order.shares = read_be32(&data[20]);
    order.price = read_be32(&data[32]);
    // Attribution at data[36]
}

FORCE_INLINE void parse_order_executed(const uint8_t* data, Order& order) {
    order.msg_type = 'E';
    order.stock_locate = read_be16(&data[1]);
    order.timestamp_ns = read_be48(&data[5]);
    order.order_ref = read_be64(&data[11]);
    order.shares = read_be32(&data[19]);
    // Match number at data[23]
}

FORCE_INLINE void parse_order_executed_with_price(const uint8_t* data, Order& order) {
    order.msg_type = 'C';
    order.stock_locate = read_be16(&data[1]);
    order.timestamp_ns = read_be48(&data[5]);
    order.order_ref = read_be64(&data[11]);
    order.shares = read_be32(&data[19]);
    // Match number at data[23]
    order.printable = data[31];
    order.price = read_be32(&data[32]);
}

FORCE_INLINE void parse_order_cancel(const uint8_t* data, Order& order) {
    order.msg_type = 'X';
    order.stock_locate = read_be16(&data[1]);
    order.timestamp_ns = read_be48(&data[5]);
    order.order_ref = read_be64(&data[11]);
    order.shares = read_be32(&data[19]);
}

FORCE_INLINE void parse_delete_order(const uint8_t* data, Order& order) {
    order.msg_type = 'D';
    order.stock_locate = read_be16(&data[1]);
    order.timestamp_ns = read_be48(&data[5]);
    order.order_ref = read_be64(&data[11]);
}

FORCE_INLINE void parse_order_replace(const uint8_t* data, Order& order) {
    order.msg_type = 'U';
    order.stock_locate = read_be16(&data[1]);
    order.timestamp_ns = read_be48(&data[5]);
    order.order_ref = read_be64(&data[11]); // Original ref
    order.new_order_ref = read_be64(&data[19]);
    order.shares = read_be32(&data[27]);
    order.price = read_be32(&data[31]);
}

FORCE_INLINE void parse_stock_directory(const uint8_t* data, Order& order) {
    order.msg_type = 'R';
    order.stock_locate = read_be16(&data[1]);
    order.timestamp_ns = read_be48(&data[5]);
    memcpy(order.symbol, &data[11], 8);
    order.market_category = data[19];
}




using ParserFn = void(*)(const uint8_t*, Order&);

class ITCHParser {
private:
    static ParserFn table[256];
    static std::array<bool, 65536> interest_mask_;

public:
    static void init() {
        interest_mask_.fill(true); // Default to true if no whitelist is used
        for (int i = 0; i < 256; ++i)
            table[i] = nullptr;

        table['A'] = parse_add_order;
        table['F'] = parse_add_order_mpid;
        table['E'] = parse_order_executed;
        table['C'] = parse_order_executed_with_price;
        table['X'] = parse_order_cancel;
        table['D'] = parse_delete_order;
        table['U'] = parse_order_replace;
        table['R'] = parse_stock_directory;
        table['S'] = [](const uint8_t* d, Order& o) { o.msg_type = 'S'; o.stock_locate = read_be16(&d[1]); };
        table['P'] = [](const uint8_t* d, Order& o) { o.msg_type = 'P'; o.stock_locate = read_be16(&d[1]); o.price = read_be32(&d[32]); o.shares = read_be32(&d[20]); };
        table['Q'] = [](const uint8_t* d, Order& o) { o.msg_type = 'Q'; o.stock_locate = read_be16(&d[1]); o.price = read_be32(&d[32]); o.shares = read_be32(&d[20]); };
        table['B'] = [](const uint8_t* d, Order& o) { o.msg_type = 'B'; o.stock_locate = read_be16(&d[1]); };
    }

    static void set_interest(uint16_t locate, bool interest) {
        interest_mask_[locate] = interest;
    }

    static void clear_interest() {
        interest_mask_.fill(false);
    }

    FORCE_INLINE static bool parse(const uint8_t* data, Order& order) {
        uint8_t type = data[0];
        
        // System, Directory, and Trading Action messages always pass
        // Others have locate at offset 1. 'T' (Time) does not.
        if (type == 'T') return false; 

        if (type != 'S' && type != 'R' && type != 'H') [[unlikely]] {
             uint16_t locate = read_be16(&data[1]);
             if (!interest_mask_[locate]) [[likely]] return false;
        }

        order.msg_type = 0; // Safety reset
        ParserFn fn = table[type];

        if (LIKELY(fn)) {
            fn(data, order);
            return true;
        }
        return false;
    }
};

inline ParserFn ITCHParser::table[256];
inline std::array<bool, 65536> ITCHParser::interest_mask_;

