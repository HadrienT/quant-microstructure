#include "quantMicrostructure/mm/quoting.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace quantMicrostructure
{

    namespace
    {

        void requireValid(const QuotingParams &params)
        {
            if (!(params.sigma >= 0.0) || !(params.gamma > 0.0) || !(params.k > 0.0) || !(params.A > 0.0))
            {
                throw std::invalid_argument("quoting: gamma, k and A must be positive, sigma non-negative");
            }
        }

        /// (1 / gamma) ln(1 + gamma / k): half the spread that remains when
        /// there is no inventory risk.
        double baseDistance(const QuotingParams &params)
        {
            return std::log1p(params.gamma / params.k) / params.gamma;
        }

    } // namespace

    double avellanedaStoikovReservationPrice(double reference, double inventory, const QuotingParams &params, double time_left)
    {
        requireValid(params);
        return reference - inventory * params.gamma * params.sigma * params.sigma * time_left;
    }

    double avellanedaStoikovSpread(const QuotingParams &params, double time_left)
    {
        requireValid(params);
        return params.gamma * params.sigma * params.sigma * time_left + 2.0 * baseDistance(params);
    }

    Quotes avellanedaStoikovQuotes(double inventory, const QuotingParams &params, double time_left)
    {
        const double half_spread = 0.5 * avellanedaStoikovSpread(params, time_left);
        // Reservation price minus reference price.
        const double shift = avellanedaStoikovReservationPrice(0.0, inventory, params, time_left);
        return Quotes{half_spread - shift, half_spread + shift};
    }

    Quotes symmetricQuotes(const QuotingParams &params, double time_left)
    {
        const double half_spread = 0.5 * avellanedaStoikovSpread(params, time_left);
        return Quotes{half_spread, half_spread};
    }

    Quotes glftApproximateQuotes(double inventory, const QuotingParams &params)
    {
        requireValid(params);
        const double ratio = params.gamma / params.k;
        const double c = std::sqrt(params.sigma * params.sigma * params.gamma / (2.0 * params.k * params.A) * std::pow(1.0 + ratio, 1.0 + 1.0 / ratio));
        const double base = baseDistance(params);
        return Quotes{base + 0.5 * (2.0 * inventory + 1.0) * c, base - 0.5 * (2.0 * inventory - 1.0) * c};
    }

    GlftSolution::GlftSolution(const QuotingParams &params, int inventory_limit, double time_left)
        : params_(params),
          limit_(inventory_limit)
    {
        requireValid(params);
        if (inventory_limit < 1 || time_left < 0.0)
        {
            throw std::invalid_argument("GlftSolution: the inventory limit must be at least 1 and the time left non-negative");
        }
        const double ratio = params.gamma / params.k;
        const double alpha = 0.5 * params.k * params.gamma * params.sigma * params.sigma;
        const double eta = params.A * std::pow(1.0 + ratio, -(1.0 + 1.0 / ratio));
        const std::size_t size = static_cast<std::size_t>(2 * inventory_limit + 1);

        // y = -M x for the tridiagonal M of proposition 2.
        const auto minusM = [&](const std::vector<double> &x)
        {
            std::vector<double> y(size);
            for (std::size_t i = 0; i < size; ++i)
            {
                const double q = static_cast<double>(i) - static_cast<double>(inventory_limit);
                y[i] = -alpha * q * q * x[i];
                if (i > 0)
                {
                    y[i] += eta * x[i - 1];
                }
                if (i + 1 < size)
                {
                    y[i] += eta * x[i + 1];
                }
            }
            return y;
        };

        // exp(-M tau) applied to the vector of ones, by a Taylor series on
        // steps short enough for it to converge fast. The vector is rescaled
        // at each step: only ratios of its entries matter, and they would
        // otherwise under- or overflow over a long horizon.
        const double norm = alpha * static_cast<double>(inventory_limit) * static_cast<double>(inventory_limit) + 2.0 * eta;
        const std::size_t steps = static_cast<std::size_t>(std::ceil(time_left * norm / 0.25)) + 1;
        const double h = time_left / static_cast<double>(steps);
        v_.assign(size, 1.0);
        for (std::size_t step = 0; step < steps; ++step)
        {
            std::vector<double> term = v_;
            std::vector<double> sum = v_;
            for (int order = 1; order <= 16; ++order)
            {
                term = minusM(term);
                for (std::size_t i = 0; i < size; ++i)
                {
                    term[i] *= h / static_cast<double>(order);
                    sum[i] += term[i];
                }
            }
            double largest = 0.0;
            for (const double value : sum)
            {
                largest = std::max(largest, value);
            }
            for (std::size_t i = 0; i < size; ++i)
            {
                v_[i] = sum[i] / largest;
            }
        }
    }

    Quotes GlftSolution::quotes(int q) const
    {
        if (q < -limit_ || q > limit_)
        {
            throw std::invalid_argument("GlftSolution::quotes: inventory beyond the limit");
        }
        const std::size_t i = static_cast<std::size_t>(q + limit_);
        const double base = baseDistance(params_);
        const double infinity = std::numeric_limits<double>::infinity();
        Quotes out;
        out.bid = q == limit_ ? infinity : std::log(v_[i] / v_[i + 1]) / params_.k + base;
        out.ask = q == -limit_ ? infinity : std::log(v_[i] / v_[i - 1]) / params_.k + base;
        return out;
    }

    std::vector<QuotingOutcome> simulateQuoting(const QuotingSimulation &simulation, const QuotePolicy &policy, std::size_t paths, Rng &rng)
    {
        requireValid(simulation.params);
        if (!(simulation.dt > 0.0) || !(simulation.horizon > 0.0))
        {
            throw std::invalid_argument("simulateQuoting: horizon and time step must be positive");
        }
        const std::size_t steps = static_cast<std::size_t>(std::llround(simulation.horizon / simulation.dt));
        const double move = simulation.params.sigma * std::sqrt(simulation.dt);
        std::vector<QuotingOutcome> outcomes;
        outcomes.reserve(paths);
        for (std::size_t path = 0; path < paths; ++path)
        {
            double reference = simulation.s0;
            double cash = 0.0;
            double inventory = 0.0;
            for (std::size_t step = 0; step < steps; ++step)
            {
                const double time_left = simulation.horizon - static_cast<double>(step) * simulation.dt;
                const Quotes quotes = policy(inventory, time_left);
                if (rng.uniform() < simulation.params.A * std::exp(-simulation.params.k * quotes.ask) * simulation.dt)
                {
                    inventory -= 1.0;
                    cash += reference + quotes.ask;
                }
                if (rng.uniform() < simulation.params.A * std::exp(-simulation.params.k * quotes.bid) * simulation.dt)
                {
                    inventory += 1.0;
                    cash -= reference - quotes.bid;
                }
                reference += rng.below(2) == 0 ? move : -move;
            }
            outcomes.push_back(QuotingOutcome{cash + inventory * reference, inventory});
        }
        return outcomes;
    }

} // namespace quantMicrostructure
