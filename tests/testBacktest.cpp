// Book-level backtest (docs/companion.md, sections 10, 13 and 14): what a
// replay assumes of latency, queue position and fees, and the same strategy
// in a simulator.
#include <gtest/gtest.h>

#include "quantMicrostructure/book/level_aggregator.hpp"
#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/matching/matching_engine.hpp"
#include "quantMicrostructure/replay/backtest.hpp"
#include "quantMicrostructure/sim/zero_intelligence.hpp"

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <vector>

namespace quantMicrostructure
{

    namespace
    {

        constexpr Timestamp Second = 1'000'000'000;

        // ---- Strategies ----

        class NeverQuotes : public Strategy
        {
          public:
            void onMarket(const MarketView &, Gateway &) override { ++calls; }
            std::size_t calls{0};
        };

        /// Sends what `script` says the first time the clock reaches each
        /// time, and nothing else.
        class Scripted : public Strategy
        {
          public:
            using Step = std::function<void(Gateway &)>;

            Scripted &at(Timestamp ts, Step step)
            {
                steps_.push_back({ts, std::move(step)});
                return *this;
            }

            void onMarket(const MarketView &view, Gateway &gateway) override
            {
                while (next_ < steps_.size() && steps_[next_].first <= view.now)
                {
                    steps_[next_++].second(gateway);
                }
            }

          private:
            std::vector<std::pair<Timestamp, Step>> steps_;
            std::size_t next_{0};
        };

        /// Keeps one lot on each best price: when a best price moves, cancels
        /// the order there and joins the new one. Ignores its own fills, so
        /// that what it sends depends on the market alone.
        class JoinsTheBest : public Strategy
        {
          public:
            struct Decision
            {
                Timestamp ts{};
                Side side{};
                Price price{};
                friend bool operator==(const Decision &, const Decision &) = default;
            };

            void onMarket(const MarketView &view, Gateway &gateway) override
            {
                if (!view.quote.twoSided())
                {
                    return;
                }
                requote(Side::Buy, view.quote.bid, bid_, bid_price_, view.now, gateway);
                requote(Side::Sell, view.quote.ask, ask_, ask_price_, view.now, gateway);
            }

            std::vector<Decision> decisions;

          private:
            void requote(Side side, Price price, OrderId &id, Price &quoted, Timestamp now, Gateway &gateway)
            {
                if (id != 0 && quoted == price)
                {
                    return;
                }
                if (id != 0)
                {
                    gateway.cancel(id);
                }
                id = gateway.submitLimit(side, price, 1);
                quoted = price;
                decisions.push_back(Decision{now, side, price});
            }

            OrderId bid_{0};
            OrderId ask_{0};
            Price bid_price_{0};
            Price ask_price_{0};
        };

        // ---- Streams ----

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

        ZeroIntelligenceParams marketParams()
        {
            ZeroIntelligenceParams p;
            p.limit_rate = 2.0;
            p.market_rate = 1.5;
            p.cancel_rate = 0.2;
            p.window = 10;
            p.initial_price = 10'000;
            return p;
        }

        const std::vector<Event> &recordedMarket()
        {
            static const std::vector<Event> events = []
            {
                ZeroIntelligenceSimulator sim(marketParams(), 2026);
                std::vector<Event> out;
                sim.runUntil(600 * Second, out);
                return out;
            }();
            return events;
        }

        /// A market written by hand: `build` sends orders to an engine, and
        /// the stream is what the engine emitted.
        std::vector<Event> handMarket(const std::function<void(MatchingEngine &, std::vector<Event> &)> &build)
        {
            MatchingEngine engine;
            std::vector<Event> events;
            build(engine, events);
            return events;
        }

        Quantity filled(const BacktestResult &result)
        {
            Quantity total = 0;
            for (const Fill &fill : result.fills)
            {
                total += fill.qty;
            }
            return total;
        }

    } // namespace

    // Acceptance criterion of M5: no latency, a strategy that never quotes:
    // no fill, no P&L.
    TEST(Replay, StrategyThatNeverQuotesHasNoFillAndNoPnl)
    {
        for (const StreamKind kind : {StreamKind::OrderByOrder, StreamKind::Aggregated})
        {
            const std::vector<Event> events = kind == StreamKind::OrderByOrder ? recordedMarket() : aggregate(recordedMarket());
            NeverQuotes strategy;
            const BacktestResult result = runReplay(events, kind, strategy, BacktestConfig{});
            EXPECT_EQ(strategy.calls, events.size());
            EXPECT_TRUE(result.fills.empty());
            EXPECT_EQ(result.position, 0);
            EXPECT_DOUBLE_EQ(result.pnl, 0.0);
            EXPECT_EQ(result.orders_sent, 0u);
            EXPECT_FALSE(result.market_reacts);
            EXPECT_GT(result.mark, 0.0);
        }
        NeverQuotes strategy;
        EXPECT_THROW(runReplay({}, StreamKind::TradesOnly, strategy, BacktestConfig{}), std::invalid_argument);
    }

    // Acceptance criterion of M5: an order at the back of the queue is filled
    // only after the volume ahead of it. Ten lots are bid at 100 when we join
    // with five.
    TEST(Replay, OrderAtTheBackWaitsForTheVolumeAhead)
    {
        const std::vector<Event> orders = handMarket([](MatchingEngine &engine, std::vector<Event> &out)
                                                     {
                                                         engine.submitLimit(1, Side::Buy, 100, 10, 10, out);
                                                         engine.submitLimit(2, Side::Sell, 102, 50, 20, out);
                                                         engine.submitMarket(Side::Sell, 6, 30, out); // 4 left ahead
                                                         engine.submitMarket(Side::Sell, 4, 40, out); // none left ahead
                                                         engine.submitLimit(3, Side::Buy, 100, 20, 50, out);
                                                         engine.submitMarket(Side::Sell, 3, 60, out); // 3 for us
                                                         engine.submitMarket(Side::Sell, 7, 70, out); // our last 2
                                                     });
        for (const StreamKind kind : {StreamKind::OrderByOrder, StreamKind::Aggregated})
        {
            SCOPED_TRACE(kind == StreamKind::OrderByOrder ? "order by order" : "aggregated");
            const std::vector<Event> events = kind == StreamKind::OrderByOrder ? orders : aggregate(orders);
            Scripted strategy;
            strategy.at(20, [](Gateway &g)
                        { g.submitLimit(Side::Buy, 100, 5); });
            const BacktestResult result = runReplay(events, kind, strategy, BacktestConfig{});

            ASSERT_EQ(result.fills.size(), 2u);
            EXPECT_EQ(result.fills[0], (Fill{60, 1, Side::Buy, 100, 3, true}));
            EXPECT_EQ(result.fills[1], (Fill{70, 1, Side::Buy, 100, 2, true}));
            EXPECT_EQ(result.position, 5);
            EXPECT_DOUBLE_EQ(result.cash, -500.0);
            // Valued at the last mid: bid 100 (what is left of order 3), ask 102.
            EXPECT_DOUBLE_EQ(result.mark, 101.0);
            EXPECT_DOUBLE_EQ(result.pnl, 5.0);
        }
    }

    // In an order-by-order stream the queue is known: a cancellation ahead of
    // us moves us up, one behind us does not.
    TEST(Replay, OrderByOrderStreamKnowsWhoWasAhead)
    {
        const auto market = [](OrderId cancelled)
        {
            return handMarket([cancelled](MatchingEngine &engine, std::vector<Event> &out)
                              {
                                  engine.submitLimit(1, Side::Buy, 100, 5, 10, out);   // ahead of us
                                  engine.submitLimit(2, Side::Sell, 102, 50, 20, out); // we join now
                                  engine.submitLimit(3, Side::Buy, 100, 5, 30, out);   // behind us
                                  engine.cancel(cancelled, 40, out);
                                  engine.submitMarket(Side::Sell, 3, 50, out); });
        };
        const auto run = [&](OrderId cancelled)
        {
            Scripted strategy;
            strategy.at(20, [](Gateway &g)
                        { g.submitLimit(Side::Buy, 100, 5); });
            return runReplay(market(cancelled), StreamKind::OrderByOrder, strategy, BacktestConfig{});
        };
        // The order ahead leaves: we are first, and the sale is ours.
        EXPECT_EQ(filled(run(1)), 3);
        // The order behind leaves: the sale goes to the order still ahead.
        EXPECT_EQ(filled(run(3)), 0);
    }

    // In an aggregated stream a cancellation is anonymous, and the result
    // depends on what is assumed. Ten lots ahead of us, five behind, then four
    // are cancelled and nine are sold.
    TEST(Replay, AggregatedStreamNeedsAQueueAssumption)
    {
        const std::vector<Event> events = aggregate(handMarket([](MatchingEngine &engine, std::vector<Event> &out)
                                                               {
                                                                   engine.submitLimit(1, Side::Buy, 100, 10, 10, out);
                                                                   engine.submitLimit(2, Side::Sell, 102, 50, 20, out);
                                                                   engine.submitLimit(3, Side::Buy, 100, 4, 30, out);
                                                                   engine.submitLimit(4, Side::Buy, 100, 1, 31, out);
                                                                   engine.cancel(3, 40, out);
                                                                   engine.submitMarket(Side::Sell, 9, 50, out); }));
        const auto run = [&](QueueModel model)
        {
            Scripted strategy;
            strategy.at(20, [](Gateway &g)
                        { g.submitLimit(Side::Buy, 100, 5); });
            BacktestConfig config;
            config.queue_model = model;
            return runReplay(events, StreamKind::Aggregated, strategy, config);
        };
        // Pessimistic: the four cancelled were behind us, ten are still ahead.
        EXPECT_EQ(filled(run(QueueModel::Pessimistic)), 0);
        // Pro rata: 4 * 10 / 15 of them were ahead, 7.33 remain, and the sale
        // of nine reaches us for one whole lot.
        EXPECT_EQ(filled(run(QueueModel::ProRata)), 1);
    }

    // A recorded trade at a worse price than ours would have met us first.
    TEST(Replay, OrderInsideTheSpreadIsHitFirst)
    {
        const std::vector<Event> orders = handMarket([](MatchingEngine &engine, std::vector<Event> &out)
                                                     {
                                                         engine.submitLimit(1, Side::Buy, 100, 10, 10, out);
                                                         engine.submitLimit(2, Side::Sell, 104, 50, 20, out);
                                                         engine.submitMarket(Side::Sell, 3, 30, out); });
        for (const StreamKind kind : {StreamKind::OrderByOrder, StreamKind::Aggregated})
        {
            const std::vector<Event> events = kind == StreamKind::OrderByOrder ? orders : aggregate(orders);
            Scripted strategy;
            strategy.at(20, [](Gateway &g)
                        { g.submitLimit(Side::Buy, 102, 2); });
            const BacktestResult result = runReplay(events, kind, strategy, BacktestConfig{});
            ASSERT_EQ(result.fills.size(), 1u);
            // Filled at our own price, not at the recorded one.
            EXPECT_EQ(result.fills[0], (Fill{30, 1, Side::Buy, 102, 2, true}));
        }
    }

    // An order that trades on arrival takes what is displayed, best price
    // first, and pays the taker fee; what is left of a limit order rests.
    TEST(Replay, AggressiveOrderTakesTheDisplayedBookAndPaysFees)
    {
        const std::vector<Event> events = handMarket([](MatchingEngine &engine, std::vector<Event> &out)
                                                     {
                                                         engine.submitLimit(1, Side::Sell, 101, 3, 10, out);
                                                         engine.submitLimit(2, Side::Sell, 102, 4, 20, out);
                                                         engine.submitLimit(3, Side::Buy, 99, 5, 30, out);
                                                         engine.submitMarket(Side::Sell, 1, 40, out);
                                                         engine.submitMarket(Side::Sell, 1, 50, out); });
        BacktestConfig config;
        config.fees.taker = 0.001;
        config.fees.maker = -0.0005;

        Scripted market_order;
        market_order.at(30, [](Gateway &g)
                        { g.submitMarket(Side::Buy, 5); });
        BacktestResult result = runReplay(events, StreamKind::OrderByOrder, market_order, config);
        ASSERT_EQ(result.fills.size(), 2u);
        EXPECT_EQ(result.fills[0], (Fill{30, 0, Side::Buy, 101, 3, false}));
        EXPECT_EQ(result.fills[1], (Fill{30, 0, Side::Buy, 102, 2, false}));
        EXPECT_DOUBLE_EQ(result.cash, -(3 * 101.0 + 2 * 102.0));
        EXPECT_DOUBLE_EQ(result.fees, 0.001 * (3 * 101.0 + 2 * 102.0));

        // A buy limit at 101 for 5 takes the 3 displayed there; 2 rest at 101,
        // inside the spread, and the recorded sales of 1 lot each hit them: a
        // maker fill earns the rebate.
        Scripted limit_order;
        limit_order.at(30, [](Gateway &g)
                       { g.submitLimit(Side::Buy, 101, 5); });
        result = runReplay(events, StreamKind::OrderByOrder, limit_order, config);
        ASSERT_EQ(result.fills.size(), 3u);
        EXPECT_FALSE(result.fills[0].maker);
        EXPECT_EQ(result.fills[0].qty, 3);
        EXPECT_TRUE(result.fills[1].maker);
        EXPECT_TRUE(result.fills[2].maker);
        EXPECT_EQ(result.position, 5);
        EXPECT_DOUBLE_EQ(result.fees, 0.001 * 303.0 - 0.0005 * 202.0);
        EXPECT_DOUBLE_EQ(result.pnl, result.cash + 5 * result.mark - result.fees);
    }

    // Latency: the order meets the book as it is when it arrives, not as it
    // was when it was decided. The ask goes from 101 to 103 at time 100.
    TEST(Replay, LatencyChangesWhatTheOrderMeets)
    {
        const std::vector<Event> events = handMarket([](MatchingEngine &engine, std::vector<Event> &out)
                                                     {
                                                         engine.submitLimit(1, Side::Sell, 101, 5, 10, out);
                                                         engine.submitLimit(2, Side::Sell, 103, 5, 20, out);
                                                         engine.cancel(1, 100, out);
                                                         engine.submitLimit(3, Side::Sell, 110, 1, 500, out); });
        const auto priceWithLatency = [&](Timestamp latency) -> Price
        {
            Scripted strategy;
            strategy.at(20, [](Gateway &g)
                        { g.submitMarket(Side::Buy, 1); });
            BacktestConfig config;
            config.latency = latency;
            const BacktestResult result = runReplay(events, StreamKind::OrderByOrder, strategy, config);
            return result.fills.empty() ? 0 : result.fills[0].price;
        };
        EXPECT_EQ(priceWithLatency(0), 101);
        EXPECT_EQ(priceWithLatency(79), 101);
        // Arriving at the very time of the cancellation: the order is applied
        // first, since it had been travelling before that event happened.
        EXPECT_EQ(priceWithLatency(80), 101);
        EXPECT_EQ(priceWithLatency(81), 103);
        // Arriving after the end of the recording: dropped.
        EXPECT_EQ(priceWithLatency(10'000), 0);

        NeverQuotes strategy;
        BacktestConfig negative;
        negative.latency = -1;
        EXPECT_THROW(runReplay(events, StreamKind::OrderByOrder, strategy, negative), std::invalid_argument);
    }

    // A cancel that arrives before the trade saves the order; one that arrives
    // after does not.
    TEST(Replay, CancelRacesTheTrade)
    {
        const std::vector<Event> events = handMarket([](MatchingEngine &engine, std::vector<Event> &out)
                                                     {
                                                         engine.submitLimit(1, Side::Sell, 104, 50, 10, out);
                                                         engine.submitLimit(2, Side::Buy, 90, 1, 20, out);
                                                         engine.submitLimit(3, Side::Buy, 91, 1, 60, out);
                                                         engine.submitMarket(Side::Sell, 1, 100, out); });
        const auto run = [&](Timestamp latency)
        {
            // Joins at 100 on the first event, decides to cancel at time 60.
            class JoinThenCancel : public Strategy
            {
              public:
                void onMarket(const MarketView &view, Gateway &gateway) override
                {
                    if (id_ == 0)
                    {
                        id_ = gateway.submitLimit(Side::Buy, 100, 1);
                    }
                    else if (view.now >= 60 && !cancelled_)
                    {
                        gateway.cancel(id_);
                        cancelled_ = true;
                    }
                }

              private:
                OrderId id_{0};
                bool cancelled_{false};
            } strategy;
            BacktestConfig config;
            config.latency = latency;
            return filled(runReplay(events, StreamKind::OrderByOrder, strategy, config));
        };
        EXPECT_EQ(run(0), 0);
        EXPECT_EQ(run(39), 0);
        // The cancel leaves at 60 and arrives after the sale of time 100.
        EXPECT_EQ(run(41), 1);
    }

    // After a gap the book is rebuilt from a snapshot, and our order goes
    // behind what the snapshot shows.
    TEST(Replay, SnapshotAfterAGapPutsUsAtTheBack)
    {
        const auto level = [](SequenceNumber seq, Timestamp ts, EventType type, Side side, Price price, Quantity qty)
        {
            Event e;
            e.seq = seq;
            e.ts = ts;
            e.ts_recv = ts;
            e.type = type;
            e.side = side;
            e.price = price;
            e.qty = qty;
            return e;
        };
        const std::vector<Event> events{
            level(1, 10, EventType::Level, Side::Sell, 102, 50),
            level(2, 20, EventType::Level, Side::Buy, 100, 2),
            level(3, 30, EventType::Clear, Side::Buy, 0, 0),
            level(4, 30, EventType::Level, Side::Sell, 102, 50),
            level(5, 30, EventType::Level, Side::Buy, 100, 6),
            level(6, 40, EventType::Trade, Side::Sell, 100, 5),
            level(7, 40, EventType::Level, Side::Buy, 100, 1),
            level(8, 50, EventType::Trade, Side::Sell, 100, 3),
            level(9, 50, EventType::Level, Side::Buy, 100, 0),
        };
        Scripted strategy;
        strategy.at(20, [](Gateway &g)
                    { g.submitLimit(Side::Buy, 100, 5); });
        const BacktestResult result = runReplay(events, StreamKind::Aggregated, strategy, BacktestConfig{});
        // Two lots were ahead before the gap; six after it. The sale of five
        // does not reach us; of the next three, one is ahead and two are ours.
        ASSERT_EQ(result.fills.size(), 1u);
        EXPECT_EQ(result.fills[0], (Fill{50, 1, Side::Buy, 100, 2, true}));
    }

    // Section 14, "no future": changing the stream after time t changes no
    // decision and no fill up to t.
    TEST(Replay, NothingAfterTChangesAnythingUpToT)
    {
        for (const StreamKind kind : {StreamKind::OrderByOrder, StreamKind::Aggregated})
        {
            SCOPED_TRACE(kind == StreamKind::OrderByOrder ? "order by order" : "aggregated");
            const std::vector<Event> full = kind == StreamKind::OrderByOrder ? recordedMarket() : aggregate(recordedMarket());
            const std::size_t cut = full.size() / 2;
            const Timestamp t = full[cut - 1].ts;

            // Two other futures: nothing at all, and the book wiped out.
            std::vector<Event> truncated(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(cut));
            std::vector<Event> wiped = truncated;
            Event clear;
            clear.type = EventType::Clear;
            clear.seq = truncated.back().seq + 1;
            clear.ts = t + Second;
            wiped.push_back(clear);

            BacktestConfig config;
            config.latency = 2'000'000;
            config.queue_model = QueueModel::ProRata;

            const auto upToT = [&](const std::vector<Event> &events)
            {
                JoinsTheBest strategy;
                BacktestResult result = runReplay(events, kind, strategy, config);
                std::erase_if(result.fills, [t](const Fill &f)
                              { return f.ts > t; });
                std::erase_if(strategy.decisions, [t](const JoinsTheBest::Decision &d)
                              { return d.ts > t; });
                return std::pair{result.fills, strategy.decisions};
            };
            const auto reference = upToT(full);
            EXPECT_GT(reference.first.size(), 20u);
            EXPECT_GT(reference.second.size(), 100u);
            EXPECT_EQ(upToT(truncated), reference);
            EXPECT_EQ(upToT(wiped), reference);
        }
    }

    // A strategy is told of a fill once the book shows the trade that caused
    // it: what it decides there must not rest on a stale book.
    TEST(Replay, FillIsReportedWithTheBookUpToDate)
    {
        const std::vector<Event> events = handMarket([](MatchingEngine &engine, std::vector<Event> &out)
                                                     {
                                                         engine.submitLimit(1, Side::Buy, 100, 1, 10, out);  // ahead of us
                                                         engine.submitLimit(2, Side::Sell, 104, 5, 20, out); // we join now
                                                         engine.submitLimit(3, Side::Buy, 100, 2, 25, out);  // behind us
                                                         // Takes the whole bid: order 1, then order 3, whose place is ours.
                                                         engine.submitMarket(Side::Sell, 3, 30, out); });
        class RecordsTheView : public Strategy
        {
          public:
            void onMarket(const MarketView &view, Gateway &gateway) override
            {
                if (view.now == 20 && !sent_)
                {
                    gateway.submitLimit(Side::Buy, 100, 1);
                    sent_ = true;
                }
            }
            void onFill(const Fill &, const MarketView &view, Gateway &) override { seen = view.quote; }
            Quote seen;

          private:
            bool sent_{false};
        } strategy;
        const BacktestResult result = runReplay(events, StreamKind::OrderByOrder, strategy, BacktestConfig{});
        ASSERT_EQ(result.fills.size(), 1u);
        // The recorded bid at 100, two lots just before the trade that
        // filled us, is gone by the time we are told.
        EXPECT_EQ(strategy.seen.bid_qty, 0);
        EXPECT_EQ(strategy.seen.ask, 104);
    }

    TEST(Replay, SameInputsSameResult)
    {
        const std::vector<Event> events = aggregate(recordedMarket());
        BacktestConfig config;
        config.latency = 1'000'000;
        JoinsTheBest first;
        JoinsTheBest second;
        const BacktestResult a = runReplay(events, StreamKind::Aggregated, first, config);
        const BacktestResult b = runReplay(events, StreamKind::Aggregated, second, config);
        EXPECT_EQ(a.fills, b.fills);
        EXPECT_EQ(a.pnl, b.pnl);
        EXPECT_EQ(first.decisions, second.decisions);
    }

    // Principle 7: the result moves with the queue assumption, in the
    // direction expected. The strategy ignores its fills, so both runs send
    // the same orders, and an order is never further back pro rata than in
    // the pessimistic case.
    TEST(Replay, ProRataFillsAtLeastAsMuchAsPessimistic)
    {
        const std::vector<Event> events = aggregate(recordedMarket());
        const auto run = [&](QueueModel model)
        {
            JoinsTheBest strategy;
            BacktestConfig config;
            config.queue_model = model;
            return filled(runReplay(events, StreamKind::Aggregated, strategy, config));
        };
        const Quantity pessimistic = run(QueueModel::Pessimistic);
        const Quantity pro_rata = run(QueueModel::ProRata);
        EXPECT_GT(pessimistic, 0);
        EXPECT_GT(pro_rata, pessimistic);
    }

    // ---- The same strategies, inside the simulator ----

    // A strategy that sends nothing leaves the simulated market exactly as it
    // would have been without it.
    TEST(Simulation, StrategyThatNeverQuotesDoesNotDisturbTheMarket)
    {
        ZeroIntelligenceSimulator observed(marketParams(), 7);
        NeverQuotes strategy;
        const BacktestResult result = runSimulation(observed, 300 * Second, strategy, BacktestConfig{});
        EXPECT_TRUE(result.fills.empty());
        EXPECT_DOUBLE_EQ(result.pnl, 0.0);
        EXPECT_TRUE(result.market_reacts);
        EXPECT_GT(strategy.calls, 1000u);
        EXPECT_EQ(observed.now(), 300 * Second);

        ZeroIntelligenceSimulator alone(marketParams(), 7);
        std::vector<Event> events;
        alone.runUntil(300 * Second, events);
        EXPECT_EQ(observed.book().orders(Side::Buy), alone.book().orders(Side::Buy));
        EXPECT_EQ(observed.book().orders(Side::Sell), alone.book().orders(Side::Sell));
    }

    TEST(Simulation, AccountAddsUp)
    {
        ZeroIntelligenceSimulator sim(marketParams(), 2026);
        JoinsTheBest strategy;
        BacktestConfig config;
        config.latency = 1'000'000;
        config.fees.maker = 0.0001;
        const BacktestResult result = runSimulation(sim, 600 * Second, strategy, config);

        EXPECT_GT(result.fills.size(), 50u);
        Quantity position = 0;
        double cash = 0.0;
        double notional = 0.0;
        Timestamp last = 0;
        for (const Fill &fill : result.fills)
        {
            // The strategy only joins the best prices: every fill is passive,
            // for one lot, and in time order.
            EXPECT_TRUE(fill.maker);
            EXPECT_EQ(fill.qty, 1);
            EXPECT_GE(fill.order, ZeroIntelligenceSimulator::FirstParticipantId);
            EXPECT_GE(fill.ts, last);
            last = fill.ts;
            position += fill.side == Side::Buy ? fill.qty : -fill.qty;
            cash += static_cast<double>(fill.price) * static_cast<double>(fill.side == Side::Buy ? -fill.qty : fill.qty);
            notional += static_cast<double>(fill.price) * static_cast<double>(fill.qty);
        }
        EXPECT_EQ(result.position, position);
        EXPECT_DOUBLE_EQ(result.cash, cash);
        EXPECT_NEAR(result.fees, 0.0001 * notional, 1e-9);
        EXPECT_DOUBLE_EQ(result.pnl, result.cash + static_cast<double>(result.position) * result.mark - result.fees);
        // The simulated book is still a valid one.
        ASSERT_TRUE(sim.book().bestBid() && sim.book().bestAsk());
        EXPECT_LT(*sim.book().bestBid(), *sim.book().bestAsk());
    }

    // In the simulator the market reacts: a market order of ours empties the
    // best level for everybody.
    TEST(Simulation, AggressiveOrderMovesTheSimulatedBook)
    {
        ZeroIntelligenceSimulator sim(marketParams(), 3);
        std::vector<Event> warmup;
        sim.runUntil(100 * Second, warmup);
        const Price ask_before = *sim.book().bestAsk();
        const Quantity at_best = sim.book().levelQuantity(Side::Sell, ask_before);

        class BuysOnce : public Strategy
        {
          public:
            explicit BuysOnce(Quantity qty)
                : qty_(qty)
            {
            }
            void onMarket(const MarketView &, Gateway &gateway) override
            {
                if (!sent_)
                {
                    gateway.submitMarket(Side::Buy, qty_);
                    sent_ = true;
                }
            }

          private:
            Quantity qty_;
            bool sent_{false};
        } strategy(at_best + 1);

        const BacktestResult result = runSimulation(sim, 101 * Second, strategy, BacktestConfig{});
        EXPECT_EQ(result.position, at_best + 1);
        ASSERT_GE(result.fills.size(), 2u);
        EXPECT_FALSE(result.fills.front().maker);
        // It paid more than the best ask for its last lot.
        EXPECT_GT(result.fills.back().price, result.fills.front().price);
    }

    TEST(Simulation, SameSeedSameResult)
    {
        const auto run = [](std::uint64_t seed)
        {
            ZeroIntelligenceSimulator sim(marketParams(), seed);
            JoinsTheBest strategy;
            BacktestConfig config;
            config.latency = 500'000;
            return runSimulation(sim, 300 * Second, strategy, config);
        };
        const BacktestResult a = run(5);
        const BacktestResult b = run(5);
        const BacktestResult c = run(6);
        EXPECT_FALSE(a.fills.empty());
        EXPECT_EQ(a.fills, b.fills);
        EXPECT_EQ(a.pnl, b.pnl);
        EXPECT_NE(a.fills, c.fills);
    }

} // namespace quantMicrostructure
