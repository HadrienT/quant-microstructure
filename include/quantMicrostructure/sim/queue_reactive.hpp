#ifndef QUANTMICROSTRUCTURE_SIM_QUEUE_REACTIVE_HPP
#define QUANTMICROSTRUCTURE_SIM_QUEUE_REACTIVE_HPP

#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/core/types.hpp"
#include "quantMicrostructure/sim/rng.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace quantMicrostructure
{

    /// Intensities of the three flows at one queue, in events per second, as
    /// functions of the size of that queue: entry n is the intensity when the
    /// queue holds n units. The three tables have the same length; the queue
    /// cannot grow beyond the last entry, where `limit` must be zero, and
    /// nothing can leave an empty queue (`cancel[0]` and `market[0]` are zero).
    struct QueueIntensities
    {
        /// lambda^L(n): a limit order adds one unit.
        std::vector<double> limit;
        /// lambda^C(n): a cancellation removes one unit.
        std::vector<double> cancel;
        /// lambda^M(n): a market order trades one unit.
        std::vector<double> market;
    };

    /// Stationary distribution of the size of one queue on its own: entry n is
    /// the long-run share of time spent at size n. With rho(n) = lambda^L(n) /
    /// (lambda^C(n + 1) + lambda^M(n + 1)),
    ///
    ///     pi(n) = pi(0) * product over j = 1..n of rho(j - 1).
    ///
    /// Huang, Lehalle & Rosenbaum (2015), section 2.3.3. Throws
    /// std::invalid_argument if the tables are not as QueueIntensities
    /// requires, or if a size the queue can reach cannot be left downwards.
    std::vector<double> queueInvariantDistribution(const QueueIntensities &intensities);

    struct QueueReactiveParams
    {
        /// One entry per distance to the reference price, nearest first. The
        /// bid and the ask use the same tables.
        std::vector<QueueIntensities> queues;
        /// Probability that the reference price follows a move of the mid
        /// price. Zero is Model I of the article: a fixed reference price.
        double theta{};
        /// Probability that, when the reference price moves, every queue is
        /// redrawn from its stationary distribution.
        double theta_reinit{};
        /// Lots in one unit of queue size: the "average event size".
        Quantity event_size{1};
        /// The bid queues start at this price and below, the ask queues one
        /// tick above it and beyond.
        Price initial_reference{};
    };

    /// The queue-reactive model of Huang, Lehalle & Rosenbaum (2015)
    /// (docs/companion.md, section 6.2): the intensities of limit orders,
    /// cancellations and market orders at each queue depend on the size of
    /// that queue.
    ///
    /// Regime: LARGE TICK. The book is K queues on each side of a reference
    /// price; queue i of the ask is i ticks above the reference, queue i of
    /// the bid i - 1 ticks below it, so the spread is one tick when both
    /// nearest queues hold something.
    ///
    /// Between moves of the reference price the queues are independent birth
    /// and death processes (Model I, section 2.3.1). When the mid price moves
    /// up while the nearest ask queue is empty, the reference price goes up one
    /// tick with probability `theta`, and likewise downwards (Model III,
    /// section 3.1.1). The queues then shift by one tick: the queue that enters
    /// the window at the far end is drawn from its stationary distribution,
    /// the one that leaves it at the other end is dropped. With probability
    /// `theta_reinit` all the queues are redrawn instead.
    ///
    /// Where this departs from the article: one event size for all queues
    /// (the article has one per queue), and queue sizes capped at the length
    /// of the tables. As in the article, a market order can be sent to a queue
    /// that is not the nearest one if its table says so; give those a zero
    /// market intensity to keep price priority.
    ///
    /// The model knows quantities per price, not orders: the output is an
    /// AGGREGATED event stream (Level events, and a Trade before the Level it
    /// changes).
    class QueueReactiveSimulator
    {
      public:
        /// Throws std::invalid_argument if there is no queue, if a table is
        /// invalid, or if a probability is outside [0, 1].
        QueueReactiveSimulator(const QueueReactiveParams &params, std::uint64_t seed);

        /// Appends the events that publish the current book, which is drawn
        /// from the stationary distributions at construction. Call it once,
        /// before the first step, for the stream to be complete.
        void snapshot(std::vector<Event> &out);

        /// Draws the next event of the model and appends what it publishes.
        void step(std::vector<Event> &out);

        /// Runs until the time `end`; now() is `end` afterwards.
        void runUntil(Timestamp end, std::vector<Event> &out);

        /// Nanoseconds since the start of the run.
        Timestamp now() const noexcept;

        std::size_t depth() const noexcept { return params_.queues.size(); }
        /// Size, in units, of queue `i` (1 is the nearest) of a side.
        std::int64_t queueSize(Side side, std::size_t i) const { return sizes(side)[i - 1]; }
        Price queuePrice(Side side, std::size_t i) const noexcept;
        Price reference() const noexcept { return reference_; }
        /// Number of moves of the reference price so far.
        std::uint64_t referenceMoves() const noexcept { return reference_moves_; }

      private:
        const std::vector<std::int64_t> &sizes(Side side) const noexcept { return side == Side::Buy ? bid_ : ask_; }
        std::vector<std::int64_t> &sizes(Side side) noexcept { return side == Side::Buy ? bid_ : ask_; }
        double totalRate() const noexcept;
        void act(std::vector<Event> &out);
        void moveReference(Side emptied, std::vector<Event> &out);
        std::int64_t drawSize(std::size_t queue);
        void emitLevel(Side side, Price price, std::int64_t size, std::vector<Event> &out);
        void emit(Event event, std::vector<Event> &out);

        QueueReactiveParams params_;
        // Cumulative stationary distribution of each queue, to draw from it.
        std::vector<std::vector<double>> cumulative_;
        Rng rng_;
        std::vector<std::int64_t> bid_;
        std::vector<std::int64_t> ask_;
        Price reference_{};
        std::uint64_t reference_moves_{};
        SequenceNumber next_seq_{1};
        double time_{};
    };

} // namespace quantMicrostructure

#endif
