#include "quantMicrostructure/sim/zero_intelligence.hpp"

#include <stdexcept>

namespace quantMicrostructure
{

    namespace
    {

        constexpr double NanosPerSecond = 1e9;

    } // namespace

    ZeroIntelligenceSimulator::ZeroIntelligenceSimulator(const ZeroIntelligenceParams &params, std::uint64_t seed)
        : params_(params),
          rng_(seed),
          reference_bid_(params.initial_price),
          reference_ask_(params.initial_price + 1)
    {
        if (params.limit_rate < 0.0 || params.market_rate < 0.0 || params.cancel_rate < 0.0)
        {
            throw std::invalid_argument("ZeroIntelligenceSimulator: rates must not be negative");
        }
        if (params.order_size <= 0 || params.window <= 0)
        {
            throw std::invalid_argument("ZeroIntelligenceSimulator: order size and window must be positive");
        }
        if (params.limit_rate + params.market_rate <= 0.0)
        {
            throw std::invalid_argument("ZeroIntelligenceSimulator: no order would ever arrive");
        }
    }

    Timestamp ZeroIntelligenceSimulator::now() const noexcept
    {
        return static_cast<Timestamp>(time_ * NanosPerSecond);
    }

    double ZeroIntelligenceSimulator::totalRate() const noexcept
    {
        const double limits = 2.0 * params_.limit_rate * static_cast<double>(params_.window);
        const double markets = 2.0 * params_.market_rate;
        const double cancels = params_.cancel_rate * static_cast<double>(resting_.size());
        return limits + markets + cancels;
    }

    void ZeroIntelligenceSimulator::step(std::vector<Event> &out)
    {
        time_ += rng_.exponential(totalRate());
        act(out);
    }

    void ZeroIntelligenceSimulator::runUntil(Timestamp end, std::vector<Event> &out)
    {
        while (stepBefore(end, out))
        {
        }
    }

    bool ZeroIntelligenceSimulator::stepBefore(Timestamp end, std::vector<Event> &out)
    {
        const double end_seconds = static_cast<double>(end) / NanosPerSecond;
        const double next = time_ + rng_.exponential(totalRate());
        if (next > end_seconds)
        {
            // The waiting time is memoryless: dropping this draw and drawing
            // again later does not change the law of the flow.
            if (end_seconds > time_)
            {
                time_ = end_seconds;
            }
            return false;
        }
        time_ = next;
        act(out);
        return true;
    }

    SubmitResult ZeroIntelligenceSimulator::submitLimit(OrderId id, Side side, Price price, Quantity qty, std::vector<Event> &out)
    {
        if (id < FirstParticipantId)
        {
            throw std::invalid_argument("ZeroIntelligenceSimulator: a participant's order id must be at least FirstParticipantId");
        }
        const std::size_t first = out.size();
        const SubmitResult result = engine_.submitLimit(id, side, price, qty, now(), out);
        afterEvents(out, first);
        return result;
    }

    SubmitResult ZeroIntelligenceSimulator::submitMarket(Side side, Quantity qty, std::vector<Event> &out)
    {
        const std::size_t first = out.size();
        const SubmitResult result = engine_.submitMarket(side, qty, now(), out);
        afterEvents(out, first);
        return result;
    }

    Quantity ZeroIntelligenceSimulator::cancel(OrderId id, std::vector<Event> &out)
    {
        if (id < FirstParticipantId)
        {
            throw std::invalid_argument("ZeroIntelligenceSimulator: a participant can only cancel its own orders");
        }
        const Quantity cancelled = engine_.cancel(id, now(), out);
        afterEvents(out, out.size());
        return cancelled;
    }

    void ZeroIntelligenceSimulator::act(std::vector<Event> &out)
    {
        const double limit_side = params_.limit_rate * static_cast<double>(params_.window);
        const double draw = rng_.uniform() * totalRate();
        const Timestamp ts = now();

        if (draw < 2.0 * limit_side)
        {
            const Side side = draw < limit_side ? Side::Buy : Side::Sell;
            const Price offset = 1 + static_cast<Price>(rng_.below(static_cast<std::uint64_t>(params_.window)));
            const Price price = side == Side::Buy ? reference_ask_ - offset : reference_bid_ + offset;
            const OrderId id = next_id_++;
            engine_.submitLimit(id, side, price, params_.order_size, ts, out);
            position_.emplace(id, resting_.size());
            resting_.push_back(id);
        }
        else if (draw < 2.0 * (limit_side + params_.market_rate))
        {
            const Side side = draw < 2.0 * limit_side + params_.market_rate ? Side::Buy : Side::Sell;
            const std::size_t first = out.size();
            engine_.submitMarket(side, params_.order_size, ts, out);
            afterEvents(out, first);
            return;
        }
        else if (!resting_.empty())
        {
            const OrderId id = resting_[static_cast<std::size_t>(rng_.below(resting_.size()))];
            engine_.cancel(id, ts, out);
            forget(id);
        }
        afterEvents(out, out.size());
    }

    void ZeroIntelligenceSimulator::afterEvents(const std::vector<Event> &out, std::size_t first)
    {
        // Orders of the model that a trade used up are no longer there to be
        // cancelled.
        for (std::size_t i = first; i < out.size(); ++i)
        {
            if (out[i].type == EventType::Trade && !engine_.book().find(out[i].order_id))
            {
                forget(out[i].order_id);
            }
        }
        if (const auto bid = engine_.book().bestBid())
        {
            reference_bid_ = *bid;
        }
        if (const auto ask = engine_.book().bestAsk())
        {
            reference_ask_ = *ask;
        }
    }

    void ZeroIntelligenceSimulator::forget(OrderId id)
    {
        const auto slot = position_.find(id);
        if (slot == position_.end())
        {
            return;
        }
        const std::size_t at = slot->second;
        const OrderId moved = resting_.back();
        resting_[at] = moved;
        position_[moved] = at;
        resting_.pop_back();
        position_.erase(id);
    }

} // namespace quantMicrostructure
