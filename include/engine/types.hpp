#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cmath>
#include <algorithm>

// ═══════════════════════════════════════════════════
// Prosperity 4 Position Limits
// ═══════════════════════════════════════════════════
// Updated to match Prosperity 3 known limits + extensible for P4.
// CRITICAL FIX: Old code hardcoded 20 for everything.
inline int get_position_limit(const std::string& symbol) {
    // Prosperity 3 known limits
    if (symbol == "RAINFOREST_RESIN")     return 50;
    if (symbol == "KELP")                 return 50;
    if (symbol == "SQUID_INK")            return 50;
    if (symbol == "CROISSANTS")           return 250;
    if (symbol == "JAMS")                 return 350;
    if (symbol == "DJEMBES")              return 60;
    if (symbol == "PICNIC_BASKET1")       return 60;
    if (symbol == "PICNIC_BASKET2")       return 100;
    if (symbol == "VOLCANIC_ROCK")        return 400;
    if (symbol == "MAGNIFICENT_MACARONS") return 75;
    // Prosperity 2 products
    if (symbol == "AMETHYSTS")            return 20;
    if (symbol == "STARFRUIT")            return 20;
    if (symbol == "ORCHIDS")              return 100;
    // Prosperity 4 placeholder (update when products are revealed)
    if (symbol == "EMERALDS")             return 50;
    if (symbol == "TOMATOES")             return 50;
    return 50; // Safe default (was 20, too conservative)
}

struct OrderBookState {
    uint32_t timestamp;
    uint32_t bid_price_1, bid_volume_1;
    uint32_t bid_price_2, bid_volume_2;
    uint32_t bid_price_3, bid_volume_3;
    uint32_t ask_price_1, ask_volume_1;
    uint32_t ask_price_2, ask_volume_2;
    uint32_t ask_price_3, ask_volume_3;
    int32_t  mid_price_x100;
    
    double mid_price() const { return mid_price_x100 / 100.0; }
    
    double spread() const {
        if (bid_price_1 == 0 || ask_price_1 == 0) return 0.0;
        return (double)ask_price_1 - (double)bid_price_1;
    }
    
    // Volume-weighted microprice (Stoikov-style)
    double weighted_mid() const {
        if (bid_volume_1 == 0 || ask_volume_1 == 0) return mid_price();
        return (double(bid_price_1) * ask_volume_1 + double(ask_price_1) * bid_volume_1) / 
               (double)(bid_volume_1 + ask_volume_1);
    }
    
    // Multi-level weighted mid for deeper signal
    double deep_weighted_mid() const {
        double bid_vol = bid_volume_1 + bid_volume_2 + bid_volume_3;
        double ask_vol = ask_volume_1 + ask_volume_2 + ask_volume_3;
        if (bid_vol == 0 || ask_vol == 0) return mid_price();
        
        double bid_vwap = (double(bid_price_1) * bid_volume_1 + 
                           double(bid_price_2) * bid_volume_2 + 
                           double(bid_price_3) * bid_volume_3) / bid_vol;
        double ask_vwap = (double(ask_price_1) * ask_volume_1 + 
                           double(ask_price_2) * ask_volume_2 + 
                           double(ask_price_3) * ask_volume_3) / ask_vol;
        return (bid_vwap * ask_vol + ask_vwap * bid_vol) / (bid_vol + ask_vol);
    }
    
    // Order Book Imbalance (L1 only - cleaner signal)
    double obi_l1() const {
        if (bid_volume_1 == 0 && ask_volume_1 == 0) return 0.0;
        return (double(bid_volume_1) - double(ask_volume_1)) / 
               (double(bid_volume_1) + double(ask_volume_1));
    }
    
    // Multi-level OBI (noisier but captures depth pressure)
    double obi() const {
        double b_vol = bid_volume_1 + bid_volume_2 + bid_volume_3;
        double a_vol = ask_volume_1 + ask_volume_2 + ask_volume_3;
        if (b_vol + a_vol == 0) return 0.0;
        return (b_vol - a_vol) / (b_vol + a_vol);
    }
    
    // Trade Pressure Imbalance across levels (weighted by proximity)
    double weighted_obi() const {
        // Weight closer levels more (3x for L1, 2x for L2, 1x for L3)
        double b_vol = 3.0 * bid_volume_1 + 2.0 * bid_volume_2 + 1.0 * bid_volume_3;
        double a_vol = 3.0 * ask_volume_1 + 2.0 * ask_volume_2 + 1.0 * ask_volume_3;
        if (b_vol + a_vol == 0) return 0.0;
        return (b_vol - a_vol) / (b_vol + a_vol);
    }
    
    // Total book depth
    double total_depth() const {
        return bid_volume_1 + bid_volume_2 + bid_volume_3 +
               ask_volume_1 + ask_volume_2 + ask_volume_3;
    }
};

struct PublicTrade {
    uint32_t timestamp;
    char symbol[16];
    int32_t price;
    int32_t quantity;
};

struct StrategyOrder {
    int32_t price;
    int32_t quantity;
    int32_t queue_ahead = 0;

    bool is_buy() const { return quantity > 0; }
    bool is_sell() const { return quantity < 0; }
};

struct Fill {
    uint32_t timestamp;
    int32_t price;
    int32_t quantity;
    bool aggressive;
};

struct PnLSnapshot {
    uint32_t timestamp;
    int32_t  position;
    double   cash;               
    double   liquidation_price;  
    double   mtm_pnl;            
};

struct ProductResult {
    std::string symbol;
    double total_pnl = 0.0;
    double cash = 0.0;
    int total_buys = 0;
    int total_sells = 0;
    int total_volume = 0;
    int final_position = 0;
    double max_drawdown = 0.0;
    double peak_pnl = 0.0;
    
    std::vector<PnLSnapshot> pnl_history;
    std::vector<Fill> fills;

    void update_drawdown(double current_pnl) {
        if (current_pnl > peak_pnl) peak_pnl = current_pnl;
        double dd = peak_pnl - current_pnl;
        if (dd > max_drawdown) max_drawdown = dd;
    }
};