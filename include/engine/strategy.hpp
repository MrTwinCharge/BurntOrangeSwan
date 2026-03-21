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
    
    // 🚀 OPTIMIZATION 3: Fast O(1) integer array replacing slow std::map string lookups
    std::vector<bool> trade_flags; 

    virtual void init(const std::vector<std::string>& symbols) {
        trade_flags.assign(symbols.size(), false);
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (target_symbols.empty() || 
                std::find(target_symbols.begin(), target_symbols.end(), symbols[i]) != target_symbols.end()) {
                trade_flags[i] = true;
            }
        }
    }

    virtual void on_tick(uint32_t timestamp,
                         const std::vector<OrderBookState>& books,
                         const std::vector<std::vector<PublicTrade>>& trades,
                         std::vector<LimitOrderBook>& lobs) = 0;
};