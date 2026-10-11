#ifndef QUANTMICROSTRUCTURE_MATCHING_MATCHING_ENGINE_HPP
#define QUANTMICROSTRUCTURE_MATCHING_MATCHING_ENGINE_HPP

#include "quantMicrostructure/book/order_book.hpp"
#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/core/types.hpp"

#include <vector>

namespace quantMicrostructure
{

    /// What became of an incoming order. For a market order, the part that
    /// was neither filled nor left resting found no counterparty and is
    /// dropped.
    struct SubmitResult
    {
        Quantity filled{};
        Quantity resting{};
    };

    /// A continuous double auction with price-time priority
    /// (docs/companion.md, sections 2.1 and 2.2): the best price trades first
    /// and, at the same price, the oldest order. A trade is made at the price
    /// of the resting order.
    ///
    /// Each call appends to `out` the order-by-order events it causes, numbered
    /// consecutively from 1 and stamped with `ts`. An OrderBook that applies
    /// them is the book of the engine.
    class MatchingEngine
    {
      public:
        /// A limit order: trades against the other side as far as `price`
        /// allows, then rests for what is left. Throws std::invalid_argument,
        /// before anything happens, if `qty` is not positive or `id` is
        /// already resting.
        SubmitResult submitLimit(OrderId id, Side side, Price price, Quantity qty, Timestamp ts, std::vector<Event> &out);

        /// A market order: trades against the other side at any price, and
        /// never rests. Throws std::invalid_argument if `qty` is not positive.
        SubmitResult submitMarket(Side side, Quantity qty, Timestamp ts, std::vector<Event> &out);

        /// Cancels what is left of a resting order and returns that quantity;
        /// zero, and no event, if `id` is not resting (it may have been filled
        /// in the meantime, which is not an error).
        Quantity cancel(OrderId id, Timestamp ts, std::vector<Event> &out);

        const OrderBook &book() const noexcept { return book_; }

      private:
        Quantity match(Side side, Price limit, Quantity qty, Timestamp ts, std::vector<Event> &out);
        void emit(EventType type, OrderId id, Side side, Price price, Quantity qty, Timestamp ts, std::vector<Event> &out);

        OrderBook book_;
        SequenceNumber next_seq_{1};
    };

} // namespace quantMicrostructure

#endif
