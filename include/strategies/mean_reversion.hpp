#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <map>
#include <string>

class MeanReversion : public Strategy {
public:
    // Sweepable params
    double ema_alpha    = 0.1;    // EMA smoothing (higher = faster)
    double z_threshold  = 1.5;    // z-score threshold to trade
    int    order_size   = 5;
    bool   verbose      = false;

    void on_tick(uint32_t timestamp,
                 const std::map<std::string, OrderBookState>& books,
                 const std::map<std::string, std::vector<PublicTrade>>& /*trades*/,
                 std::map<std::string, LimitOrderBook>& lobs) override {

        for (const auto& [symbol, book] : books) {
            auto& lob = lobs[symbol];

            if (book.bid_price_1 == 0 || book.ask_price_1 == 0) {
                lob.match_orders({});
                continue;
            }

            double mid = book.mid_price();
            if (mid == 0) { lob.match_orders({}); continue; }

            // Initialize state on first tick
            if (ema.find(symbol) == ema.end()) {
                ema[symbol] = mid;
                ema_var[symbol] = 0;
                lob.match_orders({});
                continue;
            }

            // Update EMA and variance
            double prev_ema = ema[symbol];
            ema[symbol] = ema_alpha * mid + (1.0 - ema_alpha) * prev_ema;
            double diff = mid - ema[symbol];
            ema_var[symbol] = ema_alpha * (diff * diff) + (1.0 - ema_alpha) * ema_var[symbol];

            double std_dev = std::sqrt(ema_var[symbol]);
            if (std_dev < 0.5) { // not enough variance yet
                lob.match_orders({});
                continue;
            }

            double z_score = (mid - ema[symbol]) / std_dev;

            int limit = lob.position_limit;
            int pos = lob.position;
            std::vector<StrategyOrder> orders;

            if (z_score > z_threshold && pos > -limit) {
                // Price is high relative to EMA → sell (expect reversion down)
                int qty = std::min(order_size, limit + pos);
                if (qty > 0) {
                    orders.push_back({(int32_t)book.bid_price_1, -qty});
                }
            }
            else if (z_score < -z_threshold && pos < limit) {
                // Price is low relative to EMA → buy (expect reversion up)
                int qty = std::min(order_size, limit - pos);
                if (qty > 0) {
                    orders.push_back({(int32_t)book.ask_price_1, qty});
                }
            }

            // Flatten if z-score crosses zero (take profit)
            if (pos > 0 && z_score < 0) {
                int qty = std::min(pos, std::abs(book.bid_volume_1));
                if (qty > 0) orders.push_back({(int32_t)book.bid_price_1, -qty});
            }
            else if (pos < 0 && z_score > 0) {
                int qty = std::min(-pos, std::abs(book.ask_volume_1));
                if (qty > 0) orders.push_back({(int32_t)book.ask_price_1, qty});
            }

            auto fills = lob.match_orders(orders);

            if (verbose) {
                for (const auto& f : fills) {
                    std::cout << "[" << timestamp << "] MR "
                              << (f.quantity > 0 ? "BUY " : "SELL ")
                              << symbol << " " << std::abs(f.quantity)
                              << "@" << f.price
                              << " | z=" << z_score
                              << " ema=" << (int)ema[symbol]
                              << " | Pos=" << lob.position << std::endl;
                }
            }
        }
    }

private:
    std::map<std::string, double> ema;
    std::map<std::string, double> ema_var;
};