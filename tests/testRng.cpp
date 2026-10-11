#include <gtest/gtest.h>

#include "quantMicrostructure/sim/rng.hpp"

#include "support/statistics.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace quantMicrostructure
{

    namespace
    {

        constexpr std::size_t Draws = 200'000;

    } // namespace

    TEST(Rng, SameSeedSameDraws)
    {
        Rng a(42);
        Rng b(42);
        Rng c(43);
        bool differs = false;
        for (int i = 0; i < 100; ++i)
        {
            const double x = a.uniform();
            EXPECT_EQ(x, b.uniform());
            differs = differs || x != c.uniform();
        }
        EXPECT_TRUE(differs);
    }

    // std::mt19937_64 is specified: its 10000th output with the default seed
    // is given by the C++ standard. The integer stream is the same everywhere.
    TEST(Rng, EngineIsTheStandardMersenneTwister)
    {
        Rng rng(5489);
        std::uint64_t last = 0;
        for (int i = 0; i < 10000; ++i)
        {
            last = rng.next();
        }
        EXPECT_EQ(last, 9981545732273789042ull);
    }

    TEST(Rng, UniformIsInsideTheOpenUnitInterval)
    {
        Rng rng(1);
        std::vector<double> x;
        for (std::size_t i = 0; i < Draws; ++i)
        {
            x.push_back(rng.uniform());
            ASSERT_GT(x.back(), 0.0);
            ASSERT_LT(x.back(), 1.0);
        }
        // Mean 1/2 and variance 1/12, within five standard errors.
        EXPECT_NEAR(testing::mean(x), 0.5, 5.0 * std::sqrt(1.0 / 12.0 / Draws));
        EXPECT_NEAR(testing::variance(x), 1.0 / 12.0, 0.001);
    }

    TEST(Rng, BelowCoversItsRangeEvenly)
    {
        Rng rng(2);
        constexpr std::uint64_t Bound = 7;
        std::vector<std::size_t> counts(Bound, 0);
        for (std::size_t i = 0; i < Draws; ++i)
        {
            const std::uint64_t v = rng.below(Bound);
            ASSERT_LT(v, Bound);
            ++counts[v];
        }
        const double expected = static_cast<double>(Draws) / Bound;
        for (const std::size_t count : counts)
        {
            EXPECT_NEAR(static_cast<double>(count), expected, 5.0 * std::sqrt(expected));
        }
        EXPECT_EQ(rng.below(1), 0u);
    }

    TEST(Rng, ExponentialHasTheRightLaw)
    {
        Rng rng(3);
        constexpr double Rate = 4.0;
        std::vector<double> scaled;
        for (std::size_t i = 0; i < Draws; ++i)
        {
            scaled.push_back(Rate * rng.exponential(Rate));
        }
        EXPECT_LT(testing::scaledKsDistanceToExponential(scaled), testing::KsCritical99);
    }

    TEST(Rng, NormalHasMeanZeroAndVarianceOne)
    {
        Rng rng(4);
        std::vector<double> x;
        std::size_t beyond_two = 0;
        for (std::size_t i = 0; i < Draws; ++i)
        {
            x.push_back(rng.normal());
            beyond_two += std::abs(x.back()) > 2.0 ? 1 : 0;
        }
        EXPECT_NEAR(testing::mean(x), 0.0, 5.0 / std::sqrt(static_cast<double>(Draws)));
        EXPECT_NEAR(testing::variance(x), 1.0, 0.02);
        // P(|Z| > 2) = 4.55 %.
        EXPECT_NEAR(static_cast<double>(beyond_two) / Draws, 0.0455, 0.003);
    }

} // namespace quantMicrostructure
