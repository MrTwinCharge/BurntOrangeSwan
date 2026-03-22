#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <algorithm>

class SpreadCapture : public Strategy {
private:
    std::vector<StrategyOrder> orders;
    std::vector<int32_t> current_bids;
    std::vector<int32_t> current_asks;
    std::vector<int> last_positions;
    bool initialized = false;

public:
    double max_position_frac = 0.5;
    int order_size = 5;
    int min_spread = 2;

    void on_tick([[maybe_unused]] uint32_t timestamp,
                 const std::vector<OrderBookState>& books,
                 [[maybe_unused]] const std::vector<std::vector<PublicTrade>>& trades,
                 std::vector<LimitOrderBook>& lobs) override {

        if (!initialized) {
            current_bids.assign(books.size(), 0);
            current_asks.assign(books.size(), 0);
            last_positions.assign(books.size(), 0);
            initialized = true;
        }

        for (size_t i = 0; i < books.size(); ++i) {
            if (!trade_flags[i]) continue;

            auto& book = books[i];
            auto& lob = lobs[i];

            if (book.bid_price_1 == 0 || book.ask_price_1 == 0) continue;

            double spread = book.ask_price_1 - book.bid_price_1;
            
            int32_t target_bid = 0;
            int32_t target_ask = 0;

            if (spread >= min_spread) {
                target_bid = (int32_t)book.bid_price_1;
                target_ask = (int32_t)book.ask_price_1;
            }

            // 🚀 THE FIX: Only reset queue position if target shifts or we get filled
            if (target_bid != current_bids[i] || target_ask != current_asks[i] || lob.position != last_positions[i]) {
                lob.cancel_all_resting();
                orders.clear();

                int max_pos = (int)(lob.position_limit * max_position_frac);
                int max_buy = std::min(max_pos - lob.position, order_size);
                int max_sell = std::min(max_pos + lob.position, order_size);

                if (target_bid > 0 && max_buy > 0) orders.push_back({target_bid, max_buy});
                if (target_ask > 0 && max_sell > 0) orders.push_back({target_ask, -max_sell});

                lob.match_orders(orders);

                current_bids[i] = target_bid;
                current_asks[i] = target_ask;
                last_positions[i] = lob.position;
            }
        }
    }
};