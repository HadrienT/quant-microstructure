#ifndef QUANTMICROSTRUCTURE_CORE_EVENT_HPP
#define QUANTMICROSTRUCTURE_CORE_EVENT_HPP

#include "quantMicrostructure/core/types.hpp"

#include <cstdint>

namespace quantMicrostructure
{

    /// The five things that can happen to a book. A stream is either
    /// order-by-order (L3: Add, Cancel, Trade) or aggregated by price level
    /// (L2: Level, Trade); Clear belongs to both.
    enum class EventType : std::uint8_t
    {
        /// An order starts resting: `order_id`, `side`, `price`, `qty`.
        Add = 0,
        /// A resting order loses `qty` without trading (all of it or part of
        /// it; it keeps its place in the queue). `side` and `price` are those
        /// of the order.
        Cancel = 1,
        /// The resting order `order_id` trades `qty` at `price`, which is its
        /// own price. `side` is the side of the AGGRESSOR, so that a buy is the
        /// +1 of the trade sign convention (docs/companion.md, section 0); the
        /// resting order is on the opposite side. In an order-by-order stream
        /// it reduces the resting order. In an aggregated stream it changes
        /// nothing: the Level event that follows carries the new quantity.
        Trade = 2,
        /// The total quantity resting at (`side`, `price`) is now `qty`; zero
        /// removes the level. `order_id` is unused.
        Level = 3,
        /// The book is emptied. What follows rebuilds it (a snapshot, after a
        /// gap in the feed for instance).
        Clear = 4
    };

    /// The single event format of the repository (blueprint/decisions.md,
    /// ADR-007): a market recording, the output of a simulator and the output
    /// of the matching engine are all streams of these.
    struct Event
    {
        /// When it happened, by the clock of the venue (or of the simulation).
        Timestamp ts{};
        /// When it was received by the capture. Equal to `ts` in a stream that
        /// was not captured from a network.
        Timestamp ts_recv{};
        /// Position in the stream. Consecutive events differ by one; anything
        /// else is a gap (feed/sequence_tracker.hpp).
        SequenceNumber seq{};
        OrderId order_id{};
        Price price{};
        Quantity qty{};
        EventType type{};
        Side side{};

        friend bool operator==(const Event &, const Event &) = default;
    };

} // namespace quantMicrostructure

#endif
