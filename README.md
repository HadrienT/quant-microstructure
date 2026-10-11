# quant-microstructure

A study of market microstructure in C++20: how a price forms in a limit order
book, what a trade costs, and what quoting earns.

It is built around the three questions of the field:

| Question | What is built |
|---|---|
| How does the price form? | A limit order book and a price-time matching engine, order flow simulators (zero-intelligence, Hawkes, queue-reactive), measures of the stylised facts |
| What does a trade cost? | Market impact (response function, propagator, square-root law), optimal execution (Almgren-Chriss, TWAP, VWAP, POV), transaction cost analysis |
| What does quoting earn? | Market making (Avellaneda-Stoikov, Guéant-Lehalle-Fernandez-Tapia), markouts, a book-level backtest with latency and queue position |

**Status: build skeleton only.** The toolchain, the tests, the benchmark
harness, the wheel and the CI are in place (work package M0); there is no
domain code yet. The next work package is M2, the order book and the matching
engine.

**This is a study repository, not a trading system.** It never connects to an
exchange to send an order.

## Where it sits

| Repository | Role |
|---|---|
| **quant-microstructure** (here) | Order book, matching engine, simulators, execution and market-making models, book-level backtest |
| [data-ingest](https://github.com/HadrienT/data-ingest) | Capture of the public order book feeds this repository replays |
| [quant-backtest](https://github.com/HadrienT/quant-backtest) | Daily-bar backtesting engine; a later consumer of the cost models calibrated here |
| [quant-modeling](https://github.com/HadrienT/quant-modeling) | Derivatives pricing library and website; no dependency either way |

## Documentation

| File | What it is |
|---|---|
| [`docs/companion.md`](docs/companion.md) | Reading companion and specification (in French): the theory with formulas and references, the architecture, the work packages, the property tests, the state of the data |
| [`blueprint/README.md`](blueprint/README.md) | Work packages and their dependency graph |
| [`blueprint/decisions.md`](blueprint/decisions.md) | Architecture decisions, with the alternatives rejected |
| [`CLAUDE.md`](CLAUDE.md) | Working notes for Claude Code sessions |

## Build

Requires CMake 3.25+, Ninja, a C++20 compiler, and `VCPKG_ROOT` pointing at a
vcpkg clone (dependencies are installed from `vcpkg.json` at configure time).

| Command | Effect |
|---|---|
| `scripts/make.sh` | Configure, build into `build/`, run all tests |
| `scripts/make_asan.sh` | Same with ASan and UBSan, into `build-asan/` |
| `scripts/bench.sh` | Build and run the benchmarks (google-benchmark) |
| `scripts/build_wheel.sh` | Build the `quantmicrostructure` wheel into `dist/` |
| `pytest` | Python tests, against the wheel installed in the current environment |
| `pre-commit run --all-files` | clang-format 18 and black |

## License

MIT, see [`LICENSE`](LICENSE).
