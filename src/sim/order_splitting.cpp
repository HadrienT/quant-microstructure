#include "quantMicrostructure/sim/order_splitting.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace quantMicrostructure
{

    std::vector<double> simulateOrderSplittingSigns(double alpha, std::size_t hidden_orders, std::size_t count, Rng &rng)
    {
        if (!(alpha > 1.0) || hidden_orders == 0)
        {
            throw std::invalid_argument("simulateOrderSplittingSigns: need alpha > 1 and at least one hidden order");
        }
        struct Hidden
        {
            double sign{};
            double remaining{};
        };
        const auto fresh = [&]()
        {
            Hidden order;
            order.sign = rng.below(2) == 0 ? 1.0 : -1.0;
            order.remaining = std::floor(std::pow(rng.uniform(), -1.0 / alpha));
            return order;
        };
        std::vector<Hidden> orders;
        for (std::size_t i = 0; i < hidden_orders; ++i)
        {
            orders.push_back(fresh());
        }
        std::vector<double> signs;
        signs.reserve(count);
        for (std::size_t n = 0; n < count; ++n)
        {
            Hidden &order = orders[static_cast<std::size_t>(rng.below(hidden_orders))];
            signs.push_back(order.sign);
            order.remaining -= 1.0;
            if (order.remaining < 1.0)
            {
                order = fresh();
            }
        }
        return signs;
    }

} // namespace quantMicrostructure
