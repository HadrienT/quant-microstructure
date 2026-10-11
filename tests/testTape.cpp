#include <gtest/gtest.h>

#include "quantMicrostructure/book/level_aggregator.hpp"
#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/matching/matching_engine.hpp"
#include "quantMicrostructure/measures/tape.hpp"
#include "quantMicrostructure/sim/rng.hpp"
#include "quantMicrostructure/sim/toy_models.hpp"
#include "quantMicrostructure/sim/zero_intelligence.hpp"

#include <stdexcept>
#include <vector>

namespace quantMicrostructure
{

    namespace
    {

        std::vector<Event> aggregate(const std::vector<Event> &order_events)
        {
            LevelAggregator aggregator;
            std::vector<Event> out;
            for (const Event &event : order_events)
            {
                aggregator.apply(event, out);
            }
            return out;
        }

        std::vector<Event> smallMarket()
        {
            MatchingEngine engine;
            std::vector<Event> events;
            engine.submitLimit(1, Side::Buy, 99, 5, 10, events);
            engine.submitLimit(2, Side::Sell, 101, 3, 20, events);
            engine.submitLimit(3, Side::Sell, 102, 4, 30, events);
            // Walks through the whole first level and into the second.
            engine.submitMarket(Side::Buy, 5, 40, events);
            engine.cancel(1, 50, events);
            return events;
        }

    } // namespace

    TEST(Tape, RecordsEachChangeOfTheBestPrices)
    {
        const Tape tape = buildTape(smallMarket(), StreamKind::OrderByOrder);

        ASSERT_EQ(tape.quotes.size(), 5u);
        EXPECT_EQ(tape.quotes[0], (Quote{10, 99, 0, 5, 0}));
        EXPECT_FALSE(tape.quotes[0].twoSided());
        EXPECT_EQ(tape.quotes[1], (Quote{20, 99, 101, 5, 3}));
        EXPECT_TRUE(tape.quotes[1].twoSided());
        EXPECT_DOUBLE_EQ(tape.quotes[1].mid(), 100.0);
        EXPECT_DOUBLE_EQ(tape.quotes[1].spread(), 2.0);
        // The order at 102 is behind the best: no new quote at time 30.
        EXPECT_EQ(tape.quotes[2], (Quote{40, 99, 102, 5, 4}));
        EXPECT_EQ(tape.quotes[3], (Quote{40, 99, 102, 5, 2}));
        EXPECT_EQ(tape.quotes[4], (Quote{50, 0, 102, 0, 2}));
        EXPECT_EQ(tape.end, 50);
    }

    TEST(Tape, OneAggressiveOrderIsOneTrade)
    {
        const Tape tape = buildTape(smallMarket(), StreamKind::OrderByOrder);

        ASSERT_EQ(tape.trades.size(), 1u);
        const Trade &trade = tape.trades[0];
        EXPECT_EQ(trade.ts, 40);
        EXPECT_EQ(trade.side, Side::Buy);
        EXPECT_DOUBLE_EQ(trade.sign(), 1.0);
        EXPECT_EQ(trade.qty, 5);
        // 3 lots at 101 and 2 at 102.
        EXPECT_DOUBLE_EQ(trade.price, 101.4);
        // The quote in force before the order arrived, not one it produced.
        EXPECT_EQ(trade.quote, 1u);
    }

    TEST(Tape, TradeBeforeAnyQuoteHasNone)
    {
        Rng rng(1);
        const Tape tape = buildTape(simulateRoll(RollParams{2, 1, 100, 10}, 50, rng), StreamKind::TradesOnly);
        EXPECT_TRUE(tape.quotes.empty());
        ASSERT_EQ(tape.trades.size(), 50u);
        EXPECT_EQ(tape.trades[7].quote, Trade::NoQuote);
        EXPECT_EQ(tape.end, 490);
    }

    TEST(Tape, RejectsEventsOfAnotherKindOfStream)
    {
        const std::vector<Event> events = smallMarket();
        EXPECT_THROW(buildTape(events, StreamKind::Aggregated), std::invalid_argument);
        EXPECT_THROW(buildTape(events, StreamKind::TradesOnly), std::invalid_argument);
        EXPECT_THROW(buildTape(aggregate(events), StreamKind::OrderByOrder), std::invalid_argument);
    }

    // The order-by-order stream of a market and its aggregated stream tell the
    // same story at the best prices: same quotes, same trades.
    TEST(Tape, OrderByOrderAndAggregatedStreamsGiveTheSameTape)
    {
        ZeroIntelligenceParams params;
        params.limit_rate = 1.0;
        params.market_rate = 2.0;
        params.cancel_rate = 0.2;
        params.window = 20;
        params.initial_price = 10'000;
        ZeroIntelligenceSimulator sim(params, 2026);
        std::vector<Event> events;
        sim.runUntil(500'000'000'000, events);

        const Tape from_orders = buildTape(events, StreamKind::OrderByOrder);
        const Tape from_levels = buildTape(aggregate(events), StreamKind::Aggregated);
        EXPECT_GT(from_orders.quotes.size(), 1000u);
        EXPECT_GT(from_orders.trades.size(), 1000u);
        EXPECT_EQ(from_orders.quotes, from_levels.quotes);
        EXPECT_EQ(from_orders.trades, from_levels.trades);
        EXPECT_EQ(from_orders.end, from_levels.end);
    }

} // namespace quantMicrostructure
