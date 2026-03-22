from datamodel import OrderDepth, TradingState, Order
from typing import Dict, List, Tuple, Any
import math

class Trader_EMERALDS:
    EDGE = 5
    ORDER_SIZE = 14
    RISK_AVERSION = 0.15
    LIMIT = 50
    TOTAL_TICKS = 2000
    FLATTEN_PCT = 0.90

    def __init__(self):
        self.tick_count = 0

    def run(self, state: TradingState) -> Tuple[Dict[str, List[Order]], int, str]:
        result: Dict[str, List[Order]] = {}
        self.tick_count += 1

        flatten_tick = int(self.TOTAL_TICKS * self.FLATTEN_PCT)
        urgent_tick = int(self.TOTAL_TICKS * 0.975)
        flattening = self.tick_count >= flatten_tick
        urgent = self.tick_count >= urgent_tick

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
            
            # 1. Volume-Weighted Micro-Price
            micro_price = (best_bid * av + best_ask * bv) / (bv + av) if bv + av > 0 else (best_bid + best_ask) / 2.0

            if flattening:
                if pos > 0:
                    qty = min(pos, self.ORDER_SIZE)
                    if urgent:
                        orders.append(Order(product, best_bid, -qty))
                    else:
                        ask_price = max(int(micro_price), best_ask)
                        orders.append(Order(product, ask_price, -qty))
                elif pos < 0:
                    qty = min(-pos, self.ORDER_SIZE)
                    if urgent:
                        orders.append(Order(product, best_ask, qty))
                    else:
                        bid_price = min(int(micro_price), best_bid)
                        orders.append(Order(product, bid_price, qty))
            else:
                # 2. Continuous Inventory Skew
                inventory_skew = pos * self.RISK_AVERSION
                
                # 3. Shift reservation price
                reservation_price = micro_price - inventory_skew

                # 4. Set asymmetric quotes
                bid_price = int(reservation_price) - self.EDGE
                ask_price = int(reservation_price) + self.EDGE
                
                # Ensure we don't cross the spread passively (but allow stepping inside it)
                bid_price = min(bid_price, best_ask - 1)
                ask_price = max(ask_price, best_bid + 1)

                buy_qty = min(self.ORDER_SIZE, self.LIMIT - pos)
                sell_qty = min(self.ORDER_SIZE, self.LIMIT + pos)

                if buy_qty > 0:
                    orders.append(Order(product, bid_price, buy_qty))
                if sell_qty > 0:
                    orders.append(Order(product, ask_price, -sell_qty))

            result[product] = orders

        return result, 0, ""

class Trader_TOMATOES:
    EDGE = 5
    ORDER_SIZE = 14
    RISK_AVERSION = 0.01
    LIMIT = 50
    TOTAL_TICKS = 2000
    FLATTEN_PCT = 0.90

    def __init__(self):
        self.tick_count = 0

    def run(self, state: TradingState) -> Tuple[Dict[str, List[Order]], int, str]:
        result: Dict[str, List[Order]] = {}
        self.tick_count += 1

        flatten_tick = int(self.TOTAL_TICKS * self.FLATTEN_PCT)
        urgent_tick = int(self.TOTAL_TICKS * 0.975)
        flattening = self.tick_count >= flatten_tick
        urgent = self.tick_count >= urgent_tick

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
            
            # 1. Volume-Weighted Micro-Price
            micro_price = (best_bid * av + best_ask * bv) / (bv + av) if bv + av > 0 else (best_bid + best_ask) / 2.0

            if flattening:
                if pos > 0:
                    qty = min(pos, self.ORDER_SIZE)
                    if urgent:
                        orders.append(Order(product, best_bid, -qty))
                    else:
                        ask_price = max(int(micro_price), best_ask)
                        orders.append(Order(product, ask_price, -qty))
                elif pos < 0:
                    qty = min(-pos, self.ORDER_SIZE)
                    if urgent:
                        orders.append(Order(product, best_ask, qty))
                    else:
                        bid_price = min(int(micro_price), best_bid)
                        orders.append(Order(product, bid_price, qty))
            else:
                # 2. Continuous Inventory Skew
                inventory_skew = pos * self.RISK_AVERSION
                
                # 3. Shift reservation price
                reservation_price = micro_price - inventory_skew

                # 4. Set asymmetric quotes
                bid_price = int(reservation_price) - self.EDGE
                ask_price = int(reservation_price) + self.EDGE
                
                # Ensure we don't cross the spread passively (but allow stepping inside it)
                bid_price = min(bid_price, best_ask - 1)
                ask_price = max(ask_price, best_bid + 1)

                buy_qty = min(self.ORDER_SIZE, self.LIMIT - pos)
                sell_qty = min(self.ORDER_SIZE, self.LIMIT + pos)

                if buy_qty > 0:
                    orders.append(Order(product, bid_price, buy_qty))
                if sell_qty > 0:
                    orders.append(Order(product, ask_price, -sell_qty))

            result[product] = orders

        return result, 0, ""

class Trader:
    def __init__(self):
        self.modules = {}
        self.modules['EMERALDS'] = Trader_EMERALDS()
        self.modules['TOMATOES'] = Trader_TOMATOES()

    def run(self, state: TradingState) -> Tuple[Dict[str, List[Order]], int, str]:
        final_result = {}
        
        for product in state.order_depths:
            if product in self.modules:
                # Isolate the state so sub-traders think they are running independently
                mock_state = TradingState(
                    traderData=state.traderData,
                    timestamp=state.timestamp,
                    listings={product: state.listings[product]} if product in state.listings else {},
                    order_depths={product: state.order_depths[product]},
                    own_trades={product: state.own_trades[product]} if product in state.own_trades else {},
                    market_trades={product: state.market_trades[product]} if product in state.market_trades else {},
                    position={product: state.position.get(product, 0)},
                    observations=state.observations
                )
                
                # Execute the specialized execution unit
                res, _, _ = self.modules[product].run(mock_state)
                if product in res:
                    final_result[product] = res[product]
                    
        return final_result, 0, ""
