#pragma once
#include "engine/types.hpp"
#include "engine/lob.hpp"
#include <map>
#include <string>
#include <vector>
#include <algorithm>

class Strategy {
public:
    virtual ~Strategy() = default;

    // Filter to limit which products this strategy trades. 
    // Empty means trade all available products.
    std::vector<std::string> target_symbols; 

    bool should_trade(const std::string& symbol) const {
        if (target_symbols.empty()) return true;
        return std::find(target_symbols.begin(), target_symbols.end(), symbol) != target_symbols.end();
    }

    virtual void on_tick(uint32_t timestamp,
                         const std::map<std::string, OrderBookState>& books,
                         const std::map<std::string, std::vector<PublicTrade>>& trades,
                         std::map<std::string, LimitOrderBook>& lobs) = 0;
};