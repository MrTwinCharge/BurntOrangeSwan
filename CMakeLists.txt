cmake_minimum_required(VERSION 3.10)

# Name your project and set the language to C++
project(ProsperityEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add maximum compiler optimization flags for speed (-O3)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Wextra")

# Tell CMake where your header files live
include_directories(include)

add_library(engine src/engine/loader.cpp src/engine/lob.cpp)

add_executable(translator scripts/translator.cpp)
# Link the core engine library so the translator can use your struct definitions
target_link_libraries(translator engine)
add_executable(backtester src/main.cpp)
target_link_libraries(backtester engine)