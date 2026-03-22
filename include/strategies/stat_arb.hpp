#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <deque>
#include <numeric>

class StatArb : public Strategy {
private:
    std::deque<double> price_history;
    std::vector<StrategyOrder> orders;

public:
    int window_size = 50; 
    double z_threshold = 2.0;
    int order_size = 10;

    void on_tick([[maybe_unused]] uint32_t timestamp,
                 const std::vector<OrderBookState>& books,
                 [[maybe_unused]] const std::vector<std::vector<PublicTrade>>& trades,
                 std::vector<LimitOrderBook>& lobs) override {

        for (size_t i = 0; i < books.size(); ++i) {
            if (!trade_flags[i]) continue;

            auto& book = books[i];
            auto& lob = lobs[i];

            if (book.bid_price_1 == 0 || book.ask_price_1 == 0) continue;

            double mid_price = book.mid_price();
            price_history.push_back(mid_price);
            
            if (price_history.size() > (size_t)window_size) {
                price_history.pop_front();
            }

            if (price_history.size() < (size_t)window_size) continue;

            // Calculate Mean
            double sum = std::accumulate(price_history.begin(), price_history.end(), 0.0);
            double mean = sum / window_size;

            // Calculate Standard Deviation
            double sq_sum = 0.0;
            for (double p : price_history) {
                sq_sum += (p - mean) * (p - mean);
            }
            double std_dev = std::sqrt(sq_sum / window_size);

            if (std_dev == 0) continue;

            double z_score = (mid_price - mean) / std_dev;

            lob.cancel_all_resting();
            orders.clear();

            int max_can_buy = lob.position_limit - lob.position;
            int max_can_sell = lob.position_limit + lob.position;

            int buy_qty = std::min(max_can_buy, order_size);
            int sell_qty = std::min(max_can_sell, order_size);

            // Price is statistically too low -> BUY (Aggressively cross the spread)
            if (z_score < -z_threshold && buy_qty > 0) {
                orders.push_back({(int32_t)book.ask_price_1, buy_qty});
            }
            // Price is statistically too high -> SELL (Aggressively cross the spread)
            else if (z_score > z_threshold && sell_qty > 0) {
                orders.push_back({(int32_t)book.bid_price_1, -sell_qty});
            }

            if (!orders.empty()) {
                lob.match_orders(orders);
            }
        }
    }
};