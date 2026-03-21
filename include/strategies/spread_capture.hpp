#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <algorithm>

class SpreadCapture : public Strategy {
private:
    std::vector<StrategyOrder> orders; // 🚀 OPTIMIZATION 4: Vector reuse

public:
    double max_position_frac = 0.5;
    int order_size = 5;
    int min_spread = 2;

    void on_tick([[maybe_unused]] uint32_t timestamp,
                 const std::vector<OrderBookState>& books,
                 [[maybe_unused]] const std::vector<std::vector<PublicTrade>>& trades,
                 std::vector<LimitOrderBook>& lobs) override {

        for (size_t i = 0; i < books.size(); ++i) {
            if (!trade_flags[i]) continue;

            auto& book = books[i];
            auto& lob = lobs[i];

            lob.cancel_all_resting();
            orders.clear(); // Fast O(1) reset

            if (book.bid_price_1 == 0 || book.ask_price_1 == 0) {
                lob.match_orders(orders);
                continue;
            }

            double spread = book.ask_price_1 - book.bid_price_1;
            int max_pos = (int)(lob.position_limit * max_position_frac);
            
            if (spread >= min_spread) {
                if (lob.position < max_pos) orders.push_back({(int32_t)book.bid_price_1, order_size}); 
                if (lob.position > -max_pos) orders.push_back({(int32_t)book.ask_price_1, -order_size});
            }

            lob.match_orders(orders);
        }
    }
};