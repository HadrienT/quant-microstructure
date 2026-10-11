#ifndef QUANTMICROSTRUCTURE_SIM_TOY_MODELS_HPP
#define QUANTMICROSTRUCTURE_SIM_TOY_MODELS_HPP

#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/core/types.hpp"
#include "quantMicrostructure/sim/rng.hpp"

#include <cstddef>
#include <vector>

// Three models whose answer is known in closed form (docs/companion.md,
// section 4). None is a model of a real market: they are simulated so that an
// estimator can be checked against the truth it is supposed to find.
//
// The formulas below are derived in the comments and checked by simulation in
// tests/testToyModels.cpp. They have NOT been re-read in the original articles
// (Roll 1984, Kyle 1985, Glosten & Milgrom 1985), which is why no equation
// number is cited.

namespace quantMicrostructure
{

    // ---- Roll (1984): the spread as a pure cost ----

    struct RollParams
    {
        /// Half of the spread, s / 2, in ticks.
        Price half_spread{};
        /// Size of a move of the efficient price, in ticks.
        Price step{1};
        Price initial_price{};
        /// Time between two trades, in nanoseconds.
        Timestamp interval{1'000'000'000};
    };

    /// A stream of Trade events of one lot. Between two trades the efficient
    /// price m moves by +step or -step with equal probability; trade t is made
    /// at p_t = m_t + e_t * s / 2, where the sign e_t (the side of the event)
    /// is +1 or -1 with equal probability, independent of everything else.
    ///
    /// Then p_t - p_{t-1} = u_t + (s / 2)(e_t - e_{t-1}), where the moves u are
    /// independent, so that
    ///
    ///     Cov(p_t - p_{t-1}, p_{t-1} - p_{t-2}) = -(s / 2)^2 Var(e) = -s^2 / 4.
    ///
    /// Assumes independent trade signs, which real order flow contradicts
    /// (section 3 of the companion).
    std::vector<Event> simulateRoll(const RollParams &params, std::size_t trades, Rng &rng);

    // ---- Kyle (1985), one period: the price as a function of the flow ----

    struct KyleParams
    {
        /// Prior mean of the value.
        double p0{};
        /// Standard deviation of the value, known to the insider alone.
        double sigma_v{};
        /// Standard deviation of the order of the noise traders.
        double sigma_u{};
    };

    struct KyleEquilibrium
    {
        /// Price move per unit of total flow: Kyle's lambda.
        double lambda{};
        /// Insider's order per unit of mispricing.
        double beta{};
    };

    /// The linear equilibrium. The insider, who knows v and expects the price
    /// p0 + lambda (x + u), maximises E[(v - p) x] = (v - p0 - lambda x) x, so
    /// x = beta (v - p0) with beta = 1 / (2 lambda). The market maker sees only
    /// y = x + u and sets p = E[v | y] = p0 + lambda y, with lambda =
    /// Cov(v, y) / Var(y) = beta sigma_v^2 / (beta^2 sigma_v^2 + sigma_u^2).
    /// Solving the two gives
    ///
    ///     lambda = sigma_v / (2 sigma_u),    beta = sigma_u / sigma_v.
    ///
    /// The price then reveals half of the insider's information:
    /// Var(v | p) = sigma_v^2 / 2.
    KyleEquilibrium kyleEquilibrium(const KyleParams &params);

    /// One auction of the one-period model.
    struct KyleAuction
    {
        double value{};
        double insider_order{};
        double noise_order{};
        double price{};
    };

    /// Independent auctions at the equilibrium of `params`.
    std::vector<KyleAuction> simulateKyle(const KyleParams &params, std::size_t auctions, Rng &rng);

    // ---- Glosten & Milgrom (1985): the spread as adverse selection ----

    struct GlostenMilgromParams
    {
        double v_low{};
        double v_high{};
        /// Share of traders who know the value; the others buy or sell with
        /// equal probability.
        double informed_share{};
        /// Prior probability that the value is `v_high`.
        double prior_high{0.5};
    };

    struct GlostenMilgromQuotes
    {
        double bid{};
        double ask{};
        /// Probability that the value is high after a sell, and after a buy.
        double belief_after_sell{};
        double belief_after_buy{};
    };

    /// Quotes of a competitive, risk-neutral market maker who believes the
    /// value is high with probability `belief`: each quote is the expected
    /// value given the trade it would get.
    ///
    /// An informed trader (share pi) buys if the value is high and sells if it
    /// is low, so P(buy | high) = (1 + pi) / 2 and P(buy | low) = (1 - pi) / 2.
    /// By Bayes' rule, with theta the belief,
    ///
    ///     P(high | buy)  = theta (1 + pi) / (theta (1 + pi) + (1 - theta)(1 - pi))
    ///     P(high | sell) = theta (1 - pi) / (theta (1 - pi) + (1 - theta)(1 + pi))
    ///
    /// and ask = E[V | buy], bid = E[V | sell]. At theta = 1/2 the spread is
    /// pi (v_high - v_low).
    GlostenMilgromQuotes glostenMilgromQuotes(const GlostenMilgromParams &params, double belief);

    struct GlostenMilgromTrade
    {
        /// Side of the trader, who is the aggressor.
        Side side{};
        /// The ask for a buy, the bid for a sell.
        double price{};
        double bid{};
        double ask{};
        /// The market maker's belief once the trade is seen.
        double belief_after{};
    };

    struct GlostenMilgromPath
    {
        bool value_is_high{false};
        std::vector<GlostenMilgromTrade> trades;
    };

    /// Draws the value from the prior, then `trades` traders one after the
    /// other; the market maker updates its belief after each.
    GlostenMilgromPath simulateGlostenMilgrom(const GlostenMilgromParams &params, std::size_t trades, Rng &rng);

} // namespace quantMicrostructure

#endif
