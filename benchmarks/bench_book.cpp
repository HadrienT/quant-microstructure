// Throughput and per-command latency of the matching engine, and replay speed
// of the two books, on the random flow of tests/support/random_flow.hpp.
// Results and their caveats are in benchmarks/RESULTS.md: rerun and update
// them whenever book/ or matching/ changes.
#include <benchmark/benchmark.h>

#include "quantMicrostructure/book/level_aggregator.hpp"
#include "quantMicrostructure/book/level_book.hpp"
#include "quantMicrostructure/book/order_book.hpp"
#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/matching/matching_engine.hpp"

#include "support/random_flow.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{

    using namespace quantMicrostructure;

    constexpr std::size_t Commands = 1'000'000;
    constexpr std::uint64_t Seed = 2026;
    // The book holds a few thousand orders over about 40 price levels.
    constexpr std::size_t TargetOrders = 5'000;

    const std::vector<testing::Command> &flow()
    {
        static const std::vector<testing::Command> f = testing::makeFlow(Commands, Seed, TargetOrders);
        return f;
    }

    /// The order-by-order stream the engine emits for the flow.
    const std::vector<Event> &orderEvents()
    {
        static const std::vector<Event> events = []
        {
            MatchingEngine engine;
            std::vector<Event> out;
            Timestamp ts = 0;
            for (const testing::Command &command : flow())
            {
                testing::send(engine, command, ++ts, out);
            }
            return out;
        }();
        return events;
    }

    /// The same market, aggregated by price level.
    const std::vector<Event> &levelEvents()
    {
        static const std::vector<Event> events = []
        {
            LevelAggregator aggregator;
            std::vector<Event> out;
            for (const Event &event : orderEvents())
            {
                aggregator.apply(event, out);
            }
            return out;
        }();
        return events;
    }

    double percentile(const std::vector<std::int64_t> &sorted, double p)
    {
        const std::size_t rank = static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1));
        return static_cast<double>(sorted[rank]);
    }

    void engineThroughput(benchmark::State &state)
    {
        const std::vector<testing::Command> &commands = flow();
        std::vector<Event> out;
        std::size_t events = 0;
        std::size_t resting = 0;
        for (auto _ : state)
        {
            MatchingEngine engine;
            Timestamp ts = 0;
            events = 0;
            for (const testing::Command &command : commands)
            {
                out.clear();
                testing::send(engine, command, ++ts, out);
                events += out.size();
            }
            resting = engine.book().orderCount();
            benchmark::DoNotOptimize(resting);
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(commands.size()));
        state.counters["events_per_command"] = static_cast<double>(events) / static_cast<double>(commands.size());
        state.counters["resting_orders_at_end"] = static_cast<double>(resting);
    }
    BENCHMARK(engineThroughput)->Unit(benchmark::kMillisecond);

    // Each command is timed on its own with the steady clock. Reading the clock
    // costs about as much as a command: `clock_ns` is the median of an empty
    // measurement, to be read next to the percentiles, which include it.
    void engineLatency(benchmark::State &state)
    {
        using Clock = std::chrono::steady_clock;
        const auto elapsed = [](Clock::time_point from, Clock::time_point to)
        { return std::chrono::duration_cast<std::chrono::nanoseconds>(to - from).count(); };

        const std::vector<testing::Command> &commands = flow();
        std::vector<Event> out;
        std::vector<std::int64_t> samples;
        std::vector<std::int64_t> empty;
        for (auto _ : state)
        {
            MatchingEngine engine;
            Timestamp ts = 0;
            for (const testing::Command &command : commands)
            {
                out.clear();
                const Clock::time_point start = Clock::now();
                testing::send(engine, command, ++ts, out);
                const Clock::time_point stop = Clock::now();
                samples.push_back(elapsed(start, stop));
            }
        }
        for (std::size_t i = 0; i < 100'000; ++i)
        {
            const Clock::time_point start = Clock::now();
            const Clock::time_point stop = Clock::now();
            empty.push_back(elapsed(start, stop));
        }
        std::sort(samples.begin(), samples.end());
        std::sort(empty.begin(), empty.end());
        state.counters["p50_ns"] = percentile(samples, 0.50);
        state.counters["p99_ns"] = percentile(samples, 0.99);
        state.counters["p99.9_ns"] = percentile(samples, 0.999);
        state.counters["max_ns"] = static_cast<double>(samples.back());
        state.counters["clock_ns"] = percentile(empty, 0.50);
    }
    BENCHMARK(engineLatency)->Unit(benchmark::kMillisecond);

    template <typename Book>
    void replay(benchmark::State &state, const std::vector<Event> &events)
    {
        for (auto _ : state)
        {
            Book book;
            for (const Event &event : events)
            {
                book.apply(event);
            }
            benchmark::DoNotOptimize(book.bestBid());
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(events.size()));
    }

    void orderBookReplay(benchmark::State &state)
    {
        replay<OrderBook>(state, orderEvents());
    }
    BENCHMARK(orderBookReplay)->Unit(benchmark::kMillisecond);

    void levelBookReplay(benchmark::State &state)
    {
        replay<LevelBook>(state, levelEvents());
    }
    BENCHMARK(levelBookReplay)->Unit(benchmark::kMillisecond);

} // namespace
