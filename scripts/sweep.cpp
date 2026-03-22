#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include "engine/loader.hpp"
#include "engine/lob.hpp"
#include "engine/results.hpp"
#include "engine/sweeper.hpp"
#include "strategies/omni_strategy.hpp"
#include "strategies/market_maker.hpp"
#include "strategies/mean_reversion.hpp"
#include "strategies/spread_capture.hpp"
#include "strategies/fair_value.hpp"

namespace fs = std::filesystem;

struct StrategyEntry {
    std::string name;
    std::string python_template;
    std::vector<SweepParam> params;
    StrategyFactory factory;
};

std::vector<StrategyEntry> build_registry() {
    std::vector<StrategyEntry> reg;

    reg.push_back({"omni", "trader_obi.py",
        {{"threshold", 0.05, 0.50, 0.05}, {"order_size", 2, 20, 2}},
        [](const std::vector<double>& p) -> Strategy* {
            auto* s = new OmniImbalance(); s->default_threshold=p[0]; s->default_size=(int)p[1]; 
            s->target_symbols = {"TOMATOES"};
            return s;
        }});

    reg.push_back({"mm", "trader_mm.py",
        {{"edge", 0, 5, 1}, {"order_size", 2, 20, 2}},
        [](const std::vector<double>& p) -> Strategy* {
            auto* s = new MarketMaker(); s->default_edge=(int)p[0]; s->default_order_size=(int)p[1]; 
            s->target_symbols = {"EMERALDS"}; 
            return s;
        }});

    reg.push_back({"mr", "trader_mr.py",
        {{"ema_alpha", 0.02, 0.20, 0.02}, {"z_threshold", 0.5, 3.0, 0.5}, {"order_size", 2, 10, 2}},
        [](const std::vector<double>& p) -> Strategy* {
            auto* s = new MeanReversion(); s->ema_alpha=p[0]; s->z_threshold=p[1]; s->order_size=(int)p[2]; 
            s->target_symbols = {"EMERALDS"}; 
            return s;
        }});

    reg.push_back({"sc", "trader_sc.py",
        {{"max_pos_frac", 0.2, 0.8, 0.2}, {"order_size", 2, 10, 2}, {"min_spread", 2, 10, 2}},
        [](const std::vector<double>& p) -> Strategy* {
            auto* s = new SpreadCapture(); s->max_position_frac=p[0]; s->order_size=(int)p[1]; s->min_spread=(int)p[2]; 
            s->target_symbols = {"EMERALDS"};
            return s;
        }});

    reg.push_back({"fv", "trader_fv.py",
        {{"threshold", 0.5, 3.0, 0.5}, {"order_size", 2, 20, 2}},
        [](const std::vector<double>& p) -> Strategy* {
            auto* s = new FairValue(); s->threshold=p[0]; s->order_size=(int)p[1]; 
            s->target_symbols = {"TOMATOES"};
            return s;
        }});

    return reg;
}

void run_and_save(const std::string& strat_name, Strategy* strat,
    const std::vector<std::string>& symbols,
    const std::map<std::string,const OrderBookState*>& pd,
    const std::map<std::string,const PublicTrade*>& td,
    const std::map<std::string,size_t>& tc, size_t ticks) {

    strat->init(symbols);
    size_t num_symbols = symbols.size();
    
    std::vector<LimitOrderBook> lobs;
    std::vector<size_t> tp(num_symbols, 0);
    std::vector<OrderBookState> bk(num_symbols);
    std::vector<std::vector<PublicTrade>> tr(num_symbols);

    for (size_t s = 0; s < num_symbols; ++s) {
        lobs.push_back(LimitOrderBook(symbols[s]));
        tr[s].reserve(20);
    }

    for (size_t i = 0; i < ticks; ++i) {
        uint32_t ts = pd.at(symbols[0])[i].timestamp;
        
        for (size_t s = 0; s < num_symbols; ++s) {
            bk[s] = pd.at(symbols[s])[i];
            tr[s].clear();
            
            while (tp[s] < tc.at(symbols[s]) && td.at(symbols[s]) && td.at(symbols[s])[tp[s]].timestamp <= ts) {
                tr[s].push_back(td.at(symbols[s])[tp[s]]);
                tp[s]++;
            }
            lobs[s].update(bk[s], tr[s]);
        }
        strat->on_tick(ts, bk, tr, lobs);
    }

    std::map<std::string, LimitOrderBook> out_lobs;
    for (size_t s = 0; s < num_symbols; ++s) {
        out_lobs[symbols[s]] = lobs[s];
    }

    std::string pnl_path = "../results/pnl_" + strat_name + ".csv";
    std::string trades_path = "../results/trades_" + strat_name + ".csv";
    results::export_pnl_csv(out_lobs, pnl_path);
    results::export_trades_csv(out_lobs, trades_path);

    double total = 0;
    int total_trades = 0;
    for (const auto& [sym, lob] : out_lobs) {
        total += lob.result.total_pnl;
        total_trades += lob.result.total_buys + lob.result.total_sells;
    }
    std::cout << "  [" << strat_name << "] PnL: " << std::fixed << std::setprecision(2) << total
              << "  Trades: " << total_trades << "\n";
}

void generate_trader(const StrategyEntry& entry, const SweepResult& best) {
    std::ifstream fin("../python/traders/" + entry.python_template);
    if (!fin.is_open()) { std::cerr << "[Gen] Missing: ../python/traders/" << entry.python_template << "\n"; return; }
    std::stringstream buf; buf << fin.rdbuf(); std::string code = buf.str(); fin.close();

    auto inject = [&](const std::string& name, const std::string& val) {
        auto p = code.find(name + " = ");
        if (p != std::string::npos) { auto e = code.find('\n', p); code.replace(p, e - p, name + " = " + val); }
    };

    if (entry.name == "omni") {
        std::ostringstream t; t << std::fixed << std::setprecision(2) << best.params[0];
        inject("THRESHOLD", t.str()); inject("ORDER_SIZE", std::to_string((int)best.params[1]));
    } else if (entry.name == "mm") {
        inject("EDGE", std::to_string((int)best.params[0])); inject("ORDER_SIZE", std::to_string((int)best.params[1]));
    } else if (entry.name == "mr") {
        std::ostringstream a; a << std::fixed << std::setprecision(2) << best.params[0];
        std::ostringstream z; z << std::fixed << std::setprecision(2) << best.params[1];
        inject("EMA_ALPHA", a.str()); inject("Z_THRESHOLD", z.str()); inject("ORDER_SIZE", std::to_string((int)best.params[2]));
    } else if (entry.name == "sc") {
        std::ostringstream f; f << std::fixed << std::setprecision(2) << best.params[0];
        inject("MAX_POS_FRAC", f.str()); inject("ORDER_SIZE", std::to_string((int)best.params[1]));
        inject("MIN_SPREAD", std::to_string((int)best.params[2]));
    } else if (entry.name == "fv") { 
        std::ostringstream t; t << std::fixed << std::setprecision(2) << best.params[0];
        inject("THRESHOLD", t.str()); inject("ORDER_SIZE", std::to_string((int)best.params[1]));
    }

    std::ofstream fout("../python/trader.py"); fout << code; fout.close();
    std::cout << "[Gen] " << entry.name << " ->";
    for (size_t i = 0; i < entry.params.size(); i++) std::cout << " " << entry.params[i].name << "=" << best.params[i];
    std::cout << "\n[Gen] Written: " << fs::absolute("../python/trader.py").string() << "\n";
}

static std::string read_file(const std::string& p) { std::ifstream f(p); if (!f) return ""; std::stringstream b; b << f.rdbuf(); return b.str(); }
static std::string js_esc(const std::string& s) { std::string o; o.reserve(s.size()); for (char c : s) { if (c=='`') o += "\\`"; else if (c=='\\') o += "\\\\"; else if (c=='$') o += "\\$"; else o += c; } return o; }

void open_dashboard(const std::vector<std::string>& strat_names) {
    std::string v = read_file("../tools/visualizer.html");
    if (v.empty()) { std::cerr << "[Vis] No visualizer.html\n"; return; }

    std::string inj = "\n<script>\nconst _i=[\n";
    auto add = [&](const std::string& n, const std::string& c) { if (!c.empty()) inj += "{name:'" + n + "',content:`" + js_esc(c) + "`},\n"; };

    add("pnl.csv", read_file("../results/pnl_" + strat_names[0] + ".csv"));
    add("trades.csv", read_file("../results/trades_" + strat_names[0] + ".csv"));

    for (const auto& name : strat_names) {
        add("pnl_" + name + ".csv", read_file("../results/pnl_" + name + ".csv"));
        add("trades_" + name + ".csv", read_file("../results/trades_" + name + ".csv"));
    }

    add("sweep.csv", read_file("../results/sweep.csv"));

    if (fs::exists("../data/raw/")) {
        for (const auto& e : fs::directory_iterator("../data/raw/")) {
            if (e.is_regular_file() && e.path().extension() == ".csv")
                add(e.path().filename().string(), read_file(e.path().string()));
        }
    }

    inj += "];\n";
    inj += "window.addEventListener('DOMContentLoaded',()=>{"
           "const f=_i.map(x=>new File([new Blob([x.content],{type:'text/csv'})],x.name,{type:'text/csv'}));"
           "setTimeout(()=>{if(typeof handleFiles==='function')handleFiles(f);},100);});\n";
    inj += "</script>\n";

    std::string out = v;
    auto p = out.rfind("</body>");
    if (p != std::string::npos) out.insert(p, inj);
    else out += inj;

    std::ofstream f("../results/dashboard.html"); f << out; f.close();
    std::string ap = fs::absolute("../results/dashboard.html").string();
    std::string cmd = "command -v explorer.exe>/dev/null 2>&1&&explorer.exe \"$(wslpath -w '" + ap + "')\" 2>/dev/null||"
                      "command -v xdg-open>/dev/null 2>&1&&xdg-open \"" + ap + "\" 2>/dev/null||"
                      "echo '[Vis] Open: " + ap + "'";
    
    // FIX: Acknowledged system command output to silence the GCC warning
    if (system(cmd.c_str()) != 0) {
        // Do nothing, just suppressing the warning
    }
}

int main(int argc, char* argv[]) {
    std::string target = "all"; bool vis = true, gen = true;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--no-vis") vis = false;
        else if (a == "--no-gen") gen = false;
        else if (a == "-h" || a == "--help") { std::cout << "Usage: ./sweep [omni|mm|mr|sc|fv|all] [--no-vis] [--no-gen]\n"; return 0; }
        else target = a;
    }

    std::string bd = "../data/binary/";
    if (!fs::exists(bd)) { std::cerr << "Run ./translator first.\n"; return 1; }

    std::vector<std::string> sym;
    for (const auto& e : fs::directory_iterator(bd)) {
        if (!e.is_regular_file()) continue;
        std::string n = e.path().filename().string(), sfx = "_prices.bin";
        if (n.size() > sfx.size() && n.substr(n.size() - sfx.size()) == sfx)
            sym.push_back(n.substr(0, n.size() - sfx.size()));
    }
    std::sort(sym.begin(), sym.end());
    if (sym.empty()) { std::cerr << "No data.\n"; return 1; }

    std::map<std::string,const OrderBookState*> pd;
    std::map<std::string,const PublicTrade*> td;
    std::map<std::string,size_t> tc;
    size_t ticks = SIZE_MAX;
    for (const auto& s : sym) {
        size_t a = 0, b = 0;
        auto* p = load_price_data(bd + s + "_prices.bin", a);
        auto* t = load_trade_data(bd + s + "_trades.bin", b);
        if (!p || a == 0) continue;
        pd[s] = p; td[s] = t; tc[s] = b;
        ticks = std::min(ticks, a);
    }

    std::cout << "[Sweep] Products:";
    for (const auto& s : sym) std::cout << " " << s;
    std::cout << "\n[Sweep] Ticks: " << ticks << "\n\n";

    auto reg = build_registry();
    std::vector<StrategyEntry> torun;
    if (target == "all") torun = reg;
    else {
        for (const auto& e : reg) if (e.name == target) { torun.push_back(e); break; }
        if (torun.empty()) {
            std::cerr << "Unknown: " << target << ". Available:";
            for (const auto& e : reg) std::cerr << " " << e.name;
            std::cerr << " all\n"; return 1;
        }
    }

    struct Best { std::string name, tmpl; SweepResult r; std::vector<SweepParam> p; StrategyFactory f; };
    std::vector<Best> all;

    for (const auto& e : torun) {
        std::cout << "══════════════════════════════════════════\n  Sweeping: " << e.name
                  << "\n══════════════════════════════════════════\n\n";
                  
        auto res = ParamSweeper::sweep(e.params, e.factory, sym, pd, td, tc, ticks);
        
        ParamSweeper::print_top(res, e.params, 5);
        std::cout << "\n";
        if (!res.empty()) all.push_back({e.name, e.python_template, res[0], e.params, e.factory});
    }

    if (all.empty()) { std::cerr << "No results.\n"; return 1; }
    std::sort(all.begin(), all.end(), [](auto& a, auto& b) { return a.r.total_pnl > b.r.total_pnl; });

    std::cout << "\n╔══════════════════════════════════════════════════╗\n"
              << "║  GLOBAL RANKINGS                                 ║\n"
              << "╠══════════════════════════════════════════════════╣\n";
    for (size_t i = 0; i < all.size(); i++) {
        auto& x = all[i];
        std::cout << "║ " << (i == 0 ? ">>>" : "   ") << " " << std::left << std::setw(6) << x.name
                  << "  PnL:" << std::right << std::setw(12) << std::fixed << std::setprecision(2) << x.r.total_pnl
                  << "  Trades:" << std::setw(6) << x.r.total_trades << "  [";
        for (size_t j = 0; j < x.p.size(); j++) {
            if (j) std::cout << ",";
            std::cout << x.p[j].name << "=" << std::setprecision(2) << x.r.params[j];
        }
        std::cout << "]" << (i == 0 ? " <- WINNER" : "") << "\n";
    }
    std::cout << "╚══════════════════════════════════════════════════╝\n\n";

    fs::create_directories("../results");

    {
        std::ofstream f("../results/sweep.csv");
        f << "strategy";
        size_t mp = 0;
        for (auto& x : all) mp = std::max(mp, x.p.size());
        for (size_t i = 0; i < mp; i++) f << ",param_" << i;
        f << ",total_pnl,total_trades,max_drawdown\n";
        for (auto& x : all) {
            f << x.name;
            for (size_t i = 0; i < mp; i++) { f << ","; if (i < x.r.params.size()) f << std::fixed << std::setprecision(4) << x.r.params[i]; }
            f << "," << std::fixed << std::setprecision(2) << x.r.total_pnl << "," << x.r.total_trades << "," << x.r.max_drawdown << "\n";
        }
    }

    std::cout << "[Sweep] Running full backtests for all strategies...\n";
    std::vector<std::string> strat_names;
    for (auto& x : all) {
        strat_names.push_back(x.name);
        Strategy* s = x.f(x.r.params);
        run_and_save(x.name, s, sym, pd, td, tc, ticks);
        delete s;
    }

    {
        auto content = read_file("../results/pnl_" + all[0].name + ".csv");
        std::ofstream f("../results/pnl.csv"); f << content;
    }
    {
        auto content = read_file("../results/trades_" + all[0].name + ".csv");
        std::ofstream f("../results/trades.csv"); f << content;
    }

    if (gen) {
        for (const auto& e : torun) {
            if (e.name == all[0].name) { std::cout << "\n"; generate_trader(e, all[0].r); break; }
        }
    }

    if (vis) open_dashboard(strat_names);

    return 0;
}