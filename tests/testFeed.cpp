#include <gtest/gtest.h>

#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/feed/event_file.hpp"
#include "quantMicrostructure/feed/sequence_tracker.hpp"

#include <filesystem>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace quantMicrostructure
{

    namespace
    {

        std::vector<Event> sampleEvents()
        {
            Event add;
            add.ts = 1'760'000'000'000'000'000;
            add.ts_recv = add.ts + 1'234'567;
            add.seq = 1;
            add.order_id = std::numeric_limits<OrderId>::max();
            add.price = 6'512'345;
            add.qty = 42;
            add.type = EventType::Add;
            add.side = Side::Sell;

            // Extreme and negative values must survive the encoding.
            Event trade;
            trade.ts = std::numeric_limits<Timestamp>::min();
            trade.ts_recv = std::numeric_limits<Timestamp>::max();
            trade.seq = std::numeric_limits<SequenceNumber>::max();
            trade.order_id = 7;
            trade.price = -3;
            trade.qty = std::numeric_limits<Quantity>::max();
            trade.type = EventType::Trade;
            trade.side = Side::Buy;

            Event clear;
            clear.seq = 3;
            clear.type = EventType::Clear;

            return {add, trade, clear};
        }

        std::string encode(const std::vector<Event> &events)
        {
            std::ostringstream out(std::ios::binary);
            EventWriter writer(out);
            for (const Event &event : events)
            {
                writer.write(event);
            }
            return out.str();
        }

        std::vector<Event> decode(const std::string &bytes)
        {
            std::istringstream in(bytes, std::ios::binary);
            EventReader reader(in);
            std::vector<Event> events;
            Event event;
            while (reader.next(event))
            {
                events.push_back(event);
            }
            return events;
        }

    } // namespace

    TEST(EventFile, RoundTripGivesBackTheSameEvents)
    {
        const std::vector<Event> events = sampleEvents();
        EXPECT_EQ(decode(encode(events)), events);
    }

    TEST(EventFile, SizeIsHeaderPlusFixedRecords)
    {
        const std::vector<Event> events = sampleEvents();
        EXPECT_EQ(encode(events).size(), 8 + events.size() * EventRecordSize);
    }

    TEST(EventFile, EmptyStreamHasNoEvent)
    {
        EXPECT_TRUE(decode(encode({})).empty());
    }

    TEST(EventFile, RoundTripThroughAFile)
    {
        const std::vector<Event> events = sampleEvents();
        const std::filesystem::path path = std::filesystem::path(::testing::TempDir()) / "quantMicrostructure_events.qmev";
        writeEventFile(path, events);
        EXPECT_EQ(readEventFile(path), events);
        std::filesystem::remove(path);
    }

    TEST(EventFile, RejectsWhatIsNotAnEventFile)
    {
        EXPECT_THROW(decode(""), std::runtime_error);
        EXPECT_THROW(decode("not an event file"), std::runtime_error);

        std::string wrong_version = encode({});
        wrong_version[4] = 99;
        EXPECT_THROW(decode(wrong_version), std::runtime_error);
    }

    TEST(EventFile, RejectsATruncatedRecord)
    {
        std::string bytes = encode(sampleEvents());
        bytes.pop_back();
        EXPECT_THROW(decode(bytes), std::runtime_error);
    }

    TEST(EventFile, RejectsAnUnknownTypeOrSide)
    {
        const std::string bytes = encode(sampleEvents());

        std::string bad_type = bytes;
        bad_type[8 + 48] = 5;
        EXPECT_THROW(decode(bad_type), std::runtime_error);

        std::string bad_side = bytes;
        bad_side[8 + 49] = 2;
        EXPECT_THROW(decode(bad_side), std::runtime_error);
    }

    TEST(SequenceTracker, ConsecutiveNumbersAreInOrder)
    {
        SequenceTracker tracker;
        // The first number is whatever the stream starts at.
        EXPECT_EQ(tracker.observe(100), SequenceStatus::InOrder);
        EXPECT_EQ(tracker.observe(101), SequenceStatus::InOrder);
        EXPECT_EQ(tracker.observe(102), SequenceStatus::InOrder);
        EXPECT_EQ(tracker.gaps(), 0u);
        EXPECT_EQ(tracker.missing(), 0u);
    }

    TEST(SequenceTracker, CountsGapsAndMissingEvents)
    {
        SequenceTracker tracker;
        tracker.observe(1);
        EXPECT_EQ(tracker.observe(5), SequenceStatus::Gap);
        // It resumes from the number received.
        EXPECT_EQ(tracker.observe(6), SequenceStatus::InOrder);
        EXPECT_EQ(tracker.observe(8), SequenceStatus::Gap);
        EXPECT_EQ(tracker.gaps(), 2u);
        EXPECT_EQ(tracker.missing(), 4u);
    }

    TEST(SequenceTracker, OldNumbersAreStaleAndDoNotMoveIt)
    {
        SequenceTracker tracker;
        tracker.observe(10);
        tracker.observe(11);
        EXPECT_EQ(tracker.observe(11), SequenceStatus::Stale);
        EXPECT_EQ(tracker.observe(3), SequenceStatus::Stale);
        EXPECT_EQ(tracker.observe(12), SequenceStatus::InOrder);
        EXPECT_EQ(tracker.gaps(), 0u);
    }

} // namespace quantMicrostructure
