// The properties of docs/companion.md, section 14, that concern the book and
// the matching engine, checked after every command of random order flows.
#include <gtest/gtest.h>

#include "quantMicrostructure/book/level_aggregator.hpp"
#include "quantMicrostructure/book/level_book.hpp"
#include "quantMicrostructure/book/order_book.hpp"
#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/feed/sequence_tracker.hpp"
#include "quantMicrostructure/matching/matching_engine.hpp"

#include "support/random_flow.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace quantMicrostructure
{

    namespace
    {

        using testing::Command;
        using testing::CommandKind;

        constexpr std::size_t FlowLength = 20'000;

        struct Scenario
        {
            std::uint64_t seed;
            // Around 5 the book keeps emptying, which exercises the edges;
            // around 300 it is deep and queues are long.
            std::size_t target_orders;
        };

        constexpr Scenario Scenarios[] = {{1, 5}, {2, 5}, {3, 300}, {4, 300}, {2026, 40}};

        /// Runs every scenario and calls `check` after each command with the
        /// engine, the command, its result and the events it caused. `reset`
        /// is called before each scenario, `finish` after its last command.
        void forEachCommand(
            const std::function<void()> &reset,
            const std::function<void(const MatchingEngine &, const Command &, const SubmitResult &, std::span<const Event>)> &check,
            const std::function<void(const MatchingEngine &)> &finish = [](const MatchingEngine &) {})
        {
            for (const Scenario &scenario : Scenarios)
            {
                SCOPED_TRACE("seed " + std::to_string(scenario.seed) + ", target " + std::to_string(scenario.target_orders));
                reset();
                MatchingEngine engine;
                std::vector<Event> events;
                Timestamp ts = 0;
                for (const Command &command : testing::makeFlow(FlowLength, scenario.seed, scenario.target_orders))
                {
                    events.clear();
                    const SubmitResult result = testing::send(engine, command, ++ts, events);
                    check(engine, command, result, events);
                    if (::testing::Test::HasFatalFailure())
                    {
                        return;
                    }
                }
                finish(engine);
                if (::testing::Test::HasFatalFailure())
                {
                    return;
                }
            }
        }

        std::vector<Event> runFlow(const Scenario &scenario)
        {
            MatchingEngine engine;
            std::vector<Event> events;
            Timestamp ts = 0;
            for (const Command &command : testing::makeFlow(FlowLength, scenario.seed, scenario.target_orders))
            {
                testing::send(engine, command, ++ts, events);
            }
            return events;
        }

    } // namespace

    // The flows must actually trade, rest, cancel and empty the book, or the
    // properties below would hold for want of anything to check.
    TEST(BookProperties, FlowsExerciseTheEngine)
    {
        for (const Scenario &scenario : Scenarios)
        {
            SCOPED_TRACE("seed " + std::to_string(scenario.seed));
            const std::vector<Event> events = runFlow(scenario);
            const auto count = [&](EventType type)
            {
                return std::count_if(events.begin(), events.end(), [type](const Event &e)
                                     { return e.type == type; });
            };
            EXPECT_GT(count(EventType::Add), 1000);
            EXPECT_GT(count(EventType::Cancel), 1000);
            EXPECT_GT(count(EventType::Trade), 1000);
        }
    }

    // What an order asked for is what it traded, plus what was cancelled, plus
    // what still rests. And every lot bought was sold.
    TEST(BookProperties, Conservation)
    {
        struct Account
        {
            Side side{};
            Quantity initial{};
            Quantity traded{};
            Quantity cancelled{};
        };
        std::unordered_map<OrderId, Account> accounts;

        forEachCommand(
            [&]()
            { accounts.clear(); },
            [&](const MatchingEngine &, const Command &command, const SubmitResult &result, std::span<const Event> events)
            {
                Quantity traded_now = 0;
                for (const Event &e : events)
                {
                    if (e.type == EventType::Trade)
                    {
                        accounts.at(e.order_id).traded += e.qty;
                        traded_now += e.qty;
                    }
                    else if (e.type == EventType::Cancel)
                    {
                        accounts.at(e.order_id).cancelled += e.qty;
                    }
                }
                if (command.kind == CommandKind::Cancel)
                {
                    ASSERT_EQ(traded_now, 0);
                    return;
                }
                ASSERT_EQ(result.filled, traded_now);
                Account account;
                account.side = command.side;
                account.initial = command.qty;
                account.traded = result.filled;
                if (command.kind == CommandKind::Market)
                {
                    ASSERT_EQ(result.resting, 0);
                    account.cancelled = command.qty - result.filled;
                }
                else
                {
                    ASSERT_EQ(result.resting, command.qty - result.filled);
                }
                ASSERT_TRUE(accounts.emplace(command.id, account).second);
            },
            [&](const MatchingEngine &engine)
            {
                Quantity bought = 0;
                Quantity sold = 0;
                for (const auto &[id, account] : accounts)
                {
                    const auto resting = engine.book().find(id);
                    const Quantity resting_qty = resting ? resting->qty : 0;
                    ASSERT_EQ(account.initial, account.traded + account.cancelled + resting_qty) << "order " << id;
                    (account.side == Side::Buy ? bought : sold) += account.traded;
                }
                EXPECT_EQ(bought, sold);
                EXPECT_GT(bought, 0);
            });
    }

    TEST(BookProperties, NeverCrossed)
    {
        forEachCommand(
            []() {},
            [](const MatchingEngine &engine, const Command &, const SubmitResult &, std::span<const Event>)
            {
                const auto bid = engine.book().bestBid();
                const auto ask = engine.book().bestAsk();
                if (bid && ask)
                {
                    ASSERT_LT(*bid, *ask);
                }
            });
    }

    // Checked against a model that shares no code with OrderBook: the resting
    // orders in a plain vector, in the order of their Add events. An order may
    // trade only if no other order of its side has a better price, and none
    // that arrived before it has the same price.
    TEST(BookProperties, PriceTimePriority)
    {
        std::vector<OrderView> resting;
        std::size_t trades = 0;

        forEachCommand(
            [&]()
            { resting.clear(); },
            [&](const MatchingEngine &, const Command &, const SubmitResult &, std::span<const Event> events)
            {
                for (const Event &e : events)
                {
                    if (e.type == EventType::Add)
                    {
                        resting.push_back(OrderView{e.order_id, e.side, e.price, e.qty});
                        continue;
                    }
                    const auto traded = std::find_if(resting.begin(), resting.end(), [&e](const OrderView &o)
                                                     { return o.id == e.order_id; });
                    ASSERT_NE(traded, resting.end());
                    if (e.type == EventType::Trade)
                    {
                        ++trades;
                        ASSERT_EQ(traded->side, opposite(e.side));
                        ASSERT_EQ(traded->price, e.price);
                        for (auto other = resting.begin(); other != resting.end(); ++other)
                        {
                            if (other == traded || other->side != traded->side)
                            {
                                continue;
                            }
                            const bool better = traded->side == Side::Buy ? other->price > traded->price : other->price < traded->price;
                            ASSERT_FALSE(better) << "order " << other->id << " had a better price than " << traded->id;
                            if (other < traded)
                            {
                                ASSERT_NE(other->price, traded->price) << "order " << other->id << " was ahead of " << traded->id;
                            }
                        }
                    }
                    traded->qty -= e.qty;
                    ASSERT_GE(traded->qty, 0);
                    if (traded->qty == 0)
                    {
                        resting.erase(traded);
                    }
                }
            });
        EXPECT_GT(trades, 5000u);
    }

    // The engine can read back what it wrote: a book fed with its events is its
    // book, queue order included, and the stream has no gap.
    TEST(BookProperties, StreamRoundTrip)
    {
        OrderBook replica;
        SequenceTracker tracker;

        forEachCommand(
            [&]()
            {
                replica.clear();
                tracker = SequenceTracker{};
            },
            [&](const MatchingEngine &engine, const Command &, const SubmitResult &, std::span<const Event> events)
            {
                for (const Event &e : events)
                {
                    ASSERT_EQ(tracker.observe(e.seq), SequenceStatus::InOrder);
                    replica.apply(e);
                }
                ASSERT_EQ(replica.orders(Side::Buy), engine.book().orders(Side::Buy));
                ASSERT_EQ(replica.orders(Side::Sell), engine.book().orders(Side::Sell));
            });
    }

    // Aggregating the order-by-order book by level gives the book rebuilt from
    // the aggregated stream.
    TEST(BookProperties, AggregatedStreamRebuildsTheAggregatedBook)
    {
        LevelAggregator aggregator;
        LevelBook levels;
        SequenceTracker tracker;
        std::vector<Event> aggregated;

        forEachCommand(
            [&]()
            {
                aggregator = LevelAggregator{};
                levels.clear();
                tracker = SequenceTracker{};
            },
            [&](const MatchingEngine &engine, const Command &, const SubmitResult &, std::span<const Event> events)
            {
                aggregated.clear();
                for (const Event &e : events)
                {
                    aggregator.apply(e, aggregated);
                }
                for (const Event &e : aggregated)
                {
                    ASSERT_EQ(tracker.observe(e.seq), SequenceStatus::InOrder);
                    levels.apply(e);
                }
                ASSERT_EQ(levels.depth(Side::Buy), engine.book().depth(Side::Buy));
                ASSERT_EQ(levels.depth(Side::Sell), engine.book().depth(Side::Sell));
                ASSERT_EQ(levels.bestBid(), engine.book().bestBid());
                ASSERT_EQ(levels.bestAsk(), engine.book().bestAsk());
            });
    }

    TEST(BookProperties, Determinism)
    {
        const Scenario scenario{7, 100};
        const std::vector<Event> first = runFlow(scenario);
        EXPECT_EQ(first, runFlow(scenario));
        // And the seed matters: the comparison above is not between two empty
        // or constant streams.
        EXPECT_NE(first, runFlow(Scenario{8, 100}));
    }

} // namespace quantMicrostructure
