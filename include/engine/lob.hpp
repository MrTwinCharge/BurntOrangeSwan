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
    double cash = 0.0;
    double last_mid = 0.0;

    std::vector<StrategyOrder> resting_orders;
    ProductResult result;

    LimitOrderBook() = default;
    LimitOrderBook(const std::string& sym) 
        : symbol(sym), position_limit(get_position_limit(sym)) {
        result.symbol = sym;
    }

    void process_resting_orders(const OrderBookState& next_state, const std::vector<PublicTrade>& trades) {
        std::vector<Fill> passive_fills;

        for (auto it = resting_orders.begin(); it != resting_orders.end(); ) {
            int fill_qty = 0;

            // 1. Real Queue Depletion (from public trades)
            for (const auto& trade : trades) {
                if (trade.price == it->price) {
                    it->queue_ahead -= (int32_t)trade.quantity;
                    
                    // If queue is cleared, the overflow fills our limit order
                    if (it->queue_ahead < 0) {
                        int available_to_fill = std::abs(it->queue_ahead);
                        int order_qty_remaining = std::abs(it->quantity);
                        
                        int chunk = std::min(available_to_fill, order_qty_remaining);
                        fill_qty += chunk;
                        
                        it->queue_ahead = 0; // Reset so we don't double count
                        if (it->is_buy()) it->quantity -= chunk;
                        else it->quantity += chunk;
                    }
                }
            }

            // 2. Hard spread crossing (Level was swept entirely)
            if (it->is_buy() && next_state.ask_price_1 > 0 && it->price >= (int32_t)next_state.ask_price_1) {
                fill_qty += std::abs(it->quantity);
            } else if (it->is_sell() && next_state.bid_price_1 > 0 && it->price <= (int32_t)next_state.bid_price_1) {
                fill_qty += std::abs(it->quantity);
            }

            // 3. Execute fill
            if (fill_qty > 0) {
                int actual_fill = 0;
                if (it->is_buy()) {
                    actual_fill = std::min(fill_qty, position_limit - position);
                    if (actual_fill > 0) {
                        position += actual_fill;
                        cash -= (double)it->price * actual_fill;
                        passive_fills.push_back({next_state.timestamp, it->price, actual_fill, false});
                        result.total_buys++;
                    }
                } else {
                    actual_fill = std::min(fill_qty, position_limit + position);
                    if (actual_fill > 0) {
                        position -= actual_fill;
                        cash += (double)it->price * actual_fill;
                        passive_fills.push_back({next_state.timestamp, it->price, -actual_fill, false});
                        result.total_sells++;
                    }
                }
                
                result.total_volume += actual_fill;
                
                // Remove from book if fully filled (or if discarded due to pos limit)
                if (std::abs(it->quantity) <= 0 || actual_fill == 0) {
                    it = resting_orders.erase(it);
                    continue;
                }
            }
            ++it;
        }

        if (!passive_fills.empty()) {
            result.fills.insert(result.fills.end(), passive_fills.begin(), passive_fills.end());
        }
    }

    void update(const OrderBookState& state, const std::vector<PublicTrade>& trades) {
        process_resting_orders(state, trades);
        current = state;
        if (state.mid_price_x100 != 0) last_mid = state.mid_price();
    }

    void cancel_all_resting() {
        resting_orders.clear();
    }

    std::vector<Fill> match_orders(const std::vector<StrategyOrder>& orders) {
        std::vector<Fill> fills;

        struct Level { int32_t price; int32_t volume; };
        std::vector<Level> asks, bids;

        if (current.ask_price_1 > 0 && current.ask_volume_1 != 0) asks.push_back({(int32_t)current.ask_price_1, (int32_t)current.ask_volume_1});
        if (current.ask_price_2 > 0 && current.ask_volume_2 != 0) asks.push_back({(int32_t)current.ask_price_2, (int32_t)current.ask_volume_2});
        if (current.ask_price_3 > 0 && current.ask_volume_3 != 0) asks.push_back({(int32_t)current.ask_price_3, (int32_t)current.ask_volume_3});

        if (current.bid_price_1 > 0 && current.bid_volume_1 != 0) bids.push_back({(int32_t)current.bid_price_1, (int32_t)current.bid_volume_1});
        if (current.bid_price_2 > 0 && current.bid_volume_2 != 0) bids.push_back({(int32_t)current.bid_price_2, (int32_t)current.bid_volume_2});
        if (current.bid_price_3 > 0 && current.bid_volume_3 != 0) bids.push_back({(int32_t)current.bid_price_3, (int32_t)current.bid_volume_3});

        std::sort(asks.begin(), asks.end(), [](auto& a, auto& b){ return a.price < b.price; });
        std::sort(bids.begin(), bids.end(), [](auto& a, auto& b){ return a.price > b.price; });

        for (const auto& order : orders) {
            if (order.is_buy()) {
                int remaining = order.quantity;
                int max_buy = position_limit - position;
                if (max_buy <= 0) continue;
                remaining = std::min(remaining, max_buy);

                for (auto& ask : asks) {
                    if (remaining <= 0) break;
                    if (order.price < ask.price) break; 
                    if (ask.volume <= 0) continue;

                    int fill_qty = std::min(remaining, ask.volume);
                    fills.push_back({ current.timestamp, ask.price, fill_qty, true });
                    position += fill_qty;
                    cash -= (double)ask.price * fill_qty;
                    ask.volume -= fill_qty;
                    remaining -= fill_qty;

                    result.total_buys++;
                    result.total_volume += fill_qty;
                }
                
                if (remaining > 0) {
                    int queue = 0;
                    if (order.price == (int32_t)current.bid_price_1) queue = current.bid_volume_1;
                    else if (order.price == (int32_t)current.bid_price_2) queue = current.bid_volume_2;
                    else if (order.price == (int32_t)current.bid_price_3) queue = current.bid_volume_3;
                    resting_orders.push_back({order.price, remaining, queue});
                }
            }
            else if (order.is_sell()) {
                int remaining = std::abs(order.quantity);
                int max_sell = position_limit + position;
                if (max_sell <= 0) continue;
                remaining = std::min(remaining, max_sell);

                for (auto& bid : bids) {
                    if (remaining <= 0) break;
                    if (order.price > bid.price) break; 
                    if (bid.volume <= 0) continue;

                    int fill_qty = std::min(remaining, bid.volume);
                    fills.push_back({ current.timestamp, bid.price, -fill_qty, true });
                    position -= fill_qty;
                    cash += (double)bid.price * fill_qty;
                    bid.volume -= fill_qty;
                    remaining -= fill_qty;

                    result.total_sells++;
                    result.total_volume += fill_qty;
                }
                
                if (remaining > 0) {
                    int queue = 0;
                    if (order.price == (int32_t)current.ask_price_1) queue = current.ask_volume_1;
                    else if (order.price == (int32_t)current.ask_price_2) queue = current.ask_volume_2;
                    else if (order.price == (int32_t)current.ask_price_3) queue = current.ask_volume_3;
                    resting_orders.push_back({order.price, -remaining, queue});
                }
            }
        }

        double liquidation_price = last_mid;
        if (position > 0 && current.bid_price_1 > 0) liquidation_price = current.bid_price_1; 
        else if (position < 0 && current.ask_price_1 > 0) liquidation_price = current.ask_price_1; 

        double mtm = cash + position * liquidation_price;
        
        result.pnl_history.push_back({ current.timestamp, position, cash, liquidation_price, mtm });
        result.update_drawdown(mtm);
        result.total_pnl = mtm;
        result.final_position = position;
        result.fills.insert(result.fills.end(), fills.begin(), fills.end());

        return fills;
    }

private:
    OrderBookState current{};
};