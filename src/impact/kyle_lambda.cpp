#include "quantMicrostructure/impact/kyle_lambda.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace quantMicrostructure
{

    Regression linearRegression(std::span<const double> x, std::span<const double> y)
    {
        const std::size_t n = x.size();
        if (y.size() != n || n < 3)
        {
            throw std::invalid_argument("linearRegression: need two samples of the same length, at least three points");
        }
        double mean_x = 0.0;
        double mean_y = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            mean_x += x[i];
            mean_y += y[i];
        }
        mean_x /= static_cast<double>(n);
        mean_y /= static_cast<double>(n);
        double sxx = 0.0;
        double sxy = 0.0;
        double syy = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            sxx += (x[i] - mean_x) * (x[i] - mean_x);
            sxy += (x[i] - mean_x) * (y[i] - mean_y);
            syy += (y[i] - mean_y) * (y[i] - mean_y);
        }
        if (!(sxx > 0.0))
        {
            throw std::invalid_argument("linearRegression: x is constant");
        }
        Regression out;
        out.observations = n;
        out.slope = sxy / sxx;
        out.intercept = mean_y - out.slope * mean_x;
        const double residual = std::max(0.0, syy - out.slope * sxy);
        out.r_squared = syy > 0.0 ? 1.0 - residual / syy : 1.0;
        out.slope_se = std::sqrt(residual / static_cast<double>(n - 2) / sxx);
        return out;
    }

    FlowBars signedFlowBars(const Tape &tape, Timestamp interval)
    {
        if (interval <= 0)
        {
            throw std::invalid_argument("signedFlowBars: interval must be positive");
        }
        FlowBars bars;
        std::size_t quote = 0;
        while (quote < tape.quotes.size() && !tape.quotes[quote].twoSided())
        {
            ++quote;
        }
        if (quote == tape.quotes.size())
        {
            return bars;
        }
        double mid = tape.quotes[quote].mid();
        std::size_t trade = 0;
        const Timestamp start = tape.quotes[quote].ts;
        while (trade < tape.trades.size() && tape.trades[trade].ts <= start)
        {
            ++trade;
        }
        for (Timestamp from = start; from + interval <= tape.end; from += interval)
        {
            const Timestamp to = from + interval;
            const double mid_before = mid;
            for (; quote < tape.quotes.size() && tape.quotes[quote].ts <= to; ++quote)
            {
                if (tape.quotes[quote].twoSided())
                {
                    mid = tape.quotes[quote].mid();
                }
            }
            double flow = 0.0;
            for (; trade < tape.trades.size() && tape.trades[trade].ts <= to; ++trade)
            {
                flow += tape.trades[trade].sign() * static_cast<double>(tape.trades[trade].qty);
            }
            bars.flow.push_back(flow);
            bars.price_change.push_back(mid - mid_before);
        }
        return bars;
    }

    Regression kyleLambda(const Tape &tape, Timestamp interval)
    {
        const FlowBars bars = signedFlowBars(tape, interval);
        return linearRegression(bars.flow, bars.price_change);
    }

} // namespace quantMicrostructure
