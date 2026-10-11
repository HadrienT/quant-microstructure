// Each toy model gives back its closed formula (docs/companion.md, section
// 14, "models with a known truth").
#include <gtest/gtest.h>

#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/sim/rng.hpp"
#include "quantMicrostructure/sim/toy_models.hpp"

#include "support/statistics.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

namespace quantMicrostructure
{

    // ---- Roll ----

    TEST(Roll, StreamIsMadeOfTradesOneTickGridApart)
    {
        Rng rng(1);
        const RollParams params{3, 1, 10'000, 500};
        const std::vector<Event> trades = simulateRoll(params, 1000, rng);
        ASSERT_EQ(trades.size(), 1000u);
        for (std::size_t i = 0; i < trades.size(); ++i)
        {
            EXPECT_EQ(trades[i].type, EventType::Trade);
            EXPECT_EQ(trades[i].qty, 1);
            EXPECT_EQ(trades[i].seq, i + 1);
            EXPECT_EQ(trades[i].ts, static_cast<Timestamp>(i) * 500);
        }
    }

    TEST(Roll, SerialCovarianceGivesBackTheSpread)
    {
        Rng rng(2026);
        const RollParams params{4, 1, 10'000, 1'000'000'000};
        const std::vector<Event> trades = simulateRoll(params, 400'000, rng);

        std::vector<double> changes;
        std::vector<double> signs;
        for (std::size_t i = 1; i < trades.size(); ++i)
        {
            changes.push_back(static_cast<double>(trades[i].price - trades[i - 1].price));
        }
        for (const Event &trade : trades)
        {
            signs.push_back(trade.side == Side::Buy ? 1.0 : -1.0);
        }

        // Cov = -s^2 / 4, so s = 2 sqrt(-Cov); here s = 8 ticks.
        const double cov = testing::autocovariance(changes, 1);
        ASSERT_LT(cov, 0.0);
        EXPECT_NEAR(2.0 * std::sqrt(-cov), 8.0, 0.1);
        // The model's assumptions hold in the sample: balanced, independent
        // signs, and no covariance beyond the first lag.
        EXPECT_NEAR(testing::mean(signs), 0.0, 0.01);
        EXPECT_NEAR(testing::autocovariance(signs, 1), 0.0, 0.01);
        EXPECT_NEAR(testing::autocovariance(changes, 2), 0.0, 0.2);
    }

    TEST(Roll, WithoutSpreadPriceChangesAreNotCorrelated)
    {
        Rng rng(3);
        const std::vector<Event> trades = simulateRoll(RollParams{0, 1, 10'000, 1}, 400'000, rng);
        std::vector<double> changes;
        for (std::size_t i = 1; i < trades.size(); ++i)
        {
            changes.push_back(static_cast<double>(trades[i].price - trades[i - 1].price));
        }
        EXPECT_NEAR(testing::autocovariance(changes, 1), 0.0, 0.01);
    }

    // ---- Kyle ----

    TEST(Kyle, EquilibriumFormulas)
    {
        const KyleEquilibrium e = kyleEquilibrium(KyleParams{100.0, 2.0, 5.0});
        EXPECT_DOUBLE_EQ(e.lambda, 0.2);
        EXPECT_DOUBLE_EQ(e.beta, 2.5);
        EXPECT_THROW(kyleEquilibrium(KyleParams{100.0, 0.0, 5.0}), std::invalid_argument);
    }

    TEST(Kyle, PriceIsTheExpectedValueGivenTheFlow)
    {
        const KyleParams params{100.0, 2.0, 5.0};
        const KyleEquilibrium e = kyleEquilibrium(params);
        Rng rng(2026);
        const std::vector<KyleAuction> auctions = simulateKyle(params, 400'000, rng);

        std::vector<double> flow;
        std::vector<double> value;
        std::vector<double> surprise;
        for (const KyleAuction &a : auctions)
        {
            flow.push_back(a.insider_order + a.noise_order);
            value.push_back(a.value - params.p0);
            surprise.push_back(a.value - a.price);
        }
        // The market maker's rule is rational: regressing the value (which the
        // simulation never used to set the price) on the flow gives lambda.
        EXPECT_NEAR(testing::covariance(value, flow) / testing::variance(flow), e.lambda, 0.01 * e.lambda);
        // What the price leaves unknown is not predictable from the flow...
        EXPECT_NEAR(testing::covariance(surprise, flow), 0.0, 0.05);
        // ...and is half of the insider's information.
        EXPECT_NEAR(testing::variance(surprise), params.sigma_v * params.sigma_v / 2.0, 0.02);
    }

    // The insider's rule is the best response to the market maker's: with the
    // same values and the same noise, trading 20 % more or 20 % less earns
    // less.
    TEST(Kyle, InsiderCannotDoBetter)
    {
        const KyleParams params{100.0, 2.0, 5.0};
        const KyleEquilibrium e = kyleEquilibrium(params);
        Rng rng(5);
        const std::vector<KyleAuction> auctions = simulateKyle(params, 200'000, rng);

        const auto profit = [&](double scale)
        {
            double total = 0.0;
            for (const KyleAuction &a : auctions)
            {
                const double x = scale * a.insider_order;
                const double price = params.p0 + e.lambda * (x + a.noise_order);
                total += (a.value - price) * x;
            }
            return total / static_cast<double>(auctions.size());
        };
        const double at_equilibrium = profit(1.0);
        // Expected profit of the insider: sigma_v sigma_u / 2.
        EXPECT_NEAR(at_equilibrium, params.sigma_v * params.sigma_u / 2.0, 0.1);
        EXPECT_GT(at_equilibrium, profit(1.2));
        EXPECT_GT(at_equilibrium, profit(0.8));
    }

    // ---- Glosten-Milgrom ----

    TEST(GlostenMilgrom, SpreadAtAnEvenPriorIsInformedShareTimesRange)
    {
        const GlostenMilgromParams params{90.0, 110.0, 0.3, 0.5};
        const GlostenMilgromQuotes q = glostenMilgromQuotes(params, 0.5);
        EXPECT_NEAR(q.ask - q.bid, 0.3 * 20.0, 1e-12);
        EXPECT_NEAR(q.ask, 103.0, 1e-12);
        EXPECT_NEAR(q.bid, 97.0, 1e-12);
    }

    TEST(GlostenMilgrom, NoInformedTraderNoSpread)
    {
        const GlostenMilgromQuotes q = glostenMilgromQuotes(GlostenMilgromParams{90.0, 110.0, 0.0, 0.5}, 0.3);
        EXPECT_DOUBLE_EQ(q.ask, q.bid);
        EXPECT_DOUBLE_EQ(q.ask, 96.0);
    }

    TEST(GlostenMilgrom, RejectsInvalidInput)
    {
        EXPECT_THROW(glostenMilgromQuotes(GlostenMilgromParams{110.0, 90.0, 0.3, 0.5}, 0.5), std::invalid_argument);
        EXPECT_THROW(glostenMilgromQuotes(GlostenMilgromParams{90.0, 110.0, 1.3, 0.5}, 0.5), std::invalid_argument);
        EXPECT_THROW(glostenMilgromQuotes(GlostenMilgromParams{90.0, 110.0, 0.3, 0.5}, 1.5), std::invalid_argument);
    }

    // The quotes earn nothing on average: the mean value behind the buys is the
    // ask, the mean value behind the sells is the bid. Checked on the first
    // trade of many independent markets.
    TEST(GlostenMilgrom, QuotesMakeZeroExpectedProfit)
    {
        const GlostenMilgromParams params{90.0, 110.0, 0.3, 0.5};
        const GlostenMilgromQuotes quotes = glostenMilgromQuotes(params, params.prior_high);
        Rng rng(2026);
        double value_of_buys = 0.0;
        double value_of_sells = 0.0;
        std::size_t buys = 0;
        std::size_t sells = 0;
        for (int i = 0; i < 400'000; ++i)
        {
            const GlostenMilgromPath path = simulateGlostenMilgrom(params, 1, rng);
            const double value = path.value_is_high ? params.v_high : params.v_low;
            if (path.trades[0].side == Side::Buy)
            {
                value_of_buys += value;
                ++buys;
            }
            else
            {
                value_of_sells += value;
                ++sells;
            }
        }
        EXPECT_NEAR(value_of_buys / static_cast<double>(buys), quotes.ask, 0.1);
        EXPECT_NEAR(value_of_sells / static_cast<double>(sells), quotes.bid, 0.1);
    }

    // Trades reveal the value: the belief goes to the truth, the quotes close
    // on the value and the spread vanishes.
    TEST(GlostenMilgrom, TradesRevealTheValue)
    {
        const GlostenMilgromParams params{90.0, 110.0, 0.3, 0.5};
        Rng rng(8);
        for (int market = 0; market < 20; ++market)
        {
            const GlostenMilgromPath path = simulateGlostenMilgrom(params, 2000, rng);
            const double value = path.value_is_high ? params.v_high : params.v_low;
            const GlostenMilgromTrade &first = path.trades.front();
            const GlostenMilgromTrade &last = path.trades.back();
            EXPECT_NEAR(first.ask - first.bid, 6.0, 1e-12);
            EXPECT_NEAR(last.belief_after, path.value_is_high ? 1.0 : 0.0, 1e-6);
            EXPECT_NEAR(last.price, value, 1e-3);
            EXPECT_LT(last.ask - last.bid, 1e-3);
            for (const GlostenMilgromTrade &trade : path.trades)
            {
                ASSERT_LE(trade.bid, trade.ask);
                ASSERT_EQ(trade.price, trade.side == Side::Buy ? trade.ask : trade.bid);
            }
        }
    }

} // namespace quantMicrostructure
