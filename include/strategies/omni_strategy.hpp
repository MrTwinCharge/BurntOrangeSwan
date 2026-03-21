#pragma once
#include "engine/strategy.hpp"
#include <iostream>
#include <cmath>
 
class OmniImbalance : public Strategy {
public:
    double default_threshold = 0.15;  // lowered — tutorial data is near-symmetric
    int    default_size      = 10;
    bool   verbose           = false;
 
    struct Params { double threshold; int size; };
    std::map<std::string, Params> overrides;
 
    void on_tick(uint32_t timestamp,
                 const std::map<std::string, OrderBookState>& books,
                 const std::map<std::string, std::vector<PublicTrade>>& trades,
                 std::map<std::string, LimitOrderBook>& lobs) override {
 
        for (const auto& [symbol, book] : books) {
            auto& lob = lobs[symbol];
 
            double threshold = default_threshold;
            int size = default_size;
            auto it = overrides.find(symbol);
            if (it != overrides.end()) {
                threshold = it->second.threshold;
                size = it->second.size;
            }
 
            // Skip if book is empty
            if (book.bid_price_1 == 0 || book.ask_price_1 == 0) {
                lob.match_orders({});
                continue;
            }
 
            // Full L3 OBI
            double imbalance = book.obi();
 
            // Also check: is the mid price drifting vs our fair (weighted mid)?
            double wmid = book.weighted_mid();
            double mid  = book.mid_price();
            double drift = (wmid - mid) / std::max(mid, 1.0);
 
            std::vector<StrategyOrder> orders;
 
            if (imbalance > threshold || drift > 0.0002) {
                // Buy signal — take the best ask
                orders.push_back({(int32_t)book.ask_price_1, size});
            }
            else if (imbalance < -threshold || drift < -0.0002) {
                // Sell signal — hit the best bid
                orders.push_back({(int32_t)book.bid_price_1, -size});
            }
 
            auto fills = lob.match_orders(orders);
 
            if (verbose) {
                for (const auto& f : fills) {
                    std::cout << "[" << timestamp << "] "
                              << (f.quantity > 0 ? "BUY " : "SELL ")
                              << symbol << " " << std::abs(f.quantity)
                              << "@" << f.price
                              << " | OBI=" << imbalance
                              << " drift=" << drift
                              << " | Pos: " << lob.position << std::endl;
                }
            }
        }
    }
};