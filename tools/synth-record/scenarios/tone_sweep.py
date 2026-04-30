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
            s = base.copy()
            s.aoa = env.lerp(a0, a1, u)
            s.ias = wfb.ias_from_aoa(s.aoa, fs0)
            yield s

    onspeed_mid = 0.5 * (fs0.onspeed_fast_aoa + fs0.onspeed_slow_aoa)
    cruise_aoa  = fs0.ldmax_aoa - 2.0      # well below ldmax — silent
    above_stall = fs0.stallwarn_aoa + 1.5

    # Total: 0.5s settle + 4.5s low-pulse + 2.0s solid + 4.5s high-pulse
    #        + 0.5s buzz = 12.0s. That's longer than 10s — accept it for
    #        ear-check legibility; the spin/pip demos are the 10s ones.
    stream = env.chain(
        # Settle at cruise.
        aoa_at(cruise_aoa, 0.5),                                       # 0.0–0.5
        # Quick rise from cruise to L/Dmax (silent → first low-pulse tick).
        aoa_ramp(cruise_aoa, fs0.ldmax_aoa, 1.0),                      # 0.5–1.5
        # Low-tone pulsing region (L/Dmax → onspeed_fast).  Long dwell
        # so the PPS sweep from 1.5 → 8.2 is unmistakable.
        aoa_ramp(fs0.ldmax_aoa, fs0.onspeed_fast_aoa, 6.0),            # 1.5–7.5
        # ONSPEED solid tone — drift slowly across the on-speed window
        # so the bar keeps moving without leaving the solid-tone region.
        aoa_ramp(fs0.onspeed_fast_aoa, fs0.onspeed_slow_aoa, 2.0),     # 7.5–9.5
        # High-tone pulsing region (onspeed_slow → stall_warn).  Dwell
        # to hear PPS rise from 1.5 to 6.2 + carrier amp ramp 0.25→1.0.
        aoa_ramp(fs0.onspeed_slow_aoa, fs0.stallwarn_aoa, 4.5),        # 9.5–14.0
        # Slow ramp into stall buzz (20 PPS, full amp), then hold.
        aoa_ramp(fs0.stallwarn_aoa, above_stall, 1.0),                 # 14.0–15.0
        aoa_at(above_stall, 0.7),                                      # 15.0–15.7
    )
    yield from env.add_t_offsets(stream)
