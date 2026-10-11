// Work package M8 (docs/companion.md, sections 9, 13 and 14): the quotes of
// Avellaneda & Stoikov and of Guéant, Lehalle & Fernandez-Tapia, and a market
// maker in the book-level backtest.
#include <gtest/gtest.h>

#include "quantMicrostructure/book/level_aggregator.hpp"
#include "quantMicrostructure/measures/tape.hpp"
#include "quantMicrostructure/mm/market_maker.hpp"
#include "quantMicrostructure/mm/quoting.hpp"
#include "quantMicrostructure/replay/backtest.hpp"
#include "quantMicrostructure/sim/rng.hpp"
#include "quantMicrostructure/sim/zero_intelligence.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace quantMicrostructure
{

    namespace
    {

        constexpr Timestamp Second = 1'000'000'000;

        // The parameters of the simulations of Avellaneda & Stoikov (2008),
        // section 3.3.
        constexpr QuotingParams Article{2.0, 0.1, 1.5, 140.0};

        struct Summary
        {
            double profit{};
            double profit_sd{};
            double inventory{};
            double inventory_sd{};
        };

        Summary summarise(const std::vector<QuotingOutcome> &outcomes)
        {
            Summary s;
            const double n = static_cast<double>(outcomes.size());
            for (const QuotingOutcome &o : outcomes)
            {
                s.profit += o.pnl / n;
                s.inventory += o.final_inventory / n;
            }
            for (const QuotingOutcome &o : outcomes)
            {
                s.profit_sd += (o.pnl - s.profit) * (o.pnl - s.profit) / n;
                s.inventory_sd += (o.final_inventory - s.inventory) * (o.final_inventory - s.inventory) / n;
            }
            s.profit_sd = std::sqrt(s.profit_sd);
            s.inventory_sd = std::sqrt(s.inventory_sd);
            return s;
        }

        ZeroIntelligenceParams market()
        {
            ZeroIntelligenceParams p;
            p.limit_rate = 2.0;
            p.market_rate = 1.5;
            p.cancel_rate = 0.2;
            p.window = 10;
            p.initial_price = 10'000;
            return p;
        }

        // Quotes that join the best prices of that market when flat, and move
        // by about a third of a tick per lot of inventory. Chosen by hand, not
        // fitted to anything.
        constexpr QuotingParams TickScale{0.3, 0.5, 4.0, 0.2};

        TickQuotePolicy skewed()
        {
            return [](Quantity inventory, Timestamp)
            { return glftApproximateQuotes(static_cast<double>(inventory), TickScale); };
        }

        TickQuotePolicy unskewed()
        {
            return [](Quantity, Timestamp)
            { return glftApproximateQuotes(0.0, TickScale); };
        }

    } // namespace

    // ---- Avellaneda & Stoikov ----

    // Acceptance criterion of M8: with no inventory the quotes are symmetric.
    TEST(AvellanedaStoikov, NoInventoryGivesSymmetricQuotes)
    {
        const Quotes quotes = avellanedaStoikovQuotes(0.0, Article, 0.7);
        EXPECT_DOUBLE_EQ(quotes.bid, quotes.ask);
        EXPECT_DOUBLE_EQ(quotes.bid + quotes.ask, avellanedaStoikovSpread(Article, 0.7));
        EXPECT_DOUBLE_EQ(avellanedaStoikovReservationPrice(100.0, 0.0, Article, 0.7), 100.0);
        const Quotes symmetric = symmetricQuotes(Article, 0.7);
        EXPECT_DOUBLE_EQ(quotes.bid, symmetric.bid);
        EXPECT_DOUBLE_EQ(quotes.ask, symmetric.ask);
    }

    // Equations 3.17 and 3.18, with the numbers of the article: gamma = 0.1,
    // sigma = 2, k = 1.5.
    TEST(AvellanedaStoikov, ReservationPriceAndSpreadOfTheArticle)
    {
        // r = s - q gamma sigma^2 (T - t) = 100 - 3 * 0.1 * 4 * 0.5.
        EXPECT_DOUBLE_EQ(avellanedaStoikovReservationPrice(100.0, 3.0, Article, 0.5), 99.4);
        // The spread the article reports in its table 1, 1.29, is the part
        // that does not depend on time.
        EXPECT_NEAR(avellanedaStoikovSpread(Article, 0.0), 1.29, 0.005);
        EXPECT_DOUBLE_EQ(avellanedaStoikovSpread(Article, 0.5), 0.1 * 4.0 * 0.5 + 2.0 / 0.1 * std::log(1.0 + 0.1 / 1.5));
        EXPECT_THROW(avellanedaStoikovSpread(QuotingParams{2.0, 0.0, 1.5, 140.0}, 1.0), std::invalid_argument);
    }

    // Acceptance criterion of M8: the shift has the sign opposite to the
    // inventory. Long, both prices go down: the ask comes closer to the
    // reference price and the bid moves away.
    TEST(AvellanedaStoikov, QuotesLeanAgainstTheInventory)
    {
        const Quotes flat = avellanedaStoikovQuotes(0.0, Article, 0.5);
        Quotes previous = flat;
        for (const double inventory : {1.0, 2.0, 5.0})
        {
            const Quotes quotes = avellanedaStoikovQuotes(inventory, Article, 0.5);
            EXPECT_LT(quotes.ask, previous.ask);
            EXPECT_GT(quotes.bid, previous.bid);
            // The spread itself does not depend on the inventory.
            EXPECT_NEAR(quotes.bid + quotes.ask, flat.bid + flat.ask, 1e-12);
            // And a short position is the mirror image.
            const Quotes mirror = avellanedaStoikovQuotes(-inventory, Article, 0.5);
            EXPECT_NEAR(mirror.bid, quotes.ask, 1e-12);
            EXPECT_NEAR(mirror.ask, quotes.bid, 1e-12);
            previous = quotes;
        }
        // At the horizon the inventory no longer matters.
        const Quotes at_end = avellanedaStoikovQuotes(5.0, Article, 0.0);
        EXPECT_DOUBLE_EQ(at_end.bid, at_end.ask);
    }

    // Acceptance criterion of M8, the result of the article: against the
    // strategy that is symmetric around the reference price, the standard
    // deviations of the P&L and of the final inventory are lower, for a
    // slightly lower profit.
    //
    // Table 1 of the article (gamma = 0.1, 1000 paths) gives a profit of 62.94
    // (standard deviation 5.89) and a final inventory of standard deviation
    // 2.80 for the inventory strategy, and 67.21 (13.43) and 8.66 for the
    // symmetric one. The procedure of section 3.3, as written, gives figures
    // close to these but not equal (64.9, 6.5, 2.9 here); for gamma = 0.5 the
    // profit of the inventory strategy is 48.6 here against 33.92 in table 3.
    // The article does not say enough about its simulation to explain it.
    TEST(AvellanedaStoikov, InventoryStrategyHasLessRiskThanTheSymmetricOne)
    {
        constexpr std::size_t Paths = 20'000;
        QuotingSimulation simulation;
        simulation.params = Article;

        Rng rng_inventory(1);
        Rng rng_symmetric(1);
        const Summary inventory = summarise(simulateQuoting(
            simulation, [&](double q, double left)
            { return avellanedaStoikovQuotes(q, Article, left); },
            Paths, rng_inventory));
        const Summary symmetric = summarise(simulateQuoting(
            simulation, [&](double, double left)
            { return symmetricQuotes(Article, left); },
            Paths, rng_symmetric));

        EXPECT_LT(inventory.profit_sd, 0.6 * symmetric.profit_sd);
        EXPECT_LT(inventory.inventory_sd, 0.5 * symmetric.inventory_sd);
        EXPECT_LT(inventory.profit, symmetric.profit);
        EXPECT_GT(inventory.profit, 0.9 * symmetric.profit);
        EXPECT_NEAR(inventory.inventory, 0.0, 0.1);
        EXPECT_NEAR(symmetric.inventory, 0.0, 0.3);

        // Within 5 % of the article for the profits, 15 % for the standard
        // deviations.
        EXPECT_NEAR(inventory.profit, 62.94, 0.05 * 62.94);
        EXPECT_NEAR(inventory.profit_sd, 5.89, 0.15 * 5.89);
        EXPECT_NEAR(inventory.inventory_sd, 2.80, 0.15 * 2.80);
        EXPECT_NEAR(symmetric.profit, 67.21, 0.05 * 67.21);
        EXPECT_NEAR(symmetric.profit_sd, 13.43, 0.15 * 13.43);
        EXPECT_NEAR(symmetric.inventory_sd, 8.66, 0.15 * 8.66);
    }

    // The more risk-averse, the tighter the inventory is kept (tables 1 to 3
    // of the article).
    TEST(AvellanedaStoikov, MoreAversionKeepsLessInventory)
    {
        double previous = 1e9;
        for (const double gamma : {0.01, 0.1, 0.5})
        {
            QuotingSimulation simulation;
            simulation.params = QuotingParams{2.0, gamma, 1.5, 140.0};
            Rng rng(3);
            const Summary s = summarise(simulateQuoting(
                simulation, [&](double q, double left)
                { return avellanedaStoikovQuotes(q, simulation.params, left); },
                5'000, rng));
            EXPECT_LT(s.inventory_sd, previous);
            previous = s.inventory_sd;
        }
    }

    // ---- Guéant, Lehalle & Fernandez-Tapia ----

    TEST(Glft, ApproximateQuotesAreSymmetricAtZeroAndLeanAgainstTheInventory)
    {
        const Quotes flat = glftApproximateQuotes(0.0, TickScale);
        EXPECT_DOUBLE_EQ(flat.bid, flat.ask);
        const double base = std::log(1.0 + 0.5 / 4.0) / 0.5;
        const double c = std::sqrt(0.09 * 0.5 / (2.0 * 4.0 * 0.2) * std::pow(1.125, 9.0));
        EXPECT_NEAR(flat.bid, base + 0.5 * c, 1e-12);
        const Quotes longer = glftApproximateQuotes(3.0, TickScale);
        EXPECT_NEAR(longer.bid, base + 3.5 * c, 1e-12);
        EXPECT_NEAR(longer.ask, base - 2.5 * c, 1e-12);
        // The spread does not depend on the inventory.
        EXPECT_NEAR(longer.bid + longer.ask, flat.bid + flat.ask, 1e-12);
        const Quotes shorter = glftApproximateQuotes(-3.0, TickScale);
        EXPECT_NEAR(shorter.bid, longer.ask, 1e-12);
    }

    // The exact solution tends, far from the horizon, to quotes that no
    // longer depend on time (their theorem 2), and the closed-form
    // approximation is close to them for small inventories.
    TEST(Glft, ExactQuotesBecomeStationaryAndTheApproximationIsClose)
    {
        const QuotingParams params{0.3, 0.05, 0.3, 0.9};
        const GlftSolution far(params, 30, 600.0);
        const GlftSolution farther(params, 30, 3000.0);
        const GlftSolution near(params, 30, 10.0);
        for (const int q : {-10, -3, 0, 3, 10})
        {
            const Quotes stationary = farther.quotes(q);
            EXPECT_NEAR(far.quotes(q).bid, stationary.bid, 1e-6);
            EXPECT_NEAR(far.quotes(q).ask, stationary.ask, 1e-6);
            const Quotes approximate = glftApproximateQuotes(static_cast<double>(q), params);
            EXPECT_NEAR(approximate.bid, stationary.bid, 0.01);
            EXPECT_NEAR(approximate.ask, stationary.ask, 0.01);
        }
        // Symmetric at zero inventory, at any time.
        EXPECT_NEAR(near.quotes(0).bid, near.quotes(0).ask, 1e-12);
        // Close to the horizon the inventory matters less: the quotes of a
        // long position are less skewed.
        EXPECT_LT(near.quotes(10).bid - near.quotes(10).ask, far.quotes(10).bid - far.quotes(10).ask);
        // At the limit there is nothing to quote on the side that would pass
        // it.
        EXPECT_TRUE(std::isinf(far.quotes(30).bid));
        EXPECT_TRUE(std::isinf(far.quotes(-30).ask));
        EXPECT_TRUE(std::isfinite(far.quotes(30).ask));
        EXPECT_THROW(far.quotes(31), std::invalid_argument);
        EXPECT_THROW(GlftSolution(params, 0, 1.0), std::invalid_argument);
    }

    // At the horizon v is the vector of ones: the quotes are the same for
    // every inventory, (1 / gamma) ln(1 + gamma / k).
    TEST(Glft, AtTheHorizonOnlyTheBaseDistanceIsLeft)
    {
        const QuotingParams params{0.3, 0.05, 0.3, 0.9};
        const GlftSolution at_horizon(params, 5, 0.0);
        const double base = std::log(1.0 + 0.05 / 0.3) / 0.05;
        for (const int q : {-4, 0, 4})
        {
            EXPECT_NEAR(at_horizon.quotes(q).bid, base, 1e-12);
            EXPECT_NEAR(at_horizon.quotes(q).ask, base, 1e-12);
        }
    }

    // In the model of the articles the stationary quotes do what they are for:
    // against the same quotes without skew, the inventory stays near zero.
    TEST(Glft, StationaryQuotesKeepTheInventoryNearZero)
    {
        QuotingSimulation simulation;
        simulation.params = QuotingParams{2.0, 0.1, 1.5, 140.0};
        Rng a(5);
        Rng b(5);
        const Summary skew = summarise(simulateQuoting(
            simulation, [&](double q, double)
            { return glftApproximateQuotes(q, simulation.params); },
            5'000, a));
        const Summary none = summarise(simulateQuoting(
            simulation, [&](double, double)
            { return glftApproximateQuotes(0.0, simulation.params); },
            5'000, b));
        EXPECT_LT(skew.inventory_sd, 0.5 * none.inventory_sd);
        EXPECT_LT(skew.profit_sd, none.profit_sd);
    }

    // ---- A market maker in the backtest ----

    TEST(MarketMaker, RejectsInvalidConfiguration)
    {
        EXPECT_THROW(MarketMaker(nullptr, MarketMakerConfig{1, 10}), std::invalid_argument);
        EXPECT_THROW(MarketMaker(unskewed(), MarketMakerConfig{0, 10}), std::invalid_argument);
        EXPECT_THROW(MarketMaker(unskewed(), MarketMakerConfig{5, 2}), std::invalid_argument);
    }

    // It only provides liquidity, its inventory respects the limit, and its
    // P&L splits exactly into spread earned, price moves and fees.
    TEST(MarketMaker, InTheSimulatorItsAccountAddsUp)
    {
        ZeroIntelligenceSimulator sim(market(), 2026);
        std::vector<Event> events;
        sim.runUntil(60 * Second, events);
        MarketMaker maker(skewed(), MarketMakerConfig{1, 5});
        BacktestConfig config;
        config.fees.maker = -0.00001;
        const BacktestResult result = runSimulation(sim, 660 * Second, maker, config, &events);

        ASSERT_GT(result.fills.size(), 100u);
        Quantity position = 0;
        Quantity largest = 0;
        for (const Fill &fill : result.fills)
        {
            EXPECT_TRUE(fill.maker);
            position += fill.side == Side::Buy ? fill.qty : -fill.qty;
            largest = std::max(largest, position < 0 ? -position : position);
        }
        EXPECT_EQ(position, result.position);
        EXPECT_LE(largest, 5);

        const Tape tape = buildTape(events, StreamKind::OrderByOrder);
        const MakerPnl pnl = makerPnl(result, tape);
        EXPECT_EQ(pnl.fills, result.fills.size());
        EXPECT_NEAR(pnl.total, result.pnl, 1e-6);
        EXPECT_NEAR(pnl.spread + pnl.inventory - pnl.fees, result.pnl, 1e-6);
        // Each fill is at the best price, at least half a tick from the mid.
        EXPECT_GE(pnl.spread, 0.5 * static_cast<double>(result.fills.size()));
        // A rebate: negative fees.
        EXPECT_LT(pnl.fees, 0.0);
    }

    // Markouts (section 4.5): what a lot is worth some time after the fill.
    // Just after it, the half-spread; later, less, because the order that hit
    // us pushed the price our way and it keeps going on average.
    TEST(MarketMaker, MarkoutsShrinkWithTheHorizon)
    {
        ZeroIntelligenceSimulator sim(market(), 7);
        std::vector<Event> events;
        sim.runUntil(60 * Second, events);
        MarketMaker maker(unskewed(), MarketMakerConfig{1, 50});
        const BacktestResult result = runSimulation(sim, 1'260 * Second, maker, BacktestConfig{}, &events);
        const Tape tape = buildTape(events, StreamKind::OrderByOrder);

        const std::vector<Timestamp> horizons{0, Second, 10 * Second, 10'000 * Second};
        const std::vector<Markout> m = markouts(result.fills, tape, horizons);
        ASSERT_EQ(m.size(), 4u);
        EXPECT_GT(m[0].lots, 300);
        // Right after the fill the mid has already moved against us.
        const MakerPnl pnl = makerPnl(result, tape);
        const double captured = pnl.spread / static_cast<double>(m[0].lots);
        EXPECT_LT(m[0].mean, captured);
        EXPECT_LT(m[2].mean, m[0].mean);
        // Fewer fills have a known future as the horizon grows, and none
        // beyond the end of the tape.
        EXPECT_LE(m[2].lots, m[1].lots);
        EXPECT_EQ(m[3].lots, 0);
        EXPECT_DOUBLE_EQ(m[3].mean, 0.0);
    }

    // Acceptance criterion of M8, in the simulated book this time: the same
    // quotes with and without the inventory skew, in the same thirty markets.
    // With the skew the final inventory and the P&L vary much less.
    //
    // The level of the P&L means nothing: this simulated flow is never
    // informed, so quoting at the best price earns the spread at little risk.
    TEST(MarketMaker, SkewingTheQuotesReducesInventoryAndPnlRisk)
    {
        const auto run = [](const TickQuotePolicy &policy)
        {
            std::vector<QuotingOutcome> outcomes;
            for (std::uint64_t seed = 0; seed < 30; ++seed)
            {
                ZeroIntelligenceSimulator sim(market(), 100 + seed);
                std::vector<Event> warmup;
                sim.runUntil(60 * Second, warmup);
                MarketMaker maker(policy, MarketMakerConfig{1, 30});
                const BacktestResult result = runSimulation(sim, 660 * Second, maker, BacktestConfig{});
                outcomes.push_back(QuotingOutcome{result.pnl, static_cast<double>(result.position)});
            }
            return summarise(outcomes);
        };
        const Summary with_skew = run(skewed());
        const Summary without = run(unskewed());
        EXPECT_LT(with_skew.inventory_sd, 0.3 * without.inventory_sd);
        EXPECT_LT(with_skew.profit_sd, 0.5 * without.profit_sd);
    }

    // Principle 7: on a recorded stream the result of a market maker depends
    // on the latency and on what is assumed of the queue, and both must be
    // shown with it. Here they change the number of fills.
    TEST(MarketMaker, OnARecordingTheResultDependsOnLatencyAndQueueAssumption)
    {
        ZeroIntelligenceSimulator sim(market(), 11);
        std::vector<Event> orders;
        sim.runUntil(900 * Second, orders);
        LevelAggregator aggregator;
        std::vector<Event> levels;
        for (const Event &event : orders)
        {
            aggregator.apply(event, levels);
        }
        const Tape tape = buildTape(levels, StreamKind::Aggregated);

        const auto run = [&](Timestamp latency, QueueModel model)
        {
            MarketMaker maker(skewed(), MarketMakerConfig{1, 5});
            BacktestConfig config;
            config.latency = latency;
            config.queue_model = model;
            const BacktestResult result = runReplay(levels, StreamKind::Aggregated, maker, config);
            // Whatever is assumed, the account adds up.
            EXPECT_NEAR(makerPnl(result, tape).total, result.pnl, 1e-6);
            std::size_t takers = 0;
            for (const Fill &fill : result.fills)
            {
                takers += fill.maker ? 0 : 1;
            }
            // Without latency a quote never crosses the book it was decided
            // on. With latency the book has moved by the time it arrives, and
            // it can: there is no post-only order.
            if (latency == 0)
            {
                EXPECT_EQ(takers, 0u);
            }
            return result;
        };
        const BacktestResult base = run(0, QueueModel::Pessimistic);
        const BacktestResult pro_rata = run(0, QueueModel::ProRata);
        const BacktestResult slow = run(2 * Second, QueueModel::Pessimistic);
        EXPECT_GT(base.fills.size(), 20u);
        EXPECT_NE(base.fills.size(), pro_rata.fills.size());
        EXPECT_NE(base.fills.size(), slow.fills.size());
        EXPECT_NE(base.pnl, slow.pnl);
    }

} // namespace quantMicrostructure
