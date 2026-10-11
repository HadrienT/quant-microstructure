#include "quantMicrostructure/sim/hawkes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace quantMicrostructure
{

    namespace
    {

        void requireValid(const HawkesParams &params)
        {
            if (!(params.mu > 0.0) || !(params.alpha >= 0.0) || !(params.beta > 0.0))
            {
                throw std::invalid_argument("Hawkes: mu and beta must be positive, alpha must not be negative");
            }
        }

        using Point = std::array<double, 3>;

        HawkesParams fromLog(const Point &x)
        {
            return HawkesParams{std::exp(x[0]), std::exp(x[1]), std::exp(x[2])};
        }

    } // namespace

    double hawkesBranchingRatio(const HawkesParams &params)
    {
        requireValid(params);
        return params.alpha / params.beta;
    }

    double hawkesMeanIntensity(const HawkesParams &params)
    {
        const double n = hawkesBranchingRatio(params);
        if (n >= 1.0)
        {
            throw std::invalid_argument("Hawkes: not stationary, the branching ratio is not below 1");
        }
        return params.mu / (1.0 - n);
    }

    std::vector<double> simulateHawkes(const HawkesParams &params, double horizon, Rng &rng)
    {
        requireValid(params);
        std::vector<double> times;
        double t = 0.0;
        // The excited part of the intensity, lambda(t) - mu, just after t.
        double excitation = 0.0;
        while (true)
        {
            const double bound = params.mu + excitation;
            const double wait = rng.exponential(bound);
            t += wait;
            if (t >= horizon)
            {
                return times;
            }
            excitation *= std::exp(-params.beta * wait);
            if (rng.uniform() * bound <= params.mu + excitation)
            {
                times.push_back(t);
                excitation += params.alpha;
            }
        }
    }

    double hawkesLogLikelihood(const HawkesParams &params, std::span<const double> times, double horizon)
    {
        requireValid(params);
        const double ratio = params.alpha / params.beta;
        double log_intensities = 0.0;
        double compensator = params.mu * horizon;
        // A(i) of equation (20): the sum of exp(-beta (t_i - t_j)) over j < i.
        double a = 0.0;
        for (std::size_t i = 0; i < times.size(); ++i)
        {
            if (i > 0)
            {
                a = std::exp(-params.beta * (times[i] - times[i - 1])) * (1.0 + a);
            }
            log_intensities += std::log(params.mu + params.alpha * a);
            compensator += ratio * (1.0 - std::exp(-params.beta * (horizon - times[i])));
        }
        return log_intensities - compensator;
    }

    std::vector<double> hawkesResiduals(const HawkesParams &params, std::span<const double> times)
    {
        requireValid(params);
        const double ratio = params.alpha / params.beta;
        std::vector<double> residuals;
        residuals.reserve(times.size());
        // Sum of exp(-beta (t_{i-1} - t_j)) over j <= i - 1: the excitation,
        // in units of alpha, just after the previous event.
        double excited = 0.0;
        double previous = 0.0;
        for (const double t : times)
        {
            const double decay = std::exp(-params.beta * (t - previous));
            residuals.push_back(params.mu * (t - previous) + ratio * excited * (1.0 - decay));
            excited = excited * decay + 1.0;
            previous = t;
        }
        return residuals;
    }

    HawkesFit fitHawkes(std::span<const double> times, double horizon)
    {
        if (times.size() < 2)
        {
            throw std::invalid_argument("fitHawkes: at least two events are needed");
        }
        const auto cost = [&](const Point &x)
        {
            const double value = -hawkesLogLikelihood(fromLog(x), times, horizon);
            return std::isfinite(value) ? value : std::numeric_limits<double>::infinity();
        };

        // Start from half the events being triggered, with an excitation that
        // lasts about one mean waiting time.
        const double rate = static_cast<double>(times.size()) / horizon;
        const Point start{std::log(0.5 * rate), std::log(0.5 * rate), std::log(rate)};

        constexpr int MaxIterations = 2000;
        constexpr double Tolerance = 1e-10;

        std::array<Point, 4> simplex;
        std::array<double, 4> values{};
        simplex.fill(start);
        for (std::size_t i = 0; i < 3; ++i)
        {
            simplex[i + 1][i] += 0.5;
        }
        for (std::size_t i = 0; i < 4; ++i)
        {
            values[i] = cost(simplex[i]);
        }

        const auto along = [](const Point &from, const Point &to, double step)
        {
            Point p{};
            for (std::size_t k = 0; k < 3; ++k)
            {
                p[k] = from[k] + step * (to[k] - from[k]);
            }
            return p;
        };

        HawkesFit fit;
        for (fit.iterations = 0; fit.iterations < MaxIterations; ++fit.iterations)
        {
            std::array<std::size_t, 4> order{0, 1, 2, 3};
            std::sort(order.begin(), order.end(), [&](std::size_t l, std::size_t r)
                      { return values[l] < values[r]; });
            const std::size_t best = order[0];
            const std::size_t second_worst = order[2];
            const std::size_t worst = order[3];

            if (values[worst] - values[best] <= Tolerance * (std::abs(values[best]) + Tolerance))
            {
                fit.converged = true;
                break;
            }

            Point centroid{};
            for (std::size_t i = 0; i < 4; ++i)
            {
                if (i != worst)
                {
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        centroid[k] += simplex[i][k] / 3.0;
                    }
                }
            }

            const Point reflected = along(simplex[worst], centroid, 2.0);
            const double reflected_value = cost(reflected);
            if (reflected_value < values[best])
            {
                const Point expanded = along(simplex[worst], centroid, 3.0);
                const double expanded_value = cost(expanded);
                const bool take_expanded = expanded_value < reflected_value;
                simplex[worst] = take_expanded ? expanded : reflected;
                values[worst] = take_expanded ? expanded_value : reflected_value;
            }
            else if (reflected_value < values[second_worst])
            {
                simplex[worst] = reflected;
                values[worst] = reflected_value;
            }
            else
            {
                const Point contracted = along(simplex[worst], centroid, 0.5);
                const double contracted_value = cost(contracted);
                if (contracted_value < values[worst])
                {
                    simplex[worst] = contracted;
                    values[worst] = contracted_value;
                }
                else
                {
                    for (std::size_t i = 0; i < 4; ++i)
                    {
                        if (i != best)
                        {
                            simplex[i] = along(simplex[best], simplex[i], 0.5);
                            values[i] = cost(simplex[i]);
                        }
                    }
                }
            }
        }

        const std::size_t best = static_cast<std::size_t>(std::min_element(values.begin(), values.end()) - values.begin());
        fit.params = fromLog(simplex[best]);
        fit.log_likelihood = -values[best];
        return fit;
    }

} // namespace quantMicrostructure
