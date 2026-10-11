#ifndef QUANTMICROSTRUCTURE_EXECUTION_SCHEDULE_HPP
#define QUANTMICROSTRUCTURE_EXECUTION_SCHEDULE_HPP

#include "quantMicrostructure/core/types.hpp"
#include "quantMicrostructure/execution/almgren_chriss.hpp"
#include "quantMicrostructure/replay/backtest.hpp"

#include <cstddef>
#include <span>
#include <vector>

// The usual execution targets (docs/companion.md, section 8.3), and a
// strategy that carries a schedule out in a backtest.
//
// Two stages make an execution algorithm: the schedule (how much in each
// slice) and the tactic (how, within the slice). Only the simplest tactic is
// written here: a market order at the start of each slice.

namespace quantMicrostructure
{

    /// TWAP: the same quantity in each interval (Almgren & Chriss 2000,
    /// equation 9).
    TradeSchedule twapSchedule(double shares, double horizon, std::size_t intervals);

    /// VWAP: in each interval, the share of the order equal to the share of
    /// the day's volume expected there. `volume_profile` holds one
    /// non-negative weight per interval, in any unit.
    TradeSchedule vwapSchedule(double shares, double horizon, std::span<const double> volume_profile);

    /// POV: in each interval, `participation` times the volume the market
    /// traded there, until the order is done. Unlike the two above it follows
    /// the market, so the volumes are those observed, and the order may not be
    /// finished at the end: holdings.back() is what is left.
    TradeSchedule povSchedule(double shares, double participation, std::span<const double> market_volumes, double tau);

    /// Whole lots for each slice of a schedule, rounding the running total so
    /// that the lots add up to the rounded size of the order.
    std::vector<Quantity> roundToLots(const TradeSchedule &schedule);

    /// Sends `lots[k]` by market order when the clock first reaches `start` +
    /// k * `interval`. A slice whose time has passed without any market event
    /// is sent at the next one.
    class ScheduleExecutor : public Strategy
    {
      public:
        ScheduleExecutor(Side side, std::vector<Quantity> lots, Timestamp start, Timestamp interval);

        void onMarket(const MarketView &view, Gateway &gateway) override;

        /// Mid price when the first slice was sent; 0 before.
        double arrivalMid() const noexcept { return arrival_mid_; }
        bool done() const noexcept { return next_ == lots_.size(); }

      private:
        Side side_;
        std::vector<Quantity> lots_;
        Timestamp start_;
        Timestamp interval_;
        std::size_t next_{0};
        double arrival_mid_{0.0};
    };

} // namespace quantMicrostructure

#endif
