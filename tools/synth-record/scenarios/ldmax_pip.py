"""L/Dmax pip-movement demo (10s).

What this shows:

  1. The pilot is "flying the chevron" — holding a steady percent-lift
     reading by adjusting the stick as the aircraft state changes.
  2. The pilot smoothly deploys flaps 0 → 33 in a single continuous
     pull on the flap lever.
  3. Two things happen on the indexer simultaneously:
     (a) the L/Dmax pip slides up the indexer (because alpha_0 drops
         as flaps deploy → pip's percent rises), via the firmware's
         lever-interpolated `ComputeDisplayPctAnchors`.
     (b) the chevron *stays put* relative to the pip because the pilot
         is holding a constant percent — this is what holding angle
         of attack feels like when the wing's alpha_0 is shifting
         under you.
  4. AOA naturally drops a few degrees during the deployment, because
     flaps add lift coefficient at a given body angle.  IAS also bleeds
     because IAS = sqrt(K / (AOA - alpha_0)) drops as alpha_0 falls.

Why "constant percent-lift" instead of "constant body angle":

  - In real flight, the pilot reads the indexer.  The chevron position
    is the operational signal; the *body angle* number is buried in
    the calibration.  A pilot deploying flaps doesn't try to hold a
    body angle — they look at the chevron and trim/pull/relax to keep
    it where they want.
  - That makes percent-lift the natural invariant during deployment.
    Holding it constant means AOA must shift to match the new flap's
    alpha_0/alpha_stall envelope.  This is exactly what real pilots do.

Timeline:
  0.0 – 1.5s : flaps 0 (lever fully up).  AOA 3.57° → 52% lift,
                fast-tone region for clean (low-pulse).
  1.5 – 8.0s : single smooth lever sweep from flaps 0's pot value
                to flaps 33's pot value (6.5 seconds).
                AOA drops smoothly: 3.57° → 1.60° to hold 52% lift.
                IAS bleeds: 84 kt → 69 kt.
  8.0 – 10s  : flaps 33 (lever fully down).  AOA 1.60°, IAS 69 kt,
                still 52% lift — chevron has stayed at the same
                position on the indexer the whole time, but the L/Dmax
                pip has slid up to almost meet it.
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
    fs0  = wfb.setpoints_for_flap(0,  cfg)
    fs33 = wfb.setpoints_for_flap(33, cfg)

    # Pilot is holding a constant percent-lift = 52% (mid fast-tone for
    # clean — between L/Dmax 50% and OnSpeed-fast ~52% for flap 0).
    # As flaps deploy and alpha_0 drops, the AOA the pilot must hold
    # drops too, in lock-step.  IAS follows from the K/IAS² + alpha_0
    # fit applied to the new flap's parameters.
    HOLD_PCT = 52

    def aoa_for_pct(fs):
        span = fs.alpha_stall - fs.alpha_0
        return fs.alpha_0 + (HOLD_PCT / 100.0) * span

    aoa_clean = aoa_for_pct(fs0)         # 3.57°
    aoa_full  = aoa_for_pct(fs33)        # 1.60°
    ias_clean = wfb.ias_from_aoa(aoa_clean, fs0)    # ~84 kt
    ias_full  = wfb.ias_from_aoa(aoa_full,  fs33)   # ~69 kt

    base = LiveSnapshot(
        pitch=5.0, roll=0.0, yaw_rate=0.0,
        vertical_g=1.0, lateral_g=0.0, palt=2000.0, vsi=-200.0,
        oat=15, flight_path=-2.5,
    )

    def hold(lever_raw: int, aoa: float, ias: float, duration: float):
        for _ in range(env.n_ticks(duration)):
            s = base.copy()
            s.aoa       = aoa
            s.ias       = ias
            s.lever_raw = lever_raw
            yield s

    def sweep(lever_from: int, lever_to: int,
              aoa_from: float, aoa_to: float,
              ias_from: float, ias_to: float,
              duration: float):
        n = env.n_ticks(duration)
        for i in range(n):
            u = env.smoothstep(i / max(1, n - 1))
            s = base.copy()
            s.aoa       = env.lerp(aoa_from, aoa_to, u)
            s.ias       = env.lerp(ias_from, ias_to, u)
            s.lever_raw = int(round(env.lerp(lever_from, lever_to, u)))
            yield s

    stream = env.chain(
        hold(fs0.pot_value,  aoa_clean, ias_clean, 1.5),                          # 0.0–1.5s
        sweep(fs0.pot_value, fs33.pot_value,
              aoa_clean,     aoa_full,
              ias_clean,     ias_full,
              6.5),                                                                # 1.5–8.0s
        hold(fs33.pot_value, aoa_full,  ias_full,  2.0),                          # 8.0–10.0s
    )
    stream = env.add_realistic_jitter(stream, seed=7)
    yield from env.add_t_offsets(stream)
