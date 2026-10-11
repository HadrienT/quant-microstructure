#include "quantMicrostructure/impact/round_trip.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace quantMicrostructure
{

    double impactTradingCost(std::span<const double> trades, std::span<const double> kernel, double exponent)
    {
        std::vector<double> push(trades.size());
        for (std::size_t k = 0; k < trades.size(); ++k)
        {
            push[k] = trades[k] < 0.0 ? -std::pow(-trades[k], exponent) : std::pow(trades[k], exponent);
        }
        double cost = 0.0;
        for (std::size_t n = 0; n < trades.size(); ++n)
        {
            double price = 0.0;
            const std::size_t reach = std::min(kernel.size(), n + 1);
            for (std::size_t lag = 0; lag < reach; ++lag)
            {
                price += kernel[lag] * push[n - lag];
            }
            cost += trades[n] * price;
        }
        return cost;
    }

} // namespace quantMicrostructure
