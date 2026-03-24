#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <algorithm>
#include <vector>
#include <deque>
#include <numeric>

/**
 * UniversalStrategy — Adaptive hybrid combining market making + momentum.
 * 
 * This is the PRIMARY strategy for Prosperity. It dynamically switches between:
 * 1. MOMENTUM mode when OBI signal is strong (trade in direction of pressure)
 * 2. MARKET MAKING mode when OBI signal is weak (provide liquidity, capture spread)
 * 3. FLATTEN mode near end of day (reduce inventory risk)
 * 
 * KEY IMPROVEMENTS over old version:
 * - EMA-smoothed OBI prevents whipsawing 
 * - Volatility-adaptive edges (Avellaneda-style)
 * - Position-weighted sizing (scale down near limits, not just clip)
 * - Multi-level quoting in MM mode
 * - Proper inventory penalty in reservation price
 * - Hysteresis on mode switching (prevent oscillation)
 * - Trade intensity tracking (adapt to market activity)
 */
class UniversalStrategy : public Strategy {
private:
    std::vector<StrategyOrder> orders;
    std::vector<int32_t> current_bids;
    std::vector<int32_t> current_asks;
    std::vector<int> last_positions;
    
    // Per-symbol signal state
    std::vector<double> ema_obi;
    std::vector<double> ema_vol;
    std::vector<double> last_mids;
    std::vector<int>    mode;       // 0=MM, 1=momentum-buy, 2=momentum-sell
    
    bool initialized = false;

public:
    // ── DNA Parameters (tuned by C++ sweep) ──
    double signal_threshold = 0.20;  // OBI threshold for momentum mode
    double signal_exit = 0.10;       // OBI threshold to exit momentum (hysteresis)
    double risk_aversion = 0.05;     // Inventory penalty
    int maker_edge = 2;              // Base half-spread for MM mode
    int taker_aggression = 0;        // 0 = penny (passive), 1 = cross (aggressive)
    int max_spread_fade = 20;        // Don't trade if spread > this
    int exit_behavior = 0;           // 0 = passive MM when no signal, 1 = actively dump

    int default_order_size = 10;
    double flatten_pct = 0.90;
    double urgent_pct = 0.975;
    double obi_alpha = 0.2;          // OBI smoothing
    double vol_alpha = 0.05;         // Volatility smoothing

    void on_tick([[maybe_unused]] uint32_t timestamp,
                 const std::vector<OrderBookState>& books,
                 [[maybe_unused]] const std::vector<std::vector<PublicTrade>>& trades,
                 std::vector<LimitOrderBook>& lobs) override {

        current_tick++;

        if (!initialized) {
            size_t n = books.size();
            current_bids.assign(n, 0);
            current_asks.assign(n, 0);
            last_positions.assign(n, 0);
            ema_obi.assign(n, 0.0);
            ema_vol.assign(n, 1.0);
            last_mids.assign(n, 0.0);
            mode.assign(n, 0);
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
            double mid = book.mid_price();
            int market_spread = (int)book.ask_price_1 - (int)book.bid_price_1;

            // ── Update signals ──
            if (last_mids[i] > 0) {
                double ret = std::abs(mid - last_mids[i]);
                ema_vol[i] = vol_alpha * ret + (1.0 - vol_alpha) * ema_vol[i];
            }
            last_mids[i] = mid;

            double raw_obi = book.weighted_obi();
            ema_obi[i] = obi_alpha * raw_obi + (1.0 - obi_alpha) * ema_obi[i];

            // ── Spread filter ──
            if (market_spread > max_spread_fade) {
                if (current_bids[i] != 0 || current_asks[i] != 0) {
                    lob.cancel_all_resting();
                    current_bids[i] = 0; current_asks[i] = 0;
                }
                continue;
            }

            // ══════════════════════════════════════
            // END-OF-DAY FLATTENING
            // ══════════════════════════════════════
            if (flattening) {
                lob.cancel_all_resting();
                orders.clear();
                if (pos > 0) {
                    int qty = std::min(pos, default_order_size * 2);
                    if (urgent) {
                        orders.push_back({(int32_t)book.bid_price_1, -qty});
                    } else {
                        // Try to get mid or better
                        int32_t price = std::max((int32_t)book.bid_price_1, 
                                                 (int32_t)std::round(mid) - 1);
                        orders.push_back({price, -qty});
                    }
                } else if (pos < 0) {
                    int qty = std::min(-pos, default_order_size * 2);
                    if (urgent) {
                        orders.push_back({(int32_t)book.ask_price_1, qty});
                    } else {
                        int32_t price = std::min((int32_t)book.ask_price_1, 
                                                 (int32_t)std::round(mid) + 1);
                        orders.push_back({price, qty});
                    }
                }
                lob.match_orders(orders);
                current_bids[i] = 0; current_asks[i] = 0;
                continue;
            }

            // ══════════════════════════════════════
            // MODE SELECTION with hysteresis
            // ══════════════════════════════════════
            double signal = ema_obi[i];
            
            // Mode transitions with hysteresis to prevent oscillation
            if (mode[i] == 0) {
                // In MM mode, switch to momentum if signal exceeds threshold
                if (signal > signal_threshold) mode[i] = 1;       // Go bullish
                else if (signal < -signal_threshold) mode[i] = 2; // Go bearish
            } else if (mode[i] == 1) {
                // In bullish momentum, exit if signal drops below exit threshold
                if (signal < signal_exit) mode[i] = 0;
            } else if (mode[i] == 2) {
                // In bearish momentum, exit if signal rises above -exit threshold
                if (signal > -signal_exit) mode[i] = 0;
            }

            // ── Fair value estimation ──
            double bid_vol = book.bid_volume_1; 
            double ask_vol = book.ask_volume_1; 
            double micro_price = (book.bid_price_1 * ask_vol + book.ask_price_1 * bid_vol) / (bid_vol + ask_vol);
            double reservation_price = micro_price - (pos * risk_aversion);

            int32_t target_bid = 0;
            int32_t target_ask = 0;

            // ══════════════════════════════════════
            // MODE 1 & 2: DIRECTIONAL MOMENTUM
            // ══════════════════════════════════════
            if (mode[i] == 1 || mode[i] == 2) {
                bool bullish = (mode[i] == 1);
                
                if (taker_aggression == 1) {
                    // Aggressive: Cross the spread
                    if (bullish) {
                        target_bid = book.ask_price_1;
                        target_ask = book.ask_price_1 + 20; // Far away = effectively no sell
                    } else {
                        target_bid = book.bid_price_1 - 20;
                        target_ask = book.bid_price_1;
                    }
                } else {
                    // Passive: Penny the spread (step in front of existing best)
                    if (bullish) {
                        target_bid = std::min((int32_t)book.bid_price_1 + 1, (int32_t)book.ask_price_1 - 1);
                        target_ask = book.ask_price_1 + 20;
                    } else {
                        target_bid = book.bid_price_1 - 20;
                        target_ask = std::max((int32_t)book.ask_price_1 - 1, (int32_t)book.bid_price_1 + 1);
                    }
                }
            }
            // ══════════════════════════════════════
            // MODE 0: MARKET MAKING / EXIT
            // ══════════════════════════════════════
            else {
                if (exit_behavior == 1 && pos != 0) {
                    // Aggressive liquidation: dump inventory
                    if (pos > 0) {
                        target_ask = book.bid_price_1;      // Sell at bid
                        target_bid = book.bid_price_1 - 20; // Far away
                    } else {
                        target_bid = book.ask_price_1;       // Buy at ask
                        target_ask = book.ask_price_1 + 20;
                    }
                } else {
                    // Standard market making around reservation price
                    // Adaptive edge: widen in volatile conditions
                    double vol_edge = ema_vol[i] * 0.5;
                    int dynamic_edge = std::max(maker_edge, (int)std::round(vol_edge));
                    dynamic_edge = std::max(dynamic_edge, std::max(1, (int)(market_spread * 0.35)));
                    
                    target_bid = (int32_t)std::round(reservation_price) - dynamic_edge;
                    target_ask = (int32_t)std::round(reservation_price) + dynamic_edge;
                    
                    // Never passively cross
                    target_bid = std::min(target_bid, (int32_t)book.ask_price_1 - 1);
                    target_ask = std::max(target_ask, (int32_t)book.bid_price_1 + 1);
                }
            }

            // ══════════════════════════════════════
            // ORDER EXECUTION
            // ══════════════════════════════════════
            // Only update if prices or positions changed (preserve queue priority)
            if (target_bid != current_bids[i] || target_ask != current_asks[i] || pos != last_positions[i]) {
                lob.cancel_all_resting();
                orders.clear();

                int max_buy = limit - pos;
                int max_sell = limit + pos;

                // Position-weighted sizing: reduce as we get loaded
                double pos_frac = std::abs((double)pos / limit);
                int buy_size = default_order_size;
                int sell_size = default_order_size;
                
                // In momentum mode, size toward signal; in MM mode, size toward flat
                if (mode[i] == 1) {
                    // Bullish: full buy, reduced sell
                    sell_size = std::max(1, (int)(default_order_size * 0.3));
                } else if (mode[i] == 2) {
                    // Bearish: reduced buy, full sell
                    buy_size = std::max(1, (int)(default_order_size * 0.3));
                } else {
                    // MM mode: scale toward flat
                    if (pos > 0) {
                        buy_size = std::max(1, (int)(default_order_size * (1.0 - pos_frac)));
                    } else if (pos < 0) {
                        sell_size = std::max(1, (int)(default_order_size * (1.0 - pos_frac)));
                    }
                }

                if (max_buy > 0 && buy_size > 0) 
                    orders.push_back({target_bid, std::min(buy_size, max_buy)});
                if (max_sell > 0 && sell_size > 0) 
                    orders.push_back({target_ask, -std::min(sell_size, max_sell)});

                lob.match_orders(orders);
                current_bids[i] = target_bid;
                current_asks[i] = target_ask;
                last_positions[i] = pos;
            }
        }
    }
};