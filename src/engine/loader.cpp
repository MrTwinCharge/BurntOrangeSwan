#include "engine/loader.hpp"
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

void* mmap_file(const std::string& filepath, size_t& out_size_bytes) {
    int fd = open(filepath.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cerr << "[Loader] Fatal: Could not open: " << filepath << std::endl;
        return nullptr;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        std::cerr << "[Loader] Fatal: Could not stat: " << filepath << std::endl;
        close(fd);
        return nullptr;
    }

    out_size_bytes = sb.st_size;

    void* mapped = mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd); // safe to close after mmap

    if (mapped == MAP_FAILED) {
        std::cerr << "[Loader] Fatal: mmap() failed for: " << filepath << std::endl;
        return nullptr;
    }

    return mapped;
}

const OrderBookState* load_price_data(const std::string& filepath, size_t& out_count) {
    size_t bytes = 0;
    void* data = mmap_file(filepath, bytes);
    if (!data) { out_count = 0; return nullptr; }
    out_count = bytes / sizeof(OrderBookState);
    return static_cast<const OrderBookState*>(data);
}

const PublicTrade* load_trade_data(const std::string& filepath, size_t& out_count) {
    size_t bytes = 0;
    void* data = mmap_file(filepath, bytes);
    if (!data) { out_count = 0; return nullptr; }
    out_count = bytes / sizeof(PublicTrade);
    return static_cast<const PublicTrade*>(data);
}