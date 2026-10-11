#ifndef QUANTMICROSTRUCTURE_BOOK_LEVEL_AGGREGATOR_HPP
#define QUANTMICROSTRUCTURE_BOOK_LEVEL_AGGREGATOR_HPP

#include "quantMicrostructure/book/order_book.hpp"
#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/core/types.hpp"

#include <vector>

namespace quantMicrostructure
{

    /// Turns an order-by-order stream into the aggregated stream of the same
    /// market: what a venue publishes on its L2 channel while its L3 channel
    /// publishes the input.
    class LevelAggregator
    {
      public:
        /// Applies one order-by-order event and appends to `out` what it
        /// becomes: a Level for an Add or a Cancel, the Trade followed by a
        /// Level for a Trade, a Clear for a Clear. Events are renumbered from 1.
        void apply(const Event &event, std::vector<Event> &out);

        const OrderBook &book() const noexcept { return book_; }

      private:
        void emitLevel(const Event &source, Side side, Price price, std::vector<Event> &out);

        OrderBook book_;
        SequenceNumber next_seq_{1};
    };

} // namespace quantMicrostructure

#endif
