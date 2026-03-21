#pragma once
#include "engine/types.hpp"
#include <string>
#include <vector>
#include <random>

/**
 * LimitOrderBook — Calibrated against real Prosperity 4 exchange.
 *
 * CALIBRATION (from 5 real submissions):
 *   EMERALDS: ~10 buy fills + ~16 sell fills per 2000 ticks, size 3-8
 *   TOMATOES: ~35 buy fills + ~32 sell fills per 2000 ticks, size 2-5
 *   Fill count is independent of quote width or order size.
 *   Fills happen at YOUR resting price.
 *
 * FILL MODEL:
 *   - Aggressive: orders crossing the spread fill immediately at book price
 *   - Passive: stochastic fills at calibrated rates (no public trade matching)
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

private:
    void process_fills(uint32_t timestamp, int32_t price, int32_t quantity, bool aggressive);

    double passive_fill_prob_buy = 0.005;
    double passive_fill_prob_sell = 0.008;
    int min_fill_size = 3;
    int max_fill_size = 8;

    std::mt19937 rng;
    bool rng_initialized = false;

    void init_rng();
    void calibrate_for_product();
};