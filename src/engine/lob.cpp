// src/engine/lob.cpp
#include "engine/lob.hpp"
#include <cmath>
#include <algorithm>

LimitOrderBook::LimitOrderBook(const std::string& sym) {
    symbol = sym;
    position_limit = get_position_limit(sym);
    result.symbol = sym;
}

void LimitOrderBook::cancel_all_resting() {
    cancel_requested = true;
}

void LimitOrderBook::match_orders(const std::vector<StrategyOrder>& orders) {
    // Orders go into pending to simulate 1-tick latency
    pending_orders = orders;
}

int32_t LimitOrderBook::calculate_queue_ahead(int32_t price, bool is_buy, const OrderBookState& state) const {
    // Cast uint32_t prices and volumes to int32_t to match our order structs safely
    if (is_buy) {
        if (price == (int32_t)state.bid_price_1) return (int32_t)state.bid_volume_1;
        if (price == (int32_t)state.bid_price_2) return (int32_t)state.bid_volume_2;
        if (price == (int32_t)state.bid_price_3) return (int32_t)state.bid_volume_3;
    } else {
        if (price == (int32_t)state.ask_price_1) return (int32_t)state.ask_volume_1;
        if (price == (int32_t)state.ask_price_2) return (int32_t)state.ask_volume_2;
        if (price == (int32_t)state.ask_price_3) return (int32_t)state.ask_volume_3;
    }
    // If we are improving the best bid/ask, our queue ahead is 0!
    return 0; 
}

void LimitOrderBook::update(const OrderBookState& state, const std::vector<PublicTrade>& trades) {
    current_state = state;
    uint32_t ts = state.timestamp;

    // 1. Process latency: Pending cancels and new orders from the LAST tick arrive now
    if (cancel_requested) {
        resting_orders.clear();
        cancel_requested = false;
    }

    if (!pending_orders.empty()) {
        for (auto& order : pending_orders) {
            // Calculate how many people beat us to this price level
            order.queue_ahead = calculate_queue_ahead(order.price, order.is_buy(), current_state);
            resting_orders.push_back(order);
        }
        pending_orders.clear();
    }

    // 2. Queue Depletion: Public trades eat through the queue
    for (const auto& trade : trades) {
        for (auto& order : resting_orders) {
            if (order.quantity == 0) continue;

            // If a public trade happened at our exact price level
            if (order.price == trade.price) {
                // Determine if this public trade was a Buy or Sell based on the book (with casts)
                bool trade_was_buy = (trade.price >= (int32_t)current_state.ask_price_1 && current_state.ask_price_1 > 0);
                bool trade_was_sell = (trade.price <= (int32_t)current_state.bid_price_1 && current_state.bid_price_1 > 0);

                // If the trade matches our side, it's eating our queue
                if ((order.is_buy() && trade_was_sell) || (order.is_sell() && trade_was_buy) || (!trade_was_buy && !trade_was_sell)) {
                    if (order.queue_ahead > 0) {
                        order.queue_ahead -= trade.quantity;
                        // Did the trade eat through the queue and partially fill us?
                        if (order.queue_ahead < 0) {
                            int fill_qty = std::min(std::abs(order.quantity), -order.queue_ahead);
                            if (order.is_sell()) fill_qty = -fill_qty;
                            
                            process_fills(ts, order.price, fill_qty, false);
                            order.quantity -= fill_qty;
                            order.queue_ahead = 0; 
                        }
                    } else {
                        // We are at the front of the queue! We get filled.
                        int fill_qty = std::min(std::abs(order.quantity), trade.quantity);
                        if (order.is_sell()) fill_qty = -fill_qty;

                        process_fills(ts, order.price, fill_qty, false);
                        order.quantity -= fill_qty;
                    }
                }
            }
        }
    }

    // 3. Aggressive Crosses: Did the market price move straight through our resting limit orders?
    for (auto& order : resting_orders) {
        if (order.quantity == 0) continue;

        if (order.is_buy() && state.ask_price_1 > 0 && order.price >= (int32_t)state.ask_price_1) {
            int max_can_buy = position_limit - position;
            if (max_can_buy <= 0) continue;
            
            int fill_qty = std::min(order.quantity, max_can_buy);
            fill_qty = std::min(fill_qty, (int)state.ask_volume_1); // Bound by available volume
            
            process_fills(ts, (int32_t)state.ask_price_1, fill_qty, true);
            order.quantity -= fill_qty;
        }
        else if (order.is_sell() && state.bid_price_1 > 0 && order.price <= (int32_t)state.bid_price_1) {
            int max_can_sell = position_limit + position;
            if (max_can_sell <= 0) continue;
            
            int fill_qty = std::min(std::abs(order.quantity), max_can_sell);
            fill_qty = std::min(fill_qty, (int)state.bid_volume_1);
            
            process_fills(ts, (int32_t)state.bid_price_1, -fill_qty, true);
            order.quantity += fill_qty; // Quantity is negative for sells
        }
    }

    // Clean up fully filled orders
    resting_orders.erase(
        std::remove_if(resting_orders.begin(), resting_orders.end(), 
            [](const StrategyOrder& o) { return o.quantity == 0; }), 
        resting_orders.end()
    );

    // 4. True Mark-to-Market PnL Calculation
    result.total_pnl = result.cash + (position * current_state.mid_price());
    result.update_drawdown(result.total_pnl);

    // Update MTM PnL snapshot
    PnLSnapshot snap;
    snap.timestamp = ts;
    snap.position = position;
    snap.cash = result.cash; 
    snap.mtm_pnl = result.total_pnl; 
    result.pnl_history.push_back(snap);
}

void LimitOrderBook::process_fills(uint32_t timestamp, int32_t price, int32_t quantity, bool aggressive) {
    if (quantity == 0) return;

    Fill f;
    f.timestamp = timestamp;
    f.price = price;
    f.quantity = quantity;
    f.aggressive = aggressive;
    result.fills.push_back(f);

    if (quantity > 0) {
        result.total_buys++;
        result.total_volume += quantity;
    } else {
        result.total_sells++;
        result.total_volume += std::abs(quantity);
    }

    // Adjust position
    position += quantity;
    
    // Accurately track raw cash flow
    result.cash -= (double)price * quantity; 
    result.final_position = position;
}