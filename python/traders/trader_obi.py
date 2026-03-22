from datamodel import OrderDepth, TradingState, Order
from typing import Dict, List, Tuple


class Trader:
    THRESHOLD = 0.20
    ORDER_SIZE = 2
    LIMIT = 50

    def run(self, state: TradingState) -> Tuple[Dict[str, List[Order]], int, str]:
        result: Dict[str, List[Order]] = {}

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

            imbalance = (bid_vol - ask_vol) / total

            if imbalance > self.THRESHOLD and pos < self.LIMIT:
                qty = min(self.ORDER_SIZE, self.LIMIT - pos)
                if qty > 0:
                    orders.append(Order(product, best_ask, qty))
            elif imbalance < -self.THRESHOLD and pos > -self.LIMIT:
                qty = min(self.ORDER_SIZE, self.LIMIT + pos)
                if qty > 0:
                    orders.append(Order(product, best_bid, -qty))

            result[product] = orders

        return result, 0, ""