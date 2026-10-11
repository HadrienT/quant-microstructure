#ifndef QUANTMICROSTRUCTURE_MEASURES_VOLATILITY_HPP
#define QUANTMICROSTRUCTURE_MEASURES_VOLATILITY_HPP

#include "quantMicrostructure/core/types.hpp"
#include "quantMicrostructure/measures/tape.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace quantMicrostructure
{

    // Realised variance in the presence of microstructure noise
    // (docs/companion.md, section 7.1). Prices are whatever the caller
    // sampled, in ticks or in logarithms: the variance is in the square of
    // that unit, over the whole sample, not per unit of time.
    //
    // Notation of Zhang, Mykland & Ait-Sahalia (2005): the sample has n + 1
    // prices, hence n increments.

    /// Sum of squared increments of every `step`-th price, starting at
    /// `offset`: [Y, Y] on a sparse grid; `step` 1 is [Y, Y]^(all).
    double realizedVariance(std::span<const double> prices, std::size_t step = 1, std::size_t offset = 0);

    struct SignaturePoint
    {
        std::size_t step{};
        /// Number of increments at that step.
        std::size_t increments{};
        double variance{};
    };

    /// The signature plot: realised variance against the sampling step. Flat
    /// for a price without noise; with noise of variance omega^2 added to each
    /// observation it rises towards the small steps by 2 omega^2 per increment
    /// (Zhang, Mykland & Ait-Sahalia 2005, equation 5).
    std::vector<SignaturePoint> signaturePlot(std::span<const double> prices, std::span<const std::size_t> steps);

    /// Variance of the noise read off the fastest scale: [Y, Y]^(all) / (2 n)
    /// (equation 5). It overstates the noise by the true variance of one
    /// increment, which is negligible only when the sampling is fast.
    double noiseVariance(std::span<const double> prices);

    /// The two-scale estimator of the variance of the price without its noise
    /// (equation 55): the average of the realised variances on the `subgrids`
    /// grids that take every `subgrids`-th price, minus n_bar / n times the
    /// realised variance on all prices, with n_bar = (n - K + 1) / K
    /// (equation 34). With `adjust`, divided by 1 - n_bar / n, the
    /// small-sample adjustment of equation 64.
    ///
    /// Assumes noise independent from one observation to the next and from the
    /// price. Throws std::invalid_argument unless 2 <= subgrids <= n.
    double twoScaleRealizedVariance(std::span<const double> prices, std::size_t subgrids, bool adjust = true);

    /// The mid price every `interval` nanoseconds, from the first two-sided
    /// quote to the end of the tape, each time the last one known (while the
    /// book is one-sided, the last two-sided one).
    std::vector<double> sampleMid(const Tape &tape, Timestamp interval);

} // namespace quantMicrostructure

#endif
