#pragma once
#include "config.hpp"
#include "object_pool.hpp"
#include "types/order.hpp"
#include "types/order_book_event.hpp"
#include "types/price_level.hpp"
#include "hierarchical_bitset.hpp"
#include "types/orderbook_memory.hpp"
#include "utils/hash.hpp"
#include <atomic>
#include <cstdint>
#include <array>
#include <memory>
#include <cstring>
#include "utils/compiler.hpp"

class OrderBook {
private:
    static constexpr size_t INDEX_SIZE = config::INDEX_SIZE;
    static constexpr size_t INDEX_MASK = config::INDEX_MASK;
    static constexpr size_t PRICE_WINDOW = config::PRICE_WINDOW;

    OrderBookMemory* memory_ = nullptr;

    uint64_t base_price_ = 0;
    SingleThreadedObjectPool<Order, config::MAX_ORDERS>& order_pool_;
    SingleThreadedObjectPool<PriceLevel, config::MAX_PRICE_LEVELS>& level_pool_;
    char market_category_ = ' ';
    uint32_t order_count_ = 0;

    uint64_t last_prices_[2][5] = {{0}};
    uint32_t last_qtys_[2][5] = {{0}};
    double last_valid_mid_ = 0.0;
    
    int64_t ofi_accumulator_ = 0;
    double ofi_ema_ = 0.0;
    double ofi_ema_mean_ = 0.0;
    double ofi_ema_var_ = 0.0;
    uint64_t total_volume_ = 0;
    uint64_t last_update_ns_ = 0;
    uint64_t out_of_window_drops_ = 0;
    
    // Lazy OFI: only recompute when top-of-book actually changes
    bool ofi_dirty_ = true;
    uint64_t cached_top_[2] = {0, 0};  // [bid, ask] last computed best prices

public:

    OrderBook(SingleThreadedObjectPool<Order, config::MAX_ORDERS>& order_pool, SingleThreadedObjectPool<PriceLevel, config::MAX_PRICE_LEVELS>& level_pool) 
        : order_pool_(order_pool), level_pool_(level_pool)
    {
        memory_ = static_cast<OrderBookMemory*>(alloc_zeroed_aligned(sizeof(OrderBookMemory)));
        if (memory_) {
            memset(memory_, 0, sizeof(OrderBookMemory));
            new (&memory_->bits[0]) HierarchicalBitset(); //b
            new (&memory_->bits[1]) HierarchicalBitset(); //s      
        }
    }

    ~OrderBook() {
        if (memory_) free_zeroed_aligned(memory_, sizeof(OrderBookMemory));
    }



    void set_market_category(char cat) { market_category_ = cat; }


    /*
    *@brief: add new order to orderbook
    *@param ref: order reference
    *@param side: order side
    *@param price: order price
    *@param qty: order quantity
    *@param locate: order stock locate
    *@param ts_ns: order timestamp ns
    */
    FORCE_INLINE void apply_add(uint64_t ref, uint8_t side, uint64_t price, uint32_t qty, uint16_t locate, uint64_t ts_ns) {
        if (!memory_) [[unlikely]] return;
        last_update_ns_ = ts_ns;
        if (order_count_ == 0) [[unlikely]] base_price_ = price - (PRICE_WINDOW / 2);

        if (price < base_price_ || price >= base_price_ + PRICE_WINDOW) [[unlikely]] {
            out_of_window_drops_++;
            return;
        }

        Order* o = order_pool_.allocate();
        if (!o) return;
        
        o->order_ref = ref;
        o->side = side;
        o->price = price;
        o->shares = qty;
        o->stock_locate = locate;
        o->timestamp_ns = ts_ns;
        o->market_category = market_category_;
        o->new_order_ref = 0;
        o->printable = 0;
        o->prev = o->next = nullptr;
        order_count_++;



        uint32_t s = (side & 1);
        size_t idx = (size_t)(price - base_price_);
        
        if (idx >= PRICE_WINDOW) [[unlikely]] return;

        PriceLevel* lvl = memory_->price_levels[s][idx];
        if (!lvl) [[unlikely]] {
            lvl = level_pool_.allocate();
            if (!lvl) return;
            lvl->price = price;
            lvl->total_qty = qty;
            lvl->head = lvl->tail = o;
            lvl->order_count = 1;
            memory_->price_levels[s][idx] = lvl;
            memory_->bits[s].set(idx);
        } else {
            if (!lvl->tail) [[unlikely]] { // Recovery path
                lvl->head = lvl->tail = o;
                lvl->order_count = 1;
                lvl->total_qty = qty;
            } else {
                lvl->tail->next = o;
                o->prev = lvl->tail;
                lvl->tail = o;
                lvl->total_qty += qty;
                lvl->order_count++;
            }
        }
        // add to the hashmap index for fast lookup , i use a custom hash , not  std::map or std::unordered_map 
        index_insert_impl(ref, o);
        // update the order flow imbalance
        update_ofi();
    }

    /*
    *@brief: delete order from orderbook
    *@param ref: order reference
    *@param ts_ns: order timestamp ns
    */
    FORCE_INLINE void apply_delete(uint64_t ref, uint64_t ts_ns) {
        last_update_ns_ = ts_ns;
        if (!memory_) [[unlikely]] return;

        Order* o = index_find_impl(ref);
        if (!o) [[unlikely]] return;

        
        uint32_t s = (o->side & 1);
        size_t idx = (size_t)(o->price - base_price_);
        if (idx >= PRICE_WINDOW) [[unlikely]] {
            index_erase_impl(ref);
            order_pool_.deallocate(o);
            if (order_count_ > 0) order_count_--;
            return;
        }

        PriceLevel* lvl = memory_->price_levels[s][idx]; 
        if (!lvl) [[unlikely]] {
            index_erase_impl(ref);
            order_pool_.deallocate(o);
            if (order_count_ > 0) order_count_--;
            return;
        }

        if (o->prev) o->prev->next = o->next; 
        else         lvl->head = o->next;    
        if (o->next) o->next->prev = o->prev; 
        else         lvl->tail = o->prev;    

        if (lvl->total_qty >= o->shares) {
            lvl->total_qty -= o->shares;
        } else {
            lvl->total_qty = 0;
        }
        if (--lvl->order_count == 0) {
            memory_->bits[s].reset(idx);
            memory_->price_levels[s][idx] = nullptr;
            level_pool_.deallocate(lvl); 
        }

        index_erase_impl(ref);
        order_pool_.deallocate(o);
        if (order_count_ > 0) order_count_--;
        update_ofi();
    }

    FORCE_INLINE void apply_execute(uint64_t ref, uint32_t qty, uint64_t ts_ns) {
        last_update_ns_ = ts_ns;
        total_volume_ += qty;
        apply_cancel(ref, qty, ts_ns);
    }

    FORCE_INLINE void apply_cancel(uint64_t ref, uint32_t qty, uint64_t ts_ns) {
        last_update_ns_ = ts_ns;
        if (!memory_) [[unlikely]] return;


        //insta
        Order* o = index_find_impl(ref);
        if (!o) [[unlikely]] return;

        if (qty >= o->shares)[[likely]] {
            apply_delete(ref, ts_ns);
        } else {
            o->shares -= qty;
            size_t idx = (size_t)(o->price - base_price_);
            if (idx < PRICE_WINDOW) {
                PriceLevel* lvl = memory_->price_levels[o->side & 1][idx];
                if (lvl) {
                    if (lvl->total_qty >= qty) {
                        lvl->total_qty -= qty;
                    } else {
                        lvl->total_qty = 0;
                    }
                }
            }
        }
        update_ofi();
    }



    FORCE_INLINE void apply_replace(uint64_t old_ref, uint64_t new_ref, uint32_t new_qty, uint64_t new_price, uint64_t ts_ns) {
        last_update_ns_ = ts_ns;

        Order* old_order = index_find_impl(old_ref);
        if (!old_order) [[unlikely]] return;

        uint8_t side = old_order->side;
        uint16_t locate = old_order->stock_locate;
        
        apply_delete(old_ref, ts_ns);
        apply_add(new_ref, side, new_price, new_qty, locate, ts_ns);
        update_ofi();
    }


    FORCE_INLINE Order* get_order(uint64_t ref) { return index_find_impl(ref); }


    FORCE_INLINE uint64_t best_bid() const {
        if (!memory_) [[unlikely]] return 0;
        int idx = memory_->bits[0].find_last();
        return (idx != -1) ? memory_->price_levels[0][idx]->price : 0;
    }

    FORCE_INLINE uint64_t best_ask() const {
        if (!memory_) [[unlikely]] return 0;
        int idx = memory_->bits[1].find_first();
        return (idx != -1) ? memory_->price_levels[1][idx]->price : 0;
    }

    FORCE_INLINE uint32_t bid_qty() const {
        if (!memory_) [[unlikely]] return 0;
        int idx = memory_->bits[0].find_last();
        return (idx != -1) ? (uint32_t)memory_->price_levels[0][idx]->total_qty : 0;
    }

    FORCE_INLINE uint32_t ask_qty() const {
        if (!memory_) [[unlikely]] return 0;
        int idx = memory_->bits[1].find_first();
        return (idx != -1) ? (uint32_t)memory_->price_levels[1][idx]->total_qty : 0;
    }

    FORCE_INLINE uint32_t qty_at_price(uint8_t side, uint64_t price) const {
        if (!memory_ || price < base_price_) [[unlikely]] return 0;
        size_t idx = (size_t)(price - base_price_);
        if (idx >= PRICE_WINDOW) [[unlikely]] return 0;
        PriceLevel* lvl = memory_->price_levels[side & 1][idx];
        return lvl ? (uint32_t)lvl->total_qty : 0;
    }

    
    FORCE_INLINE void update_ofi() {
        if (!memory_) return;

        // Fast path: check if top-of-book changed (2 bitset traversals)
        uint64_t cur_bid = best_bid();
        uint64_t cur_ask = best_ask();
        
        if (!ofi_dirty_ && cur_bid == cached_top_[0] && cur_ask == cached_top_[1]) {
            return;  // Top unchanged → OFI unchanged → skip expensive computation
        }

        // Slow path: top-of-book changed, do full OFI computation
        ofi_dirty_ = false;
        cached_top_[0] = cur_bid;
        cached_top_[1] = cur_ask;

        double current_mid = (cur_bid > 0 && cur_ask > 0) 
            ? (double(cur_bid) + double(cur_ask)) / 2.0 
            : last_valid_mid_;
        if (cur_bid > 0 && cur_ask > 0) last_valid_mid_ = current_mid;

        double total_delta = 0.0;
        
        // Track top 5 levels for both Bid (0) and Ask (1)
        for (int side = 0; side <= 1; ++side) {
            int idx = (side == 0) ? memory_->bits[0].find_last() : memory_->bits[1].find_first();
            
            for (int level = 0; level < 5; ++level) {
                if (idx == -1) break;
                
                uint64_t price = memory_->price_levels[side][idx]->price;
                uint32_t qty = (uint32_t)memory_->price_levels[side][idx]->total_qty;
                
                double delta = 0;
                if (side == 0) { // Bid
                    if (price > last_prices_[0][level]) delta = (double)qty;
                    else if (price == last_prices_[0][level]) delta = (double)qty - (double)last_qtys_[0][level];
                    else delta = -(double)last_qtys_[0][level];
                } else { // Ask
                    if (price < last_prices_[1][level]) delta = (double)qty;
                    else if (price == last_prices_[1][level]) delta = (double)qty - (double)last_qtys_[1][level];
                    else delta = -(double)last_qtys_[1][level];
                }

                // Weight by 1/distance
                total_delta += (side == 0 ? delta : -delta) / (double)(level + 1);

                last_prices_[side][level] = price;
                last_qtys_[side][level] = qty;
                
                idx = (side == 0) ? memory_->bits[0].find_prev(idx) : memory_->bits[1].find_next(idx);
            }
        }

        ofi_accumulator_ += (int64_t)total_delta;
        ofi_ema_ = config::OFI_ALPHA * total_delta + (1.0 - config::OFI_ALPHA) * ofi_ema_;

        double delta_from_mean = ofi_ema_ - ofi_ema_mean_;
        ofi_ema_mean_ += config::ZSCORE_ALPHA * delta_from_mean;
        ofi_ema_var_ = (1.0 - config::ZSCORE_ALPHA) * (ofi_ema_var_ + config::ZSCORE_ALPHA * delta_from_mean * delta_from_mean);
    }

    FORCE_INLINE double mid_price() const {
        uint64_t bb = best_bid();
        uint64_t ba = best_ask();
        if (bb > 0 && ba > 0) return (double(bb) + double(ba)) / 2.0;
        return last_valid_mid_;
    }

    FORCE_INLINE double get_ofi() const { return ofi_ema_; }
    FORCE_INLINE double get_ofi_zscore() const {
        double std = ofi_ema_var_ > 1e-12 ? FAST_SQRT(ofi_ema_var_) : 1.0; //standard deviation
        return (ofi_ema_ - ofi_ema_mean_) / std;
    }
    // Raw accumulator 
    FORCE_INLINE int64_t get_ofi_raw() const { return ofi_accumulator_; }
    FORCE_INLINE double get_normalized_ofi() const {
        if (total_volume_ == 0) return 0.0;
        return (double)ofi_accumulator_ / (double)total_volume_;
    }

    FORCE_INLINE double get_imbalance() const {
        if (!memory_) [[unlikely]] return 0.0;
        int bid_idx = memory_->bits[0].find_last();
        int ask_idx = memory_->bits[1].find_first();
        uint32_t b = (bid_idx != -1) ? (uint32_t)memory_->price_levels[0][bid_idx]->total_qty : 0;
        uint32_t a = (ask_idx != -1) ? (uint32_t)memory_->price_levels[1][ask_idx]->total_qty : 0;
        if (b == 0 && a == 0) return 0.0;
        return (double(b) - double(a)) / (double(b) + double(a));
    }

    FORCE_INLINE double get_book_imbalance(int max_levels) const {
        if (!memory_) [[unlikely]] return 0.0;
        uint64_t b_total = 0, a_total = 0;

        int idx = memory_->bits[0].find_last();
        for (int n = 0; idx != -1 && n < max_levels; n++) {
            b_total += memory_->price_levels[0][idx]->total_qty;
            idx = memory_->bits[0].find_prev(idx);
        }

        idx = memory_->bits[1].find_first();
        for (int n = 0; idx != -1 && n < max_levels; n++) {
            a_total += memory_->price_levels[1][idx]->total_qty;
            idx = memory_->bits[1].find_next(idx);
        }

        if (b_total + a_total == 0) return 0.0;
        return (double(b_total) - double(a_total)) / (double(b_total) + double(a_total));
    }

    uint64_t get_total_volume() const { return total_volume_; }
    uint64_t get_last_update_ns() const { return last_update_ns_; }
    uint64_t get_drops() const { return out_of_window_drops_; }

    FORCE_INLINE double weighted_mid() const {
        if (!memory_) [[unlikely]] return 0.0;
        int bid_idx = memory_->bits[0].find_last();
        int ask_idx = memory_->bits[1].find_first();
        if (bid_idx == -1 || ask_idx == -1) [[unlikely]] return 0.0;
        uint64_t bb = memory_->price_levels[0][bid_idx]->price;
        uint64_t ba = memory_->price_levels[1][ask_idx]->price;
        uint32_t bq = (uint32_t)memory_->price_levels[0][bid_idx]->total_qty;
        uint32_t aq = (uint32_t)memory_->price_levels[1][ask_idx]->total_qty;
        if (bq == 0 || aq == 0) [[unlikely]] return 0.0;
        return (double(bb) * aq + double(ba) * bq) / (bq + aq);
    }

    //delete everything copy and move
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(const OrderBook&&) = delete;
    OrderBook& operator=(const OrderBook&&) = delete;

private:
    FORCE_INLINE void index_insert_impl(uint64_t ref, Order* o) {
        if (!memory_) [[unlikely]] return;
        size_t h = hash_util::murmur64(ref) & INDEX_MASK;
        __builtin_prefetch(&memory_->order_index[h], 1, 3);
        while (memory_->order_index[h].ref != 0) {
            h = (h + 1) & INDEX_MASK;
            __builtin_prefetch(&memory_->order_index[(h + 1) & INDEX_MASK], 0, 3);
        }
        memory_->order_index[h] = { ref, o };
    }

    FORCE_INLINE Order* index_find_impl(uint64_t ref) {
        if (!memory_) [[unlikely]] return nullptr;
        size_t h = hash_util::murmur64(ref) & INDEX_MASK;
        __builtin_prefetch(&memory_->order_index[h], 0, 3);
        while (memory_->order_index[h].ref != 0) {
            if (memory_->order_index[h].ref == ref) return memory_->order_index[h].order;
            h = (h + 1) & INDEX_MASK;
            __builtin_prefetch(&memory_->order_index[(h + 1) & INDEX_MASK], 0, 3);
        }
        return nullptr;
    }
    // linear probing
    FORCE_INLINE void index_erase_impl(uint64_t ref) {
        if (!memory_) [[unlikely]] return;
        size_t i = hash_util::murmur64(ref) & INDEX_MASK;
        while (memory_->order_index[i].ref != 0) {
            if (memory_->order_index[i].ref == ref) {
                memory_->order_index[i].ref = 0;
                memory_->order_index[i].order = nullptr;
                
                size_t j = i;
                while (true) {
                    j = (j + 1) & INDEX_MASK;
                    if (memory_->order_index[j].ref == 0) break;
                    
                    size_t k = hash_util::murmur64(memory_->order_index[j].ref) & INDEX_MASK;
             
                    if (i <= j) {
                        if (!(i < k && k <= j)) {
                            memory_->order_index[i] = memory_->order_index[j];
                            memory_->order_index[j].ref = 0;
                            memory_->order_index[j].order = nullptr;
                            i = j;
                        }
                    } else {
                        if (!(i < k || k <= j)) {
                            memory_->order_index[i] = memory_->order_index[j];
                            memory_->order_index[j].ref = 0;
                            memory_->order_index[j].order = nullptr;
                            i = j;
                        }
                    }
                }
                return;
            }
            i = (i + 1) & INDEX_MASK;
        }
    }
};
