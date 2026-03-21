from datamodel import OrderDepth, TradingState, Order
from typing import Dict, List, Tuple
import jsonpickle
import math


class Trader:
    EMA_ALPHA = 0.10
    Z_THRESHOLD = 1.50
    ORDER_SIZE = 5
    LIMIT = 50

    def __init__(self):
        self.ema = {}
        self.ema_var = {}

    def run(self, state: TradingState) -> Tuple[Dict[str, List[Order]], int, str]:
        result: Dict[str, List[Order]] = {}

        # Restore state
        if state.traderData:
            saved = jsonpickle.decode(state.traderData)
            self.ema = saved.get("ema", {})
            self.ema_var = saved.get("ema_var", {})

        for product, od in state.order_depths.items():
            orders: List[Order] = []
            pos = state.position.get(product, 0)

            if not od.buy_orders or not od.sell_orders:
                result[product] = orders
                continue

            best_bid = max(od.buy_orders.keys())
            best_ask = min(od.sell_orders.keys())
            mid = (best_bid + best_ask) / 2.0

            if product not in self.ema:
                self.ema[product] = mid
                self.ema_var[product] = 0.0
                result[product] = orders
                continue

            prev = self.ema[product]
            self.ema[product] = self.EMA_ALPHA * mid + (1 - self.EMA_ALPHA) * prev
            diff = mid - self.ema[product]
            self.ema_var[product] = self.EMA_ALPHA * diff * diff + (1 - self.EMA_ALPHA) * self.ema_var[product]

            std = math.sqrt(self.ema_var[product])
            if std < 0.5:
                result[product] = orders
                continue

            z = (mid - self.ema[product]) / std

            if z > self.Z_THRESHOLD and pos > -self.LIMIT:
                qty = min(self.ORDER_SIZE, self.LIMIT + pos)
                if qty > 0:
                    orders.append(Order(product, best_bid, -qty))

            elif z < -self.Z_THRESHOLD and pos < self.LIMIT:
                qty = min(self.ORDER_SIZE, self.LIMIT - pos)
                if qty > 0:
                    orders.append(Order(product, best_ask, qty))

            if pos > 0 and z < 0:
                qty = min(pos, abs(od.buy_orders.get(best_bid, 0)))
                if qty > 0:
                    orders.append(Order(product, best_bid, -qty))
            elif pos < 0 and z > 0:
                qty = min(-pos, abs(od.sell_orders.get(best_ask, 0)))
                if qty > 0:
                    orders.append(Order(product, best_ask, qty))

            result[product] = orders

        traderData = jsonpickle.encode({"ema": self.ema, "ema_var": self.ema_var})
        return result, 0, traderData