#include "quantMicrostructure/measures/flow.hpp"

#include <stdexcept>

namespace quantMicrostructure
{

    std::vector<double> tradePrices(const Tape &tape)
    {
        std::vector<double> prices;
        prices.reserve(tape.trades.size());
        for (const Trade &trade : tape.trades)
        {
            prices.push_back(trade.price);
        }
        return prices;
    }

    std::vector<double> tradeSigns(const Tape &tape)
    {
        std::vector<double> signs;
        signs.reserve(tape.trades.size());
        for (const Trade &trade : tape.trades)
        {
            signs.push_back(trade.sign());
        }
        return signs;
    }

    std::vector<double> autocovariance(std::span<const double> x, std::size_t max_lag)
    {
        std::vector<double> out;
        if (x.empty())
        {
            return out;
        }
        double mean = 0.0;
        for (const double v : x)
        {
            mean += v;
        }
        mean /= static_cast<double>(x.size());
        for (std::size_t lag = 0; lag <= max_lag && lag < x.size(); ++lag)
        {
            double sum = 0.0;
            for (std::size_t i = lag; i < x.size(); ++i)
            {
                sum += x[i] * x[i - lag];
            }
            out.push_back(sum / static_cast<double>(x.size() - lag) - mean * mean);
        }
        return out;
    }

    std::vector<double> responseFunction(const Tape &tape, std::size_t max_lag)
    {
        const std::size_t n = tape.trades.size();
        // Mid before each trade, where there is one.
        std::vector<double> mid(n, 0.0);
        std::vector<char> known(n, 0);
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::size_t q = tape.trades[i].quote;
            if (q != Trade::NoQuote && tape.quotes[q].twoSided())
            {
                mid[i] = tape.quotes[q].mid();
                known[i] = 1;
            }
        }
        std::vector<double> out;
        for (std::size_t lag = 0; lag <= max_lag && lag < n; ++lag)
        {
            double sum = 0.0;
            std::size_t pairs = 0;
            for (std::size_t i = 0; i + lag < n; ++i)
            {
                if (known[i] != 0 && known[i + lag] != 0)
                {
                    sum += (mid[i + lag] - mid[i]) * tape.trades[i].sign();
                    ++pairs;
                }
            }
            out.push_back(pairs == 0 ? 0.0 : sum / static_cast<double>(pairs));
        }
        return out;
    }

    std::vector<double> tickTestSigns(std::span<const double> trade_prices)
    {
        std::vector<double> signs(trade_prices.size(), 0.0);
        double last = 0.0;
        for (std::size_t i = 1; i < trade_prices.size(); ++i)
        {
            if (trade_prices[i] > trade_prices[i - 1])
            {
                last = 1.0;
            }
            else if (trade_prices[i] < trade_prices[i - 1])
            {
                last = -1.0;
            }
            signs[i] = last;
        }
        return signs;
    }

    std::vector<double> quoteRuleSigns(const Tape &tape)
    {
        const std::vector<double> prices = tradePrices(tape);
        std::vector<double> signs = tickTestSigns(prices);
        for (std::size_t i = 0; i < tape.trades.size(); ++i)
        {
            const std::size_t q = tape.trades[i].quote;
            if (q == Trade::NoQuote || !tape.quotes[q].twoSided())
            {
                continue;
            }
            const double mid = tape.quotes[q].mid();
            if (prices[i] > mid)
            {
                signs[i] = 1.0;
            }
            else if (prices[i] < mid)
            {
                signs[i] = -1.0;
            }
        }
        return signs;
    }

    double signAgreement(std::span<const double> a, std::span<const double> b)
    {
        if (a.size() != b.size())
        {
            throw std::invalid_argument("signAgreement: the two sequences must have the same length");
        }
        std::size_t compared = 0;
        std::size_t same = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (a[i] != 0.0 && b[i] != 0.0)
            {
                ++compared;
                same += a[i] == b[i] ? 1 : 0;
            }
        }
        return compared == 0 ? 0.0 : static_cast<double>(same) / static_cast<double>(compared);
    }

} // namespace quantMicrostructure
