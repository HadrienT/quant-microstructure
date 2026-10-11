#ifndef QUANTMICROSTRUCTURE_IMPACT_METAORDER_HPP
#define QUANTMICROSTRUCTURE_IMPACT_METAORDER_HPP

#include "quantMicrostructure/sim/zero_intelligence.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace quantMicrostructure
{

    // Impact of a metaorder (docs/companion.md, sections 5.1 and 5.4). No
    // public data marks the start and the end of large orders: the only place
    // where it can be measured here is a simulator, into which the metaorders
    // are injected.

    struct MetaorderExperiment
    {
        /// The market the metaorders are sent into.
        ZeroIntelligenceParams market;
        /// Number of child orders of each metaorder size to measure; a child
        /// is a market order of `market.order_size` lots.
        std::vector<std::size_t> children;
        /// Seconds between two child orders.
        double child_interval{1.0};
        /// Seconds the market runs before the first child, for the book to
        /// reach its stationary shape.
        double warmup{100.0};
        /// Independent markets per size.
        std::size_t repetitions{200};
        std::uint64_t seed{1};
    };

    struct MetaorderImpactPoint
    {
        std::size_t children{};
        /// Lots bought: children * market.order_size.
        Quantity size{};
        /// Mean rise of the mid price, in ticks, from just before the first
        /// child to just after the last, in excess of the same market without
        /// the metaorder.
        double impact{};
        double standard_error{};
        /// Repetitions in which the book was two-sided at both ends.
        std::size_t samples{};
    };

    /// Buys each size by market orders at a constant pace, in `repetitions`
    /// independent markets, and measures how far the mid price moved. Each
    /// market is run twice from the same seed, with and without the metaorder,
    /// and the difference is averaged: the two runs share their history up to
    /// the first child, which removes most of the noise.
    ///
    /// The impact is "peak" impact: nothing is said of what remains later.
    /// The market is the zero-intelligence model, whose flow has no memory and
    /// does not react to the metaorder otherwise than through the book.
    std::vector<MetaorderImpactPoint> measureMetaorderImpact(const MetaorderExperiment &experiment);

    /// y = prefactor * x^exponent, fitted by least squares on logarithms.
    struct PowerLawFit
    {
        double prefactor{};
        double exponent{};
    };

    /// Throws std::invalid_argument unless x and y are positive, of the same
    /// length, with at least two different x.
    PowerLawFit fitPowerLaw(std::span<const double> x, std::span<const double> y);

} // namespace quantMicrostructure

#endif
