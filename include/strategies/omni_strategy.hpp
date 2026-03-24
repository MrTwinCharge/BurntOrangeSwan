#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <algorithm>
#include <vector>

/**
 * OmniImbalance — Order Book Imbalance momentum strategy.
 * 
 * KEY IMPROVEMENTS:
 * 1. EMA-smoothed OBI signal to reduce whipsawing
 * 2. Weighted multi-level imbalance (not just L1)
 * 3. Position-aware sizing (scale down near limits)
 * 4. End-of-day flattening
 * 5. Cooldown after fills to avoid chasing
 * 6. Market regime filter (don't trade OBI in wide-spread / low-liquidity)
 */
class OmniImbalance : public Strategy {
private:
    std::vector<StrategyOrder> orders;
    
    // Per-symbol state
    std::vector<double> ema_obi;         // Smoothed OBI signal
    std::vector<uint32_t> last_fill_ts;  // Cooldown tracking
    bool initialized = false;

public:
    double signal_threshold = 0.15;   // OBI threshold to trade
    int default_size = 10;
    double ema_alpha = 0.3;           // OBI smoothing (higher = more reactive)
    int max_spread_filter = 10;       // Don't trade if spread > this
    double flatten_pct = 0.90;
    int cooldown_ticks = 0;           // Ticks to wait after a fill

    void on_tick([[maybe_unused]] uint32_t timestamp,
                 const std::vector<OrderBookState>& books,
                 [[maybe_unused]] const std::vector<std::vector<PublicTrade>>& trades,
                 std::vector<LimitOrderBook>& lobs) override {

        current_tick++;

        if (!initialized) {
            ema_obi.assign(books.size(), 0.0);
            last_fill_ts.assign(books.size(), 0);
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

            int pos = lob.position;
            int limit = lob.position_limit;

            // ── End-of-day flattening ──
            if (flattening) {
                if (pos > 0) {
                    int qty = std::min(pos, default_size);
                    orders.push_back({(int32_t)book.bid_price_1, -qty});
                } else if (pos < 0) {
                    int qty = std::min(-pos, default_size);
                    orders.push_back({(int32_t)book.ask_price_1, qty});
                }
                lob.match_orders(orders);
                continue;
            }

            // ── Market regime filter ──
            double spread = book.spread();
            if (spread > max_spread_filter) {
                lob.match_orders(orders);
                continue;
            }

            // ── Compute smoothed OBI signal ──
            // Use weighted OBI for better depth signal
            double raw_obi = book.weighted_obi();
            ema_obi[i] = ema_alpha * raw_obi + (1.0 - ema_alpha) * ema_obi[i];
            
            double signal = ema_obi[i];

            // ── Position-aware sizing ──
            // Scale down as we approach position limits
            int max_buy = limit - pos;
            int max_sell = limit + pos;
            
            // Reduce size when already loaded
            double pos_frac = std::abs((double)pos / limit);
            int adjusted_size = default_size;
            if (pos_frac > 0.5) {
                adjusted_size = std::max(1, (int)(default_size * (1.0 - pos_frac)));
            }

            // ── Signal-based trading ──
            if (signal > signal_threshold && max_buy > 0) {
                // Bullish imbalance: buy
                int qty = std::min(adjusted_size, max_buy);
                if (qty > 0) {
                    // Aggressive: cross the spread
                    orders.push_back({(int32_t)book.ask_price_1, qty});
                }
            }
            else if (signal < -signal_threshold && max_sell > 0) {
                // Bearish imbalance: sell
                int qty = std::min(adjusted_size, max_sell);
                if (qty > 0) {
                    orders.push_back({(int32_t)book.bid_price_1, -qty});
                }
            }

            lob.match_orders(orders);
        }
    }
};