"""Generated NumPy EKFQ Jacobians (F and H).

Self-contained: imports numpy only. Do not edit by hand -- this
file is regenerated from onspeed_ekf/symbolic.py.

Callables:
    F(x, u, dt) -> (11, 11) numpy array  (predict Jacobian)
    H(x, u)     -> (3, 11) numpy array   (measurement Jacobian)

State  x = [q0,q1,q2,q3, bp,bq,br, z,vz,b_az, beta]
Input  u = [p,q,r, ax_raw,ay_raw,az_raw, tas, tasdot]
"""

# GENERATED FILE -- DO NOT EDIT.
# Regenerate with:  python -m onspeed_ekf.symbolic
# Source of truth:  onspeed_ekf/symbolic.py

import numpy as _np


def F(x, u, dt):
    x = _np.asarray(x, dtype=_np.float64)
    u = _np.asarray(u, dtype=_np.float64)
    q0, q1, q2, q3, bp, bq, br, z, vz, b_az, beta = x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7], x[8], x[9], x[10]
    p, q, r, ax_raw, ay_raw, az_raw, tas, tasdot = u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7]
    x0 = (1/2)*dt
    x1 = x0*(bp - p)
    x2 = x0*(bq - q)
    x3 = x0*(br - r)
    x4 = q1*x0
    x5 = q2*x0
    x6 = q3*x0
    x7 = -x1
    x8 = -x3
    x9 = -q0*x0
    x10 = -x2
    x11 = dt**2
    x12 = -ax_raw*q2 + ay_raw*q1 + az_raw*q0
    x13 = ax_raw*q3 + ay_raw*q0 - az_raw*q1
    x14 = ax_raw*q0 - ay_raw*q3 + az_raw*q2
    x15 = ax_raw*q1 + ay_raw*q2 + az_raw*q3
    x16 = -dt
    x17 = 2*dt
    x18 = 19.6133*dt/tas
    out = _np.zeros((11, 11), dtype=_np.float64)
    out[0, 0] = 1
    out[0, 1] = x1
    out[0, 2] = x2
    out[0, 3] = x3
    out[0, 4] = x4
    out[0, 5] = x5
    out[0, 6] = x6
    out[1, 0] = x7
    out[1, 1] = 1
    out[1, 2] = x8
    out[1, 3] = x2
    out[1, 4] = x9
    out[1, 5] = x6
    out[1, 6] = -x5
    out[2, 0] = x10
    out[2, 1] = x3
    out[2, 2] = 1
    out[2, 3] = x7
    out[2, 4] = -x6
    out[2, 5] = x9
    out[2, 6] = x4
    out[3, 0] = x8
    out[3, 1] = x10
    out[3, 2] = x1
    out[3, 3] = 1
    out[3, 4] = x5
    out[3, 5] = -x4
    out[3, 6] = x9
    out[4, 4] = 1
    out[5, 5] = 1
    out[6, 6] = 1
    out[7, 0] = -x11*x12
    out[7, 1] = -x11*x13
    out[7, 2] = x11*x14
    out[7, 3] = -x11*x15
    out[7, 7] = 1
    out[7, 8] = x16
    out[7, 9] = (1/2)*x11
    out[8, 0] = x12*x17
    out[8, 1] = x13*x17
    out[8, 2] = -x14*x17
    out[8, 3] = x15*x17
    out[8, 8] = 1
    out[8, 9] = x16
    out[9, 9] = 1
    out[10, 0] = q1*x18
    out[10, 1] = q0*x18
    out[10, 2] = q3*x18
    out[10, 3] = q2*x18
    out[10, 6] = dt
    out[10, 10] = 1
    return out


def H(x, u):
    x = _np.asarray(x, dtype=_np.float64)
    u = _np.asarray(u, dtype=_np.float64)
    q0, q1, q2, q3, bp, bq, br, z, vz, b_az, beta = x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7], x[8], x[9], x[10]
    p, q, r, ax_raw, ay_raw, az_raw, tas, tasdot = u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7]
    x0 = 19.6133*q2
    x1 = -19.6133*q3
    x2 = 19.6133*q0
    x3 = 19.6133*q1
    x4 = -x3
    x5 = -x2
    out = _np.zeros((3, 11), dtype=_np.float64)
    out[0, 0] = x0
    out[0, 1] = x1
    out[0, 2] = x2
    out[0, 3] = x4
    out[1, 0] = x4
    out[1, 1] = x5
    out[1, 2] = x1
    out[1, 3] = -x0
    out[1, 6] = -tas
    out[2, 0] = x5
    out[2, 1] = x3
    out[2, 2] = x0
    out[2, 3] = x1
    out[2, 5] = tas
    return out
