#ifndef QUANTMICROSTRUCTURE_SIM_ZERO_INTELLIGENCE_HPP
#define QUANTMICROSTRUCTURE_SIM_ZERO_INTELLIGENCE_HPP

#include "quantMicrostructure/book/order_book.hpp"
#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/core/types.hpp"
#include "quantMicrostructure/matching/matching_engine.hpp"
#include "quantMicrostructure/sim/rng.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace quantMicrostructure
{

    /// Rates of the zero-intelligence model, in orders per second. Every order
    /// has the same size.
    ///
    /// In the notation of Smith, Farmer, Gillemot & Krishnamurthy (2003),
    /// section I.C, where rates are in shares: `limit_rate` is alpha * dp /
    /// sigma, `market_rate` is mu / (2 sigma), `cancel_rate` is delta and
    /// `order_size` is sigma.
    struct ZeroIntelligenceParams
    {
        /// Limit orders per price level, on each side.
        double limit_rate{};
        /// Market orders on each side.
        double market_rate{};
        /// Cancellation of each resting order.
        double cancel_rate{};
        Quantity order_size{1};
        /// Number of price levels on which each side places its orders.
        Price window{};
        /// The book starts empty, around this price.
        Price initial_price{};
    };

    /// The zero-intelligence model of a limit order book (docs/companion.md,
    /// section 6.1; Smith, Farmer, Gillemot & Krishnamurthy 2003, section
    /// I.C): three independent Poisson flows, and no agent that decides
    /// anything.
    ///
    /// - A buy limit order arrives at each of the `window` prices below the
    ///   best ask at rate `limit_rate`; a sell limit order at each of the
    ///   `window` prices above the best bid. No limit order trades on arrival.
    /// - A market order arrives on each side at rate `market_rate`.
    /// - Each resting order is cancelled at rate `cancel_rate`.
    ///
    /// Where this departs from the article: orders are placed on a finite
    /// window instead of a semi-infinite interval, so the book can be empty.
    /// When a side is empty, the other side places its orders relative to the
    /// last best price that side had (`initial_price` and `initial_price` + 1
    /// before the first order).
    ///
    /// Regime: none assumed; the ratio `market_rate` / `limit_rate` sets the
    /// typical spread in ticks. What it does not have: any memory in the flow.
    /// Order signs are independent, so the price is more predictable than a
    /// real one.
    ///
    /// The orders go through a MatchingEngine: the output is an order-by-order
    /// event stream.
    class ZeroIntelligenceSimulator
    {
      public:
        /// Throws std::invalid_argument if a rate is negative, if nothing can
        /// ever happen, or if the size or the window is not positive.
        ZeroIntelligenceSimulator(const ZeroIntelligenceParams &params, std::uint64_t seed);

        /// Draws the next order and appends to `out` the events it causes
        /// (none for a market order that finds nobody).
        void step(std::vector<Event> &out);

        /// Runs until the time `end`: the events appended are those up to
        /// `end`, and now() is `end` afterwards.
        void runUntil(Timestamp end, std::vector<Event> &out);

        /// Draws the next order if it comes before `end`, appends its events
        /// and returns true. Otherwise moves the clock to `end` and returns
        /// false.
        bool stepBefore(Timestamp end, std::vector<Event> &out);

        // ---- An outside participant ----
        //
        // Orders sent at the current time by somebody who is not part of the
        // model (a strategy under test). They go through the same matching
        // engine as the simulated flow, which then reacts to them the only
        // way this model can: mechanically, through the book. The simulated
        // flow never cancels them. Their ids must be at least
        // FirstParticipantId, so that they cannot collide with the model's.

        static constexpr OrderId FirstParticipantId = OrderId{1} << 62;

        SubmitResult submitLimit(OrderId id, Side side, Price price, Quantity qty, std::vector<Event> &out);
        SubmitResult submitMarket(Side side, Quantity qty, std::vector<Event> &out);
        Quantity cancel(OrderId id, std::vector<Event> &out);

        /// Nanoseconds since the start of the run.
        Timestamp now() const noexcept;

        const OrderBook &book() const noexcept { return engine_.book(); }

      private:
        double totalRate() const noexcept;
        void act(std::vector<Event> &out);
        void forget(OrderId id);
        void afterEvents(const std::vector<Event> &out, std::size_t first);

        ZeroIntelligenceParams params_;
        Rng rng_;
        MatchingEngine engine_;
        // Resting orders, in no particular order, to cancel one uniformly.
        std::vector<OrderId> resting_;
        std::unordered_map<OrderId, std::size_t> position_;
        OrderId next_id_{1};
        Price reference_bid_{};
        Price reference_ask_{};
        double time_{};
    };

} // namespace quantMicrostructure

#endif
