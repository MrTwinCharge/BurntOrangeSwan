#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <algorithm>

class MarketMaker : public Strategy {
private:
    std::vector<StrategyOrder> orders;
    std::vector<int32_t> current_bids;
    std::vector<int32_t> current_asks;
    std::vector<int> last_positions;
    bool initialized = false;

public:
    int default_edge = 2;
    int default_order_size = 5;
    double risk_aversion = 0.05; // Continuous inventory risk parameter

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

            // 1. Calculate the volume-weighted micro-price
            double bid_vol = book.bid_volume_1; 
            double ask_vol = book.ask_volume_1; 
            double micro_price = (book.bid_price_1 * ask_vol + book.ask_price_1 * bid_vol) / (bid_vol + ask_vol);

            // 2. Calculate the Avellaneda-Stoikov continuous inventory skew
            double inventory_skew = lob.position * risk_aversion;
            
            // 3. Shift the reservation price away from inventory risk
            double reservation_price = micro_price - inventory_skew;

            // 4. Set asymmetric quotes around the reservation price
            int32_t target_bid = (int32_t)std::round(reservation_price) - default_edge;
            int32_t target_ask = (int32_t)std::round(reservation_price) + default_edge;

            // Ensure we do not passively cross the spread (but allow stepping inside it)
            target_bid = std::min(target_bid, (int32_t)book.ask_price_1 - 1);
            target_ask = std::max(target_ask, (int32_t)book.bid_price_1 + 1);

            if (target_bid != current_bids[i] || target_ask != current_asks[i] || lob.position != last_positions[i]) {
                
                lob.cancel_all_resting();
                orders.clear();

                int max_buy = lob.position_limit - lob.position;
                int max_sell = lob.position_limit + lob.position;

                if (max_buy > 0) orders.push_back({target_bid, std::min(default_order_size, max_buy)});
                if (max_sell > 0) orders.push_back({target_ask, -std::min(default_order_size, max_sell)});

                lob.match_orders(orders);

                current_bids[i] = target_bid;
                current_asks[i] = target_ask;
                last_positions[i] = lob.position;
            }
        }
    }
};