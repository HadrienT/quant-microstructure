#include "quantMicrostructure/mm/market_maker.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace quantMicrostructure
{

    MarketMaker::MarketMaker(TickQuotePolicy policy, const MarketMakerConfig &config)
        : policy_(std::move(policy)),
          config_(config)
    {
        if (!policy_ || config.order_size <= 0 || config.inventory_limit < config.order_size)
        {
            throw std::invalid_argument("MarketMaker: needs a policy, a positive order size and an inventory limit of at least one order");
        }
    }

    void MarketMaker::onMarket(const MarketView &view, Gateway &gateway)
    {
        requote(view, gateway);
    }

    void MarketMaker::onFill(const Fill &fill, const MarketView &view, Gateway &gateway)
    {
        for (Working *working : {&bid_, &ask_})
        {
            if (working->id != 0 && working->id == fill.order)
            {
                working->remaining -= fill.qty;
                if (working->remaining <= 0)
                {
                    *working = Working{};
                }
            }
        }
        requote(view, gateway);
    }

    void MarketMaker::requote(const MarketView &view, Gateway &gateway)
    {
        if (!view.quote.twoSided())
        {
            // No reference price: stand aside.
            update(bid_, Side::Buy, false, 0, gateway);
            update(ask_, Side::Sell, false, 0, gateway);
            return;
        }
        const double mid = view.quote.mid();
        const Quotes quotes = policy_(view.position, view.now);
        // Whole ticks, away from the mid, and never through the book.
        const Price bid = std::min(static_cast<Price>(std::floor(mid - quotes.bid)), view.quote.ask - 1);
        const Price ask = std::max(static_cast<Price>(std::ceil(mid + quotes.ask)), view.quote.bid + 1);
        const bool may_buy = std::isfinite(quotes.bid) && view.position + config_.order_size <= config_.inventory_limit;
        const bool may_sell = std::isfinite(quotes.ask) && view.position - config_.order_size >= -config_.inventory_limit;
        update(bid_, Side::Buy, may_buy, bid, gateway);
        update(ask_, Side::Sell, may_sell, ask, gateway);
    }

    void MarketMaker::update(Working &working, Side side, bool wanted, Price price, Gateway &gateway)
    {
        if (working.id != 0 && (!wanted || working.price != price))
        {
            gateway.cancel(working.id);
            working = Working{};
        }
        if (wanted && working.id == 0)
        {
            working.id = gateway.submitLimit(side, price, config_.order_size);
            working.price = price;
            working.remaining = config_.order_size;
        }
    }

    namespace
    {

        /// Mid of the last quote at or before `ts` (or strictly before it);
        /// false if that quote is one-sided or there is none.
        bool midAt(const Tape &tape, Timestamp ts, bool strictly_before, double &mid)
        {
            const auto after = strictly_before
                                   ? std::lower_bound(tape.quotes.begin(), tape.quotes.end(), ts, [](const Quote &q, Timestamp t)
                                                      { return q.ts < t; })
                                   : std::upper_bound(tape.quotes.begin(), tape.quotes.end(), ts, [](Timestamp t, const Quote &q)
                                                      { return t < q.ts; });
            if (after == tape.quotes.begin() || !std::prev(after)->twoSided())
            {
                return false;
            }
            mid = std::prev(after)->mid();
            return true;
        }

    } // namespace

    MakerPnl makerPnl(const BacktestResult &result, const Tape &tape)
    {
        MakerPnl out;
        for (const Fill &fill : result.fills)
        {
            const double sign = fill.side == Side::Buy ? 1.0 : -1.0;
            const double qty = static_cast<double>(fill.qty);
            const double price = static_cast<double>(fill.price);
            double mid = price;
            if (midAt(tape, fill.ts, true, mid))
            {
                ++out.fills;
            }
            out.spread += sign * qty * (mid - price);
            out.inventory += sign * qty * (result.mark - mid);
        }
        out.fees = result.fees;
        out.total = out.spread + out.inventory - out.fees;
        return out;
    }

    std::vector<Markout> markouts(std::span<const Fill> fills, const Tape &tape, std::span<const Timestamp> horizons)
    {
        std::vector<Markout> out;
        out.reserve(horizons.size());
        for (const Timestamp horizon : horizons)
        {
            Markout markout;
            markout.horizon = horizon;
            double sum = 0.0;
            for (const Fill &fill : fills)
            {
                const Timestamp later = fill.ts + horizon;
                double mid = 0.0;
                if (later > tape.end || !midAt(tape, later, false, mid))
                {
                    continue;
                }
                const double sign = fill.side == Side::Buy ? 1.0 : -1.0;
                sum += sign * static_cast<double>(fill.qty) * (mid - static_cast<double>(fill.price));
                markout.lots += fill.qty;
            }
            markout.mean = markout.lots > 0 ? sum / static_cast<double>(markout.lots) : 0.0;
            out.push_back(markout);
        }
        return out;
    }

} // namespace quantMicrostructure
