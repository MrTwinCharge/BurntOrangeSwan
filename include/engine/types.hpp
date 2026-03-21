#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>

inline int get_position_limit(const std::string& symbol) {
    if (symbol == "AMETHYSTS" || symbol == "EMERALDS") return 20;
    if (symbol == "STARFRUIT" || symbol == "TOMATOES") return 20;
    return 20; // Default fallback
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
    
    double weighted_mid() const {
        if (bid_volume_1 == 0 || ask_volume_1 == 0) return mid_price();
        return (bid_price_1 * ask_volume_1 + ask_price_1 * bid_volume_1) / 
               (double)(bid_volume_1 + ask_volume_1);
    }
    
    double obi() const {
        if (bid_volume_1 == 0 && ask_volume_1 == 0) return 0.0;
        double b_vol = bid_volume_1 + bid_volume_2 + bid_volume_3;
        double a_vol = ask_volume_1 + ask_volume_2 + ask_volume_3;
        return (b_vol - a_vol) / (b_vol + a_vol);
    }
};

struct PublicTrade {
    uint32_t timestamp;
    std::string symbol;
    int32_t price;
    int32_t quantity;
    // Note: CSV doesn't specify side aggressively, but we can infer based on price relative to mid.
};

struct StrategyOrder {
    int32_t price;
    int32_t quantity;
    int32_t queue_ahead = 0; // NEW: Volume ahead of us in the book

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