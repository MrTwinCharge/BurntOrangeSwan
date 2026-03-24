#include "engine/lob.hpp"
#include <cmath>
#include <algorithm>

LimitOrderBook::LimitOrderBook(const std::string& sym) {
    symbol = sym;
    position_limit = get_position_limit(sym);
    result.symbol = sym;
}

void LimitOrderBook::cancel_all_resting() { cancel_requested = true; }
void LimitOrderBook::match_orders(const std::vector<StrategyOrder>& orders) { pending_orders = orders; }

int32_t LimitOrderBook::calculate_queue_ahead(int32_t price, bool is_buy, const OrderBookState& state) const {
    if (is_buy) {
        if (price > (int32_t)state.bid_price_1) return 0;
        if (price == (int32_t)state.bid_price_1) return state.bid_volume_1;
        if (price == (int32_t)state.bid_price_2) return state.bid_volume_1 + state.bid_volume_2;
        return state.bid_volume_1 + state.bid_volume_2 + state.bid_volume_3;
    } else {
        if (price < (int32_t)state.ask_price_1) return 0;
        if (price == (int32_t)state.ask_price_1) return state.ask_volume_1;
        if (price == (int32_t)state.ask_price_2) return state.ask_volume_1 + state.ask_volume_2;
        return state.ask_volume_1 + state.ask_volume_2 + state.ask_volume_3;
    }
}

void LimitOrderBook::update(const OrderBookState& state, const std::vector<PublicTrade>& trades) {
    prev_state = current_state;
    current_state = state;
    uint32_t ts = state.timestamp;

    if (cancel_requested) { resting_orders.clear(); cancel_requested = false; }

    for (auto& o : pending_orders) {
        o.queue_ahead = calculate_queue_ahead(o.price, o.is_buy(), current_state);
        if (o.queue_ahead > 0) {
            o.queue_ahead = (int32_t)(o.queue_ahead * friction_coefficient);
        }
        resting_orders.push_back(o);
    }
    pending_orders.clear();

    // ═══════════════════════════════════════════════════════════
    // 1. TAKER FILLS — Our order crosses the existing spread
    // ═══════════════════════════════════════════════════════════
    for (auto& order : resting_orders) {
        if (order.quantity == 0) continue;
        if (order.is_buy() && state.ask_price_1 > 0 && order.price >= (int32_t)state.ask_price_1) {
            int fill = std::min({std::abs(order.quantity), (int)state.ask_volume_1, position_limit - position});
            if (fill > 0) { process_fills(ts, (int32_t)state.ask_price_1, fill, true); order.quantity -= fill; }
        } 
        else if (order.is_sell() && state.bid_price_1 > 0 && order.price <= (int32_t)state.bid_price_1) {
            int fill = std::min({std::abs(order.quantity), (int)state.bid_volume_1, position_limit + position});
            if (fill > 0) { process_fills(ts, (int32_t)state.bid_price_1, -fill, true); order.quantity += fill; }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // 2. PASSIVE FILLS FROM EXPLICIT TRADE DATA
    // ═══════════════════════════════════════════════════════════
    for (const auto& trade : trades) {
        int32_t trade_price = trade.price;
        int32_t trade_vol = std::abs(trade.quantity);
        
        double mid = current_state.mid_price();
        bool is_buy_aggressor = (trade_price >= mid);
        if (trade.quantity < 0) is_buy_aggressor = false;
        
        for (auto& order : resting_orders) {
            if (order.quantity == 0) continue;

            bool hit = false;
            if (order.is_sell() && is_buy_aggressor && trade_price >= order.price)
                hit = true;
            if (order.is_buy() && !is_buy_aggressor && trade_price <= order.price)
                hit = true;
            
            if (!hit) continue;

            if (order.queue_ahead > 0) {
                if ((order.is_buy() && trade_price < order.price) || 
                    (order.is_sell() && trade_price > order.price))
                    order.queue_ahead = 0;
                else
                    order.queue_ahead -= trade_vol;
            }
            
            if (order.queue_ahead <= 0) {
                int fillable = std::min(trade_vol, std::abs(order.quantity));
                if (order.is_buy())  fillable = std::min(fillable, position_limit - position);
                else                 fillable = std::min(fillable, position_limit + position);
                
                if (fillable > 0) {
                    process_fills(ts, order.price, order.is_buy() ? fillable : -fillable, false);
                    if (order.is_buy()) order.quantity -= fillable; else order.quantity += fillable;
                }
                order.queue_ahead = 0;
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // 3. BOOK-DELTA INFERENCE (when no explicit trade data)
    // ═══════════════════════════════════════════════════════════
    //
    // CRITICAL DISTINCTION:
    //
    //   AT-TOUCH trade (volume drops at L1 price, price unchanged):
    //     A bot sold at bid=9992 or bought at ask=10008.
    //     This does NOT fill our inside-spread order at 9994 or 10006.
    //     It only fills orders AT the touch (queue_ahead > 0).
    //
    //   THROUGH-LEVEL sweep (L1 price shifts toward us):
    //     Ask dropped from 10008 to 10000 → a buyer consumed the ask level.
    //     If our sell is at 10005, that sweep went THROUGH us → we get filled.
    //     Bid rose from 9992 to 9998 → a seller consumed the bid level.
    //     If our buy is at 9995, that sweep went THROUGH us → we get filled.
    //
    // Only THROUGH-LEVEL sweeps fill inside-spread orders.
    // AT-TOUCH trades only fill orders at the touch with queue draining.
    
    if (trades.empty() && prev_state.timestamp > 0) {
        
        // ── BUY-SIDE AGGRESSION ──
        
        // Case A: Volume dropped at same ask price (AT-TOUCH buy)
        // Someone bought AT the ask. Only fills our sells AT that exact price.
        if (current_state.ask_price_1 == prev_state.ask_price_1 && 
            current_state.ask_price_1 > 0 &&
            current_state.ask_volume_1 < prev_state.ask_volume_1) {
            
            int cross_vol = prev_state.ask_volume_1 - current_state.ask_volume_1;
            int32_t cross_price = current_state.ask_price_1;
            
            for (auto& order : resting_orders) {
                if (order.quantity == 0 || !order.is_sell()) continue;
                
                // AT-TOUCH: only fills if our order is AT or BELOW the touch price
                // Inside-spread orders (price < ask) don't get filled by AT-TOUCH trades
                if (order.price == cross_price && order.queue_ahead > 0) {
                    order.queue_ahead -= cross_vol;
                    if (order.queue_ahead <= 0) {
                        int fillable = std::min({-order.queue_ahead, std::abs(order.quantity), position_limit + position});
                        if (fillable > 0) {
                            process_fills(ts, order.price, -fillable, false);
                            order.quantity += fillable;
                        }
                        order.queue_ahead = 0;
                    }
                }
            }
        }
        
        // Case B: Ask price DROPPED (level consumed = SWEEP THROUGH)
        // The entire ask level was consumed, price moved down.
        // This fills inside-spread sells between new_ask and old_ask.
        else if (current_state.ask_price_1 < prev_state.ask_price_1 && 
                 current_state.ask_price_1 > 0 && prev_state.ask_price_1 > 0) {
            
            int sweep_vol = prev_state.ask_volume_1;
            int32_t old_ask = prev_state.ask_price_1;
            int32_t new_ask = current_state.ask_price_1;
            
            // A buyer swept from old_ask down to new_ask (or further).
            // Wait — if ask DROPPED, that means ask got cheaper. That's not a buy sweep.
            // Actually: ask dropping means the old ask was consumed AND replaced by a lower ask.
            // This is ambiguous — could be a cancel, not a trade.
            // CONSERVATIVE: Don't infer fills from ask price drops.
            // Only the explicit volume-at-same-price case is reliable.
        }
        
        // ── SELL-SIDE AGGRESSION ──
        
        // Case A: Volume dropped at same bid price (AT-TOUCH sell)
        if (current_state.bid_price_1 == prev_state.bid_price_1 && 
            current_state.bid_price_1 > 0 &&
            current_state.bid_volume_1 < prev_state.bid_volume_1) {
            
            int cross_vol = prev_state.bid_volume_1 - current_state.bid_volume_1;
            int32_t cross_price = current_state.bid_price_1;
            
            for (auto& order : resting_orders) {
                if (order.quantity == 0 || !order.is_buy()) continue;
                
                // AT-TOUCH: only fills if our order is AT the bid price
                if (order.price == cross_price && order.queue_ahead > 0) {
                    order.queue_ahead -= cross_vol;
                    if (order.queue_ahead <= 0) {
                        int fillable = std::min({-order.queue_ahead, std::abs(order.quantity), position_limit - position});
                        if (fillable > 0) {
                            process_fills(ts, order.price, fillable, false);
                            order.quantity -= fillable;
                        }
                        order.queue_ahead = 0;
                    }
                }
            }
        }
        
        // Case B: Bid price ROSE (level consumed)
        // Conservative: don't infer fills from price shifts.
        
        // ══════════════════════════════════════════════════════
        // Case C: SPREAD COMPRESSION (inside-spread fills)
        // ══════════════════════════════════════════════════════
        // The most important case for inside-spread orders:
        // If the spread compressed such that a NEW price level appeared
        // that crosses our resting order, that's a fill.
        //
        // Example: prev ask=10008, curr ask=10000 (new ask appeared at 10000)
        //   Our sell at 10005 is now BELOW the old ask but ABOVE the new ask.
        //   A bot placed a limit buy at 10000 — that doesn't fill us.
        //   BUT if a bot market-bought and swept through, our sell would fill.
        //
        // We can detect this when: new ask < our sell price AND 
        //   the book shows the new ask has FRESH volume (wasn't there before).
        //   This means someone crossed up through our level.
        //
        // Similarly: new bid > our buy price = someone crossed down through us.
        
        // Ask went UP or a new higher bid appeared that crosses our sells
        if (current_state.bid_price_1 > prev_state.bid_price_1 && 
            current_state.bid_price_1 > 0 && prev_state.bid_price_1 > 0) {
            // New bid is higher than before. If it's at or above our sell price,
            // that's a buyer who crossed through our level.
            int32_t new_bid = current_state.bid_price_1;
            int cross_vol = current_state.bid_volume_1;
            
            for (auto& order : resting_orders) {
                if (order.quantity == 0 || !order.is_sell()) continue;
                if (order.price <= new_bid && order.queue_ahead <= 0) {
                    int fillable = std::min({cross_vol, std::abs(order.quantity), position_limit + position});
                    if (fillable > 0) {
                        process_fills(ts, order.price, -fillable, false);
                        order.quantity += fillable;
                        cross_vol -= fillable;
                    }
                }
            }
        }
        
        // Bid went DOWN or a new lower ask appeared that crosses our buys
        if (current_state.ask_price_1 < prev_state.ask_price_1 && 
            current_state.ask_price_1 > 0 && prev_state.ask_price_1 > 0) {
            int32_t new_ask = current_state.ask_price_1;
            int cross_vol = current_state.ask_volume_1;
            
            for (auto& order : resting_orders) {
                if (order.quantity == 0 || !order.is_buy()) continue;
                if (order.price >= new_ask && order.queue_ahead <= 0) {
                    int fillable = std::min({cross_vol, std::abs(order.quantity), position_limit - position});
                    if (fillable > 0) {
                        process_fills(ts, order.price, fillable, false);
                        order.quantity -= fillable;
                        cross_vol -= fillable;
                    }
                }
            }
        }
    }

    // Clean up
    resting_orders.erase(
        std::remove_if(resting_orders.begin(), resting_orders.end(), 
                       [](const StrategyOrder& o) { return o.quantity == 0; }), 
        resting_orders.end());
    
    result.final_position = position;
    result.total_pnl = result.cash + (position * current_state.mid_price());
    result.update_drawdown(result.total_pnl);
    
    PnLSnapshot snap;
    snap.timestamp = ts;
    snap.position = position;
    snap.cash = result.cash;
    snap.liquidation_price = current_state.mid_price();
    snap.mtm_pnl = result.total_pnl;
    result.pnl_history.push_back(snap);
}

void LimitOrderBook::process_fills(uint32_t timestamp, int32_t price, int32_t quantity, bool aggressive) {
    if (quantity == 0) return;
    Fill f; f.timestamp = timestamp; f.price = price; f.quantity = quantity; f.aggressive = aggressive;
    result.fills.push_back(f);
    result.cash -= (double)price * quantity;
    position += quantity;
    if (quantity > 0) result.total_buys += quantity;
    else result.total_sells += std::abs(quantity);
    result.total_volume += std::abs(quantity);
}