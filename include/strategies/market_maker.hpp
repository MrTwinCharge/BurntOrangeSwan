#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <algorithm>

class MarketMaker : public Strategy {
private:
    std::vector<StrategyOrder> orders;

public:
    int default_edge = 2;
    int default_order_size = 5;

    void on_tick([[maybe_unused]] uint32_t timestamp,
                 const std::vector<OrderBookState>& books,
                 [[maybe_unused]] const std::vector<std::vector<PublicTrade>>& trades,
                 std::vector<LimitOrderBook>& lobs) override {

        for (size_t i = 0; i < books.size(); ++i) {
            if (!trade_flags[i]) continue;

            auto& book = books[i];
            auto& lob = lobs[i];

            lob.cancel_all_resting();
            orders.clear();

            if (book.bid_price_1 == 0 || book.ask_price_1 == 0) {
                lob.match_orders(orders);
                continue;
            }

            double fair_value = std::round(book.mid_price());

            int32_t our_bid = (int32_t)fair_value - default_edge;
            int32_t our_ask = (int32_t)fair_value + default_edge;

            our_bid = std::min(our_bid, (int32_t)book.bid_price_1);
            our_ask = std::max(our_ask, (int32_t)book.ask_price_1);

            int max_buy = lob.position_limit - lob.position;
            int max_sell = lob.position_limit + lob.position;

            if (max_buy > 0) orders.push_back({our_bid, std::min(default_order_size, max_buy)});
            if (max_sell > 0) orders.push_back({our_ask, -std::min(default_order_size, max_sell)});

            lob.match_orders(orders);
        }
    }
};