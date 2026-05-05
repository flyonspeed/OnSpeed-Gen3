"""Vac's actual stall break, synthetically extended into a developed
spin → spin-recovery cue → recovery.

The architectural point: a `LiveSnapshot` is a `LiveSnapshot` regardless
of whether it came from a real log row or a synthetic envelope.  This
scenario chains both:

  Phase 1 (REAL) — log replay from t=2873 to t=2877.58 of Vac's actual
  flight (`vac_log.csv`).  Vac flies into the stall_warn region in the
  high-pulse audio band, AOA climbs through 15° and crosses stall_warn
  (16.48°) at ~t=2875.4, hits 19.83° at t=2877.58.  This is real
  flying, captured by his Gen2 box, smoothed for the Gen3 ball/AI.

  Phase 2 (SYNTHETIC) — picks up from Vac's last log state and pushes
  forward into a developed left spin (his roll was already -4°
  dropping, so we extend the natural left-wing-down departure).
  Yaw_rate climbs from his -1.4°/s through the SpinDetector's 20°/s
  gate up to a plateau of -80°/s, AOA holds well above his fSTALLAOA
  (alpha_stall = 17.98° from his V1 config heuristic), vertical_g
  drops to ~0.4, lateral_g builds.

  Phase 3 (SYNTHETIC RECOVERY) — anti-yaw rudder, AOA un-stalls
  (drops below alpha_stall), cue extinguishes, dive pull-out.

The seam between phases is smoothed: the synthetic Phase 2 start
takes Vac's last log state as its initial values, so there's no step
discontinuity.
"""

from __future__ import annotations
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from live_snapshot import LiveSnapshot
import _envelopes as env
import wire_frame_builder as wfb
from scenarios.from_log import scenario_from_log


VAC_LOG = pathlib.Path.home() / "Downloads/vac_log.csv"
VAC_CFG = pathlib.Path.home() / "Downloads/vac_config.cfg"

# Real-flight window: from 2 seconds before AOA crosses ldmax (t=2859.76)
# through the stall break.  We cut OFF Vac's real recovery and substitute
# a synthetic spin development at t_handoff.
LOG_T_START   = 2857.0
LOG_T_HANDOFF = 2877.58   # last real tick — wing dropping, yaw building
LOG_T_LEN     = LOG_T_HANDOFF - LOG_T_START   # 20.58 seconds


def scenario():
    cfg = wfb.load_flap_setpoints(VAC_CFG)
    fs0 = cfg[0]   # Vac flying clean

    # --- Phase 1: real-flight log replay through stall onset ---
    log_states = list(scenario_from_log(
        log_path=VAC_LOG,
        cfg_path=VAC_CFG,
        t_start_s=LOG_T_START,
        t_end_s=LOG_T_HANDOFF,
    ))
    if not log_states:
        raise RuntimeError("vac log produced no ticks — check timestamps")

    # Snapshot Vac's last log state so the synthetic phase picks up
    # without discontinuity.  His actual values at t=2877.58:
    #   aoa≈19.83  ias≈46.2  pitch≈18.5  roll≈-4.1  yaw≈-1.4
    #   vG≈1.15   latG≈-0.16  flap=0
    last = log_states[-1].copy()

    # --- Phase 2: synthetic spin development ---
    # SpinDetector's gate is `aoa > fSTALLAOA`.  Vac's V1 config heuristic
    # set alpha_stall = stallwarn + 1.5 = 16.48 + 1.5 = 17.98°.  His real
    # AOA at handoff (19.83°) is already above that, so the cue should
    # latch as soon as the τ=1s yaw filter saturates above 20°/s.
    #
    # Real GA spin yaw rates: 60-180 deg/s sustained.  Vac was at -1.4
    # deg/s and dropping a wing, so we extend in the LEFT spin direction
    # (negative yaw).  Cue value will be +1 (press RIGHT rudder).
    yaw_plateau    = -80.0
    aoa_in_spin    = last.aoa + 0.5     # ~20.3°, well past alpha_stall
    aoa_recovered  = fs0.onspeed_slow_aoa  # 13.84° — well below alpha_stall

    base = LiveSnapshot(
        palt=last.palt,
        oat=last.oat,
        flap_deg=0,
        lever_raw=last.lever_raw,
    )

    def smooth_to(field_pairs: dict, duration: float, ease: str = "smooth"):
        n_ticks = env.n_ticks(duration)
        for i in range(n_ticks):
            u = i / max(1, n_ticks - 1)
            us = env.smoothstep(u) if ease == "smooth" else u
            snap = base.copy()
            for field, (a, b) in field_pairs.items():
                setattr(snap, field, env.lerp(a, b, us))
            yield snap

    # Phase 2a: spin onset — yaw builds, roll deepens, vG drops.
    # 1.0s — fast enough to feel the autorotation start.
    #
    # Sign convention (CRITICAL — easy to get wrong).  See
    # local-plans/LATERAL_G_CONVENTION.md for the full chain.
    #   This scenario is a LEFT spin: yaw_rate < 0 (nose-left).
    #   Body-frame lateral_g is NEGATIVE — the airframe is being
    #     pulled leftward by centripetal force.
    #   Wire `lateralG` carries body-frame as-is (PR #386 / v4.23).
    #   M5 SerialRead::SerialProcess negates locally for ball-frame
    #     display: Slip = -LateralG × 850.  Negative body-frame →
    #     positive Slip → ball drawn RIGHT of center, matching the
    #     ball-lag-behind-airframe physics for a left spin.
    #   Cue: -sign(yaw_rate) = +1 → press RIGHT rudder (anti-yaw).
    spin_onset = smooth_to({
        "aoa":         (last.aoa,        aoa_in_spin),
        "ias":         (last.ias,        wfb.ias_from_aoa(aoa_in_spin, fs0, n=0.5)),
        "pitch":       (last.pitch,      6.0),
        "roll":        (last.roll,       -25.0),
        "yaw_rate":    (last.yaw_rate,   -50.0),
        "vertical_g":  (last.vertical_g, 0.6),
        "lateral_g":   (last.lateral_g,  -0.30),   # body-frame: -ve = leftward airframe accel = LEFT spin
    }, duration=1.0)

    # Phase 2b: yaw climbs to plateau, roll deepens further, vG settles low.
    spin_developing = smooth_to({
        "aoa":         (aoa_in_spin,    aoa_in_spin),
        "ias":         (wfb.ias_from_aoa(aoa_in_spin, fs0, n=0.5),
                        wfb.ias_from_aoa(aoa_in_spin, fs0, n=0.4)),
        "pitch":       (6.0,            -8.0),
        "roll":        (-25.0,          -50.0),
        "yaw_rate":    (-50.0,          yaw_plateau),
        "vertical_g":  (0.6,            0.4),
        "lateral_g":   (-0.30,          -0.40),
    }, duration=1.5)

    # Phase 2c: developed spin, cue actively displayed.
    spin_hold = smooth_to({
        "aoa":         (aoa_in_spin,   aoa_in_spin - 0.1),
        "ias":         (wfb.ias_from_aoa(aoa_in_spin, fs0, n=0.4),
                        wfb.ias_from_aoa(aoa_in_spin, fs0, n=0.4)),
        "pitch":       (-8.0,          -10.0),
        "roll":        (-50.0,         -55.0),
        "yaw_rate":    (yaw_plateau,   yaw_plateau),
        "vertical_g":  (0.4,           0.4),
        "lateral_g":   (-0.40,         -0.40),
    }, duration=2.0)

    # --- Phase 3: synthetic recovery ---
    # Anti-yaw rudder (right), forward stick.  Yaw drops, AOA un-stalls,
    # dive pull-out, IAS rebuilds.  Lateral accel decays to zero as the
    # spin stops.
    recover = smooth_to({
        "aoa":         (aoa_in_spin - 0.1, aoa_recovered),
        "ias":         (wfb.ias_from_aoa(aoa_in_spin, fs0, n=0.4),
                        wfb.ias_from_aoa(aoa_recovered, fs0, n=1.0)),
        "pitch":       (-10.0,             -18.0),
        "roll":        (-55.0,             -15.0),
        "yaw_rate":    (yaw_plateau,       -3.0),
        "vertical_g":  (0.4,               1.0),
        "lateral_g":   (-0.40,             -0.05),
    }, duration=1.0)

    pullout = smooth_to({
        "aoa":         (aoa_recovered,                aoa_recovered - 1.0),
        "ias":         (wfb.ias_from_aoa(aoa_recovered, fs0, n=1.0), 110.0),
        "pitch":       (-18.0,                        2.0),
        "roll":        (-15.0,                        0.0),
        "yaw_rate":    (-3.0,                         0.0),
        "vertical_g":  (1.0,                          1.5),
        "lateral_g":   (-0.05,                        0.0),
    }, duration=1.5)

    synthetic = env.chain(spin_onset, spin_developing, spin_hold,
                          recover, pullout)
    # Apply jitter to the synthetic phase only — the log-derived phase is
    # already realistic (smoothed real flight).
    synthetic = env.add_realistic_jitter(synthetic,
                                          lateral_g_amp=0.05,
                                          yaw_rate_amp=2.0,
                                          vertical_g_amp=0.03,
                                          aoa_amp=0.10,
                                          ias_amp=0.5,
                                          seed=43)

    # --- Stitch: log + synthetic, with absolute-time stamps ---
    # Both phases yield LiveSnapshots; just chain them.  add_t_offsets
    # restamps the whole sequence with monotonic t starting at 0.
    yield from env.add_t_offsets(env.chain(iter(log_states), synthetic))
