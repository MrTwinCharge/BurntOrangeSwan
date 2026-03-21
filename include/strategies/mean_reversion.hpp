#pragma once
#include "engine/strategy.hpp"
#include <map>
#include <cmath>

class MeanReversion : public Strategy {
public:
    double ema_alpha = 0.1;
    double z_threshold = 1.5;
    int order_size = 5;

    std::map<std::string, double> emas;
    std::map<std::string, double> variances;

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

            double mid = book.mid_price();

            if (emas.find(symbol) == emas.end()) {
                emas[symbol] = mid;
                variances[symbol] = 0.0;
                lob.match_orders({});
                continue;
            }

            double diff = mid - emas[symbol];
            emas[symbol] += ema_alpha * diff;
            variances[symbol] = (1.0 - ema_alpha) * (variances[symbol] + ema_alpha * diff * diff);

            double stddev = std::sqrt(variances[symbol]);
            double z_score = (stddev > 0.0) ? (mid - emas[symbol]) / stddev : 0.0;

            std::vector<StrategyOrder> orders;

            if (z_score < -z_threshold) {
                orders.push_back({(int32_t)book.ask_price_1, order_size}); 
            } else if (z_score > z_threshold) {
                orders.push_back({(int32_t)book.bid_price_1, -order_size}); 
            }

            lob.match_orders(orders);
        }
    }
};