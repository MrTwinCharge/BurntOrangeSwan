#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <algorithm>

class MarketMaker : public Strategy {
public:
    int default_edge = 2;
    int default_order_size = 5;

    void on_tick([[maybe_unused]] uint32_t timestamp,
                 const std::map<std::string, OrderBookState>& books,
                 [[maybe_unused]] const std::map<std::string, std::vector<PublicTrade>>& trades,
                 std::map<std::string, LimitOrderBook>& lobs) override {

        for (const auto& [symbol, book] : books) {
            if (!should_trade(symbol)) continue;

            auto& lob = lobs[symbol];
            lob.cancel_all_resting();

            if (book.bid_price_1 == 0 || book.ask_price_1 == 0) {
                lob.match_orders({});
                continue;
            }

            double fair_value = std::round(book.mid_price());
            std::vector<StrategyOrder> orders;

            int32_t our_bid = (int32_t)fair_value - default_edge;
            int32_t our_ask = (int32_t)fair_value + default_edge;

            our_bid = std::min(our_bid, (int32_t)book.bid_price_1);
            our_ask = std::max(our_ask, (int32_t)book.ask_price_1);

            int max_buy = lob.position_limit - lob.position;
            int max_sell = lob.position_limit + lob.position;

            if (max_buy > 0) {
                orders.push_back({our_bid, std::min(default_order_size, max_buy)});
            }
            if (max_sell > 0) {
                orders.push_back({our_ask, -std::min(default_order_size, max_sell)});
            }

            lob.match_orders(orders);
        }
    }
};