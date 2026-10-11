#include <gtest/gtest.h>

#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/matching/matching_engine.hpp"

#include <stdexcept>
#include <vector>

namespace quantMicrostructure
{

    TEST(MatchingEngine, LimitOrderThatDoesNotCrossRests)
    {
        MatchingEngine engine;
        std::vector<Event> out;

        const SubmitResult r = engine.submitLimit(1, Side::Buy, 100, 10, 5, out);

        EXPECT_EQ(r.filled, 0);
        EXPECT_EQ(r.resting, 10);
        ASSERT_EQ(out.size(), 1u);
        EXPECT_EQ(out[0].type, EventType::Add);
        EXPECT_EQ(out[0].order_id, 1u);
        EXPECT_EQ(out[0].side, Side::Buy);
        EXPECT_EQ(out[0].price, 100);
        EXPECT_EQ(out[0].qty, 10);
        EXPECT_EQ(out[0].ts, 5);
        EXPECT_EQ(out[0].ts_recv, 5);
        EXPECT_EQ(out[0].seq, 1u);
        EXPECT_EQ(engine.book().bestBid(), 100);
    }

    TEST(MatchingEngine, TradeIsAtThePriceOfTheRestingOrder)
    {
        MatchingEngine engine;
        std::vector<Event> out;
        engine.submitLimit(1, Side::Sell, 101, 10, 0, out);
        out.clear();

        // The buyer accepts up to 105 and pays 101.
        const SubmitResult r = engine.submitLimit(2, Side::Buy, 105, 4, 1, out);

        EXPECT_EQ(r.filled, 4);
        EXPECT_EQ(r.resting, 0);
        ASSERT_EQ(out.size(), 1u);
        EXPECT_EQ(out[0].type, EventType::Trade);
        EXPECT_EQ(out[0].order_id, 1u);
        EXPECT_EQ(out[0].side, Side::Buy);
        EXPECT_EQ(out[0].price, 101);
        EXPECT_EQ(out[0].qty, 4);
        EXPECT_EQ(engine.book().find(1)->qty, 6);
        EXPECT_FALSE(engine.book().find(2));
    }

    TEST(MatchingEngine, LimitOrderRestsForWhatItCouldNotTrade)
    {
        MatchingEngine engine;
        std::vector<Event> out;
        engine.submitLimit(1, Side::Sell, 101, 3, 0, out);
        engine.submitLimit(2, Side::Sell, 103, 5, 0, out);
        out.clear();

        // 102 reaches the first level only.
        const SubmitResult r = engine.submitLimit(3, Side::Buy, 102, 10, 1, out);

        EXPECT_EQ(r.filled, 3);
        EXPECT_EQ(r.resting, 7);
        ASSERT_EQ(out.size(), 2u);
        EXPECT_EQ(out[0].type, EventType::Trade);
        EXPECT_EQ(out[1].type, EventType::Add);
        EXPECT_EQ(out[1].qty, 7);
        EXPECT_EQ(engine.book().bestBid(), 102);
        EXPECT_EQ(engine.book().bestAsk(), 103);
    }

    TEST(MatchingEngine, BestPriceFirstThenOldestFirst)
    {
        MatchingEngine engine;
        std::vector<Event> out;
        engine.submitLimit(1, Side::Sell, 102, 5, 0, out);
        engine.submitLimit(2, Side::Sell, 101, 5, 1, out);
        engine.submitLimit(3, Side::Sell, 101, 5, 2, out);
        out.clear();

        const SubmitResult r = engine.submitMarket(Side::Buy, 12, 3, out);

        EXPECT_EQ(r.filled, 12);
        ASSERT_EQ(out.size(), 3u);
        EXPECT_EQ(out[0].order_id, 2u);
        EXPECT_EQ(out[0].price, 101);
        EXPECT_EQ(out[0].qty, 5);
        EXPECT_EQ(out[1].order_id, 3u);
        EXPECT_EQ(out[1].price, 101);
        EXPECT_EQ(out[1].qty, 5);
        EXPECT_EQ(out[2].order_id, 1u);
        EXPECT_EQ(out[2].price, 102);
        EXPECT_EQ(out[2].qty, 2);
        EXPECT_EQ(engine.book().find(1)->qty, 3);
    }

    TEST(MatchingEngine, MarketOrderNeverRests)
    {
        MatchingEngine engine;
        std::vector<Event> out;

        // Nobody to trade with.
        SubmitResult r = engine.submitMarket(Side::Sell, 5, 0, out);
        EXPECT_EQ(r.filled, 0);
        EXPECT_EQ(r.resting, 0);
        EXPECT_TRUE(out.empty());

        // More than the book holds: the rest is dropped.
        engine.submitLimit(1, Side::Buy, 100, 3, 1, out);
        out.clear();
        r = engine.submitMarket(Side::Sell, 5, 2, out);
        EXPECT_EQ(r.filled, 3);
        EXPECT_EQ(r.resting, 0);
        EXPECT_EQ(out.size(), 1u);
        EXPECT_TRUE(engine.book().empty());
    }

    TEST(MatchingEngine, CancelRemovesWhatIsLeft)
    {
        MatchingEngine engine;
        std::vector<Event> out;
        engine.submitLimit(1, Side::Buy, 100, 10, 0, out);
        engine.submitMarket(Side::Sell, 4, 1, out);
        out.clear();

        EXPECT_EQ(engine.cancel(1, 2, out), 6);

        ASSERT_EQ(out.size(), 1u);
        EXPECT_EQ(out[0].type, EventType::Cancel);
        EXPECT_EQ(out[0].order_id, 1u);
        EXPECT_EQ(out[0].side, Side::Buy);
        EXPECT_EQ(out[0].price, 100);
        EXPECT_EQ(out[0].qty, 6);
        EXPECT_TRUE(engine.book().empty());
    }

    TEST(MatchingEngine, CancelOfAnOrderThatIsGoneDoesNothing)
    {
        MatchingEngine engine;
        std::vector<Event> out;
        engine.submitLimit(1, Side::Buy, 100, 10, 0, out);
        engine.submitMarket(Side::Sell, 10, 1, out);
        out.clear();

        EXPECT_EQ(engine.cancel(1, 2, out), 0);
        EXPECT_EQ(engine.cancel(42, 2, out), 0);
        EXPECT_TRUE(out.empty());
    }

    TEST(MatchingEngine, RejectsInvalidOrdersBeforeAnythingHappens)
    {
        MatchingEngine engine;
        std::vector<Event> out;
        engine.submitLimit(1, Side::Sell, 101, 10, 0, out);
        out.clear();

        EXPECT_THROW(engine.submitLimit(2, Side::Buy, 100, 0, 1, out), std::invalid_argument);
        EXPECT_THROW(engine.submitMarket(Side::Buy, -1, 1, out), std::invalid_argument);
        // A crossing order with an id already resting must not trade first.
        EXPECT_THROW(engine.submitLimit(1, Side::Buy, 105, 5, 1, out), std::invalid_argument);

        EXPECT_TRUE(out.empty());
        EXPECT_EQ(engine.book().find(1)->qty, 10);
    }

    TEST(MatchingEngine, SequenceNumbersAreConsecutiveAcrossCalls)
    {
        MatchingEngine engine;
        std::vector<Event> out;
        engine.submitLimit(1, Side::Sell, 101, 10, 0, out);
        engine.submitLimit(2, Side::Buy, 101, 4, 1, out);
        engine.cancel(1, 2, out);

        ASSERT_EQ(out.size(), 3u);
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            EXPECT_EQ(out[i].seq, i + 1);
        }
    }

} // namespace quantMicrostructure
