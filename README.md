# quant-microstructure

A study of market microstructure in C++20: how a price forms in a limit order
book, what a trade costs, and what quoting earns.

It is built around the three questions of the field:

| Question | What is built |
|---|---|
| How does the price form? | A limit order book and a price-time matching engine, order flow simulators (zero-intelligence, Hawkes, queue-reactive), measures of the stylised facts |
| What does a trade cost? | Market impact (response function, propagator, square-root law), optimal execution (Almgren-Chriss, TWAP, VWAP, POV), transaction cost analysis |
| What does quoting earn? | Market making (Avellaneda-Stoikov, Guéant-Lehalle-Fernandez-Tapia), markouts, a book-level backtest with latency and queue position |

## What is built

- integer types and the single event format, an event file reader and writer
  with gap detection;
- an order-by-order (L3) book, a price-level (L2) book, and a price-time
  matching engine, with property tests and a first
  [benchmark](benchmarks/RESULTS.md);
- simulators: zero-intelligence, Hawkes (simulation, maximum likelihood,
  time-change validation), queue-reactive, and the Roll, Kyle and
  Glosten-Milgrom models, each tested against its closed formula;
- measures: quoted, effective and realised spreads, Roll's estimator,
  imbalance, realised variance, signature plot, two-scale estimator, sign
  correlation, response function, trade signing;
- a book-level backtest: one strategy interface, run against a recorded
  stream (latency, queue position, fees) or inside the simulator, where the
  market reacts;
- market impact: the propagator model and its fit, Kyle's lambda, metaorders
  injected into the simulator, round-trip costs;
- optimal execution: Almgren-Chriss trajectories and efficient frontier,
  TWAP, VWAP and POV schedules, carried out in the backtest, and
  implementation shortfall;
- market making: Avellaneda-Stoikov and Guéant-Lehalle-Fernandez-Tapia
  quotes, a market maker for the backtest, P&L decomposition and markouts;
- all of it available from Python.

## Status

**Every model is tested on simulated markets, and no parameter is calibrated
yet.** `data-ingest` has been recording Bitstamp's BTC/USD order-by-order feed
since October 2026 in the format this library replays; calibrating on it is the
next step. One expected result is **not** obtained: the zero-intelligence
simulator does not reproduce the square-root law of metaorder impact (see
ADR-012 in [`blueprint/decisions.md`](blueprint/decisions.md)).

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

## From Python

```python
import quantmicrostructure as qm

# A zero-intelligence market: rates in orders per second, duration in seconds.
market = qm.simulate_zero_intelligence(
    limit_rate=2.0, market_rate=1.5, cancel_rate=0.2, window=10,
    duration=2000.0, seed=2026,
)
tape = qm.build_tape(market)              # quotes and trades
qm.spread_decomposition(tape, horizon=1.0)  # effective = realized + impact
qm.imbalance_next_move(tape, bins=5)["probability_up"]
qm.response_function(tape, max_lag=50)

# A market maker on a recording: the result comes with its assumptions, and
# is to be shown for several latencies and both queue models.
recording = qm.aggregate(market)
for latency in (0.0, 0.05, 2.0):
    for queue_model in ("pessimistic", "pro_rata"):
        report = qm.market_maker_replay(
            recording, sigma=0.3, gamma=0.5, k=4.0, A=0.2,
            latency=latency, queue_model=queue_model,
        )
        print(report["assumptions"], report["pnl"], report["decomposition"])
```

Prices are in ticks, quantities in lots, timestamps in nanoseconds; the
Python functions take durations in seconds. These figures come from a
simulator, not from a market. In particular, quoting at the best price is
almost riskless in the zero-intelligence simulator, whose flow is never
informed: that P&L says nothing about a real market.

## License

MIT, see [`LICENSE`](LICENSE).
