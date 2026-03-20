#pragma once
#include "engine/types.hpp"
#include "engine/lob.hpp"
#include <string>
#include <map>
#include <vector>

class Strategy {
public:
    virtual ~Strategy() = default;
    
    virtual void on_tick(uint32_t timestamp, 
                        const std::map<std::string, OrderBookState>& books,
                        const std::map<std::string, std::vector<PublicTrade>>& trades, // NEW
                        std::map<std::string, LimitOrderBook>& lobs) = 0;
};