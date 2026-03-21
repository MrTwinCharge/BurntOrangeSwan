#pragma once
#include "engine/types.hpp"
#include <string>
#include <vector>

class LimitOrderBook {
public:
    std::string symbol;
    int position = 0;
    int position_limit = 20;
    
    ProductResult result;

    // Split orders to simulate latency
    std::vector<StrategyOrder> resting_orders;
    std::vector<StrategyOrder> pending_orders; 
    bool cancel_requested = false;

    OrderBookState current_state;

    LimitOrderBook() = default;
    LimitOrderBook(const std::string& sym);

    // Called by strategy
    void match_orders(const std::vector<StrategyOrder>& orders);
    void cancel_all_resting();

    // Called by engine
    void update(const OrderBookState& state, const std::vector<PublicTrade>& trades);

private:
    void process_fills(uint32_t timestamp, int32_t price, int32_t quantity, bool aggressive);
    int32_t calculate_queue_ahead(int32_t price, bool is_buy, const OrderBookState& state) const;
};