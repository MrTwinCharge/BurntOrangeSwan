# BurntOrangeSwan
🏗️ Architecture
The engine is split into three distinct layers:

Translator (scripts/): Converts bloated CSV data into tightly packed binary blobs (.bin).

Loader (src/engine/loader.cpp): Maps binary data directly into the process's virtual memory space.

Engine/Strategy: A synchronized multi-asset loop that feeds price snapshots and trade events to pluggable strategy modules.

⚡ Key Features
Zero-Copy Loading: Using mmap(), we load 10,000+ ticks in <1ms.

Multi-Asset Sync: Automatically aligns timestamps across multiple products (e.g., TOMATOES and EMERALDS).

Memory Aligned: All data structures are padded for cache-line efficiency.

🚀 Getting Started
1. Prerequisites
Ensure you are running in WSL (Ubuntu). Do not run this from a Windows-mounted directory (/mnt/c/) if you want full performance.

Bash
cd ~/prosperity_engine
2. Data Ingestion
Drop your Prosperity CSVs into data/raw/ and run the translator to generate the binary files:

Bash
cd build
cmake ..
make -j$(nproc)
./translator
3. Run the Backtester
Bash
./backtester