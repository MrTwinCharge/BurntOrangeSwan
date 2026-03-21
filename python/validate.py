#!/usr/bin/env python3
"""
validate.py — Run trader.py against historical data and compare to C++ results.

Usage:
  python validate.py                          # auto-finds data and trader.py
  python validate.py --trader trader_mm.py    # specify trader file
  python validate.py --prices path.csv        # specify price CSV

Compares output PnL to ../results/pnl.csv from C++ backtester.
"""

import csv
import sys
import os
import importlib.util
from collections import defaultdict

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)

from datamodel import Listing, OrderDepth, Trade, Order, TradingState, Observation


def load_csv(path):
    with open(path, "r") as f:
        sample = f.read(4096)
        f.seek(0)
        delim = ";" if sample.count(";") > sample.count(",") else ","
        return list(csv.DictReader(f, delimiter=delim))


def safe_int(v):
    if v is None:
        return None
    v = str(v).strip()
    if v in ("", "nan", "NaN"):
        return None
    try:
        return int(round(float(v)))
    except (ValueError, TypeError):
        return None


def safe_float(v):
    if v is None:
        return None
    v = str(v).strip()
    if v in ("", "nan", "NaN"):
        return None
    try:
        return float(v)
    except (ValueError, TypeError):
        return None


POSITION_LIMITS = {
    "RAINFOREST_RESIN": 50, "KELP": 50, "SQUID_INK": 50,
    "CROISSANTS": 250, "JAMS": 350, "DJEMBES": 60,
    "PICNIC_BASKET1": 60, "PICNIC_BASKET2": 100,
    "VOLCANIC_ROCK": 400, "MAGNIFICENT_MACARONS": 75,
}


def run_backtest(trader, price_rows, trade_rows):
    """Run the trader against historical data. Returns per-product PnL."""

    # Group by timestamp
    prices_by_ts = defaultdict(list)
    for r in price_rows:
        ts = int(r.get("timestamp", 0))
        prices_by_ts[ts].append(r)

    trades_by_ts = defaultdict(list)
    for r in trade_rows:
        ts = int(r.get("timestamp", 0))
        trades_by_ts[ts].append(r)

    timestamps = sorted(prices_by_ts.keys())
    products = sorted(set(r.get("product", "").strip() for r in price_rows if r.get("product", "").strip()))

    positions = {p: 0 for p in products}
    cash = {p: 0.0 for p in products}
    last_mid = {p: 0.0 for p in products}
    trader_data = ""
    prev_own_trades = {p: [] for p in products}
    listings = {p: Listing(p, p, "SEASHELLS") for p in products}
    total_fills = 0

    for ts in timestamps:
        # Build order depths
        order_depths = {}
        for p in products:
            od = OrderDepth()
            for r in prices_by_ts[ts]:
                if r.get("product", "").strip() != p:
                    continue
                for i in range(1, 4):
                    bp = safe_int(r.get(f"bid_price_{i}"))
                    bv = safe_int(r.get(f"bid_volume_{i}"))
                    if bp is not None and bv is not None and bv != 0:
                        od.buy_orders[bp] = od.buy_orders.get(bp, 0) + abs(bv)
                    ap = safe_int(r.get(f"ask_price_{i}"))
                    av = safe_int(r.get(f"ask_volume_{i}"))
                    if ap is not None and av is not None and av != 0:
                        od.sell_orders[ap] = od.sell_orders.get(ap, 0) - abs(av)
                mid = safe_float(r.get("mid_price"))
                if mid is not None:
                    last_mid[p] = mid
            order_depths[p] = od

        # Build market trades
        market_trades = {p: [] for p in products}
        for r in trades_by_ts[ts]:
            sym = (r.get("symbol") or r.get("product", "")).strip()
            if sym in products:
                price = safe_int(r.get("price"))
                qty = safe_int(r.get("quantity"))
                if price and qty:
                    market_trades[sym].append(Trade(sym, price, qty,
                                                     r.get("buyer", ""), r.get("seller", ""), ts))

        state = TradingState(
            traderData=trader_data,
            timestamp=ts,
            listings=listings,
            order_depths=order_depths,
            own_trades=prev_own_trades,
            market_trades=market_trades,
            position=dict(positions),
            observations=Observation(),
        )

        # Call trader
        try:
            ret = trader.run(state)
            if isinstance(ret, tuple):
                orders_dict = ret[0]
                trader_data = ret[2] if len(ret) >= 3 else ""
            else:
                orders_dict = ret
                trader_data = ""
        except Exception as e:
            print(f"  [ERR] t={ts}: {e}")
            orders_dict = {}

        if not isinstance(trader_data, str):
            trader_data = str(trader_data) if trader_data else ""

        # Match orders
        prev_own_trades = {p: [] for p in products}
        for p in products:
            p_orders = orders_dict.get(p, [])
            if not p_orders:
                continue

            limit = POSITION_LIMITS.get(p, 50)
            pos = positions[p]

            # Copy order depth for matching
            od = order_depths[p]
            asks = dict(od.sell_orders)
            bids = dict(od.buy_orders)

            for order in p_orders:
                if order.quantity > 0:
                    remaining = min(order.quantity, limit - pos)
                    for ask_p in sorted(asks.keys()):
                        if remaining <= 0 or order.price < ask_p:
                            break
                        fill = min(remaining, abs(asks[ask_p]))
                        cash[p] -= ask_p * fill
                        pos += fill
                        remaining -= fill
                        total_fills += 1
                        asks[ask_p] += fill
                        if asks[ask_p] >= 0:
                            del asks[ask_p]

                elif order.quantity < 0:
                    remaining = min(abs(order.quantity), limit + pos)
                    for bid_p in sorted(bids.keys(), reverse=True):
                        if remaining <= 0 or order.price > bid_p:
                            break
                        fill = min(remaining, bids[bid_p])
                        cash[p] += bid_p * fill
                        pos -= fill
                        remaining -= fill
                        total_fills += 1
                        bids[bid_p] -= fill
                        if bids[bid_p] <= 0:
                            del bids[bid_p]

            positions[p] = pos

    # Final PnL
    final_pnl = {}
    for p in products:
        final_pnl[p] = cash[p] + positions[p] * last_mid[p]

    return final_pnl, total_fills, positions


def load_cpp_pnl(path):
    """Load C++ pnl.csv final row."""
    rows = load_csv(path)
    if not rows:
        return {}
    last = rows[-1]
    pnl = {}
    for k, v in last.items():
        if k.endswith("_pnl") and k != "total_pnl":
            product = k.replace("_pnl", "")
            pnl[product] = float(v)
    return pnl


def main():
    trader_path = os.path.join(SCRIPT_DIR, "trader.py")
    prices_path = None
    trades_path = None

    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--trader" and i + 1 < len(args):
            trader_path = args[i + 1]
            i += 2
        elif args[i] == "--prices" and i + 1 < len(args):
            prices_path = args[i + 1]
            i += 2
        elif args[i] == "--trades" and i + 1 < len(args):
            trades_path = args[i + 1]
            i += 2
        else:
            i += 1

    # Auto-find data
    raw_dir = os.path.join(SCRIPT_DIR, "..", "data", "raw")
    if prices_path is None:
        for f in sorted(os.listdir(raw_dir)):
            if f.startswith("prices_") and f.endswith(".csv"):
                prices_path = os.path.join(raw_dir, f)
                break
    if trades_path is None:
        for f in sorted(os.listdir(raw_dir)):
            if f.startswith("trades_") and f.endswith(".csv"):
                trades_path = os.path.join(raw_dir, f)
                break

    if not prices_path or not os.path.exists(prices_path):
        print("Error: No price CSV found. Use --prices path.csv")
        sys.exit(1)

    print(f"[validate] Trader: {trader_path}")
    print(f"[validate] Prices: {prices_path}")
    print(f"[validate] Trades: {trades_path or '(none)'}")

    # Load trader
    if not os.path.exists(trader_path):
        print(f"Error: {trader_path} not found. Run generate_trader.py first.")
        sys.exit(1)

    spec = importlib.util.spec_from_file_location("trader_mod", trader_path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules["datamodel"] = importlib.import_module("datamodel")
    spec.loader.exec_module(mod)
    trader = mod.Trader()

    # Load data
    print("[validate] Loading data...")
    price_rows = load_csv(prices_path)
    trade_rows = load_csv(trades_path) if trades_path and os.path.exists(trades_path) else []
    print(f"  {len(price_rows)} price rows, {len(trade_rows)} trade rows")

    # Run backtest
    print("[validate] Running Python backtest...")
    py_pnl, py_fills, py_pos = run_backtest(trader, price_rows, trade_rows)

    # Load C++ results for comparison
    cpp_pnl_path = os.path.join(SCRIPT_DIR, "..", "results", "pnl.csv")
    cpp_pnl = load_cpp_pnl(cpp_pnl_path) if os.path.exists(cpp_pnl_path) else {}

    # Print comparison
    print()
    print("=" * 66)
    print("  VALIDATION RESULTS")
    print("=" * 66)
    print(f"  {'Product':<25} {'Python PnL':>12} {'C++ PnL':>12} {'Delta':>12}")
    print("-" * 66)

    total_py = 0
    total_cpp = 0
    all_match = True

    for p in sorted(set(list(py_pnl.keys()) + list(cpp_pnl.keys()))):
        py = py_pnl.get(p, 0)
        cpp = cpp_pnl.get(p, 0)
        delta = py - cpp
        total_py += py
        total_cpp += cpp

        status = " " if abs(delta) < 1 else "!"
        if abs(delta) >= 1:
            all_match = False

        print(f" {status} {p:<24} {py:>12.2f} {cpp:>12.2f} {delta:>+12.2f}")

    print("-" * 66)
    total_delta = total_py - total_cpp
    print(f"  {'TOTAL':<25} {total_py:>12.2f} {total_cpp:>12.2f} {total_delta:>+12.2f}")
    print(f"  Fills: {py_fills}")
    print(f"  Final positions: {py_pos}")
    print("=" * 66)

    if all_match:
        print("\n  Python matches C++ within tolerance. Safe to submit!")
    elif not cpp_pnl:
        print("\n  No C++ results found for comparison.")
        print("  Run ./sweep or ./backtester first to generate results/pnl.csv")
    else:
        print(f"\n  WARNING: Python differs from C++ by {total_delta:+.2f}")
        print("  Check order matching logic or float rounding.")

    return 0 if all_match else 1


if __name__ == "__main__":
    sys.exit(main())