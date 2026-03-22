#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <algorithm>

class SpreadCapture : public Strategy {
private:
    std::vector<StrategyOrder> orders;
    int tick_count = 0;
public:
    double max_position_frac = 0.5;
    int order_size = 5;
    int min_spread = 2;
    double flatten_pct = 0.90;

    void on_tick([[maybe_unused]] uint32_t timestamp,
                 const std::vector<OrderBookState>& books,
                 [[maybe_unused]] const std::vector<std::vector<PublicTrade>>& trades,
                 std::vector<LimitOrderBook>& lobs) override {

        tick_count++;

        int flatten_tick = (total_ticks > 0) ? (int)(total_ticks * flatten_pct) : 0;
        int urgent_tick = (total_ticks > 0) ? (int)(total_ticks * 0.975) : 0;
        bool flattening = (total_ticks > 0) && (tick_count >= flatten_tick);
        bool urgent = (total_ticks > 0) && (tick_count >= urgent_tick);

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
            int spread = (int)book.ask_price_1 - (int)book.bid_price_1;
            int max_pos = (int)(lob.position_limit * max_position_frac);

            if (flattening) {
                if (pos > 0) {
                    int qty = std::min(pos, order_size);
                    if (urgent) {
                        orders.push_back({(int32_t)book.bid_price_1, -qty});
                    } else {
                        orders.push_back({(int32_t)(book.ask_price_1 - 1), -qty});
                    }
                } else if (pos < 0) {
                    int qty = std::min(-pos, order_size);
                    if (urgent) {
                        orders.push_back({(int32_t)book.ask_price_1, qty});
                    } else {
                        orders.push_back({(int32_t)(book.bid_price_1 + 1), qty});
                    }
                }
            } else {
                if (spread < min_spread) {
                    lob.match_orders(orders);
                    continue;
                }

                // Quote INSIDE the spread for queue priority
                int32_t our_bid = (int32_t)book.bid_price_1 + 1;
                int32_t our_ask = (int32_t)book.ask_price_1 - 1;

                if (our_ask - our_bid < 2) {
                    lob.match_orders(orders);
                    continue;
                }

                // Skew quotes to flatten position
                if (pos > max_pos / 2) {
                    our_ask -= 1;
                    our_bid -= 1;
                } else if (pos < -max_pos / 2) {
                    our_ask += 1;
                    our_bid += 1;
                }

                // Passive quotes inside spread
                if (pos < max_pos) {
                    int qty = std::min(order_size, max_pos - pos);
                    if (qty > 0) orders.push_back({our_bid, qty});
                }
                if (pos > -max_pos) {
                    int qty = std::min(order_size, max_pos + pos);
                    if (qty > 0) orders.push_back({our_ask, -qty});
                }

                // Aggressive take if mispriced
                double mid = book.mid_price();
                if (book.ask_price_1 < mid && pos < max_pos) {
                    int qty = std::min((int)std::abs((int)book.ask_volume_1), max_pos - pos);
                    if (qty > 0) orders.push_back({(int32_t)book.ask_price_1, qty});
                }
                if (book.bid_price_1 > mid && pos > -max_pos) {
                    int qty = std::min((int)std::abs((int)book.bid_volume_1), max_pos + pos);
                    if (qty > 0) orders.push_back({(int32_t)book.bid_price_1, -qty});
                }
            }

            lob.match_orders(orders);
        }
    }
};