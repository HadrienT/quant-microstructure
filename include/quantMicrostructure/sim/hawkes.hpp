#ifndef QUANTMICROSTRUCTURE_SIM_HAWKES_HPP
#define QUANTMICROSTRUCTURE_SIM_HAWKES_HPP

#include "quantMicrostructure/sim/rng.hpp"

#include <span>
#include <vector>

namespace quantMicrostructure
{

    /// A univariate Hawkes process with an exponential kernel
    /// (docs/companion.md, section 6.3):
    ///
    ///     lambda(t) = mu + sum over t_i < t of alpha * exp(-beta * (t - t_i))
    ///
    /// Times are in seconds, as floating-point numbers: this is a model of
    /// arrival times, not yet an event stream. The multivariate case (buys and
    /// sells exciting each other) is not written.
    struct HawkesParams
    {
        /// Baseline intensity, events per second.
        double mu{};
        /// Jump of the intensity at each event.
        double alpha{};
        /// Rate at which the excitation decays.
        double beta{};
    };

    /// Mean number of events triggered by one event: the integral of the
    /// kernel, alpha / beta. The process is stationary when it is below 1.
    double hawkesBranchingRatio(const HawkesParams &params);

    /// Mean intensity of the stationary process, mu / (1 - n). Bacry,
    /// Mastromatteo & Muzy (2015), equations (20) and (21), in one dimension.
    /// Throws std::invalid_argument if the process is not stationary.
    double hawkesMeanIntensity(const HawkesParams &params);

    /// Event times on [0, horizon), by Ogata's modified thinning: between two
    /// events the intensity only decays, so its value just after the current
    /// time bounds it until the next event. Laub, Taimre & Pollett (2015),
    /// algorithm 2. The process starts with no past: the first events are
    /// below the stationary intensity.
    std::vector<double> simulateHawkes(const HawkesParams &params, double horizon, Rng &rng);

    /// Log-likelihood of event times observed on [0, horizon], computed in
    /// linear time by the recursion of Laub, Taimre & Pollett (2015),
    /// equations (20) and (21), with the compensator of equation (18) taken to
    /// `horizon`. `times` must be increasing and within [0, horizon].
    double hawkesLogLikelihood(const HawkesParams &params, std::span<const double> times, double horizon);

    /// The durations between consecutive events in the time of the
    /// compensator: Lambda(t_i) - Lambda(t_{i-1}), with Lambda(t_0) = 0. If
    /// the times come from this process they are independent exponentials of
    /// mean 1 (random time change theorem; Laub, Taimre & Pollett 2015,
    /// theorem 4). Testing that is how a fit is validated.
    std::vector<double> hawkesResiduals(const HawkesParams &params, std::span<const double> times);

    struct HawkesFit
    {
        HawkesParams params;
        double log_likelihood{};
        int iterations{};
        /// False if the search stopped on its iteration limit.
        bool converged{false};
    };

    /// Maximum likelihood estimate, by a Nelder-Mead search on the logarithms
    /// of the three parameters (which keeps them positive). Stationarity is not
    /// imposed: check the branching ratio of the result. Throws
    /// std::invalid_argument with fewer than two events.
    HawkesFit fitHawkes(std::span<const double> times, double horizon);

} // namespace quantMicrostructure

#endif
