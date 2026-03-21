#pragma once
#include "engine/types.hpp"
#include "engine/lob.hpp"
#include <string>
#include <map>
#include <vector>

class Strategy {
public:
    virtual ~Strategy() = default;

    /**
     * Called once per tick with the current state of all products.
     *
     * @param timestamp     Current tick timestamp (ms)
     * @param books         L3 order book snapshot per symbol
     * @param trades        Public trades since last tick per symbol
     * @param lobs          Limit order books (strategy places orders via these)
     *
     * The strategy should call lob.match_orders() with its desired orders.
     * The engine will record all fills and PnL automatically.
     */
    virtual void on_tick(uint32_t timestamp,
                         const std::map<std::string, OrderBookState>& books,
                         const std::map<std::string, std::vector<PublicTrade>>& trades,
                         std::map<std::string, LimitOrderBook>& lobs) = 0;
};