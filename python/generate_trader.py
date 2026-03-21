#!/usr/bin/env python3
"""
generate_trader.py — Auto-generates a submission-ready trader.py
from your C++ sweep results.

Usage:
  python generate_trader.py                     # uses best from sweep.csv, auto-detects strategy
  python generate_trader.py omni                # force OBI template
  python generate_trader.py mm                  # force MM template
  python generate_trader.py --sweep path.csv    # custom sweep file

Reads: ../results/sweep.csv (or specified path)
Writes: ../python/trader.py (ready to upload to prosperity.imc.com)
"""

import csv
import sys
import os
import re

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
RESULTS_DIR = os.path.join(SCRIPT_DIR, "..", "results")
PYTHON_DIR = SCRIPT_DIR


def load_sweep(path):
    """Load sweep CSV, return rows sorted by total_pnl descending."""
    rows = []
    with open(path, "r") as f:
        # Detect delimiter
        sample = f.read(2048)
        f.seek(0)
        delim = ";" if sample.count(";") > sample.count(",") else ","
        reader = csv.DictReader(f, delimiter=delim)
        for row in reader:
            rows.append(row)
    rows.sort(key=lambda r: float(r.get("total_pnl", 0)), reverse=True)
    return rows


def detect_strategy(sweep_rows):
    """Guess which strategy was swept from column names."""
    if not sweep_rows:
        return "omni"
    cols = set(sweep_rows[0].keys())
    if "threshold" in cols:
        return "omni"
    if "edge" in cols or "spread" in cols:
        return "mm"
    return "omni"


def inject_params_obi(template_path, output_path, params):
    """Read OBI template, replace THRESHOLD and ORDER_SIZE."""
    with open(template_path, "r") as f:
        code = f.read()

    threshold = float(params.get("threshold", 0.20))
    order_size = int(float(params.get("order_size", 2)))

    code = re.sub(r'THRESHOLD\s*=\s*[\d.]+', f'THRESHOLD = {threshold}', code)
    code = re.sub(r'ORDER_SIZE\s*=\s*\d+', f'ORDER_SIZE = {order_size}', code)

    with open(output_path, "w") as f:
        f.write(code)

    print(f"  THRESHOLD = {threshold}")
    print(f"  ORDER_SIZE = {order_size}")


def inject_params_mm(template_path, output_path, params):
    """Read MM template, replace EDGE and ORDER_SIZE."""
    with open(template_path, "r") as f:
        code = f.read()

    edge = int(float(params.get("edge", params.get("spread", 1))))
    order_size = int(float(params.get("order_size", 10)))

    code = re.sub(r'EDGE\s*=\s*\d+', f'EDGE = {edge}', code)
    code = re.sub(r'ORDER_SIZE\s*=\s*\d+', f'ORDER_SIZE = {order_size}', code)

    with open(output_path, "w") as f:
        f.write(code)

    print(f"  EDGE = {edge}")
    print(f"  ORDER_SIZE = {order_size}")


def main():
    sweep_path = os.path.join(RESULTS_DIR, "sweep.csv")
    strategy = None

    # Parse args
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--sweep" and i + 1 < len(args):
            sweep_path = args[i + 1]
            i += 2
        elif args[i] in ("omni", "mm"):
            strategy = args[i]
            i += 1
        else:
            i += 1

    if not os.path.exists(sweep_path):
        print(f"Error: Sweep results not found at {sweep_path}")
        print("Run ./sweep first, or specify --sweep path/to/sweep.csv")
        sys.exit(1)

    print(f"[generate_trader] Loading sweep results: {sweep_path}")
    rows = load_sweep(sweep_path)
    if not rows:
        print("Error: No results in sweep CSV")
        sys.exit(1)

    best = rows[0]
    pnl = float(best.get("total_pnl", 0))
    trades = best.get("total_trades", "?")

    if strategy is None:
        strategy = detect_strategy(rows)

    print(f"[generate_trader] Strategy: {strategy}")
    print(f"[generate_trader] Best params (PnL={pnl:.2f}, trades={trades}):")

    output_path = os.path.join(PYTHON_DIR, "trader.py")

    if strategy == "omni":
        template = os.path.join(PYTHON_DIR, "traders", "trader_obi.py")
        inject_params_obi(template, output_path, best)
    elif strategy == "mm":
        template = os.path.join(PYTHON_DIR, "traders", "trader_mm.py")
        inject_params_mm(template, output_path, best)
    else:
        print(f"Unknown strategy: {strategy}")
        sys.exit(1)

    print(f"\n[generate_trader] Written: {output_path}")
    print(f"[generate_trader] Ready to submit to prosperity.imc.com!")
    print(f"\n  To validate first:  python validate.py")
    print(f"  To submit directly: upload python/trader.py")


if __name__ == "__main__":
    main()