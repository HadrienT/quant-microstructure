#ifndef QUANTMICROSTRUCTURE_IMPACT_ROUND_TRIP_HPP
#define QUANTMICROSTRUCTURE_IMPACT_ROUND_TRIP_HPP

#include <span>

namespace quantMicrostructure
{

    /// Cash spent by a sequence of trades in a transient impact model, in
    /// excess of the price at which it started (docs/companion.md, section
    /// 5.5). Trade n, of signed size q_n, is made at the initial price plus
    ///
    ///     P_n = sum over k <= n of G(n - k) * sign(q_k) * |q_k|^exponent
    ///
    /// and the cost is the sum of q_n P_n. `kernel` holds G(0), G(1), ...; the
    /// impact of a trade has vanished beyond its last entry.
    ///
    /// For a round trip (sizes that add up to zero) the cost is what the
    /// strategy loses. A model in which some round trip has a negative
    /// expected cost can be manipulated for a profit: that is the dynamic
    /// arbitrage of Gatheral (2010), whose abstract states that an exponential
    /// decay is compatible only with a linear impact (exponent 1). The article
    /// itself was not re-read.
    ///
    /// With a linear impact the cost is the quadratic form of the matrix
    /// G(|i - j|), half of it plus half of G(0) times the sum of squares, so
    /// that no round trip is profitable when that matrix is positive
    /// semi-definite, which holds for a non-negative, non-increasing and
    /// convex kernel.
    double impactTradingCost(std::span<const double> trades, std::span<const double> kernel, double exponent);

} // namespace quantMicrostructure

#endif
