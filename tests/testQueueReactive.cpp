#include <gtest/gtest.h>

#include "quantMicrostructure/book/level_book.hpp"
#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/feed/sequence_tracker.hpp"
#include "quantMicrostructure/sim/queue_reactive.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace quantMicrostructure
{

    namespace
    {

        constexpr Timestamp Second = 1'000'000'000;
        constexpr std::size_t MaxSize = 15;

        // A queue that fills at a constant rate, is cancelled in proportion to
        // its size, and is hit by market orders only if it is the nearest.
        QueueIntensities queue(double limit, double cancel_per_unit, double market)
        {
            QueueIntensities q;
            for (std::size_t n = 0; n <= MaxSize; ++n)
            {
                q.limit.push_back(n < MaxSize ? limit : 0.0);
                q.cancel.push_back(cancel_per_unit * static_cast<double>(n));
                q.market.push_back(n > 0 ? market : 0.0);
            }
            return q;
        }

        QueueReactiveParams params(double theta, double theta_reinit)
        {
            QueueReactiveParams p;
            p.queues = {queue(1.0, 0.1, 0.6), queue(1.2, 0.2, 0.0), queue(1.0, 0.25, 0.0)};
            p.theta = theta;
            p.theta_reinit = theta_reinit;
            p.event_size = 5;
            p.initial_reference = 10'000;
            return p;
        }

    } // namespace

    TEST(QueueInvariantDistribution, SumsToOne)
    {
        const std::vector<double> pi = queueInvariantDistribution(queue(1.0, 0.1, 0.6));
        ASSERT_EQ(pi.size(), MaxSize + 1);
        double sum = 0.0;
        for (const double p : pi)
        {
            EXPECT_GT(p, 0.0);
            sum += p;
        }
        EXPECT_NEAR(sum, 1.0, 1e-12);
    }

    // Constant arrival and departure rates: the M/M/1 queue with a finite
    // room, whose stationary law is geometric in rho = arrival / departure.
    TEST(QueueInvariantDistribution, ConstantRatesGiveAGeometricLaw)
    {
        QueueIntensities q;
        q.limit = {2.0, 2.0, 2.0, 0.0};
        q.cancel = {0.0, 1.0, 1.0, 1.0};
        q.market = {0.0, 3.0, 3.0, 3.0};
        const std::vector<double> pi = queueInvariantDistribution(q);
        const double rho = 0.5;
        const double norm = 1.0 + rho + rho * rho + rho * rho * rho;
        for (std::size_t n = 0; n < pi.size(); ++n)
        {
            EXPECT_NEAR(pi[n], std::pow(rho, static_cast<double>(n)) / norm, 1e-12);
        }
    }

    // It satisfies the balance equations of the birth and death process: the
    // flow from n to n + 1 equals the flow back.
    TEST(QueueInvariantDistribution, SatisfiesDetailedBalance)
    {
        const QueueIntensities q = queue(1.2, 0.2, 0.3);
        const std::vector<double> pi = queueInvariantDistribution(q);
        for (std::size_t n = 0; n + 1 < pi.size(); ++n)
        {
            EXPECT_NEAR(pi[n] * q.limit[n], pi[n + 1] * (q.cancel[n + 1] + q.market[n + 1]), 1e-12);
        }
    }

    TEST(QueueInvariantDistribution, RejectsInvalidTables)
    {
        QueueIntensities q = queue(1.0, 0.1, 0.6);
        q.limit.back() = 1.0;
        EXPECT_THROW(queueInvariantDistribution(q), std::invalid_argument);
        q = queue(1.0, 0.1, 0.6);
        q.market.pop_back();
        EXPECT_THROW(queueInvariantDistribution(q), std::invalid_argument);
        q = queue(1.0, 0.1, 0.6);
        q.cancel[0] = 0.5;
        EXPECT_THROW(queueInvariantDistribution(q), std::invalid_argument);
        // Size 1 can be reached and never left.
        q = queue(1.0, 0.0, 0.0);
        EXPECT_THROW(queueInvariantDistribution(q), std::invalid_argument);
    }

    TEST(QueueReactive, RejectsInvalidParameters)
    {
        QueueReactiveParams p = params(0.5, 0.5);
        p.theta = 1.5;
        EXPECT_THROW(QueueReactiveSimulator(p, 1), std::invalid_argument);
        p = params(0.5, 0.5);
        p.queues.clear();
        EXPECT_THROW(QueueReactiveSimulator(p, 1), std::invalid_argument);
        p = params(0.5, 0.5);
        p.event_size = 0;
        EXPECT_THROW(QueueReactiveSimulator(p, 1), std::invalid_argument);
    }

    // Model I: with a fixed reference price each queue is a birth and death
    // process on its own, and the share of time it spends at each size is the
    // stationary distribution of Huang, Lehalle & Rosenbaum (2015), section
    // 2.3.3.
    TEST(QueueReactive, FixedReferenceQueuesFollowTheirInvariantDistribution)
    {
        const QueueReactiveParams p = params(0.0, 0.0);
        QueueReactiveSimulator sim(p, 2026);
        std::vector<Event> events;

        std::vector<std::vector<double>> time_at(p.queues.size(), std::vector<double>(MaxSize + 1, 0.0));
        double total = 0.0;
        for (int i = 0; i < 2'000'000; ++i)
        {
            const Timestamp before = sim.now();
            std::vector<std::int64_t> sizes;
            for (std::size_t q = 1; q <= sim.depth(); ++q)
            {
                sizes.push_back(sim.queueSize(Side::Buy, q));
                sizes.push_back(sim.queueSize(Side::Sell, q));
            }
            events.clear();
            sim.step(events);
            const double held = static_cast<double>(sim.now() - before);
            for (std::size_t q = 0; q < sim.depth(); ++q)
            {
                time_at[q][static_cast<std::size_t>(sizes[2 * q])] += held;
                time_at[q][static_cast<std::size_t>(sizes[2 * q + 1])] += held;
            }
            total += 2.0 * held;
        }
        EXPECT_EQ(sim.referenceMoves(), 0u);
        EXPECT_EQ(sim.reference(), p.initial_reference);

        for (std::size_t q = 0; q < p.queues.size(); ++q)
        {
            const std::vector<double> pi = queueInvariantDistribution(p.queues[q]);
            for (std::size_t n = 0; n <= MaxSize; ++n)
            {
                EXPECT_NEAR(time_at[q][n] / total, pi[n], 0.005) << "queue " << q + 1 << ", size " << n;
            }
        }
    }

    // Model III: the stream is a valid aggregated stream. A book rebuilt from
    // it shows, at every step, exactly the queues of the simulator, and is
    // never crossed, including through moves of the reference price and
    // redraws of the whole book.
    TEST(QueueReactive, StreamRebuildsTheQueues)
    {
        for (const double theta_reinit : {0.0, 0.3, 1.0})
        {
            SCOPED_TRACE("theta_reinit " + std::to_string(theta_reinit));
            const QueueReactiveParams p = params(0.7, theta_reinit);
            QueueReactiveSimulator sim(p, 11);
            LevelBook book;
            SequenceTracker tracker;
            std::vector<Event> events;
            sim.snapshot(events);
            Timestamp last = 0;
            for (int i = 0; i < 100'000; ++i)
            {
                for (const Event &e : events)
                {
                    ASSERT_EQ(tracker.observe(e.seq), SequenceStatus::InOrder);
                    ASSERT_GE(e.ts, last);
                    last = e.ts;
                    book.apply(e);
                    const auto bid = book.bestBid();
                    const auto ask = book.bestAsk();
                    if (bid && ask)
                    {
                        ASSERT_LT(*bid, *ask);
                    }
                }
                Quantity bid_total = 0;
                Quantity ask_total = 0;
                for (std::size_t q = 1; q <= sim.depth(); ++q)
                {
                    const Quantity bid_qty = sim.queueSize(Side::Buy, q) * p.event_size;
                    const Quantity ask_qty = sim.queueSize(Side::Sell, q) * p.event_size;
                    ASSERT_EQ(book.levelQuantity(Side::Buy, sim.queuePrice(Side::Buy, q)), bid_qty);
                    ASSERT_EQ(book.levelQuantity(Side::Sell, sim.queuePrice(Side::Sell, q)), ask_qty);
                    bid_total += bid_qty;
                    ask_total += ask_qty;
                }
                // Nothing is left in the book outside the window.
                Quantity book_bid = 0;
                Quantity book_ask = 0;
                for (const BookLevel &level : book.depth(Side::Buy))
                {
                    book_bid += level.qty;
                }
                for (const BookLevel &level : book.depth(Side::Sell))
                {
                    book_ask += level.qty;
                }
                ASSERT_EQ(book_bid, bid_total);
                ASSERT_EQ(book_ask, ask_total);

                events.clear();
                sim.step(events);
            }
            EXPECT_GT(sim.referenceMoves(), 100u);
        }
    }

    // A trade is published before the level it changes, at the price of a
    // queue of the side opposite to the aggressor, for one event size.
    TEST(QueueReactive, TradesHitTheNearestQueue)
    {
        const QueueReactiveParams p = params(0.7, 0.3);
        QueueReactiveSimulator sim(p, 5);
        std::vector<Event> events;
        std::size_t trades = 0;
        for (int i = 0; i < 50'000; ++i)
        {
            const Price bid_price = sim.queuePrice(Side::Buy, 1);
            const Price ask_price = sim.queuePrice(Side::Sell, 1);
            events.clear();
            sim.step(events);
            for (std::size_t k = 0; k < events.size(); ++k)
            {
                if (events[k].type != EventType::Trade)
                {
                    continue;
                }
                ++trades;
                ASSERT_EQ(k, 0u);
                ASSERT_EQ(events[k].qty, p.event_size);
                ASSERT_EQ(events[k].price, events[k].side == Side::Buy ? ask_price : bid_price);
                ASSERT_GT(events.size(), 1u);
                ASSERT_EQ(events[1].type, EventType::Level);
                ASSERT_EQ(events[1].price, events[k].price);
                ASSERT_EQ(events[1].side, opposite(events[k].side));
            }
        }
        EXPECT_GT(trades, 1000u);
    }

    // The more often the reference price follows the mid price, the more it
    // moves: theta sets the volatility of the model (section 3.1.2).
    TEST(QueueReactive, ThetaDrivesTheReferencePrice)
    {
        std::vector<std::uint64_t> moves;
        for (const double theta : {0.0, 0.3, 1.0})
        {
            QueueReactiveSimulator sim(params(theta, 0.0), 2026);
            std::vector<Event> events;
            sim.runUntil(20'000 * Second, events);
            moves.push_back(sim.referenceMoves());
        }
        EXPECT_EQ(moves[0], 0u);
        EXPECT_GT(moves[1], 0u);
        EXPECT_GT(moves[2], moves[1]);
    }

    TEST(QueueReactive, SameSeedSameStream)
    {
        std::vector<Event> first;
        std::vector<Event> second;
        std::vector<Event> other;
        QueueReactiveSimulator(params(0.7, 0.3), 7).runUntil(500 * Second, first);
        QueueReactiveSimulator(params(0.7, 0.3), 7).runUntil(500 * Second, second);
        QueueReactiveSimulator(params(0.7, 0.3), 8).runUntil(500 * Second, other);
        EXPECT_GT(first.size(), 1000u);
        EXPECT_EQ(first, second);
        EXPECT_NE(first, other);
    }

} // namespace quantMicrostructure
