#include "quantMicrostructure/execution/almgren_chriss.hpp"

#include <cmath>
#include <stdexcept>

namespace quantMicrostructure
{

    namespace
    {

        double etaTilde(const AlmgrenChrissParams &params, double tau)
        {
            const double value = params.eta - 0.5 * params.gamma * tau;
            if (!(value > 0.0))
            {
                throw std::invalid_argument("Almgren-Chriss: eta - gamma tau / 2 must be positive");
            }
            return value;
        }

        void requireGrid(double horizon, std::size_t intervals)
        {
            if (!(horizon > 0.0) || intervals == 0)
            {
                throw std::invalid_argument("Almgren-Chriss: horizon and number of intervals must be positive");
            }
        }

    } // namespace

    TradeSchedule scheduleFromTrades(std::span<const double> trades, double tau)
    {
        if (!(tau > 0.0))
        {
            throw std::invalid_argument("scheduleFromTrades: tau must be positive");
        }
        TradeSchedule schedule;
        schedule.tau = tau;
        schedule.trades.assign(trades.begin(), trades.end());
        schedule.holdings.assign(trades.size() + 1, 0.0);
        // Summed from the end, so that the last holding is exactly zero.
        for (std::size_t k = trades.size(); k-- > 0;)
        {
            schedule.holdings[k] = schedule.holdings[k + 1] + trades[k];
        }
        return schedule;
    }

    double almgrenChrissKappa(const AlmgrenChrissParams &params, double lambda, double tau)
    {
        if (lambda < 0.0 || !(tau > 0.0))
        {
            throw std::invalid_argument("Almgren-Chriss: lambda must not be negative and tau must be positive");
        }
        const double kappa_tilde_squared = lambda * params.sigma * params.sigma / etaTilde(params, tau);
        return std::acosh(1.0 + 0.5 * kappa_tilde_squared * tau * tau) / tau;
    }

    TradeSchedule almgrenChrissSchedule(double shares, double horizon, std::size_t intervals, const AlmgrenChrissParams &params, double lambda)
    {
        requireGrid(horizon, intervals);
        const double tau = horizon / static_cast<double>(intervals);
        const double kappa = almgrenChrissKappa(params, lambda, tau);
        std::vector<double> trades(intervals);
        // Below this the hyperbolic sines are those of a straight line to
        // within rounding, and their ratio is 0 / 0 at kappa = 0.
        if (kappa * horizon < 1e-8)
        {
            for (double &n : trades)
            {
                n = shares / static_cast<double>(intervals);
            }
        }
        else
        {
            const double scale = 2.0 * std::sinh(0.5 * kappa * tau) / std::sinh(kappa * horizon) * shares;
            for (std::size_t j = 1; j <= intervals; ++j)
            {
                const double mid_time = (static_cast<double>(j) - 0.5) * tau;
                trades[j - 1] = scale * std::cosh(kappa * (horizon - mid_time));
            }
        }
        return scheduleFromTrades(trades, tau);
    }

    double expectedCost(const TradeSchedule &schedule, const AlmgrenChrissParams &params)
    {
        const double shares = schedule.holdings.empty() ? 0.0 : schedule.holdings.front();
        double absolute = 0.0;
        double squares = 0.0;
        for (const double n : schedule.trades)
        {
            absolute += std::abs(n);
            squares += n * n;
        }
        return 0.5 * params.gamma * shares * shares + params.epsilon * absolute + etaTilde(params, schedule.tau) / schedule.tau * squares;
    }

    double costVariance(const TradeSchedule &schedule, const AlmgrenChrissParams &params)
    {
        double squares = 0.0;
        for (std::size_t k = 1; k < schedule.holdings.size(); ++k)
        {
            squares += schedule.holdings[k] * schedule.holdings[k];
        }
        return params.sigma * params.sigma * schedule.tau * squares;
    }

    CostMoments almgrenChrissCost(double shares, double horizon, std::size_t intervals, const AlmgrenChrissParams &params, double lambda)
    {
        requireGrid(horizon, intervals);
        if (!(lambda > 0.0))
        {
            throw std::invalid_argument("almgrenChrissCost: the closed form needs a positive lambda");
        }
        const double tau = horizon / static_cast<double>(intervals);
        const double kappa = almgrenChrissKappa(params, lambda, tau);
        const double sinh_kt = std::sinh(kappa * horizon);
        const double sinh_ktau = std::sinh(kappa * tau);

        CostMoments moments;
        moments.expected = 0.5 * params.gamma * shares * shares + params.epsilon * std::abs(shares) +
                           etaTilde(params, tau) * shares * shares * std::tanh(0.5 * kappa * tau) * (tau * std::sinh(2.0 * kappa * horizon) + 2.0 * horizon * sinh_ktau) / (2.0 * tau * tau * sinh_kt * sinh_kt);
        moments.variance = 0.5 * params.sigma * params.sigma * shares * shares * (tau * sinh_kt * std::cosh(kappa * (horizon - tau)) - horizon * sinh_ktau) / (sinh_kt * sinh_kt * sinh_ktau);
        return moments;
    }

    std::vector<FrontierPoint> efficientFrontier(double shares, double horizon, std::size_t intervals, const AlmgrenChrissParams &params, std::span<const double> lambdas)
    {
        std::vector<FrontierPoint> frontier;
        frontier.reserve(lambdas.size());
        for (const double lambda : lambdas)
        {
            const TradeSchedule schedule = almgrenChrissSchedule(shares, horizon, intervals, params, lambda);
            frontier.push_back(FrontierPoint{lambda, expectedCost(schedule, params), costVariance(schedule, params)});
        }
        return frontier;
    }

    std::vector<double> simulateShortfall(const TradeSchedule &schedule, const AlmgrenChrissParams &params, std::size_t paths, Rng &rng)
    {
        const double tau = schedule.tau;
        const double root_tau = std::sqrt(tau);
        std::vector<double> shortfalls;
        shortfalls.reserve(paths);
        for (std::size_t path = 0; path < paths; ++path)
        {
            // Prices relative to S_0: the shortfall does not depend on it.
            double price = 0.0;
            double cash = 0.0;
            for (const double n : schedule.trades)
            {
                const double rate = n / tau;
                // Equation 2: the sale is made at the previous price less the
                // temporary impact. Equation 1: then the price moves.
                const double sign = n > 0.0 ? 1.0 : (n < 0.0 ? -1.0 : 0.0);
                cash += n * (price - params.epsilon * sign - params.eta * rate);
                price += params.sigma * root_tau * rng.normal() - tau * params.gamma * rate;
            }
            shortfalls.push_back(-cash);
        }
        return shortfalls;
    }

} // namespace quantMicrostructure
