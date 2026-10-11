#include "quantMicrostructure/book/order_book.hpp"

#include <iterator>
#include <stdexcept>

namespace quantMicrostructure
{

    void OrderBook::add(OrderId id, Side side, Price price, Quantity qty)
    {
        if (qty <= 0)
        {
            throw std::invalid_argument("OrderBook::add: quantity must be positive");
        }
        const auto [slot, inserted] = index_.try_emplace(id, Nil);
        if (!inserted)
        {
            throw std::invalid_argument("OrderBook::add: order id is already resting");
        }

        const LevelMap::iterator level = levels(side).try_emplace(price).first;

        std::uint32_t n = 0;
        if (free_.empty())
        {
            n = static_cast<std::uint32_t>(nodes_.size());
            nodes_.emplace_back();
        }
        else
        {
            n = free_.back();
            free_.pop_back();
        }
        slot->second = n;

        Level &queue = level->second;
        nodes_[n] = Node{id, qty, level, queue.tail, Nil, side};
        if (queue.tail == Nil)
        {
            queue.head = n;
        }
        else
        {
            nodes_[queue.tail].next = n;
        }
        queue.tail = n;
        queue.total += qty;
    }

    Quantity OrderBook::reduce(OrderId id, Quantity qty)
    {
        const auto slot = index_.find(id);
        if (slot == index_.end())
        {
            throw std::invalid_argument("OrderBook::reduce: order id is not resting");
        }
        const std::uint32_t n = slot->second;
        Node &node = nodes_[n];
        if (qty <= 0 || qty > node.qty)
        {
            throw std::invalid_argument("OrderBook::reduce: quantity is not in (0, resting quantity]");
        }

        Level &queue = node.level->second;
        node.qty -= qty;
        queue.total -= qty;
        if (node.qty > 0)
        {
            return node.qty;
        }

        if (node.prev == Nil)
        {
            queue.head = node.next;
        }
        else
        {
            nodes_[node.prev].next = node.next;
        }
        if (node.next == Nil)
        {
            queue.tail = node.prev;
        }
        else
        {
            nodes_[node.next].prev = node.prev;
        }
        if (queue.head == Nil)
        {
            levels(node.side).erase(node.level);
        }
        free_.push_back(n);
        index_.erase(slot);
        return 0;
    }

    void OrderBook::clear()
    {
        nodes_.clear();
        free_.clear();
        index_.clear();
        bids_.clear();
        asks_.clear();
    }

    void OrderBook::apply(const Event &event)
    {
        switch (event.type)
        {
            case EventType::Add:
                add(event.order_id, event.side, event.price, event.qty);
                return;
            case EventType::Cancel:
            case EventType::Trade:
                reduce(event.order_id, event.qty);
                return;
            case EventType::Clear:
                clear();
                return;
            case EventType::Level:
                break;
        }
        throw std::invalid_argument("OrderBook::apply: not an event of an order-by-order stream");
    }

    std::optional<Price> OrderBook::best(Side side) const
    {
        const LevelMap &map = levels(side);
        if (map.empty())
        {
            return std::nullopt;
        }
        return side == Side::Buy ? std::prev(map.end())->first : map.begin()->first;
    }

    std::optional<OrderView> OrderBook::front(Side side) const
    {
        const LevelMap &map = levels(side);
        if (map.empty())
        {
            return std::nullopt;
        }
        const Level &queue = side == Side::Buy ? std::prev(map.end())->second : map.begin()->second;
        return view(nodes_[queue.head]);
    }

    std::optional<OrderView> OrderBook::find(OrderId id) const
    {
        const auto slot = index_.find(id);
        if (slot == index_.end())
        {
            return std::nullopt;
        }
        return view(nodes_[slot->second]);
    }

    Quantity OrderBook::levelQuantity(Side side, Price price) const
    {
        const LevelMap &map = levels(side);
        const auto level = map.find(price);
        return level == map.end() ? 0 : level->second.total;
    }

    std::vector<BookLevel> OrderBook::depth(Side side, std::size_t max_levels) const
    {
        const LevelMap &map = levels(side);
        std::vector<BookLevel> out;
        const auto collect = [&](auto first, auto last)
        {
            for (; first != last && out.size() < max_levels; ++first)
            {
                out.push_back(BookLevel{first->first, first->second.total});
            }
        };
        if (side == Side::Buy)
        {
            collect(map.rbegin(), map.rend());
        }
        else
        {
            collect(map.begin(), map.end());
        }
        return out;
    }

    std::vector<OrderView> OrderBook::orders(Side side) const
    {
        const LevelMap &map = levels(side);
        std::vector<OrderView> out;
        const auto collect = [&](auto first, auto last)
        {
            for (; first != last; ++first)
            {
                for (std::uint32_t n = first->second.head; n != Nil; n = nodes_[n].next)
                {
                    out.push_back(view(nodes_[n]));
                }
            }
        };
        if (side == Side::Buy)
        {
            collect(map.rbegin(), map.rend());
        }
        else
        {
            collect(map.begin(), map.end());
        }
        return out;
    }

    OrderView OrderBook::view(const Node &node) const
    {
        return OrderView{node.id, node.side, node.level->first, node.qty};
    }

} // namespace quantMicrostructure
