#ifndef QUANTMICROSTRUCTURE_EXECUTION_ALMGREN_CHRISS_HPP
#define QUANTMICROSTRUCTURE_EXECUTION_ALMGREN_CHRISS_HPP

#include "quantMicrostructure/sim/rng.hpp"

#include <cstddef>
#include <span>
#include <vector>

// Optimal execution after Almgren & Chriss (2000) (docs/companion.md, section
// 8.2), in the discrete-time form of the article, whose notation is kept: a
// program to SELL X units over a time T cut into N intervals of length tau.
// A buy program is the same with the sign of the price moves reversed.
//
// Regime and assumptions: an arithmetic random walk without drift, a
// permanent impact linear in the trading rate, a temporary impact linear in
// the trading rate plus a fixed cost per unit. The optimal trajectory is
// fixed in advance: it does not react to the price, which is optimal in this
// mean-variance setting and not in general.

namespace quantMicrostructure
{

    struct AlmgrenChrissParams
    {
        /// Volatility of the price, per square root of unit time.
        double sigma{};
        /// Permanent impact per unit traded: g(v) = gamma v (equation 6).
        double gamma{};
        /// Temporary impact per unit of trading rate (equation 7).
        double eta{};
        /// Fixed cost per unit traded, half the spread plus fees (equation 7).
        double epsilon{};
    };

    /// A trading trajectory: holdings[k] is x_k, what is left to sell at time
    /// k tau (N + 1 values, from X to 0); trades[k] is n_{k+1} = x_k - x_{k+1},
    /// what is sold during interval k + 1 (N values).
    struct TradeSchedule
    {
        std::vector<double> holdings;
        std::vector<double> trades;
        /// Length of an interval.
        double tau{};
    };

    /// Builds a schedule from what is traded in each interval.
    TradeSchedule scheduleFromTrades(std::span<const double> trades, double tau);

    /// The rate kappa at which the optimal trajectory decays, solution of
    /// (2 / tau^2)(cosh(kappa tau) - 1) = lambda sigma^2 / eta_tilde, with
    /// eta_tilde = eta - gamma tau / 2 (equation 16 and below). It tends to
    /// sqrt(lambda sigma^2 / eta) as tau goes to zero (equation 19). 1 / kappa
    /// is the time scale of the trade. Throws std::invalid_argument if
    /// eta_tilde is not positive or lambda is negative.
    double almgrenChrissKappa(const AlmgrenChrissParams &params, double lambda, double tau);

    /// The trajectory that minimises E + lambda V (equations 17 and 18):
    /// x_j = X sinh(kappa (T - t_j)) / sinh(kappa T). With lambda = 0 it is
    /// the straight line of equation 9, which minimises the expected cost.
    TradeSchedule almgrenChrissSchedule(double shares, double horizon, std::size_t intervals, const AlmgrenChrissParams &params, double lambda);

    /// Expected cost of a trajectory (equation 8):
    /// gamma X^2 / 2 + epsilon sum |n_k| + (eta_tilde / tau) sum n_k^2.
    double expectedCost(const TradeSchedule &schedule, const AlmgrenChrissParams &params);

    /// Variance of the cost of a trajectory (equation 5): sigma^2 tau sum x_k^2
    /// over k = 1..N.
    double costVariance(const TradeSchedule &schedule, const AlmgrenChrissParams &params);

    struct CostMoments
    {
        double expected{};
        double variance{};
    };

    /// Expected cost and variance of the optimal trajectory in closed form
    /// (equation 20). Needs lambda > 0: at lambda = 0 use equations 10 and 11
    /// through expectedCost and costVariance.
    CostMoments almgrenChrissCost(double shares, double horizon, std::size_t intervals, const AlmgrenChrissParams &params, double lambda);

    struct FrontierPoint
    {
        double lambda{};
        double expected{};
        double variance{};
    };

    /// The efficient frontier: for each risk aversion, the expected cost and
    /// the variance of the optimal trajectory.
    std::vector<FrontierPoint> efficientFrontier(double shares, double horizon, std::size_t intervals, const AlmgrenChrissParams &params, std::span<const double> lambdas);

    /// Simulates the price of equation 1 and the sale prices of equation 2
    /// along a trajectory, `paths` times, and returns each implementation
    /// shortfall: X S_0 minus the cash received (equation 3).
    std::vector<double> simulateShortfall(const TradeSchedule &schedule, const AlmgrenChrissParams &params, std::size_t paths, Rng &rng);

} // namespace quantMicrostructure

#endif
