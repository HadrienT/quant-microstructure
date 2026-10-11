#ifndef QUANTMICROSTRUCTURE_MEASURES_FLOW_HPP
#define QUANTMICROSTRUCTURE_MEASURES_FLOW_HPP

#include "quantMicrostructure/measures/tape.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace quantMicrostructure
{

    // Measures in trade time: one step per trade (docs/companion.md, sections
    // 3, 5.2 and 7.2).

    /// The price of each trade of the tape, in ticks.
    std::vector<double> tradePrices(const Tape &tape);

    /// The sign of each trade of the tape, as the stream gave it: +1 when the
    /// aggressor buys.
    std::vector<double> tradeSigns(const Tape &tape);

    /// C(l) = <x_{n+l} x_n> - <x>^2 for l = 0..max_lag: for trade signs, the
    /// correlation function of Bouchaud, Gefen, Potters & Wyart (2004),
    /// equation 6. Each lag is averaged over the pairs that exist.
    std::vector<double> autocovariance(std::span<const double> x, std::size_t max_lag);

    /// The response function R(l) = <(p_{n+l} - p_n) e_n> for l = 0..max_lag
    /// (Bouchaud, Gefen, Potters & Wyart 2004, equation 3), in ticks: how far
    /// the price has moved, on average and in the direction of trade n, by the
    /// time of trade n + l. The price p_n is the mid just before trade n
    /// (their section 2.2), so R(0) = 0.
    ///
    /// It mixes the effect of trade n with that of the trades that follow,
    /// which tend to have the same sign. Trades without a two-sided quote
    /// before them are left out of the pairs they would belong to. Prices are
    /// not detrended.
    std::vector<double> responseFunction(const Tape &tape, std::size_t max_lag);

    /// Signs inferred from prices alone, by the tick test: +1 if the price is
    /// above the previous different price, -1 if it is below. Trades before
    /// the first price change get 0.
    std::vector<double> tickTestSigns(std::span<const double> trade_prices);

    /// Signs inferred the way they are when the feed does not give them
    /// (section 7.2 of the companion): +1 for a trade above the mid in force
    /// just before it, -1 below (the rule of Bouchaud et al. 2004, section
    /// 2.1); at the mid, or without a two-sided quote, the tick test. This is
    /// the algorithm attributed to Lee & Ready (1991), whose article was not
    /// re-read: in particular no delay is applied to the quotes.
    std::vector<double> quoteRuleSigns(const Tape &tape);

    /// Share of positions where two sign sequences agree, among those where
    /// neither is 0.
    double signAgreement(std::span<const double> a, std::span<const double> b);

} // namespace quantMicrostructure

#endif
