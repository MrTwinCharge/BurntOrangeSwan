#pragma once
#include "engine/strategy.hpp"
#include <cmath>
#include <iostream>
#include <algorithm>
 
class MarketMaker : public Strategy {
public:
    struct Config {
        double fair_value;
        int    edge_threshold;  // minimum edge (fair - ask, or bid - fair) to take
        int    order_size;
        bool   use_fixed_fair;
    };
 
    std::map<std::string, Config> overrides;
    int    default_edge       = 1;   // take anything with >= 1 tick edge vs fair
    int    default_order_size = 10;
    bool   verbose            = false;
 
    MarketMaker() {
        overrides["RAINFOREST_RESIN"] = {10000.0, 1, 20, true};
    }
 
    void on_tick(uint32_t timestamp,
                 const std::map<std::string, OrderBookState>& books,
                 const std::map<std::string, std::vector<PublicTrade>>& /*trades*/,
                 std::map<std::string, LimitOrderBook>& lobs) override {
 
        for (const auto& [symbol, book] : books) {
            auto& lob = lobs[symbol];
 
            if (book.bid_price_1 == 0 || book.ask_price_1 == 0) {
                lob.match_orders({});
                continue;
            }
 
            // Resolve config
            int edge = default_edge;
            int size = default_order_size;
            double fair = 0;
            bool fixed_fair = false;
 
            auto it = overrides.find(symbol);
            if (it != overrides.end()) {
                edge = it->second.edge_threshold;
                size = it->second.order_size;
                fair = it->second.fair_value;
                fixed_fair = it->second.use_fixed_fair;
            }
 
            if (!fixed_fair || fair == 0) {
                fair = book.weighted_mid();
                if (fair == 0) fair = book.mid_price();
            }
            if (fair == 0) {
                lob.match_orders({});
                continue;
            }
 
            int limit = lob.position_limit;
            int pos = lob.position;
            std::vector<StrategyOrder> orders;
 
            // ── Take liquidity when edge vs fair is sufficient ──
            // BUY side: buy from asks that are below fair - edge
            // (we profit because we're buying cheap relative to fair)
            auto try_buy = [&](uint32_t price, int32_t volume) {
                if (price == 0 || volume == 0) return;
                double buy_edge = fair - (double)price;
                if (buy_edge >= edge && pos < limit) {
                    int qty = std::min({std::abs(volume), size, limit - pos});
                    if (qty > 0) {
                        orders.push_back({(int32_t)price, qty});
                        pos += qty; // track locally to not exceed limit across levels
                    }
                }
            };
 
            // SELL side: sell to bids that are above fair + edge
            auto try_sell = [&](uint32_t price, int32_t volume) {
                if (price == 0 || volume == 0) return;
                double sell_edge = (double)price - fair;
                if (sell_edge >= edge && pos > -limit) {
                    int qty = std::min({std::abs(volume), size, limit + pos});
                    if (qty > 0) {
                        orders.push_back({(int32_t)price, -qty});
                        pos -= qty;
                    }
                }
            };
 
            // Walk all 3 ask levels (buy cheap)
            try_buy(book.ask_price_1, book.ask_volume_1);
            try_buy(book.ask_price_2, book.ask_volume_2);
            try_buy(book.ask_price_3, book.ask_volume_3);
 
            // Walk all 3 bid levels (sell expensive)
            try_sell(book.bid_price_1, book.bid_volume_1);
            try_sell(book.bid_price_2, book.bid_volume_2);
            try_sell(book.bid_price_3, book.bid_volume_3);
 
            // ── Position management: unwind when we're too heavy ──
            // If we're long and there's a bid near fair, flatten
            if (pos > limit / 2 && book.bid_price_1 > 0) {
                double unwind_cost = fair - (double)book.bid_price_1;
                if (unwind_cost < 3) { // willing to lose up to 3 ticks to flatten
                    int qty = std::min(pos - limit / 4, std::abs(book.bid_volume_1));
                    if (qty > 0) orders.push_back({(int32_t)book.bid_price_1, -qty});
                }
            }
            // If we're short and there's an ask near fair, flatten
            if (pos < -limit / 2 && book.ask_price_1 > 0) {
                double unwind_cost = (double)book.ask_price_1 - fair;
                if (unwind_cost < 3) {
                    int qty = std::min(-pos - limit / 4, std::abs(book.ask_volume_1));
                    if (qty > 0) orders.push_back({(int32_t)book.ask_price_1, qty});
                }
            }
 
            auto fills = lob.match_orders(orders);
 
            if (verbose) {
                for (const auto& f : fills) {
                    std::cout << "[" << timestamp << "] MM "
                              << (f.quantity > 0 ? "BUY " : "SELL ")
                              << symbol << " " << std::abs(f.quantity)
                              << "@" << f.price
                              << " | Fair=" << fair
                              << " | Edge=" << (f.quantity > 0 ? fair - f.price : f.price - fair)
                              << " | Pos=" << lob.position << std::endl;
                }
            }
        }
    }
};