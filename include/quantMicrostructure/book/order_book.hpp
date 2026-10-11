#ifndef QUANTMICROSTRUCTURE_BOOK_ORDER_BOOK_HPP
#define QUANTMICROSTRUCTURE_BOOK_ORDER_BOOK_HPP

#include "quantMicrostructure/book/book_level.hpp"
#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace quantMicrostructure
{

    /// A resting order, as the book shows it.
    struct OrderView
    {
        OrderId id{};
        Side side{};
        Price price{};
        Quantity qty{};

        friend bool operator==(const OrderView &, const OrderView &) = default;
    };

    /// An order-by-order (L3) book: every resting order, in price-time
    /// priority (docs/companion.md, section 2.2).
    ///
    /// It only stores. It does not match: a book fed with crossing orders is
    /// crossed. Matching is matching/matching_engine.hpp.
    ///
    /// Structure (blueprint/decisions.md, ADR-008): per side, an ordered map
    /// from price to level; per level, a doubly linked list of orders, oldest
    /// first; a hash map from order id to order. Adding, reducing and reading
    /// the best order are O(1), except for the O(log levels) of opening a new
    /// price level.
    class OrderBook
    {
      public:
        static constexpr std::size_t AllLevels = std::numeric_limits<std::size_t>::max();

        /// Puts a new order at the back of the queue of its price. Throws
        /// std::invalid_argument if `qty` is not positive or `id` is already
        /// resting.
        void add(OrderId id, Side side, Price price, Quantity qty);

        /// Takes `qty` off a resting order, which keeps its place in the queue,
        /// and removes it when nothing is left. Returns what is left. Throws
        /// std::invalid_argument if `id` is not resting or `qty` is not in
        /// (0, resting quantity].
        Quantity reduce(OrderId id, Quantity qty);

        void clear();

        /// Applies one event of an order-by-order stream. Throws
        /// std::invalid_argument on a Level event, which belongs to an
        /// aggregated stream.
        void apply(const Event &event);

        std::optional<Price> best(Side side) const;
        std::optional<Price> bestBid() const { return best(Side::Buy); }
        std::optional<Price> bestAsk() const { return best(Side::Sell); }

        /// The order that trades next on this side: the oldest at the best
        /// price.
        std::optional<OrderView> front(Side side) const;

        std::optional<OrderView> find(OrderId id) const;

        /// Total quantity resting at this price; zero if there is no such level.
        Quantity levelQuantity(Side side, Price price) const;

        std::size_t orderCount() const noexcept { return index_.size(); }
        bool empty() const noexcept { return index_.empty(); }

        /// The first `max_levels` levels of a side, best price first.
        std::vector<BookLevel> depth(Side side, std::size_t max_levels = AllLevels) const;

        /// Every order of a side, in the order in which it would trade.
        std::vector<OrderView> orders(Side side) const;

      private:
        static constexpr std::uint32_t Nil = std::numeric_limits<std::uint32_t>::max();

        struct Level
        {
            Quantity total{};
            std::uint32_t head{Nil};
            std::uint32_t tail{Nil};
        };

        // Both sides are sorted by increasing price: the best bid is the last
        // level of `bids_`, the best ask the first of `asks_`.
        using LevelMap = std::map<Price, Level>;

        // Orders live in `nodes_` and link to each other by index, so that the
        // vector can grow. Map iterators stay valid until their level is erased.
        struct Node
        {
            OrderId id{};
            Quantity qty{};
            LevelMap::iterator level{};
            std::uint32_t prev{Nil};
            std::uint32_t next{Nil};
            Side side{};
        };

        const LevelMap &levels(Side side) const noexcept { return side == Side::Buy ? bids_ : asks_; }
        LevelMap &levels(Side side) noexcept { return side == Side::Buy ? bids_ : asks_; }
        OrderView view(const Node &node) const;

        std::vector<Node> nodes_;
        std::vector<std::uint32_t> free_;
        std::unordered_map<OrderId, std::uint32_t> index_;
        LevelMap bids_;
        LevelMap asks_;
    };

} // namespace quantMicrostructure

#endif
