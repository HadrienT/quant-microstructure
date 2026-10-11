find_package(benchmark CONFIG REQUIRED)

add_executable(quantMicrostructure_bench
  bench_book.cpp
)
target_link_libraries(quantMicrostructure_bench
  PRIVATE
    quantMicrostructure
    benchmark::benchmark_main
)
# The random flow of the tests.
target_include_directories(quantMicrostructure_bench PRIVATE ${PROJECT_SOURCE_DIR}/tests)
qm_set_warnings(quantMicrostructure_bench)
