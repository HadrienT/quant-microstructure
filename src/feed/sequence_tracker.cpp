#include "quantMicrostructure/feed/sequence_tracker.hpp"

namespace quantMicrostructure
{

    SequenceStatus SequenceTracker::observe(SequenceNumber seq) noexcept
    {
        if (!started_)
        {
            started_ = true;
            last_ = seq;
            return SequenceStatus::InOrder;
        }
        if (seq <= last_)
        {
            return SequenceStatus::Stale;
        }
        const SequenceNumber skipped = seq - last_ - 1;
        last_ = seq;
        if (skipped == 0)
        {
            return SequenceStatus::InOrder;
        }
        ++gaps_;
        missing_ += skipped;
        return SequenceStatus::Gap;
    }

} // namespace quantMicrostructure
