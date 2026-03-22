#include "engine/lob.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <set>

static void print_cal_once(const std::string& msg) {
    static std::set<std::string> printed;
    if (printed.insert(msg).second) {
        std::cout << msg << std::endl;
    }
}

LimitOrderBook::LimitOrderBook(const std::string& sym) {
    symbol = sym;
    position_limit = get_position_limit(sym);
    result.symbol = sym;
    set_hardcoded_rates();
}

void LimitOrderBook::init_rng() {
    if (!rng_initialized) {
        std::hash<std::string> hasher;
        rng.seed(hasher(symbol) ^ 42);
        rng_initialized = true;
    }
}

void LimitOrderBook::set_hardcoded_rates() {
    // Known products calibrated from 5 real Prosperity 4 submissions
    if (symbol == "EMERALDS") {
        passive_fill_prob_buy = 10.0 / 2000.0;   // 0.005
        passive_fill_prob_sell = 16.0 / 2000.0;   // 0.008
        min_fill_size = 3;
        max_fill_size = 8;
        use_hardcoded = true;
    } else if (symbol == "TOMATOES") {
        passive_fill_prob_buy = 35.0 / 2000.0;    // 0.0175
        passive_fill_prob_sell = 32.0 / 2000.0;    // 0.016
        min_fill_size = 2;
        max_fill_size = 5;
        use_hardcoded = true;
    } else {
        // Unknown product — will auto-calibrate from observed trade data
        use_hardcoded = false;
        auto_calibrated = false;
        // Conservative defaults until calibration completes
        passive_fill_prob_buy = 0.0075;
        passive_fill_prob_sell = 0.0075;
        min_fill_size = 2;
        max_fill_size = 6;
    }
}

void LimitOrderBook::update_calibration(const std::vector<PublicTrade>& trades, const OrderBookState& state) {
    if (auto_calibrated) return;
    if (ticks_seen >= CALIBRATION_WINDOW) return;

    ticks_seen++;

    for (const auto& trade : trades) {
        trades_observed++;
        int qty = std::abs(trade.quantity);
        if (qty < min_qty_seen) min_qty_seen = qty;
        if (qty > max_qty_seen) max_qty_seen = qty;
        sum_qty += qty;

        // Classify: was this bot buying (trade at ask) or selling (trade at bid)?
        if (state.ask_price_1 > 0 && trade.price >= (int32_t)state.ask_price_1) {
            buy_trades++;   // bot bought aggressively → would sell to our bid passively
        } else if (state.bid_price_1 > 0 && trade.price <= (int32_t)state.bid_price_1) {
            sell_trades++;  // bot sold aggressively → would buy from our ask passively
        } else {
            // Mid-price trade — split evenly
            buy_trades++;
            sell_trades++;
        }
    }

    // Auto-finalize after calibration window
    if (ticks_seen >= CALIBRATION_WINDOW) {
        finalize_calibration();
    }
}

void LimitOrderBook::finalize_calibration() {
    if (auto_calibrated) return;
    if (ticks_seen == 0) return;

    // Compute observed fill rates
    double obs_rate = (double)trades_observed / ticks_seen;
    double fill_rate = obs_rate * TUTORIAL_TO_REAL_RATIO;

    double total_dir = buy_trades + sell_trades;
    double buy_frac = (total_dir > 0) ? (double)sell_trades / total_dir : 0.5;
    double sell_frac = (total_dir > 0) ? (double)buy_trades / total_dir : 0.5;

    double obs_buy = std::max(0.001, std::min(0.05, fill_rate * buy_frac));
    double obs_sell = std::max(0.001, std::min(0.05, fill_rate * sell_frac));

    int obs_min_size = (min_qty_seen <= max_qty_seen && min_qty_seen < 999) ? min_qty_seen : min_fill_size;
    int obs_max_size = (max_qty_seen > 0) ? max_qty_seen : max_fill_size;

    if (use_hardcoded) {
        // Check if observed behavior diverges >30% from hardcoded
        double hardcoded_total = passive_fill_prob_buy + passive_fill_prob_sell;
        double obs_total = obs_buy + obs_sell;
        double divergence = std::abs(obs_total - hardcoded_total) / hardcoded_total;

        if (divergence > 0.30) {
            // Behavior changed — override with observed
            passive_fill_prob_buy = obs_buy;
            passive_fill_prob_sell = obs_sell;
            min_fill_size = obs_min_size;
            max_fill_size = obs_max_size;
            print_cal_once("[LOB Re-Cal] " + symbol + ":"
                + " hardcoded=" + std::to_string(hardcoded_total)
                + " observed=" + std::to_string(obs_total)
                + " -> OVERRIDING with observed rates");
        } else {
            print_cal_once("[LOB Cal-OK] " + symbol + ":"
                + " hardcoded rates confirmed (divergence="
                + std::to_string((int)(divergence * 100)) + "%)");
        }
    } else {
        // Unknown product — use observed
        passive_fill_prob_buy = obs_buy;
        passive_fill_prob_sell = obs_sell;
        min_fill_size = obs_min_size;
        max_fill_size = obs_max_size;

        print_cal_once("[LOB Auto-Cal] " + symbol + ":"
            + " fill_buy=" + std::to_string(passive_fill_prob_buy)
            + " fill_sell=" + std::to_string(passive_fill_prob_sell)
            + " size=" + std::to_string(min_fill_size) + "-" + std::to_string(max_fill_size)
            + " (from " + std::to_string(trades_observed) + " trades in "
            + std::to_string(ticks_seen) + " ticks)");
    }

    auto_calibrated = true;
}

void LimitOrderBook::cancel_all_resting() {
    cancel_requested = true;
}

void LimitOrderBook::match_orders(const std::vector<StrategyOrder>& orders) {
    pending_orders = orders;
}

void LimitOrderBook::update(const OrderBookState& state, const std::vector<PublicTrade>& trades) {
    current_state = state;
    uint32_t ts = state.timestamp;
    init_rng();

    // ═══ 0. AUTO-CALIBRATION: observe trade data for unknown products ═══
    update_calibration(trades, state);

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
    std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
    std::uniform_int_distribution<int> size_dist(min_fill_size, std::max(min_fill_size, max_fill_size));

    for (auto& order : resting_orders) {
        if (order.quantity == 0) continue;

        if (order.is_buy()) {
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