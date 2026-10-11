// Work package M6 (docs/companion.md, sections 5, 13 and 14): Kyle's lambda,
// the propagator and its critical exponent, metaorders in a simulator, and
// round trips in an impact model.
#include <gtest/gtest.h>

#include "quantMicrostructure/impact/kyle_lambda.hpp"
#include "quantMicrostructure/impact/metaorder.hpp"
#include "quantMicrostructure/impact/propagator.hpp"
#include "quantMicrostructure/impact/round_trip.hpp"
#include "quantMicrostructure/measures/tape.hpp"
#include "quantMicrostructure/sim/order_splitting.hpp"
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

        /// <x_{n+lag} x_n>, without removing the mean: the signs have mean
        /// zero by construction, and their sample mean is too noisy to
        /// subtract.
        double correlation(const std::vector<double> &x, std::size_t lag)
        {
            double sum = 0.0;
            for (std::size_t i = lag; i < x.size(); ++i)
            {
                sum += x[i] * x[i - lag];
            }
            return sum / static_cast<double>(x.size() - lag);
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

        double exponentOf(const std::vector<MetaorderImpactPoint> &points)
        {
            std::vector<double> size;
            std::vector<double> impact;
            for (const MetaorderImpactPoint &point : points)
            {
                size.push_back(static_cast<double>(point.size));
                impact.push_back(point.impact);
            }
            return fitPowerLaw(size, impact).exponent;
        }

    } // namespace

    // ---- Long memory of the flow ----

    // With one hidden order at a time, two trades l apart have the same sign
    // when they belong to the same order, and independent signs otherwise:
    // C(l) = E[(L - l)+] / E[L] = sum over j > l of P(L >= j), over the same
    // sum from 1. With P(L >= j) = j^(-alpha) this is known exactly.
    TEST(OrderSplitting, CorrelationOfOneHiddenOrderIsKnownExactly)
    {
        constexpr double Alpha = 1.5;
        double zeta = 0.0;
        std::vector<double> tail(6, 0.0);
        for (std::size_t j = 20'000'000; j >= 1; --j)
        {
            const double p = std::pow(static_cast<double>(j), -Alpha);
            zeta += p;
            for (std::size_t lag = 1; lag <= 5; ++lag)
            {
                if (j > lag)
                {
                    tail[lag] += p;
                }
            }
        }
        // The sizes have an infinite variance, so one run is dominated by its
        // few largest orders: the correlation is averaged over several runs.
        constexpr int Runs = 8;
        std::vector<double> measured(6, 0.0);
        for (int run = 0; run < Runs; ++run)
        {
            Rng rng(200 + static_cast<std::uint64_t>(run));
            const std::vector<double> signs = simulateOrderSplittingSigns(Alpha, 1, 1'000'000, rng);
            for (std::size_t lag = 1; lag <= 5; ++lag)
            {
                measured[lag] += correlation(signs, lag) / Runs;
            }
            if (run == 0)
            {
                for (const double sign : signs)
                {
                    ASSERT_TRUE(sign == 1.0 || sign == -1.0);
                }
            }
        }
        for (std::size_t lag = 1; lag <= 5; ++lag)
        {
            EXPECT_NEAR(measured[lag], tail[lag] / zeta, 0.03 * tail[lag] / zeta) << "lag " << lag;
        }
    }

    // Lillo, Mike & Farmer (2005), equation 17: the correlation decays as
    // lag^(-(alpha - 1)). The exponent converges slowly, because the sizes
    // have an infinite variance: it is fitted on the average of several runs,
    // and the tolerance is wide.
    TEST(OrderSplitting, MemoryExponentIsAlphaMinusOne)
    {
        const std::vector<std::size_t> lags{10, 20, 40, 80, 160, 320};
        for (const double alpha : {1.5, 1.8})
        {
            std::vector<double> mean_correlation(lags.size(), 0.0);
            constexpr int Runs = 8;
            for (int run = 0; run < Runs; ++run)
            {
                Rng rng(100 + static_cast<std::uint64_t>(run));
                const std::vector<double> signs = simulateOrderSplittingSigns(alpha, 1, 1'000'000, rng);
                for (std::size_t i = 0; i < lags.size(); ++i)
                {
                    mean_correlation[i] += correlation(signs, lags[i]) / Runs;
                }
            }
            std::vector<double> x(lags.begin(), lags.end());
            const double gamma = -fitPowerLaw(x, mean_correlation).exponent;
            EXPECT_NEAR(gamma, alpha - 1.0, 0.1) << "alpha " << alpha;
        }
    }

    TEST(OrderSplitting, RejectsInvalidInput)
    {
        Rng rng(1);
        EXPECT_THROW(simulateOrderSplittingSigns(1.0, 1, 10, rng), std::invalid_argument);
        EXPECT_THROW(simulateOrderSplittingSigns(1.5, 0, 10, rng), std::invalid_argument);
        EXPECT_EQ(simulateOrderSplittingSigns(1.5, 3, 10, rng).size(), 10u);
    }

    // ---- Propagator ----

    TEST(Propagator, PowerLawShape)
    {
        const std::vector<double> g = powerLawPropagator(2.0, 20.0, 0.4, 100);
        ASSERT_EQ(g.size(), 100u);
        EXPECT_DOUBLE_EQ(g[0], 2.0 * std::pow(20.0 / 21.0, 0.4));
        EXPECT_DOUBLE_EQ(g[99], 2.0 * std::pow(20.0 / 120.0, 0.4));
        EXPECT_DOUBLE_EQ(criticalPropagatorExponent(0.2), 0.4);
        EXPECT_THROW(powerLawPropagator(1.0, 0.0, 0.4, 10), std::invalid_argument);
    }

    // The definition, equation 12 of Bouchaud et al. (2004), written out.
    TEST(Propagator, PricesFollowTheDefinition)
    {
        const std::vector<double> signs{1, 1, -1, 1, -1, -1};
        const std::vector<double> g{1.0, 0.6, 0.5};
        Rng rng(1);
        const std::vector<double> prices = propagatorPrices(signs, g, 0.0, rng);
        ASSERT_EQ(prices.size(), signs.size());
        for (std::size_t n = 0; n < signs.size(); ++n)
        {
            double expected = 0.0;
            for (std::size_t k = 0; k < n; ++k)
            {
                // Beyond its last entry the propagator stays where it is.
                expected += g[std::min(n - k, g.size()) - 1] * signs[k];
            }
            EXPECT_NEAR(prices[n], expected, 1e-12) << "trade " << n;
        }
    }

    // A constant propagator is a permanent impact: with independent signs the
    // price is a random walk, D(l) = G^2 l (their equation 16).
    TEST(Propagator, PermanentImpactAndIndependentSignsGiveARandomWalk)
    {
        Rng rng(3);
        std::vector<double> signs;
        for (int i = 0; i < 400'000; ++i)
        {
            signs.push_back(rng.below(2) == 0 ? 1.0 : -1.0);
        }
        const std::vector<double> g(10, 0.5);
        const std::vector<double> prices = propagatorPrices(signs, g, 0.0, rng);
        const std::vector<std::size_t> lags{1, 10, 100};
        const std::vector<double> d = priceDiffusion(prices, lags);
        for (std::size_t i = 0; i < lags.size(); ++i)
        {
            EXPECT_NEAR(d[i] / (0.25 * static_cast<double>(lags[i])), 1.0, 0.05);
        }
        EXPECT_THROW(priceDiffusion(prices, std::vector<std::size_t>{prices.size()}), std::invalid_argument);
    }

    // Acceptance criterion of M6: the relation beta = (1 - gamma) / 2 on a
    // simulated flow of known memory. Signs of exponent gamma = 1/2 go through
    // propagators of three exponents; the growth exponent of D(l), which is 1
    // for a diffusive price, is 2 - 2 beta - gamma below the critical
    // exponent and crosses 1 there (their equation 23).
    TEST(Propagator, PriceIsDiffusiveAtTheCriticalExponent)
    {
        constexpr double Gamma = 0.5;
        Rng rng(7);
        const std::vector<double> signs = simulateOrderSplittingSigns(1.0 + Gamma, 1, 150'000, rng);
        const std::vector<std::size_t> lags{20, 40, 80, 160, 320};
        const std::vector<double> x(lags.begin(), lags.end());

        const auto growth = [&](double beta)
        {
            const std::vector<double> prices = propagatorPrices(signs, powerLawPropagator(1.0, 1.0, beta, 2000), 0.0, rng);
            return fitPowerLaw(x, priceDiffusion(prices, lags)).exponent;
        };
        const double critical = criticalPropagatorExponent(Gamma);
        ASSERT_DOUBLE_EQ(critical, 0.25);

        const double below = growth(critical - 0.15);
        const double at = growth(critical);
        const double above = growth(critical + 0.15);
        // Impact that decays too slowly: persistent price changes.
        EXPECT_NEAR(below, 2.0 - 2.0 * (critical - 0.15) - Gamma, 0.1);
        EXPECT_GT(below, 1.15);
        // Exactly enough decay to cancel the memory of the flow.
        EXPECT_NEAR(at, 1.0, 0.12);
        // Too much: the price reverts.
        EXPECT_LT(above, 0.92);
    }

    // The propagator is read back from the signs and the prices it produced,
    // although the signs are strongly correlated and noise is added.
    TEST(Propagator, FitRecoversThePropagator)
    {
        Rng rng(2026);
        const std::vector<double> signs = simulateOrderSplittingSigns(1.5, 1, 300'000, rng);
        const std::vector<double> truth = powerLawPropagator(1.0, 1.0, 0.25, 2000);
        const std::vector<double> prices = propagatorPrices(signs, truth, 0.5, rng);

        const std::vector<double> fitted = fitPropagator(signs, prices, 100);
        ASSERT_EQ(fitted.size(), 100u);
        // The first half is the part to trust.
        for (std::size_t l = 1; l <= 50; ++l)
        {
            EXPECT_NEAR(fitted[l - 1], truth[l - 1], 0.03) << "lag " << l;
        }
        // It decays: the response to one trade is not permanent.
        EXPECT_LT(fitted[49], 0.6 * fitted[0]);
    }

    TEST(Propagator, FitRejectsWhatItCannotSolve)
    {
        const std::vector<double> constant(1000, 1.0);
        const std::vector<double> prices(1000, 0.0);
        EXPECT_THROW(fitPropagator(constant, prices, 10), std::invalid_argument);
        const std::vector<double> shorter(999, 0.0);
        EXPECT_THROW(fitPropagator(constant, shorter, 10), std::invalid_argument);
        EXPECT_THROW(fitPropagator(constant, prices, 600), std::invalid_argument);
    }

    // ---- Kyle's lambda ----

    TEST(LinearRegression, HandComputed)
    {
        const std::vector<double> x{1, 2, 3, 4};
        const std::vector<double> y{3, 5, 7, 9};
        const Regression exact = linearRegression(x, y);
        EXPECT_DOUBLE_EQ(exact.slope, 2.0);
        EXPECT_DOUBLE_EQ(exact.intercept, 1.0);
        EXPECT_DOUBLE_EQ(exact.r_squared, 1.0);
        EXPECT_NEAR(exact.slope_se, 0.0, 1e-12);

        const std::vector<double> noisy{3, 5, 8, 8};
        const Regression fit = linearRegression(x, noisy);
        EXPECT_DOUBLE_EQ(fit.slope, 1.8);
        EXPECT_DOUBLE_EQ(fit.intercept, 1.5);
        // Residuals -0.3, -0.1, 1.1, -0.7: sum of squares 1.8, over 2 degrees
        // of freedom and a sum of squares of x of 5.
        EXPECT_NEAR(fit.slope_se, std::sqrt(1.8 / 2.0 / 5.0), 1e-12);
        EXPECT_NEAR(fit.r_squared, 1.0 - 1.8 / 18.0, 1e-12);

        const std::vector<double> flat{2, 2, 2, 2};
        EXPECT_THROW(linearRegression(flat, y), std::invalid_argument);
        EXPECT_THROW(linearRegression(x, std::vector<double>{1, 2}), std::invalid_argument);
    }

    // Acceptance criterion of M6: on a simulated Kyle model the regression
    // finds sigma_v / (2 sigma_u).
    TEST(KyleLambda, RegressionFindsLambdaOnASimulatedKyleModel)
    {
        const KyleParams params{100.0, 2.0, 5.0};
        const double lambda = kyleEquilibrium(params).lambda;
        Rng rng(2026);
        const std::vector<KyleAuction> auctions = simulateKyle(params, 200'000, rng);
        std::vector<double> flow;
        std::vector<double> price_change;
        std::vector<double> value;
        for (const KyleAuction &a : auctions)
        {
            flow.push_back(a.insider_order + a.noise_order);
            price_change.push_back(a.price - params.p0);
            value.push_back(a.value - params.p0);
        }
        // The price is a linear function of the flow: the regression is exact.
        const Regression of_price = linearRegression(flow, price_change);
        EXPECT_NEAR(of_price.slope, lambda, 1e-9);
        EXPECT_NEAR(of_price.r_squared, 1.0, 1e-9);
        // And the same slope explains the value, which the price was never
        // told: within three standard errors, with half of the variance left.
        const Regression of_value = linearRegression(flow, value);
        EXPECT_NEAR(of_value.slope, lambda, 3.0 * of_value.slope_se);
        EXPECT_LT(of_value.slope_se, 0.01 * lambda);
        EXPECT_NEAR(of_value.r_squared, 0.5, 0.01);
    }

    TEST(KyleLambda, FlowBarsHandComputed)
    {
        Tape tape;
        tape.quotes = {Quote{0, 99, 101, 1, 1}, Quote{12, 100, 102, 1, 1}, Quote{27, 98, 100, 1, 1}};
        const auto trade = [](Timestamp ts, Side side, Quantity qty)
        {
            Trade t;
            t.ts = ts;
            t.side = side;
            t.qty = qty;
            return t;
        };
        tape.trades = {trade(5, Side::Buy, 3), trade(12, Side::Buy, 2), trade(25, Side::Sell, 7), trade(38, Side::Buy, 1)};
        tape.end = 35;
        // Intervals (0, 10], (10, 20], (20, 30]; the fourth is beyond the end.
        const FlowBars bars = signedFlowBars(tape, 10);
        EXPECT_EQ(bars.flow, (std::vector<double>{3, 2, -7}));
        EXPECT_EQ(bars.price_change, (std::vector<double>{0, 1, -2}));
        EXPECT_THROW(signedFlowBars(tape, 0), std::invalid_argument);
    }

    // In the zero-intelligence market buying pushes the price up: lambda is
    // positive, and many standard errors from zero.
    TEST(KyleLambda, IsPositiveInTheZeroIntelligenceModel)
    {
        ZeroIntelligenceSimulator sim(market(), 2026);
        std::vector<Event> events;
        sim.runUntil(3'000 * Second, events);
        const Regression lambda = kyleLambda(buildTape(events, StreamKind::OrderByOrder), 5 * Second);
        EXPECT_GT(lambda.observations, 500u);
        EXPECT_GT(lambda.slope, 10.0 * lambda.slope_se);
        EXPECT_GT(lambda.r_squared, 0.05);
    }

    // ---- Metaorders ----

    TEST(FitPowerLaw, RecoversAnExactPowerLaw)
    {
        const std::vector<double> x{1, 2, 4, 8};
        std::vector<double> y;
        for (const double v : x)
        {
            y.push_back(3.0 * std::sqrt(v));
        }
        const PowerLawFit fit = fitPowerLaw(x, y);
        EXPECT_NEAR(fit.exponent, 0.5, 1e-12);
        EXPECT_NEAR(fit.prefactor, 3.0, 1e-12);
        EXPECT_THROW(fitPowerLaw(x, std::vector<double>{1, 2, 3, -1}), std::invalid_argument);
        EXPECT_THROW(fitPowerLaw(std::vector<double>{2, 2}, std::vector<double>{1, 3}), std::invalid_argument);
    }

    // A market order that walks the book at once moves the price less than
    // proportionally to its size: the book is thinner near the best price than
    // behind it (Smith et al. 2003, section II.B.2).
    TEST(MetaorderImpact, InstantaneousImpactIsConcave)
    {
        MetaorderExperiment experiment;
        experiment.market = market();
        experiment.children = {2, 8, 32};
        experiment.child_interval = 1e-6;
        experiment.warmup = 50.0;
        experiment.repetitions = 200;
        experiment.seed = 1;
        const std::vector<MetaorderImpactPoint> points = measureMetaorderImpact(experiment);
        ASSERT_EQ(points.size(), 3u);
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            EXPECT_GT(points[i].samples, 190u);
            EXPECT_GT(points[i].impact, 5.0 * points[i].standard_error);
            EXPECT_EQ(points[i].size, static_cast<Quantity>(experiment.children[i]));
        }
        EXPECT_GT(points[2].impact, points[1].impact);
        EXPECT_GT(points[1].impact, points[0].impact);
        // Sixteen times the size, well under sixteen times the impact.
        EXPECT_LT(points[2].impact, 13.0 * points[0].impact);
        EXPECT_LT(exponentOf(points), 0.95);
    }

    // A NEGATIVE RESULT, kept as a test so that it is not forgotten: in the
    // zero-intelligence model a metaorder executed slowly has an impact
    // proportional to its size. The square-root law of section 5.4 of the
    // companion is NOT reproduced: this flow has no memory and does not
    // anticipate anything, so the book refills at the same pace whatever is
    // being executed.
    TEST(MetaorderImpact, ZeroIntelligenceDoesNotGiveTheSquareRootLaw)
    {
        MetaorderExperiment experiment;
        experiment.market = market();
        experiment.children = {4, 16, 64};
        experiment.child_interval = 0.5;
        experiment.warmup = 50.0;
        experiment.repetitions = 200;
        experiment.seed = 1;
        const std::vector<MetaorderImpactPoint> points = measureMetaorderImpact(experiment);
        for (const MetaorderImpactPoint &point : points)
        {
            EXPECT_GT(point.impact, 5.0 * point.standard_error);
        }
        const double exponent = exponentOf(points);
        EXPECT_GT(exponent, 0.85);
        EXPECT_LT(exponent, 1.15);
    }

    TEST(MetaorderImpact, RejectsInvalidExperiments)
    {
        MetaorderExperiment experiment;
        experiment.market = market();
        experiment.children = {2};
        experiment.repetitions = 1;
        EXPECT_THROW(measureMetaorderImpact(experiment), std::invalid_argument);
        experiment.repetitions = 10;
        experiment.child_interval = 0.0;
        EXPECT_THROW(measureMetaorderImpact(experiment), std::invalid_argument);
    }

    // ---- Round trips ----

    TEST(ImpactTradingCost, HandComputed)
    {
        const std::vector<double> kernel{1.0, 0.5};
        // Buy 2 then sell 2, linear impact: prices 2 and -2 + 0.5 * 2 = -1.
        EXPECT_DOUBLE_EQ(impactTradingCost(std::vector<double>{2, -2}, kernel, 1.0), 2 * 2.0 + (-2) * (-1.0));
        // Square-root impact: pushes sqrt(2) and -sqrt(2).
        const double r = std::sqrt(2.0);
        EXPECT_DOUBLE_EQ(impactTradingCost(std::vector<double>{2, -2}, kernel, 0.5), 2 * r + (-2) * (-r + 0.5 * r));
        // The impact has vanished beyond the kernel.
        EXPECT_DOUBLE_EQ(impactTradingCost(std::vector<double>{1, 0, 0, -1}, kernel, 1.0), 1.0 + 1.0);
    }

    // Section 14, "no arbitrage": with a linear impact and a kernel that
    // decays (exponentially or as a power law), no round trip makes money.
    TEST(ImpactTradingCost, LinearImpactAdmitsNoProfitableRoundTrip)
    {
        std::vector<double> exponential(400);
        std::vector<double> power_law(400);
        for (std::size_t i = 0; i < 400; ++i)
        {
            exponential[i] = std::exp(-0.05 * static_cast<double>(i));
            power_law[i] = std::pow(1.0 + static_cast<double>(i), -0.4);
        }
        Rng rng(2026);
        for (int trial = 0; trial < 2000; ++trial)
        {
            const std::size_t length = 2 + static_cast<std::size_t>(rng.below(60));
            std::vector<double> trades(length);
            double total = 0.0;
            for (double &q : trades)
            {
                q = rng.normal();
                total += q;
            }
            // Make it a round trip.
            for (double &q : trades)
            {
                q -= total / static_cast<double>(length);
            }
            ASSERT_GE(impactTradingCost(trades, exponential, 1.0), 0.0);
            ASSERT_GE(impactTradingCost(trades, power_law, 1.0), 0.0);
        }
    }

    // The same exponential decay with a concave impact can be manipulated:
    // buying slowly moves the price more per lot than selling quickly brings
    // it back. This is the inconsistency Gatheral (2010) points out, and the
    // check that the test above is able to fail.
    TEST(ImpactTradingCost, ConcaveImpactWithExponentialDecayAdmitsOne)
    {
        std::vector<double> kernel(4000);
        for (std::size_t i = 0; i < kernel.size(); ++i)
        {
            kernel[i] = std::exp(-0.002 * static_cast<double>(i));
        }
        std::vector<double> round_trip;
        for (int i = 0; i < 500; ++i)
        {
            round_trip.push_back(1.0 / 500);
        }
        for (int i = 0; i < 50; ++i)
        {
            round_trip.push_back(-1.0 / 50);
        }
        EXPECT_LT(impactTradingCost(round_trip, kernel, 0.5), 0.0);
        EXPECT_GT(impactTradingCost(round_trip, kernel, 1.0), 0.0);
    }

} // namespace quantMicrostructure
