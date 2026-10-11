#ifndef QUANTMICROSTRUCTURE_SIM_ORDER_SPLITTING_HPP
#define QUANTMICROSTRUCTURE_SIM_ORDER_SPLITTING_HPP

#include "quantMicrostructure/sim/rng.hpp"

#include <cstddef>
#include <vector>

namespace quantMicrostructure
{

    /// Trade signs with a long memory, from the order-splitting model of
    /// Lillo, Mike & Farmer (2005), section II, in its "fixed N" form:
    /// `hidden_orders` large orders are being worked at any time; each has a
    /// random sign and a size L drawn from a Pareto law of tail exponent
    /// `alpha`, P(L >= l) = l^(-alpha). At each step one of them, chosen
    /// uniformly, trades one unit; a finished order is replaced by a new one.
    ///
    /// The autocorrelation of the signs then decays as lag^(-(alpha - 1))
    /// (their equation 17): the exponent gamma of the companion, section 3,
    /// is alpha - 1. Long memory (gamma < 1) needs 1 < alpha < 2.
    ///
    /// Sizes are the integer part of a continuous Pareto draw, which has the
    /// tail of the article's distribution but not its exact values. Returns
    /// `count` signs, +1 or -1. Throws std::invalid_argument if `alpha` is not
    /// above 1 (the mean size would be infinite) or there is no hidden order.
    std::vector<double> simulateOrderSplittingSigns(double alpha, std::size_t hidden_orders, std::size_t count, Rng &rng);

} // namespace quantMicrostructure

#endif
