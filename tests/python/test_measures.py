import numpy as np
import pytest

import quantmicrostructure as qm


@pytest.fixture(scope="module")
def market():
    return qm.simulate_zero_intelligence(
        limit_rate=2.0,
        market_rate=1.5,
        cancel_rate=0.2,
        window=10,
        duration=2000.0,
        seed=2026,
    )


def test_stream_arrays_have_one_row_per_event(market):
    arrays = market.to_arrays()
    assert market.kind == "l3"
    assert set(arrays) == {
        "ts",
        "ts_recv",
        "seq",
        "order_id",
        "price",
        "qty",
        "type",
        "side",
    }
    assert all(len(column) == len(market) for column in arrays.values())
    assert np.array_equal(arrays["seq"], np.arange(1, len(market) + 1))
    assert np.all(np.diff(arrays["ts"]) >= 0)
    assert set(np.unique(arrays["type"])) == {0, 1, 2}


def test_same_seed_same_stream():
    def run(seed):
        return qm.simulate_zero_intelligence(1.0, 1.0, 0.2, 10, 100.0, seed).to_arrays()

    first, second, other = run(1), run(1), run(2)
    assert all(np.array_equal(first[k], second[k]) for k in first)
    assert len(first["ts"]) != len(other["ts"]) or not np.array_equal(
        first["price"], other["price"]
    )


def test_aggregated_stream_gives_the_same_tape(market):
    levels = qm.aggregate(market)
    assert levels.kind == "l2"
    from_orders = qm.build_tape(market)
    from_levels = qm.build_tape(levels)
    for name in ("ts", "bid", "ask", "bid_qty", "ask_qty"):
        assert np.array_equal(from_orders.quotes()[name], from_levels.quotes()[name])
    assert np.array_equal(from_orders.trades()["price"], from_levels.trades()["price"])
    with pytest.raises(ValueError):
        qm.aggregate(levels)


def test_final_book_is_not_crossed(market):
    book = qm.final_book(market, max_levels=5)
    bids, asks = book["bids"], book["asks"]
    assert len(bids["price"]) == 5 and len(asks["price"]) == 5
    assert np.all(np.diff(bids["price"]) < 0)
    assert np.all(np.diff(asks["price"]) > 0)
    assert bids["price"][0] < asks["price"][0]
    assert np.all(bids["qty"] > 0)


def test_event_file_round_trip(market, tmp_path):
    path = str(tmp_path / "market.qmev")
    market.write(path)
    back = qm.read_event_file(path, "l3")
    original, read = market.to_arrays(), back.to_arrays()
    assert all(np.array_equal(original[k], read[k]) for k in original)
    with pytest.raises(ValueError):
        qm.read_event_file(path, "l4")


def test_spread_decomposition_adds_up(market):
    tape = qm.build_tape(market)
    d = qm.spread_decomposition(tape, horizon=1.0)
    assert d["trades"] > 1000
    assert d["effective"] == pytest.approx(d["realized"] + d["impact"])
    assert d["effective"] > 0
    assert qm.time_weighted_spread(tape) >= 1.0


def test_imbalance_predicts_the_next_move(market):
    bins = qm.imbalance_next_move(qm.build_tape(market), bins=5)
    probability = bins["probability_up"]
    assert probability[0] < 0.4 < 0.6 < probability[-1]
    assert np.all(np.diff(probability) > 0)


def test_response_function_starts_at_zero_and_is_positive(market):
    response = qm.response_function(qm.build_tape(market), max_lag=10)
    assert response[0] == 0.0
    assert np.all(response[1:] > 0)


def test_trade_signs_are_recovered_by_the_quote_rule(market):
    tape = qm.build_tape(market)
    true_signs = tape.trades()["sign"]
    assert qm.sign_agreement(qm.quote_rule_signs(tape), true_signs) == 1.0
    assert set(np.unique(true_signs)) == {-1.0, 1.0}


def test_roll_spread_is_recovered():
    stream = qm.simulate_roll(half_spread=4, trades=200_000, seed=2026)
    assert stream.kind == "trades"
    prices = qm.build_tape(stream).trades()["price"]
    assert qm.roll_spread(prices) == pytest.approx(8.0, abs=0.15)
    assert qm.roll_spread(np.array([0.0, 1.0, 2.0, 4.0, 7.0, 12.0])) is None


def test_signature_plot_and_two_scale_estimator():
    rng = np.random.default_rng(2026)
    n, omega = 200_000, 3.0
    efficient = np.concatenate([[0.0], np.cumsum(rng.standard_normal(n))])
    observed = efficient + omega * rng.standard_normal(n + 1)

    steps = np.array([1, 5, 50], dtype=np.uint64)
    clean = qm.signature_plot(efficient, steps)
    assert np.allclose(clean["variance"] / n, 1.0, atol=0.1)

    noisy = qm.signature_plot(observed, steps)
    bias = noisy["variance"] - n
    assert np.allclose(bias / (2 * noisy["increments"] * omega**2), 1.0, atol=0.1)

    assert qm.realized_variance(observed) / n > 18
    assert qm.two_scale_realized_variance(observed, 100) / n == pytest.approx(
        1.0, abs=0.05
    )
    assert qm.noise_variance(observed) == pytest.approx(omega**2 + 0.5, abs=0.2)


def test_hawkes_fit_recovers_the_parameters():
    horizon = 5000.0
    times = qm.simulate_hawkes(mu=1.0, alpha=2.0, beta=4.0, horizon=horizon, seed=2026)
    assert len(times) / horizon == pytest.approx(2.0, rel=0.1)

    fit = qm.fit_hawkes(times, horizon)
    assert fit["converged"]
    assert fit["mu"] == pytest.approx(1.0, rel=0.15)
    assert fit["branching_ratio"] == pytest.approx(0.5, abs=0.05)
    assert fit["log_likelihood"] >= qm.hawkes_log_likelihood(
        1.0, 2.0, 4.0, times, horizon
    )

    residuals = qm.hawkes_residuals(fit["mu"], fit["alpha"], fit["beta"], times)
    assert residuals.mean() == pytest.approx(1.0, abs=0.05)
    assert residuals.var() == pytest.approx(1.0, abs=0.1)


def test_kyle_lambda_is_the_regression_slope():
    equilibrium = qm.kyle_equilibrium(sigma_v=2.0, sigma_u=5.0)
    assert equilibrium == {"lambda": 0.2, "beta": 2.5}
    sample = qm.simulate_kyle(100.0, 2.0, 5.0, 200_000, seed=2026)
    flow = sample["insider_order"] + sample["noise_order"]
    slope = np.cov(sample["value"], flow)[0, 1] / flow.var()
    assert slope == pytest.approx(0.2, rel=0.03)


def test_glosten_milgrom_spread():
    quotes = qm.glosten_milgrom_quotes(90.0, 110.0, informed_share=0.3)
    assert quotes["ask"] - quotes["bid"] == pytest.approx(0.3 * 20.0)
    path = qm.simulate_glosten_milgrom(90.0, 110.0, 0.3, trades=2000, seed=8)
    value = 110.0 if path["value_is_high"] else 90.0
    assert path["price"][-1] == pytest.approx(value, abs=1e-3)
    assert np.all(path["bid"] <= path["ask"])


def test_queue_reactive_stream():
    size = np.arange(16, dtype=float)
    first = {
        "limit": np.where(size < 15, 1.0, 0.0),
        "cancel": 0.1 * size,
        "market": np.where(size > 0, 0.6, 0.0),
    }
    second = {
        "limit": np.where(size < 15, 1.2, 0.0),
        "cancel": 0.2 * size,
        "market": np.zeros(16),
    }
    pi = qm.queue_invariant_distribution(**first)
    assert pi.sum() == pytest.approx(1.0)

    stream = qm.simulate_queue_reactive(
        [first, second], theta=0.7, theta_reinit=0.3, duration=2000.0, seed=11
    )
    assert stream.kind == "l2"
    quotes = qm.build_tape(stream).quotes()
    two_sided = (quotes["bid_qty"] > 0) & (quotes["ask_qty"] > 0)
    assert np.all(quotes["bid"][two_sided] < quotes["ask"][two_sided])
    assert len(np.unique(quotes["mid"][two_sided])) > 10
