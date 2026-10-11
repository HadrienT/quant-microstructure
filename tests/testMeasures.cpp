// Measures of work package M3, each checked on a model whose answer is known
// (docs/companion.md, sections 13 and 14).
#include <gtest/gtest.h>

#include "quantMicrostructure/book/book_level.hpp"
#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/measures/flow.hpp"
#include "quantMicrostructure/measures/liquidity.hpp"
#include "quantMicrostructure/measures/spread.hpp"
#include "quantMicrostructure/measures/tape.hpp"
#include "quantMicrostructure/measures/volatility.hpp"
#include "quantMicrostructure/sim/rng.hpp"
#include "quantMicrostructure/sim/toy_models.hpp"
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

        /// A zero-intelligence market with long queues at the best prices.
        const Tape &simulatedTape()
        {
            static const Tape tape = []
            {
                ZeroIntelligenceParams params;
                params.limit_rate = 2.0;
                params.market_rate = 1.5;
                params.cancel_rate = 0.2;
                params.window = 10;
                params.initial_price = 10'000;
                ZeroIntelligenceSimulator sim(params, 2026);
                std::vector<Event> events;
                sim.runUntil(5'000 * Second, events);
                return buildTape(events, StreamKind::OrderByOrder);
            }();
            return tape;
        }

        /// A tape in which every trade moves both quotes by `kick` ticks in its
        /// own direction, for good, and signs are independent.
        Tape permanentImpactTape(std::size_t trades, Price kick, std::uint64_t seed)
        {
            Rng rng(seed);
            Tape tape;
            Price bid = 1000;
            tape.quotes.push_back(Quote{0, bid, bid + 2, 10, 10});
            for (std::size_t i = 0; i < trades; ++i)
            {
                const Timestamp ts = static_cast<Timestamp>(i + 1) * 10;
                Trade trade;
                trade.ts = ts;
                trade.side = rng.below(2) == 0 ? Side::Buy : Side::Sell;
                trade.qty = 1;
                trade.price = static_cast<double>(trade.side == Side::Buy ? bid + 2 : bid);
                trade.quote = tape.quotes.size() - 1;
                tape.trades.push_back(trade);
                bid += trade.side == Side::Buy ? kick : -kick;
                tape.quotes.push_back(Quote{ts, bid, bid + 2, 10, 10});
                tape.end = ts;
            }
            return tape;
        }

        /// A random walk of standard deviation `sigma` per step, observed with
        /// independent noise of standard deviation `omega`.
        std::vector<double> noisyWalk(std::size_t increments, double sigma, double omega, std::uint64_t seed)
        {
            Rng rng(seed);
            std::vector<double> prices;
            double efficient = 0.0;
            for (std::size_t i = 0; i <= increments; ++i)
            {
                if (i > 0)
                {
                    efficient += sigma * rng.normal();
                }
                prices.push_back(efficient + omega * rng.normal());
            }
            return prices;
        }

    } // namespace

    // ---- Spread ----

    // Acceptance criterion of M3: on a simulated Roll model the estimator
    // finds the spread.
    TEST(RollSpread, FindsTheSpreadOfASimulatedRollModel)
    {
        Rng rng(2026);
        const Tape tape = buildTape(simulateRoll(RollParams{4, 1, 10'000, Second}, 400'000, rng), StreamKind::TradesOnly);
        const auto spread = rollSpread(tradePrices(tape));
        ASSERT_TRUE(spread);
        EXPECT_NEAR(*spread, 8.0, 0.1);
    }

    TEST(RollSpread, IsUndefinedWhenPriceChangesAreNotNegativelyCorrelated)
    {
        // Changes 1, 1, 2, 3, 5: they grow together.
        const std::vector<double> trending{0, 1, 2, 4, 7, 12};
        EXPECT_FALSE(rollSpread(trending));
        const std::vector<double> too_short{1, 2};
        EXPECT_FALSE(rollSpread(too_short));
    }

    TEST(TimeWeightedSpread, WeighsEachQuoteByHowLongItLasted)
    {
        Tape tape;
        tape.quotes = {
            Quote{0, 99, 101, 1, 1},  // spread 2 for 30
            Quote{30, 99, 104, 1, 1}, // spread 5 for 10
            Quote{40, 0, 104, 0, 1},  // one-sided for 50: not counted
            Quote{90, 100, 101, 1, 1} // spread 1 for 10
        };
        tape.end = 100;
        const auto spread = timeWeightedSpread(tape);
        ASSERT_TRUE(spread);
        EXPECT_DOUBLE_EQ(*spread, (2.0 * 30 + 5.0 * 10 + 1.0 * 10) / 50.0);

        EXPECT_FALSE(timeWeightedSpread(Tape{}));
    }

    TEST(SpreadDecomposition, HandComputedTrade)
    {
        Tape tape;
        tape.quotes = {Quote{0, 99, 101, 1, 1}, Quote{15, 100, 102, 1, 1}};
        Trade buy;
        buy.ts = 10;
        buy.side = Side::Buy;
        buy.qty = 1;
        buy.price = 101.0;
        buy.quote = 0;
        tape.trades = {buy};
        tape.end = 100;

        // Mid 100 before, 101 twenty later: the buyer paid 1 over the mid, and
        // the price moved 1 his way, so the seller kept nothing.
        const SpreadDecomposition d = spreadDecomposition(tape, 20);
        EXPECT_EQ(d.trades, 1u);
        EXPECT_DOUBLE_EQ(d.effective, 1.0);
        EXPECT_DOUBLE_EQ(d.impact, 1.0);
        EXPECT_DOUBLE_EQ(d.realized, 0.0);

        // Before the quote moves, the seller still holds the whole half-spread.
        const SpreadDecomposition early = spreadDecomposition(tape, 2);
        EXPECT_DOUBLE_EQ(early.impact, 0.0);
        EXPECT_DOUBLE_EQ(early.realized, 1.0);

        // Beyond the end of the tape nothing is known.
        EXPECT_EQ(spreadDecomposition(tape, 1000).trades, 0u);
    }

    TEST(SpreadDecomposition, EffectiveIsRealizedPlusImpact)
    {
        const Tape &tape = simulatedTape();
        for (const Timestamp horizon : {Second / 10, Second, 10 * Second})
        {
            const SpreadDecomposition d = spreadDecomposition(tape, horizon);
            EXPECT_GT(d.trades, 1000u);
            EXPECT_NEAR(d.effective, d.realized + d.impact, 1e-9);
            // Trades are made at the best price, on the right side of the mid.
            EXPECT_GT(d.effective, 0.0);
            // A market order removes liquidity on its side: the mid follows.
            EXPECT_GT(d.impact, 0.0);
        }
    }

    // With permanent impact of one tick and a half-spread of one tick, the
    // maker keeps nothing.
    TEST(SpreadDecomposition, PermanentImpactEatsTheSpread)
    {
        const Tape tape = permanentImpactTape(10'000, 1, 3);
        const SpreadDecomposition d = spreadDecomposition(tape, 0);
        EXPECT_EQ(d.trades, 10'000u);
        EXPECT_DOUBLE_EQ(d.effective, 1.0);
        EXPECT_DOUBLE_EQ(d.impact, 1.0);
        EXPECT_DOUBLE_EQ(d.realized, 0.0);
    }

    // ---- Liquidity ----

    TEST(Imbalance, ValueAndWeightedMid)
    {
        const Quote quote{0, 100, 101, 30, 10};
        EXPECT_DOUBLE_EQ(imbalance(quote), 0.75);
        // More volume on the bid: the price leans towards the ask.
        EXPECT_DOUBLE_EQ(weightedMid(quote), 100.75);
        EXPECT_DOUBLE_EQ(weightedMid(Quote{0, 100, 101, 10, 10}), 100.5);
    }

    TEST(DepthWithin, SumsTheLevelsNearTheBest)
    {
        const std::vector<BookLevel> bids{{100, 5}, {99, 7}, {97, 2}, {90, 50}};
        EXPECT_EQ(depthWithin(bids, 0), 5);
        EXPECT_EQ(depthWithin(bids, 1), 12);
        EXPECT_EQ(depthWithin(bids, 3), 14);
        EXPECT_EQ(depthWithin(bids, 100), 64);
        const std::vector<BookLevel> asks{{101, 4}, {103, 6}};
        EXPECT_EQ(depthWithin(asks, 1), 4);
        EXPECT_EQ(depthWithin(asks, 2), 10);
        EXPECT_EQ(depthWithin({}, 5), 0);
    }

    TEST(ImbalanceNextMove, HandComputedTape)
    {
        Tape tape;
        tape.quotes = {
            Quote{0, 100, 101, 9, 1}, // I = 0.9, next move up
            Quote{1, 100, 101, 8, 2}, // I = 0.8, same mid, next move up
            Quote{2, 0, 101, 0, 2},   // one-sided: stepped over
            Quote{3, 101, 102, 1, 9}, // I = 0.1, next move down
            Quote{4, 100, 101, 5, 5}, // I = 0.5, never moves again: left out
            Quote{5, 100, 101, 6, 5}  // left out too
        };
        const std::vector<ImbalanceBin> bins = imbalanceNextMove(tape, 2);
        ASSERT_EQ(bins.size(), 2u);
        EXPECT_EQ(bins[0].up, 0u);
        EXPECT_EQ(bins[0].down, 1u);
        EXPECT_EQ(bins[1].up, 2u);
        EXPECT_EQ(bins[1].down, 0u);
        EXPECT_DOUBLE_EQ(bins[1].probabilityUp(), 1.0);
        EXPECT_DOUBLE_EQ(bins[1].lower, 0.5);
        EXPECT_THROW(imbalanceNextMove(tape, 0), std::invalid_argument);
    }

    // The stylised fact of section 3, in the zero-intelligence model: the
    // thinner side is exhausted first, so the price goes towards it.
    TEST(ImbalanceNextMove, PredictsTheNextMoveInTheZeroIntelligenceModel)
    {
        const std::vector<ImbalanceBin> bins = imbalanceNextMove(simulatedTape(), 5);
        for (const ImbalanceBin &bin : bins)
        {
            EXPECT_GT(bin.up + bin.down, 500u);
        }
        EXPECT_LT(bins.front().probabilityUp(), 0.4);
        EXPECT_GT(bins.back().probabilityUp(), 0.6);
        for (std::size_t b = 1; b < bins.size(); ++b)
        {
            EXPECT_GT(bins[b].probabilityUp(), bins[b - 1].probabilityUp());
        }
        // The model is symmetric between buyers and sellers.
        EXPECT_NEAR(bins.front().probabilityUp() + bins.back().probabilityUp(), 1.0, 0.05);
    }

    // ---- Volatility ----

    TEST(RealizedVariance, HandComputed)
    {
        const std::vector<double> prices{0, 1, 3, 2, 2, 5};
        EXPECT_DOUBLE_EQ(realizedVariance(prices), 1 + 4 + 1 + 0 + 9);
        // Every second price: 0, 3, 2.
        EXPECT_DOUBLE_EQ(realizedVariance(prices, 2), 9 + 1);
        // Every second price from the second: 1, 2, 5.
        EXPECT_DOUBLE_EQ(realizedVariance(prices, 2, 1), 1 + 9);
        EXPECT_THROW(realizedVariance(prices, 0), std::invalid_argument);
    }

    // Acceptance criterion of M3: on a Brownian motion without noise the
    // signature plot is flat.
    TEST(SignaturePlot, IsFlatWithoutNoise)
    {
        constexpr std::size_t N = 400'000;
        const std::vector<double> prices = noisyWalk(N, 1.0, 0.0, 2026);
        const std::vector<std::size_t> steps{1, 2, 5, 10, 50};
        for (const SignaturePoint &point : signaturePlot(prices, steps))
        {
            EXPECT_EQ(point.increments, N / point.step);
            // A sum of m squared Gaussian increments has relative standard
            // deviation sqrt(2 / m).
            const double relative_sd = std::sqrt(2.0 / static_cast<double>(point.increments));
            EXPECT_NEAR(point.variance / static_cast<double>(N), 1.0, 5.0 * relative_sd) << "step " << point.step;
        }
    }

    // Acceptance criterion of M3: with noise, the measured bias is 2 n omega^2
    // (Zhang, Mykland & Ait-Sahalia 2005, equation 5), n being the number of
    // increments at each sampling step.
    TEST(SignaturePlot, NoiseAddsTwoOmegaSquaredPerIncrement)
    {
        constexpr std::size_t N = 400'000;
        constexpr double Omega = 3.0;
        const std::vector<double> prices = noisyWalk(N, 1.0, Omega, 2026);
        const std::vector<std::size_t> steps{1, 2, 5, 10, 50};
        for (const SignaturePoint &point : signaturePlot(prices, steps))
        {
            const double bias = point.variance - static_cast<double>(N);
            const double expected = 2.0 * static_cast<double>(point.increments) * Omega * Omega;
            EXPECT_NEAR(bias / expected, 1.0, 0.05) << "step " << point.step;
        }
        // The fastest scale is almost all noise: it estimates omega^2, plus
        // half the variance of one increment.
        EXPECT_NEAR(noiseVariance(prices), Omega * Omega + 0.5, 0.1);
    }

    // Equations 34, 55 and 64 of Zhang, Mykland & Ait-Sahalia (2005), by hand.
    TEST(TwoScaleRealizedVariance, HandComputed)
    {
        // Six increments, two subgrids: {0, 3, 2, 4} and {1, 2, 5}.
        const std::vector<double> prices{0, 1, 3, 2, 2, 5, 4};
        const double average = ((9.0 + 1.0 + 4.0) + (1.0 + 9.0)) / 2.0;
        const double all = 1.0 + 4.0 + 1.0 + 0.0 + 9.0 + 1.0;
        const double n_bar = (6.0 - 2.0 + 1.0) / 2.0;
        const double estimate = average - n_bar / 6.0 * all;
        EXPECT_DOUBLE_EQ(twoScaleRealizedVariance(prices, 2, false), estimate);
        EXPECT_DOUBLE_EQ(twoScaleRealizedVariance(prices, 2, true), estimate / (1.0 - n_bar / 6.0));
    }

    // Section 14: the two-scale estimator removes the bias.
    TEST(TwoScaleRealizedVariance, RemovesTheBiasOfTheNoise)
    {
        constexpr std::size_t N = 400'000;
        const std::vector<double> prices = noisyWalk(N, 1.0, 3.0, 2026);
        const double truth = static_cast<double>(N);

        // Realised variance on all prices is 19 times the truth.
        EXPECT_GT(realizedVariance(prices) / truth, 18.0);
        for (const std::size_t subgrids : {30u, 300u})
        {
            // Subsampling and averaging alone leave a bias of 2 n_bar omega^2:
            // 60 % of the truth with 30 subgrids, 6 % with 300.
            double average = 0.0;
            for (std::size_t k = 0; k < subgrids; ++k)
            {
                average += realizedVariance(prices, subgrids, k) / static_cast<double>(subgrids);
            }
            const double n_bar = static_cast<double>(N - subgrids + 1) / static_cast<double>(subgrids);
            EXPECT_NEAR(average / truth, 1.0 + 2.0 * n_bar * 9.0 / truth, 0.03) << subgrids << " subgrids";
            EXPECT_NEAR(twoScaleRealizedVariance(prices, subgrids) / truth, 1.0, 0.03) << subgrids << " subgrids";
        }
    }

    TEST(TwoScaleRealizedVariance, WithoutNoiseItIsStillRight)
    {
        constexpr std::size_t N = 400'000;
        const std::vector<double> prices = noisyWalk(N, 1.0, 0.0, 7);
        EXPECT_NEAR(twoScaleRealizedVariance(prices, 300) / static_cast<double>(N), 1.0, 0.05);
        EXPECT_THROW(twoScaleRealizedVariance(prices, 1), std::invalid_argument);
        EXPECT_THROW(twoScaleRealizedVariance(prices, N + 1), std::invalid_argument);
    }

    TEST(SampleMid, TakesTheLastKnownMid)
    {
        Tape tape;
        tape.quotes = {
            Quote{5, 0, 101, 0, 1},    // one-sided: sampling starts later
            Quote{10, 99, 101, 1, 1},  // mid 100
            Quote{25, 100, 102, 1, 1}, // mid 101
            Quote{31, 0, 102, 0, 1},   // one-sided: the last mid is kept
            Quote{48, 102, 104, 1, 1}  // mid 103
        };
        tape.end = 50;
        // Sampled at 10, 20, 30, 40, 50.
        EXPECT_EQ(sampleMid(tape, 10), (std::vector<double>{100, 100, 101, 101, 103}));
        EXPECT_TRUE(sampleMid(Tape{}, 10).empty());
        EXPECT_THROW(sampleMid(tape, 0), std::invalid_argument);
    }

    // ---- Flow ----

    TEST(Autocovariance, HandComputedAndIndependentSigns)
    {
        const std::vector<double> alternating{1, -1, 1, -1, 1, -1, 1, -1};
        const std::vector<double> c = autocovariance(alternating, 2);
        ASSERT_EQ(c.size(), 3u);
        EXPECT_DOUBLE_EQ(c[0], 1.0);
        EXPECT_DOUBLE_EQ(c[1], -1.0);
        EXPECT_DOUBLE_EQ(c[2], 1.0);

        const Tape tape = permanentImpactTape(200'000, 1, 5);
        const std::vector<double> independent = autocovariance(tradeSigns(tape), 5);
        EXPECT_NEAR(independent[0], 1.0, 0.001);
        for (std::size_t lag = 1; lag <= 5; ++lag)
        {
            EXPECT_NEAR(independent[lag], 0.0, 0.01);
        }
    }

    // With independent signs and a permanent impact of `kick` per trade, the
    // response function is `kick` at every lag from 1 on: the later trades
    // add nothing on average.
    TEST(ResponseFunction, PermanentImpactGivesAFlatResponse)
    {
        const Tape tape = permanentImpactTape(200'000, 3, 2026);
        const std::vector<double> r = responseFunction(tape, 20);
        ASSERT_EQ(r.size(), 21u);
        EXPECT_DOUBLE_EQ(r[0], 0.0);
        for (std::size_t lag = 1; lag <= 20; ++lag)
        {
            EXPECT_NEAR(r[lag], 3.0, 0.15) << "lag " << lag;
        }
    }

    TEST(ResponseFunction, IsPositiveInTheZeroIntelligenceModel)
    {
        const std::vector<double> r = responseFunction(simulatedTape(), 10);
        EXPECT_DOUBLE_EQ(r[0], 0.0);
        for (std::size_t lag = 1; lag <= 10; ++lag)
        {
            EXPECT_GT(r[lag], 0.0);
        }
    }

    TEST(TickTest, HandComputed)
    {
        const std::vector<double> prices{10, 10, 11, 11, 10, 10, 12};
        EXPECT_EQ(tickTestSigns(prices), (std::vector<double>{0, 0, 1, 1, -1, -1, 1}));
    }

    // In a Roll model whose efficient price does not move, the price only
    // bounces between bid and ask: the tick test is always right. Once the
    // efficient price moves it is fooled, the more so the larger the moves.
    TEST(TickTest, IsExactOnAPureBounceAndDegradesWithVolatility)
    {
        const auto accuracy = [](Price step)
        {
            Rng rng(2026);
            const Tape tape = buildTape(simulateRoll(RollParams{1, step, 10'000, Second}, 100'000, rng), StreamKind::TradesOnly);
            return signAgreement(tickTestSigns(tradePrices(tape)), tradeSigns(tape));
        };
        EXPECT_DOUBLE_EQ(accuracy(0), 1.0);
        const double moving = accuracy(1);
        const double volatile_price = accuracy(4);
        EXPECT_LT(moving, 0.95);
        EXPECT_GT(moving, 0.6);
        EXPECT_LT(volatile_price, moving);
    }

    // In a simulated market the quote rule cannot be wrong: a trade is made at
    // the best price, which is never the mid. Its errors on real data come
    // from what a simulator does not have: trades and quotes that arrive out
    // of step, and trades inside the spread.
    TEST(QuoteRule, IsExactWhenTradesAndQuotesAreInStep)
    {
        const Tape &tape = simulatedTape();
        EXPECT_DOUBLE_EQ(signAgreement(quoteRuleSigns(tape), tradeSigns(tape)), 1.0);
    }

    TEST(QuoteRule, FallsBackOnTheTickTestAtTheMid)
    {
        Tape tape;
        tape.quotes = {Quote{0, 99, 101, 1, 1}};
        const auto trade = [](Timestamp ts, double price, std::size_t quote)
        {
            Trade t;
            t.ts = ts;
            t.qty = 1;
            t.price = price;
            t.quote = quote;
            return t;
        };
        tape.trades = {trade(1, 101, 0), trade(2, 99, 0), trade(3, 100, 0), trade(4, 100.5, Trade::NoQuote)};
        tape.end = 4;
        // Above the mid, below it, at the mid after a fall then a rise (tick
        // test: up from 99), and with no quote (tick test: up from 100).
        EXPECT_EQ(quoteRuleSigns(tape), (std::vector<double>{1, -1, 1, 1}));
    }

    TEST(SignAgreement, IgnoresUnknownSigns)
    {
        const std::vector<double> a{1, -1, 0, 1, 1};
        const std::vector<double> b{1, 1, 1, 0, 1};
        EXPECT_DOUBLE_EQ(signAgreement(a, b), 2.0 / 3.0);
        const std::vector<double> shorter{1};
        EXPECT_THROW(signAgreement(a, shorter), std::invalid_argument);
    }

} // namespace quantMicrostructure
