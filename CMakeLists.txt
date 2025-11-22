project(NullRVSim)

cmake_minimum_required(VERSION 3.00)

set(CMAKE_CXX_STANDARD 20)

if (NOT DEFINED CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Debug")
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_CXX_FLAGS "-std=c++20 -O3 -DNDEBUG")
else ()
        set(CMAKE_CXX_FLAGS "-std=c++20 -g -O2")
endif ()

include_directories(
        src
        )

file(GLOB_RECURSE ALL_CXX_SRC
        src/*.cpp
        )

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/conf/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/conf/ FILES_MATCHING PATTERN "*.ini")

add_executable(nullrvsim ${ALL_CXX_SRC})

add_subdirectory(rvsrc/ecallproxy)
add_subdirectory(example)
