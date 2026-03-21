#pragma once
#include "engine/types.hpp"
#include "engine/lob.hpp"
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cmath>

namespace results {

// ── Print summary to stdout ──────────────────────────────────────────────
inline void print_summary(const std::map<std::string, LimitOrderBook>& lobs,
                          double elapsed_ms, size_t total_ticks) {
    std::cout << "\n";
    std::cout << "================================================================\n";
    std::cout << "  BACKTEST RESULTS\n";
    std::cout << "================================================================\n";
    std::cout << "  Ticks processed : " << total_ticks << "\n";
    std::cout << "  Wall-clock time : " << std::fixed << std::setprecision(2) 
              << elapsed_ms << " ms\n";
    if (elapsed_ms > 0) {
        std::cout << "  Throughput      : " << std::fixed << std::setprecision(0)
                  << (total_ticks / (elapsed_ms / 1000.0)) << " ticks/s\n";
    }
    std::cout << "----------------------------------------------------------------\n";
    std::cout << std::left << std::setw(28) << "  Product"
              << std::right << std::setw(8) << "Pos"
              << std::setw(10) << "Buys"
              << std::setw(10) << "Sells"
              << std::setw(14) << "PnL"
              << std::setw(14) << "MaxDD" << "\n";
    std::cout << "----------------------------------------------------------------\n";

    double total_pnl = 0;
    for (const auto& [sym, lob] : lobs) {
        const auto& r = lob.result;
        total_pnl += r.total_pnl;
        std::cout << "  " << std::left << std::setw(26) << sym
                  << std::right << std::setw(8) << r.final_position
                  << std::setw(10) << r.total_buys
                  << std::setw(10) << r.total_sells
                  << std::setw(14) << std::fixed << std::setprecision(2) << r.total_pnl
                  << std::setw(14) << std::fixed << std::setprecision(2) << r.max_drawdown
                  << "\n";
    }

    std::cout << "----------------------------------------------------------------\n";
    std::cout << "  " << std::left << std::setw(26) << "TOTAL"
              << std::right 
              << std::setw(8) << ""
              << std::setw(10) << ""
              << std::setw(10) << ""
              << std::setw(14) << std::fixed << std::setprecision(2) << total_pnl
              << "\n";
    std::cout << "================================================================\n";
}

// ── Export PnL curves to CSV ─────────────────────────────────────────────
inline void export_pnl_csv(const std::map<std::string, LimitOrderBook>& lobs,
                           const std::string& filepath) {
    std::ofstream f(filepath);
    if (!f.is_open()) {
        std::cerr << "[Results] Could not open: " << filepath << std::endl;
        return;
    }

    // Collect all symbols
    std::vector<std::string> symbols;
    for (const auto& [sym, _] : lobs) symbols.push_back(sym);

    // Header
    f << "timestamp";
    for (const auto& sym : symbols) {
        f << "," << sym << "_pnl"
          << "," << sym << "_position"
          << "," << sym << "_cash"
          << "," << sym << "_mid";
    }
    f << ",total_pnl\n";

    // Find max history length
    size_t max_len = 0;
    for (const auto& [_, lob] : lobs)
        max_len = std::max(max_len, lob.result.pnl_history.size());

    // Write rows
    for (size_t i = 0; i < max_len; i++) {
        uint32_t ts = 0;
        double total = 0;

        // Find timestamp from first available product
        for (const auto& sym : symbols) {
            const auto& hist = lobs.at(sym).result.pnl_history;
            if (i < hist.size()) { ts = hist[i].timestamp; break; }
        }
        f << ts;

        for (const auto& sym : symbols) {
            const auto& hist = lobs.at(sym).result.pnl_history;
            if (i < hist.size()) {
                f << "," << std::fixed << std::setprecision(2) << hist[i].mtm_pnl
                  << "," << hist[i].position
                  << "," << std::fixed << std::setprecision(2) << hist[i].cash
                  << "," << std::fixed << std::setprecision(2) << hist[i].mid_price;
                total += hist[i].mtm_pnl;
            } else {
                f << ",,,,";
            }
        }
        f << "," << std::fixed << std::setprecision(2) << total << "\n";
    }

    f.close();
    std::cout << "[Results] PnL exported to: " << filepath << std::endl;
}

// ── Export trade log to CSV ──────────────────────────────────────────────
inline void export_trades_csv(const std::map<std::string, LimitOrderBook>& lobs,
                              const std::string& filepath) {
    std::ofstream f(filepath);
    if (!f.is_open()) {
        std::cerr << "[Results] Could not open: " << filepath << std::endl;
        return;
    }

    f << "timestamp,symbol,side,price,quantity,aggressive\n";

    // Collect all fills, sort by timestamp
    struct TaggedFill {
        std::string symbol;
        Fill fill;
    };
    std::vector<TaggedFill> all_fills;

    for (const auto& [sym, lob] : lobs) {
        for (const auto& fill : lob.result.fills) {
            all_fills.push_back({sym, fill});
        }
    }
    std::sort(all_fills.begin(), all_fills.end(),
              [](const auto& a, const auto& b){ return a.fill.timestamp < b.fill.timestamp; });

    for (const auto& tf : all_fills) {
        f << tf.fill.timestamp
          << "," << tf.symbol
          << "," << (tf.fill.quantity > 0 ? "BUY" : "SELL")
          << "," << tf.fill.price
          << "," << std::abs(tf.fill.quantity)
          << "," << (tf.fill.aggressive ? "true" : "false")
          << "\n";
    }

    f.close();
    std::cout << "[Results] Trades exported to: " << filepath << std::endl;
}

} // namespace results