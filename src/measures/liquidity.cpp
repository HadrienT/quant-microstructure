#include "quantMicrostructure/measures/liquidity.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace quantMicrostructure
{

    double imbalance(const Quote &quote)
    {
        return static_cast<double>(quote.bid_qty) / static_cast<double>(quote.bid_qty + quote.ask_qty);
    }

    double weightedMid(const Quote &quote)
    {
        const double bid_qty = static_cast<double>(quote.bid_qty);
        const double ask_qty = static_cast<double>(quote.ask_qty);
        return (static_cast<double>(quote.ask) * bid_qty + static_cast<double>(quote.bid) * ask_qty) / (bid_qty + ask_qty);
    }

    Quantity depthWithin(std::span<const BookLevel> levels, Price ticks)
    {
        Quantity total = 0;
        for (const BookLevel &level : levels)
        {
            if (std::abs(level.price - levels.front().price) > ticks)
            {
                break;
            }
            total += level.qty;
        }
        return total;
    }

    double ImbalanceBin::probabilityUp() const noexcept
    {
        const std::size_t total = up + down;
        return total == 0 ? 0.5 : static_cast<double>(up) / static_cast<double>(total);
    }

    std::vector<ImbalanceBin> imbalanceNextMove(const Tape &tape, std::size_t bins)
    {
        if (bins == 0)
        {
            throw std::invalid_argument("imbalanceNextMove: at least one bin is needed");
        }
        std::vector<ImbalanceBin> out(bins);
        for (std::size_t b = 0; b < bins; ++b)
        {
            out[b].lower = static_cast<double>(b) / static_cast<double>(bins);
            out[b].upper = static_cast<double>(b + 1) / static_cast<double>(bins);
        }

        // Walk back in time, carrying the direction of the next mid change
        // seen from the nearest later two-sided quote. One-sided quotes have
        // no mid and are stepped over.
        int next_move = 0;
        double later_mid = 0.0;
        bool has_later = false;
        for (std::size_t i = tape.quotes.size(); i-- > 0;)
        {
            const Quote &quote = tape.quotes[i];
            if (!quote.twoSided())
            {
                continue;
            }
            const double mid = quote.mid();
            if (has_later && later_mid != mid)
            {
                next_move = later_mid > mid ? 1 : -1;
            }
            later_mid = mid;
            has_later = true;
            if (next_move == 0)
            {
                continue;
            }
            const double value = imbalance(quote);
            const std::size_t bin = std::min(bins - 1, static_cast<std::size_t>(value * static_cast<double>(bins)));
            ++(next_move > 0 ? out[bin].up : out[bin].down);
        }
        return out;
    }

} // namespace quantMicrostructure
