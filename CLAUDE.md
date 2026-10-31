# quant-microstructure — notes for Claude Code

A study of **market microstructure** in **C++20**: order book and matching engine, flow
simulators, market impact, optimal execution, market making, book-level backtest. Exposed to
Python by a pybind11 wheel `quantmicrostructure`, for analysis and charts.

It is a declared **learning project**: the maintainer studied derivatives then xVA
(`quant-modeling`), then backtesting (`quant-backtest`), and opens the order book side here. The
repository serves understanding as much as production: the reading companion matters as much as
the code.

**This repository is not a trading system.** No connection to a venue to place an order, no
trading API key, anywhere. Never add one.

## Repository status

**All work packages of this repository (M0, M2 to M8) are delivered.** The capture (M1, in
`data-ingest`) now records Bitstamp's BTC/USD order-by-order feed, but nothing is calibrated on
it yet. The repository was created on October 11, 2026. Update this section with each delivered
work package.

| Work package | Status |
|---|---|
| M0 — Foundations | delivered: build chain, tests, benchmark, wheel, CI |
| M2 — Book and engine | delivered: `core/` (integer types, event format), `feed/` (event file, gap detection), `book/` (L3 book, L2 book, L3 to L2 aggregation), `matching/` (price-time engine) |
| M4 — Simulators | delivered: `sim/` (random generator, zero intelligence, univariate Hawkes with maximum likelihood and time change, queue-reactive, Roll, Kyle, Glosten-Milgrom). Deviations from the papers and unreviewed formulas: ADR-009 |
| M3 — Measures | delivered: `measures/` (tape of quotes and trades, quoted, effective, realized spreads, Roll, imbalance, depth, realized variance, signature plot, two scales, sign correlation, response function, trade sign) and **Python bindings** for all of the above. What is not done: ADR-010 |
| M5 — Book backtest | delivered: `replay/` (strategy, gateway, latency, queue, fees; `runReplay` on an L2 or L3 stream, `runSimulation` in zero intelligence). Assumptions: ADR-011. Exposed to Python through execution (`execute_schedule`) and market making (`market_maker_replay`, `market_maker_simulation`); a strategy remains a C++ class |
| M6 — Impact | delivered: `impact/` (propagator, Kyle's lambda, metaorder impact in the simulator, round-trip cost) and `sim/order_splitting` (long-memory signs). **The square-root law is not reproduced** by zero intelligence: ADR-012 |
| M7 — Execution | delivered: `execution/` (discrete Almgren-Chriss, TWAP, VWAP, POV, schedule executor in the backtest, implementation shortfall). Limits: ADR-013 |
| M8 — Market making | delivered: `mm/` (Avellaneda-Stoikov, exact and approximate Guéant-Lehalle-Fernandez-Tapia, market maker for the backtest, P&L decomposition, markouts). What the result is worth: ADR-014 |
| M1 — Capture | **running, in `data-ingest`**: Bitstamp BTC/USD. Raw events in fixed-size binary files, in the format of ADR-007; catalogue and aggregates computed from the files (ADR-015). It unblocks everything else: measures, calibration, backtest on data |

Three findings not to forget, recorded in the ADRs and issues: the square-root law is not
reproduced by zero intelligence, so the repository does not verify it (§5.4 of the companion,
ADR-012); several formulas were coded from a derivation, for lack of access to the paper (#16);
a market maker's P&L in zero intelligence says nothing about a real market (ADR-014).

Where to read the code, in order: `core/event.hpp` (the format, ADR-007), `book/order_book.hpp`
(the structure, ADR-008), `matching/matching_engine.hpp`, then `tests/testBookProperties.cpp` (the
properties of §14). For the simulators: `sim/toy_models.hpp` (the three models of §4, with their
derivation), `sim/zero_intelligence.hpp`, `sim/hawkes.hpp`, `sim/queue_reactive.hpp`.

For the measures: `measures/tape.hpp` first (ADR-010), then one file per family. On the Python
side, everything is in `bindings/python/module.cpp`; the tests in `tests/python/` show how to chain
simulator, tape and measure.

For the backtest: `replay/backtest.hpp`, whose comments carry the assumptions (ADR-011), then
`tests/testBacktest.cpp`, which walks through them one by one on hand-written markets.

`sim/`, `measures/`, `replay/`, `impact/`, `execution/` and `mm/` use `double` (rates, time in
seconds, Kyle values): the ban only applies to `core/`, `book/` and `matching/`. Every simulator
takes a seed and goes through `sim/rng.hpp`, never through the `<random>` distributions.

## Where to start

| File | Role |
|---|---|
| [`docs/companion.md`](docs/companion.md) | Reading companion **and** specification: the theory (§1 to §10), the architecture (§11), what the repository is not (§12), the work packages (§13), the property tests (§14), the state of the data (§15), the bibliography (§17) |
| [`blueprint/README.md`](blueprint/README.md) | The work packages and their dependency graph: consult it before starting a work package |
| [`blueprint/decisions.md`](blueprint/decisions.md) | Architecture decisions, with the alternatives rejected, and the decisions **still open** |

## Relationship with the other repositories

| Repository | Role relative to this one |
|---|---|
| `~/data-ingest` | **Capture of book feeds** (work package M1). This repository reads what it records; it opens no network connection itself |
| `~/quant-backtest` | Backtest engine on daily data. No dependency today. Serves as a **model for the build chain** (CMake, presets, vcpkg, scripts, CI). Later, a possible consumer of the cost models calibrated here |
| `~/quant-modeling` | Pricing library and website. No dependency, in either direction |
| `~/quant-platform` | Kafka, audit, observability. This repository emits nothing there |

Boundary rule: **never edit a neighboring repository from a session opened here.** A change
touching two repositories means one issue in each, referencing each other.

## Non-negotiable architecture principles

1. **Prices, quantities and times are integers**: ticks, lots, nanoseconds. No `double` in
   `core/`, `book/` or `matching/`.
2. **Everything is an event stream, in a single format**: a market recording, a simulator's output
   and the matching engine's output are read the same way.
3. **The strategy is separate from the execution venue**: the same one runs on a replay and in a
   simulator.
4. **No network here.** The capture is in `data-ingest`; here we read event files.
5. **Determinism**: same stream and same seed, same outputs bit for bit.
6. **No future**: in the replay, nothing happening after $t$ can change a decision or an execution
   up to $t$.
7. **A book backtest declares its assumptions**: latency, queue position, impact of its own orders,
   fees. The result is shown with its sensitivity to latency and the queue assumption (§10 of the
   companion).
8. **Performance is measured.** No speed claim without the benchmark backing it, kept up to date in
   the repository.
9. **Stay close to the sources**: names, formulas and sign conventions are those of the references
   of §17, first of all Bouchaud et al. (2018) and Cartea, Jaimungal & Penalva (2015). The formula
   is **rechecked in the paper** before being coded, and the equation cited in the comment.
10. **A model declares its regime** (large tick or small tick) and what it assumes about the flow.
11. **Every figure shown is defensible**: it carries its convention, its uncertainty, and the limit
    of the data (a crypto market is not an equity market; this server is not colocated).

## Commands

`VCPKG_ROOT` must point to a vcpkg clone; dependencies are installed by the
[`vcpkg.json`](vcpkg.json) manifest at configure time.

| Command | Effect |
|---|---|
| `scripts/make.sh` | Configures (preset `default`), builds into `build/`, runs all tests |
| `scripts/make_asan.sh` | Same with ASan and UBSan, into `build-asan/` |
| `cmake --preset default && cmake --build build` | Build without running the tests |
| `ctest --test-dir build --output-on-failure` | All tests |
| `ctest --test-dir build -R BuildInfo` | Tests whose name contains a pattern |
| `scripts/bench.sh` | Builds (preset `bench`, into `build-bench/`) and runs the benchmark; arguments go to google-benchmark, for example `--benchmark_filter=...` |
| `scripts/build_wheel.sh` | `quantmicrostructure` wheel in `dist/` (without vcpkg: the library has no third-party dependency) |
| `pytest` | Python tests, on the wheel installed in the current environment (rebuild and reinstall it after a C++ change: `uv pip install --force-reinstall dist/*.whl`) |
| `pre-commit run --all-files` | `clang-format` 18 and `black` |

Scripts bound the build and tests to `NPROC` processes (16 by default, 8 under ASan):
`NPROC=4 scripts/make.sh`.

CMake targets: `quantMicrostructure` (the library), `quantMicrostructure_tests`,
`quantMicrostructure_bench`, and `quantmicrostructure` (the Python module). Options:
`QM_BUILD_TESTS`, `QM_BUILD_BENCHMARKS`, `QM_BUILD_PYTHON`. A new source file or test is added by
hand to its target's list.

Tests and the benchmark draw their order flows from
[`tests/support/random_flow.hpp`](tests/support/random_flow.hpp): a random flow without a model,
made to stress the book, not to resemble a market. Statistical tests (means, Kolmogorov-Smirnov
test) go through [`tests/support/statistics.hpp`](tests/support/statistics.hpp), with a fixed seed
and tolerances of four to five standard deviations.

Benchmark results are in [`benchmarks/RESULTS.md`](benchmarks/RESULTS.md), with their limits. **Rerun
it and update this file with each change to `book/` or `matching/`**
(`scripts/bench.sh --benchmark_repetitions=5`), after checking that no other build is running.

## Tracking work: GitHub Issues

`gh` is authenticated (account `HadrienT`).

- **At the start of a session**: `gh issue list --state open`.
- **Issue handled** → `gh issue close <n> --comment "done in <sha>"`.
- **Never** a markdown handoff file between sessions. A task that outlives the session is an issue.
- Design lives in `docs/` and `blueprint/`; the issue points to it, it does not replace it.

## Conventions

- **Language: English by default.** Everything you produce is in English: code, identifiers, comments, commit messages, PRs, issues, documentation and interface text. The only exception is the conversation: reply in the language the maintainer writes in, so in French when they write in French. `docs/` and `blueprint/` are historically in French: new content there is in English too.
- **Commits: go through a branch, never directly on `main`** (the initial documentation commit is
  the only exception). End messages with the session's `Co-Authored-By` line.
- **C++**: `clang-format --style=file`, **version 18**; the `.clang-format` is that of
  `quant-modeling`. **Python**: `black`. The pre-commit hook must be installed on each clone:
  `pre-commit install -t pre-commit`.
- **Memory ownership**: prefer `unique_ptr` and non-owning references.
- **Every module comes with its test**, preferably a *property* test (conservation, price-time
  priority, stream round trip, closed formula recovered on a simulated model) rather than a numeric
  equality. The list is in §14 of the companion.
- **Declarative dependency management**: everything goes through `vcpkg.json` and the CMake presets;
  a fresh clone builds with no manual step.
- **The server is shared** with other projects: bound the number of threads of tests and benchmarks,
  and do not run a latency benchmark while another build is running (the figure would be worthless).
