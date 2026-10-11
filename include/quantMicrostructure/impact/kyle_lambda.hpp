#ifndef QUANTMICROSTRUCTURE_IMPACT_KYLE_LAMBDA_HPP
#define QUANTMICROSTRUCTURE_IMPACT_KYLE_LAMBDA_HPP

#include "quantMicrostructure/core/types.hpp"
#include "quantMicrostructure/measures/tape.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace quantMicrostructure
{

    /// Ordinary least squares of y on x, with an intercept.
    struct Regression
    {
        double slope{};
        double intercept{};
        double r_squared{};
        /// Standard error of the slope, assuming independent errors of equal
        /// variance. Price changes of consecutive intervals are neither: it
        /// understates the uncertainty on market data.
        double slope_se{};
        std::size_t observations{};
    };

    /// Throws std::invalid_argument unless x and y have the same length, at
    /// least three points, and x is not constant.
    Regression linearRegression(std::span<const double> x, std::span<const double> y);

    /// Signed volume and change of the mid price over consecutive intervals.
    struct FlowBars
    {
        /// Lots bought by aggressors minus lots sold, per interval.
        std::vector<double> flow;
        /// Mid at the end of the interval minus mid at its start, in ticks.
        std::vector<double> price_change;
    };

    /// Cuts the tape into intervals of `interval` nanoseconds from its first
    /// two-sided quote. An interval is kept if the book had a mid at both of
    /// its ends (the last one known, as in sampleMid).
    FlowBars signedFlowBars(const Tape &tape, Timestamp interval);

    /// Kyle's lambda as it is estimated on data (docs/companion.md, sections
    /// 4.3 and 7.3): the slope of the regression of the mid price change on
    /// the signed volume of the same interval, in ticks per lot. It assumes a
    /// linear impact, which the square-root law contradicts for large orders,
    /// and its value depends on the interval.
    Regression kyleLambda(const Tape &tape, Timestamp interval);

} // namespace quantMicrostructure

#endif
