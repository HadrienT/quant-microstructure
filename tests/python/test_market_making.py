import numpy as np
import pytest

import quantmicrostructure as qm

ARTICLE = dict(sigma=2.0, gamma=0.1, k=1.5, A=140.0)
TICK_SCALE = dict(sigma=0.3, gamma=0.5, k=4.0, A=0.2)
MARKET = dict(limit_rate=2.0, market_rate=1.5, cancel_rate=0.2, window=10)


def test_quotes_are_symmetric_when_flat_and_lean_against_the_inventory():
    flat = qm.avellaneda_stoikov_quotes(0.0, time_left=0.5, **ARTICLE)
    assert flat["bid"] == flat["ask"]
    assert flat["reservation_shift"] == 0.0
    long = qm.avellaneda_stoikov_quotes(3.0, time_left=0.5, **ARTICLE)
    assert long["reservation_shift"] == pytest.approx(-3 * 0.1 * 4 * 0.5)
    assert long["ask"] < flat["ask"] == flat["bid"] < long["bid"]
    assert long["bid"] + long["ask"] == pytest.approx(long["spread"])

    glft_flat = qm.glft_approximate_quotes(0.0, **TICK_SCALE)
    glft_long = qm.glft_approximate_quotes(3.0, **TICK_SCALE)
    assert glft_flat["bid"] == glft_flat["ask"]
    assert glft_long["ask"] < glft_flat["ask"] < glft_long["bid"]


def test_exact_glft_quotes_are_close_to_the_approximation():
    params = dict(sigma=0.3, gamma=0.05, k=0.3, A=0.9)
    exact = qm.glft_exact_quotes(inventory_limit=30, time_left=3000.0, **params)
    assert np.array_equal(exact["inventory"], np.arange(-30, 31))
    assert np.isinf(exact["bid"][-1]) and np.isinf(exact["ask"][0])
    for q in (-5, 0, 5):
        approx = qm.glft_approximate_quotes(float(q), **params)
        assert exact["bid"][q + 30] == pytest.approx(approx["bid"], abs=0.01)
        assert exact["ask"][q + 30] == pytest.approx(approx["ask"], abs=0.01)


def test_inventory_strategy_has_less_risk_than_the_symmetric_one():
    inventory = qm.simulate_quoting("inventory", paths=5000, seed=1, **ARTICLE)
    symmetric = qm.simulate_quoting("symmetric", paths=5000, seed=1, **ARTICLE)
    assert inventory["pnl"].std() < 0.6 * symmetric["pnl"].std()
    assert inventory["final_inventory"].std() < 0.5 * symmetric["final_inventory"].std()
    assert (
        0.9 * symmetric["pnl"].mean()
        < inventory["pnl"].mean()
        < symmetric["pnl"].mean()
    )
    with pytest.raises(ValueError):
        qm.simulate_quoting("random", paths=1, seed=1, **ARTICLE)


def test_market_maker_in_the_simulator():
    report = qm.market_maker_simulation(
        duration=600.0, seed=2026, inventory_limit=5, **MARKET, **TICK_SCALE
    )
    fills = report["fills"]
    assert len(fills["ts"]) > 100
    assert fills["maker"].all()
    assert report["position"] == int((fills["sign"] * fills["qty"]).sum())
    assert abs(np.cumsum(fills["sign"] * fills["qty"])).max() <= 5
    d = report["decomposition"]
    assert d["total"] == pytest.approx(report["pnl"])
    assert d["spread"] + d["inventory"] - d["fees"] == pytest.approx(report["pnl"])
    assert report["assumptions"]["market_reacts"] is True
    assert np.allclose(report["markouts"]["horizon"], [0.0, 1.0, 10.0])
    assert report["markouts"]["mean"][2] < report["markouts"]["mean"][0]


def test_skew_reduces_the_risk_in_the_simulator():
    def spread_of_outcomes(skew):
        runs = [
            qm.market_maker_simulation(
                duration=400.0,
                seed=s,
                skew=skew,
                inventory_limit=30,
                **MARKET,
                **TICK_SCALE,
            )
            for s in range(20)
        ]
        return np.std([r["position"] for r in runs]), np.std([r["pnl"] for r in runs])

    skewed, unskewed = spread_of_outcomes(True), spread_of_outcomes(False)
    assert skewed[0] < 0.3 * unskewed[0]
    assert skewed[1] < 0.6 * unskewed[1]


def test_replay_result_is_shown_with_its_assumptions():
    recording = qm.aggregate(
        qm.simulate_zero_intelligence(duration=900.0, seed=11, **MARKET)
    )
    reports = {
        (latency, model): qm.market_maker_replay(
            recording,
            latency=latency,
            queue_model=model,
            inventory_limit=5,
            **TICK_SCALE,
        )
        for latency in (0.0, 2.0)
        for model in ("pessimistic", "pro_rata")
    }
    base = reports[(0.0, "pessimistic")]
    assert base["assumptions"] == {
        "latency": 0.0,
        "queue_model": "pessimistic",
        "maker_fee": 0.0,
        "taker_fee": 0.0,
        "market_reacts": False,
    }
    fill_counts = {key: len(r["fills"]["ts"]) for key, r in reports.items()}
    assert fill_counts[(0.0, "pessimistic")] > 20
    assert len(set(fill_counts.values())) > 2
    for report in reports.values():
        assert report["decomposition"]["total"] == pytest.approx(report["pnl"])
    with pytest.raises(ValueError):
        qm.market_maker_replay(recording, queue_model="optimistic", **TICK_SCALE)
