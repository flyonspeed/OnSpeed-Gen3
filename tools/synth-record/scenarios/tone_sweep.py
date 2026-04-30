"""Tone-sweep verification (10s).

Sweeps AOA through every tone region so the audio engine can be
ear-checked against the firmware's expected behavior:

  0.0 – 1.5s : cruise (AOA below ldmax)         → silent
  1.5 – 4.0s : ramp through fast-tone region    → low-pitch pulsing,
                                                  pps speeding up
  4.0 – 5.5s : hold at ONSPEED                  → solid low-pitch tone
  5.5 – 8.0s : ramp through slow-tone region    → high-pitch pulsing
  8.0 – 10s  : hold past stall warn             → 20 pps stall buzz

Flaps stay at 0 throughout so the band edges don't shift mid-clip.
"""

from __future__ import annotations
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from live_snapshot import LiveSnapshot
import _envelopes as env
import wire_frame_builder as wfb

DEFAULT_CFG = pathlib.Path.home() / "Downloads/onspeed2_latest.cfg"


def scenario():
    cfg = wfb.load_flap_setpoints(DEFAULT_CFG)
    fs0 = wfb.setpoints_for_flap(0, cfg)

    base = LiveSnapshot(pitch=4.0, vertical_g=1.0, palt=2000.0, oat=15)

    def aoa_at(value: float, duration: float):
        for _ in range(env.n_ticks(duration)):
            s = base.copy()
            s.aoa = value
            s.ias = wfb.ias_from_aoa(value, fs0)
            yield s

    def aoa_ramp(a0: float, a1: float, duration: float, ease: str = "smooth"):
        n = env.n_ticks(duration)
        for i in range(n):
            u = i / max(1, n - 1)
            if ease == "smooth":
                u = env.smoothstep(u)
            elif ease == "out_pow":
                # Fast at the start, slows toward the end — for the
                # high-AOA "can't stop pulling" feeling.
                u = env.ease_out_pow(u, power=2.5)
            s = base.copy()
            s.aoa = env.lerp(a0, a1, u)
            s.ias = wfb.ias_from_aoa(s.aoa, fs0)
            yield s

    cruise_aoa = fs0.ldmax_aoa - 2.0      # well below ldmax — silent
    # 99% lift on this aircraft: alpha_0 + 0.99 * (alpha_stall - alpha_0)
    # = -3.72 + 0.99 * 14.03 = ~10.17° (just under alpha_stall = 10.31).
    # Pushing AOA above alpha_stall would saturate at 99 and stop moving;
    # ramping to alpha_stall + 1.5° gives a margin past 99 to demonstrate
    # the saturation cleanly.
    deep_stall = fs0.alpha_stall + 1.5    # 11.81°, 99% saturated

    # Total: 0.5s settle + 1.0s rise + 6.0s low-pulse + 2.0s solid +
    #        4.5s high-pulse + 1.5s ramp into deep stall + 1.0s hold.
    #        15.5s total.
    # Pacing intent: fast off cruise, then progressively slower as AOA
    # climbs into the high-pulse and stall regions — the "I shouldn't be
    # doing this but I can't stop pulling" feel.  Lenny needs time to
    # parse what's happening in the slow-tone and stall-warn regions,
    # so those segments use ease_out_pow which holds the high end longer.
    stream = env.chain(
        # Settle at cruise.
        aoa_at(cruise_aoa, 0.5),                                       # 0.0–0.5
        # Quick rise from cruise to L/Dmax (silent → first low-pulse tick).
        aoa_ramp(cruise_aoa, fs0.ldmax_aoa, 1.0),                      # 0.5–1.5
        # Low-tone pulsing region (L/Dmax → onspeed_fast).  Smooth ramp,
        # PPS sweep 1.5 → 8.2 is unmistakable over 5 s.
        aoa_ramp(fs0.ldmax_aoa, fs0.onspeed_fast_aoa, 5.0),            # 1.5–6.5
        # ONSPEED solid tone — drift slowly across the on-speed window.
        aoa_ramp(fs0.onspeed_fast_aoa, fs0.onspeed_slow_aoa, 2.0),     # 6.5–8.5
        # High-tone pulsing region (onspeed_slow → stall_warn).  Use
        # ease_out_pow so the early part of the band sweeps quickly
        # (PPS already rising) and the LATE part — closer to stall_warn,
        # where amp also ramps 0.25 → 1.0 — slows down for emphasis.
        aoa_ramp(fs0.onspeed_slow_aoa, fs0.stallwarn_aoa, 6.0,
                 ease="out_pow"),                                      # 8.5–14.5
        # Stall buzz.  ease_out_pow again so the entry into the buzz
        # has dramatic hesitation right at the threshold and then we
        # creep past alpha_stall into 99% saturation.
        aoa_ramp(fs0.stallwarn_aoa, deep_stall, 3.0,
                 ease="out_pow"),                                      # 14.5–17.5
        aoa_at(deep_stall, 1.0),                                       # 17.5–18.5
    )
    yield from env.add_t_offsets(stream)
