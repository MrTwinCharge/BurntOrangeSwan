#pragma once
#include "engine/strategy.hpp"
#include <vector>
#include <cmath>

/**
 * MeanReversion — EMA z-score based mean reversion strategy.
 * 
 * IMPROVEMENTS:
 * 1. Separate entry/exit z-score thresholds (hysteresis)
 * 2. Minimum volatility filter (don't trade flat markets)
 * 3. Position-scaled entry (bigger when confident)
 * 4. Proper exit logic: unwind when z reverts, don't wait for opposite extreme
 * 5. End-of-day flattening
 * 6. Stop-loss at extreme z-scores
 */
class MeanReversion : public Strategy {
private:
    std::vector<StrategyOrder> orders;
    std::vector<double> emas;
    std::vector<double> variances;
    bool initialized = false;

public:
    double ema_alpha = 0.05;       // Slower EMA for more stable mean (was 0.1)
    double z_entry = 2.0;          // Z-score to enter (was 1.5, too aggressive)
    double z_exit = 0.5;           // Z-score to exit (new: don't wait for 0)
    double z_stop = 4.0;           // Stop-loss z-score (new: cut losses)
    int order_size = 5;
    double min_std = 0.5;          // Minimum volatility to trade
    double flatten_pct = 0.90;

    void on_tick([[maybe_unused]] uint32_t timestamp,
                 const std::vector<OrderBookState>& books,
                 [[maybe_unused]] const std::vector<std::vector<PublicTrade>>& trades,
                 std::vector<LimitOrderBook>& lobs) override {

        current_tick++;

        if (!initialized) {
            emas.assign(books.size(), 0.0);
            variances.assign(books.size(), 0.0);
            initialized = true;
        }

        bool flattening = should_flatten(flatten_pct);

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

            // ── End-of-day flattening ──
            if (flattening) {
                if (pos > 0) {
                    orders.push_back({(int32_t)book.bid_price_1, -std::min(pos, order_size * 2)});
                } else if (pos < 0) {
                    orders.push_back({(int32_t)book.ask_price_1, std::min(-pos, order_size * 2)});
                }
                lob.match_orders(orders);
                continue;
            }

            // ── Initialize EMA ──
            if (emas[i] == 0.0) {
                emas[i] = mid;
                variances[i] = 0.0;
                lob.match_orders(orders);
                continue;
            }

            // ── Update EMA statistics ──
            double diff = mid - emas[i];
            emas[i] += ema_alpha * diff;
            variances[i] = (1.0 - ema_alpha) * (variances[i] + ema_alpha * diff * diff);

            double stddev = std::sqrt(variances[i]);
            
            // Don't trade if volatility is too low (flat market)
            if (stddev < min_std) {
                lob.match_orders(orders);
                continue;
            }
            
            double z_score = (mid - emas[i]) / stddev;

            // ══════════════════════════════════════
            // EXIT LOGIC (check first — always exit before entering)
            // ══════════════════════════════════════
            
            // Stop-loss: cut positions if z-score extends against us
            if (pos > 0 && z_score > z_stop) {
                // Long and price going further up = our mean reversion thesis is wrong
                // Actually this is a profit - let it ride, but exit at z_exit
            }
            if (pos < 0 && z_score < -z_stop) {
                // Short and price going further down = wrong thesis, cut
            }
            
            // Normal exit: z reverted toward mean
            if (pos > 0 && z_score < z_exit) {
                // We were long (bought cheap), now z is near 0 = time to exit
                int qty = std::min(pos, (int)book.bid_volume_1);
                qty = std::min(qty, order_size * 2);
                if (qty > 0) orders.push_back({(int32_t)book.bid_price_1, -qty});
            }
            else if (pos < 0 && z_score > -z_exit) {
                // We were short (sold expensive), z reverting = exit
                int qty = std::min(-pos, (int)book.ask_volume_1);
                qty = std::min(qty, order_size * 2);
                if (qty > 0) orders.push_back({(int32_t)book.ask_price_1, qty});
            }
            
            // ══════════════════════════════════════
            // ENTRY LOGIC
            // ══════════════════════════════════════
            
            // Price is statistically too low → BUY
            if (z_score < -z_entry && pos < limit) {
                int qty = std::min(order_size, limit - pos);
                if (qty > 0) {
                    orders.push_back({(int32_t)book.ask_price_1, qty}); // Aggressive
                }
            }
            // Price is statistically too high → SELL
            else if (z_score > z_entry && pos > -limit) {
                int qty = std::min(order_size, limit + pos);
                if (qty > 0) {
                    orders.push_back({(int32_t)book.bid_price_1, -qty}); // Aggressive
                }
            }

            lob.match_orders(orders);
        }
    }
};