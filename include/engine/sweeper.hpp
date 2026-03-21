// include/engine/sweeper.hpp
#pragma once
#include "engine/types.hpp"
#include "engine/lob.hpp"
#include "engine/strategy.hpp"
#include <vector>
#include <string>
#include <map>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <functional>

struct SweepParam {
    std::string name;
    double min;
    double max;
    double step;

    int num_steps() const {
        return (int)((max - min) / step) + 1;
    }
};

struct SweepResult {
    std::vector<double> params;
    double total_pnl;
    int total_trades;
    double max_drawdown;
    std::map<std::string, double> per_product_pnl;
};

using StrategyFactory = std::function<Strategy*(const std::vector<double>& params)>;

class ParamSweeper {
public:
    static std::vector<SweepResult> sweep(
        const std::vector<SweepParam>& param_defs,
        const StrategyFactory& factory,
        const std::vector<std::string>& symbols,
        const std::map<std::string, const OrderBookState*>& price_data,
        const std::map<std::string, const PublicTrade*>& trade_data,
        [[maybe_unused]] const std::map<std::string, size_t>& price_counts,
        const std::map<std::string, size_t>& trade_counts,
        size_t total_ticks)
    {
        std::vector<std::vector<double>> combos;
        std::vector<double> current(param_defs.size());
        generate_combos(param_defs, 0, current, combos);

        std::cout << "[Sweeper] " << combos.size() << " param combinations x "
                  << total_ticks << " ticks each" << std::endl;

        auto t0 = std::chrono::high_resolution_clock::now();

        std::vector<SweepResult> results;
        results.reserve(combos.size());

        int done = 0;
        for (const auto& params : combos) {
            Strategy* strat = factory(params);
            std::map<std::string, LimitOrderBook> lobs;
            for (const auto& sym : symbols) {
                lobs[sym] = LimitOrderBook(sym);
            }

            std::map<std::string, size_t> trade_ptr;
            for (const auto& sym : symbols) trade_ptr[sym] = 0;

            for (size_t i = 0; i < total_ticks; ++i) {
                uint32_t ts = price_data.at(symbols[0])[i].timestamp;

                std::map<std::string, OrderBookState> books;
                std::map<std::string, std::vector<PublicTrade>> trades;

                for (const auto& sym : symbols) {
                    books[sym] = price_data.at(sym)[i];
                    
                    // GATHER TRADES BEFORE LOB UPDATE
                    while (trade_ptr[sym] < trade_counts.at(sym) &&
                           trade_data.at(sym) != nullptr &&
                           trade_data.at(sym)[trade_ptr[sym]].timestamp <= ts) {
                        trades[sym].push_back(trade_data.at(sym)[trade_ptr[sym]]);
                        trade_ptr[sym]++;
                    }

                    // UPDATE WITH TRADES TO SIMULATE QUEUE DEPLETION
                    lobs[sym].update(books[sym], trades[sym]);
                }

                strat->on_tick(ts, books, trades, lobs);
            }

            SweepResult sr;
            sr.params = params;
            sr.total_pnl = 0;
            sr.total_trades = 0;
            sr.max_drawdown = 0;

            for (const auto& sym : symbols) {
                sr.per_product_pnl[sym] = lobs[sym].result.total_pnl;
                sr.total_pnl += lobs[sym].result.total_pnl;
                sr.total_trades += lobs[sym].result.total_buys + lobs[sym].result.total_sells;
                sr.max_drawdown += lobs[sym].result.max_drawdown;
            }

            results.push_back(sr);
            delete strat;

            done++;
            if (done % 50 == 0 || done == (int)combos.size()) {
                std::cout << "\r[Sweeper] " << done << "/" << combos.size()
                          << " (" << std::fixed << std::setprecision(1)
                          << (100.0 * done / combos.size()) << "%)" << std::flush;
            }
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        std::cout << "\n[Sweeper] Done in " << std::fixed << std::setprecision(2)
                  << elapsed << "s" << std::endl;

        std::sort(results.begin(), results.end(),
                  [](const auto& a, const auto& b){ return a.total_pnl > b.total_pnl; });

        return results;
    }

    static void export_csv(const std::vector<SweepResult>& results,
                           const std::vector<SweepParam>& param_defs,
                           const std::vector<std::string>& symbols,
                           const std::string& filepath) {
        std::ofstream f(filepath);
        if (!f.is_open()) {
            std::cerr << "[Sweeper] Could not open: " << filepath << std::endl;
            return;
        }

        for (const auto& p : param_defs) f << p.name << ",";
        f << "total_pnl,total_trades,max_drawdown";
        for (const auto& sym : symbols) f << "," << sym << "_pnl";
        f << "\n";

        for (const auto& r : results) {
            for (size_t i = 0; i < r.params.size(); i++) {
                f << std::fixed << std::setprecision(4) << r.params[i] << ",";
            }
            f << std::fixed << std::setprecision(2)
              << r.total_pnl << "," << r.total_trades << "," << r.max_drawdown;
            for (const auto& sym : symbols) {
                auto it = r.per_product_pnl.find(sym);
                f << "," << (it != r.per_product_pnl.end() ? it->second : 0.0);
            }
            f << "\n";
        }

        f.close();
        std::cout << "[Sweeper] Results exported to: " << filepath << std::endl;
    }

    static void print_top(const std::vector<SweepResult>& results,
                          const std::vector<SweepParam>& param_defs,
                          int n = 10) {
        std::cout << "\n[Sweeper] Top " << std::min(n, (int)results.size()) << " results:\n";
        std::cout << "  ";
        for (const auto& p : param_defs) {
            std::cout << std::setw(10) << p.name;
        }
        std::cout << std::setw(14) << "PnL"
                  << std::setw(10) << "Trades"
                  << std::setw(12) << "MaxDD" << "\n";
        std::cout << "  " << std::string(param_defs.size() * 10 + 36, '-') << "\n";

        for (int i = 0; i < std::min(n, (int)results.size()); i++) {
            const auto& r = results[i];
            std::cout << "  ";
            for (double p : r.params) {
                std::cout << std::setw(10) << std::fixed << std::setprecision(3) << p;
            }
            std::cout << std::setw(14) << std::fixed << std::setprecision(2) << r.total_pnl
                      << std::setw(10) << r.total_trades
                      << std::setw(12) << std::fixed << std::setprecision(2) << r.max_drawdown
                      << "\n";
        }
    }

private:
    static void generate_combos(const std::vector<SweepParam>& params,
                                size_t idx,
                                std::vector<double>& current,
                                std::vector<std::vector<double>>& out) {
        if (idx == params.size()) {
            out.push_back(current);
            return;
        }
        for (double v = params[idx].min; v <= params[idx].max + 1e-9; v += params[idx].step) {
            current[idx] = v;
            generate_combos(params, idx + 1, current, out);
        }
    }
};