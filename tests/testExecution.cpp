// Work package M7 (docs/companion.md, sections 8, 13 and 14): Almgren-Chriss,
// the usual schedules, and transaction cost analysis.
#include <gtest/gtest.h>

#include "quantMicrostructure/execution/almgren_chriss.hpp"
#include "quantMicrostructure/execution/schedule.hpp"
#include "quantMicrostructure/execution/tca.hpp"
#include "quantMicrostructure/replay/backtest.hpp"
#include "quantMicrostructure/sim/rng.hpp"
#include "quantMicrostructure/sim/zero_intelligence.hpp"

#include "support/statistics.hpp"

#include <cmath>
#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace quantMicrostructure
{

    namespace
    {

        constexpr Timestamp Second = 1'000'000'000;

        // The example of the article, section 4.4 excepted: a million shares
        // over five days, in units of days and dollars.
        constexpr AlmgrenChrissParams Params{0.95, 2.5e-7, 2.5e-6, 0.0625};
        constexpr double Shares = 1e6;
        constexpr double Horizon = 5.0;
        constexpr std::size_t Intervals = 5;

    } // namespace

    // Acceptance criterion of M7: with no risk aversion the trajectory is a
    // straight line, which is TWAP.
    TEST(AlmgrenChriss, NoRiskAversionGivesAStraightLine)
    {
        const TradeSchedule schedule = almgrenChrissSchedule(Shares, Horizon, Intervals, Params, 0.0);
        const TradeSchedule twap = twapSchedule(Shares, Horizon, Intervals);
        ASSERT_EQ(schedule.holdings.size(), Intervals + 1);
        ASSERT_EQ(schedule.trades.size(), Intervals);
        for (std::size_t k = 0; k <= Intervals; ++k)
        {
            EXPECT_NEAR(schedule.holdings[k], Shares * (1.0 - static_cast<double>(k) / Intervals), 1e-6);
            EXPECT_DOUBLE_EQ(schedule.holdings[k], twap.holdings[k]);
        }
        EXPECT_DOUBLE_EQ(schedule.tau, 1.0);
        EXPECT_DOUBLE_EQ(almgrenChrissKappa(Params, 0.0, 1.0), 0.0);

        // And a tiny aversion gives almost the same line: no jump at zero.
        const TradeSchedule nearly = almgrenChrissSchedule(Shares, Horizon, Intervals, Params, 1e-12);
        for (std::size_t k = 0; k <= Intervals; ++k)
        {
            EXPECT_NEAR(nearly.holdings[k], schedule.holdings[k], 1.0);
        }
    }

    // Equations 17 and 18: the trajectory, written out.
    TEST(AlmgrenChriss, TrajectoryIsTheHyperbolicSineOfTheArticle)
    {
        constexpr double Lambda = 2e-6;
        const TradeSchedule schedule = almgrenChrissSchedule(Shares, Horizon, Intervals, Params, Lambda);
        const double kappa = almgrenChrissKappa(Params, Lambda, 1.0);
        // The defining equation of kappa.
        const double eta_tilde = Params.eta - 0.5 * Params.gamma * 1.0;
        EXPECT_NEAR(2.0 * (std::cosh(kappa) - 1.0), Lambda * Params.sigma * Params.sigma / eta_tilde, 1e-12);

        for (std::size_t j = 0; j <= Intervals; ++j)
        {
            const double expected = Shares * std::sinh(kappa * (Horizon - static_cast<double>(j))) / std::sinh(kappa * Horizon);
            EXPECT_NEAR(schedule.holdings[j], expected, 1e-6 * Shares);
        }
        EXPECT_DOUBLE_EQ(schedule.holdings.back(), 0.0);
        EXPECT_NEAR(std::accumulate(schedule.trades.begin(), schedule.trades.end(), 0.0), Shares, 1e-6);
        // A sell program only sells, and sells most at the start.
        for (std::size_t k = 0; k < Intervals; ++k)
        {
            EXPECT_GT(schedule.trades[k], 0.0);
            if (k > 0)
            {
                EXPECT_LT(schedule.trades[k], schedule.trades[k - 1]);
            }
        }
    }

    // Equation 19: as the intervals shorten, kappa tends to
    // sqrt(lambda sigma^2 / eta).
    TEST(AlmgrenChriss, KappaTendsToItsContinuousLimit)
    {
        constexpr double Lambda = 2e-6;
        const double limit = std::sqrt(Lambda * Params.sigma * Params.sigma / Params.eta);
        // The error is of first order in tau: ten times smaller for a step
        // ten times shorter.
        const double error_fine = almgrenChrissKappa(Params, Lambda, 0.01) - limit;
        const double error_finer = almgrenChrissKappa(Params, Lambda, 0.001) - limit;
        EXPECT_GT(error_finer, 0.0);
        EXPECT_NEAR(error_fine / error_finer, 10.0, 0.5);
        EXPECT_NEAR(almgrenChrissKappa(Params, Lambda, 1e-4), limit, 1e-4 * limit);
        EXPECT_THROW(almgrenChrissKappa(Params, -1.0, 1.0), std::invalid_argument);
        // gamma tau / 2 above eta: the discrete model breaks down.
        EXPECT_THROW(almgrenChrissKappa(Params, Lambda, 30.0), std::invalid_argument);
    }

    // The closed forms of equation 20 against the sums of equations 5 and 8,
    // and the two extreme trajectories of equations 10 to 13.
    TEST(AlmgrenChriss, ClosedFormCostsMatchTheSums)
    {
        for (const double lambda : {1e-8, 2e-7, 2e-6, 1e-4})
        {
            const TradeSchedule schedule = almgrenChrissSchedule(Shares, Horizon, Intervals, Params, lambda);
            const CostMoments closed = almgrenChrissCost(Shares, Horizon, Intervals, Params, lambda);
            EXPECT_NEAR(closed.expected, expectedCost(schedule, Params), 1e-9 * closed.expected) << "lambda " << lambda;
            EXPECT_NEAR(closed.variance, costVariance(schedule, Params), 1e-7 * closed.variance + 1e-3) << "lambda " << lambda;
        }
        EXPECT_THROW(almgrenChrissCost(Shares, Horizon, Intervals, Params, 0.0), std::invalid_argument);

        // Constant rate (equations 10 and 11).
        const TradeSchedule twap = twapSchedule(Shares, Horizon, Intervals);
        const double n = static_cast<double>(Intervals);
        EXPECT_NEAR(expectedCost(twap, Params), 0.5 * Params.gamma * Shares * Shares + Params.epsilon * Shares + (Params.eta - 0.5 * Params.gamma) * Shares * Shares / Horizon, 1e-6);
        EXPECT_NEAR(costVariance(twap, Params), Params.sigma * Params.sigma * Shares * Shares * Horizon / 3.0 * (1.0 - 1.0 / n) * (1.0 - 0.5 / n), 1.0);

        // Everything in the first interval (equations 12 and 13).
        const std::vector<double> at_once{Shares, 0, 0, 0, 0};
        const TradeSchedule block = scheduleFromTrades(at_once, 1.0);
        EXPECT_NEAR(expectedCost(block, Params), Params.epsilon * Shares + Params.eta * Shares * Shares, 1e-6);
        EXPECT_DOUBLE_EQ(costVariance(block, Params), 0.0);
    }

    // Acceptance criterion of M7: the simulated cost gives back the closed
    // formula. The price of equation 1 is simulated, the trajectory sold along
    // it, and the mean and the variance of the shortfall compared with E and V.
    TEST(AlmgrenChriss, SimulatedCostGivesBackTheFormula)
    {
        constexpr double Lambda = 2e-6;
        constexpr std::size_t Paths = 200'000;
        const TradeSchedule schedule = almgrenChrissSchedule(Shares, Horizon, Intervals, Params, Lambda);
        const CostMoments theory = almgrenChrissCost(Shares, Horizon, Intervals, Params, Lambda);

        Rng rng(2026);
        const std::vector<double> shortfalls = simulateShortfall(schedule, Params, Paths, rng);
        const double sd = std::sqrt(theory.variance);
        EXPECT_NEAR(testing::mean(shortfalls), theory.expected, 4.0 * sd / std::sqrt(static_cast<double>(Paths)));
        EXPECT_NEAR(testing::variance(shortfalls) / theory.variance, 1.0, 0.02);

        // Without volatility the cost is exactly its expectation.
        AlmgrenChrissParams quiet = Params;
        quiet.sigma = 0.0;
        const TradeSchedule twap = twapSchedule(Shares, Horizon, Intervals);
        const std::vector<double> certain = simulateShortfall(twap, quiet, 3, rng);
        EXPECT_NEAR(certain[0], expectedCost(twap, quiet), 1e-6);
        EXPECT_DOUBLE_EQ(certain[0], certain[2]);
    }

    // The trajectory really is optimal: any other with the same total has a
    // higher E + lambda V.
    TEST(AlmgrenChriss, NoOtherTrajectoryDoesBetter)
    {
        constexpr double Lambda = 2e-6;
        const TradeSchedule optimal = almgrenChrissSchedule(Shares, Horizon, 10, Params, Lambda);
        const auto utility = [&](const TradeSchedule &s)
        { return expectedCost(s, Params) + Lambda * costVariance(s, Params); };
        const double best = utility(optimal);

        Rng rng(7);
        for (int trial = 0; trial < 500; ++trial)
        {
            std::vector<double> trades = optimal.trades;
            // Move some quantity from one interval to another.
            const std::size_t from = static_cast<std::size_t>(rng.below(trades.size()));
            const std::size_t to = static_cast<std::size_t>(rng.below(trades.size()));
            if (from == to)
            {
                continue;
            }
            const double moved = 0.2 * trades[from] * rng.uniform();
            trades[from] -= moved;
            trades[to] += moved;
            ASSERT_GT(utility(scheduleFromTrades(trades, optimal.tau)), best);
        }
        EXPECT_GT(utility(twapSchedule(Shares, Horizon, 10)), best);
    }

    // Section 14, "monotonic in aversion": the more risk-averse, the less is
    // held at every time.
    TEST(AlmgrenChriss, MoreAversionSellsFaster)
    {
        const std::vector<double> lambdas{0.0, 1e-7, 1e-6, 1e-5, 1e-4};
        TradeSchedule previous = almgrenChrissSchedule(Shares, Horizon, 10, Params, lambdas[0]);
        for (std::size_t i = 1; i < lambdas.size(); ++i)
        {
            const TradeSchedule next = almgrenChrissSchedule(Shares, Horizon, 10, Params, lambdas[i]);
            for (std::size_t k = 1; k < 10; ++k)
            {
                EXPECT_LT(next.holdings[k], previous.holdings[k]) << "lambda " << lambdas[i] << ", time " << k;
            }
            previous = next;
        }
    }

    // Acceptance criterion of M7: the frontier is decreasing and convex. As
    // the aversion rises the variance falls and the expected cost rises, and
    // the slope dE/dV is -lambda: steeper and steeper towards low variance.
    TEST(AlmgrenChriss, EfficientFrontierIsDecreasingAndConvex)
    {
        std::vector<double> lambdas;
        for (double lambda = 1e-8; lambda < 2e-4; lambda *= 1.5)
        {
            lambdas.push_back(lambda);
        }
        const std::vector<FrontierPoint> frontier = efficientFrontier(Shares, Horizon, 20, Params, lambdas);
        ASSERT_EQ(frontier.size(), lambdas.size());
        double previous_slope = 0.0;
        for (std::size_t i = 1; i < frontier.size(); ++i)
        {
            EXPECT_LT(frontier[i].variance, frontier[i - 1].variance);
            EXPECT_GT(frontier[i].expected, frontier[i - 1].expected);
            const double slope = (frontier[i].expected - frontier[i - 1].expected) / (frontier[i].variance - frontier[i - 1].variance);
            // The chord between two points has the slope of the tangent
            // somewhere between them.
            EXPECT_LT(slope, -lambdas[i - 1]);
            EXPECT_GT(slope, -lambdas[i]);
            if (i > 1)
            {
                EXPECT_LT(slope, previous_slope);
            }
            previous_slope = slope;
        }
        // TWAP is the end of the frontier with the lowest expected cost.
        const TradeSchedule twap = twapSchedule(Shares, Horizon, 20);
        EXPECT_LT(expectedCost(twap, Params), frontier.front().expected);
        EXPECT_GT(costVariance(twap, Params), frontier.front().variance);
    }

    // ---- Schedules ----

    TEST(Schedules, VwapFollowsTheVolumeProfile)
    {
        // A U-shaped day.
        const std::vector<double> profile{3, 1, 1, 1, 4};
        const TradeSchedule schedule = vwapSchedule(1000.0, 5.0, profile);
        EXPECT_EQ(schedule.trades, (std::vector<double>{300, 100, 100, 100, 400}));
        EXPECT_EQ(schedule.holdings, (std::vector<double>{1000, 700, 600, 500, 400, 0}));
        EXPECT_DOUBLE_EQ(schedule.tau, 1.0);
        // A flat profile is TWAP.
        EXPECT_EQ(vwapSchedule(1000.0, 5.0, std::vector<double>(5, 7.0)).trades, twapSchedule(1000.0, 5.0, 5).trades);
        EXPECT_THROW(vwapSchedule(1000.0, 5.0, std::vector<double>{1, -1}), std::invalid_argument);
        EXPECT_THROW(vwapSchedule(1000.0, 5.0, std::vector<double>{0, 0}), std::invalid_argument);
    }

    TEST(Schedules, PovTakesAShareOfTheMarketVolumeUntilDone)
    {
        const std::vector<double> volumes{1000, 500, 4000, 1000};
        const TradeSchedule done = povSchedule(400.0, 0.1, volumes, 1.0);
        EXPECT_EQ(done.trades, (std::vector<double>{100, 50, 250, 0}));
        EXPECT_DOUBLE_EQ(done.holdings.back(), 0.0);

        // Too large for the volume there was: some is left at the end.
        const TradeSchedule unfinished = povSchedule(1000.0, 0.1, volumes, 1.0);
        EXPECT_EQ(unfinished.trades, (std::vector<double>{100, 50, 400, 100}));
        EXPECT_DOUBLE_EQ(unfinished.holdings.front(), 1000.0);
        EXPECT_DOUBLE_EQ(unfinished.holdings.back(), 350.0);
        EXPECT_THROW(povSchedule(100.0, 0.0, volumes, 1.0), std::invalid_argument);
    }

    TEST(Schedules, RoundingToLotsKeepsTheTotal)
    {
        const TradeSchedule thirds = twapSchedule(10.0, 3.0, 3);
        EXPECT_EQ(roundToLots(thirds), (std::vector<Quantity>{3, 4, 3}));

        const TradeSchedule optimal = almgrenChrissSchedule(1000.0, Horizon, 37, Params, 2.0);
        const std::vector<Quantity> lots = roundToLots(optimal);
        EXPECT_EQ(std::accumulate(lots.begin(), lots.end(), Quantity{0}), 1000);
        for (const Quantity lot : lots)
        {
            EXPECT_GE(lot, 0);
        }
    }

    // ---- Transaction cost analysis ----

    TEST(ImplementationShortfall, HandComputedBuy)
    {
        ExecutionRecord record;
        record.side = Side::Buy;
        record.target = 10;
        record.decision_price = 100.0;
        record.arrival_price = 100.5;
        record.final_price = 103.0;
        record.fees = 0.7;
        const std::vector<Fill> fills{Fill{1, 0, Side::Buy, 101, 4, false}, Fill{2, 0, Side::Buy, 102, 2, false}};

        const ShortfallBreakdown b = implementationShortfall(record, fills);
        EXPECT_EQ(b.filled, 6);
        EXPECT_DOUBLE_EQ(b.average_price, (4 * 101.0 + 2 * 102.0) / 6.0);
        // The price rose half a tick before the first order, on 6 lots.
        EXPECT_DOUBLE_EQ(b.delay, 3.0);
        // 4 lots half a tick above arrival, 2 lots one and a half.
        EXPECT_DOUBLE_EQ(b.trading, 4 * 0.5 + 2 * 1.5);
        // 4 lots never bought, while the price rose 3.
        EXPECT_DOUBLE_EQ(b.opportunity, 12.0);
        EXPECT_DOUBLE_EQ(b.total, 3.0 + 5.0 + 12.0 + 0.7);

        // The definition: the paper portfolio bought 10 at 100 and is worth 30
        // more at 103; the real one made 4 * 2 + 2 * 1 less the fees.
        const double paper = 10 * (103.0 - 100.0);
        const double real = 4 * (103.0 - 101.0) + 2 * (103.0 - 102.0) - 0.7;
        EXPECT_DOUBLE_EQ(b.total, paper - real);
    }

    TEST(ImplementationShortfall, ASellIsTheMirrorImage)
    {
        ExecutionRecord record;
        record.side = Side::Sell;
        record.target = 5;
        record.decision_price = 100.0;
        record.arrival_price = 99.0;
        record.final_price = 97.0;
        const std::vector<Fill> fills{Fill{1, 0, Side::Sell, 98, 5, false}};
        const ShortfallBreakdown b = implementationShortfall(record, fills);
        // Sold 5 lots 2 ticks under the decision price: 1 lost before
        // starting, 1 while trading.
        EXPECT_DOUBLE_EQ(b.delay, 5.0);
        EXPECT_DOUBLE_EQ(b.trading, 5.0);
        EXPECT_DOUBLE_EQ(b.opportunity, 0.0);
        EXPECT_DOUBLE_EQ(b.total, 10.0);

        // Nothing executed: only the opportunity cost, here a gain since the
        // price fell and we did not sell.
        const ShortfallBreakdown nothing = implementationShortfall(record, {});
        EXPECT_EQ(nothing.filled, 0);
        EXPECT_DOUBLE_EQ(nothing.total, -(5 * (97.0 - 100.0)));
    }

    TEST(MarketVwap, AveragesTheTradesOfThePeriod)
    {
        Tape tape;
        const auto trade = [](Timestamp ts, double price, Quantity qty)
        {
            Trade t;
            t.ts = ts;
            t.price = price;
            t.qty = qty;
            return t;
        };
        tape.trades = {trade(10, 100, 5), trade(20, 102, 1), trade(30, 104, 3), trade(40, 90, 100)};
        const auto vwap = marketVwap(tape, 10, 30);
        ASSERT_TRUE(vwap);
        // The trade at 10 is before the period, the one at 30 inside it.
        EXPECT_DOUBLE_EQ(*vwap, (102.0 * 1 + 104.0 * 3) / 4.0);
        EXPECT_FALSE(marketVwap(tape, 50, 60));
    }

    // ---- A schedule carried out in the simulator ----

    // The same order of 60 lots bought at once or in 30 slices over five
    // minutes, in many independent markets. Buying costs something either way
    // (the spread, then the impact), and the block, which walks through the
    // book, pays more per lot than the slices.
    TEST(ScheduleExecutor, SlicingAnOrderCostsLessThanSendingItAtOnce)
    {
        ZeroIntelligenceParams market;
        market.limit_rate = 2.0;
        market.market_rate = 1.5;
        market.cancel_rate = 0.2;
        market.window = 10;
        market.initial_price = 10'000;

        const auto averageTradingCost = [&](std::size_t slices)
        {
            const std::vector<Quantity> lots = roundToLots(twapSchedule(60.0, 300.0, slices));
            double total = 0.0;
            constexpr int Markets = 60;
            for (int m = 0; m < Markets; ++m)
            {
                ZeroIntelligenceSimulator sim(market, 500 + static_cast<std::uint64_t>(m));
                std::vector<Event> warmup;
                sim.runUntil(60 * Second, warmup);
                ScheduleExecutor executor(Side::Buy, lots, 60 * Second, 300 * Second / static_cast<Timestamp>(slices));
                const BacktestResult result = runSimulation(sim, 400 * Second, executor, BacktestConfig{});
                EXPECT_TRUE(executor.done());
                EXPECT_EQ(result.position, 60);

                ExecutionRecord record;
                record.side = Side::Buy;
                record.target = 60;
                record.decision_price = executor.arrivalMid();
                record.arrival_price = executor.arrivalMid();
                record.final_price = result.mark;
                const ShortfallBreakdown b = implementationShortfall(record, result.fills);
                EXPECT_EQ(b.filled, 60);
                EXPECT_DOUBLE_EQ(b.delay, 0.0);
                EXPECT_DOUBLE_EQ(b.opportunity, 0.0);
                total += b.trading / 60.0;
            }
            return total / Markets;
        };
        const double block = averageTradingCost(1);
        const double sliced = averageTradingCost(30);
        // At least the half-spread, per lot.
        EXPECT_GT(sliced, 0.5);
        EXPECT_GT(block, 1.3 * sliced);
    }

    TEST(ScheduleExecutor, RejectsInvalidSchedules)
    {
        EXPECT_THROW(ScheduleExecutor(Side::Buy, {1, 2}, 0, 0), std::invalid_argument);
        EXPECT_THROW(ScheduleExecutor(Side::Buy, {1, -2}, 0, 10), std::invalid_argument);
    }

} // namespace quantMicrostructure
