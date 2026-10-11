#ifndef QUANTMICROSTRUCTURE_MEASURES_TAPE_HPP
#define QUANTMICROSTRUCTURE_MEASURES_TAPE_HPP

#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace quantMicrostructure
{

    /// What kind of stream the events are (core/event.hpp).
    enum class StreamKind : std::uint8_t
    {
        /// Add, Cancel, Trade: applied to an order-by-order book.
        OrderByOrder,
        /// Level, Trade: applied to a price-level book.
        Aggregated,
        /// Trade events alone, with no book behind them (the Roll model).
        TradesOnly
    };

    /// The best prices and the quantities resting there (L1), from `ts` until
    /// the next quote. A side with nothing resting has a quantity of zero, and
    /// its price means nothing.
    struct Quote
    {
        Timestamp ts{};
        Price bid{};
        Price ask{};
        Quantity bid_qty{};
        Quantity ask_qty{};

        bool twoSided() const noexcept { return bid_qty > 0 && ask_qty > 0; }
        /// Mid price and spread, in ticks. Defined only for a two-sided quote.
        double mid() const noexcept { return 0.5 * static_cast<double>(bid + ask); }
        double spread() const noexcept { return static_cast<double>(ask - bid); }

        friend bool operator==(const Quote &, const Quote &) = default;
    };

    /// One aggressive order, as the market saw it. Trade events with the same
    /// timestamp and the same aggressor side are one trade: an order that
    /// walks through several resting orders is counted once, as in Bouchaud,
    /// Gefen, Potters & Wyart (2004), section 2.1.
    struct Trade
    {
        static constexpr std::size_t NoQuote = std::numeric_limits<std::size_t>::max();

        Timestamp ts{};
        /// Side of the aggressor: the trade sign is +1 for Buy, -1 for Sell.
        Side side{};
        Quantity qty{};
        /// Volume-weighted average price, in ticks.
        double price{};
        /// Index, in Tape::quotes, of the quote in force just before the
        /// trade; NoQuote if there was none.
        std::size_t quote{NoQuote};

        double sign() const noexcept { return side == Side::Buy ? 1.0 : -1.0; }

        friend bool operator==(const Trade &, const Trade &) = default;
    };

    /// Quotes and trades in time order: what the measures read.
    struct Tape
    {
        std::vector<Quote> quotes;
        std::vector<Trade> trades;
        /// Timestamp of the last event of the stream: nothing is known later.
        Timestamp end{};
    };

    /// Replays a stream through a book and records each change of the best
    /// prices or of their quantities, and each trade. Throws
    /// std::invalid_argument if an event does not belong to that kind of
    /// stream.
    Tape buildTape(std::span<const Event> events, StreamKind kind);

} // namespace quantMicrostructure

#endif
