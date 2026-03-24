#pragma once
#include "engine/types.hpp"
#include <string>
#include <vector>

class LimitOrderBook {
public:
    std::string symbol;
    int position = 0;
    int position_limit = 50;
    
    // Friction for AT-TOUCH orders only (inside-spread orders have friction=0).
    // Set to ~1.0 for aggressive fill estimation, higher for conservative.
    double friction_coefficient = 1.0; 
    
    ProductResult result;

    std::vector<StrategyOrder> resting_orders;
    std::vector<StrategyOrder> pending_orders; 
    bool cancel_requested = false;

    OrderBookState current_state;
    OrderBookState prev_state;  // Previous tick's state for book-delta inference

    LimitOrderBook() = default;
    LimitOrderBook(const std::string& sym);

    void match_orders(const std::vector<StrategyOrder>& orders);
    void cancel_all_resting();

    void update(const OrderBookState& state, const std::vector<PublicTrade>& trades);

private:
    void process_fills(uint32_t timestamp, int32_t price, int32_t quantity, bool aggressive);
    int32_t calculate_queue_ahead(int32_t price, bool is_buy, const OrderBookState& state) const;
};