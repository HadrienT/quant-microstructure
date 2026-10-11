#ifndef QUANTMICROSTRUCTURE_FEED_SEQUENCE_TRACKER_HPP
#define QUANTMICROSTRUCTURE_FEED_SEQUENCE_TRACKER_HPP

#include "quantMicrostructure/core/types.hpp"

#include <cstdint>

namespace quantMicrostructure
{

    enum class SequenceStatus : std::uint8_t
    {
        /// The expected number (or the first event seen).
        InOrder,
        /// Events are missing before this one: the book rebuilt from the stream
        /// is wrong until the next Clear and snapshot.
        Gap,
        /// Already seen, or older than the last one: to be dropped.
        Stale
    };

    /// Checks that sequence numbers go up by one (docs/companion.md, section
    /// 2.4).
    class SequenceTracker
    {
      public:
        SequenceStatus observe(SequenceNumber seq) noexcept;

        /// Number of gaps seen, and number of events missing in them.
        std::uint64_t gaps() const noexcept { return gaps_; }
        std::uint64_t missing() const noexcept { return missing_; }

      private:
        SequenceNumber last_{};
        std::uint64_t gaps_{};
        std::uint64_t missing_{};
        bool started_{false};
    };

} // namespace quantMicrostructure

#endif
