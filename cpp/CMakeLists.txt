cmake_minimum_required(VERSION 3.24)
project(faircross_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Strict warnings and integer-only floating-point prohibition
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wdouble-promotion
    )
endif()

# Build Options
option(FAIRCROSS_ENABLE_ASAN "Enable AddressSanitizer and UB Sanitizer" OFF)

if (FAIRCROSS_ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Include directories
include_directories(include)

# Source files for faircross core library
set(FAIRCROSS_CORE_SOURCES
    src/domain/primitives.cpp
    src/domain/encoding.cpp
    src/commitments/sha256.cpp
    src/commitments/merkle.cpp
    src/commitments/order_commitment.cpp
    src/commitments/batch_commitment.cpp
    src/engine/batch.cpp
    src/engine/partition.cpp
    src/engine/auction.cpp
    src/engine/allocation.cpp
    src/engine/fill.cpp
    src/engine/transition.cpp
    src/engine/accounting.cpp
    src/engine/oracle_validation.cpp
    src/engine/checker.cpp
    src/adversary/mutator.cpp
    src/adversary/matrix.cpp
    src/proof/r1cs.cpp
    src/proof/constraints/order_validity.cpp
    src/proof/constraints/oracle.cpp
    src/proof/constraints/merkle_inclusion.cpp
    src/proof/constraints/complete_input_accounting.cpp
    src/proof/constraints/clearing_optimality.cpp
    src/proof/constraints/allocation.cpp
    src/proof/constraints/fill_bounds.cpp
    src/proof/constraints/conservation.cpp
    src/proof/statement.cpp
    src/proof/prover.cpp
    src/proof/verifier.cpp
    src/proof/recursive/state.cpp
    src/proof/recursive/prover.cpp
    src/proof/recursive/verifier.cpp
    src/simulator/rng.cpp
    src/simulator/agent.cpp
    src/simulator/population.cpp
    src/simulator/generator.cpp
    src/simulator/interval.cpp
    src/simulator/metrics.cpp
    src/marketdata/parser.cpp
    src/marketdata/replay.cpp
    src/marketdata/book.cpp
    src/marketdata/bridge.cpp
)

add_library(faircross_core STATIC ${FAIRCROSS_CORE_SOURCES})
target_include_directories(faircross_core PUBLIC include)

# Executable CLI
add_executable(faircross_cli src/cli/main.cpp)
target_link_libraries(faircross_cli PRIVATE faircross_core)
set_target_properties(faircross_cli PROPERTIES OUTPUT_NAME "faircross_cpp")

# Test suite executable
file(GLOB TEST_SOURCES "tests/*.cpp")
add_executable(faircross_tests ${TEST_SOURCES})
target_link_libraries(faircross_tests PRIVATE faircross_core)

enable_testing()
add_test(NAME FairCrossAllTests COMMAND faircross_tests)
