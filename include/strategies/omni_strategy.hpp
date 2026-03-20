#include "engine/strategy.hpp"
#include <iostream>
#include <cmath>

class OmniImbalance : public Strategy {
public:
    void on_tick(uint32_t timestamp, 
                 const std::map<std::string, OrderBookState>& books,
                 const std::map<std::string, std::vector<PublicTrade>>& trades,
                 std::map<std::string, LimitOrderBook>& lobs) override {
        
        for (auto const& [symbol, book] : books) {
            // 1. Calculate OBI
            double bid_vol = book.best_bid_volume;
            double ask_vol = std::abs((double)book.best_ask_volume);
            double imbalance = (bid_vol - ask_vol) / (bid_vol + ask_vol);

            // 2. Track "Aggression" (Did someone just sweep the book?)
            int trade_volume = 0;
            if (trades.count(symbol)) {
                for (const auto& t : trades.at(symbol)) {
                    trade_volume += t.quantity;
                }
            }

            // 3. NEW LOGIC: Buy if imbalance is high OR if we see trade activity 
            // confirming the move. 
            // Threshold lowered to 0.4 for more activity.
            if (imbalance > 0.4 && lobs[symbol].position < 200) {
                auto res = lobs[symbol].market_buy(10, book.best_ask_price);
                if(res.quantity > 0) {
                    std::cout << "[" << timestamp << "] BUY " << symbol << " @ " << book.best_ask_price << " | OBI: " << imbalance << std::endl;
                }
            } 
            else if (imbalance < -0.4 && lobs[symbol].position > -200) {
                auto res = lobs[symbol].market_sell(10, book.best_bid_price);
                if(res.quantity > 0) {
                    std::cout << "[" << timestamp << "] SELL " << symbol << " @ " << book.best_bid_price << " | OBI: " << imbalance << std::endl;
                }
            }
        }
    }
};