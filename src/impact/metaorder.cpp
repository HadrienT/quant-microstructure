#include "quantMicrostructure/impact/metaorder.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace quantMicrostructure
{

    namespace
    {

        constexpr double NanosPerSecond = 1e9;

        std::optional<double> midOf(const OrderBook &book)
        {
            const auto bid = book.bestBid();
            const auto ask = book.bestAsk();
            if (!bid || !ask)
            {
                return std::nullopt;
            }
            return 0.5 * static_cast<double>(*bid + *ask);
        }

        /// Rise of the mid over the execution window, with `children` market
        /// buys (possibly none) spread over it.
        std::optional<double> midRise(const MetaorderExperiment &experiment, std::uint64_t seed, std::size_t window_children, bool send)
        {
            ZeroIntelligenceSimulator sim(experiment.market, seed);
            std::vector<Event> events;
            const auto at = [&](double seconds)
            { return static_cast<Timestamp>(seconds * NanosPerSecond); };
            sim.runUntil(at(experiment.warmup), events);
            const std::optional<double> before = midOf(sim.book());
            for (std::size_t child = 0; child < window_children; ++child)
            {
                events.clear();
                sim.runUntil(at(experiment.warmup + static_cast<double>(child) * experiment.child_interval), events);
                if (send)
                {
                    sim.submitMarket(Side::Buy, experiment.market.order_size, events);
                }
            }
            const std::optional<double> after = midOf(sim.book());
            if (!before || !after)
            {
                return std::nullopt;
            }
            return *after - *before;
        }

    } // namespace

    std::vector<MetaorderImpactPoint> measureMetaorderImpact(const MetaorderExperiment &experiment)
    {
        if (experiment.repetitions < 2 || !(experiment.child_interval > 0.0) || experiment.warmup < 0.0)
        {
            throw std::invalid_argument("measureMetaorderImpact: need at least two repetitions, a positive child interval and a non-negative warm-up");
        }
        std::vector<MetaorderImpactPoint> out;
        std::uint64_t seed = experiment.seed;
        for (const std::size_t children : experiment.children)
        {
            MetaorderImpactPoint point;
            point.children = children;
            point.size = static_cast<Quantity>(children) * experiment.market.order_size;
            double sum = 0.0;
            double sum_squares = 0.0;
            for (std::size_t r = 0; r < experiment.repetitions; ++r, ++seed)
            {
                const std::optional<double> with = midRise(experiment, seed, children, true);
                const std::optional<double> without = midRise(experiment, seed, children, false);
                if (!with || !without)
                {
                    continue;
                }
                const double excess = *with - *without;
                sum += excess;
                sum_squares += excess * excess;
                ++point.samples;
            }
            if (point.samples >= 2)
            {
                const double n = static_cast<double>(point.samples);
                point.impact = sum / n;
                const double variance = std::max(0.0, (sum_squares - n * point.impact * point.impact) / (n - 1.0));
                point.standard_error = std::sqrt(variance / n);
            }
            out.push_back(point);
        }
        return out;
    }

    PowerLawFit fitPowerLaw(std::span<const double> x, std::span<const double> y)
    {
        const std::size_t n = x.size();
        if (y.size() != n || n < 2)
        {
            throw std::invalid_argument("fitPowerLaw: need two samples of the same length, at least two points");
        }
        double mean_x = 0.0;
        double mean_y = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            if (!(x[i] > 0.0) || !(y[i] > 0.0))
            {
                throw std::invalid_argument("fitPowerLaw: values must be positive");
            }
            mean_x += std::log(x[i]);
            mean_y += std::log(y[i]);
        }
        mean_x /= static_cast<double>(n);
        mean_y /= static_cast<double>(n);
        double sxx = 0.0;
        double sxy = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            sxx += (std::log(x[i]) - mean_x) * (std::log(x[i]) - mean_x);
            sxy += (std::log(x[i]) - mean_x) * (std::log(y[i]) - mean_y);
        }
        if (!(sxx > 0.0))
        {
            throw std::invalid_argument("fitPowerLaw: x is constant");
        }
        PowerLawFit fit;
        fit.exponent = sxy / sxx;
        fit.prefactor = std::exp(mean_y - fit.exponent * mean_x);
        return fit;
    }

} // namespace quantMicrostructure
