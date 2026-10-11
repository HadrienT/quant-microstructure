#include "quantMicrostructure/measures/tape.hpp"

#include "quantMicrostructure/book/level_book.hpp"
#include "quantMicrostructure/book/order_book.hpp"

#include <stdexcept>

namespace quantMicrostructure
{

    namespace
    {

        template <typename Book>
        Quote topOf(const Book &book, Timestamp ts)
        {
            Quote quote;
            quote.ts = ts;
            if (const auto bid = book.bestBid())
            {
                quote.bid = *bid;
                quote.bid_qty = book.levelQuantity(Side::Buy, *bid);
            }
            if (const auto ask = book.bestAsk())
            {
                quote.ask = *ask;
                quote.ask_qty = book.levelQuantity(Side::Sell, *ask);
            }
            return quote;
        }

        bool sameTop(const Quote &a, const Quote &b)
        {
            return a.bid == b.bid && a.ask == b.ask && a.bid_qty == b.bid_qty && a.ask_qty == b.ask_qty;
        }

        void recordTrade(const Event &event, Tape &tape)
        {
            if (!tape.trades.empty())
            {
                Trade &last = tape.trades.back();
                if (last.ts == event.ts && last.side == event.side)
                {
                    const double total = static_cast<double>(last.qty + event.qty);
                    last.price = (last.price * static_cast<double>(last.qty) + static_cast<double>(event.price) * static_cast<double>(event.qty)) / total;
                    last.qty += event.qty;
                    return;
                }
            }
            Trade trade;
            trade.ts = event.ts;
            trade.side = event.side;
            trade.qty = event.qty;
            trade.price = static_cast<double>(event.price);
            trade.quote = tape.quotes.empty() ? Trade::NoQuote : tape.quotes.size() - 1;
            tape.trades.push_back(trade);
        }

        template <typename Book>
        Tape replay(std::span<const Event> events)
        {
            Tape tape;
            Book book;
            for (const Event &event : events)
            {
                // The trade is recorded against the quote in force before it.
                if (event.type == EventType::Trade)
                {
                    recordTrade(event, tape);
                }
                book.apply(event);
                const Quote top = topOf(book, event.ts);
                const bool first = tape.quotes.empty();
                if (first ? (top.bid_qty > 0 || top.ask_qty > 0) : !sameTop(top, tape.quotes.back()))
                {
                    tape.quotes.push_back(top);
                }
                tape.end = event.ts;
            }
            return tape;
        }

    } // namespace

    Tape buildTape(std::span<const Event> events, StreamKind kind)
    {
        switch (kind)
        {
            case StreamKind::OrderByOrder:
                return replay<OrderBook>(events);
            case StreamKind::Aggregated:
                return replay<LevelBook>(events);
            case StreamKind::TradesOnly:
                break;
        }
        Tape tape;
        for (const Event &event : events)
        {
            if (event.type != EventType::Trade)
            {
                throw std::invalid_argument("buildTape: a trades-only stream holds Trade events only");
            }
            recordTrade(event, tape);
            tape.end = event.ts;
        }
        return tape;
    }

} // namespace quantMicrostructure
