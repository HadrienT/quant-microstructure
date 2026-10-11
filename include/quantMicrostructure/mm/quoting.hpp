#ifndef QUANTMICROSTRUCTURE_MM_QUOTING_HPP
#define QUANTMICROSTRUCTURE_MM_QUOTING_HPP

#include "quantMicrostructure/sim/rng.hpp"

#include <cstddef>
#include <functional>
#include <vector>

// Market making as a control problem (docs/companion.md, section 9): where to
// quote around a reference price, given the inventory.
//
// The model, common to the two articles: the reference price is an arithmetic
// Brownian motion of volatility sigma; a quote at a distance delta from it is
// hit at the rate A exp(-k delta); the market maker maximises an exponential
// utility of risk aversion gamma of its final wealth.
//
// What the model does not have (section 9.3): adverse selection (the price is
// a martingale whatever is traded), queue position, a tick, fees. Its
// parameters A and k are fitted to nothing here.

namespace quantMicrostructure
{

    struct QuotingParams
    {
        /// Volatility of the reference price, per square root of unit time.
        double sigma{};
        /// Risk aversion.
        double gamma{};
        /// Decay of the fill rate with the distance to the reference price.
        double k{};
        /// Fill rate of a quote at the reference price, per unit time.
        double A{};
    };

    /// Distances of the two quotes from the reference price: the bid is at
    /// reference - bid, the ask at reference + ask.
    struct Quotes
    {
        double bid{};
        double ask{};
    };

    /// The price at which the market maker is indifferent to its inventory q
    /// (Avellaneda & Stoikov 2008, equation 3.17):
    /// r = s - q gamma sigma^2 (T - t). Long, it is below the reference price.
    double avellanedaStoikovReservationPrice(double reference, double inventory, const QuotingParams &params, double time_left);

    /// The total spread set around the reservation price (their equation
    /// 3.18): gamma sigma^2 (T - t) + (2 / gamma) ln(1 + gamma / k).
    double avellanedaStoikovSpread(const QuotingParams &params, double time_left);

    /// The two quotes of the "inventory" strategy of the article: half the
    /// spread on each side of the reservation price.
    Quotes avellanedaStoikovQuotes(double inventory, const QuotingParams &params, double time_left);

    /// The "symmetric" strategy the article compares it with: the same spread,
    /// centred on the reference price whatever the inventory.
    Quotes symmetricQuotes(const QuotingParams &params, double time_left);

    /// Closed-form approximation of the stationary quotes of Guéant, Lehalle &
    /// Fernandez-Tapia (2013), given after their proposition 3:
    ///
    ///     bid = (1 / gamma) ln(1 + gamma / k) + (2 q + 1) / 2 * c
    ///     ask = (1 / gamma) ln(1 + gamma / k) - (2 q - 1) / 2 * c
    ///     c   = sqrt(sigma^2 gamma / (2 k A) * (1 + gamma / k)^(1 + k / gamma))
    ///
    /// They depend on the inventory alone, not on a horizon: this is the form
    /// used in practice. The approximation is good for small inventories.
    Quotes glftApproximateQuotes(double inventory, const QuotingParams &params);

    /// The exact solution of the same problem with the inventory bounded by
    /// Q (their propositions 1 and 2, theorem 1): v(t) = exp(-M (T - t)) 1,
    /// with M the tridiagonal matrix of diagonal alpha q^2 and off-diagonal
    /// -eta, alpha = k gamma sigma^2 / 2, eta = A (1 + gamma / k)^-(1 + k /
    /// gamma), and
    ///
    ///     bid(t, q) = (1 / k) ln(v_q / v_{q+1}) + (1 / gamma) ln(1 + gamma / k)
    ///     ask(t, q) = (1 / k) ln(v_q / v_{q-1}) + (1 / gamma) ln(1 + gamma / k)
    class GlftSolution
    {
      public:
        /// Solves for the time `time_left` before the horizon. Throws
        /// std::invalid_argument if a parameter is not positive.
        GlftSolution(const QuotingParams &params, int inventory_limit, double time_left);

        /// Quotes at inventory `q`, |q| <= limit. At q = limit there is no bid
        /// (the market maker may not buy more) and `bid` is infinite; likewise
        /// `ask` at q = -limit.
        Quotes quotes(int q) const;

        int inventoryLimit() const noexcept { return limit_; }

      private:
        QuotingParams params_;
        int limit_;
        std::vector<double> v_;
    };

    // ---- The simulation of the article ----

    struct QuotingSimulation
    {
        QuotingParams params;
        /// Initial reference price.
        double s0{100.0};
        /// Horizon T and time step dt.
        double horizon{1.0};
        double dt{0.005};
    };

    struct QuotingOutcome
    {
        /// Final wealth: cash plus inventory at the final reference price.
        double pnl{};
        double final_inventory{};
    };

    /// The quotes to post given the inventory and the time left.
    using QuotePolicy = std::function<Quotes(double inventory, double time_left)>;

    /// The procedure of Avellaneda & Stoikov (2008), section 3.3: at each
    /// step the quotes are set from the state; then, with probability
    /// A exp(-k ask) dt the inventory falls by one and the wealth rises by
    /// reference + ask, with probability A exp(-k bid) dt the inventory rises
    /// by one and the wealth falls by reference - bid, and the reference price
    /// moves by + or - sigma sqrt(dt).
    std::vector<QuotingOutcome> simulateQuoting(const QuotingSimulation &simulation, const QuotePolicy &policy, std::size_t paths, Rng &rng);

} // namespace quantMicrostructure

#endif
