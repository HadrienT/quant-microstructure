#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "quantMicrostructure/book/level_aggregator.hpp"
#include "quantMicrostructure/book/level_book.hpp"
#include "quantMicrostructure/book/order_book.hpp"
#include "quantMicrostructure/core/build_info.hpp"
#include "quantMicrostructure/core/event.hpp"
#include "quantMicrostructure/feed/event_file.hpp"
#include "quantMicrostructure/impact/kyle_lambda.hpp"
#include "quantMicrostructure/impact/metaorder.hpp"
#include "quantMicrostructure/impact/propagator.hpp"
#include "quantMicrostructure/impact/round_trip.hpp"
#include "quantMicrostructure/measures/flow.hpp"
#include "quantMicrostructure/measures/liquidity.hpp"
#include "quantMicrostructure/measures/spread.hpp"
#include "quantMicrostructure/measures/tape.hpp"
#include "quantMicrostructure/measures/volatility.hpp"
#include "quantMicrostructure/sim/hawkes.hpp"
#include "quantMicrostructure/sim/order_splitting.hpp"
#include "quantMicrostructure/sim/queue_reactive.hpp"
#include "quantMicrostructure/sim/rng.hpp"
#include "quantMicrostructure/sim/toy_models.hpp"
#include "quantMicrostructure/sim/zero_intelligence.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace py = pybind11;
using namespace quantMicrostructure;

// The interface carries plain data: numbers, strings, dictionaries of numpy
// arrays. The two classes, EventStream and Tape, are opaque handles on data
// that stays in C++ between two calls; `to_arrays`, `quotes` and `trades` copy
// it out.

namespace
{

    using DoubleArray = py::array_t<double, py::array::c_style | py::array::forcecast>;
    using SizeArray = py::array_t<std::uint64_t, py::array::c_style | py::array::forcecast>;

    constexpr double NanosPerSecond = 1e9;

    struct EventStream
    {
        std::vector<Event> events;
        StreamKind kind{StreamKind::OrderByOrder};
    };

    std::string kindName(StreamKind kind)
    {
        switch (kind)
        {
            case StreamKind::OrderByOrder:
                return "l3";
            case StreamKind::Aggregated:
                return "l2";
            case StreamKind::TradesOnly:
                break;
        }
        return "trades";
    }

    StreamKind kindFromName(const std::string &name)
    {
        if (name == "l3")
        {
            return StreamKind::OrderByOrder;
        }
        if (name == "l2")
        {
            return StreamKind::Aggregated;
        }
        if (name == "trades")
        {
            return StreamKind::TradesOnly;
        }
        throw std::invalid_argument("kind must be 'l3', 'l2' or 'trades'");
    }

    template <typename T, typename Range, typename Get>
    py::array_t<T> column(const Range &range, Get get)
    {
        py::array_t<T> out(static_cast<py::ssize_t>(range.size()));
        T *data = out.mutable_data();
        std::size_t i = 0;
        for (const auto &item : range)
        {
            data[i++] = static_cast<T>(get(item));
        }
        return out;
    }

    py::array_t<double> toArray(const std::vector<double> &values)
    {
        return column<double>(values, [](double v)
                              { return v; });
    }

    std::span<const double> view(const DoubleArray &array)
    {
        if (array.ndim() != 1)
        {
            throw std::invalid_argument("a one-dimensional array is expected");
        }
        return {array.data(), static_cast<std::size_t>(array.size())};
    }

    std::vector<double> copyOf(const DoubleArray &array)
    {
        const std::span<const double> values = view(array);
        return {values.begin(), values.end()};
    }

    std::vector<std::size_t> sizes(const SizeArray &array)
    {
        const std::span<const std::uint64_t> raw{array.data(), static_cast<std::size_t>(array.size())};
        return {raw.begin(), raw.end()};
    }

    Timestamp nanos(double seconds)
    {
        return static_cast<Timestamp>(seconds * NanosPerSecond);
    }

    py::object orNone(const std::optional<double> &value)
    {
        return value ? py::object(py::float_(*value)) : py::object(py::none());
    }

    py::dict levelsOf(const std::vector<BookLevel> &levels)
    {
        py::dict out;
        out["price"] = column<std::int64_t>(levels, [](const BookLevel &l)
                                            { return l.price; });
        out["qty"] = column<std::int64_t>(levels, [](const BookLevel &l)
                                          { return l.qty; });
        return out;
    }

    template <typename Book>
    py::dict finalBook(const std::vector<Event> &events, std::size_t max_levels)
    {
        Book book;
        for (const Event &event : events)
        {
            book.apply(event);
        }
        py::dict out;
        out["bids"] = levelsOf(book.depth(Side::Buy, max_levels));
        out["asks"] = levelsOf(book.depth(Side::Sell, max_levels));
        return out;
    }

    QueueIntensities intensities(const py::dict &queue)
    {
        QueueIntensities out;
        out.limit = copyOf(queue["limit"].cast<DoubleArray>());
        out.cancel = copyOf(queue["cancel"].cast<DoubleArray>());
        out.market = copyOf(queue["market"].cast<DoubleArray>());
        return out;
    }

    void bindStreams(py::module_ &m)
    {
        py::class_<EventStream>(m, "EventStream", "A stream of events in the single format of the library.")
            .def("__len__", [](const EventStream &s)
                 { return s.events.size(); })
            .def_property_readonly(
                "kind", [](const EventStream &s)
                { return kindName(s.kind); },
                "'l3' (order by order), 'l2' (aggregated by price level) or 'trades'.")
            .def(
                "to_arrays",
                [](const EventStream &s)
                {
                    py::dict out;
                    out["ts"] = column<std::int64_t>(s.events, [](const Event &e)
                                                     { return e.ts; });
                    out["ts_recv"] = column<std::int64_t>(s.events, [](const Event &e)
                                                          { return e.ts_recv; });
                    out["seq"] = column<std::uint64_t>(s.events, [](const Event &e)
                                                       { return e.seq; });
                    out["order_id"] = column<std::uint64_t>(s.events, [](const Event &e)
                                                            { return e.order_id; });
                    out["price"] = column<std::int64_t>(s.events, [](const Event &e)
                                                        { return e.price; });
                    out["qty"] = column<std::int64_t>(s.events, [](const Event &e)
                                                      { return e.qty; });
                    out["type"] = column<std::uint8_t>(s.events, [](const Event &e)
                                                       { return e.type; });
                    out["side"] = column<std::uint8_t>(s.events, [](const Event &e)
                                                       { return e.side; });
                    return out;
                },
                "One array per field. Times in nanoseconds, prices in ticks, quantities in\n"
                "lots. type: 0 add, 1 cancel, 2 trade, 3 level, 4 clear. side: 0 buy, 1 sell\n"
                "(for a trade, the side of the aggressor).")
            .def(
                "write", [](const EventStream &s, const std::string &path)
                { writeEventFile(path, s.events); },
                py::arg("path"), "Writes the stream to an event file.");

        m.def(
            "read_event_file",
            [](const std::string &path, const std::string &kind)
            { return EventStream{readEventFile(path), kindFromName(kind)}; },
            py::arg("path"), py::arg("kind"),
            "Reads an event file. The file does not say what kind of stream it holds:\n"
            "'l3', 'l2' or 'trades'.");

        m.def(
            "aggregate",
            [](const EventStream &stream)
            {
                if (stream.kind != StreamKind::OrderByOrder)
                {
                    throw std::invalid_argument("aggregate: an 'l3' stream is expected");
                }
                EventStream out;
                out.kind = StreamKind::Aggregated;
                LevelAggregator aggregator;
                for (const Event &event : stream.events)
                {
                    aggregator.apply(event, out.events);
                }
                return out;
            },
            py::arg("stream"), "The aggregated ('l2') stream of the same market as an 'l3' stream.");

        m.def(
            "final_book",
            [](const EventStream &stream, std::size_t max_levels)
            {
                switch (stream.kind)
                {
                    case StreamKind::OrderByOrder:
                        return finalBook<OrderBook>(stream.events, max_levels);
                    case StreamKind::Aggregated:
                        return finalBook<LevelBook>(stream.events, max_levels);
                    case StreamKind::TradesOnly:
                        break;
                }
                throw std::invalid_argument("final_book: a 'trades' stream has no book");
            },
            py::arg("stream"), py::arg("max_levels") = 20,
            "The book at the end of the stream: {'bids': {'price', 'qty'}, 'asks': ...},\n"
            "best price first.");

        py::class_<Tape>(m, "Tape", "Quotes and trades read from a stream: what the measures take.")
            .def_property_readonly(
                "end", [](const Tape &t)
                { return t.end; },
                "Timestamp of the last event, in nanoseconds.")
            .def(
                "quotes",
                [](const Tape &t)
                {
                    py::dict out;
                    out["ts"] = column<std::int64_t>(t.quotes, [](const Quote &q)
                                                     { return q.ts; });
                    out["bid"] = column<std::int64_t>(t.quotes, [](const Quote &q)
                                                      { return q.bid; });
                    out["ask"] = column<std::int64_t>(t.quotes, [](const Quote &q)
                                                      { return q.ask; });
                    out["bid_qty"] = column<std::int64_t>(t.quotes, [](const Quote &q)
                                                          { return q.bid_qty; });
                    out["ask_qty"] = column<std::int64_t>(t.quotes, [](const Quote &q)
                                                          { return q.ask_qty; });
                    out["mid"] = column<double>(t.quotes, [](const Quote &q)
                                                { return q.twoSided() ? q.mid() : std::numeric_limits<double>::quiet_NaN(); });
                    return out;
                },
                "Each change of the best prices or of their quantities. A side with a\n"
                "quantity of 0 is empty and its price means nothing; mid is then NaN.")
            .def(
                "trades",
                [](const Tape &t)
                {
                    py::dict out;
                    out["ts"] = column<std::int64_t>(t.trades, [](const Trade &x)
                                                     { return x.ts; });
                    out["sign"] = column<double>(t.trades, [](const Trade &x)
                                                 { return x.sign(); });
                    out["qty"] = column<std::int64_t>(t.trades, [](const Trade &x)
                                                      { return x.qty; });
                    out["price"] = column<double>(t.trades, [](const Trade &x)
                                                  { return x.price; });
                    out["quote"] = column<std::int64_t>(t.trades, [](const Trade &x)
                                                        { return x.quote == Trade::NoQuote ? std::int64_t{-1} : static_cast<std::int64_t>(x.quote); });
                    return out;
                },
                "One row per aggressive order. sign: +1 when the aggressor buys. price: its\n"
                "volume-weighted average price. quote: row, in quotes(), of the quote in\n"
                "force just before the trade, -1 if none.");

        m.def(
            "build_tape", [](const EventStream &stream)
            { return buildTape(stream.events, stream.kind); },
            py::arg("stream"), "Replays a stream through a book and records quotes and trades.");
    }

    void bindSimulators(py::module_ &m)
    {
        m.def(
            "simulate_zero_intelligence",
            [](double limit_rate, double market_rate, double cancel_rate, Price window, double duration, std::uint64_t seed, Quantity order_size, Price initial_price)
            {
                ZeroIntelligenceParams params;
                params.limit_rate = limit_rate;
                params.market_rate = market_rate;
                params.cancel_rate = cancel_rate;
                params.window = window;
                params.order_size = order_size;
                params.initial_price = initial_price;
                ZeroIntelligenceSimulator sim(params, seed);
                EventStream out;
                out.kind = StreamKind::OrderByOrder;
                sim.runUntil(nanos(duration), out.events);
                return out;
            },
            py::arg("limit_rate"), py::arg("market_rate"), py::arg("cancel_rate"), py::arg("window"), py::arg("duration"), py::arg("seed"),
            py::arg("order_size") = 1, py::arg("initial_price") = 10'000,
            "Zero-intelligence order book (Smith, Farmer, Gillemot & Krishnamurthy 2003),\n"
            "as an 'l3' stream. Rates are in orders per second: limit orders per price\n"
            "level and per side, market orders per side, cancellation per resting order.\n"
            "duration is in seconds.");

        m.def(
            "queue_invariant_distribution",
            [](const DoubleArray &limit, const DoubleArray &cancel, const DoubleArray &market)
            { return toArray(queueInvariantDistribution(QueueIntensities{copyOf(limit), copyOf(cancel), copyOf(market)})); },
            py::arg("limit"), py::arg("cancel"), py::arg("market"),
            "Stationary distribution of the size of one queue, given its three intensity\n"
            "tables (Huang, Lehalle & Rosenbaum 2015, section 2.3.3).");

        m.def(
            "simulate_queue_reactive",
            [](const std::vector<py::dict> &queues, double theta, double theta_reinit, double duration, std::uint64_t seed, Quantity event_size, Price initial_reference)
            {
                QueueReactiveParams params;
                for (const py::dict &queue : queues)
                {
                    params.queues.push_back(intensities(queue));
                }
                params.theta = theta;
                params.theta_reinit = theta_reinit;
                params.event_size = event_size;
                params.initial_reference = initial_reference;
                QueueReactiveSimulator sim(params, seed);
                EventStream out;
                out.kind = StreamKind::Aggregated;
                sim.snapshot(out.events);
                sim.runUntil(nanos(duration), out.events);
                return out;
            },
            py::arg("queues"), py::arg("theta"), py::arg("theta_reinit"), py::arg("duration"), py::arg("seed"),
            py::arg("event_size") = 1, py::arg("initial_reference") = 10'000,
            "Queue-reactive model (Huang, Lehalle & Rosenbaum 2015), as an 'l2' stream.\n"
            "queues: one dict per distance to the reference price, nearest first, with\n"
            "the arrays 'limit', 'cancel' and 'market': intensity per second when the\n"
            "queue holds n units. A large-tick model.");

        m.def(
            "simulate_roll",
            [](Price half_spread, std::size_t trades, std::uint64_t seed, Price step, Price initial_price, double interval)
            {
                Rng rng(seed);
                return EventStream{simulateRoll(RollParams{half_spread, step, initial_price, nanos(interval)}, trades, rng), StreamKind::TradesOnly};
            },
            py::arg("half_spread"), py::arg("trades"), py::arg("seed"), py::arg("step") = 1, py::arg("initial_price") = 10'000, py::arg("interval") = 1.0,
            "Roll (1984) model, as a 'trades' stream: an efficient price that moves by\n"
            "+-step ticks between trades, and trades at +-half_spread around it.");

        m.def(
            "kyle_equilibrium",
            [](double sigma_v, double sigma_u)
            {
                const KyleEquilibrium e = kyleEquilibrium(KyleParams{0.0, sigma_v, sigma_u});
                py::dict out;
                out["lambda"] = e.lambda;
                out["beta"] = e.beta;
                return out;
            },
            py::arg("sigma_v"), py::arg("sigma_u"),
            "Kyle (1985), one period: lambda = sigma_v / (2 sigma_u), beta = sigma_u / sigma_v.");

        m.def(
            "simulate_kyle",
            [](double p0, double sigma_v, double sigma_u, std::size_t auctions, std::uint64_t seed)
            {
                Rng rng(seed);
                const std::vector<KyleAuction> sample = simulateKyle(KyleParams{p0, sigma_v, sigma_u}, auctions, rng);
                py::dict out;
                out["value"] = column<double>(sample, [](const KyleAuction &a)
                                              { return a.value; });
                out["insider_order"] = column<double>(sample, [](const KyleAuction &a)
                                                      { return a.insider_order; });
                out["noise_order"] = column<double>(sample, [](const KyleAuction &a)
                                                    { return a.noise_order; });
                out["price"] = column<double>(sample, [](const KyleAuction &a)
                                              { return a.price; });
                return out;
            },
            py::arg("p0"), py::arg("sigma_v"), py::arg("sigma_u"), py::arg("auctions"), py::arg("seed"),
            "Independent one-period Kyle auctions at equilibrium.");

        m.def(
            "glosten_milgrom_quotes",
            [](double v_low, double v_high, double informed_share, double belief)
            {
                const GlostenMilgromQuotes q = glostenMilgromQuotes(GlostenMilgromParams{v_low, v_high, informed_share, belief}, belief);
                py::dict out;
                out["bid"] = q.bid;
                out["ask"] = q.ask;
                out["belief_after_buy"] = q.belief_after_buy;
                out["belief_after_sell"] = q.belief_after_sell;
                return out;
            },
            py::arg("v_low"), py::arg("v_high"), py::arg("informed_share"), py::arg("belief") = 0.5,
            "Glosten & Milgrom (1985): zero-profit quotes of a market maker who believes\n"
            "the value is high with probability `belief`.");

        m.def(
            "simulate_glosten_milgrom",
            [](double v_low, double v_high, double informed_share, std::size_t trades, std::uint64_t seed, double prior_high)
            {
                Rng rng(seed);
                const GlostenMilgromPath path = simulateGlostenMilgrom(GlostenMilgromParams{v_low, v_high, informed_share, prior_high}, trades, rng);
                py::dict out;
                out["value_is_high"] = path.value_is_high;
                out["sign"] = column<double>(path.trades, [](const GlostenMilgromTrade &t)
                                             { return t.side == Side::Buy ? 1.0 : -1.0; });
                out["price"] = column<double>(path.trades, [](const GlostenMilgromTrade &t)
                                              { return t.price; });
                out["bid"] = column<double>(path.trades, [](const GlostenMilgromTrade &t)
                                            { return t.bid; });
                out["ask"] = column<double>(path.trades, [](const GlostenMilgromTrade &t)
                                            { return t.ask; });
                out["belief_after"] = column<double>(path.trades, [](const GlostenMilgromTrade &t)
                                                     { return t.belief_after; });
                return out;
            },
            py::arg("v_low"), py::arg("v_high"), py::arg("informed_share"), py::arg("trades"), py::arg("seed"), py::arg("prior_high") = 0.5,
            "One market of the sequential Glosten-Milgrom model: the quotes before each\n"
            "trade, the trade, and the market maker's belief after it.");

        m.def(
            "simulate_hawkes",
            [](double mu, double alpha, double beta, double horizon, std::uint64_t seed)
            {
                Rng rng(seed);
                return toArray(simulateHawkes(HawkesParams{mu, alpha, beta}, horizon, rng));
            },
            py::arg("mu"), py::arg("alpha"), py::arg("beta"), py::arg("horizon"), py::arg("seed"),
            "Event times, in seconds, of a Hawkes process of intensity\n"
            "mu + sum alpha exp(-beta (t - t_i)), on [0, horizon).");

        m.def(
            "hawkes_log_likelihood",
            [](double mu, double alpha, double beta, const DoubleArray &times, double horizon)
            { return hawkesLogLikelihood(HawkesParams{mu, alpha, beta}, view(times), horizon); },
            py::arg("mu"), py::arg("alpha"), py::arg("beta"), py::arg("times"), py::arg("horizon"));

        m.def(
            "hawkes_residuals",
            [](double mu, double alpha, double beta, const DoubleArray &times)
            { return toArray(hawkesResiduals(HawkesParams{mu, alpha, beta}, view(times))); },
            py::arg("mu"), py::arg("alpha"), py::arg("beta"), py::arg("times"),
            "Durations between events in the time of the compensator: exponential of mean\n"
            "1 and independent if the times come from this process.");

        m.def(
            "fit_hawkes",
            [](const DoubleArray &times, double horizon)
            {
                const HawkesFit fit = fitHawkes(view(times), horizon);
                py::dict out;
                out["mu"] = fit.params.mu;
                out["alpha"] = fit.params.alpha;
                out["beta"] = fit.params.beta;
                out["branching_ratio"] = fit.params.alpha / fit.params.beta;
                out["log_likelihood"] = fit.log_likelihood;
                out["iterations"] = fit.iterations;
                out["converged"] = fit.converged;
                return out;
            },
            py::arg("times"), py::arg("horizon"),
            "Maximum likelihood estimate of (mu, alpha, beta). Stationarity is not imposed:\n"
            "check branching_ratio < 1, and validate the fit with hawkes_residuals.");
    }

    void bindMeasures(py::module_ &m)
    {
        m.def(
            "roll_spread", [](const DoubleArray &trade_prices)
            { return orNone(rollSpread(view(trade_prices))); },
            py::arg("trade_prices"),
            "Roll's spread estimate, 2 sqrt(-Cov(dp_t, dp_{t-1})); None when the covariance\n"
            "is not negative.");

        m.def(
            "time_weighted_spread", [](const Tape &tape)
            { return orNone(timeWeightedSpread(tape)); },
            py::arg("tape"), "Quoted spread in ticks, averaged over the time the book was two-sided.");

        m.def(
            "spread_decomposition",
            [](const Tape &tape, double horizon)
            {
                const SpreadDecomposition d = spreadDecomposition(tape, nanos(horizon));
                py::dict out;
                out["effective"] = d.effective;
                out["realized"] = d.realized;
                out["impact"] = d.impact;
                out["trades"] = d.trades;
                return out;
            },
            py::arg("tape"), py::arg("horizon"),
            "Effective, realized and impact half-spreads in ticks, averaged over trades,\n"
            "with the mid read `horizon` seconds after each trade. effective = realized +\n"
            "impact.");

        m.def(
            "imbalance_next_move",
            [](const Tape &tape, std::size_t bins)
            {
                const std::vector<ImbalanceBin> result = imbalanceNextMove(tape, bins);
                py::dict out;
                out["lower"] = column<double>(result, [](const ImbalanceBin &b)
                                              { return b.lower; });
                out["upper"] = column<double>(result, [](const ImbalanceBin &b)
                                              { return b.upper; });
                out["up"] = column<std::int64_t>(result, [](const ImbalanceBin &b)
                                                 { return b.up; });
                out["down"] = column<std::int64_t>(result, [](const ImbalanceBin &b)
                                                   { return b.down; });
                out["probability_up"] = column<double>(result, [](const ImbalanceBin &b)
                                                       { return b.probabilityUp(); });
                return out;
            },
            py::arg("tape"), py::arg("bins") = 10,
            "For each bin of the imbalance V_bid / (V_bid + V_ask): how often the next\n"
            "change of the mid price was up. One observation per quote update; they are\n"
            "not independent.");

        m.def(
            "realized_variance",
            [](const DoubleArray &prices, std::size_t step, std::size_t offset)
            { return realizedVariance(view(prices), step, offset); },
            py::arg("prices"), py::arg("step") = 1, py::arg("offset") = 0,
            "Sum of squared increments of every `step`-th price.");

        m.def(
            "signature_plot",
            [](const DoubleArray &prices, const SizeArray &steps)
            {
                const std::vector<SignaturePoint> points = signaturePlot(view(prices), sizes(steps));
                py::dict out;
                out["step"] = column<std::int64_t>(points, [](const SignaturePoint &p)
                                                   { return p.step; });
                out["increments"] = column<std::int64_t>(points, [](const SignaturePoint &p)
                                                         { return p.increments; });
                out["variance"] = column<double>(points, [](const SignaturePoint &p)
                                                 { return p.variance; });
                return out;
            },
            py::arg("prices"), py::arg("steps"),
            "Realized variance against the sampling step. Flat without noise; with noise\n"
            "it rises by 2 omega^2 per increment towards the small steps.");

        m.def(
            "noise_variance", [](const DoubleArray &prices)
            { return noiseVariance(view(prices)); },
            py::arg("prices"), "Realized variance on all prices divided by twice the number of increments.");

        m.def(
            "two_scale_realized_variance",
            [](const DoubleArray &prices, std::size_t subgrids, bool adjust)
            { return twoScaleRealizedVariance(view(prices), subgrids, adjust); },
            py::arg("prices"), py::arg("subgrids"), py::arg("adjust") = true,
            "Two-scale estimator of Zhang, Mykland & Ait-Sahalia (2005), which removes the\n"
            "bias of independent noise.");

        m.def(
            "sample_mid", [](const Tape &tape, double interval)
            { return toArray(sampleMid(tape, nanos(interval))); },
            py::arg("tape"), py::arg("interval"), "The mid price every `interval` seconds, last known value.");

        m.def(
            "autocovariance", [](const DoubleArray &x, std::size_t max_lag)
            { return toArray(autocovariance(view(x), max_lag)); },
            py::arg("x"), py::arg("max_lag"), "C(l) = <x_{n+l} x_n> - <x>^2 for l = 0..max_lag.");

        m.def(
            "response_function", [](const Tape &tape, std::size_t max_lag)
            { return toArray(responseFunction(tape, max_lag)); },
            py::arg("tape"), py::arg("max_lag"),
            "R(l) = <(m_{n+l} - m_n) e_n> for l = 0..max_lag, in ticks and in trade time\n"
            "(Bouchaud, Gefen, Potters & Wyart 2004), m_n being the mid before trade n.");

        m.def(
            "tick_test_signs", [](const DoubleArray &trade_prices)
            { return toArray(tickTestSigns(view(trade_prices))); },
            py::arg("trade_prices"), "Trade signs inferred from price changes; 0 before the first change.");

        m.def(
            "quote_rule_signs", [](const Tape &tape)
            { return toArray(quoteRuleSigns(tape)); },
            py::arg("tape"), "Trade signs inferred from the mid before each trade, then the tick test.");

        m.def(
            "sign_agreement", [](const DoubleArray &a, const DoubleArray &b)
            { return signAgreement(view(a), view(b)); },
            py::arg("a"), py::arg("b"), "Share of positions where two sign sequences agree, zeros left out.");
    }

    py::dict regressionDict(const Regression &r)
    {
        py::dict out;
        out["slope"] = r.slope;
        out["intercept"] = r.intercept;
        out["r_squared"] = r.r_squared;
        out["slope_se"] = r.slope_se;
        out["observations"] = r.observations;
        return out;
    }

    void bindImpact(py::module_ &m)
    {
        m.def(
            "simulate_order_splitting_signs",
            [](double alpha, std::size_t count, std::uint64_t seed, std::size_t hidden_orders)
            {
                Rng rng(seed);
                return toArray(simulateOrderSplittingSigns(alpha, hidden_orders, count, rng));
            },
            py::arg("alpha"), py::arg("count"), py::arg("seed"), py::arg("hidden_orders") = 1,
            "Trade signs with a long memory (Lillo, Mike & Farmer 2005): large orders of\n"
            "Pareto size, tail exponent alpha, split into unit trades. The sign\n"
            "correlation decays as lag^-(alpha - 1).");

        m.def(
            "power_law_propagator",
            [](double gamma0, double l0, double beta, std::size_t length)
            { return toArray(powerLawPropagator(gamma0, l0, beta, length)); },
            py::arg("gamma0"), py::arg("l0"), py::arg("beta"), py::arg("length"),
            "G(l) = gamma0 l0^beta / (l0 + l)^beta for l = 1..length.");

        m.def("critical_propagator_exponent", &criticalPropagatorExponent, py::arg("gamma"),
              "(1 - gamma) / 2: the propagator exponent for which the price is diffusive.");

        m.def(
            "propagator_prices",
            [](const DoubleArray &signs, const DoubleArray &propagator, double noise_sd, std::uint64_t seed)
            {
                Rng rng(seed);
                return toArray(propagatorPrices(view(signs), view(propagator), noise_sd, rng));
            },
            py::arg("signs"), py::arg("propagator"), py::arg("noise_sd") = 0.0, py::arg("seed") = 1,
            "Prices before each trade in the propagator model of Bouchaud et al. (2004):\n"
            "p_n = sum over k < n of G(n - k) e_k + noise.");

        m.def(
            "price_diffusion", [](const DoubleArray &prices, const SizeArray &lags)
            { return toArray(priceDiffusion(view(prices), sizes(lags))); },
            py::arg("prices"), py::arg("lags"), "D(l) = <(p_{n+l} - p_n)^2>; proportional to l for a diffusive price.");

        m.def(
            "fit_propagator",
            [](const DoubleArray &signs, const DoubleArray &prices, std::size_t length)
            { return toArray(fitPropagator(view(signs), view(prices), length)); },
            py::arg("signs"), py::arg("prices"), py::arg("length"),
            "Estimates G(1..length) from trade signs and the prices before each trade.\n"
            "Trust the first half of the result.");

        m.def(
            "linear_regression", [](const DoubleArray &x, const DoubleArray &y)
            { return regressionDict(linearRegression(view(x), view(y))); },
            py::arg("x"), py::arg("y"), "Ordinary least squares of y on x, with an intercept.");

        m.def(
            "signed_flow_bars",
            [](const Tape &tape, double interval)
            {
                const FlowBars bars = signedFlowBars(tape, nanos(interval));
                py::dict out;
                out["flow"] = toArray(bars.flow);
                out["price_change"] = toArray(bars.price_change);
                return out;
            },
            py::arg("tape"), py::arg("interval"),
            "Signed volume (lots) and mid price change (ticks) per interval of `interval`\n"
            "seconds.");

        m.def(
            "kyle_lambda", [](const Tape &tape, double interval)
            { return regressionDict(kyleLambda(tape, nanos(interval))); },
            py::arg("tape"), py::arg("interval"),
            "Kyle's lambda: slope of the mid price change on the signed volume of the\n"
            "same interval, in ticks per lot. It depends on the interval, and its\n"
            "standard error assumes independent intervals.");

        m.def(
            "measure_metaorder_impact",
            [](double limit_rate, double market_rate, double cancel_rate, Price window, const SizeArray &children, double child_interval, double warmup, std::size_t repetitions, std::uint64_t seed)
            {
                MetaorderExperiment experiment;
                experiment.market.limit_rate = limit_rate;
                experiment.market.market_rate = market_rate;
                experiment.market.cancel_rate = cancel_rate;
                experiment.market.window = window;
                experiment.market.initial_price = 10'000;
                experiment.children = sizes(children);
                experiment.child_interval = child_interval;
                experiment.warmup = warmup;
                experiment.repetitions = repetitions;
                experiment.seed = seed;
                const std::vector<MetaorderImpactPoint> points = measureMetaorderImpact(experiment);
                py::dict out;
                out["size"] = column<std::int64_t>(points, [](const MetaorderImpactPoint &p)
                                                   { return p.size; });
                out["impact"] = column<double>(points, [](const MetaorderImpactPoint &p)
                                               { return p.impact; });
                out["standard_error"] = column<double>(points, [](const MetaorderImpactPoint &p)
                                                       { return p.standard_error; });
                out["samples"] = column<std::int64_t>(points, [](const MetaorderImpactPoint &p)
                                                      { return p.samples; });
                return out;
            },
            py::arg("limit_rate"), py::arg("market_rate"), py::arg("cancel_rate"), py::arg("window"), py::arg("children"),
            py::arg("child_interval") = 1.0, py::arg("warmup") = 100.0, py::arg("repetitions") = 200, py::arg("seed") = 1,
            "Buys each metaorder size by unit market orders, `child_interval` seconds\n"
            "apart, in a zero-intelligence market, and returns the mean rise of the mid in\n"
            "excess of the same market without the metaorder. This model does NOT\n"
            "reproduce the square-root law.");

        m.def(
            "fit_power_law",
            [](const DoubleArray &x, const DoubleArray &y)
            {
                const PowerLawFit fit = fitPowerLaw(view(x), view(y));
                py::dict out;
                out["prefactor"] = fit.prefactor;
                out["exponent"] = fit.exponent;
                return out;
            },
            py::arg("x"), py::arg("y"), "y = prefactor * x^exponent, by least squares on logarithms.");

        m.def(
            "impact_trading_cost",
            [](const DoubleArray &trades, const DoubleArray &kernel, double exponent)
            { return impactTradingCost(view(trades), view(kernel), exponent); },
            py::arg("trades"), py::arg("kernel"), py::arg("exponent") = 1.0,
            "Cost of a sequence of signed trades in a transient impact model with decay\n"
            "kernel G(0), G(1), ... and impact |q|^exponent. Negative for a round trip:\n"
            "the model can be manipulated.");
    }

} // namespace

PYBIND11_MODULE(quantmicrostructure, m)
{
    m.doc() = "quantMicrostructure: order book, matching engine, flow simulators, "
              "execution and market-making models";

    const BuildInfo &info = build_info();
    m.attr("__version__") = std::string(info.version);

    m.def(
        "build_info",
        []()
        {
            const BuildInfo &b = build_info();
            py::dict out;
            out["version"] = std::string(b.version);
            out["git_sha"] = std::string(b.git_sha);
            return out;
        },
        "What to record next to a result to reproduce it: the version of the library\n"
        "and the commit it was built from.");

    bindStreams(m);
    bindSimulators(m);
    bindMeasures(m);
    bindImpact(m);
}
