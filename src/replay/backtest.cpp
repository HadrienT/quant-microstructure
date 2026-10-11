#include "quantMicrostructure/replay/backtest.hpp"

#include "quantMicrostructure/book/level_book.hpp"
#include "quantMicrostructure/book/order_book.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace quantMicrostructure
{

    void Strategy::onFill(const Fill &, const MarketView &, Gateway &)
    {
    }

    namespace
    {

        enum class ActionKind : std::uint8_t
        {
            Limit,
            Market,
            Cancel
        };

        struct Action
        {
            Timestamp arrival{};
            ActionKind kind{};
            OrderId id{};
            Side side{};
            Price price{};
            Quantity qty{};
        };

        template <typename Book>
        Quote topOf(const Book &book, Timestamp ts)
        {
            Quote quote;
            quote.ts = ts;
            if (const auto bid = book.bestBid())
            {
                quote.bid = *bid;
                quote.bid_qty = book.levelQuantity(Side::Buy, *bid);
            }
            if (const auto ask = book.bestAsk())
            {
                quote.ask = *ask;
                quote.ask_qty = book.levelQuantity(Side::Sell, *ask);
            }
            return quote;
        }

        /// What every venue shares: the clock, the orders in flight, the
        /// account.
        class Session : public Gateway
        {
          public:
            Session(Strategy &strategy, const BacktestConfig &config, OrderId first_id)
                : strategy_(strategy),
                  next_id_(first_id)
            {
                if (config.latency < 0)
                {
                    throw std::invalid_argument("backtest: latency must not be negative");
                }
                result_.config = config;
            }

            OrderId submitLimit(Side side, Price price, Quantity qty) override
            {
                if (qty <= 0)
                {
                    throw std::invalid_argument("backtest: order quantity must be positive");
                }
                const OrderId id = next_id_++;
                pending_.push_back(Action{now_ + result_.config.latency, ActionKind::Limit, id, side, price, qty});
                ++result_.orders_sent;
                return id;
            }

            void submitMarket(Side side, Quantity qty) override
            {
                if (qty <= 0)
                {
                    throw std::invalid_argument("backtest: order quantity must be positive");
                }
                pending_.push_back(Action{now_ + result_.config.latency, ActionKind::Market, 0, side, 0, qty});
                ++result_.orders_sent;
            }

            void cancel(OrderId id) override
            {
                pending_.push_back(Action{now_ + result_.config.latency, ActionKind::Cancel, id, Side::Buy, 0, 0});
                ++result_.cancels_sent;
            }

          protected:
            MarketView view(const Quote &quote) const
            {
                return MarketView{now_, quote, result_.position};
            }

            void seeQuote(const Quote &quote)
            {
                if (quote.twoSided())
                {
                    result_.mark = quote.mid();
                }
            }

            /// Books a fill at once. The strategy is told by reportFills, once
            /// the venue has brought its book up to date: it must not requote
            /// on a book that does not yet show the trade that filled it.
            void recordFill(OrderId order, Side side, Price price, Quantity qty, bool maker)
            {
                const double notional = static_cast<double>(price) * static_cast<double>(qty);
                result_.position += side == Side::Buy ? qty : -qty;
                result_.cash += side == Side::Buy ? -notional : notional;
                result_.fees += notional * (maker ? result_.config.fees.maker : result_.config.fees.taker);
                result_.fills.push_back(Fill{now_, order, side, price, qty, maker});
            }

            void reportFills(const Quote &quote)
            {
                for (; reported_ < result_.fills.size(); ++reported_)
                {
                    const Fill fill = result_.fills[reported_];
                    strategy_.onFill(fill, view(quote), *this);
                }
            }

            BacktestResult finish()
            {
                result_.pnl = result_.cash + static_cast<double>(result_.position) * result_.mark - result_.fees;
                return std::move(result_);
            }

            Strategy &strategy_;
            BacktestResult result_;
            // Latency is constant, so arrival order is decision order.
            std::deque<Action> pending_;
            Timestamp now_{};

          private:
            OrderId next_id_;
            std::size_t reported_{0};
        };

        // ---- Replay of a recorded stream ----

        struct OwnOrder
        {
            OrderId id{};
            Side side{};
            Price price{};
            Quantity remaining{};
            /// Recorded quantity that must trade or leave before ours.
            double ahead{};
            /// Sequence number of the last event applied before ours arrived.
            SequenceNumber arrival_seq{};
            /// The book was cleared and is being rebuilt from a snapshot: our
            /// place in the queue is lost, and we go behind what the snapshot
            /// shows at our price.
            bool resync{false};
        };

        template <typename Book>
        class Replay : public Session
        {
          public:
            Replay(Strategy &strategy, const BacktestConfig &config)
                : Session(strategy, config, 1)
            {
            }

            BacktestResult run(std::span<const Event> events)
            {
                for (const Event &event : events)
                {
                    // Our orders that arrive by then meet the book as it was.
                    applyDue(event.ts);
                    now_ = event.ts;
                    onEvent(event);
                    last_seq_ = event.seq;
                    seeQuote(quote());
                    reportFills(quote());
                    strategy_.onMarket(view(quote()), *this);
                    // With no latency, a decision takes effect at once.
                    applyDue(now_);
                }
                return finish();
            }

          private:
            Quote quote() const { return topOf(book_, now_); }

            void applyDue(Timestamp until)
            {
                while (!pending_.empty() && pending_.front().arrival <= until)
                {
                    const Action action = pending_.front();
                    pending_.pop_front();
                    now_ = std::max(now_, action.arrival);
                    apply(action);
                    reportFills(quote());
                }
            }

            void apply(const Action &action)
            {
                if (action.kind == ActionKind::Cancel)
                {
                    std::erase_if(own_, [&](const OwnOrder &o)
                                  { return o.id == action.id; });
                    return;
                }
                // Trades on arrival against what is displayed on the other
                // side, best price first.
                Quantity remaining = action.qty;
                for (const BookLevel &level : book_.depth(opposite(action.side)))
                {
                    if (remaining == 0)
                    {
                        break;
                    }
                    if (action.kind == ActionKind::Limit)
                    {
                        const bool crosses = action.side == Side::Buy ? level.price <= action.price : level.price >= action.price;
                        if (!crosses)
                        {
                            break;
                        }
                    }
                    const Quantity traded = std::min(remaining, level.qty);
                    remaining -= traded;
                    recordFill(action.id, action.side, level.price, traded, false);
                }
                if (action.kind == ActionKind::Limit && remaining > 0)
                {
                    OwnOrder order;
                    order.id = action.id;
                    order.side = action.side;
                    order.price = action.price;
                    order.remaining = remaining;
                    order.ahead = static_cast<double>(book_.levelQuantity(action.side, action.price));
                    order.arrival_seq = last_seq_;
                    // Kept in the order in which they would trade.
                    const auto after = std::find_if(own_.begin(), own_.end(), [&](const OwnOrder &o)
                                                    { return o.side == order.side && (order.side == Side::Buy ? o.price < order.price : o.price > order.price); });
                    own_.insert(after, order);
                }
            }

            /// Gives `available` lots of recorded aggressive volume to one of
            /// our orders, as far as it goes.
            void fillOwn(OwnOrder &order, double available, double &taken)
            {
                const Quantity lots = std::min(order.remaining, static_cast<Quantity>(std::floor(available - taken + 1e-9)));
                if (lots <= 0)
                {
                    return;
                }
                order.remaining -= lots;
                taken += static_cast<double>(lots);
                recordFill(order.id, order.side, order.price, lots, true);
            }

            void dropFilled()
            {
                std::erase_if(own_, [](const OwnOrder &o)
                              { return o.remaining == 0; });
            }

            void onEvent(const Event &event);

            Book book_;
            std::vector<OwnOrder> own_;
            SequenceNumber last_seq_{};
            // Order-by-order streams: when each recorded order was added.
            std::unordered_map<OrderId, SequenceNumber> added_;
            // Aggregated streams: volume traded at a level since its last
            // Level event, which explains that much of its next decrease.
            std::map<std::pair<Side, Price>, Quantity> traded_;
        };

        template <>
        void Replay<LevelBook>::onEvent(const Event &event)
        {
            if (event.type == EventType::Trade)
            {
                const Side resting = opposite(event.side);
                const double volume = static_cast<double>(event.qty);
                double taken = 0.0;
                // A strategy that reacts in onFill only queues actions: the list
                // of our orders does not change under this loop.
                for (OwnOrder &order : own_)
                {
                    if (order.side != resting)
                    {
                        continue;
                    }
                    const bool better = resting == Side::Buy ? order.price > event.price : order.price < event.price;
                    if (better)
                    {
                        fillOwn(order, volume, taken);
                    }
                    else if (order.price == event.price)
                    {
                        const double consumed = std::min(order.ahead, volume);
                        order.ahead -= consumed;
                        fillOwn(order, volume - consumed, taken);
                    }
                }
                dropFilled();
                traded_[{resting, event.price}] += event.qty;
                return;
            }
            if (event.type == EventType::Level)
            {
                const double before = static_cast<double>(book_.levelQuantity(event.side, event.price));
                const auto traded = traded_.find({event.side, event.price});
                const double explained = traded == traded_.end() ? 0.0 : std::min(before, static_cast<double>(traded->second));
                if (traded != traded_.end())
                {
                    traded_.erase(traded);
                }
                const double queue = before - explained;
                const double cancelled = std::max(0.0, queue - static_cast<double>(event.qty));
                for (OwnOrder &order : own_)
                {
                    if (order.side != event.side || order.price != event.price)
                    {
                        continue;
                    }
                    if (order.resync)
                    {
                        order.ahead = static_cast<double>(event.qty);
                        order.resync = false;
                        continue;
                    }
                    if (cancelled > 0.0 && queue > 0.0)
                    {
                        if (result_.config.queue_model == QueueModel::ProRata)
                        {
                            order.ahead -= cancelled * order.ahead / queue;
                        }
                        else
                        {
                            const double behind = queue - order.ahead;
                            order.ahead -= std::max(0.0, cancelled - behind);
                        }
                    }
                    // No more can be ahead of us than is displayed.
                    order.ahead = std::clamp(order.ahead, 0.0, static_cast<double>(event.qty));
                }
            }
            else if (event.type == EventType::Clear)
            {
                traded_.clear();
                book_.apply(event);
                for (OwnOrder &order : own_)
                {
                    order.ahead = 0.0;
                    order.resync = true;
                }
                return;
            }
            book_.apply(event);
        }

        template <>
        void Replay<OrderBook>::onEvent(const Event &event)
        {
            if (event.type != EventType::Add && event.type != EventType::Clear)
            {
                // A snapshot is the run of Add events that follows a Clear.
                for (OwnOrder &order : own_)
                {
                    order.resync = false;
                }
            }
            if (event.type == EventType::Add)
            {
                added_[event.order_id] = event.seq;
                for (OwnOrder &order : own_)
                {
                    if (order.resync && order.side == event.side && order.price == event.price)
                    {
                        order.ahead += static_cast<double>(event.qty);
                        order.arrival_seq = event.seq;
                    }
                }
            }
            else if (event.type == EventType::Cancel || event.type == EventType::Trade)
            {
                const auto recorded = book_.find(event.order_id);
                if (!recorded)
                {
                    throw std::invalid_argument("runReplay: the stream refers to an order that is not resting");
                }
                const SequenceNumber added = added_.at(event.order_id);
                const double volume = static_cast<double>(event.qty);
                double taken = 0.0;
                for (OwnOrder &order : own_)
                {
                    if (order.side != recorded->side)
                    {
                        continue;
                    }
                    const bool was_ahead = added <= order.arrival_seq;
                    if (order.price == recorded->price && was_ahead)
                    {
                        // It was ahead of us and leaves, by trading or not.
                        order.ahead = std::max(0.0, order.ahead - volume);
                    }
                    else if (event.type == EventType::Trade)
                    {
                        const bool better = recorded->side == Side::Buy ? order.price > recorded->price : order.price < recorded->price;
                        if (better || order.price == recorded->price)
                        {
                            // The aggressor would have met our order first.
                            fillOwn(order, volume, taken);
                        }
                    }
                }
                dropFilled();
                if (recorded->qty == event.qty)
                {
                    added_.erase(event.order_id);
                }
            }
            else if (event.type == EventType::Clear)
            {
                added_.clear();
                for (OwnOrder &order : own_)
                {
                    order.ahead = 0.0;
                    order.resync = true;
                }
            }
            book_.apply(event);
        }

        // ---- Inside the simulator ----

        class Simulation : public Session
        {
          public:
            Simulation(ZeroIntelligenceSimulator &simulator, Strategy &strategy, const BacktestConfig &config, std::vector<Event> *record)
                : Session(strategy, config, ZeroIntelligenceSimulator::FirstParticipantId),
                  simulator_(simulator),
                  record_(record)
            {
                result_.market_reacts = true;
                now_ = simulator_.now();
            }

            BacktestResult run(Timestamp end)
            {
                std::vector<Event> events;
                while (true)
                {
                    const bool has_action = !pending_.empty() && pending_.front().arrival <= end;
                    const Timestamp target = has_action ? std::max(pending_.front().arrival, simulator_.now()) : end;
                    events.clear();
                    const bool happened = simulator_.stepBefore(target, events);
                    now_ = simulator_.now();
                    if (happened)
                    {
                        keep(events);
                        makerFills(events);
                        seeQuote(quote());
                        reportFills(quote());
                        strategy_.onMarket(view(quote()), *this);
                        continue;
                    }
                    if (!has_action)
                    {
                        break;
                    }
                    const Action action = pending_.front();
                    pending_.pop_front();
                    apply(action);
                    reportFills(quote());
                }
                seeQuote(quote());
                return finish();
            }

          private:
            Quote quote() const { return topOf(simulator_.book(), now_); }

            void apply(const Action &action)
            {
                std::vector<Event> events;
                if (action.kind == ActionKind::Cancel)
                {
                    if (resting_.erase(action.id) > 0)
                    {
                        simulator_.cancel(action.id, events);
                        keep(events);
                    }
                    return;
                }
                SubmitResult result;
                if (action.kind == ActionKind::Limit)
                {
                    result = simulator_.submitLimit(action.id, action.side, action.price, action.qty, events);
                    if (result.resting > 0)
                    {
                        resting_.emplace(action.id, action.side);
                    }
                }
                else
                {
                    result = simulator_.submitMarket(action.side, action.qty, events);
                }
                keep(events);
                for (const Event &event : events)
                {
                    if (event.type == EventType::Trade)
                    {
                        recordFill(action.id, action.side, event.price, event.qty, false);
                    }
                }
                // Our own resting orders may have been on the other side.
                makerFills(events);
            }

            void makerFills(const std::vector<Event> &events)
            {
                for (const Event &event : events)
                {
                    if (event.type != EventType::Trade)
                    {
                        continue;
                    }
                    const auto own = resting_.find(event.order_id);
                    if (own == resting_.end())
                    {
                        continue;
                    }
                    const Side side = own->second;
                    if (!simulator_.book().find(event.order_id))
                    {
                        resting_.erase(own);
                    }
                    recordFill(event.order_id, side, event.price, event.qty, true);
                }
            }

            void keep(const std::vector<Event> &events)
            {
                if (record_ != nullptr)
                {
                    record_->insert(record_->end(), events.begin(), events.end());
                }
            }

            ZeroIntelligenceSimulator &simulator_;
            std::vector<Event> *record_;
            std::unordered_map<OrderId, Side> resting_;
        };

    } // namespace

    BacktestResult runReplay(std::span<const Event> events, StreamKind kind, Strategy &strategy, const BacktestConfig &config)
    {
        switch (kind)
        {
            case StreamKind::OrderByOrder:
                return Replay<OrderBook>(strategy, config).run(events);
            case StreamKind::Aggregated:
                return Replay<LevelBook>(strategy, config).run(events);
            case StreamKind::TradesOnly:
                break;
        }
        throw std::invalid_argument("runReplay: a trades-only stream has no book to trade against");
    }

    BacktestResult runSimulation(ZeroIntelligenceSimulator &simulator, Timestamp end, Strategy &strategy, const BacktestConfig &config, std::vector<Event> *record)
    {
        return Simulation(simulator, strategy, config, record).run(end);
    }

} // namespace quantMicrostructure
