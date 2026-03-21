# BurntOrangeSwan

High-performance C++ backtesting engine for algorithmic trading research. Automated parameter optimization, strategy comparison, and submission generation in one pipeline.

## Architecture

```
  CSV Data          Binary Blobs         Tick Loop            Results
┌──────────┐      ┌──────────────┐     ┌──────────────┐    ┌────────────┐
│ data/raw/ │─────▶│ data/binary/ │────▶│  Engine Loop  │───▶│ results/   │
│ prices_*  │      │ *_prices.bin │     │  per-product  │    │ pnl_*.csv  │
│ trades_*  │      │ *_trades.bin │     │  LOB + match  │    │ trades_*.csv│
└──────────┘      └──────────────┘     └──────┬───────┘    │ sweep.csv  │
  translator        mmap() loader             │            │ dashboard  │
  auto-discovers     zero-copy read           │            └─────┬──────┘
  ; vs , delim       64-byte aligned          │                  │
                                              ▼                  ▼
                                     ┌─────────────────┐  ┌───────────┐
                                     │ Strategy Layer   │  │ trader.py │
                                     │ omni│mm│mr│sc    │  │ (generated│
                                     │ pluggable via    │  │  from best│
                                     │ Strategy interface│  │  params)  │
                                     └─────────────────┘  └───────────┘
```

The engine has four layers:

**Translator** — Reads all `prices_*.csv` and `trades_*.csv` from `data/raw/`, auto-detects delimiters, parses 3 levels of order book depth, and writes cache-aligned binary blobs to `data/binary/`. Each `OrderBookState` is 64 bytes with L1-L3 bid/ask prices, volumes, and mid price.

**Loader** — Maps binary files directly into virtual memory via `mmap()`. No parsing, no allocation, no copying. 10,000 ticks load in <1ms.

**Engine Loop** — Iterates through ticks chronologically, synchronized across all products. Each tick: updates the `LimitOrderBook` per product, feeds the current `OrderBookState` and `PublicTrade` events to the active strategy, matches returned orders against the book across all 3 levels, enforces per-product position limits, and records fills + mark-to-market PnL.

**Strategy Layer** — Pluggable modules implementing a single `on_tick()` interface. Receive the full order book state and trade events for all products, return orders via `lob.match_orders()`. The engine handles everything else.

## Pipeline: data in → trader out

The full pipeline runs with one command:

```bash
./translator    # step 1: CSV → binary
./sweep         # step 2-6: everything else
```

What `./sweep` does in sequence:

1. **Discovers products** from whatever `*_prices.bin` files exist in `data/binary/`
2. **Sweeps every registered strategy** across its parameter ranges (Cartesian product grid search). 4 strategies × ~100 combos each = ~500 backtests in <10 seconds at 400k ticks/sec
3. **Ranks all strategies globally** by total PnL, prints a leaderboard
4. **Runs a full backtest** for each strategy's best params, exporting `pnl_{name}.csv` and `trades_{name}.csv`
5. **Generates `python/trader.py`** by reading the winning strategy's Python template and injecting the optimal parameters
6. **Opens the dashboard** — a self-contained HTML file with all CSV data embedded, showing PnL curves, order book, fills, spread, OBI, and strategy comparison

## Order matching

The `LimitOrderBook` simulates the Prosperity exchange:

- Buy orders match against asks (ascending price, best first)
- Sell orders match against bids (descending price, best first)
- Fills walk through all 3 depth levels consuming available volume
- Position limits are enforced before each fill
- Cash and mark-to-market PnL are tracked per tick
- All fills are recorded with timestamp, price, quantity, and side

## Strategy interface

```cpp
class Strategy {
public:
    virtual void on_tick(
        uint32_t timestamp,
        const std::map<std::string, OrderBookState>& books,
        const std::map<std::string, std::vector<PublicTrade>>& trades,
        std::map<std::string, LimitOrderBook>& lobs
    ) = 0;
};
```

Strategies receive the full market state and place orders through the LOB. To add a new strategy:

1. Create a header in `include/strategies/` implementing `Strategy`
2. Create a matching Python template in `python/traders/`
3. Register it in `build_registry()` in `scripts/sweep.cpp` with parameter ranges and a factory lambda

The sweep automatically includes it in the next run.

## Current strategies

| Name | Approach |
|------|----------|
| `omni` | Order book imbalance momentum — trades in the direction of volume asymmetry |
| `mm` | Market maker — takes mispriced levels relative to weighted mid, quotes inside the spread, skews to flatten position |
| `mr` | Mean reversion — tracks short/long EMA, fades deviations beyond a z-score threshold |
| `sc` | Spread capture — passive spread harvesting with position-aware sizing |

## Validation

```bash
cd python
python3 validate.py
```

Runs `trader.py` through a Python backtest on the same CSV data the C++ engine used. Prints a side-by-side PnL comparison per product. If the numbers match, the Python port is faithful to the C++ logic.

## Build

```bash
cd build
cmake ..
make -j$(nproc)
```

Requires C++17 and a POSIX system (Linux/WSL) for `mmap()`. Compiled with `-O3` for maximum throughput.