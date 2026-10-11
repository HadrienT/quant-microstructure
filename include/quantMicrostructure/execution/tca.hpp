#ifndef QUANTMICROSTRUCTURE_EXECUTION_TCA_HPP
#define QUANTMICROSTRUCTURE_EXECUTION_TCA_HPP

#include "quantMicrostructure/core/types.hpp"
#include "quantMicrostructure/measures/tape.hpp"
#include "quantMicrostructure/replay/backtest.hpp"

#include <optional>
#include <span>

namespace quantMicrostructure
{

    /// What a transaction cost analysis needs to know of an order, after the
    /// fact (docs/companion.md, sections 8.1 and 8.4).
    struct ExecutionRecord
    {
        Side side{};
        /// Lots the order was for.
        Quantity target{};
        /// Mid price when the order was decided.
        double decision_price{};
        /// Mid price when the first child order was sent.
        double arrival_price{};
        /// Mid price at the end, at which what was not executed is valued.
        double final_price{};
        /// Fees paid, in ticks times lots.
        double fees{};
    };

    /// Implementation shortfall, in ticks times lots: what the order cost
    /// compared with buying (or selling) all of it at the decision price. With
    /// s = +1 for a buy and -1 for a sell, q_i and p_i the fills, Q the target:
    ///
    ///     total = s sum q_i (p_i - p_decision) + s (Q - sum q_i)(p_final - p_decision) + fees
    ///
    /// The first sum splits into what happened before the first order
    /// (`delay`, at the arrival price) and during the execution (`trading`).
    /// A positive number is a cost.
    ///
    /// This is the decomposition described in section 8.4 of the companion.
    /// Perold (1988), who introduced the measure, was not re-read.
    struct ShortfallBreakdown
    {
        /// s (filled)(p_arrival - p_decision): the price moved before we began.
        double delay{};
        /// s sum q_i (p_i - p_arrival): spread and impact paid while trading.
        double trading{};
        /// s (Q - filled)(p_final - p_decision): what the unexecuted part
        /// missed.
        double opportunity{};
        double fees{};
        /// The sum of the four.
        double total{};
        Quantity filled{};
        /// Volume-weighted average fill price; 0 with no fill.
        double average_price{};
    };

    ShortfallBreakdown implementationShortfall(const ExecutionRecord &record, std::span<const Fill> fills);

    /// Volume-weighted average price of the market's trades with a timestamp
    /// in (from, to]: the VWAP benchmark. Empty if there was none. An
    /// algorithm judged against it has an interest in trading like the market,
    /// not in trading well.
    std::optional<double> marketVwap(const Tape &tape, Timestamp from, Timestamp to);

} // namespace quantMicrostructure

#endif
