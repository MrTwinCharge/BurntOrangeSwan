#include <iostream>
#include <vector>
#include <map>
#include "engine/loader.hpp"
#include "engine/lob.hpp"
#include "strategies/omni_strategy.hpp"

int main() {
    std::vector<std::string> symbols = {"TOMATOES", "EMERALDS"}; 
    
    std::map<std::string, const OrderBookState*> price_data;
    std::map<std::string, const PublicTrade*> trade_data;
    std::map<std::string, size_t> trade_counts;
    std::map<std::string, LimitOrderBook> lobs;
    
    size_t total_ticks = 0;

    for (const auto& sym : symbols) {
        size_t p_ticks = 0, t_events = 0;
        
        // Load Price Books
        price_data[sym] = load_binary_data("../data/binary/" + sym + "_prices.bin", p_ticks);
        
        // Load Trade Events (The reinterpret_cast is needed because load_binary_data 
        // currently returns OrderBookState pointers)
        trade_data[sym] = (const PublicTrade*)load_binary_data("../data/binary/" + sym + "_trades.bin", t_events);
        trade_counts[sym] = t_events;
        
        total_ticks = (total_ticks == 0) ? p_ticks : std::min(total_ticks, p_ticks);
        lobs[sym] = LimitOrderBook(); 
    }

    OmniImbalance my_strategy;
    std::map<std::string, size_t> trade_ptr; // Keeps track of where we are in each trade file

    std::cout << "[Engine] Booting Multi-Asset Engine with Trade Logic..." << std::endl;

    for (size_t i = 0; i < total_ticks; ++i) {
        uint32_t current_ts = i * 100;
        
        std::map<std::string, OrderBookState> current_books;
        std::map<std::string, std::vector<PublicTrade>> current_trades;

        for (const auto& sym : symbols) {
            current_books[sym] = price_data[sym][i];
            lobs[sym].update(current_books[sym]);

            // Sync trades: Get all trades that happened since the last tick
            while (trade_ptr[sym] < trade_counts[sym] && 
                   trade_data[sym][trade_ptr[sym]].timestamp <= current_ts) {
                current_trades[sym].push_back(trade_data[sym][trade_ptr[sym]]);
                trade_ptr[sym]++;
            }
        }

        my_strategy.on_tick(current_ts, current_books, current_trades, lobs);
    }

    // Print Results
    for (const auto& sym : symbols) {
        std::cout << sym << " | Final Pos: " << lobs[sym].position << std::endl;
    }

    return 0;
}