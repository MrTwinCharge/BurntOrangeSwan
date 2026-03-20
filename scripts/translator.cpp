#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <map>
#include <filesystem>
#include "engine/types.hpp"

namespace fs = std::filesystem;

void translate_prices(const std::string& path) {
    std::ifstream file(path);
    std::string line, token;
    std::getline(file, line); // Skip header

    // Map to hold vectors for each product found
    std::map<std::string, std::vector<OrderBookState>> product_data;

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string day, ts, symbol;
        std::getline(ss, day, ';');
        std::getline(ss, ts, ';');
        std::getline(ss, symbol, ';');

        OrderBookState s{};
        s.timestamp = std::stoul(ts);
        
        std::getline(ss, token, ';'); s.best_bid_price = token.empty() ? 0 : std::stoul(token);
        std::getline(ss, token, ';'); s.best_bid_volume = token.empty() ? 0 : std::stoi(token);
        for(int i=0; i<4; ++i) std::getline(ss, token, ';'); // Skip L2/L3 bids
        std::getline(ss, token, ';'); s.best_ask_price = token.empty() ? 0 : std::stoul(token);
        std::getline(ss, token, ';'); s.best_ask_volume = token.empty() ? 0 : std::stoi(token);

        product_data[symbol].push_back(s);
    }

    for (auto const& [symbol, data] : product_data) {
        std::string out_name = "../data/binary/" + symbol + "_prices.bin";
        std::ofstream out(out_name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(OrderBookState));
        std::cout << "[Prices] Saved " << data.size() << " ticks for " << symbol << std::endl;
    }
}

void translate_trades(const std::string& path) {
    std::ifstream file(path);
    std::string line, token;
    std::getline(file, line); // Skip header

    std::map<std::string, std::vector<PublicTrade>> trade_data;

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string ts, buyer, seller, symbol, curr, price, qty;
        
        std::getline(ss, ts, ';');
        std::getline(ss, buyer, ';');
        std::getline(ss, seller, ';');
        std::getline(ss, symbol, ';');
        std::getline(ss, curr, ';');
        std::getline(ss, price, ';');
        std::getline(ss, qty, ';');

        PublicTrade t{ (uint32_t)std::stoul(ts), (int32_t)std::stod(price), std::stoi(qty) };
        trade_data[symbol].push_back(t);
    }

    for (auto const& [symbol, data] : trade_data) {
        std::string out_name = "../data/binary/" + symbol + "_trades.bin";
        std::ofstream out(out_name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(PublicTrade));
        std::cout << "[Trades] Saved " << data.size() << " events for " << symbol << std::endl;
    }
}

int main() {
    fs::create_directories("../data/binary");
    translate_prices("../data/raw/prices_round_0_day_-1.csv");
    translate_trades("../data/raw/trades_round_0_day_-1.csv");
    return 0;
}