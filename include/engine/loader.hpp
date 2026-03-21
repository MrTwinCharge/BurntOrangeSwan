#pragma once
#include <string>
#include <cstddef>
#include "engine/types.hpp"

/**
 * Maps a binary file into the process's virtual memory space via mmap().
 * Works for both OrderBookState and PublicTrade arrays.
 *
 * @param filepath   Path to the .bin file
 * @param out_count  Reference to store the number of elements found
 * @return           Pointer to the memory-mapped data, or nullptr on failure
 */
const OrderBookState* load_price_data(const std::string& filepath, size_t& out_count);
const PublicTrade*    load_trade_data(const std::string& filepath, size_t& out_count);

// Generic mmap helper (returns void*, caller casts)
void* mmap_file(const std::string& filepath, size_t& out_size_bytes);