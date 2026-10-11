#ifndef QUANTMICROSTRUCTURE_MM_MARKET_MAKER_HPP
#define QUANTMICROSTRUCTURE_MM_MARKET_MAKER_HPP

#include "quantMicrostructure/core/types.hpp"
#include "quantMicrostructure/measures/tape.hpp"
#include "quantMicrostructure/mm/quoting.hpp"
#include "quantMicrostructure/replay/backtest.hpp"

#include <cstddef>
#include <functional>
#include <span>
#include <vector>

namespace quantMicrostructure
{

    struct MarketMakerConfig
    {
        /// Lots of each quote.
        Quantity order_size{1};
        /// No quote is posted that could take the inventory beyond this, in
        /// absolute value. With latency, an order being cancelled can still be
        /// filled, and the inventory can exceed it for a moment.
        Quantity inventory_limit{10};
    };

    /// The distances, in ticks, at which to quote given the inventory in lots
    /// and the time in nanoseconds.
    using TickQuotePolicy = std::function<Quotes(Quantity inventory, Timestamp now)>;

    /// A market maker for the book-level backtest: one bid and one ask of
    /// `order_size` lots, at the distances the policy gives around the mid
    /// price, moved to whole ticks away from the mid (bid down, ask up) and
    /// never through the other side of the book: it only ever provides
    /// liquidity. A quote is cancelled and replaced when its price changes.
    ///
    /// It quotes around the mid price of the book: the reference price of the
    /// models, which they take as given and as a martingale.
    class MarketMaker : public Strategy
    {
      public:
        MarketMaker(TickQuotePolicy policy, const MarketMakerConfig &config);

        void onMarket(const MarketView &view, Gateway &gateway) override;
        void onFill(const Fill &fill, const MarketView &view, Gateway &gateway) override;

      private:
        struct Working
        {
            OrderId id{0};
            Price price{0};
            Quantity remaining{0};
        };

        void requote(const MarketView &view, Gateway &gateway);
        void update(Working &working, Side side, bool wanted, Price price, Gateway &gateway);

        TickQuotePolicy policy_;
        MarketMakerConfig config_;
        Working bid_;
        Working ask_;
    };

    /// Where the P&L of a market maker comes from (section 9.3 of the
    /// companion), in ticks times lots. With s = +1 for a buy and -1 for a
    /// sell, p the fill price, m the mid just before the fill and M the final
    /// mark:
    ///
    ///     spread    = sum of s q (m - p)     earned at the moment of each fill
    ///     inventory = sum of s q (M - m)     what the price did afterwards
    ///
    /// and total = spread + inventory - fees. The inventory term holds both
    /// the luck of the price and the adverse selection; markouts tell them
    /// apart.
    struct MakerPnl
    {
        double spread{};
        double inventory{};
        double fees{};
        double total{};
        /// Fills that had a two-sided quote before them; the others count for
        /// nothing in `spread` and wholly in `inventory`, at their fill price.
        std::size_t fills{};
    };

    MakerPnl makerPnl(const BacktestResult &result, const Tape &tape);

    struct Markout
    {
        Timestamp horizon{};
        /// Mean over lots of s (m(t + horizon) - p), in ticks: what a lot
        /// bought or sold is worth `horizon` later. It is the realized
        /// half-spread of section 4.5, seen from the maker.
        double mean{};
        /// Lots averaged: those of fills with a mid `horizon` later, not
        /// beyond the end of the tape.
        Quantity lots{};
    };

    /// Markouts of a list of fills at several horizons. A markout that falls
    /// as the horizon grows is adverse selection: the price moves against the
    /// side that was filled.
    std::vector<Markout> markouts(std::span<const Fill> fills, const Tape &tape, std::span<const Timestamp> horizons);

} // namespace quantMicrostructure

#endif
