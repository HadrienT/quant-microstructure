#include "quantMicrostructure/sim/toy_models.hpp"

#include <stdexcept>

namespace quantMicrostructure
{

    std::vector<Event> simulateRoll(const RollParams &params, std::size_t trades, Rng &rng)
    {
        if (params.half_spread < 0 || params.step < 0 || params.interval <= 0)
        {
            throw std::invalid_argument("simulateRoll: half spread and step must not be negative, interval must be positive");
        }
        std::vector<Event> events;
        events.reserve(trades);
        Price efficient = params.initial_price;
        for (std::size_t i = 0; i < trades; ++i)
        {
            if (i > 0)
            {
                efficient += rng.below(2) == 0 ? params.step : -params.step;
            }
            Event trade;
            trade.type = EventType::Trade;
            trade.side = rng.below(2) == 0 ? Side::Buy : Side::Sell;
            trade.price = efficient + (trade.side == Side::Buy ? params.half_spread : -params.half_spread);
            trade.qty = 1;
            trade.seq = i + 1;
            trade.ts = static_cast<Timestamp>(i) * params.interval;
            trade.ts_recv = trade.ts;
            events.push_back(trade);
        }
        return events;
    }

    KyleEquilibrium kyleEquilibrium(const KyleParams &params)
    {
        if (!(params.sigma_v > 0.0) || !(params.sigma_u > 0.0))
        {
            throw std::invalid_argument("kyleEquilibrium: both standard deviations must be positive");
        }
        return KyleEquilibrium{params.sigma_v / (2.0 * params.sigma_u), params.sigma_u / params.sigma_v};
    }

    std::vector<KyleAuction> simulateKyle(const KyleParams &params, std::size_t auctions, Rng &rng)
    {
        const KyleEquilibrium equilibrium = kyleEquilibrium(params);
        std::vector<KyleAuction> out;
        out.reserve(auctions);
        for (std::size_t i = 0; i < auctions; ++i)
        {
            KyleAuction auction;
            auction.value = params.p0 + params.sigma_v * rng.normal();
            auction.noise_order = params.sigma_u * rng.normal();
            auction.insider_order = equilibrium.beta * (auction.value - params.p0);
            auction.price = params.p0 + equilibrium.lambda * (auction.insider_order + auction.noise_order);
            out.push_back(auction);
        }
        return out;
    }

    GlostenMilgromQuotes glostenMilgromQuotes(const GlostenMilgromParams &params, double belief)
    {
        if (!(params.v_low < params.v_high) || params.informed_share < 0.0 || params.informed_share > 1.0)
        {
            throw std::invalid_argument("glostenMilgromQuotes: need v_low < v_high and an informed share in [0, 1]");
        }
        if (belief < 0.0 || belief > 1.0)
        {
            throw std::invalid_argument("glostenMilgromQuotes: a belief is a probability");
        }
        const double pi = params.informed_share;
        const double range = params.v_high - params.v_low;
        // Probability of each trade under the current belief. When one is
        // impossible (everybody is informed and the belief is certain), the
        // belief is left where it is.
        const double buy = belief * (1.0 + pi) + (1.0 - belief) * (1.0 - pi);
        const double sell = belief * (1.0 - pi) + (1.0 - belief) * (1.0 + pi);

        GlostenMilgromQuotes quotes;
        quotes.belief_after_buy = buy > 0.0 ? belief * (1.0 + pi) / buy : belief;
        quotes.belief_after_sell = sell > 0.0 ? belief * (1.0 - pi) / sell : belief;
        quotes.ask = params.v_low + quotes.belief_after_buy * range;
        quotes.bid = params.v_low + quotes.belief_after_sell * range;
        return quotes;
    }

    GlostenMilgromPath simulateGlostenMilgrom(const GlostenMilgromParams &params, std::size_t trades, Rng &rng)
    {
        double belief = params.prior_high;
        // Validates the parameters before anything is drawn.
        glostenMilgromQuotes(params, belief);

        GlostenMilgromPath path;
        path.value_is_high = rng.bernoulli(params.prior_high);
        path.trades.reserve(trades);
        for (std::size_t i = 0; i < trades; ++i)
        {
            const GlostenMilgromQuotes quotes = glostenMilgromQuotes(params, belief);
            const bool informed = rng.bernoulli(params.informed_share);
            const bool buys = informed ? path.value_is_high : rng.below(2) == 0;

            GlostenMilgromTrade trade;
            trade.side = buys ? Side::Buy : Side::Sell;
            trade.bid = quotes.bid;
            trade.ask = quotes.ask;
            trade.price = buys ? quotes.ask : quotes.bid;
            trade.belief_after = buys ? quotes.belief_after_buy : quotes.belief_after_sell;
            belief = trade.belief_after;
            path.trades.push_back(trade);
        }
        return path;
    }

} // namespace quantMicrostructure
