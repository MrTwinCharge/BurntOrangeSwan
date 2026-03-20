#pragma once
#include "engine/types.hpp"
#include <vector>

struct TradeResult {
    int quantity;
    int price;
    double pnl_impact;
};

class LimitOrderBook {
public:
    int position = 0;
    static constexpr int POSITION_LIMIT = 200;

    // Update the book with the current market snapshot
    void update(const OrderBookState& state);

    // Simulate buying (hitting the market ask)
    TradeResult market_buy(int qty, int market_ask);

    // Simulate selling (hitting the market bid)
    TradeResult market_sell(int qty, int market_bid);

private:
    OrderBookState current_state;
};