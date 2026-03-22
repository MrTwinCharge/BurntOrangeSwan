#pragma once
#include "engine/types.hpp"
#include <string>
#include <vector>
#include <random>

/**
 * LimitOrderBook — Self-calibrating passive fill model.
 *
 * AUTO-TUNING:
 *   On the first pass through data, the LOB observes public trade frequency
 *   and sizes for each product. It then sets passive fill rates to ~70% of
 *   the observed bot trade rate (calibrated ratio from 5 real submissions).
 *
 *   For known products (EMERALDS, TOMATOES), hardcoded overrides from real
 *   exchange data take precedence. For any new product, the auto-tuned
 *   rates kick in automatically.
 *
 * FILL MODEL:
 *   - Aggressive: orders crossing the spread fill at book price
 *   - Passive: stochastic fills at auto-tuned rates, at YOUR resting price
 *   - 1-tick latency on all orders
 */
class LimitOrderBook {
public:
    std::string symbol;
    int position = 0;
    int position_limit = 50;

    ProductResult result;

    std::vector<StrategyOrder> resting_orders;
    std::vector<StrategyOrder> pending_orders;
    bool cancel_requested = false;
    OrderBookState current_state;

    LimitOrderBook() = default;
    LimitOrderBook(const std::string& sym);

    void match_orders(const std::vector<StrategyOrder>& orders);
    void cancel_all_resting();
    void update(const OrderBookState& state, const std::vector<PublicTrade>& trades);

    // Call after a calibration pass to lock in observed rates
    void finalize_calibration();

    // Get calibration info for logging
    double get_fill_prob_buy() const { return passive_fill_prob_buy; }
    double get_fill_prob_sell() const { return passive_fill_prob_sell; }
    int get_min_fill_size() const { return min_fill_size; }
    int get_max_fill_size() const { return max_fill_size; }
    bool is_auto_calibrated() const { return auto_calibrated; }

private:
    void process_fills(uint32_t timestamp, int32_t price, int32_t quantity, bool aggressive);

    // Fill parameters
    double passive_fill_prob_buy = 0.0075;
    double passive_fill_prob_sell = 0.0075;
    int min_fill_size = 2;
    int max_fill_size = 6;

    // Auto-calibration state
    static constexpr double TUTORIAL_TO_REAL_RATIO = 0.70;
    static constexpr int CALIBRATION_WINDOW = 500; // observe first N ticks

    bool use_hardcoded = false;     // true for known products
    bool auto_calibrated = false;   // true once calibration is finalized
    int ticks_seen = 0;
    int trades_observed = 0;        // total public trades seen during calibration
    int min_qty_seen = 999;
    int max_qty_seen = 0;
    double sum_qty = 0;
    int buy_trades = 0;             // trades at ask (aggressive buys by bots)
    int sell_trades = 0;            // trades at bid (aggressive sells by bots)

    // RNG
    std::mt19937 rng;
    bool rng_initialized = false;

    void init_rng();
    void set_hardcoded_rates();
    void update_calibration(const std::vector<PublicTrade>& trades, const OrderBookState& state);
};