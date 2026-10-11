#include <gtest/gtest.h>

#include "quantMicrostructure/book/level_aggregator.hpp"
#include "quantMicrostructure/book/level_book.hpp"
#include "quantMicrostructure/book/order_book.hpp"
#include "quantMicrostructure/core/event.hpp"

#include <stdexcept>
#include <vector>

namespace quantMicrostructure
{

    namespace
    {

        Event makeEvent(EventType type, OrderId id, Side side, Price price, Quantity qty)
        {
            Event e;
            e.type = type;
            e.order_id = id;
            e.side = side;
            e.price = price;
            e.qty = qty;
            return e;
        }

    } // namespace

    TEST(OrderBook, EmptyBookHasNoBest)
    {
        const OrderBook book;
        EXPECT_TRUE(book.empty());
        EXPECT_FALSE(book.bestBid());
        EXPECT_FALSE(book.bestAsk());
        EXPECT_FALSE(book.front(Side::Buy));
        EXPECT_TRUE(book.depth(Side::Sell).empty());
    }

    TEST(OrderBook, BestBidIsTheHighestBestAskTheLowest)
    {
        OrderBook book;
        book.add(1, Side::Buy, 99, 5);
        book.add(2, Side::Buy, 100, 3);
        book.add(3, Side::Buy, 98, 1);
        book.add(4, Side::Sell, 103, 2);
        book.add(5, Side::Sell, 102, 4);

        EXPECT_EQ(book.bestBid(), 100);
        EXPECT_EQ(book.bestAsk(), 102);
        EXPECT_EQ(book.depth(Side::Buy), (std::vector<BookLevel>{{100, 3}, {99, 5}, {98, 1}}));
        EXPECT_EQ(book.depth(Side::Sell), (std::vector<BookLevel>{{102, 4}, {103, 2}}));
        EXPECT_EQ(book.depth(Side::Buy, 2), (std::vector<BookLevel>{{100, 3}, {99, 5}}));
        EXPECT_EQ(book.orderCount(), 5u);
    }

    TEST(OrderBook, SamePriceQueuesInArrivalOrder)
    {
        OrderBook book;
        book.add(7, Side::Sell, 101, 2);
        book.add(3, Side::Sell, 101, 5);
        book.add(9, Side::Sell, 101, 1);

        EXPECT_EQ(book.levelQuantity(Side::Sell, 101), 8);
        EXPECT_EQ(book.front(Side::Sell), (OrderView{7, Side::Sell, 101, 2}));
        EXPECT_EQ(book.orders(Side::Sell), (std::vector<OrderView>{{7, Side::Sell, 101, 2}, {3, Side::Sell, 101, 5}, {9, Side::Sell, 101, 1}}));
    }

    TEST(OrderBook, PartialReduceKeepsThePlaceInTheQueue)
    {
        OrderBook book;
        book.add(1, Side::Buy, 100, 10);
        book.add(2, Side::Buy, 100, 10);

        EXPECT_EQ(book.reduce(1, 4), 6);
        EXPECT_EQ(book.front(Side::Buy), (OrderView{1, Side::Buy, 100, 6}));
        EXPECT_EQ(book.levelQuantity(Side::Buy, 100), 16);
    }

    TEST(OrderBook, FullReduceRemovesTheOrderAndAnEmptyLevel)
    {
        OrderBook book;
        book.add(1, Side::Buy, 100, 10);
        book.add(2, Side::Buy, 100, 4);
        book.add(3, Side::Buy, 99, 1);

        // The head of a two-order queue: the second becomes the front.
        EXPECT_EQ(book.reduce(1, 10), 0);
        EXPECT_FALSE(book.find(1));
        EXPECT_EQ(book.front(Side::Buy), (OrderView{2, Side::Buy, 100, 4}));

        EXPECT_EQ(book.reduce(2, 4), 0);
        EXPECT_EQ(book.bestBid(), 99);
        EXPECT_EQ(book.levelQuantity(Side::Buy, 100), 0);
        EXPECT_EQ(book.orderCount(), 1u);
    }

    TEST(OrderBook, RemovingFromTheMiddleKeepsTheOthersInOrder)
    {
        OrderBook book;
        book.add(1, Side::Sell, 101, 1);
        book.add(2, Side::Sell, 101, 2);
        book.add(3, Side::Sell, 101, 3);
        book.reduce(2, 2);
        // A later order reuses the freed slot and still goes to the back.
        book.add(4, Side::Sell, 101, 4);

        EXPECT_EQ(book.orders(Side::Sell), (std::vector<OrderView>{{1, Side::Sell, 101, 1}, {3, Side::Sell, 101, 3}, {4, Side::Sell, 101, 4}}));
    }

    TEST(OrderBook, RejectsInvalidInput)
    {
        OrderBook book;
        book.add(1, Side::Buy, 100, 10);

        EXPECT_THROW(book.add(1, Side::Sell, 105, 1), std::invalid_argument);
        EXPECT_THROW(book.add(2, Side::Buy, 100, 0), std::invalid_argument);
        EXPECT_THROW(book.add(2, Side::Buy, 100, -1), std::invalid_argument);
        EXPECT_THROW(book.reduce(42, 1), std::invalid_argument);
        EXPECT_THROW(book.reduce(1, 0), std::invalid_argument);
        EXPECT_THROW(book.reduce(1, 11), std::invalid_argument);

        // A rejected call leaves the book as it was.
        EXPECT_EQ(book.orders(Side::Buy), (std::vector<OrderView>{{1, Side::Buy, 100, 10}}));
        EXPECT_FALSE(book.find(2));
    }

    TEST(OrderBook, AppliesAnOrderByOrderStream)
    {
        OrderBook book;
        book.apply(makeEvent(EventType::Add, 1, Side::Sell, 101, 10));
        book.apply(makeEvent(EventType::Add, 2, Side::Buy, 99, 5));
        // A buyer takes 4 from order 1: the event carries the aggressor's side.
        book.apply(makeEvent(EventType::Trade, 1, Side::Buy, 101, 4));
        book.apply(makeEvent(EventType::Cancel, 2, Side::Buy, 99, 5));

        EXPECT_EQ(book.orders(Side::Sell), (std::vector<OrderView>{{1, Side::Sell, 101, 6}}));
        EXPECT_FALSE(book.bestBid());

        EXPECT_THROW(book.apply(makeEvent(EventType::Level, 0, Side::Buy, 99, 5)), std::invalid_argument);

        book.apply(makeEvent(EventType::Clear, 0, Side::Buy, 0, 0));
        EXPECT_TRUE(book.empty());
        EXPECT_FALSE(book.bestAsk());
    }

    TEST(LevelBook, SetReplacesAndZeroRemoves)
    {
        LevelBook book;
        book.set(Side::Buy, 100, 5);
        book.set(Side::Buy, 99, 7);
        book.set(Side::Sell, 102, 3);
        book.set(Side::Buy, 100, 2);

        EXPECT_EQ(book.bestBid(), 100);
        EXPECT_EQ(book.bestAsk(), 102);
        EXPECT_EQ(book.depth(Side::Buy), (std::vector<BookLevel>{{100, 2}, {99, 7}}));

        book.set(Side::Buy, 100, 0);
        EXPECT_EQ(book.bestBid(), 99);
        EXPECT_EQ(book.levelQuantity(Side::Buy, 100), 0);
        EXPECT_THROW(book.set(Side::Buy, 100, -1), std::invalid_argument);
    }

    TEST(LevelBook, AppliesAnAggregatedStream)
    {
        LevelBook book;
        book.apply(makeEvent(EventType::Level, 0, Side::Sell, 101, 10));
        // A trade alone does not change the book.
        book.apply(makeEvent(EventType::Trade, 1, Side::Buy, 101, 4));
        EXPECT_EQ(book.levelQuantity(Side::Sell, 101), 10);
        book.apply(makeEvent(EventType::Level, 0, Side::Sell, 101, 6));
        EXPECT_EQ(book.levelQuantity(Side::Sell, 101), 6);

        EXPECT_THROW(book.apply(makeEvent(EventType::Add, 1, Side::Buy, 99, 5)), std::invalid_argument);
        EXPECT_THROW(book.apply(makeEvent(EventType::Cancel, 1, Side::Buy, 99, 5)), std::invalid_argument);

        book.apply(makeEvent(EventType::Clear, 0, Side::Buy, 0, 0));
        EXPECT_FALSE(book.bestAsk());
    }

    TEST(LevelAggregator, TurnsOrderEventsIntoLevelEvents)
    {
        LevelAggregator aggregator;
        std::vector<Event> out;

        aggregator.apply(makeEvent(EventType::Add, 1, Side::Sell, 101, 10), out);
        aggregator.apply(makeEvent(EventType::Add, 2, Side::Sell, 101, 5), out);
        aggregator.apply(makeEvent(EventType::Trade, 1, Side::Buy, 101, 10), out);
        aggregator.apply(makeEvent(EventType::Cancel, 2, Side::Sell, 101, 5), out);

        ASSERT_EQ(out.size(), 5u);
        EXPECT_EQ(out[0].type, EventType::Level);
        EXPECT_EQ(out[0].qty, 10);
        EXPECT_EQ(out[1].qty, 15);
        // The trade is passed on, then the level it changed: that of the
        // resting order, on the side opposite to the aggressor.
        EXPECT_EQ(out[2].type, EventType::Trade);
        EXPECT_EQ(out[2].side, Side::Buy);
        EXPECT_EQ(out[3].type, EventType::Level);
        EXPECT_EQ(out[3].side, Side::Sell);
        EXPECT_EQ(out[3].price, 101);
        EXPECT_EQ(out[3].qty, 5);
        EXPECT_EQ(out[4].qty, 0);
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            EXPECT_EQ(out[i].seq, i + 1);
        }

        EXPECT_THROW(aggregator.apply(makeEvent(EventType::Cancel, 99, Side::Sell, 101, 5), out), std::invalid_argument);
        EXPECT_THROW(aggregator.apply(makeEvent(EventType::Level, 0, Side::Sell, 101, 5), out), std::invalid_argument);
    }

} // namespace quantMicrostructure
