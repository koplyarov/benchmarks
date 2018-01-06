project(benchmarks)
cmake_minimum_required(VERSION 2.8)

find_package(Threads REQUIRED)

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
else()
    add_definitions(-Wall)
    add_definitions(-std=c++11)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(benchmarks
    benchmarks/BenchmarkApp.cpp
    benchmarks/BenchmarkContext.cpp
    benchmarks/BenchmarkSuite.cpp
    benchmarks/detail/BenchmarkResult.cpp
    benchmarks/utils/Barrier.cpp
    benchmarks/utils/Logger.cpp
    benchmarks/utils/Memory.cpp
    benchmarks/utils/ThreadPriority.cpp
)

target_link_libraries(benchmarks ${CMAKE_THREAD_LIBS_INIT})
