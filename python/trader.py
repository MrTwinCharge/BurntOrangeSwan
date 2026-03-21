from datamodel import OrderDepth, TradingState, Order
from typing import Dict, List, Tuple


class Trader:
    MAX_POS_FRAC = 0.40
    ORDER_SIZE = 10
    MIN_SPREAD = 10
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
            spread = best_ask - best_bid

            if spread < self.MIN_SPREAD:
                result[product] = orders
                continue

            max_pos = int(self.LIMIT * self.MAX_POS_FRAC)
            our_bid = best_bid + 1
            our_ask = best_ask - 1

            if our_ask - our_bid < 2:
                result[product] = orders
                continue

            if pos > max_pos // 2:
                our_ask -= 1
                our_bid -= 1
            elif pos < -max_pos // 2:
                our_ask += 1
                our_bid += 1

            if pos < max_pos:
                qty = min(self.ORDER_SIZE, max_pos - pos)
                if qty > 0:
                    orders.append(Order(product, our_bid, qty))

            if pos > -max_pos:
                qty = min(self.ORDER_SIZE, max_pos + pos)
                if qty > 0:
                    orders.append(Order(product, our_ask, -qty))

            mid = (best_bid + best_ask) / 2.0
            if best_ask < mid and pos < max_pos:
                qty = min(abs(od.sell_orders[best_ask]), max_pos - pos)
                if qty > 0:
                    orders.append(Order(product, best_ask, qty))
            if best_bid > mid and pos > -max_pos:
                qty = min(od.buy_orders[best_bid], max_pos + pos)
                if qty > 0:
                    orders.append(Order(product, best_bid, -qty))

            result[product] = orders

        return result, 0, ""