#pragma once
#include "engine/strategy.hpp"
#include <vector>
#include <cmath>

class MeanReversion : public Strategy {
private:
    std::vector<StrategyOrder> orders;
    std::vector<double> emas;
    std::vector<double> variances;
    bool initialized = false;

public:
    double ema_alpha = 0.1;
    double z_threshold = 1.5;
    int order_size = 5;

    void on_tick([[maybe_unused]] uint32_t timestamp,
                 const std::vector<OrderBookState>& books,
                 [[maybe_unused]] const std::vector<std::vector<PublicTrade>>& trades,
                 std::vector<LimitOrderBook>& lobs) override {

        if (!initialized) {
            emas.assign(books.size(), 0.0);
            variances.assign(books.size(), 0.0);
            initialized = true;
        }

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

            double mid = book.mid_price();

            if (emas[i] == 0.0) {
                emas[i] = mid;
                variances[i] = 0.0;
                lob.match_orders(orders);
                continue;
            }

            double diff = mid - emas[i];
            emas[i] += ema_alpha * diff;
            variances[i] = (1.0 - ema_alpha) * (variances[i] + ema_alpha * diff * diff);

            double stddev = std::sqrt(variances[i]);
            double z_score = (stddev > 0.0) ? (mid - emas[i]) / stddev : 0.0;

            if (z_score < -z_threshold) {
                orders.push_back({(int32_t)book.ask_price_1, order_size}); 
            } else if (z_score > z_threshold) {
                orders.push_back({(int32_t)book.bid_price_1, -order_size}); 
            }

            lob.match_orders(orders);
        }
    }
};