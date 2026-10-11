#ifndef QUANTMICROSTRUCTURE_IMPACT_PROPAGATOR_HPP
#define QUANTMICROSTRUCTURE_IMPACT_PROPAGATOR_HPP

#include "quantMicrostructure/sim/rng.hpp"

#include <cstddef>
#include <span>
#include <vector>

// The propagator model of Bouchaud, Gefen, Potters & Wyart (2004), section 3
// (docs/companion.md, section 5.3), without the volume factor: the price just
// before trade n is the sum of the decaying impacts of the earlier trades,
//
//     p_n = sum over k < n of G(n - k) e_k + noise        (their equation 12)
//
// where e_k is the sign of trade k. Everything here is in trade time.
//
// A propagator is stored as G(1), G(2), ..., G(L): entry i is G(i + 1). Beyond
// its last entry it is taken to stay at G(L).

namespace quantMicrostructure
{

    /// G(l) = gamma0 * l0^beta / (l0 + l)^beta for l = 1..length (their
    /// equation 22).
    std::vector<double> powerLawPropagator(double gamma0, double l0, double beta, std::size_t length);

    /// The exponent of the propagator for which a flow whose sign correlation
    /// decays as lag^(-gamma) gives a diffusive price: (1 - gamma) / 2 (their
    /// equation 23). Below it the price is super-diffusive, above it
    /// sub-diffusive; in general D(l) grows as l^(2 - 2 beta - gamma).
    double criticalPropagatorExponent(double gamma);

    /// Prices p_0..p_{N-1} of the model for the given signs, p_n being the
    /// price before trade n and p_0 = 0. `noise_sd` is the standard deviation
    /// of the independent noise added between two trades.
    std::vector<double> propagatorPrices(std::span<const double> signs, std::span<const double> propagator, double noise_sd, Rng &rng);

    /// D(l) = <(p_{n+l} - p_n)^2> for each lag (their equation 1). A diffusive
    /// price has D(l) proportional to l.
    std::vector<double> priceDiffusion(std::span<const double> prices, std::span<const std::size_t> lags);

    /// Estimates G(1..length) from signs and the prices before each trade.
    ///
    /// The model says the price change after trade n is r_n = p_{n+1} - p_n =
    /// sum over m >= 0 of K(m) e_{n-m} + noise, with K(0) = G(1) and K(m) =
    /// G(m + 1) - G(m). Multiplying by e_{n-l} and averaging,
    ///
    ///     S(l) = <r_{n+l} e_n> = sum over m of K(m) C(l - m),   C(k) = <e_{n+k} e_n>
    ///
    /// which is solved for K(0..length-1), the later terms being neglected;
    /// then G(l) = K(0) + ... + K(l - 1). The neglected terms bias the last
    /// lags when the propagator is still moving there: trust the first half.
    ///
    /// `prices` and `signs` have the same length. Throws std::invalid_argument
    /// if they do not, if there are too few trades, or if the system is
    /// singular (constant signs).
    std::vector<double> fitPropagator(std::span<const double> signs, std::span<const double> prices, std::size_t length);

} // namespace quantMicrostructure

#endif
