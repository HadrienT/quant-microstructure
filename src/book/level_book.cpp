#include "quantMicrostructure/book/level_book.hpp"

#include <iterator>
#include <stdexcept>

namespace quantMicrostructure
{

    void LevelBook::set(Side side, Price price, Quantity qty)
    {
        if (qty < 0)
        {
            throw std::invalid_argument("LevelBook::set: quantity must not be negative");
        }
        if (qty == 0)
        {
            levels(side).erase(price);
        }
        else
        {
            levels(side).insert_or_assign(price, qty);
        }
    }

    void LevelBook::clear()
    {
        bids_.clear();
        asks_.clear();
    }

    void LevelBook::apply(const Event &event)
    {
        switch (event.type)
        {
            case EventType::Level:
                set(event.side, event.price, event.qty);
                return;
            case EventType::Trade:
                return;
            case EventType::Clear:
                clear();
                return;
            case EventType::Add:
            case EventType::Cancel:
                break;
        }
        throw std::invalid_argument("LevelBook::apply: not an event of an aggregated stream");
    }

    std::optional<Price> LevelBook::best(Side side) const
    {
        const LevelMap &map = levels(side);
        if (map.empty())
        {
            return std::nullopt;
        }
        return side == Side::Buy ? std::prev(map.end())->first : map.begin()->first;
    }

    Quantity LevelBook::levelQuantity(Side side, Price price) const
    {
        const LevelMap &map = levels(side);
        const auto level = map.find(price);
        return level == map.end() ? 0 : level->second;
    }

    std::vector<BookLevel> LevelBook::depth(Side side, std::size_t max_levels) const
    {
        const LevelMap &map = levels(side);
        std::vector<BookLevel> out;
        const auto collect = [&](auto first, auto last)
        {
            for (; first != last && out.size() < max_levels; ++first)
            {
                out.push_back(BookLevel{first->first, first->second});
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

} // namespace quantMicrostructure
