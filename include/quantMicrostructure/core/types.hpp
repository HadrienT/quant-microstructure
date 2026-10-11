#ifndef QUANTMICROSTRUCTURE_CORE_TYPES_HPP
#define QUANTMICROSTRUCTURE_CORE_TYPES_HPP

#include <cstdint>

namespace quantMicrostructure
{

    /// A price, as a whole number of ticks. Never a floating-point number: two
    /// prices are equal or they are not.
    using Price = std::int64_t;
    /// A quantity, as a whole number of lots (the smallest size increment of
    /// the instrument).
    using Quantity = std::int64_t;
    /// A time, in nanoseconds. The origin belongs to the stream: the Unix epoch
    /// for a recording, the start of the run for a simulation.
    using Timestamp = std::int64_t;
    using OrderId = std::uint64_t;
    using SequenceNumber = std::uint64_t;

    enum class Side : std::uint8_t
    {
        Buy = 0,
        Sell = 1
    };

    constexpr Side opposite(Side side) noexcept
    {
        return side == Side::Buy ? Side::Sell : Side::Buy;
    }

} // namespace quantMicrostructure

#endif
