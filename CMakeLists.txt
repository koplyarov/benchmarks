project(benchmarks)
cmake_minimum_required(VERSION 2.8)

find_package(Threads REQUIRED)

find_package(Boost COMPONENTS
    program_options
    system
    exception
    thread
    date_time
    regex
    serialization
)

if (Boost_FOUND)
    if (MSVC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
    else()
        add_definitions(-Wall)
        add_definitions(-std=c++11)
    endif()

    set(BENCHMARKS_DEPENDENCIES_RESOLVED TRUE PARENT_SCOPE)

    include_directories(${CMAKE_CURRENT_SOURCE_DIR})
    include_directories(${Boost_INCLUDE_DIRS})

    add_definitions(-DBOOST_SPIRIT_USE_PHOENIX_V3=1)

    add_library(benchmarks
        benchmarks/BenchmarkApp.cpp
        benchmarks/BenchmarkSuite.cpp
        benchmarks/detail/BenchmarkResult.cpp
        benchmarks/ipc/MessageQueue.cpp
        benchmarks/utils/Barrier.cpp
        benchmarks/utils/Logger.cpp
        benchmarks/utils/Memory.cpp
        benchmarks/utils/ThreadPriority.cpp
    )

    target_link_libraries(benchmarks ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
else()
    set(BENCHMARKS_DEPENDENCIES_RESOLVED FALSE PARENT_SCOPE)
endif()
