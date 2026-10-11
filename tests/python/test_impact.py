import numpy as np
import pytest

import quantmicrostructure as qm


def test_propagator_is_recovered_from_signs_and_prices():
    signs = qm.simulate_order_splitting_signs(alpha=1.5, count=200_000, seed=2026)
    assert set(np.unique(signs)) == {-1.0, 1.0}
    truth = qm.power_law_propagator(gamma0=1.0, l0=1.0, beta=0.25, length=1000)
    prices = qm.propagator_prices(signs, truth, noise_sd=0.5, seed=3)
    fitted = qm.fit_propagator(signs, prices, length=60)
    assert np.allclose(fitted[:30], truth[:30], atol=0.04)


def test_price_is_diffusive_at_the_critical_exponent():
    gamma = 0.5
    critical = qm.critical_propagator_exponent(gamma)
    assert critical == 0.25
    signs = qm.simulate_order_splitting_signs(alpha=1.0 + gamma, count=100_000, seed=7)
    lags = np.array([20, 40, 80, 160, 320], dtype=np.uint64)

    def growth(beta):
        propagator = qm.power_law_propagator(1.0, 1.0, beta, 2000)
        diffusion = qm.price_diffusion(qm.propagator_prices(signs, propagator), lags)
        return qm.fit_power_law(lags.astype(float), diffusion)["exponent"]

    assert growth(critical - 0.15) > 1.15
    assert growth(critical) == pytest.approx(1.0, abs=0.15)
    assert growth(critical + 0.15) < 0.92


def test_kyle_lambda_on_simulated_auctions_and_on_a_tape():
    sample = qm.simulate_kyle(100.0, 2.0, 5.0, 100_000, seed=2026)
    flow = sample["insider_order"] + sample["noise_order"]
    fit = qm.linear_regression(flow, sample["price"] - 100.0)
    assert fit["slope"] == pytest.approx(0.2)
    assert fit["r_squared"] == pytest.approx(1.0)

    market = qm.simulate_zero_intelligence(2.0, 1.5, 0.2, 10, 2000.0, seed=2026)
    tape = qm.build_tape(market)
    bars = qm.signed_flow_bars(tape, interval=5.0)
    assert len(bars["flow"]) == len(bars["price_change"]) > 300
    on_tape = qm.kyle_lambda(tape, interval=5.0)
    assert on_tape["slope"] > 5 * on_tape["slope_se"]


def test_metaorder_impact_grows_with_size():
    result = qm.measure_metaorder_impact(
        limit_rate=2.0,
        market_rate=1.5,
        cancel_rate=0.2,
        window=10,
        children=np.array([4, 32], dtype=np.uint64),
        child_interval=0.5,
        warmup=50.0,
        repetitions=100,
        seed=1,
    )
    assert np.array_equal(result["size"], [4, 32])
    assert result["impact"][1] > result["impact"][0] > 0
    fit = qm.fit_power_law(result["size"].astype(float), result["impact"])
    assert 0.7 < fit["exponent"] < 1.3


def test_round_trip_cost():
    kernel = np.exp(-0.002 * np.arange(4000))
    round_trip = np.concatenate([np.full(500, 1 / 500), np.full(50, -1 / 50)])
    assert qm.impact_trading_cost(round_trip, kernel, exponent=1.0) > 0
    assert qm.impact_trading_cost(round_trip, kernel, exponent=0.5) < 0
