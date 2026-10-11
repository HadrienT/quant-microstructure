#ifndef QUANTMICROSTRUCTURE_TESTS_SUPPORT_RANDOM_FLOW_HPP
#define QUANTMICROSTRUCTURE_TESTS_SUPPORT_RANDOM_FLOW_HPP

#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/core/types.hpp"
#include "quantMicrostructure/matching/matching_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace quantMicrostructure::testing
{

    enum class CommandKind : std::uint8_t
    {
        Limit,
        Market,
        Cancel
    };

    /// One message sent to a matching engine. `id` identifies the order in the
    /// flow; the engine itself does not know the id of a market order.
    struct Command
    {
        CommandKind kind{};
        OrderId id{};
        Side side{};
        Price price{};
        Quantity qty{};
    };

    /// A random flow of orders around a fixed price, with no model behind it:
    /// it is there to exercise the book, not to look like a market.
    ///
    /// 5 % of the commands are market orders of 1 to 30 lots. Limit orders are
    /// of 1 to 10 lots, at a price drawn uniformly from 20 ticks behind the
    /// centre to 4 ticks through it, so that some of them trade on arrival.
    /// Cancels pick at random among the limit orders sent and not yet
    /// cancelled, some of which were filled in the meantime. They become more
    /// frequent once more than `target_orders` such orders are outstanding,
    /// which keeps the book around that size.
    ///
    /// The same arguments give the same flow on every platform: std::mt19937_64
    /// is specified by the standard, and no standard distribution is used.
    inline std::vector<Command> makeFlow(std::size_t count, std::uint64_t seed, std::size_t target_orders)
    {
        constexpr Price Centre = 10'000;

        std::mt19937_64 rng(seed);
        const auto below = [&rng](std::uint64_t bound)
        { return rng() % bound; };

        std::vector<Command> flow;
        flow.reserve(count);
        std::vector<OrderId> outstanding;
        OrderId next_id = 1;

        while (flow.size() < count)
        {
            Command c;
            c.side = below(2) == 0 ? Side::Buy : Side::Sell;
            const std::uint64_t draw = below(100);
            const std::uint64_t cancel_share = outstanding.size() > target_orders ? 55 : 25;
            if (draw < 5)
            {
                c.kind = CommandKind::Market;
                c.id = next_id++;
                c.qty = 1 + static_cast<Quantity>(below(30));
            }
            else if (draw < 5 + cancel_share && !outstanding.empty())
            {
                const std::size_t pick = static_cast<std::size_t>(below(outstanding.size()));
                c.kind = CommandKind::Cancel;
                c.id = outstanding[pick];
                std::swap(outstanding[pick], outstanding.back());
                outstanding.pop_back();
            }
            else
            {
                const Price offset = static_cast<Price>(below(25)) - 20;
                c.kind = CommandKind::Limit;
                c.id = next_id++;
                c.price = c.side == Side::Buy ? Centre + offset : Centre - offset;
                c.qty = 1 + static_cast<Quantity>(below(10));
                outstanding.push_back(c.id);
            }
            flow.push_back(c);
        }
        return flow;
    }

    /// Sends one command to the engine. A cancel reports nothing here: what it
    /// cancelled is in the event it appends.
    inline SubmitResult send(MatchingEngine &engine, const Command &c, Timestamp ts, std::vector<Event> &out)
    {
        switch (c.kind)
        {
            case CommandKind::Limit:
                return engine.submitLimit(c.id, c.side, c.price, c.qty, ts, out);
            case CommandKind::Market:
                return engine.submitMarket(c.side, c.qty, ts, out);
            case CommandKind::Cancel:
                engine.cancel(c.id, ts, out);
                break;
        }
        return {};
    }

} // namespace quantMicrostructure::testing

#endif
