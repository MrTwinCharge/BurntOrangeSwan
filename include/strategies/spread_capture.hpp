#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <algorithm>

class SpreadCapture : public Strategy {
public:
    double max_position_frac = 0.5;
    int order_size = 5;
    int min_spread = 2;

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

            double spread = book.ask_price_1 - book.bid_price_1;
            int max_pos = (int)(lob.position_limit * max_position_frac);
            
            std::vector<StrategyOrder> orders;

            if (spread >= min_spread) {
                if (lob.position < max_pos) {
                    orders.push_back({(int32_t)book.bid_price_1, order_size}); 
                }
                if (lob.position > -max_pos) {
                    orders.push_back({(int32_t)book.ask_price_1, -order_size});
                }
            }

            lob.match_orders(orders);
        }
    }
};