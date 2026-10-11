#include "quantMicrostructure/measures/volatility.hpp"

#include <stdexcept>

namespace quantMicrostructure
{

    double realizedVariance(std::span<const double> prices, std::size_t step, std::size_t offset)
    {
        if (step == 0)
        {
            throw std::invalid_argument("realizedVariance: step must be positive");
        }
        double sum = 0.0;
        for (std::size_t i = offset; i + step < prices.size(); i += step)
        {
            const double change = prices[i + step] - prices[i];
            sum += change * change;
        }
        return sum;
    }

    std::vector<SignaturePoint> signaturePlot(std::span<const double> prices, std::span<const std::size_t> steps)
    {
        std::vector<SignaturePoint> out;
        out.reserve(steps.size());
        for (const std::size_t step : steps)
        {
            SignaturePoint point;
            point.step = step;
            point.variance = realizedVariance(prices, step);
            point.increments = prices.empty() ? 0 : (prices.size() - 1) / step;
            out.push_back(point);
        }
        return out;
    }

    double noiseVariance(std::span<const double> prices)
    {
        if (prices.size() < 2)
        {
            throw std::invalid_argument("noiseVariance: at least two prices are needed");
        }
        return realizedVariance(prices) / (2.0 * static_cast<double>(prices.size() - 1));
    }

    double twoScaleRealizedVariance(std::span<const double> prices, std::size_t subgrids, bool adjust)
    {
        const std::size_t n = prices.empty() ? 0 : prices.size() - 1;
        if (subgrids < 2 || subgrids > n)
        {
            throw std::invalid_argument("twoScaleRealizedVariance: need 2 <= subgrids <= number of increments");
        }
        double average = 0.0;
        for (std::size_t k = 0; k < subgrids; ++k)
        {
            average += realizedVariance(prices, subgrids, k);
        }
        average /= static_cast<double>(subgrids);

        const double n_bar = static_cast<double>(n - subgrids + 1) / static_cast<double>(subgrids);
        const double ratio = n_bar / static_cast<double>(n);
        const double estimate = average - ratio * realizedVariance(prices);
        return adjust ? estimate / (1.0 - ratio) : estimate;
    }

    std::vector<double> sampleMid(const Tape &tape, Timestamp interval)
    {
        if (interval <= 0)
        {
            throw std::invalid_argument("sampleMid: interval must be positive");
        }
        std::vector<double> out;
        std::size_t next = 0;
        while (next < tape.quotes.size() && !tape.quotes[next].twoSided())
        {
            ++next;
        }
        if (next == tape.quotes.size())
        {
            return out;
        }
        double mid = tape.quotes[next].mid();
        for (Timestamp t = tape.quotes[next].ts; t <= tape.end; t += interval)
        {
            for (; next < tape.quotes.size() && tape.quotes[next].ts <= t; ++next)
            {
                if (tape.quotes[next].twoSided())
                {
                    mid = tape.quotes[next].mid();
                }
            }
            out.push_back(mid);
        }
        return out;
    }

} // namespace quantMicrostructure
