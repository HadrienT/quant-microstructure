#include "quantMicrostructure/measures/spread.hpp"

#include <algorithm>
#include <cmath>

namespace quantMicrostructure
{

    std::optional<double> rollSpread(std::span<const double> trade_prices)
    {
        if (trade_prices.size() < 3)
        {
            return std::nullopt;
        }
        // Covariance of consecutive price changes.
        const std::size_t pairs = trade_prices.size() - 2;
        double mean_now = 0.0;
        double mean_before = 0.0;
        for (std::size_t i = 0; i < pairs; ++i)
        {
            mean_before += trade_prices[i + 1] - trade_prices[i];
            mean_now += trade_prices[i + 2] - trade_prices[i + 1];
        }
        mean_before /= static_cast<double>(pairs);
        mean_now /= static_cast<double>(pairs);
        double covariance = 0.0;
        for (std::size_t i = 0; i < pairs; ++i)
        {
            covariance += (trade_prices[i + 1] - trade_prices[i] - mean_before) * (trade_prices[i + 2] - trade_prices[i + 1] - mean_now);
        }
        covariance /= static_cast<double>(pairs);
        if (covariance >= 0.0)
        {
            return std::nullopt;
        }
        return 2.0 * std::sqrt(-covariance);
    }

    std::optional<double> timeWeightedSpread(const Tape &tape)
    {
        double weighted = 0.0;
        double duration = 0.0;
        for (std::size_t i = 0; i < tape.quotes.size(); ++i)
        {
            const Quote &quote = tape.quotes[i];
            if (!quote.twoSided())
            {
                continue;
            }
            const Timestamp until = i + 1 < tape.quotes.size() ? tape.quotes[i + 1].ts : tape.end;
            const double held = static_cast<double>(until - quote.ts);
            weighted += quote.spread() * held;
            duration += held;
        }
        if (duration <= 0.0)
        {
            return std::nullopt;
        }
        return weighted / duration;
    }

    SpreadDecomposition spreadDecomposition(const Tape &tape, Timestamp horizon)
    {
        SpreadDecomposition sum;
        for (const Trade &trade : tape.trades)
        {
            if (trade.quote == Trade::NoQuote || !tape.quotes[trade.quote].twoSided())
            {
                continue;
            }
            const Timestamp later = trade.ts + horizon;
            if (later > tape.end)
            {
                continue;
            }
            // The last quote at or before `later`.
            const auto after = std::upper_bound(tape.quotes.begin(), tape.quotes.end(), later, [](Timestamp t, const Quote &q)
                                                { return t < q.ts; });
            if (after == tape.quotes.begin() || !std::prev(after)->twoSided())
            {
                continue;
            }
            const double mid = tape.quotes[trade.quote].mid();
            const double mid_later = std::prev(after)->mid();
            sum.effective += trade.sign() * (trade.price - mid);
            sum.impact += trade.sign() * (mid_later - mid);
            sum.realized += trade.sign() * (trade.price - mid_later);
            ++sum.trades;
        }
        if (sum.trades > 0)
        {
            const double n = static_cast<double>(sum.trades);
            sum.effective /= n;
            sum.impact /= n;
            sum.realized /= n;
        }
        return sum;
    }

} // namespace quantMicrostructure
