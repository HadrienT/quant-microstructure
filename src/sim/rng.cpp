#include "quantMicrostructure/sim/rng.hpp"

#include <cmath>
#include <numbers>

namespace quantMicrostructure
{

    Rng::Rng(std::uint64_t seed)
        : engine_(seed)
    {
    }

    std::uint64_t Rng::next()
    {
        return engine_();
    }

    std::uint64_t Rng::below(std::uint64_t bound)
    {
        // Draws under 2^64 mod bound would make the small values more likely.
        const std::uint64_t threshold = (0 - bound) % bound;
        std::uint64_t draw = next();
        while (draw < threshold)
        {
            draw = next();
        }
        return draw % bound;
    }

    double Rng::uniform()
    {
        constexpr double Scale = 1.0 / 9007199254740992.0; // 2^-53
        return (static_cast<double>(next() >> 11) + 0.5) * Scale;
    }

    bool Rng::bernoulli(double probability)
    {
        return uniform() < probability;
    }

    double Rng::exponential(double rate)
    {
        return -std::log(uniform()) / rate;
    }

    double Rng::normal()
    {
        const double radius = std::sqrt(-2.0 * std::log(uniform()));
        return radius * std::cos(2.0 * std::numbers::pi * uniform());
    }

} // namespace quantMicrostructure
