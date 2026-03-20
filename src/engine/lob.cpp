#include "engine/lob.hpp"
#include <algorithm>

void LimitOrderBook::update(const OrderBookState& state) {
    current_state = state;
}

TradeResult LimitOrderBook::market_buy(int qty, int market_ask) {
    // How much can we actually buy without hitting the position limit?
    int max_buyable = POSITION_LIMIT - position;
    int actual_qty = std::min({qty, (int)current_state.best_ask_volume, max_buyable});

    if (actual_qty <= 0) return {0, 0, 0.0};

    position += actual_qty;
    return {actual_qty, market_ask, 0.0}; 
}

TradeResult LimitOrderBook::market_sell(int qty, int market_bid) {
    int max_sellable = position + POSITION_LIMIT;
    int actual_qty = std::min({qty, (int)current_state.best_bid_volume, max_sellable});

    if (actual_qty <= 0) return {0, 0, 0.0};

    position -= actual_qty;
    return {actual_qty, market_bid, 0.0};
}