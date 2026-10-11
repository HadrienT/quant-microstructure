#include <gtest/gtest.h>

#include "quantMicrostructure/book/order_book.hpp"
#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/feed/sequence_tracker.hpp"
#include "quantMicrostructure/sim/zero_intelligence.hpp"

#include "support/statistics.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace quantMicrostructure
{

    namespace
    {

        constexpr Timestamp Second = 1'000'000'000;

        // One limit order per second and per level, each living 10 seconds on
        // average: 10 orders per level far from the best prices. Four market
        // orders per second in all.
        ZeroIntelligenceParams params()
        {
            ZeroIntelligenceParams p;
            p.limit_rate = 1.0;
            p.market_rate = 2.0;
            p.cancel_rate = 0.1;
            p.order_size = 1;
            p.window = 60;
            p.initial_price = 10'000;
            return p;
        }

    } // namespace

    TEST(ZeroIntelligence, RejectsInvalidParameters)
    {
        ZeroIntelligenceParams p = params();
        p.cancel_rate = -1.0;
        EXPECT_THROW(ZeroIntelligenceSimulator(p, 1), std::invalid_argument);
        p = params();
        p.window = 0;
        EXPECT_THROW(ZeroIntelligenceSimulator(p, 1), std::invalid_argument);
        p = params();
        p.limit_rate = 0.0;
        p.market_rate = 0.0;
        EXPECT_THROW(ZeroIntelligenceSimulator(p, 1), std::invalid_argument);
    }

    TEST(ZeroIntelligence, SameSeedSameStream)
    {
        std::vector<Event> first;
        std::vector<Event> second;
        std::vector<Event> other;
        ZeroIntelligenceSimulator(params(), 7).runUntil(50 * Second, first);
        ZeroIntelligenceSimulator(params(), 7).runUntil(50 * Second, second);
        ZeroIntelligenceSimulator(params(), 8).runUntil(50 * Second, other);
        EXPECT_GT(first.size(), 1000u);
        EXPECT_EQ(first, second);
        EXPECT_NE(first, other);
    }

    // The stream is a valid order-by-order stream: no gap, time never goes
    // back, the book rebuilt from it is the simulator's and is never crossed.
    TEST(ZeroIntelligence, StreamRebuildsAnUncrossedBook)
    {
        ZeroIntelligenceSimulator sim(params(), 3);
        OrderBook replica;
        SequenceTracker tracker;
        std::vector<Event> events;
        Timestamp last = 0;
        for (int i = 0; i < 30'000; ++i)
        {
            events.clear();
            sim.step(events);
            for (const Event &e : events)
            {
                ASSERT_EQ(tracker.observe(e.seq), SequenceStatus::InOrder);
                ASSERT_GE(e.ts, last);
                last = e.ts;
                replica.apply(e);
            }
            const auto bid = sim.book().bestBid();
            const auto ask = sim.book().bestAsk();
            if (bid && ask)
            {
                ASSERT_LT(*bid, *ask);
            }
        }
        EXPECT_EQ(replica.orders(Side::Buy), sim.book().orders(Side::Buy));
        EXPECT_EQ(replica.orders(Side::Sell), sim.book().orders(Side::Sell));
    }

    TEST(ZeroIntelligence, RunUntilStopsAtTheGivenTime)
    {
        ZeroIntelligenceSimulator sim(params(), 4);
        std::vector<Event> events;
        sim.runUntil(10 * Second, events);
        EXPECT_EQ(sim.now(), 10 * Second);
        ASSERT_FALSE(events.empty());
        EXPECT_LE(events.back().ts, 10 * Second);
        const std::size_t before = events.size();
        sim.runUntil(20 * Second, events);
        EXPECT_GT(events.size(), before);
        EXPECT_GE(events[before].ts, 10 * Second);
    }

    // Market orders are a Poisson flow: over T seconds, 2 * market_rate * T of
    // them, each trading against exactly one resting order of the same size.
    TEST(ZeroIntelligence, MarketOrdersArriveAtTheirRate)
    {
        constexpr double Seconds = 4'000.0;
        ZeroIntelligenceSimulator sim(params(), 2026);
        std::vector<Event> events;
        sim.runUntil(static_cast<Timestamp>(Seconds) * Second, events);

        const auto trades = std::count_if(events.begin(), events.end(), [](const Event &e)
                                          { return e.type == EventType::Trade; });
        const double expected = 2.0 * params().market_rate * Seconds;
        EXPECT_NEAR(static_cast<double>(trades), expected, 4.0 * std::sqrt(expected));
    }

    // Far from the best prices, where market orders do not reach, a level
    // gains orders at `limit_rate` and each leaves at `cancel_rate`: its size
    // is Poisson of mean limit_rate / cancel_rate. This is the asymptotic
    // depth alpha / delta of Smith et al. (2003), section II.A, which the
    // article gives as exact.
    TEST(ZeroIntelligence, DepthFarFromTheBestIsLimitRateOverCancelRate)
    {
        const ZeroIntelligenceParams p = params();
        ZeroIntelligenceSimulator sim(p, 2026);
        std::vector<Event> events;
        // Let the book fill: ten lifetimes of an order.
        sim.runUntil(100 * Second, events);

        std::vector<double> depths;
        for (int sample = 1; sample <= 3000; ++sample)
        {
            events.clear();
            sim.runUntil((100 + sample) * Second, events);
            const auto bid = sim.book().bestBid();
            const auto ask = sim.book().bestAsk();
            ASSERT_TRUE(bid && ask);
            // Levels 20 to 40 ticks behind the opposite best: well inside the
            // window of 60, and far behind the best price of their own side.
            for (Price distance = 20; distance <= 40; ++distance)
            {
                depths.push_back(static_cast<double>(sim.book().levelQuantity(Side::Buy, *ask - distance)));
                depths.push_back(static_cast<double>(sim.book().levelQuantity(Side::Sell, *bid + distance)));
            }
        }
        const double expected = p.limit_rate / p.cancel_rate;
        EXPECT_NEAR(testing::mean(depths), expected, 0.03 * expected);
        // Poisson: the variance equals the mean.
        EXPECT_NEAR(testing::variance(depths), expected, 0.10 * expected);
    }

} // namespace quantMicrostructure
