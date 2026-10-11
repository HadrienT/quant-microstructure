cmake_minimum_required(VERSION 3.25)

# The version is read from this line by pyproject.toml: keep it on one line.
project(quantMicrostructure VERSION 0.1.0 LANGUAGES CXX)

# ---- Build options ----
option(QM_BUILD_TESTS "Build unit tests (GoogleTest)" OFF)
option(QM_BUILD_PYTHON "Build Python bindings (pybind11)" OFF)
option(QM_BUILD_BENCHMARKS "Build benchmarks (google-benchmark)" OFF)

# ---- Global options ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(BuildInfo)

function(qm_set_warnings target)
  target_compile_options(${target} PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
  )
endfunction()

# ---- Main library ----
configure_file(
  ${PROJECT_SOURCE_DIR}/src/core/build_info.cpp.in
  ${PROJECT_BINARY_DIR}/generated/build_info.cpp
  @ONLY
)

add_library(quantMicrostructure
  ${PROJECT_BINARY_DIR}/generated/build_info.cpp
  src/feed/event_file.cpp
  src/feed/sequence_tracker.cpp
  src/book/order_book.cpp
  src/book/level_book.cpp
  src/book/level_aggregator.cpp
  src/matching/matching_engine.cpp
  src/sim/rng.cpp
  src/sim/zero_intelligence.cpp
  src/sim/hawkes.cpp
  src/sim/toy_models.cpp
  src/sim/queue_reactive.cpp
  src/sim/order_splitting.cpp
  src/measures/tape.cpp
  src/measures/spread.cpp
  src/measures/liquidity.cpp
  src/measures/volatility.cpp
  src/measures/flow.cpp
  src/replay/backtest.cpp
  src/impact/propagator.cpp
  src/impact/kyle_lambda.cpp
  src/impact/metaorder.cpp
  src/impact/round_trip.cpp
  src/execution/almgren_chriss.cpp
  src/execution/schedule.cpp
  src/execution/tca.cpp
  src/mm/quoting.cpp
  src/mm/market_maker.cpp
)

set_target_properties(quantMicrostructure PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(quantMicrostructure
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

qm_set_warnings(quantMicrostructure)

# ---- Tests (GoogleTest) ----
if(QM_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---- Benchmarks (google-benchmark) ----
if(QM_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

# ---- Python binding (pybind11) ----
if(QM_BUILD_PYTHON)
  add_subdirectory(bindings/python)
endif()
