"""Tiny envelope helpers for output-driven scenarios.

A scenario is a generator of `LiveSnapshot` values, one per 50 Hz tick.
These helpers compose timelines: hold a value for N seconds, ramp from
A to B over T seconds, smooth-step between values, etc.

Usage:
    def scenario():
        return chain(
            lambda: cruise(2.0),
            lambda: pull_to_stall(2.0),
            ...
        )

Each segment is itself a generator of LiveSnapshots — typically built
by interpolating a few state fields and inheriting the rest from the
previous tick.
"""

from __future__ import annotations
import math
from typing import Iterator, Iterable

DT = 0.020   # 50 Hz tick period (seconds)
TICK_HZ = 50


def n_ticks(seconds: float) -> int:
    return max(1, int(round(seconds * TICK_HZ)))


def lerp(a: float, b: float, u: float) -> float:
    return a + (b - a) * u


def smoothstep(u: float) -> float:
    """Hermite smoothstep: 0 at 0, 1 at 1, derivative 0 at both ends."""
    u = max(0.0, min(1.0, u))
    return u * u * (3.0 - 2.0 * u)


def chain(*segments: Iterable) -> Iterator:
    """Concatenate iterables into one stream."""
    for seg in segments:
        for x in seg:
            yield x


def hold(value, duration_s: float):
    """Yield `value` for `duration_s` seconds."""
    for _ in range(n_ticks(duration_s)):
        yield value


def ramp(callback, duration_s: float, ease: str = "linear"):
    """Yield values produced by `callback(u)` for u in [0, 1] over duration_s.

    ease: 'linear' or 'smooth' (smoothstep).
    """
    n = n_ticks(duration_s)
    for i in range(n):
        u = i / max(1, n - 1)
        if ease == "smooth":
            u = smoothstep(u)
        yield callback(u)


def add_t_offsets(stream: Iterator, t0: float = 0.0) -> Iterator:
    """Stamp absolute scenario time onto each LiveSnapshot."""
    t = t0
    for state in stream:
        # state is a dict-like with a 't' field; mutate in place.
        state.t = t
        t += DT
        yield state


def add_realistic_jitter(stream: Iterator,
                         lateral_g_amp: float = 0.04,
                         yaw_rate_amp: float = 1.0,
                         vertical_g_amp: float = 0.02,
                         pitch_amp: float = 0.3,
                         roll_amp: float = 0.4,
                         aoa_amp: float = 0.15,
                         ias_amp: float = 0.4,
                         seed: int = 1) -> Iterator:
    """Add small random jitter to fields where real flight would never
    sit perfectly still.  Keeps the ball wobbling, the AI a bit twitchy,
    and yaw rate non-zero in coordinated flight — i.e., looks like
    smoothed-but-not-frozen firmware emissions, not bench data.

    Defaults are calibrated to look like typical light-chop cruise: ball
    moves a few px peak-to-peak, AOA chevron breathes, AI rolls 1° or so.
    Caller can crank up amplitudes for turbulence or zero them for a
    clean section.
    """
    import random
    rng = random.Random(seed)

    # Two-pole low-pass on white noise: avoids the chatter looking like
    # uniform-random hash and instead gives a softer "drift" that real
    # filtered IMU output produces.  τ ≈ 0.4s on each axis.
    state = {"lat": 0.0, "yaw": 0.0, "vg": 0.0, "p": 0.0, "r": 0.0,
             "aoa": 0.0, "ias": 0.0}
    alpha = 0.05  # heavy smoothing — the firmware EMA is α≈0.1 on most channels

    def step(key, amp):
        target = rng.uniform(-amp, amp)
        state[key] = (1.0 - alpha) * state[key] + alpha * target
        return state[key]

    for s in stream:
        s.lateral_g  += step("lat", lateral_g_amp)
        s.yaw_rate   += step("yaw", yaw_rate_amp)
        s.vertical_g += step("vg",  vertical_g_amp)
        s.pitch      += step("p",   pitch_amp)
        s.roll       += step("r",   roll_amp)
        s.aoa        += step("aoa", aoa_amp)
        s.ias        += step("ias", ias_amp)
        yield s
