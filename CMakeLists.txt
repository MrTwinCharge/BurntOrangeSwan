cmake_minimum_required(VERSION 3.10)
project(prosperity_engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -flto=auto -Wall -Wextra")

find_package(OpenMP REQUIRED)
if(OpenMP_CXX_FOUND)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
endif()

include_directories(include)

add_library(engine STATIC 
    src/engine/loader.cpp 
    src/engine/lob.cpp
)

add_executable(backtester src/main.cpp)
target_link_libraries(backtester engine)

add_executable(translator scripts/translator.cpp)

add_executable(sweep scripts/sweep.cpp)
target_link_libraries(sweep engine)