#pragma once
#include "engine/strategy.hpp"
#include <cmath>

class FairValue : public Strategy {
private:
    std::vector<StrategyOrder> orders;

public:
    double threshold = 0.5;
    int order_size = 10;

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

            int pos = lob.position;
            int limit = lob.position_limit;
            double mid = book.mid_price();
            double fair = book.weighted_mid();

            if (fair - mid > threshold && pos < limit) {
                int qty = std::min(order_size, limit - pos);
                if (qty > 0) orders.push_back({(int32_t)book.ask_price_1, qty});
            }
            else if (mid - fair > threshold && pos > -limit) {
                int qty = std::min(order_size, limit + pos);
                if (qty > 0) orders.push_back({(int32_t)book.bid_price_1, -qty});
            }

            lob.match_orders(orders);
        }
    }
};