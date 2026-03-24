from datamodel import OrderDepth, TradingState, Order
from typing import Dict, List, Tuple
import math

class Trader:
    # These will be overwritten by the python code generator based on C++ sweep
    THRESHOLD = 0.20
    RISK_AVERSION = 0.05
    MAKER_EDGE = 2
    TAKER_AGGRESSION = 0
    MAX_SPREAD_FADE = 20
    EXIT_BEHAVIOR = 0
    
    ORDER_SIZE = 10
    LIMIT = 20
    TOTAL_TICKS = 2000
    FLATTEN_PCT = 0.90

    def __init__(self):
        self.tick_count = 0

    def run(self, state: TradingState) -> Tuple[Dict[str, List[Order]], int, str]:
        result: Dict[str, List[Order]] = {}
        self.tick_count += 1
        
        flatten_tick = int(self.TOTAL_TICKS * self.FLATTEN_PCT)
        flattening = self.tick_count >= flatten_tick

        for product, od in state.order_depths.items():
            orders: List[Order] = []
            pos = state.position.get(product, 0)

            if not od.buy_orders or not od.sell_orders:
                result[product] = orders
                continue

            best_bid = max(od.buy_orders.keys())
            best_ask = min(od.sell_orders.keys())

            market_spread = best_ask - best_bid

            # Volatility Fade: Hide if the market is too chaotic
            if market_spread > self.MAX_SPREAD_FADE:
                result[product] = orders
                continue

            bid_vol = od.buy_orders[best_bid]
            ask_vol = abs(od.sell_orders[best_ask])
            total = bid_vol + ask_vol
            
            if total == 0:
                result[product] = orders
                continue

            # End of day profit taking / risk-off
            if flattening:
                if pos > 0:
                    qty = min(pos, self.ORDER_SIZE)
                    orders.append(Order(product, best_bid, -qty))
                elif pos < 0:
                    qty = min(-pos, self.ORDER_SIZE)
                    orders.append(Order(product, best_ask, qty))
                result[product] = orders
                continue

            # Metrics
            imbalance = (bid_vol - ask_vol) / total
            micro_price = (best_bid * ask_vol + best_ask * bid_vol) / total
            reservation_price = micro_price - (pos * self.RISK_AVERSION)

            target_bid = 0
            target_ask = 0

            # ==========================================
            # BEHAVIOR 1: DIRECTIONAL MOMENTUM
            # ==========================================
            if abs(imbalance) > self.THRESHOLD:
                if self.TAKER_AGGRESSION == 1:
                    # Taker Mode: Cross the spread
                    if imbalance > 0:
                        target_bid = best_ask
                        target_ask = best_ask + 20
                    else:
                        target_bid = best_bid - 20
                        target_ask = best_bid
                else:
                    # Maker Mode: Penny the spread (Passive Momentum)
                    if imbalance > 0:
                        target_bid = min(best_bid + 1, best_ask - 1)
                        target_ask = best_ask + 20
                    else:
                        target_bid = best_bid - 20
                        target_ask = max(best_ask - 1, best_bid + 1)
            
            # ==========================================
            # BEHAVIOR 2: PASSIVE OR ACTIVE EXIT
            # ==========================================
            else:
                # Active Exit: Dump inventory if momentum dies and we are holding bags
                if self.EXIT_BEHAVIOR == 1 and pos != 0:
                    if pos > 0:
                        # We are long. Dump to best bid immediately.
                        target_bid = best_bid - 20
                        target_ask = best_bid
                    else:
                        # We are short. Cover from best ask immediately.
                        target_bid = best_ask
                        target_ask = best_ask + 20
                
                # Standard Passive Market Making
                else:
                    # Dynamic Edge: Widen quotes if market volatility increases
                    dynamic_edge = max(self.MAKER_EDGE, int(market_spread * 0.4))
                    target_bid = int(round(reservation_price)) - dynamic_edge
                    target_ask = int(round(reservation_price)) + dynamic_edge

                    # Ensure we don't cross the spread passively
                    target_bid = min(target_bid, best_ask - 1)
                    target_ask = max(target_ask, best_bid + 1)

            # Order Execution (Bound by position limits)
            buy_qty = min(self.ORDER_SIZE, self.LIMIT - pos)
            sell_qty = min(self.ORDER_SIZE, self.LIMIT + pos)

            if buy_qty > 0:
                orders.append(Order(product, target_bid, buy_qty))
            if sell_qty > 0:
                orders.append(Order(product, target_ask, -sell_qty))

            result[product] = orders

        return result, 0, ""