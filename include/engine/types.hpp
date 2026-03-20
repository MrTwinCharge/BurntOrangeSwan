#pragma once
#include <cstdint>

// Existing Order Book Snapshot
struct OrderBookState {
    uint32_t timestamp;
    uint32_t best_bid_price;
    uint32_t best_ask_price;
    int32_t best_bid_volume;
    int32_t best_ask_volume;
};

// NEW: Public Trade Event
struct PublicTrade {
    uint32_t timestamp;
    int32_t price;
    int32_t quantity;
};