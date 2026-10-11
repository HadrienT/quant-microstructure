# quant-microstructure

A study of market microstructure in C++20: how a price forms in a limit order
book, what a trade costs, and what quoting earns.

It is built around the three questions of the field:

| Question | What is built |
|---|---|
| How does the price form? | A limit order book and a price-time matching engine, order flow simulators (zero-intelligence, Hawkes, queue-reactive), measures of the stylised facts |
| What does a trade cost? | Market impact (response function, propagator, square-root law), optimal execution (Almgren-Chriss, TWAP, VWAP, POV), transaction cost analysis |
| What does quoting earn? | Market making (Avellaneda-Stoikov, Guéant-Lehalle-Fernandez-Tapia), markouts, a book-level backtest with latency and queue position |

**Status: specification only.** There is no code yet. The reading companion and
the work packages are written; the first work package (M0, the build skeleton)
has not started.

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

Not available yet: the build files arrive with work package M0. The intended
toolchain is the one `quant-backtest` uses (CMake presets, Ninja, vcpkg,
GoogleTest, google-benchmark, clang-format 18).

## License

MIT, see [`LICENSE`](LICENSE).
