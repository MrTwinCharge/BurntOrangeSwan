#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <map>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include "engine/types.hpp"
 
namespace fs = std::filesystem;
 
static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    auto end   = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}
 
static int safe_int(const std::string& s) {
    std::string t = trim(s);
    if (t.empty()) return 0;
    try { return (int)std::round(std::stod(t)); }
    catch (...) { return 0; }
}
 
static int safe_price_x100(const std::string& s) {
    std::string t = trim(s);
    if (t.empty()) return 0;
    try { return (int)std::round(std::stod(t) * 100.0); }
    catch (...) { return 0; }
}
 
static char detect_delimiter(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    std::getline(f, line);
    return (std::count(line.begin(), line.end(), ';') > 
            std::count(line.begin(), line.end(), ',')) ? ';' : ',';
}
 
// ── Translate Prices CSV → Binary ────────────────────────────────────────
void translate_prices(const std::string& path) {
    char delim = detect_delimiter(path);
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Translator] Error: Could not open " << path << std::endl;
        return;
    }
 
    std::string line;
    std::getline(file, line); // Skip header
 
    std::map<std::string, std::vector<OrderBookState>> product_data;
 
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> cols;
        while (std::getline(ss, token, delim)) {
            cols.push_back(trim(token));
        }
 
        if (cols.size() < 16) continue;
 
        std::string symbol = cols[2];
        if (symbol.empty()) continue;
 
        OrderBookState s{};
        s.timestamp      = (uint32_t)safe_int(cols[1]);
        s.bid_price_1    = (uint32_t)safe_int(cols[3]);
        s.bid_volume_1   = safe_int(cols[4]);
        s.bid_price_2    = (uint32_t)safe_int(cols[5]);
        s.bid_volume_2   = safe_int(cols[6]);
        s.bid_price_3    = (uint32_t)safe_int(cols[7]);
        s.bid_volume_3   = safe_int(cols[8]);
        s.ask_price_1    = (uint32_t)safe_int(cols[9]);
        s.ask_volume_1   = safe_int(cols[10]);
        s.ask_price_2    = (uint32_t)safe_int(cols[11]);
        s.ask_volume_2   = safe_int(cols[12]);
        s.ask_price_3    = (uint32_t)safe_int(cols[13]);
        s.ask_volume_3   = safe_int(cols[14]);
        s.mid_price_x100 = safe_price_x100(cols[15]);
 
        product_data[symbol].push_back(s);
    }
 
    for (const auto& [symbol, data] : product_data) {
        std::string out_name = "../data/binary/" + symbol + "_prices.bin";
        std::ofstream out(out_name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), 
                  data.size() * sizeof(OrderBookState));
        std::cout << "  [Prices] " << symbol << ": " << data.size() 
                  << " ticks (" << (data.size() * sizeof(OrderBookState)) 
                  << " bytes)" << std::endl;
    }
}
 
// ── Translate Trades CSV → Binary ────────────────────────────────────────
void translate_trades(const std::string& path) {
    char delim = detect_delimiter(path);
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Translator] Error: Could not open " << path << std::endl;
        return;
    }
 
    std::string line;
    std::getline(file, line); // Skip header
 
    std::map<std::string, std::vector<PublicTrade>> trade_data;
 
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> cols;
        while (std::getline(ss, token, delim)) {
            cols.push_back(trim(token));
        }
 
        if (cols.size() < 7) continue;
 
        std::string symbol = cols[3];
        if (symbol.empty()) continue;
 
        PublicTrade t{};
        t.timestamp = (uint32_t)safe_int(cols[0]);
        t.price     = safe_int(cols[5]);
        t.quantity  = safe_int(cols[6]);
 
        trade_data[symbol].push_back(t);
    }
 
    for (const auto& [symbol, data] : trade_data) {
        std::string out_name = "../data/binary/" + symbol + "_trades.bin";
        std::ofstream out(out_name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()),
                  data.size() * sizeof(PublicTrade));
        std::cout << "  [Trades] " << symbol << ": " << data.size()
                  << " events (" << (data.size() * sizeof(PublicTrade))
                  << " bytes)" << std::endl;
    }
}
 
int main(int argc, char* argv[]) {
    fs::create_directories("../data/binary");
 
    std::cout << "=== BurntOrangeSwan Translator ===" << std::endl;
    std::cout << "OrderBookState size: " << sizeof(OrderBookState) << " bytes" << std::endl;
    std::cout << "PublicTrade size:    " << sizeof(PublicTrade) << " bytes\n" << std::endl;
 
    // ── Mode 1: explicit file args ──
    if (argc >= 2) {
        for (int i = 1; i < argc; i++) {
            std::string path = argv[i];
            std::string name = fs::path(path).filename().string();
            if (name.find("prices") != std::string::npos) {
                std::cout << "[File] " << name << std::endl;
                translate_prices(path);
            } else if (name.find("trades") != std::string::npos) {
                std::cout << "[File] " << name << std::endl;
                translate_trades(path);
            } else {
                std::cerr << "[Skip] " << name << " (doesn't match prices_* or trades_*)" << std::endl;
            }
        }
        std::cout << "\nDone." << std::endl;
        return 0;
    }
 
    // ── Mode 2: auto-discover all CSVs in ../data/raw/ ──
    std::string raw_dir = "../data/raw/";
    if (!fs::exists(raw_dir)) {
        std::cerr << "No data/raw/ directory found and no files passed as args.\n"
                  << "Usage:\n"
                  << "  ./translator                        (auto-scan data/raw/)\n"
                  << "  ./translator file1.csv file2.csv    (explicit files)\n";
        return 1;
    }
 
    std::vector<std::string> price_files, trade_files;
    for (const auto& entry : fs::directory_iterator(raw_dir)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.size() < 4 || name.substr(name.size() - 4) != ".csv") continue;
 
        if (name.find("prices") != std::string::npos) {
            price_files.push_back(entry.path().string());
        } else if (name.find("trades") != std::string::npos) {
            trade_files.push_back(entry.path().string());
        }
    }
 
    std::sort(price_files.begin(), price_files.end());
    std::sort(trade_files.begin(), trade_files.end());
 
    if (price_files.empty() && trade_files.empty()) {
        std::cerr << "No CSVs found in " << raw_dir << std::endl;
        return 1;
    }
 
    std::cout << "Found " << price_files.size() << " price file(s), "
              << trade_files.size() << " trade file(s) in " << raw_dir << "\n" << std::endl;
 
    for (const auto& f : price_files) {
        std::string name = fs::path(f).filename().string();
        std::cout << "[File] " << name << std::endl;
        translate_prices(f);
        std::cout << std::endl;
    }
 
    for (const auto& f : trade_files) {
        std::string name = fs::path(f).filename().string();
        std::cout << "[File] " << name << std::endl;
        translate_trades(f);
        std::cout << std::endl;
    }
 
    std::cout << "Done. Binary files written to ../data/binary/" << std::endl;
    return 0;
}