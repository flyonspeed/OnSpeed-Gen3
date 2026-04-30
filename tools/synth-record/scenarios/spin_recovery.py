"""Spin-recovery-cue demo (10s).

Vac's brief: clean (flaps 0), start on-speed, pull pitch back, wing
drops at the stall, spin develops and the spinRecoveryCue wire field
fires to point at the anti-yaw rudder pedal.  Then recovery: anti-yaw
rudder, AOA un-stalls, IAS ramps up FAST in the dive, cue
extinguishes.

Realistic IAS/G coupling (RV-10, N720AK, K=50960 deg·kt² for clean):

  IAS = sqrt(n · K / (AOA - alpha_0))

So at fixed AOA, IAS scales with sqrt(n).  At fixed n, IAS drops as
AOA climbs.  During a spin n is well below 1 (~0.4 g typical), and AOA
sits near or just past stall_warn (~8°), so IAS is low.  In the dive
recovery, AOA un-stalls and n builds (pulling out of dive), so IAS
climbs hard.

Timeline (10 seconds, smooth-step transitions throughout):
  0.0 – 2.0s : on-speed cruise, clean.  Solid 400 Hz tone.
  2.0 – 4.0s : smooth pitch-up.  AOA climbs from on-speed through
                stall-warn region.  Tones march up: solid → high-pulse
                → 20 pps stall buzz as we cross stallwarn.
  4.0 – 4.8s : stall break.  Right wing drops, roll builds to +30°,
                yaw rate begins climbing.  AOA *just* past stall_warn
                (~8.5°) — not deep post-stall.
  4.8 – 8.0s : developed right spin at stall AOA (~8.5°).  Yaw
                plateaus ~80°/s.  SpinDetector latches; cue=-1
                (left rudder) appears.  IAS sits low (~42-50 kt) at
                low n (~0.5 g).
  8.0 – 9.0s : recovery initiates — anti-yaw rudder, then forward
                stick.  Yaw drops; AOA un-stalls (drops back below
                stall_warn).  Cue extinguishes via the un-stall path.
  9.0 – 10s  : pull out of dive.  IAS builds FAST as we dive at low
                AOA, n builds back up to ~1.5 g as the pilot pulls.
                Wings rolled near level.  AOA ends back in the
                fast-tone region.
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

    # Reference AOAs (clean / flaps 0).  CRITICAL: SpinDetector's gate
    # is aoa > fSTALLAOA (alpha_stall = 10.31° on this aircraft), NOT
    # stall_warn.  We must hold AOA solidly above alpha_stall during
    # the developed-spin phase or the cue won't latch.  To extinguish
    # the cue, AOA drops back below alpha_stall during recovery —
    # "breaking the stall".
    aoa_onspeed    = 0.5 * (fs0.onspeed_fast_aoa + fs0.onspeed_slow_aoa)  # ~4.6°
    aoa_stall_warn = fs0.stallwarn_aoa                                      # 8.24°
    aoa_alpha_stall = fs0.alpha_stall                                       # 10.31°
    aoa_in_spin    = aoa_alpha_stall + 1.5                                  # 11.8°, well past stall
    aoa_recovered  = fs0.onspeed_slow_aoa                                   # 5.26°, well below alpha_stall

    # Spin yaw plateau (well above SpinDetector's 20°/s gate).
    yaw_plateau = 80.0   # deg/s, +nose-right (right spin)
    yaw_exit    =  3.0

    base = LiveSnapshot(palt=3000.0, oat=12)

    def ias_at(aoa: float, n: float) -> float:
        return wfb.ias_from_aoa(aoa, fs0, n=n)

    def smooth(field_pairs: dict, duration: float, ease: str = "smooth"):
        n_ticks = env.n_ticks(duration)
        for i in range(n_ticks):
            u = i / max(1, n_ticks - 1)
            if ease == "smooth":
                us = env.smoothstep(u)
            elif ease == "out_pow":
                # Fast at start, slows at the end — for the
                # "can't stop pulling toward stall" feel.
                us = env.ease_out_pow(u, power=2.5)
            else:
                us = u
            snap = base.copy()
            for field, (a, b) in field_pairs.items():
                setattr(snap, field, env.lerp(a, b, us))
            yield snap

    def hold_state(state: LiveSnapshot, duration: float):
        for _ in range(env.n_ticks(duration)):
            yield state.copy()

    # -------- 0.0 – 2.0s: cruise, on-speed, clean, 1g, wings level
    cruise = LiveSnapshot(
        aoa=aoa_onspeed,
        ias=ias_at(aoa_onspeed, 1.0),       # ~78 kt
        pitch=4.0, roll=0.0, yaw_rate=0.0,
        vertical_g=1.0, lateral_g=0.0,
        flap_deg=0, palt=3000.0, oat=12,
    )

    # -------- 2.0 – 6.0s: pitch-up, AOA climbs in three pieces so the
    # pilot's hesitation at the stall-warn threshold is audible.
    #
    #   2.0 – 3.0s : on-speed → between onspeed-slow and stall-warn.
    #                Smooth ramp.  High-pulse band rises in PPS.
    #   3.0 – 5.0s : (very slow) into stall-warn.  Stall buzz arrives
    #                here.  This is the "I really shouldn't" hesitation.
    #   5.0 – 6.0s : commit — push past stall-warn to alpha_stall.  Wing
    #                breaks at the end of this segment.
    aoa_just_below_stall_warn = aoa_stall_warn - 0.15

    pull_to_high_pulse = smooth({
        "aoa":         (aoa_onspeed,           aoa_just_below_stall_warn),
        "ias":         (ias_at(aoa_onspeed,1), ias_at(aoa_just_below_stall_warn,1)),
        "pitch":       (4.0,                   10.0),
        "vertical_g":  (1.0,                   1.0),
    }, duration=1.0)

    hesitate_at_stall_warn = smooth({
        "aoa":         (aoa_just_below_stall_warn, aoa_stall_warn + 0.20),
        "ias":         (ias_at(aoa_just_below_stall_warn,1),
                        ias_at(aoa_stall_warn + 0.20,1)),
        "pitch":       (10.0,                      12.0),
        "vertical_g":  (1.0,                       1.0),
    }, duration=2.0)

    commit_to_stall = smooth({
        "aoa":         (aoa_stall_warn + 0.20,     aoa_alpha_stall),
        "ias":         (ias_at(aoa_stall_warn + 0.20,1), ias_at(aoa_alpha_stall,1)),
        "pitch":       (12.0,                      13.0),
        "vertical_g":  (1.0,                       1.0),
    }, duration=1.0)

    # -------- 4.0 – 4.8s: stall break, wing drops, yaw onset
    # AOA pushes WELL past alpha_stall into the spin AOA (11.8°).
    # vertical_g drops toward 0.5 (post-stall lift fall-off).
    departure = smooth({
        "aoa":         (aoa_alpha_stall,         aoa_in_spin),
        "ias":         (ias_at(aoa_alpha_stall,1.0), ias_at(aoa_in_spin,0.5)),  # 59 → 47
        "pitch":       (13.0,                   8.0),
        "roll":        (0.0,                    +25.0),
        "yaw_rate":    (0.0,                    +50.0),
        "vertical_g":  (1.0,                    0.5),
        "lateral_g":   (0.0,                    +0.30),
    }, duration=0.8)

    # -------- 4.8 – 6.0s: spin develops — yaw climbs to plateau
    spin_in = smooth({
        "aoa":         (aoa_in_spin,            aoa_in_spin),
        "ias":         (ias_at(aoa_in_spin,0.5), ias_at(aoa_in_spin,0.4)),    # 51 → 45
        "pitch":       (8.0,                    -5.0),
        "roll":        (+25.0,                  +50.0),
        "yaw_rate":    (+50.0,                  +yaw_plateau),
        "vertical_g":  (0.5,                    0.4),
        "lateral_g":   (+0.30,                  +0.40),
    }, duration=1.2)

    # -------- developed spin holds (cue active)
    spin_hold = smooth({
        "aoa":         (aoa_in_spin,            aoa_in_spin - 0.1),
        "ias":         (ias_at(aoa_in_spin,0.4), ias_at(aoa_in_spin,0.4)),    # ~45
        "pitch":       (-5.0,                   -8.0),
        "roll":        (+50.0,                  +55.0),
        "yaw_rate":    (+yaw_plateau,           +yaw_plateau),
        "vertical_g":  (0.4,                    0.4),
        "lateral_g":   (+0.40,                  +0.40),
    }, duration=1.0)

    # -------- 8.0 – 9.0s: recovery initiates.  Anti-yaw rudder, forward
    # stick.  Yaw drops, AOA un-stalls (back below stall_warn).
    # vertical_g climbs as wing reattaches; ball drops to centred.
    recover = smooth({
        "aoa":         (aoa_in_spin - 0.1,      aoa_recovered),
        "ias":         (ias_at(aoa_in_spin,0.4), ias_at(aoa_recovered,1.0)),   # 45 → 78
        "pitch":       (-8.0,                   -15.0),    # nose-down recovery
        "roll":        (+55.0,                  +15.0),
        "yaw_rate":    (+yaw_plateau,           +yaw_exit),
        "vertical_g":  (0.4,                    1.0),
        "lateral_g":   (+0.40,                  +0.05),
    }, duration=1.0)

    # -------- 9.0 – 10s: dive pull-out.  AOA holds in the fast-tone
    # band (low pulse).  IAS ramps up FAST as we accelerate out of the
    # dive; n builds to ~1.5g pulling out.  This is where the
    # "super-fast IAS ramp" you described happens.
    pullout = smooth({
        "aoa":         (aoa_recovered,          aoa_recovered + 0.3),
        # IAS should jump because we were diving — pull-up at higher n
        # AND a residual dive speed boost.  Realistic post-spin IAS
        # exits around 100-110 kt by the time the wings are level.
        "ias":         (ias_at(aoa_recovered,1.0), 105.0),
        "pitch":       (-15.0,                  3.0),
        "roll":        (+15.0,                  0.0),
        "yaw_rate":    (+yaw_exit,              0.0),
        "vertical_g":  (1.0,                    1.5),
        "lateral_g":   (+0.05,                  0.0),
    }, duration=1.0)

    # Total: 1.0 + 1.0 + 2.0 + 1.0 + 0.8 + 1.2 + 1.0 + 1.0 + 1.0 = 10.0s
    stream = env.chain(
        hold_state(cruise, 1.0),         # 0.0–1.0 cruise
        pull_to_high_pulse,              # 1.0–2.0 climb to high-pulse band
        hesitate_at_stall_warn,          # 2.0–4.0 SLOW pull through stall_warn
        commit_to_stall,                 # 4.0–5.0 commit, wing at alpha_stall
        departure,                       # 5.0–5.8 wing breaks
        spin_in,                         # 5.8–7.0 spin develops, yaw climbs
        spin_hold,                       # 7.0–8.0 cue active
        recover,                         # 8.0–9.0 anti-yaw, AOA un-stalls
        pullout,                         # 9.0–10.0 dive pull-out
    )
    stream = env.add_realistic_jitter(stream,
                                      lateral_g_amp=0.05,
                                      yaw_rate_amp=1.5,
                                      vertical_g_amp=0.03,
                                      pitch_amp=0.4,
                                      roll_amp=0.6,
                                      aoa_amp=0.10,
                                      ias_amp=0.5,
                                      seed=42)
    yield from env.add_t_offsets(stream)
