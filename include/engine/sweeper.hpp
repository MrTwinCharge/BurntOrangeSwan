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
#include <random>
#include <cmath>
#include <numeric>

struct SweepParam {
    std::string name;
    double min;
    double max;
    double step;
    int num_steps() const { return (int)((max - min) / step) + 1; }
};

struct SweepResult {
    std::vector<double> params;
    double total_pnl;
    int total_trades;
    double max_drawdown;
    double sharpe;  // risk-adjusted return
    std::map<std::string, double> per_product_pnl;
};

using StrategyFactory = std::function<Strategy*(const std::vector<double>& params)>;

// ═══════════════════════════════════════════════════
// Gaussian Process for Bayesian Optimization
// ═══════════════════════════════════════════════════
class GaussianProcess {
public:
    double length_scale = 1.0;
    double signal_var = 1.0;
    double noise_var = 0.01;

    void fit(const std::vector<std::vector<double>>& X, const std::vector<double>& y) {
        X_ = X;
        y_ = y;
        n_ = X.size();
        if (n_ == 0) return;

        // Normalize y
        y_mean_ = 0;
        for (double v : y) y_mean_ += v;
        y_mean_ /= n_;
        y_norm_.resize(n_);
        for (size_t i = 0; i < n_; i++) y_norm_[i] = y[i] - y_mean_;

        // Build K + noise*I
        K_.assign(n_ * n_, 0);
        for (size_t i = 0; i < n_; i++) {
            for (size_t j = 0; j < n_; j++) {
                K_[i * n_ + j] = kernel(X[i], X[j]);
                if (i == j) K_[i * n_ + j] += noise_var;
            }
        }

        // Solve K * alpha = y_norm via Cholesky
        alpha_ = cholesky_solve(K_, y_norm_, n_);
    }

    // Predict mean and variance at a new point
    void predict(const std::vector<double>& x, double& mean, double& var) const {
        if (n_ == 0) { mean = 0; var = signal_var; return; }

        std::vector<double> k_star(n_);
        for (size_t i = 0; i < n_; i++) k_star[i] = kernel(X_[i], x);

        mean = y_mean_;
        for (size_t i = 0; i < n_; i++) mean += k_star[i] * alpha_[i];

        // var = k(x,x) - k_star^T K^{-1} k_star
        auto v = cholesky_solve(K_, k_star, n_);
        var = kernel(x, x);
        for (size_t i = 0; i < n_; i++) var -= k_star[i] * v[i];
        if (var < 1e-10) var = 1e-10;
    }

private:
    std::vector<std::vector<double>> X_;
    std::vector<double> y_, y_norm_, alpha_;
    std::vector<double> K_;
    double y_mean_ = 0;
    size_t n_ = 0;

    double kernel(const std::vector<double>& a, const std::vector<double>& b) const {
        double dist = 0;
        for (size_t i = 0; i < a.size(); i++) {
            double d = (a[i] - b[i]) / length_scale;
            dist += d * d;
        }
        return signal_var * std::exp(-0.5 * dist);
    }

    // Simple Cholesky solve (A * x = b) for small matrices
    static std::vector<double> cholesky_solve(const std::vector<double>& A,
                                               const std::vector<double>& b, size_t n) {
        // Cholesky decomposition A = L * L^T
        std::vector<double> L(n * n, 0);
        for (size_t i = 0; i < n; i++) {
            for (size_t j = 0; j <= i; j++) {
                double sum = 0;
                for (size_t k = 0; k < j; k++) sum += L[i * n + k] * L[j * n + k];
                if (i == j) {
                    double diag = A[i * n + i] - sum;
                    L[i * n + j] = (diag > 0) ? std::sqrt(diag) : 1e-6;
                } else {
                    L[i * n + j] = (A[i * n + j] - sum) / L[j * n + j];
                }
            }
        }

        // Forward substitution: L * y = b
        std::vector<double> y(n);
        for (size_t i = 0; i < n; i++) {
            double sum = 0;
            for (size_t j = 0; j < i; j++) sum += L[i * n + j] * y[j];
            y[i] = (b[i] - sum) / L[i * n + i];
        }

        // Back substitution: L^T * x = y
        std::vector<double> x(n);
        for (int i = (int)n - 1; i >= 0; i--) {
            double sum = 0;
            for (size_t j = i + 1; j < n; j++) sum += L[j * n + i] * x[j];
            x[i] = (y[i] - sum) / L[i * n + i];
        }
        return x;
    }
};

// Expected Improvement acquisition function
static double expected_improvement(double mean, double var, double best_y) {
    double std = std::sqrt(var);
    if (std < 1e-10) return 0;
    double z = (mean - best_y) / std;
    // Approximation of Phi(z) and phi(z)
    double phi = std::exp(-0.5 * z * z) / std::sqrt(2 * M_PI);
    double Phi = 0.5 * (1 + std::erf(z / std::sqrt(2)));
    return (mean - best_y) * Phi + std * phi;
}

// ═══════════════════════════════════════════════════
// ParamSweeper — Grid + Bayesian
// ═══════════════════════════════════════════════════
class ParamSweeper {
public:

    // Run a single backtest and return result
    static SweepResult evaluate(
        const std::vector<double>& params,
        const StrategyFactory& factory,
        const std::vector<std::string>& symbols,
        const std::map<std::string, const OrderBookState*>& price_data,
        const std::map<std::string, const PublicTrade*>& trade_data,
        const std::map<std::string, size_t>& trade_counts,
        size_t total_ticks)
    {
        size_t n_sym = symbols.size();
        Strategy* strat = factory(params);
        strat->init(symbols);

        std::vector<LimitOrderBook> lobs(n_sym);
        for (size_t s = 0; s < n_sym; s++) lobs[s] = LimitOrderBook(symbols[s]);
        std::vector<size_t> trade_ptr(n_sym, 0);

        // For Sharpe calculation
        std::vector<double> pnl_snapshots;
        double prev_pnl = 0;

        for (size_t i = 0; i < total_ticks; ++i) {
            uint32_t ts = price_data.at(symbols[0])[i].timestamp;
            std::vector<OrderBookState> books(n_sym);
            std::vector<std::vector<PublicTrade>> trades(n_sym);

            for (size_t s = 0; s < n_sym; s++) {
                books[s] = price_data.at(symbols[s])[i];
                while (trade_ptr[s] < trade_counts.at(symbols[s]) &&
                       trade_data.at(symbols[s]) != nullptr &&
                       trade_data.at(symbols[s])[trade_ptr[s]].timestamp <= ts) {
                    trades[s].push_back(trade_data.at(symbols[s])[trade_ptr[s]]);
                    trade_ptr[s]++;
                }
                lobs[s].update(books[s], trades[s]);
            }
            strat->on_tick(ts, books, trades, lobs);

            // Snapshot PnL every 100 ticks for Sharpe
            if (i % 100 == 0) {
                double total = 0;
                for (size_t s = 0; s < n_sym; s++) total += lobs[s].result.total_pnl;
                pnl_snapshots.push_back(total - prev_pnl);
                prev_pnl = total;
            }
        }

        SweepResult sr;
        sr.params = params;
        sr.total_pnl = 0;
        sr.total_trades = 0;
        sr.max_drawdown = 0;
        for (size_t s = 0; s < n_sym; s++) {
            sr.per_product_pnl[symbols[s]] = lobs[s].result.total_pnl;
            sr.total_pnl += lobs[s].result.total_pnl;
            sr.total_trades += lobs[s].result.total_buys + lobs[s].result.total_sells;
            sr.max_drawdown += lobs[s].result.max_drawdown;
        }

        // Compute Sharpe ratio
        if (pnl_snapshots.size() > 1) {
            double mean = std::accumulate(pnl_snapshots.begin(), pnl_snapshots.end(), 0.0) / pnl_snapshots.size();
            double var = 0;
            for (double v : pnl_snapshots) var += (v - mean) * (v - mean);
            var /= pnl_snapshots.size();
            double std = std::sqrt(var);
            sr.sharpe = (std > 1e-10) ? mean / std : 0;
        } else {
            sr.sharpe = 0;
        }

        delete strat;
        return sr;
    }

    // ── GRID SWEEP (original behavior) ──
    static std::vector<SweepResult> sweep(
        const std::vector<SweepParam>& param_defs,
        const StrategyFactory& factory,
        const std::vector<std::string>& symbols,
        const std::map<std::string, const OrderBookState*>& price_data,
        const std::map<std::string, const PublicTrade*>& trade_data,
        const std::map<std::string, size_t>& price_counts,
        const std::map<std::string, size_t>& trade_counts,
        size_t total_ticks)
    {
        (void)price_counts;

        std::vector<std::vector<double>> combos;
        std::vector<double> current(param_defs.size());
        generate_combos(param_defs, 0, current, combos);

        std::cout << "[Sweeper] Grid: " << combos.size() << " combinations x "
                  << total_ticks << " ticks" << std::endl;

        auto t0 = std::chrono::high_resolution_clock::now();
        std::vector<SweepResult> results;
        results.reserve(combos.size());
        int done = 0;

        for (const auto& params : combos) {
            results.push_back(evaluate(params, factory, symbols, price_data, trade_data, trade_counts, total_ticks));
            done++;
            if (done % 50 == 0 || done == (int)combos.size())
                std::cout << "\r[Sweeper] " << done << "/" << combos.size()
                          << " (" << std::fixed << std::setprecision(1)
                          << (100.0 * done / combos.size()) << "%)" << std::flush;
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        std::cout << "\n[Sweeper] Grid done in " << std::fixed << std::setprecision(2)
                  << elapsed << "s" << std::endl;

        std::sort(results.begin(), results.end(),
                  [](const auto& a, const auto& b) { return a.total_pnl > b.total_pnl; });
        return results;
    }

    // ── BAYESIAN OPTIMIZATION ──
    static std::vector<SweepResult> bayesian_optimize(
        const std::vector<SweepParam>& param_defs,
        const StrategyFactory& factory,
        const std::vector<std::string>& symbols,
        const std::map<std::string, const OrderBookState*>& price_data,
        const std::map<std::string, const PublicTrade*>& trade_data,
        const std::map<std::string, size_t>& trade_counts,
        size_t total_ticks,
        int n_initial = 20,    // random initial evaluations
        int n_iterations = 80, // bayesian iterations
        bool optimize_sharpe = false) // optimize sharpe instead of raw PnL
    {
        int total_budget = n_initial + n_iterations;
        std::cout << "[Bayesian] " << total_budget << " evaluations ("
                  << n_initial << " initial + " << n_iterations << " guided)" << std::endl;

        auto t0 = std::chrono::high_resolution_clock::now();
        std::mt19937 rng(42);
        size_t n_params = param_defs.size();

        // Normalize params to [0, 1] for the GP
        auto normalize = [&](const std::vector<double>& raw) -> std::vector<double> {
            std::vector<double> norm(n_params);
            for (size_t i = 0; i < n_params; i++) {
                double range = param_defs[i].max - param_defs[i].min;
                norm[i] = (range > 0) ? (raw[i] - param_defs[i].min) / range : 0.5;
            }
            return norm;
        };

        auto denormalize = [&](const std::vector<double>& norm) -> std::vector<double> {
            std::vector<double> raw(n_params);
            for (size_t i = 0; i < n_params; i++) {
                double range = param_defs[i].max - param_defs[i].min;
                double val = param_defs[i].min + norm[i] * range;
                // Snap to grid
                if (param_defs[i].step > 0) {
                    val = param_defs[i].min + std::round((val - param_defs[i].min) / param_defs[i].step) * param_defs[i].step;
                    val = std::max(param_defs[i].min, std::min(param_defs[i].max, val));
                }
                raw[i] = val;
            }
            return raw;
        };

        // Generate random initial sample using Latin Hypercube
        auto random_params = [&]() -> std::vector<double> {
            std::vector<double> norm(n_params);
            for (size_t i = 0; i < n_params; i++) {
                std::uniform_real_distribution<double> dist(0.0, 1.0);
                norm[i] = dist(rng);
            }
            return denormalize(norm);
        };

        std::vector<SweepResult> results;
        std::vector<std::vector<double>> X_norm;
        std::vector<double> Y;

        // Phase 1: Random exploration
        std::cout << "[Bayesian] Phase 1: Random exploration..." << std::endl;
        for (int i = 0; i < n_initial; i++) {
            auto params = random_params();
            auto sr = evaluate(params, factory, symbols, price_data, trade_data, trade_counts, total_ticks);
            results.push_back(sr);
            X_norm.push_back(normalize(params));
            Y.push_back(optimize_sharpe ? sr.sharpe : sr.total_pnl);

            if ((i + 1) % 10 == 0)
                std::cout << "\r[Bayesian] Initial: " << (i + 1) << "/" << n_initial << std::flush;
        }
        std::cout << std::endl;

        // Phase 2: Bayesian guided search
        std::cout << "[Bayesian] Phase 2: GP-guided optimization..." << std::endl;
        GaussianProcess gp;
        // Set length scale relative to param count
        gp.length_scale = 0.3 * std::sqrt((double)n_params);
        gp.signal_var = 1.0;
        gp.noise_var = 0.01;

        double best_y = *std::max_element(Y.begin(), Y.end());

        for (int iter = 0; iter < n_iterations; iter++) {
            // Fit GP on normalized data
            // Normalize Y for stable GP fitting
            double y_scale = 1.0;
            std::vector<double> Y_scaled = Y;
            {
                double y_max = *std::max_element(Y.begin(), Y.end());
                double y_min = *std::min_element(Y.begin(), Y.end());
                double range = y_max - y_min;
                if (range > 0) {
                    y_scale = range;
                    for (auto& v : Y_scaled) v = (v - y_min) / range;
                }
            }

            gp.signal_var = 1.0;
            gp.noise_var = 0.01;
            gp.fit(X_norm, Y_scaled);

            double scaled_best = (best_y - *std::min_element(Y.begin(), Y.end()));
            if (y_scale > 0) scaled_best /= y_scale;

            // Find best next point by sampling candidates
            int n_candidates = 500;
            double best_ei = -1e30;
            std::vector<double> best_candidate;

            for (int c = 0; c < n_candidates; c++) {
                std::vector<double> cand_norm(n_params);
                for (size_t j = 0; j < n_params; j++) {
                    std::uniform_real_distribution<double> dist(0.0, 1.0);
                    cand_norm[j] = dist(rng);
                }

                double mean, var;
                gp.predict(cand_norm, mean, var);
                double ei = expected_improvement(mean, var, scaled_best);

                if (ei > best_ei) {
                    best_ei = ei;
                    best_candidate = cand_norm;
                }
            }

            // Evaluate the best candidate
            auto params = denormalize(best_candidate);
            auto sr = evaluate(params, factory, symbols, price_data, trade_data, trade_counts, total_ticks);
            results.push_back(sr);
            X_norm.push_back(normalize(params));
            double y_val = optimize_sharpe ? sr.sharpe : sr.total_pnl;
            Y.push_back(y_val);

            if (y_val > best_y) {
                best_y = y_val;
                std::cout << "\r[Bayesian] iter " << (iter + 1) << "/" << n_iterations
                          << " NEW BEST: " << std::fixed << std::setprecision(2) << y_val
                          << " [";
                for (size_t j = 0; j < params.size(); j++) {
                    if (j) std::cout << ",";
                    std::cout << param_defs[j].name << "=" << params[j];
                }
                std::cout << "]         " << std::flush;
            } else if ((iter + 1) % 20 == 0) {
                std::cout << "\r[Bayesian] iter " << (iter + 1) << "/" << n_iterations
                          << " best=" << std::fixed << std::setprecision(2) << best_y << std::flush;
            }
        }
        std::cout << std::endl;

        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        std::cout << "[Bayesian] Done in " << std::fixed << std::setprecision(2) << elapsed << "s"
                  << " | Best " << (optimize_sharpe ? "Sharpe" : "PnL") << ": "
                  << std::setprecision(2) << best_y << std::endl;

        std::sort(results.begin(), results.end(),
                  [](const auto& a, const auto& b) { return a.total_pnl > b.total_pnl; });
        return results;
    }

    static void print_top(const std::vector<SweepResult>& results,
                          const std::vector<SweepParam>& param_defs, int n = 10) {
        std::cout << "\n[Sweeper] Top " << std::min(n, (int)results.size()) << " results:\n";
        // Header
        std::cout << "  ";
        for (const auto& p : param_defs)
            std::cout << std::setw(12) << p.name;
        std::cout << " |         PnL  Trades   Sharpe\n";
        std::cout << "  " << std::string(param_defs.size() * 12 + 36, '-') << "\n";

        for (int i = 0; i < std::min(n, (int)results.size()); i++) {
            const auto& r = results[i];
            std::cout << "  ";
            for (double p : r.params)
                std::cout << std::setw(12) << std::fixed << std::setprecision(3) << p;
            std::cout << " | " << std::setw(10) << std::fixed << std::setprecision(2) << r.total_pnl
                      << std::setw(8) << r.total_trades
                      << std::setw(8) << std::setprecision(3) << r.sharpe << "\n";
        }
    }

private:
    static void generate_combos(const std::vector<SweepParam>& params, size_t idx,
                                std::vector<double>& current, std::vector<std::vector<double>>& out) {
        if (idx == params.size()) { out.push_back(current); return; }
        for (double v = params[idx].min; v <= params[idx].max + 1e-9; v += params[idx].step) {
            current[idx] = v;
            generate_combos(params, idx + 1, current, out);
        }
    }
};