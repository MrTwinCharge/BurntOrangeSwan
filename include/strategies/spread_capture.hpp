#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <algorithm>

/**
 * SpreadCapture — Pure spread capture (penny quoting) strategy.
 * 
 * IMPROVEMENTS:
 * 1. Quote INSIDE the spread (bid+1, ask-1) for queue priority  
 * 2. Inventory skew: shift quotes to reduce position
 * 3. Only requote when target changes (preserve queue position)
 * 4. Tighter position limits (don't accumulate big inventory)
 * 5. End-of-day flattening with passive then aggressive phases
 * 6. Aggressive take when obvious mispricing (ask < mid or bid > mid)
 */
class SpreadCapture : public Strategy {
private:
    std::vector<StrategyOrder> orders;
    std::vector<int32_t> current_bids;
    std::vector<int32_t> current_asks;
    std::vector<int> last_positions;
    bool initialized = false;

public:
    double max_position_frac = 0.7;  // Max position as fraction of limit
    int order_size = 5;
    int min_spread = 3;               // Minimum spread to quote (need room for both sides)
    double flatten_pct = 0.90;
    double urgent_pct = 0.975;

    void on_tick([[maybe_unused]] uint32_t timestamp,
                 const std::vector<OrderBookState>& books,
                 [[maybe_unused]] const std::vector<std::vector<PublicTrade>>& trades,
                 std::vector<LimitOrderBook>& lobs) override {

        current_tick++;

        if (!initialized) {
            current_bids.assign(books.size(), 0);
            current_asks.assign(books.size(), 0);
            last_positions.assign(books.size(), 0);
            initialized = true;
        }

        bool flattening = should_flatten(flatten_pct);
        bool urgent = should_urgent_flatten(urgent_pct);

        for (size_t i = 0; i < books.size(); ++i) {
            if (!trade_flags[i]) continue;

            auto& book = books[i];
            auto& lob = lobs[i];

            if (book.bid_price_1 == 0 || book.ask_price_1 == 0) continue;

            int pos = lob.position;
            int limit = lob.position_limit;
            int max_pos = (int)(limit * max_position_frac);
            double spread = book.spread();

            // ── End-of-day flattening ──
            if (flattening) {
                lob.cancel_all_resting();
                orders.clear();
                if (pos > 0) {
                    int qty = std::min(pos, order_size);
                    if (urgent) {
                        orders.push_back({(int32_t)book.bid_price_1, -qty});
                    } else {
                        // Passive exit: quote at ask-1
                        orders.push_back({(int32_t)book.ask_price_1 - 1, -qty});
                    }
                } else if (pos < 0) {
                    int qty = std::min(-pos, order_size);
                    if (urgent) {
                        orders.push_back({(int32_t)book.ask_price_1, qty});
                    } else {
                        orders.push_back({(int32_t)book.bid_price_1 + 1, qty});
                    }
                }
                lob.match_orders(orders);
                current_bids[i] = 0; current_asks[i] = 0;
                continue;
            }

            // ── Need minimum spread to capture ──
            if (spread < min_spread) continue;

            // ── Quote inside the spread ──
            int32_t target_bid = (int32_t)book.bid_price_1 + 1;
            int32_t target_ask = (int32_t)book.ask_price_1 - 1;

            // Must have room for our quotes
            if (target_ask - target_bid < 1) continue;

            // ── Inventory skew ──
            // Shift quotes to reduce position
            if (pos > max_pos / 2) {
                // We're long: make ask more aggressive, bid less aggressive
                target_ask -= 1;
                target_bid -= 1;
            } else if (pos < -max_pos / 2) {
                target_ask += 1;
                target_bid += 1;
            }

            // Safety: don't cross
            target_bid = std::min(target_bid, target_ask - 1);

            // ── Only update if something changed ──
            if (target_bid != current_bids[i] || target_ask != current_asks[i] || pos != last_positions[i]) {
                lob.cancel_all_resting();
                orders.clear();

                int max_buy = std::min(max_pos - pos, order_size);
                int max_sell = std::min(max_pos + pos, order_size);

                if (max_buy > 0 && target_bid > 0)
                    orders.push_back({target_bid, max_buy});
                if (max_sell > 0 && target_ask > 0)
                    orders.push_back({target_ask, -max_sell});

                // ── Aggressive take on obvious mispricing ──
                double mid = book.mid_price();
                if (book.ask_price_1 < mid && pos < max_pos) {
                    int qty = std::min((int)book.ask_volume_1, max_pos - pos);
                    if (qty > 0) orders.push_back({(int32_t)book.ask_price_1, qty});
                }
                if (book.bid_price_1 > mid && pos > -max_pos) {
                    int qty = std::min((int)book.bid_volume_1, max_pos + pos);
                    if (qty > 0) orders.push_back({(int32_t)book.bid_price_1, -qty});
                }

                lob.match_orders(orders);
                current_bids[i] = target_bid;
                current_asks[i] = target_ask;
                last_positions[i] = pos;
            }
        }
    }
};