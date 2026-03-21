#pragma once
#include "engine/strategy.hpp"
#include <cmath>

class FairValue : public Strategy {
public:
    double threshold = 0.5; 
    int order_size = 10;

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

            double fair_value = book.weighted_mid();
            double mid = book.mid_price();

            std::vector<StrategyOrder> orders;

            if (fair_value - mid > threshold) {
                orders.push_back({(int32_t)book.ask_price_1, order_size});
            }
            else if (mid - fair_value > threshold) {
                orders.push_back({(int32_t)book.bid_price_1, -order_size});
            }

            lob.match_orders(orders);
        }
    }
};