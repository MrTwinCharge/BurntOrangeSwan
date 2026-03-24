#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <chrono>
#include <filesystem>
#include <algorithm>
#include "engine/loader.hpp"
#include "engine/lob.hpp"
#include "engine/results.hpp"
#include "strategies/omni_strategy.hpp"
#include "strategies/market_maker.hpp"
#include "strategies/mean_reversion.hpp"
#include "strategies/spread_capture.hpp"
#include "strategies/universal_strat.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    std::string strategy_name = "universal";
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") { /* ignored */ }
        else strategy_name = arg;
    }

    std::string bin_dir = "../data/binary/";
    std::vector<std::string> symbols;
    for (const auto& entry : fs::directory_iterator(bin_dir)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string(), sfx = "_prices.bin";
        if (name.size() > sfx.size() && name.substr(name.size() - sfx.size()) == sfx)
            symbols.push_back(name.substr(0, name.size() - sfx.size()));
    }
    std::sort(symbols.begin(), symbols.end());

    if (symbols.empty()) {
        std::cerr << "[Error] No *_prices.bin files found in " << bin_dir << std::endl;
        return 1;
    }

    std::cout << "[Engine] Found " << symbols.size() << " symbols: ";
    for (const auto& s : symbols) std::cout << s << " ";
    std::cout << std::endl;

    std::map<std::string, const OrderBookState*> price_data;
    std::map<std::string, const PublicTrade*> trade_data;
    std::map<std::string, size_t> price_counts, trade_counts;

    for (const auto& sym : symbols) {
        size_t pc = 0, tc = 0;
        price_data[sym] = load_price_data(bin_dir + sym + "_prices.bin", pc);
        trade_data[sym] = load_trade_data(bin_dir + sym + "_trades.bin", tc);
        price_counts[sym] = pc; trade_counts[sym] = tc;
        std::cout << "[Data] " << sym << ": " << pc << " prices, " << tc << " trades" << std::endl;
    }

    size_t total_ticks = SIZE_MAX;
    for (const auto& sym : symbols) total_ticks = std::min(total_ticks, price_counts[sym]);
    std::cout << "[Engine] Total ticks: " << total_ticks << std::endl;

    // ── Strategy Selection ──
    Strategy* strategy = nullptr;
    if (strategy_name == "mm") {
        std::cout << "[Strategy] Market Maker" << std::endl;
        strategy = new MarketMaker();
    } else if (strategy_name == "obi" || strategy_name == "omni") {
        std::cout << "[Strategy] OBI Momentum" << std::endl;
        strategy = new OmniImbalance();
    } else if (strategy_name == "mr") {
        std::cout << "[Strategy] Mean Reversion" << std::endl;
        strategy = new MeanReversion();
    } else if (strategy_name == "sc") {
        std::cout << "[Strategy] Spread Capture" << std::endl;
        strategy = new SpreadCapture();
    } else {
        std::cout << "[Strategy] Universal Hybrid" << std::endl;
        strategy = new UniversalStrategy();
    }

    strategy->init(symbols);
    // CRITICAL FIX: Set total_ticks so strategies know when to flatten
    strategy->total_ticks = (int)total_ticks;

    size_t num_symbols = symbols.size();
    std::vector<LimitOrderBook> lobs;
    std::vector<size_t> trade_ptr(num_symbols, 0);
    std::vector<OrderBookState> books(num_symbols);
    std::vector<std::vector<PublicTrade>> trades(num_symbols);
    
    std::vector<const OrderBookState*> p_ptrs(num_symbols);
    std::vector<const PublicTrade*> t_ptrs(num_symbols);

    for (size_t i = 0; i < num_symbols; ++i) {
        lobs.push_back(LimitOrderBook(symbols[i]));
        trades[i].reserve(20);
        p_ptrs[i] = price_data[symbols[i]];
        t_ptrs[i] = trade_data[symbols[i]];
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < total_ticks; ++i) {
        uint32_t ts = p_ptrs[0][i].timestamp;
        for (size_t s = 0; s < num_symbols; ++s) {
            books[s] = p_ptrs[s][i];
            trades[s].clear();
            while (trade_ptr[s] < trade_counts[symbols[s]] && t_ptrs[s] && t_ptrs[s][trade_ptr[s]].timestamp <= ts) {
                trades[s].push_back(t_ptrs[s][trade_ptr[s]]);
                trade_ptr[s]++;
            }
            lobs[s].update(books[s], trades[s]);
        }
        strategy->on_tick(ts, books, trades, lobs);
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    
    // Map conversion for exporters
    std::map<std::string, LimitOrderBook> out_lobs;
    for (size_t s = 0; s < num_symbols; ++s) out_lobs[symbols[s]] = lobs[s];

    results::print_summary(out_lobs, std::chrono::duration<double, std::milli>(t1 - t0).count(), total_ticks);
    results::export_pnl_csv(out_lobs, "../results/pnl.csv");
    results::export_trades_csv(out_lobs, "../results/trades.csv");

    delete strategy;
    return 0;
}