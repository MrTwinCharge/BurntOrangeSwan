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
            int pos = lob.position;
            int limit = lob.position_limit;

            if (emas[i] == 0.0) {
                emas[i] = mid;
                variances[i] = 0.0;
                lob.match_orders(orders);
                continue;
            }

            double diff = mid - emas[i];
            emas[i] += ema_alpha * diff;
            variances[i] = ema_alpha * diff * diff + (1.0 - ema_alpha) * variances[i];

            double stddev = std::sqrt(variances[i]);
            if (stddev < 0.5) {
                lob.match_orders(orders);
                continue;
            }

            double z_score = (mid - emas[i]) / stddev;

            // Entry signals
            if (z_score > z_threshold && pos > -limit) {
                int qty = std::min(order_size, limit + pos);
                if (qty > 0) orders.push_back({(int32_t)book.bid_price_1, -qty});
            }
            else if (z_score < -z_threshold && pos < limit) {
                int qty = std::min(order_size, limit - pos);
                if (qty > 0) orders.push_back({(int32_t)book.ask_price_1, qty});
            }

            // Position unwinding on reversal
            if (pos > 0 && z_score < 0) {
                int qty = std::min(pos, (int)std::abs(book.bid_volume_1));
                if (qty > 0) orders.push_back({(int32_t)book.bid_price_1, -qty});
            } else if (pos < 0 && z_score > 0) {
                int qty = std::min(-pos, (int)std::abs(book.ask_volume_1));
                if (qty > 0) orders.push_back({(int32_t)book.ask_price_1, qty});
            }

            lob.match_orders(orders);
        }
    }
};