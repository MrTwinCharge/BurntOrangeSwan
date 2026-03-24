#pragma once
#include "engine/types.hpp"
#include "engine/lob.hpp"
#include <string>
#include <vector>
#include <algorithm>

class Strategy {
public:
    virtual ~Strategy() = default;

    std::vector<std::string> target_symbols;
    std::vector<bool> trade_flags;

    // Set by the engine/sweeper before the tick loop starts
    int total_ticks = 0;
    int current_tick = 0; // NEW: Track current tick for time-based logic

    virtual void init(const std::vector<std::string>& symbols) {
        trade_flags.assign(symbols.size(), false);
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (target_symbols.empty() ||
                std::find(target_symbols.begin(), target_symbols.end(), symbols[i]) != target_symbols.end()) {
                trade_flags[i] = true;
            }
        }
    }

    // Helper: fraction of session elapsed [0.0, 1.0]
    double session_progress() const {
        if (total_ticks <= 0) return 0.0;
        return (double)current_tick / (double)total_ticks;
    }
    
    // Helper: are we in the flattening window?
    bool should_flatten(double flatten_pct = 0.90) const {
        return session_progress() >= flatten_pct;
    }
    
    bool should_urgent_flatten(double urgent_pct = 0.975) const {
        return session_progress() >= urgent_pct;
    }

    virtual void on_tick(uint32_t timestamp,
                         const std::vector<OrderBookState>& books,
                         const std::vector<std::vector<PublicTrade>>& trades,
                         std::vector<LimitOrderBook>& lobs) = 0;
};