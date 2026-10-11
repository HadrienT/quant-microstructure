#include "quantMicrostructure/execution/schedule.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace quantMicrostructure
{

    TradeSchedule twapSchedule(double shares, double horizon, std::size_t intervals)
    {
        if (!(horizon > 0.0) || intervals == 0)
        {
            throw std::invalid_argument("twapSchedule: horizon and number of intervals must be positive");
        }
        const std::vector<double> trades(intervals, shares / static_cast<double>(intervals));
        return scheduleFromTrades(trades, horizon / static_cast<double>(intervals));
    }

    TradeSchedule vwapSchedule(double shares, double horizon, std::span<const double> volume_profile)
    {
        double total = 0.0;
        for (const double weight : volume_profile)
        {
            if (weight < 0.0)
            {
                throw std::invalid_argument("vwapSchedule: a volume weight is negative");
            }
            total += weight;
        }
        if (!(horizon > 0.0) || !(total > 0.0))
        {
            throw std::invalid_argument("vwapSchedule: horizon and total volume must be positive");
        }
        std::vector<double> trades;
        trades.reserve(volume_profile.size());
        for (const double weight : volume_profile)
        {
            trades.push_back(shares * weight / total);
        }
        return scheduleFromTrades(trades, horizon / static_cast<double>(volume_profile.size()));
    }

    TradeSchedule povSchedule(double shares, double participation, std::span<const double> market_volumes, double tau)
    {
        if (!(participation > 0.0) || shares < 0.0)
        {
            throw std::invalid_argument("povSchedule: participation must be positive and the order size non-negative");
        }
        std::vector<double> trades;
        trades.reserve(market_volumes.size());
        double remaining = shares;
        for (const double volume : market_volumes)
        {
            const double traded = std::min(remaining, participation * std::max(0.0, volume));
            trades.push_back(traded);
            remaining -= traded;
        }
        TradeSchedule schedule = scheduleFromTrades(trades, tau);
        // What the market did not let us trade is still held at the end.
        for (double &holding : schedule.holdings)
        {
            holding += remaining;
        }
        return schedule;
    }

    std::vector<Quantity> roundToLots(const TradeSchedule &schedule)
    {
        std::vector<Quantity> lots;
        lots.reserve(schedule.trades.size());
        double running = 0.0;
        Quantity sent = 0;
        for (const double n : schedule.trades)
        {
            running += n;
            const Quantity target = static_cast<Quantity>(std::llround(running));
            lots.push_back(target - sent);
            sent = target;
        }
        return lots;
    }

    ScheduleExecutor::ScheduleExecutor(Side side, std::vector<Quantity> lots, Timestamp start, Timestamp interval)
        : side_(side),
          lots_(std::move(lots)),
          start_(start),
          interval_(interval)
    {
        if (interval <= 0)
        {
            throw std::invalid_argument("ScheduleExecutor: interval must be positive");
        }
        for (const Quantity lot : lots_)
        {
            if (lot < 0)
            {
                throw std::invalid_argument("ScheduleExecutor: a slice is negative");
            }
        }
    }

    void ScheduleExecutor::onMarket(const MarketView &view, Gateway &gateway)
    {
        while (next_ < lots_.size() && view.now >= start_ + static_cast<Timestamp>(next_) * interval_)
        {
            if (arrival_mid_ == 0.0 && view.quote.twoSided())
            {
                arrival_mid_ = view.quote.mid();
            }
            if (lots_[next_] > 0)
            {
                gateway.submitMarket(side_, lots_[next_]);
            }
            ++next_;
        }
    }

} // namespace quantMicrostructure
