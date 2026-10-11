#ifndef QUANTMICROSTRUCTURE_REPLAY_BACKTEST_HPP
#define QUANTMICROSTRUCTURE_REPLAY_BACKTEST_HPP

#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/core/types.hpp"
#include "quantMicrostructure/measures/tape.hpp"
#include "quantMicrostructure/sim/zero_intelligence.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// A book-level backtest (docs/companion.md, section 10). One strategy
// interface, two places to run it:
//
// - runReplay: against a recorded stream. The data are real, but the other
//   participants do not react to our orders.
// - runSimulation: inside a simulator. The flow reacts, but it is a model.
//
// The two results are to be read together. Every assumption is in
// BacktestConfig and comes back in BacktestResult.

namespace quantMicrostructure
{

    /// What a replay assumes of the orders that were ahead of ours in the
    /// queue when a level of an AGGREGATED stream shrinks without a trade. It
    /// is not used for an order-by-order stream, where the queue is known, nor
    /// in a simulation, where the matching engine holds it.
    enum class QueueModel : std::uint8_t
    {
        /// Cancellations come from behind us as long as somebody is there: the
        /// case that fills us the latest.
        Pessimistic,
        /// Cancellations are spread evenly over the queue: ahead of us in
        /// proportion to the share of the queue that is ahead of us.
        ProRata
    };

    /// Fees as a fraction of the traded notional (price times quantity), per
    /// side of the trade. Negative is a rebate.
    struct Fees
    {
        /// When our resting order is hit.
        double maker{};
        /// When our order trades on arrival.
        double taker{};
    };

    struct BacktestConfig
    {
        /// Time between a decision and the arrival of the order at the venue,
        /// in nanoseconds. The strategy sees the market without delay: the
        /// whole round trip is charged here.
        Timestamp latency{};
        QueueModel queue_model{QueueModel::Pessimistic};
        Fees fees;
    };

    struct Fill
    {
        Timestamp ts{};
        /// Id of our order, as the gateway returned it; 0 for a market order.
        OrderId order{};
        Side side{};
        Price price{};
        Quantity qty{};
        /// True if our order was resting.
        bool maker{false};

        friend bool operator==(const Fill &, const Fill &) = default;
    };

    /// What a strategy sees: the best prices, and its own position.
    struct MarketView
    {
        Timestamp now{};
        Quote quote;
        /// Lots held: positive long, negative short.
        Quantity position{};
    };

    /// Where a strategy sends its orders. They take effect `latency` later.
    class Gateway
    {
      public:
        virtual ~Gateway() = default;

        /// Returns the id to cancel the order with, and found in its fills.
        /// There is no post-only order: a limit order that crosses the book
        /// when it arrives trades as a taker, which latency makes possible
        /// even for a strategy that only means to quote.
        virtual OrderId submitLimit(Side side, Price price, Quantity qty) = 0;
        virtual void submitMarket(Side side, Quantity qty) = 0;
        /// Does nothing if the order is no longer resting when it arrives.
        virtual void cancel(OrderId id) = 0;
    };

    /// A strategy knows nothing of where it runs (principle 3).
    class Strategy
    {
      public:
        virtual ~Strategy() = default;

        /// The market changed. Not called for the effect of our own orders,
        /// which onFill reports.
        virtual void onMarket(const MarketView &view, Gateway &gateway) = 0;
        /// One of our orders traded. The view shows the book once the trade
        /// that filled it has been applied.
        virtual void onFill(const Fill &fill, const MarketView &view, Gateway &gateway);
    };

    struct BacktestResult
    {
        std::vector<Fill> fills;
        Quantity position{};
        /// Cash from the trades, in ticks times lots, before fees.
        double cash{};
        double fees{};
        /// Last mid price seen, at which the position is valued.
        double mark{};
        /// cash + position * mark - fees.
        double pnl{};
        std::size_t orders_sent{};
        std::size_t cancels_sent{};
        /// The assumptions the figures above rest on.
        BacktestConfig config;
        /// True for a simulation, where the flow reacted to our orders.
        bool market_reacts{false};
    };

    /// Runs a strategy against a recorded stream.
    ///
    /// What is assumed, beyond BacktestConfig:
    /// - Nobody reacts to our orders, and they leave the recorded book as it
    ///   is: an order that trades on arrival takes the displayed quantities,
    ///   best price first, and those quantities are there again for the next
    ///   one.
    /// - A resting order joins the back of the queue: everything displayed at
    ///   its price when it arrives is ahead of it. It is filled when recorded
    ///   aggressive volume reaches it: beyond the queue ahead at its price, or
    ///   at once if the recorded trade is at a worse price than ours.
    /// - In an order-by-order stream the queue is exact: a recorded order is
    ///   ahead of ours if it was added before ours arrived. A recorded trade
    ///   against an order behind ours is given to ours instead.
    /// - After a Clear event (a gap in the feed, then a snapshot) our resting
    ///   orders lose their place: they go behind what the snapshot shows at
    ///   their price.
    /// - Actions that would arrive after the last event are dropped.
    ///
    /// No future (principle 6): a decision or a fill at time t depends only on
    /// the events up to t.
    ///
    /// Throws std::invalid_argument for a trades-only stream.
    BacktestResult runReplay(std::span<const Event> events, StreamKind kind, Strategy &strategy, const BacktestConfig &config);

    /// Runs a strategy inside a zero-intelligence simulator until `end`. Our
    /// orders go through its matching engine: queue position is exact, our
    /// aggressive orders remove liquidity for everybody, and the simulated
    /// market orders hit ours. BacktestConfig::queue_model is not used.
    ///
    /// If `record` is given, every event of the run is appended to it, ours
    /// included: the stream of the market as it was with us in it.
    BacktestResult runSimulation(ZeroIntelligenceSimulator &simulator, Timestamp end, Strategy &strategy, const BacktestConfig &config, std::vector<Event> *record = nullptr);

} // namespace quantMicrostructure

#endif
