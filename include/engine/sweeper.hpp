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
#include <omp.h> // 🚀 OPTIMIZATION 2: Parallel Sweeping

struct SweepParam { std::string name; double min; double max; double step; };

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
        const std::map<std::string, size_t>& tc_map,
        size_t total_ticks)
    {
        std::vector<std::vector<double>> combos;
        std::vector<double> current(param_defs.size());
        generate_combos(param_defs, 0, current, combos);

        std::cout << "[Sweeper] " << combos.size() << " combinations | Running on " 
                  << omp_get_max_threads() << " CPU threads...\n";

        auto t0 = std::chrono::high_resolution_clock::now();

        std::vector<SweepResult> results;
        int done = 0;

        // Pre-extract flat arrays to completely avoid std::map lookups in parallel threads
        size_t num_symbols = symbols.size();
        std::vector<const OrderBookState*> p_ptrs(num_symbols);
        std::vector<const PublicTrade*> t_ptrs(num_symbols);
        std::vector<size_t> t_counts(num_symbols);
        for(size_t i = 0; i < num_symbols; ++i) {
            p_ptrs[i] = price_data.at(symbols[i]);
            t_ptrs[i] = trade_data.at(symbols[i]);
            t_counts[i] = tc_map.at(symbols[i]);
        }

        // 🚀 Parallel loop distributing parameter combos across CPU cores
        #pragma omp parallel for schedule(dynamic)
        for (size_t c = 0; c < combos.size(); ++c) {
            const auto& params = combos[c];
            Strategy* strat = factory(params);
            strat->init(symbols);

            // Thread-local memory allocations
            std::vector<LimitOrderBook> lobs;
            std::vector<size_t> trade_ptr(num_symbols, 0);
            std::vector<OrderBookState> books(num_symbols);
            std::vector<std::vector<PublicTrade>> trades(num_symbols);

            for (size_t i = 0; i < num_symbols; ++i) {
                lobs.push_back(LimitOrderBook(symbols[i]));
                trades[i].reserve(20);
            }

            for (size_t i = 0; i < total_ticks; ++i) {
                uint32_t ts = p_ptrs[0][i].timestamp;

                for (size_t s = 0; s < num_symbols; ++s) {
                    books[s] = p_ptrs[s][i];
                    trades[s].clear(); 
                    
                    while (trade_ptr[s] < t_counts[s] &&
                           t_ptrs[s] != nullptr &&
                           t_ptrs[s][trade_ptr[s]].timestamp <= ts) {
                        trades[s].push_back(t_ptrs[s][trade_ptr[s]]);
                        trade_ptr[s]++;
                    }

                    lobs[s].update(books[s], trades[s]);
                }
                strat->on_tick(ts, books, trades, lobs);
            }

            SweepResult sr;
            sr.params = params;
            sr.total_pnl = 0;
            sr.total_trades = 0;
            sr.max_drawdown = 0;

            for (size_t s = 0; s < num_symbols; ++s) {
                sr.per_product_pnl[symbols[s]] = lobs[s].result.total_pnl;
                sr.total_pnl += lobs[s].result.total_pnl;
                sr.total_trades += lobs[s].result.total_buys + lobs[s].result.total_sells;
                sr.max_drawdown += lobs[s].result.max_drawdown;
            }

            // Lock critical section to safely write output
            #pragma omp critical
            {
                results.push_back(sr);
                done++;
                if (done % 10 == 0 || done == (int)combos.size()) {
                    std::cout << "\r[Sweeper] " << done << "/" << combos.size()
                              << " (" << std::fixed << std::setprecision(1)
                              << (100.0 * done / combos.size()) << "%)" << std::flush;
                }
            }
            delete strat;
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        std::cout << "\n[Sweeper] Done in " << std::fixed << std::setprecision(2)
                  << elapsed << "s" << std::endl;

        std::sort(results.begin(), results.end(),
                  [](const auto& a, const auto& b){ return a.total_pnl > b.total_pnl; });

        return results;
    }

    static void print_top(const std::vector<SweepResult>& results, [[maybe_unused]] const std::vector<SweepParam>& param_defs, int n = 10) {
        std::cout << "\n[Sweeper] Top " << std::min(n, (int)results.size()) << " results:\n";
        for (int i = 0; i < std::min(n, (int)results.size()); i++) {
            const auto& r = results[i];
            std::cout << "  ";
            for (double p : r.params) std::cout << std::setw(10) << std::fixed << std::setprecision(3) << p;
            std::cout << " | PnL: " << std::setw(8) << std::fixed << std::setprecision(2) << r.total_pnl << "\n";
        }
    }

private:
    static void generate_combos(const std::vector<SweepParam>& params, size_t idx, std::vector<double>& current, std::vector<std::vector<double>>& out) {
        if (idx == params.size()) { out.push_back(current); return; }
        for (double v = params[idx].min; v <= params[idx].max + 1e-9; v += params[idx].step) {
            current[idx] = v; generate_combos(params, idx + 1, current, out);
        }
    }
};