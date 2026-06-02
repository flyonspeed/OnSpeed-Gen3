"""Python-twin mirrors of the firmware exp-map tests. The twin must integrate a
constant body rate to the closed-form angle and treat zero rate as an identity
step — matching the firmware predict() after the exponential-map change."""
import numpy as np
from onspeed_ekf import EKFQ, GRAVITY_MPS2

DT = 1.0 / 208.0
DEG2RAD = np.pi / 180.0
G = GRAVITY_MPS2


def _quat_yaw(q0, q1, q2, q3):
    return np.arctan2(2.0 * (q0 * q3 + q1 * q2), 1.0 - 2.0 * (q2 * q2 + q3 * q3))


def test_twin_exp_map_constant_rate_angle():
    ekf = EKFQ()
    ekf.init()
    rate = 30.0 * DEG2RAD
    for _ in range(208):
        ekf.predict(0.0, 0.0, rate, 0.0, 0.0, -G, 0.0, DT)
    s = ekf.x
    psi = _quat_yaw(s[0], s[1], s[2], s[3])
    assert abs(psi / DEG2RAD - 30.0) < 0.05   # exact step holds the angle
    n2 = s[0] ** 2 + s[1] ** 2 + s[2] ** 2 + s[3] ** 2
    assert abs(n2 - 1.0) < 1e-5


def test_twin_exp_map_zero_rate_identity():
    ekf = EKFQ()
    ekf.init(8.0 * DEG2RAD, -3.0 * DEG2RAD, 500.0)
    before = ekf.x.copy()
    ekf.predict(0.0, 0.0, 0.0, 0.0, 0.0, -G, 0.0, DT)
    after = ekf.x
    assert np.allclose(before[:4], after[:4], atol=1e-7)
