#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <algorithm>

class OmniImbalance : public Strategy {
private:
    std::vector<StrategyOrder> orders;

public:
    double default_threshold = 0.15;  
    int    default_size      = 10;

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

            double imbalance = book.obi();
            double wmid = book.weighted_mid();
            double mid  = book.mid_price();
            double drift = (wmid - mid) / std::max(mid, 1.0);

            // Calculate how much we are allowed to buy/sell
            int max_buy = lob.position_limit - lob.position;
            int max_sell = lob.position_limit + lob.position;

            if (imbalance > default_threshold || drift > 0.0002) {
                int qty = std::min(default_size, max_buy);
                if (qty > 0) {
                    orders.push_back({(int32_t)book.ask_price_1, qty});
                }
            }
            else if (imbalance < -default_threshold || drift < -0.0002) {
                int qty = std::min(default_size, max_sell);
                if (qty > 0) {
                    orders.push_back({(int32_t)book.bid_price_1, -qty});
                }
            }

            lob.match_orders(orders);
        }
    }
};