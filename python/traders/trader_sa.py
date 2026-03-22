from datamodel import OrderDepth, UserId, TradingState, Order
from typing import List
import math

class Trader:
    # --- HYPERPARAMETERS (Auto-injected by C++ Engine) ---
    WINDOW_SIZE = 50
    Z_THRESHOLD = 2.0
    ORDER_SIZE = 10
    # -----------------------------------------------------

    def __init__(self):
        self.price_history = []

    def run(self, state: TradingState):
        result = {}
        
        for product in state.order_depths:
            if product != "TOMATOES":
                continue
                
            order_depth: OrderDepth = state.order_depths[product]
            orders: List[Order] = []
            
            if len(order_depth.sell_orders) == 0 or len(order_depth.buy_orders) == 0:
                continue

            best_ask = min(order_depth.sell_orders.keys())
            best_bid = max(order_depth.buy_orders.keys())
            mid_price = (best_ask + best_bid) / 2.0

            self.price_history.append(mid_price)
            if len(self.price_history) > self.WINDOW_SIZE:
                self.price_history.pop(0)

            if len(self.price_history) < self.WINDOW_SIZE:
                result[product] = orders
                continue

            mean = sum(self.price_history) / self.WINDOW_SIZE
            variance = sum([((x - mean) ** 2) for x in self.price_history]) / self.WINDOW_SIZE
            std_dev = math.sqrt(variance)

            if std_dev == 0:
                result[product] = orders
                continue

            z_score = (mid_price - mean) / std_dev
            position = state.position.get(product, 0)
            
            # Statistically Cheap -> Buy
            if z_score < -self.Z_THRESHOLD:
                buy_qty = min(20 - position, self.ORDER_SIZE)
                if buy_qty > 0:
                    orders.append(Order(product, best_ask, buy_qty))
            
            # Statistically Expensive -> Sell
            elif z_score > self.Z_THRESHOLD:
                sell_qty = min(20 + position, self.ORDER_SIZE)
                if sell_qty > 0:
                    orders.append(Order(product, best_bid, -sell_qty))

            result[product] = orders
            
        return result, 1, ""