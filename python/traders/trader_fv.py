from datamodel import OrderDepth, TradingState, Order
from typing import Dict, List, Tuple


class Trader:
    THRESHOLD = 0.50
    ORDER_SIZE = 10
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
            mid = (best_bid + best_ask) / 2.0

            bv = od.buy_orders[best_bid]
            av = abs(od.sell_orders[best_ask])
            fair = (best_bid * av + best_ask * bv) / (bv + av) if bv + av > 0 else mid

            if fair - mid > self.THRESHOLD and pos < self.LIMIT:
                qty = min(self.ORDER_SIZE, self.LIMIT - pos)
                if qty > 0:
                    orders.append(Order(product, best_ask, qty))
            elif mid - fair > self.THRESHOLD and pos > -self.LIMIT:
                qty = min(self.ORDER_SIZE, self.LIMIT + pos)
                if qty > 0:
                    orders.append(Order(product, best_bid, -qty))

            result[product] = orders

        return result, 0, ""