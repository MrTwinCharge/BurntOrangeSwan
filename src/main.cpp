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
 
namespace fs = std::filesystem;
 
void print_usage() {
    std::cout << "Usage: ./backtester [strategy] [--verbose]\n"
              << "  strategy: omni (default), mm (market maker)\n"
              << "  --verbose: print individual fills\n"
              << "\nExpects binary data in ../data/binary/\n"
              << "Run ./translator first to convert your CSVs.\n";
}
 
int main(int argc, char* argv[]) {
    // ── Parse args ──
    std::string strategy_name = "omni";
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") {
            // Ignored: Verbose mode was removed to optimize backtesting speed
        }
        else if (arg == "--help" || arg == "-h") { print_usage(); return 0; }
        else strategy_name = arg;
    }
 
    // ── Auto-discover products from binary directory ──
    std::string bin_dir = "../data/binary/";
    if (!fs::exists(bin_dir)) {
        std::cerr << "[Engine] No data/binary/ directory found.\n"
                  << "  Run ./translator first." << std::endl;
        return 1;
    }
 
    std::vector<std::string> symbols;
    for (const auto& entry : fs::directory_iterator(bin_dir)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        std::string suffix = "_prices.bin";
        if (name.size() > suffix.size() &&
            name.substr(name.size() - suffix.size()) == suffix) {
            std::string sym = name.substr(0, name.size() - suffix.size());
            symbols.push_back(sym);
        }
    }
    std::sort(symbols.begin(), symbols.end());
 
    if (symbols.empty()) {
        std::cerr << "[Engine] No *_prices.bin files found in " << bin_dir << "\n"
                  << "  Run ./translator first." << std::endl;
        return 1;
    }
 
    // ── Load data ──
    std::map<std::string, const OrderBookState*> price_data;
    std::map<std::string, const PublicTrade*>     trade_data;
    std::map<std::string, size_t> price_counts;
    std::map<std::string, size_t> trade_counts;
    std::map<std::string, LimitOrderBook> lobs;
 
    for (const auto& sym : symbols) {
        size_t p_count = 0;
        auto* prices = load_price_data(bin_dir + sym + "_prices.bin", p_count);
        if (!prices || p_count == 0) {
            std::cerr << "[Engine] Warning: Could not load prices for " << sym << std::endl;
            continue;
        }
        price_data[sym]   = prices;
        price_counts[sym] = p_count;
        lobs[sym]         = LimitOrderBook(sym);
 
        size_t t_count = 0;
        auto* trades = load_trade_data(bin_dir + sym + "_trades.bin", t_count);
        trade_data[sym]   = trades;
        trade_counts[sym] = t_count;
    }
 
    std::vector<std::string> loaded;
    for (const auto& sym : symbols) {
        if (price_data.count(sym) && price_data[sym]) loaded.push_back(sym);
    }
    symbols = loaded;
 
    if (symbols.empty()) {
        std::cerr << "[Engine] No data loaded." << std::endl;
        return 1;
    }
 
    size_t total_ticks = SIZE_MAX;
    for (const auto& sym : symbols) {
        total_ticks = std::min(total_ticks, price_counts[sym]);
    }
 
    // ── Create strategy ──
    Strategy* strategy = nullptr;
    if (strategy_name == "mm" || strategy_name == "market_maker") {
        auto* mm = new MarketMaker();
        strategy = mm;
        std::cout << "[Engine] Strategy: MarketMaker" << std::endl;
    } else {
        auto* omni = new OmniImbalance();
        strategy = omni;
        std::cout << "[Engine] Strategy: OmniImbalance" << std::endl;
    }
 
    std::cout << "[Engine] Products: ";
    for (const auto& sym : symbols) {
        std::cout << sym << "(" << price_counts[sym] << " ticks, limit="
                  << lobs[sym].position_limit << ") ";
    }
    std::cout << std::endl;
    std::cout << "[Engine] Aligned ticks: " << total_ticks << std::endl;
    std::cout << "[Engine] Running..." << std::endl;
 
    // ── Main loop ────────────────────────────────────────────────────────
    auto t0 = std::chrono::high_resolution_clock::now();
 
    std::map<std::string, size_t> trade_ptr;
    for (const auto& sym : symbols) trade_ptr[sym] = 0;
 
    for (size_t i = 0; i < total_ticks; ++i) {
        uint32_t current_ts = price_data[symbols[0]][i].timestamp;
 
        std::map<std::string, OrderBookState> current_books;
        std::map<std::string, std::vector<PublicTrade>> current_trades;
 
        for (const auto& sym : symbols) {
            current_books[sym] = price_data[sym][i];
 
            // GATHER TRADES FIRST
            while (trade_ptr[sym] < trade_counts[sym] &&
                   trade_data[sym] != nullptr &&
                   trade_data[sym][trade_ptr[sym]].timestamp <= current_ts) {
                current_trades[sym].push_back(trade_data[sym][trade_ptr[sym]]);
                trade_ptr[sym]++;
            }

            // UPDATE LOB WITH TRADES
            lobs[sym].update(current_books[sym], current_trades[sym]);
        }
 
        strategy->on_tick(current_ts, current_books, current_trades, lobs);
    }
 
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
 
    fs::create_directories("../results");
 
    results::print_summary(lobs, elapsed_ms, total_ticks);
    results::export_pnl_csv(lobs, "../results/pnl.csv");
    results::export_trades_csv(lobs, "../results/trades.csv");
 
    delete strategy;
    return 0;
}