#pragma once
#include "engine/strategy.hpp"
#include <cmath>

class SpreadCapture : public Strategy {
public:
    // Sweepable params
    double max_position_frac = 0.5;  // max position as fraction of limit (0.0-1.0)
    int    order_size        = 5;
    int    min_spread        = 4;    // only trade if spread >= this
    bool   verbose           = false;

    void on_tick(uint32_t timestamp,
                 const std::map<std::string, OrderBookState>& books,
                 const std::map<std::string, std::vector<PublicTrade>>& /*trades*/,
                 std::map<std::string, LimitOrderBook>& lobs) override {

        for (const auto& [symbol, book] : books) {
            auto& lob = lobs[symbol];

            if (book.bid_price_1 == 0 || book.ask_price_1 == 0) {
                lob.match_orders({});
                continue;
            }

            int spread = (int)book.ask_price_1 - (int)book.bid_price_1;
            if (spread < min_spread) {
                lob.match_orders({});
                continue;
            }

            int limit = lob.position_limit;
            int pos = lob.position;
            int max_pos = (int)(limit * max_position_frac);
            std::vector<StrategyOrder> orders;

            // Quote inside the spread: improve by 1 tick on each side
            int our_bid = (int)book.bid_price_1 + 1;
            int our_ask = (int)book.ask_price_1 - 1;

            // Only quote if we'd still capture positive spread
            if (our_ask - our_bid < 2) {
                lob.match_orders({});
                continue;
            }

            // Skew based on position
            if (pos > max_pos / 2) {
                // Long heavy — be more aggressive selling, less aggressive buying
                our_ask -= 1;
                our_bid -= 1;
            } else if (pos < -max_pos / 2) {
                // Short heavy — more aggressive buying
                our_ask += 1;
                our_bid += 1;
            }

            // Place buy order (passive — will fill if bots cross our price)
            // In backtester, this fills against L1 bid if our bid >= their bid
            if (pos < max_pos) {
                int qty = std::min(order_size, max_pos - pos);
                if (qty > 0) orders.push_back({our_bid, qty});
            }

            // Place sell order
            if (pos > -max_pos) {
                int qty = std::min(order_size, max_pos + pos);
                if (qty > 0) orders.push_back({our_ask, -qty});
            }

            // Also take L2 if it's mispriced vs mid
            double mid = book.mid_price();
            if (mid > 0) {
                // Buy L1 ask if it's below mid (shouldn't happen often but free money)
                if ((double)book.ask_price_1 < mid && pos < max_pos) {
                    int qty = std::min(std::abs(book.ask_volume_1), max_pos - pos);
                    if (qty > 0) orders.push_back({(int32_t)book.ask_price_1, qty});
                }
                // Sell to L1 bid if it's above mid
                if ((double)book.bid_price_1 > mid && pos > -max_pos) {
                    int qty = std::min(std::abs(book.bid_volume_1), max_pos + pos);
                    if (qty > 0) orders.push_back({(int32_t)book.bid_price_1, -qty});
                }
            }

            auto fills = lob.match_orders(orders);

            if (verbose) {
                for (const auto& f : fills) {
                    std::cout << "[" << timestamp << "] SC "
                              << (f.quantity > 0 ? "BUY " : "SELL ")
                              << symbol << " " << std::abs(f.quantity)
                              << "@" << f.price
                              << " | spread=" << spread
                              << " | Pos=" << lob.position << std::endl;
                }
            }
        }
    }
};