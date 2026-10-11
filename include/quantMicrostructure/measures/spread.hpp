#ifndef QUANTMICROSTRUCTURE_MEASURES_SPREAD_HPP
#define QUANTMICROSTRUCTURE_MEASURES_SPREAD_HPP

#include "quantMicrostructure/core/types.hpp"
#include "quantMicrostructure/measures/tape.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace quantMicrostructure
{

    /// Roll's estimate of the spread from trade prices alone
    /// (docs/companion.md, section 4.1): s = 2 sqrt(-Cov(dp_t, dp_{t-1})),
    /// derived in sim/toy_models.hpp. Not re-read in Roll (1984).
    ///
    /// Empty when the covariance is not negative, which happens on real data:
    /// the model assumes independent trade signs, and real ones are
    /// persistent. Needs at least three prices.
    std::optional<double> rollSpread(std::span<const double> trade_prices);

    /// Quoted spread in ticks, averaged over the time during which the book
    /// was two-sided. Empty if it never was.
    std::optional<double> timeWeightedSpread(const Tape &tape);

    /// The three half-spreads of section 4.5 of the companion, in ticks,
    /// averaged over trades (each trade counts once, whatever its size). With
    /// e the trade sign, p the trade price, m the mid just before the trade
    /// and m' the mid `horizon` later:
    ///
    ///     effective = e (p - m)      what the aggressor pays
    ///     impact    = e (m' - m)     how far the price moved against the maker
    ///     realized  = e (p - m')     what the maker keeps
    ///
    /// so that effective = realized + impact, trade by trade.
    struct SpreadDecomposition
    {
        double effective{};
        double realized{};
        double impact{};
        /// Trades averaged: those with a two-sided quote before them and
        /// `horizon` later, the latter not beyond the end of the tape.
        std::size_t trades{};
    };

    SpreadDecomposition spreadDecomposition(const Tape &tape, Timestamp horizon);

} // namespace quantMicrostructure

#endif
