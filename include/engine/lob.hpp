#pragma once
#include "engine/types.hpp"
#include <vector>
#include <algorithm>
#include <cmath>
#include <string>

class LimitOrderBook {
public:
    std::string symbol;
    int position = 0;
    int position_limit = 50;
    double cash = 0.0;        // cumulative P&L from fills
    double last_mid = 0.0;    // last known mid price

    // Result tracking
    ProductResult result;

    LimitOrderBook() = default;
    LimitOrderBook(const std::string& sym) 
        : symbol(sym), position_limit(get_position_limit(sym)) {
        result.symbol = sym;
    }

    // Update the book with the current market snapshot
    void update(const OrderBookState& state) {
        current = state;
        if (state.mid_price_x100 != 0) {
            last_mid = state.mid_price();
        }
    }

    // ── Order Matching Engine ────────────────────────────────────────────
    // Matches a list of strategy orders against the current book state.
    // Returns fills. Mutates position, cash, and result.
    std::vector<Fill> match_orders(const std::vector<StrategyOrder>& orders) {
        std::vector<Fill> fills;

        // Build the book levels we can trade against
        // Asks: levels available to buy from (ascending price)
        struct Level { int32_t price; int32_t volume; };
        std::vector<Level> asks, bids;

        if (current.ask_price_1 > 0 && current.ask_volume_1 != 0)
            asks.push_back({(int32_t)current.ask_price_1, std::abs(current.ask_volume_1)});
        if (current.ask_price_2 > 0 && current.ask_volume_2 != 0)
            asks.push_back({(int32_t)current.ask_price_2, std::abs(current.ask_volume_2)});
        if (current.ask_price_3 > 0 && current.ask_volume_3 != 0)
            asks.push_back({(int32_t)current.ask_price_3, std::abs(current.ask_volume_3)});

        if (current.bid_price_1 > 0 && current.bid_volume_1 != 0)
            bids.push_back({(int32_t)current.bid_price_1, std::abs(current.bid_volume_1)});
        if (current.bid_price_2 > 0 && current.bid_volume_2 != 0)
            bids.push_back({(int32_t)current.bid_price_2, std::abs(current.bid_volume_2)});
        if (current.bid_price_3 > 0 && current.bid_volume_3 != 0)
            bids.push_back({(int32_t)current.bid_price_3, std::abs(current.bid_volume_3)});

        // Sort asks ascending, bids descending
        std::sort(asks.begin(), asks.end(), [](auto& a, auto& b){ return a.price < b.price; });
        std::sort(bids.begin(), bids.end(), [](auto& a, auto& b){ return a.price > b.price; });

        for (const auto& order : orders) {
            if (order.is_buy()) {
                // ── BUY: match against asks ──
                int remaining = order.quantity;
                int max_buy = position_limit - position;
                if (max_buy <= 0) continue;
                remaining = std::min(remaining, max_buy);

                for (auto& ask : asks) {
                    if (remaining <= 0) break;
                    if (order.price < ask.price) break; // our bid is below this ask
                    if (ask.volume <= 0) continue;

                    int fill_qty = std::min(remaining, ask.volume);
                    fills.push_back({
                        current.timestamp,
                        ask.price,
                        fill_qty,
                        true // aggressive
                    });
                    position += fill_qty;
                    cash -= (double)ask.price * fill_qty;
                    ask.volume -= fill_qty;
                    remaining -= fill_qty;

                    result.total_buys++;
                    result.total_volume += fill_qty;
                }
            }
            else if (order.is_sell()) {
                // ── SELL: match against bids ──
                int remaining = std::abs(order.quantity);
                int max_sell = position_limit + position;
                if (max_sell <= 0) continue;
                remaining = std::min(remaining, max_sell);

                for (auto& bid : bids) {
                    if (remaining <= 0) break;
                    if (order.price > bid.price) break; // our ask is above this bid
                    if (bid.volume <= 0) continue;

                    int fill_qty = std::min(remaining, bid.volume);
                    fills.push_back({
                        current.timestamp,
                        bid.price,
                        -fill_qty,
                        true
                    });
                    position -= fill_qty;
                    cash += (double)bid.price * fill_qty;
                    bid.volume -= fill_qty;
                    remaining -= fill_qty;

                    result.total_sells++;
                    result.total_volume += fill_qty;
                }
            }
        }

        // Record PnL snapshot
        double mtm = cash + position * last_mid;
        result.pnl_history.push_back({
            current.timestamp,
            position,
            cash,
            last_mid,
            mtm
        });
        result.update_drawdown(mtm);
        result.total_pnl = mtm;
        result.final_position = position;
        result.fills.insert(result.fills.end(), fills.begin(), fills.end());

        return fills;
    }

    // Convenience: single market buy at best ask
    Fill market_buy(int qty) {
        std::vector<StrategyOrder> orders = {{(int32_t)current.ask_price_1, qty}};
        auto fills = match_orders(orders);
        if (!fills.empty()) return fills[0];
        return {current.timestamp, 0, 0, false};
    }

    // Convenience: single market sell at best bid
    Fill market_sell(int qty) {
        std::vector<StrategyOrder> orders = {{(int32_t)current.bid_price_1, -qty}};
        auto fills = match_orders(orders);
        if (!fills.empty()) return fills[0];
        return {current.timestamp, 0, 0, false};
    }

private:
    OrderBookState current{};
};