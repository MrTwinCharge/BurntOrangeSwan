from datamodel import OrderDepth, TradingState, Order
from typing import Dict, List, Tuple

class Trader:
    THRESHOLD = 0.20
    ORDER_SIZE = 10
    LIMIT = 50
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

            # Core Omni Logic
            imbalance = (bid_vol - ask_vol) / total
            
            mid = (best_bid + best_ask) / 2.0
            wmid = (best_bid * ask_vol + best_ask * bid_vol) / total
            drift = (wmid - mid) / max(mid, 1.0)

            if (imbalance > self.THRESHOLD or drift > 0.0002) and pos < self.LIMIT:
                qty = min(self.ORDER_SIZE, self.LIMIT - pos)
                if qty > 0:
                    orders.append(Order(product, best_ask, qty))
                    
            elif (imbalance < -self.THRESHOLD or drift < -0.0002) and pos > -self.LIMIT:
                qty = min(self.ORDER_SIZE, self.LIMIT + pos)
                if qty > 0:
                    orders.append(Order(product, best_bid, -qty))

            result[product] = orders

        return result, 0, ""