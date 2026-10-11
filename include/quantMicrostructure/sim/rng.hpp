#ifndef QUANTMICROSTRUCTURE_SIM_RNG_HPP
#define QUANTMICROSTRUCTURE_SIM_RNG_HPP

#include <cstdint>
#include <random>

namespace quantMicrostructure
{

    /// The random source of every simulator.
    ///
    /// The engine is std::mt19937_64, whose output is fixed by the C++
    /// standard; the distributions are written here because the standard ones
    /// are not specified and differ between standard libraries. A seed
    /// therefore gives the same integers everywhere, and the same
    /// floating-point draws wherever std::log, std::sqrt and std::cos round the
    /// same way.
    class Rng
    {
      public:
        explicit Rng(std::uint64_t seed);

        std::uint64_t next();

        /// Uniform on {0, ..., bound - 1}, without modulo bias. `bound` must be
        /// positive.
        std::uint64_t below(std::uint64_t bound);

        /// Uniform on the open interval (0, 1), with 53 random bits.
        double uniform();

        bool bernoulli(double probability);

        /// Exponential of the given rate (mean 1 / rate).
        double exponential(double rate);

        /// Standard normal, by the Box-Muller transform.
        double normal();

      private:
        std::mt19937_64 engine_;
    };

} // namespace quantMicrostructure

#endif
