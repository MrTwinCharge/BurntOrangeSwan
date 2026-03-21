"""
datamodel.py — Prosperity 4 official data model.
DO NOT MODIFY. Must match the competition environment exactly.
"""

import json
from typing import Dict, List

Time = int
Symbol = str
Product = str
Position = int
UserId = str
ObservationValue = int


class Listing:
    def __init__(self, symbol: Symbol, product: Product, denomination: Symbol):
        self.symbol = symbol
        self.product = product
        self.denomination = denomination


class OrderDepth:
    def __init__(self):
        self.buy_orders: Dict[int, int] = {}
        self.sell_orders: Dict[int, int] = {}


class Trade:
    def __init__(self, symbol: Symbol, price: int, quantity: int,
                 buyer: UserId = "", seller: UserId = "", timestamp: int = 0) -> None:
        self.symbol = symbol
        self.price = price
        self.quantity = quantity
        self.buyer = buyer
        self.seller = seller
        self.timestamp = timestamp

    def __str__(self) -> str:
        return f"({self.symbol}) {self.buyer} << {self.seller}: {self.quantity}@{self.price} [{self.timestamp}]"

    def __repr__(self) -> str:
        return self.__str__()


class Order:
    def __init__(self, symbol: Symbol, price: int, quantity: int) -> None:
        self.symbol = symbol
        self.price = price
        self.quantity = quantity

    def __str__(self) -> str:
        return f"({self.symbol}) {'BUY' if self.quantity > 0 else 'SELL'} {abs(self.quantity)}@{self.price}"

    def __repr__(self) -> str:
        return self.__str__()


class ConversionObservation:
    def __init__(self, bidPrice: float = 0, askPrice: float = 0,
                 transportFees: float = 0, exportTariff: float = 0,
                 importTariff: float = 0, sugarPrice: float = 0,
                 sunlightIndex: float = 0):
        self.bidPrice = bidPrice
        self.askPrice = askPrice
        self.transportFees = transportFees
        self.exportTariff = exportTariff
        self.importTariff = importTariff
        self.sugarPrice = sugarPrice
        self.sunlightIndex = sunlightIndex


class Observation:
    def __init__(self, plainValueObservations: Dict[Product, ObservationValue] = None,
                 conversionObservations: Dict[Product, "ConversionObservation"] = None) -> None:
        self.plainValueObservations = plainValueObservations or {}
        self.conversionObservations = conversionObservations or {}


class TradingState:
    def __init__(self, traderData: str, timestamp: Time,
                 listings: Dict[Symbol, Listing],
                 order_depths: Dict[Symbol, OrderDepth],
                 own_trades: Dict[Symbol, List[Trade]],
                 market_trades: Dict[Symbol, List[Trade]],
                 position: Dict[Product, Position],
                 observations: Observation):
        self.traderData = traderData
        self.timestamp = timestamp
        self.listings = listings
        self.order_depths = order_depths
        self.own_trades = own_trades
        self.market_trades = market_trades
        self.position = position
        self.observations = observations

    def toJSON(self):
        return json.dumps(self, default=lambda o: o.__dict__, sort_keys=True)