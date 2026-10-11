#include "quantMicrostructure/matching/matching_engine.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace quantMicrostructure
{

    SubmitResult MatchingEngine::submitLimit(OrderId id, Side side, Price price, Quantity qty, Timestamp ts, std::vector<Event> &out)
    {
        if (qty <= 0)
        {
            throw std::invalid_argument("MatchingEngine::submitLimit: quantity must be positive");
        }
        if (book_.find(id))
        {
            throw std::invalid_argument("MatchingEngine::submitLimit: order id is already resting");
        }

        SubmitResult result;
        result.filled = match(side, price, qty, ts, out);
        result.resting = qty - result.filled;
        if (result.resting > 0)
        {
            book_.add(id, side, price, result.resting);
            emit(EventType::Add, id, side, price, result.resting, ts, out);
        }
        return result;
    }

    SubmitResult MatchingEngine::submitMarket(Side side, Quantity qty, Timestamp ts, std::vector<Event> &out)
    {
        if (qty <= 0)
        {
            throw std::invalid_argument("MatchingEngine::submitMarket: quantity must be positive");
        }
        // A market order is a limit order at the worst possible price.
        const Price limit = side == Side::Buy ? std::numeric_limits<Price>::max() : std::numeric_limits<Price>::min();
        SubmitResult result;
        result.filled = match(side, limit, qty, ts, out);
        return result;
    }

    Quantity MatchingEngine::cancel(OrderId id, Timestamp ts, std::vector<Event> &out)
    {
        const auto order = book_.find(id);
        if (!order)
        {
            return 0;
        }
        book_.reduce(id, order->qty);
        emit(EventType::Cancel, id, order->side, order->price, order->qty, ts, out);
        return order->qty;
    }

    Quantity MatchingEngine::match(Side side, Price limit, Quantity qty, Timestamp ts, std::vector<Event> &out)
    {
        const Side resting_side = opposite(side);
        Quantity remaining = qty;
        while (remaining > 0)
        {
            const auto resting = book_.front(resting_side);
            if (!resting)
            {
                break;
            }
            const bool crosses = side == Side::Buy ? resting->price <= limit : resting->price >= limit;
            if (!crosses)
            {
                break;
            }
            const Quantity traded = std::min(remaining, resting->qty);
            book_.reduce(resting->id, traded);
            emit(EventType::Trade, resting->id, side, resting->price, traded, ts, out);
            remaining -= traded;
        }
        return qty - remaining;
    }

    void MatchingEngine::emit(EventType type, OrderId id, Side side, Price price, Quantity qty, Timestamp ts, std::vector<Event> &out)
    {
        Event event;
        event.ts = ts;
        event.ts_recv = ts;
        event.seq = next_seq_++;
        event.order_id = id;
        event.price = price;
        event.qty = qty;
        event.type = type;
        event.side = side;
        out.push_back(event);
    }

} // namespace quantMicrostructure
