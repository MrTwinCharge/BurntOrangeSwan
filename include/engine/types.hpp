#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <cmath>

// ── Order Book Snapshot (3 levels of depth) ──────────────────────────────
// Packed for cache-line efficiency. Each level: price + volume.
struct alignas(64) OrderBookState {
    uint32_t timestamp;

    // Bid side (buy orders) — sorted descending by price
    uint32_t bid_price_1;   int32_t bid_volume_1;
    uint32_t bid_price_2;   int32_t bid_volume_2;
    uint32_t bid_price_3;   int32_t bid_volume_3;

    // Ask side (sell orders) — sorted ascending by price
    uint32_t ask_price_1;   int32_t ask_volume_1;
    uint32_t ask_price_2;   int32_t ask_volume_2;
    uint32_t ask_price_3;   int32_t ask_volume_3;

    // Mid price (x100 for 2 decimal places, e.g. 1000050 = 10000.50)
    int32_t mid_price_x100;

    // Helpers
    double mid_price() const { return mid_price_x100 / 100.0; }
    int32_t spread() const { return (int32_t)ask_price_1 - (int32_t)bid_price_1; }
    double weighted_mid() const {
        double bv = std::abs(bid_volume_1);
        double av = std::abs(ask_volume_1);
        if (bv + av == 0) return mid_price();
        return (bid_price_1 * av + ask_price_1 * bv) / (bv + av);
    }
    double obi() const {
        double bv = std::abs(bid_volume_1) + std::abs(bid_volume_2) + std::abs(bid_volume_3);
        double av = std::abs(ask_volume_1) + std::abs(ask_volume_2) + std::abs(ask_volume_3);
        if (bv + av == 0) return 0.0;
        return (bv - av) / (bv + av);
    }
};

// ── Public Trade Event ───────────────────────────────────────────────────
struct PublicTrade {
    uint32_t timestamp;
    int32_t  price;
    int32_t  quantity;  // positive = buyer-initiated, negative = seller-initiated
};

// ── Order (what the strategy sends) ──────────────────────────────────────
struct StrategyOrder {
    int32_t price;
    int32_t quantity;  // positive = buy, negative = sell

    bool is_buy()  const { return quantity > 0; }
    bool is_sell() const { return quantity < 0; }
};

// ── Fill (result of order matching) ──────────────────────────────────────
struct Fill {
    uint32_t timestamp;
    int32_t  price;
    int32_t  quantity;  // positive = bought, negative = sold
    bool     aggressive; // true = took liquidity, false = passive fill
};

// ── Per-product position limits (Prosperity 4) ───────────────────────────
// Update these as new rounds release products.
inline int get_position_limit(const std::string& product) {
    static const std::map<std::string, int> limits = {
        // Tutorial / Round 1
        {"RAINFOREST_RESIN", 50},
        {"KELP", 50},
        {"SQUID_INK", 50},
        // Round 2
        {"CROISSANTS", 250},
        {"JAMS", 350},
        {"DJEMBES", 60},
        {"PICNIC_BASKET1", 60},
        {"PICNIC_BASKET2", 100},
        // Round 3
        {"VOLCANIC_ROCK", 400},
        {"VOLCANIC_ROCK_VOUCHER_9500", 200},
        {"VOLCANIC_ROCK_VOUCHER_9750", 200},
        {"VOLCANIC_ROCK_VOUCHER_10000", 200},
        {"VOLCANIC_ROCK_VOUCHER_10250", 200},
        {"VOLCANIC_ROCK_VOUCHER_10500", 200},
        // Round 4
        {"MAGNIFICENT_MACARONS", 75},
    };
    auto it = limits.find(product);
    return (it != limits.end()) ? it->second : 50; // default 50
}

// ── PnL Snapshot (recorded each tick) ────────────────────────────────────
struct PnLSnapshot {
    uint32_t timestamp;
    int32_t  position;
    double   cash;        // cumulative cash from fills
    double   mid_price;   // last known mid
    double   mtm_pnl;     // cash + position * mid
};

// ── Backtest Result (per product) ────────────────────────────────────────
struct ProductResult {
    std::string symbol;
    int    total_buys     = 0;
    int    total_sells    = 0;
    int    total_volume   = 0;
    double total_pnl      = 0.0;
    int    final_position = 0;
    double max_drawdown   = 0.0;
    double peak_pnl       = 0.0;

    std::vector<Fill>        fills;
    std::vector<PnLSnapshot> pnl_history;

    void update_drawdown(double pnl) {
        if (pnl > peak_pnl) peak_pnl = pnl;
        double dd = peak_pnl - pnl;
        if (dd > max_drawdown) max_drawdown = dd;
    }
};