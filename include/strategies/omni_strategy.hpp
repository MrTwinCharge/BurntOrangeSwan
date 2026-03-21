#pragma once
#include "engine/strategy.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
 
class OmniImbalance : public Strategy {
public:
    double default_threshold = 0.15;  
    int    default_size      = 10;
 
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
 
            double imbalance = book.obi();
            double wmid = book.weighted_mid();
            double mid  = book.mid_price();
            double drift = (wmid - mid) / std::max(mid, 1.0);
 
            std::vector<StrategyOrder> orders;
 
            if (imbalance > default_threshold || drift > 0.0002) {
                orders.push_back({(int32_t)book.ask_price_1, default_size});
            }
            else if (imbalance < -default_threshold || drift < -0.0002) {
                orders.push_back({(int32_t)book.bid_price_1, -default_size});
            }
 
            lob.match_orders(orders);
        }
    }
};