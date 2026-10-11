#include "quantMicrostructure/sim/queue_reactive.hpp"

#include <algorithm>
#include <stdexcept>

namespace quantMicrostructure
{

    namespace
    {

        constexpr double NanosPerSecond = 1e9;

    } // namespace

    std::vector<double> queueInvariantDistribution(const QueueIntensities &intensities)
    {
        const std::size_t states = intensities.limit.size();
        if (states == 0 || intensities.cancel.size() != states || intensities.market.size() != states)
        {
            throw std::invalid_argument("queue intensities: the three tables must have the same, non-zero length");
        }
        for (std::size_t n = 0; n < states; ++n)
        {
            if (intensities.limit[n] < 0.0 || intensities.cancel[n] < 0.0 || intensities.market[n] < 0.0)
            {
                throw std::invalid_argument("queue intensities: an intensity is negative");
            }
        }
        if (intensities.limit.back() != 0.0 || intensities.cancel.front() != 0.0 || intensities.market.front() != 0.0)
        {
            throw std::invalid_argument("queue intensities: the queue must not grow beyond its table nor shrink below zero");
        }

        std::vector<double> pi(states, 0.0);
        pi[0] = 1.0;
        double total = 1.0;
        for (std::size_t n = 1; n < states && intensities.limit[n - 1] > 0.0; ++n)
        {
            const double departure = intensities.cancel[n] + intensities.market[n];
            if (departure <= 0.0)
            {
                throw std::invalid_argument("queue intensities: a size that can be reached cannot be left");
            }
            pi[n] = pi[n - 1] * intensities.limit[n - 1] / departure;
            total += pi[n];
        }
        for (double &p : pi)
        {
            p /= total;
        }
        return pi;
    }

    QueueReactiveSimulator::QueueReactiveSimulator(const QueueReactiveParams &params, std::uint64_t seed)
        : params_(params),
          rng_(seed),
          reference_(params.initial_reference)
    {
        if (params.queues.empty())
        {
            throw std::invalid_argument("QueueReactiveSimulator: at least one queue per side is needed");
        }
        if (params.theta < 0.0 || params.theta > 1.0 || params.theta_reinit < 0.0 || params.theta_reinit > 1.0)
        {
            throw std::invalid_argument("QueueReactiveSimulator: theta and theta_reinit are probabilities");
        }
        if (params.event_size <= 0)
        {
            throw std::invalid_argument("QueueReactiveSimulator: event size must be positive");
        }
        for (const QueueIntensities &queue : params.queues)
        {
            std::vector<double> cumulative = queueInvariantDistribution(queue);
            for (std::size_t n = 1; n < cumulative.size(); ++n)
            {
                cumulative[n] += cumulative[n - 1];
            }
            cumulative_.push_back(std::move(cumulative));
        }
        for (std::size_t i = 0; i < depth(); ++i)
        {
            bid_.push_back(drawSize(i));
            ask_.push_back(drawSize(i));
        }
    }

    Timestamp QueueReactiveSimulator::now() const noexcept
    {
        return static_cast<Timestamp>(time_ * NanosPerSecond);
    }

    Price QueueReactiveSimulator::queuePrice(Side side, std::size_t i) const noexcept
    {
        const Price distance = static_cast<Price>(i);
        return side == Side::Buy ? reference_ - (distance - 1) : reference_ + distance;
    }

    void QueueReactiveSimulator::snapshot(std::vector<Event> &out)
    {
        Event clear;
        clear.type = EventType::Clear;
        emit(clear, out);
        for (const Side side : {Side::Buy, Side::Sell})
        {
            for (std::size_t i = 1; i <= depth(); ++i)
            {
                if (queueSize(side, i) > 0)
                {
                    emitLevel(side, queuePrice(side, i), queueSize(side, i), out);
                }
            }
        }
    }

    double QueueReactiveSimulator::totalRate() const noexcept
    {
        double total = 0.0;
        for (std::size_t i = 0; i < depth(); ++i)
        {
            const QueueIntensities &queue = params_.queues[i];
            for (const std::int64_t size : {bid_[i], ask_[i]})
            {
                const std::size_t n = static_cast<std::size_t>(size);
                total += queue.limit[n] + queue.cancel[n] + queue.market[n];
            }
        }
        return total;
    }

    void QueueReactiveSimulator::step(std::vector<Event> &out)
    {
        const double rate = totalRate();
        if (rate <= 0.0)
        {
            throw std::logic_error("QueueReactiveSimulator: no event can happen in this state");
        }
        time_ += rng_.exponential(rate);
        act(out);
    }

    void QueueReactiveSimulator::runUntil(Timestamp end, std::vector<Event> &out)
    {
        const double end_seconds = static_cast<double>(end) / NanosPerSecond;
        while (true)
        {
            const double rate = totalRate();
            if (rate <= 0.0)
            {
                throw std::logic_error("QueueReactiveSimulator: no event can happen in this state");
            }
            const double next = time_ + rng_.exponential(rate);
            if (next > end_seconds)
            {
                // Memoryless: the draw can be dropped.
                time_ = end_seconds;
                return;
            }
            time_ = next;
            act(out);
        }
    }

    void QueueReactiveSimulator::act(std::vector<Event> &out)
    {
        enum class Flow
        {
            Limit,
            Cancel,
            Market
        };

        // Walk the queues until the cumulated intensity passes the draw. If
        // rounding leaves the draw beyond the last one, the last possible
        // event is taken.
        double remaining = rng_.uniform() * totalRate();
        Side side = Side::Buy;
        std::size_t index = 0;
        Flow flow = Flow::Limit;
        bool found = false;
        for (std::size_t i = 0; i < depth() && !found; ++i)
        {
            const QueueIntensities &queue = params_.queues[i];
            for (const Side s : {Side::Buy, Side::Sell})
            {
                const std::size_t n = static_cast<std::size_t>(sizes(s)[i]);
                const double rates[] = {queue.limit[n], queue.cancel[n], queue.market[n]};
                const Flow flows[] = {Flow::Limit, Flow::Cancel, Flow::Market};
                for (std::size_t k = 0; k < 3 && !found; ++k)
                {
                    if (rates[k] <= 0.0)
                    {
                        continue;
                    }
                    side = s;
                    index = i;
                    flow = flows[k];
                    found = remaining < rates[k];
                    remaining -= rates[k];
                }
                if (found)
                {
                    break;
                }
            }
        }

        std::int64_t &size = sizes(side)[index];
        const bool was_empty = size == 0;
        const Price price = queuePrice(side, index + 1);
        if (flow == Flow::Limit)
        {
            ++size;
        }
        else
        {
            --size;
            if (flow == Flow::Market)
            {
                Event trade;
                trade.type = EventType::Trade;
                // The market order is on the side opposite to the queue it hits.
                trade.side = opposite(side);
                trade.price = price;
                trade.qty = params_.event_size;
                emit(trade, out);
            }
        }
        emitLevel(side, price, size, out);

        if (index != 0)
        {
            return;
        }
        // Section 3.1.1: the reference price follows a move of the mid price
        // with probability theta, provided the nearest queue on the side the
        // mid moves towards is empty. Two events of a nearest queue do that.
        const Side other = opposite(side);
        if (size == 0)
        {
            // Its last unit left: the mid moved towards this side, whose
            // nearest queue is now empty.
            if (rng_.bernoulli(params_.theta))
            {
                moveReference(side, out);
            }
        }
        else if (was_empty && sizes(other)[0] == 0)
        {
            // A limit order inside the spread: the mid moved towards the other
            // side, whose nearest queue is empty.
            if (rng_.bernoulli(params_.theta))
            {
                moveReference(other, out);
            }
        }
    }

    void QueueReactiveSimulator::moveReference(Side emptied, std::vector<Event> &out)
    {
        // The reference price moves one tick towards the side whose nearest
        // queue is empty: up if it is the ask, down if it is the bid. Every
        // quantity stays at its price; what changes is which queue each price
        // belongs to.
        const Side growing = opposite(emptied);
        std::vector<std::int64_t> &near = sizes(emptied);
        std::vector<std::int64_t> &far = sizes(growing);
        const std::size_t last = depth() - 1;

        // The price that leaves the window, at the far end of the other side.
        const Price leaving = queuePrice(growing, depth());
        const std::vector<std::int64_t> old_bid = bid_;
        const std::vector<std::int64_t> old_ask = ask_;
        const Price old_reference = reference_;

        reference_ += emptied == Side::Sell ? 1 : -1;
        ++reference_moves_;

        // On the emptied side every queue takes the size of the next one out,
        // and a new queue enters at the far end. On the other side every queue
        // moves one step out, and its nearest queue is the price that was the
        // emptied queue.
        for (std::size_t i = 0; i < last; ++i)
        {
            near[i] = near[i + 1];
        }
        near[last] = drawSize(last);
        for (std::size_t i = last; i > 0; --i)
        {
            far[i] = far[i - 1];
        }
        far[0] = 0;

        if (rng_.bernoulli(params_.theta_reinit))
        {
            for (std::size_t i = 0; i < depth(); ++i)
            {
                bid_[i] = drawSize(i);
                ask_[i] = drawSize(i);
            }
        }

        // Publish what changed, price by price: without a redraw, only the
        // two ends of the window. The one price that changes side is the
        // emptied queue, so the stream never shows a crossed book.
        const auto old_size = [&](Side s, Price price) -> std::int64_t
        {
            const Price distance = s == Side::Buy ? old_reference - price + 1 : price - old_reference;
            if (distance < 1 || distance > static_cast<Price>(depth()))
            {
                return 0;
            }
            return (s == Side::Buy ? old_bid : old_ask)[static_cast<std::size_t>(distance - 1)];
        };
        if (old_size(growing, leaving) > 0)
        {
            emitLevel(growing, leaving, 0, out);
        }
        for (const Side s : {Side::Buy, Side::Sell})
        {
            for (std::size_t i = 1; i <= depth(); ++i)
            {
                const Price price = queuePrice(s, i);
                if (queueSize(s, i) != old_size(s, price))
                {
                    emitLevel(s, price, queueSize(s, i), out);
                }
            }
        }
    }

    std::int64_t QueueReactiveSimulator::drawSize(std::size_t queue)
    {
        const std::vector<double> &cumulative = cumulative_[queue];
        const double u = rng_.uniform();
        const auto at = std::upper_bound(cumulative.begin(), cumulative.end(), u);
        const auto n = std::min<std::ptrdiff_t>(at - cumulative.begin(), static_cast<std::ptrdiff_t>(cumulative.size()) - 1);
        return static_cast<std::int64_t>(n);
    }

    void QueueReactiveSimulator::emitLevel(Side side, Price price, std::int64_t size, std::vector<Event> &out)
    {
        Event level;
        level.type = EventType::Level;
        level.side = side;
        level.price = price;
        level.qty = size * params_.event_size;
        emit(level, out);
    }

    void QueueReactiveSimulator::emit(Event event, std::vector<Event> &out)
    {
        event.ts = now();
        event.ts_recv = event.ts;
        event.seq = next_seq_++;
        out.push_back(event);
    }

} // namespace quantMicrostructure
