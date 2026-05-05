"""Percent-lift calculation and IAS-from-AOA inversion.

Mirrors the C++ implementation in
`software/Libraries/onspeed_core/src/aoa/PercentLift.cpp::ComputePercentLift`.
The percent-lift convention here is the "honest single-linear envelope"
formula:

    percent_lift = (body_angle - alpha_0) / (alpha_stall - alpha_0) * 100

clamped to [0, 99]. `alpha_0` is per-flap (from the calibration fit) and
is typically negative on aircraft with positive wing incidence — see the
body-angle admonition in `docs/site/docs/calibration/how-aoa-works.md`.
"""

from __future__ import annotations

import math

from .config import FlapSetpoints


def compute_percent_lift(aoa: float, fs: FlapSetpoints) -> float:
    """Honest single-linear envelope fraction, clamped to [0.0, 99.9].

    Mirrors `onspeed_core/aoa/PercentLift.cpp::ComputePercentLift` —
    returns whole-percent units (e.g. 47.3) so callers don't have to
    juggle a separate "tenths" scale.  Wire encoders (Frame builder
    here, BuildDisplayFrame in C++) scale ×10 and truncate to int for
    the `#1` wire's tenths field.

    When `alpha_stall` is uncalibrated (≤ `stallwarn_aoa`), use the
    synthetic ceiling `stallwarn * 100/90` so band edges still render
    in roughly the right places on a not-yet-fitted config.

    The 99.9 ceiling (rather than 99.0) is load-bearing for the wire
    encoding: the encoder multiplies by 10 and truncates, and the
    field saturates at 999 (never 1000) by convention.
    """
    alpha_stall = fs.alpha_stall
    if alpha_stall <= fs.stallwarn_aoa:
        alpha_stall = fs.stallwarn_aoa * 100.0 / 90.0
    span = alpha_stall - fs.alpha_0
    if span <= 0:
        return 0.0
    pct = (aoa - fs.alpha_0) / span * 100.0
    return max(0.0, min(99.9, pct))


def ias_from_aoa(aoa: float, fs: FlapSetpoints, n: float = 1.0) -> float:
    """Invert `AOA = K/IAS² + alpha_0` to get IAS for a given body angle.

    Multi-G version: `AOA - alpha_0` scales with `n`, so

        IAS = sqrt(n × K_fit / (AOA - alpha_0))

    Returns 0.0 if `k_fit` is unset (V1 configs) or the AOA is at/below
    `alpha_0`. Returns 999.0 (a "very fast" sentinel) if AOA sits
    barely above `alpha_0`.
    """
    if fs.k_fit <= 0:
        return 0.0
    margin = aoa - fs.alpha_0
    if margin <= 1e-3:
        return 999.0
    return math.sqrt(n * fs.k_fit / margin)
