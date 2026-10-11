find_package(benchmark CONFIG REQUIRED)

add_executable(quantMicrostructure_bench
  bench_skeleton.cpp
)
target_link_libraries(quantMicrostructure_bench
  PRIVATE
    quantMicrostructure
    benchmark::benchmark_main
)
qm_set_warnings(quantMicrostructure_bench)
