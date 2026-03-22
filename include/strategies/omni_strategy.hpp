#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <algorithm>

class OmniImbalance : public Strategy {
private:
    std::vector<StrategyOrder> orders;

public:
    double default_threshold = 0.15;
    int default_size = 10;

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

            // L1 volume imbalance (matches Python)
            double bid_vol = std::abs(book.bid_volume_1);
            double ask_vol = std::abs(book.ask_volume_1);
            double total = bid_vol + ask_vol;
            if (total == 0) {
                lob.match_orders(orders);
                continue;
            }
            double imbalance = (bid_vol - ask_vol) / total;

            if (imbalance > default_threshold && pos < limit) {
                int qty = std::min(default_size, limit - pos);
                if (qty > 0) orders.push_back({(int32_t)book.ask_price_1, qty});
            }
            else if (imbalance < -default_threshold && pos > -limit) {
                int qty = std::min(default_size, limit + pos);
                if (qty > 0) orders.push_back({(int32_t)book.bid_price_1, -qty});
            }

            lob.match_orders(orders);
        }
    }
};