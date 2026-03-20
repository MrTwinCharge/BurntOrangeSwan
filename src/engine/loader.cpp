#include "engine/loader.hpp"
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

const OrderBookState* load_binary_data(const std::string& filepath, size_t& out_num_ticks) {
    // 1. Open the file
    int fd = open(filepath.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cerr << "Fatal: Could not open binary file: " << filepath << std::endl;
        return nullptr;
    }

    // 2. Get file size
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        std::cerr << "Fatal: Could not get file size." << std::endl;
        close(fd);
        return nullptr;
    }

    // 3. Map the file to memory
    void* mapped_data = mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped_data == MAP_FAILED) {
        std::cerr << "Fatal: mmap() failed." << std::endl;
        close(fd);
        return nullptr;
    }

    // The OS keeps the mapping even after we close the file descriptor
    close(fd);

    out_num_ticks = sb.st_size / sizeof(OrderBookState);
    return static_cast<const OrderBookState*>(mapped_data);
}