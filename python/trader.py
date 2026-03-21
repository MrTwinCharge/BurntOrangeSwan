from datamodel import OrderDepth, TradingState, Order
from typing import Dict, List, Tuple


class Trader:
    EDGE = 0
    ORDER_SIZE = 20
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

            bv = od.buy_orders[best_bid]
            av = abs(od.sell_orders[best_ask])
            fair = (best_bid * av + best_ask * bv) / (bv + av) if bv + av > 0 else (best_bid + best_ask) / 2.0

            # Take mispriced levels
            for ask_price in sorted(od.sell_orders.keys()):
                if fair - ask_price >= self.EDGE and pos < self.LIMIT:
                    qty = min(abs(od.sell_orders[ask_price]), self.ORDER_SIZE, self.LIMIT - pos)
                    if qty > 0:
                        orders.append(Order(product, ask_price, qty))
                        pos += qty

            for bid_price in sorted(od.buy_orders.keys(), reverse=True):
                if bid_price - fair >= self.EDGE and pos > -self.LIMIT:
                    qty = min(od.buy_orders[bid_price], self.ORDER_SIZE, self.LIMIT + pos)
                    if qty > 0:
                        orders.append(Order(product, bid_price, -qty))
                        pos -= qty

            # Passive quotes inside spread
            bid_price = int(fair) - self.EDGE - 1
            ask_price = int(fair) + self.EDGE + 1

            if pos > self.LIMIT // 2:
                ask_price -= 1
            elif pos < -self.LIMIT // 2:
                bid_price += 1

            buy_qty = min(self.ORDER_SIZE, self.LIMIT - pos)
            sell_qty = min(self.ORDER_SIZE, self.LIMIT + pos)

            if buy_qty > 0 and bid_price >= best_bid:
                orders.append(Order(product, bid_price, buy_qty))
            if sell_qty > 0 and ask_price <= best_ask:
                orders.append(Order(product, ask_price, -sell_qty))

            result[product] = orders

        return result, 0, ""