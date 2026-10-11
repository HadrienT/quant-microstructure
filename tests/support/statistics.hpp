#ifndef QUANTMICROSTRUCTURE_TESTS_SUPPORT_STATISTICS_HPP
#define QUANTMICROSTRUCTURE_TESTS_SUPPORT_STATISTICS_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace quantMicrostructure::testing
{

    inline double mean(std::span<const double> x)
    {
        double sum = 0.0;
        for (const double v : x)
        {
            sum += v;
        }
        return sum / static_cast<double>(x.size());
    }

    /// Covariance of x[i] and y[i], with the 1 / n normalisation.
    inline double covariance(std::span<const double> x, std::span<const double> y)
    {
        const double mx = mean(x);
        const double my = mean(y);
        double sum = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            sum += (x[i] - mx) * (y[i] - my);
        }
        return sum / static_cast<double>(x.size());
    }

    inline double variance(std::span<const double> x)
    {
        return covariance(x, x);
    }

    /// Covariance of x[i] and x[i - lag].
    inline double autocovariance(std::span<const double> x, std::size_t lag)
    {
        return covariance(x.subspan(lag), x.first(x.size() - lag));
    }

    /// Kolmogorov-Smirnov distance between the sample and the exponential law
    /// of mean 1, scaled by sqrt(n). Under that law it stays below 1.36 with
    /// probability 95 % and below 1.63 with probability 99 % (asymptotic
    /// critical values of the Kolmogorov distribution).
    inline double scaledKsDistanceToExponential(std::span<const double> sample)
    {
        std::vector<double> sorted(sample.begin(), sample.end());
        std::sort(sorted.begin(), sorted.end());
        const double n = static_cast<double>(sorted.size());
        double distance = 0.0;
        for (std::size_t i = 0; i < sorted.size(); ++i)
        {
            const double cdf = 1.0 - std::exp(-sorted[i]);
            distance = std::max(distance, std::abs(cdf - static_cast<double>(i) / n));
            distance = std::max(distance, std::abs(static_cast<double>(i + 1) / n - cdf));
        }
        return distance * std::sqrt(n);
    }

    inline constexpr double KsCritical99 = 1.63;

} // namespace quantMicrostructure::testing

#endif
