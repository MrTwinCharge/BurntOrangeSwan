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
    pending_orders = orders;
}

int32_t LimitOrderBook::calculate_queue_ahead(int32_t price, bool is_buy, const OrderBookState& state) const {
    double base_queue = 0;
    if (is_buy) {
        if (price == (int32_t)state.bid_price_1) base_queue = state.bid_volume_1;
        else if (price == (int32_t)state.bid_price_2) base_queue = state.bid_volume_2;
        else if (price == (int32_t)state.bid_price_3) base_queue = state.bid_volume_3;
    } else {
        if (price == (int32_t)state.ask_price_1) base_queue = state.ask_volume_1;
        else if (price == (int32_t)state.ask_price_2) base_queue = state.ask_volume_2;
        else if (price == (int32_t)state.ask_price_3) base_queue = state.ask_volume_3;
    }
    return (int32_t)base_queue; // Removed artificial friction
}

void LimitOrderBook::update(const OrderBookState& state, const std::vector<PublicTrade>& trades) {
    current_state = state;
    uint32_t ts = state.timestamp;

    if (cancel_requested) {
        resting_orders.clear();
        cancel_requested = false;
    }

    if (!pending_orders.empty()) {
        for (auto& order : pending_orders) {
            order.queue_ahead = calculate_queue_ahead(order.price, order.is_buy(), current_state);
            resting_orders.push_back(order);
        }
        pending_orders.clear();
    }

    // 1. Latent Liquidity Fills (Simulating Prosperity Bots)
    for (auto& order : resting_orders) {
        if (order.quantity == 0) continue;
        
        // If we improve the best bid, the game's bots will likely hit us
        if (order.is_buy() && state.bid_price_1 > 0 && order.price > (int32_t)state.bid_price_1 && order.price < (int32_t)state.ask_price_1) {
            int fill_qty = std::min(std::abs(order.quantity), 5); // Conservative latent fill
            process_fills(ts, order.price, fill_qty, false);
            order.quantity -= fill_qty;
        }
        // If we improve the best ask
        else if (order.is_sell() && state.ask_price_1 > 0 && order.price < (int32_t)state.ask_price_1 && order.price > (int32_t)state.bid_price_1) {
            int fill_qty = -std::min(std::abs(order.quantity), 5); 
            process_fills(ts, order.price, fill_qty, false);
            order.quantity += fill_qty; // order.quantity is negative for sells
        }
    }

    // 2. Historical Trade-Through and Queue Fills
    for (const auto& trade : trades) {
        for (auto& order : resting_orders) {
            if (order.quantity == 0) continue;

            bool trade_crosses_bid = order.is_buy() && (trade.price < order.price);
            bool trade_crosses_ask = order.is_sell() && (trade.price > order.price);
            bool trade_touches = (trade.price == order.price);

            // The market crashed completely through our order price -> Guaranteed Fill
            if (trade_crosses_bid || trade_crosses_ask) {
                int fill_qty = std::min(std::abs(order.quantity), trade.quantity);
                if (order.is_sell()) fill_qty = -fill_qty;
                process_fills(ts, order.price, fill_qty, false);
                order.quantity -= fill_qty;
            }
            // The market touched our exact price -> Queue Depletion
            else if (trade_touches) {
                bool trade_was_buy = (trade.price >= (int32_t)current_state.ask_price_1 && current_state.ask_price_1 > 0);
                bool trade_was_sell = (trade.price <= (int32_t)current_state.bid_price_1 && current_state.bid_price_1 > 0);

                if ((order.is_buy() && trade_was_sell) || (order.is_sell() && trade_was_buy) || (!trade_was_buy && !trade_was_sell)) {
                    if (order.queue_ahead > 0) {
                        order.queue_ahead -= trade.quantity;
                        if (order.queue_ahead < 0) {
                            int fill_qty = std::min(std::abs(order.quantity), -order.queue_ahead);
                            if (order.is_sell()) fill_qty = -fill_qty;
                            
                            process_fills(ts, order.price, fill_qty, false);
                            order.quantity -= fill_qty;
                            order.queue_ahead = 0; 
                        }
                    } else {
                        int fill_qty = std::min(std::abs(order.quantity), trade.quantity);
                        if (order.is_sell()) fill_qty = -fill_qty;

                        process_fills(ts, order.price, fill_qty, false);
                        order.quantity -= fill_qty;
                    }
                }
            }
        }
    }

    // 3. Aggressive Crosses (Walking The Book)
    for (auto& order : resting_orders) {
        if (order.quantity == 0) continue;

        if (order.is_buy() && state.ask_price_1 > 0 && order.price >= (int32_t)state.ask_price_1) {
            int max_can_buy = position_limit - position;
            if (max_can_buy <= 0) continue;
            
            int remaining = std::min(order.quantity, max_can_buy);
            
            if (remaining > 0 && state.ask_volume_1 > 0) {
                int fill = std::min(remaining, (int)state.ask_volume_1);
                process_fills(ts, (int32_t)state.ask_price_1, fill, true);
                remaining -= fill; order.quantity -= fill;
            }
            if (remaining > 0 && state.ask_volume_2 > 0 && order.price >= (int32_t)state.ask_price_2) {
                int fill = std::min(remaining, (int)state.ask_volume_2);
                process_fills(ts, (int32_t)state.ask_price_2, fill, true);
                remaining -= fill; order.quantity -= fill;
            }
        }
        else if (order.is_sell() && state.bid_price_1 > 0 && order.price <= (int32_t)state.bid_price_1) {
            int max_can_sell = position_limit + position;
            if (max_can_sell <= 0) continue;
            
            int remaining = std::min(std::abs(order.quantity), max_can_sell);
            
            if (remaining > 0 && state.bid_volume_1 > 0) {
                int fill = std::min(remaining, (int)state.bid_volume_1);
                process_fills(ts, (int32_t)state.bid_price_1, -fill, true);
                remaining -= fill; order.quantity += fill;
            }
            if (remaining > 0 && state.bid_volume_2 > 0 && order.price <= (int32_t)state.bid_price_2) {
                int fill = std::min(remaining, (int)state.bid_volume_2);
                process_fills(ts, (int32_t)state.bid_price_2, -fill, true);
                remaining -= fill; order.quantity += fill;
            }
        }
    }

    resting_orders.erase(
        std::remove_if(resting_orders.begin(), resting_orders.end(), [](const StrategyOrder& o) { return o.quantity == 0; }), 
        resting_orders.end()
    );

    result.total_pnl = result.cash + (position * current_state.mid_price());
    result.update_drawdown(result.total_pnl);

    PnLSnapshot snap;
    snap.timestamp = ts;
    snap.position = position;
    snap.cash = result.cash; 
    snap.mtm_pnl = result.total_pnl; 
    result.pnl_history.push_back(snap);
}

void LimitOrderBook::process_fills(uint32_t timestamp, int32_t price, int32_t quantity, bool aggressive) {
    if (quantity == 0) return;

    Fill f; f.timestamp = timestamp; f.price = price; f.quantity = quantity; f.aggressive = aggressive;
    result.fills.push_back(f);

    if (quantity > 0) { result.total_buys++; result.total_volume += quantity; } 
    else { result.total_sells++; result.total_volume += std::abs(quantity); }

    position += quantity;
    result.cash -= (double)price * quantity; 
    result.final_position = position;
}