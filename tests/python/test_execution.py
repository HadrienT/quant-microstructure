import numpy as np
import pytest

import quantmicrostructure as qm

MODEL = dict(sigma=0.95, gamma=2.5e-7, eta=2.5e-6)


def test_no_risk_aversion_is_twap():
    optimal = qm.almgren_chriss_schedule(1e6, 5.0, 5, **MODEL)
    twap = qm.twap_schedule(1e6, 5.0, 5)
    assert np.allclose(optimal["holdings"], twap["holdings"])
    assert np.allclose(twap["holdings"], [1e6, 8e5, 6e5, 4e5, 2e5, 0])
    assert optimal["tau"] == 1.0


def test_risk_aversion_front_loads_the_trades():
    schedule = qm.almgren_chriss_schedule(1e6, 5.0, 5, risk_aversion=2e-6, **MODEL)
    assert schedule["holdings"][0] == pytest.approx(1e6)
    assert schedule["holdings"][-1] == 0.0
    assert np.all(np.diff(schedule["trades"]) < 0)
    assert schedule["trades"].sum() == pytest.approx(1e6)


def test_frontier_is_decreasing_and_convex():
    aversions = np.geomspace(1e-8, 1e-4, 20)
    frontier = qm.efficient_frontier(1e6, 5.0, 20, epsilon=0.0625, risk_aversions=aversions, **MODEL)
    assert np.all(np.diff(frontier["variance"]) < 0)
    assert np.all(np.diff(frontier["expected"]) > 0)
    slopes = np.diff(frontier["expected"]) / np.diff(frontier["variance"])
    assert np.all(np.diff(slopes) < 0)


def test_simulated_shortfall_matches_the_formula():
    schedule = qm.almgren_chriss_schedule(1e6, 5.0, 5, risk_aversion=2e-6, **MODEL)
    cost = qm.schedule_cost(schedule["trades"], schedule["tau"], **MODEL)
    sample = qm.simulate_shortfall(
        schedule["trades"], schedule["tau"], epsilon=0.0, paths=100_000, seed=2026, **MODEL
    )
    standard_error = np.sqrt(cost["variance"] / len(sample))
    assert sample.mean() == pytest.approx(cost["expected"], abs=4 * standard_error)
    assert sample.var() == pytest.approx(cost["variance"], rel=0.03)


def test_vwap_and_pov_schedules():
    vwap = qm.vwap_schedule(1000.0, 5.0, np.array([3.0, 1, 1, 1, 4]))
    assert np.allclose(vwap["trades"], [300, 100, 100, 100, 400])
    pov = qm.pov_schedule(1000.0, 0.1, np.array([1000.0, 500, 4000, 1000]))
    assert np.allclose(pov["trades"], [100, 50, 400, 100])
    assert pov["holdings"][-1] == 350.0


def test_implementation_shortfall_adds_up():
    b = qm.implementation_shortfall(
        "buy",
        target=10,
        decision_price=100.0,
        arrival_price=100.5,
        final_price=103.0,
        fill_prices=np.array([101.0, 102.0]),
        fill_quantities=np.array([4.0, 2.0]),
        fees=0.7,
    )
    assert b["filled"] == 6
    assert (b["delay"], b["trading"], b["opportunity"]) == (3.0, 5.0, 12.0)
    assert b["total"] == pytest.approx(20.7)
    with pytest.raises(ValueError):
        qm.implementation_shortfall("hold", 1, 1.0, 1.0, 1.0, np.array([]), np.array([]))


def test_execute_schedule_in_the_simulator():
    market = dict(limit_rate=2.0, market_rate=1.5, cancel_rate=0.2, window=10)

    def cost_per_lot(slices):
        trades = qm.twap_schedule(60.0, 300.0, slices)["trades"]
        costs = []
        for seed in range(40):
            run = qm.execute_schedule("buy", trades, 300.0 / slices, seed=seed, **market)
            assert run["target"] == 60 and run["shortfall"]["filled"] == 60
            assert np.all(run["fills"]["sign"] == 1.0)
            assert not run["fills"]["maker"].any()
            costs.append(run["shortfall"]["trading"] / 60)
        return np.mean(costs)

    block, sliced = cost_per_lot(1), cost_per_lot(30)
    assert sliced > 0.5
    assert block > 1.3 * sliced
