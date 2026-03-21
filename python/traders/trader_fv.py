from datamodel import OrderDepth, TradingState, Order
from typing import Dict, List, Tuple


class Trader:
    FAIR_VALUES = {"EMERALDS": 10000}
    PASSIVE_OFFSET = 1
    MAX_POS_FRAC = 0.80
    ORDER_SIZE = 20
    LIMIT = 50

    def run(self, state: TradingState) -> Tuple[Dict[str, List[Order]], int, str]:
        result: Dict[str, List[Order]] = {}

        for product, od in state.order_depths.items():
            orders: List[Order] = []
            pos = state.position.get(product, 0)
            max_pos = int(self.LIMIT * self.MAX_POS_FRAC)

            if not od.buy_orders or not od.sell_orders:
                result[product] = orders
                continue

            best_bid = max(od.buy_orders.keys())
            best_ask = min(od.sell_orders.keys())
            fair = self.FAIR_VALUES.get(product, (best_bid + best_ask) / 2.0)

            for ask_p in sorted(od.sell_orders.keys()):
                if ask_p < fair and pos < max_pos:
                    qty = min(abs(od.sell_orders[ask_p]), self.ORDER_SIZE, max_pos - pos)
                    if qty > 0:
                        orders.append(Order(product, ask_p, qty))
                        pos += qty

            for bid_p in sorted(od.buy_orders.keys(), reverse=True):
                if bid_p > fair and pos > -max_pos:
                    qty = min(od.buy_orders[bid_p], self.ORDER_SIZE, max_pos + pos)
                    if qty > 0:
                        orders.append(Order(product, bid_p, -qty))
                        pos -= qty

            bid_q = int(fair) - self.PASSIVE_OFFSET
            ask_q = int(fair) + self.PASSIVE_OFFSET
            if pos > max_pos // 2:
                ask_q -= 1; bid_q -= 1
            elif pos < -max_pos // 2:
                bid_q += 1; ask_q += 1

            if bid_q >= best_bid and pos < max_pos:
                qty = min(self.ORDER_SIZE, max_pos - pos)
                if qty > 0: orders.append(Order(product, bid_q, qty))
            if ask_q <= best_ask and pos > -max_pos:
                qty = min(self.ORDER_SIZE, max_pos + pos)
                if qty > 0: orders.append(Order(product, ask_q, -qty))

            result[product] = orders

        return result, 0, ""