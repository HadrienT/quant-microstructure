// Acceptance criteria of work package M4 (docs/companion.md, section 13): the
// simulated mean intensity is mu / (1 - n), maximum likelihood finds the
// parameters back, and the time-changed durations are exponential.
#include <gtest/gtest.h>

#include "quantMicrostructure/sim/hawkes.hpp"
#include "quantMicrostructure/sim/rng.hpp"

#include "support/statistics.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

namespace quantMicrostructure
{

    namespace
    {

        // Branching ratio 1/2: half of the events are triggered by another.
        constexpr HawkesParams Truth{1.0, 2.0, 4.0};
        constexpr double Horizon = 20'000.0;

        const std::vector<double> &sample()
        {
            static const std::vector<double> times = []
            {
                Rng rng(2026);
                return simulateHawkes(Truth, Horizon, rng);
            }();
            return times;
        }

    } // namespace

    TEST(Hawkes, BranchingRatioAndMeanIntensity)
    {
        EXPECT_DOUBLE_EQ(hawkesBranchingRatio(Truth), 0.5);
        EXPECT_DOUBLE_EQ(hawkesMeanIntensity(Truth), 2.0);
        EXPECT_THROW(hawkesMeanIntensity(HawkesParams{1.0, 4.0, 4.0}), std::invalid_argument);
        EXPECT_THROW(hawkesBranchingRatio(HawkesParams{0.0, 1.0, 2.0}), std::invalid_argument);
    }

    TEST(Hawkes, TimesAreIncreasingAndInsideTheHorizon)
    {
        const std::vector<double> &times = sample();
        ASSERT_FALSE(times.empty());
        EXPECT_GT(times.front(), 0.0);
        EXPECT_LT(times.back(), Horizon);
        for (std::size_t i = 1; i < times.size(); ++i)
        {
            ASSERT_GT(times[i], times[i - 1]);
        }
    }

    TEST(Hawkes, SimulatedMeanIntensityIsMuOverOneMinusN)
    {
        const double expected = hawkesMeanIntensity(Truth) * Horizon;
        // The count of a stationary Hawkes process over a long window has
        // variance Lambda T / (1 - n)^2: clustering makes it 1 / (1 - n)^2
        // times that of a Poisson process of the same rate.
        const double n = hawkesBranchingRatio(Truth);
        const double sd = std::sqrt(expected) / (1.0 - n);
        EXPECT_NEAR(static_cast<double>(sample().size()), expected, 4.0 * sd);
    }

    // Without excitation the process is a Poisson process of rate mu.
    TEST(Hawkes, NoExcitationIsAPoissonProcess)
    {
        const HawkesParams poisson{3.0, 0.0, 1.0};
        Rng rng(7);
        const std::vector<double> times = simulateHawkes(poisson, 5'000.0, rng);
        EXPECT_NEAR(static_cast<double>(times.size()), 15'000.0, 4.0 * std::sqrt(15'000.0));

        std::vector<double> scaled;
        double previous = 0.0;
        for (const double t : times)
        {
            scaled.push_back(poisson.mu * (t - previous));
            previous = t;
        }
        EXPECT_LT(testing::scaledKsDistanceToExponential(scaled), testing::KsCritical99);
    }

    // The recursion against the definition, with its double sum (Laub, Taimre
    // & Pollett 2015, equation 19, with the compensator taken to the horizon).
    TEST(Hawkes, RecursiveLikelihoodMatchesTheDirectSum)
    {
        const std::vector<double> times{0.3, 0.35, 1.2, 1.25, 1.3, 4.0};
        const double horizon = 5.0;
        const HawkesParams p{0.7, 1.5, 3.0};

        double direct = -p.mu * horizon;
        for (std::size_t i = 0; i < times.size(); ++i)
        {
            double intensity = p.mu;
            for (std::size_t j = 0; j < i; ++j)
            {
                intensity += p.alpha * std::exp(-p.beta * (times[i] - times[j]));
            }
            direct += std::log(intensity);
            direct -= p.alpha / p.beta * (1.0 - std::exp(-p.beta * (horizon - times[i])));
        }
        EXPECT_NEAR(hawkesLogLikelihood(p, times, horizon), direct, 1e-12);
    }

    // The residuals add up to the compensator at the last event.
    TEST(Hawkes, ResidualsSumToTheCompensator)
    {
        const std::vector<double> times{0.3, 0.35, 1.2, 1.25, 1.3, 4.0};
        const HawkesParams p{0.7, 1.5, 3.0};

        double compensator = p.mu * times.back();
        for (const double t : times)
        {
            compensator += p.alpha / p.beta * (1.0 - std::exp(-p.beta * (times.back() - t)));
        }
        double sum = 0.0;
        for (const double r : hawkesResiduals(p, times))
        {
            EXPECT_GT(r, 0.0);
            sum += r;
        }
        EXPECT_NEAR(sum, compensator, 1e-12);
    }

    TEST(Hawkes, TimeChangedDurationsAreExponential)
    {
        const std::vector<double> residuals = hawkesResiduals(Truth, sample());
        EXPECT_LT(testing::scaledKsDistanceToExponential(residuals), testing::KsCritical99);
        // And independent, as far as the first autocorrelation can tell.
        const double rho = testing::autocovariance(residuals, 1) / testing::variance(residuals);
        EXPECT_LT(std::abs(rho), 4.0 / std::sqrt(static_cast<double>(residuals.size())));
    }

    // The test above can fail: the same times, read through a Poisson model of
    // the same mean rate, are rejected.
    TEST(Hawkes, WrongModelFailsTheTimeChangeTest)
    {
        const HawkesParams poisson{hawkesMeanIntensity(Truth), 0.0, 1.0};
        const std::vector<double> residuals = hawkesResiduals(poisson, sample());
        EXPECT_GT(testing::scaledKsDistanceToExponential(residuals), 5.0);
    }

    TEST(Hawkes, MaximumLikelihoodRecoversTheParameters)
    {
        const HawkesFit fit = fitHawkes(sample(), Horizon);
        EXPECT_TRUE(fit.converged);
        EXPECT_NEAR(fit.params.mu, Truth.mu, 0.05 * Truth.mu);
        EXPECT_NEAR(fit.params.alpha, Truth.alpha, 0.05 * Truth.alpha);
        EXPECT_NEAR(fit.params.beta, Truth.beta, 0.05 * Truth.beta);
        // The maximum is at least as likely as the truth.
        EXPECT_GE(fit.log_likelihood, hawkesLogLikelihood(Truth, sample(), Horizon));
        // And the fitted model passes its own validation.
        EXPECT_LT(testing::scaledKsDistanceToExponential(hawkesResiduals(fit.params, sample())), testing::KsCritical99);
    }

    TEST(Hawkes, SameSeedSameTimes)
    {
        Rng a(11);
        Rng b(11);
        EXPECT_EQ(simulateHawkes(Truth, 200.0, a), simulateHawkes(Truth, 200.0, b));
    }

    TEST(Hawkes, FitNeedsEvents)
    {
        const std::vector<double> one{1.0};
        EXPECT_THROW(fitHawkes(one, 10.0), std::invalid_argument);
    }

} // namespace quantMicrostructure
