#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <algorithm>

class MarketMaker : public Strategy {
private:
    std::vector<StrategyOrder> orders;
    int tick_count = 0;
public:
    int default_edge = 2;
    int default_order_size = 5;
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
            int limit = lob.position_limit;

            // Compute fair value (weighted mid)
            double bv = std::abs(book.bid_volume_1);
            double av = std::abs(book.ask_volume_1);
            int32_t fair = (bv + av > 0)
                ? (int32_t)std::round(((double)book.bid_price_1 * av + (double)book.ask_price_1 * bv) / (bv + av))
                : (int32_t)std::round(book.mid_price());

            if (flattening) {
                if (pos > 0) {
                    int qty = std::min(pos, default_order_size);
                    if (urgent) {
                        orders.push_back({(int32_t)book.bid_price_1, -qty});
                    } else {
                        int32_t ask_price = std::max(fair, (int32_t)book.ask_price_1);
                        orders.push_back({ask_price, -qty});
                    }
                } else if (pos < 0) {
                    int qty = std::min(-pos, default_order_size);
                    if (urgent) {
                        orders.push_back({(int32_t)book.ask_price_1, qty});
                    } else {
                        int32_t bid_price = std::min(fair, (int32_t)book.bid_price_1);
                        orders.push_back({bid_price, qty});
                    }
                }
            } else {
                // Aggressive: take mispriced levels
                for (int lvl = 0; lvl < 3; lvl++) {
                    uint32_t ap = 0; int32_t av2 = 0;
                    if (lvl == 0) { ap = book.ask_price_1; av2 = book.ask_volume_1; }
                    else if (lvl == 1) { ap = book.ask_price_2; av2 = book.ask_volume_2; }
                    else { ap = book.ask_price_3; av2 = book.ask_volume_3; }
                    if (ap == 0 || av2 == 0) continue;
                    if (fair - (int32_t)ap >= default_edge && pos < limit) {
                        int qty = std::min({(int)std::abs(av2), default_order_size, limit - pos});
                        if (qty > 0) { orders.push_back({(int32_t)ap, qty}); pos += qty; }
                    }
                }
                for (int lvl = 0; lvl < 3; lvl++) {
                    uint32_t bp = 0; int32_t bv2 = 0;
                    if (lvl == 0) { bp = book.bid_price_1; bv2 = book.bid_volume_1; }
                    else if (lvl == 1) { bp = book.bid_price_2; bv2 = book.bid_volume_2; }
                    else { bp = book.bid_price_3; bv2 = book.bid_volume_3; }
                    if (bp == 0 || bv2 == 0) continue;
                    if ((int32_t)bp - fair >= default_edge && pos > -limit) {
                        int qty = std::min({(int)std::abs(bv2), default_order_size, limit + pos});
                        if (qty > 0) { orders.push_back({(int32_t)bp, -qty}); pos -= qty; }
                    }
                }

                // Passive quotes at fair ± (edge + 1)
                int32_t bid_price = fair - default_edge - 1;
                int32_t ask_price = fair + default_edge + 1;

                if (pos > limit / 2) {
                    ask_price -= 1;
                } else if (pos < -limit / 2) {
                    bid_price += 1;
                }

                int buy_qty = std::min(default_order_size, limit - pos);
                int sell_qty = std::min(default_order_size, limit + pos);

                if (buy_qty > 0 && bid_price >= (int32_t)book.bid_price_1) {
                    orders.push_back({bid_price, buy_qty});
                }
                if (sell_qty > 0 && ask_price <= (int32_t)book.ask_price_1) {
                    orders.push_back({ask_price, -sell_qty});
                }
            }

            lob.match_orders(orders);
        }
    }
};