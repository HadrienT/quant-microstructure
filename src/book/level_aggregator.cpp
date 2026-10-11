#include "quantMicrostructure/book/level_aggregator.hpp"

#include <stdexcept>

namespace quantMicrostructure
{

    void LevelAggregator::apply(const Event &event, std::vector<Event> &out)
    {
        switch (event.type)
        {
            case EventType::Add:
                book_.apply(event);
                emitLevel(event, event.side, event.price, out);
                return;
            case EventType::Cancel:
            case EventType::Trade:
            {
                // The level is that of the resting order, read before the event
                // possibly removes it.
                const auto resting = book_.find(event.order_id);
                if (!resting)
                {
                    throw std::invalid_argument("LevelAggregator::apply: order id is not resting");
                }
                if (event.type == EventType::Trade)
                {
                    Event trade = event;
                    trade.seq = next_seq_++;
                    out.push_back(trade);
                }
                book_.apply(event);
                emitLevel(event, resting->side, resting->price, out);
                return;
            }
            case EventType::Clear:
            {
                book_.apply(event);
                Event clear = event;
                clear.seq = next_seq_++;
                out.push_back(clear);
                return;
            }
            case EventType::Level:
                break;
        }
        throw std::invalid_argument("LevelAggregator::apply: not an event of an order-by-order stream");
    }

    void LevelAggregator::emitLevel(const Event &source, Side side, Price price, std::vector<Event> &out)
    {
        Event level;
        level.ts = source.ts;
        level.ts_recv = source.ts_recv;
        level.seq = next_seq_++;
        level.price = price;
        level.qty = book_.levelQuantity(side, price);
        level.type = EventType::Level;
        level.side = side;
        out.push_back(level);
    }

} // namespace quantMicrostructure
