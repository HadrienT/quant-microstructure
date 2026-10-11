#ifndef QUANTMICROSTRUCTURE_MEASURES_LIQUIDITY_HPP
#define QUANTMICROSTRUCTURE_MEASURES_LIQUIDITY_HPP

#include "quantMicrostructure/book/book_level.hpp"
#include "quantMicrostructure/core/types.hpp"
#include "quantMicrostructure/measures/tape.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace quantMicrostructure
{

    /// Volume imbalance at the best prices (docs/companion.md, section 3):
    /// I = V_bid / (V_bid + V_ask), between 0 and 1. For a two-sided quote.
    double imbalance(const Quote &quote);

    /// Mid price weighted by the opposite volumes (section 3):
    /// (ask V_bid + bid V_ask) / (V_bid + V_ask). It leans towards the side
    /// with less volume, the one more likely to be exhausted first. For a
    /// two-sided quote.
    double weightedMid(const Quote &quote);

    /// Total quantity within `ticks` ticks of the best price of one side.
    /// `levels` is that side of a book, best price first, as Book::depth
    /// returns it; zero ticks is the best level alone.
    Quantity depthWithin(std::span<const BookLevel> levels, Price ticks);

    /// What the mid price did next, for the quotes whose imbalance fell in
    /// [lower, upper).
    struct ImbalanceBin
    {
        double lower{};
        double upper{};
        std::size_t up{};
        std::size_t down{};

        /// Share of next moves that were up; 0.5 if the bin is empty.
        double probabilityUp() const noexcept;
    };

    /// Does the imbalance say where the price goes next (section 3)? Every
    /// two-sided quote is one observation: its imbalance, and the direction of
    /// the next change of the mid price. Quotes after which the mid never
    /// changes again are left out. The interval [0, 1] is cut into `bins` equal
    /// bins; an imbalance of exactly 1 goes into the last one.
    ///
    /// Observations are in event time (one per quote update), and consecutive
    /// ones are strongly dependent: the counts are not independent draws.
    std::vector<ImbalanceBin> imbalanceNextMove(const Tape &tape, std::size_t bins);

} // namespace quantMicrostructure

#endif
