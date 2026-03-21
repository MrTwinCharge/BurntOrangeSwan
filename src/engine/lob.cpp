#include "engine/lob.hpp"
#include <cmath>
#include <algorithm>

LimitOrderBook::LimitOrderBook(const std::string& sym) {
    symbol = sym;
    position_limit = get_position_limit(sym);
    result.symbol = sym;
    calibrate_for_product();
}

void LimitOrderBook::init_rng() {
    if (!rng_initialized) {
        std::hash<std::string> hasher;
        rng.seed(hasher(symbol) ^ 42);
        rng_initialized = true;
    }
}

void LimitOrderBook::calibrate_for_product() {
    // Averaged across 5 real Prosperity 4 submissions:
    //   EMERALDS: ~10 buys + ~16 sells per 2000 ticks, size 3-8
    //   TOMATOES: ~35 buys + ~32 sells per 2000 ticks, size 2-5
    if (symbol == "EMERALDS") {
        passive_fill_prob_buy = 10.0 / 2000.0;   // 0.005
        passive_fill_prob_sell = 16.0 / 2000.0;   // 0.008
        min_fill_size = 3;
        max_fill_size = 8;
    } else if (symbol == "TOMATOES") {
        passive_fill_prob_buy = 35.0 / 2000.0;    // 0.0175
        passive_fill_prob_sell = 32.0 / 2000.0;    // 0.016
        min_fill_size = 2;
        max_fill_size = 5;
    } else {
        // Conservative default for unknown products
        passive_fill_prob_buy = 15.0 / 2000.0;
        passive_fill_prob_sell = 15.0 / 2000.0;
        min_fill_size = 2;
        max_fill_size = 6;
    }
}

void LimitOrderBook::cancel_all_resting() {
    cancel_requested = true;
}

void LimitOrderBook::match_orders(const std::vector<StrategyOrder>& orders) {
    pending_orders = orders;
}

void LimitOrderBook::update(const OrderBookState& state, const std::vector<PublicTrade>& /*trades*/) {
    current_state = state;
    uint32_t ts = state.timestamp;
    init_rng();

    // ═══ 1. LATENCY: Pending cancels and orders from LAST tick arrive now ═══
    if (cancel_requested) {
        resting_orders.clear();
        cancel_requested = false;
    }

    if (!pending_orders.empty()) {
        for (auto& order : pending_orders) {
            resting_orders.push_back(order);
        }
        pending_orders.clear();
    }

    // ═══ 2. AGGRESSIVE CROSSES ═══
    // Orders priced through the spread fill immediately at book price
    for (auto& order : resting_orders) {
        if (order.quantity == 0) continue;

        if (order.is_buy() && state.ask_price_1 > 0 && order.price >= (int32_t)state.ask_price_1) {
            int max_can_buy = position_limit - position;
            if (max_can_buy <= 0) continue;

            struct Level { uint32_t price; int32_t vol; };
            Level asks[] = {
                {state.ask_price_1, (int32_t)state.ask_volume_1},
                {state.ask_price_2, (int32_t)state.ask_volume_2},
                {state.ask_price_3, (int32_t)state.ask_volume_3},
            };

            int remaining = std::min(std::abs(order.quantity), max_can_buy);
            for (auto& lvl : asks) {
                if (lvl.price == 0 || lvl.vol == 0 || remaining <= 0) break;
                if (order.price < (int32_t)lvl.price) break;
                int fill_qty = std::min(remaining, std::abs(lvl.vol));
                process_fills(ts, (int32_t)lvl.price, fill_qty, true);
                remaining -= fill_qty;
                order.quantity -= fill_qty;
            }
        }
        else if (order.is_sell() && state.bid_price_1 > 0 && order.price <= (int32_t)state.bid_price_1) {
            int max_can_sell = position_limit + position;
            if (max_can_sell <= 0) continue;

            struct Level { uint32_t price; int32_t vol; };
            Level bids[] = {
                {state.bid_price_1, (int32_t)state.bid_volume_1},
                {state.bid_price_2, (int32_t)state.bid_volume_2},
                {state.bid_price_3, (int32_t)state.bid_volume_3},
            };

            int remaining = std::min(std::abs(order.quantity), max_can_sell);
            for (auto& lvl : bids) {
                if (lvl.price == 0 || lvl.vol == 0 || remaining <= 0) break;
                if (order.price > (int32_t)lvl.price) break;
                int fill_qty = std::min(remaining, std::abs(lvl.vol));
                process_fills(ts, (int32_t)lvl.price, -fill_qty, true);
                remaining -= fill_qty;
                order.quantity += fill_qty;
            }
        }
    }

    // ═══ 3. STOCHASTIC PASSIVE FILLS ═══
    // This is the SOLE source of passive fills. No public trade matching.
    // Calibrated from real exchange data: bots interact with resting orders
    // at fixed rates regardless of quote width or order size.
    // Fill happens at YOUR resting price. Size is bot-determined (3-8 or 2-5).
    std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
    std::uniform_int_distribution<int> size_dist(min_fill_size, max_fill_size);

    for (auto& order : resting_orders) {
        if (order.quantity == 0) continue;

        if (order.is_buy()) {
            // Skip if this would be an aggressive cross (already handled above)
            if (state.ask_price_1 > 0 && order.price >= (int32_t)state.ask_price_1) continue;

            if (prob_dist(rng) < passive_fill_prob_buy) {
                int max_can_buy = position_limit - position;
                if (max_can_buy <= 0) continue;
                int bot_qty = size_dist(rng);
                int fill_qty = std::min({bot_qty, std::abs(order.quantity), max_can_buy});
                if (fill_qty > 0) {
                    process_fills(ts, order.price, fill_qty, false);
                    order.quantity -= fill_qty;
                }
            }
        }
        else if (order.is_sell()) {
            if (state.bid_price_1 > 0 && order.price <= (int32_t)state.bid_price_1) continue;

            if (prob_dist(rng) < passive_fill_prob_sell) {
                int max_can_sell = position_limit + position;
                if (max_can_sell <= 0) continue;
                int bot_qty = size_dist(rng);
                int fill_qty = std::min({bot_qty, std::abs(order.quantity), max_can_sell});
                if (fill_qty > 0) {
                    process_fills(ts, order.price, -fill_qty, false);
                    order.quantity += fill_qty;
                }
            }
        }
    }

    // ═══ 4. CLEANUP ═══
    resting_orders.erase(
        std::remove_if(resting_orders.begin(), resting_orders.end(),
            [](const StrategyOrder& o) { return o.quantity == 0; }),
        resting_orders.end()
    );

    // ═══ 5. MARK-TO-MARKET ═══
    double mid = current_state.mid_price();
    result.total_pnl = result.cash + (position * mid);
    result.update_drawdown(result.total_pnl);

    PnLSnapshot snap;
    snap.timestamp = ts;
    snap.position = position;
    snap.cash = result.cash;
    snap.mtm_pnl = result.total_pnl;
    result.pnl_history.push_back(snap);
}

void LimitOrderBook::process_fills(uint32_t timestamp, int32_t price, int32_t quantity, bool aggressive) {
    if (quantity == 0) return;

    Fill f;
    f.timestamp = timestamp;
    f.price = price;
    f.quantity = quantity;
    f.aggressive = aggressive;
    result.fills.push_back(f);

    if (quantity > 0) {
        result.total_buys++;
        result.total_volume += quantity;
    } else {
        result.total_sells++;
        result.total_volume += std::abs(quantity);
    }

    position += quantity;
    result.cash -= (double)price * quantity;
    result.final_position = position;
}