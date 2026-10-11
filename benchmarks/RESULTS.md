# Benchmark results

Results of `scripts/bench.sh` ([`bench_book.cpp`](bench_book.cpp)). Rerun and
update this file whenever `book/` or `matching/` changes.

## Latest run

| | |
|---|---|
| Date | 2026-10-11 |
| Code | work package M2, first version of the book and the engine |
| Machine | Intel Xeon E5-2680 v4 @ 2.40 GHz, 56 logical cores, shared server |
| Compiler | g++ 14.2.0, preset `bench` (`Release`) |
| Command | `build-bench/benchmarks/quantMicrostructure_bench --benchmark_repetitions=5` |

Each figure is the median of 5 repetitions; the coefficient of variation
across repetitions is in the last column.

| Benchmark | What is measured | Result | Variation |
|---|---|---|---|
| `engineThroughput` | Commands through the matching engine, single thread | 9.05 M commands/s (110 ns per command) | 0.6 % |
| `engineLatency` | Time of one command, clock included: p50 | 127 ns | 0.6 % |
| | p99 | 562 ns | 0.7 % |
| | p99.9 | 861 ns | 1.4 % |
| | max | 82 µs | 29 % |
| | An empty measurement (two clock reads): median | 13 ns | 4 % |
| `orderBookReplay` | Order-by-order events applied to an `OrderBook` | 12.4 M events/s | 3.6 % |
| `levelBookReplay` | Aggregated events applied to a `LevelBook` | 37.2 M events/s | 3.9 % |

## The flow

One million commands from `makeFlow(1'000'000, seed 2026, target 5'000)`
([`tests/support/random_flow.hpp`](../tests/support/random_flow.hpp)): 5 %
market orders, the rest limit orders and cancels, prices spread over 41 ticks
around a fixed centre. The engine emits 0.94 order-by-order event per command;
the book holds 2,488 orders at the end of the run.

## How to read these figures

- **The flow is not a market.** It has no model behind it: 41 price levels, a
  centre that never moves, uniform sizes. A real feed has more levels, a moving
  price and bursts. These figures say how the data structure behaves on this
  flow, nothing about a real one. Replaying a captured feed is work package M1
  onwards.
- **A command is a function call.** There is no network, no parsing, no
  queue between threads: this is the cost of the book and the engine alone.
- **Timing a command costs about as much as the command.** With the two clock
  reads, the loop takes 161 ns per command instead of 110 ns. The percentiles
  include that cost; the p50 is therefore an upper bound, and
  `engineThroughput` is the better estimate of the typical cost.
- **The maximum is not a property of the code.** It varies by 29 % between
  repetitions: on a shared machine with no core pinned it mostly measures
  interruptions.
- **The machine was not quiet.** Load average 2.5 on 56 cores, CPU frequency
  scaling enabled, no core isolation. No other build was running.
- **What makes the tail (p99, p99.9) has not been investigated.**
