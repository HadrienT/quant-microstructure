#include "quantMicrostructure/execution/tca.hpp"

namespace quantMicrostructure
{

    ShortfallBreakdown implementationShortfall(const ExecutionRecord &record, std::span<const Fill> fills)
    {
        const double sign = record.side == Side::Buy ? 1.0 : -1.0;
        ShortfallBreakdown out;
        double notional = 0.0;
        for (const Fill &fill : fills)
        {
            out.filled += fill.qty;
            notional += static_cast<double>(fill.price) * static_cast<double>(fill.qty);
        }
        const double filled = static_cast<double>(out.filled);
        out.average_price = out.filled > 0 ? notional / filled : 0.0;
        out.delay = sign * filled * (record.arrival_price - record.decision_price);
        out.trading = sign * (notional - filled * record.arrival_price);
        out.opportunity = sign * static_cast<double>(record.target - out.filled) * (record.final_price - record.decision_price);
        out.fees = record.fees;
        out.total = out.delay + out.trading + out.opportunity + out.fees;
        return out;
    }

    std::optional<double> marketVwap(const Tape &tape, Timestamp from, Timestamp to)
    {
        double notional = 0.0;
        double volume = 0.0;
        for (const Trade &trade : tape.trades)
        {
            if (trade.ts > from && trade.ts <= to)
            {
                notional += trade.price * static_cast<double>(trade.qty);
                volume += static_cast<double>(trade.qty);
            }
        }
        if (!(volume > 0.0))
        {
            return std::nullopt;
        }
        return notional / volume;
    }

} // namespace quantMicrostructure
