#ifndef QUANTMICROSTRUCTURE_BOOK_LEVEL_BOOK_HPP
#define QUANTMICROSTRUCTURE_BOOK_LEVEL_BOOK_HPP

#include "quantMicrostructure/book/book_level.hpp"
#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/core/types.hpp"

#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <vector>

namespace quantMicrostructure
{

    /// A book aggregated by price level (L2): a quantity per price, and no
    /// knowledge of the orders that make it up. Queue position cannot be read
    /// from it, only assumed (docs/companion.md, sections 2.4 and 10).
    class LevelBook
    {
      public:
        static constexpr std::size_t AllLevels = std::numeric_limits<std::size_t>::max();

        /// Sets the total quantity at a price; zero removes the level. Throws
        /// std::invalid_argument if `qty` is negative.
        void set(Side side, Price price, Quantity qty);

        void clear();

        /// Applies one event of an aggregated stream. A Trade changes nothing
        /// (the Level event that follows it does). Throws std::invalid_argument
        /// on an Add or a Cancel, which belong to an order-by-order stream.
        void apply(const Event &event);

        std::optional<Price> best(Side side) const;
        std::optional<Price> bestBid() const { return best(Side::Buy); }
        std::optional<Price> bestAsk() const { return best(Side::Sell); }

        /// Total quantity at this price; zero if there is no such level.
        Quantity levelQuantity(Side side, Price price) const;

        /// The first `max_levels` levels of a side, best price first.
        std::vector<BookLevel> depth(Side side, std::size_t max_levels = AllLevels) const;

      private:
        // Both sides are sorted by increasing price, as in OrderBook.
        using LevelMap = std::map<Price, Quantity>;

        const LevelMap &levels(Side side) const noexcept { return side == Side::Buy ? bids_ : asks_; }
        LevelMap &levels(Side side) noexcept { return side == Side::Buy ? bids_ : asks_; }

        LevelMap bids_;
        LevelMap asks_;
    };

} // namespace quantMicrostructure

#endif
