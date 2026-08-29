cmake_minimum_required(VERSION 3.16)
project(mos6502 LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# The core is header-only: the CPU is a template over its bus, so there is
# nothing to compile until a host instantiates it.
add_library(mos6502 INTERFACE)
target_include_directories(mos6502 INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(mos6502 INTERFACE cxx_std_20)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(test_cpu tests/test_cpu.cpp)
target_link_libraries(test_cpu PRIVATE mos6502)
add_test(NAME unit COMMAND test_cpu)

# Runs Klaus Dormann's 6502 functional test image (see README for where to get
# it). Not wired into ctest, because the image is not in the repository.
add_executable(run_klaus tools/run_klaus.cpp)
target_link_libraries(run_klaus PRIVATE mos6502)

add_executable(benchmark tools/benchmark.cpp)
target_link_libraries(benchmark PRIVATE mos6502)

# Runs the SingleStepTests / Tom Harte per-cycle suite against the vendored
# Harte-65x02 test data. With no arguments it runs all 256 opcode files.
add_executable(run_harte tools/run_harte.cpp)
target_link_libraries(run_harte PRIVATE mos6502)
target_include_directories(run_harte PRIVATE tools)
