#!/usr/bin/env python3
"""Render a scenario to MP4: M5 sim video + onspeed_core::audio audio.

Usage:
    python3 record.py scenarios/spin_recovery.py --out out/spin.mp4
    python3 record.py scenarios/ldmax_pip.py     --out out/ldmax.mp4

Pipeline:
  1. Import the scenario module; collect its tick stream of LiveSnapshot.
  2. For each tick:
       - run SpinDetector to fill spin_cue
       - build a 74-byte #1 wire frame
       - emit a per-tick line of body-angle thresholds for the audio harness
  3. Run audio harness over the body-angle stream → audio.pcm.
  4. Run M5 sim record mode over the wire-frame stream → frames.rgb.
  5. ffmpeg-mux audio + video → MP4.

This tool is the prototype for the missing 'Synthetic data generator'
data source in docs/ARCHITECTURE_DECOUPLING.md. It consumes the same
wire-frame schema that the LogReplay path uses, so the same pipeline
later supports a CSV-window-to-snapshot adapter without architectural
change.
"""

from __future__ import annotations

import argparse
import importlib.util
import shutil
import subprocess
import sys
from pathlib import Path

# Resolve module-local paths
THIS_DIR = Path(__file__).resolve().parent
REPO     = THIS_DIR.parent.parent
sys.path.insert(0, str(THIS_DIR))

import wire_frame_builder as wfb     # noqa: E402
from live_snapshot import LiveSnapshot   # noqa: E402

DEFAULT_CFG = Path.home() / "Downloads/onspeed2_latest.cfg"
SAMPLE_RATE_HZ = 16000   # mono PCM out of the audio harness


def import_scenario(path: Path):
    """Load a scenario module by file path; must expose `scenario()` -> Iterator[LiveSnapshot]."""
    spec = importlib.util.spec_from_file_location(path.stem, path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"failed to load {path}")
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(path.parent))
    spec.loader.exec_module(mod)
    if not hasattr(mod, "scenario"):
        raise SystemExit(f"{path} must define a 'scenario()' function")
    return mod


def detect_active_index(raw_adc: int,
                        sorted_flaps: list[wfb.FlapSetpoints]) -> int:
    """Mirror onspeed::sensors::DetectFlaps midpoint-threshold logic.

    Used so the orchestrator can pass the same `activeIndex` to the
    DisplayPctAnchors harness that the firmware would compute from the
    same lever-ADC reading.
    """
    n = len(sorted_flaps)
    if n <= 1:
        return 0
    descending = sorted_flaps[0].pot_value > sorted_flaps[-1].pot_value
    idx = 0
    for i in range(1, n):
        midpoint = (sorted_flaps[i].pot_value + sorted_flaps[i - 1].pot_value) // 2
        if descending:
            if raw_adc < midpoint:
                idx = i
        else:
            if raw_adc > midpoint:
                idx = i
    return idx


def run_display_anchors(states: list[LiveSnapshot],
                        flap_setpoints: dict[int, wfb.FlapSetpoints],
                        anchors_bin: Path):
    """Run ComputeDisplayPctAnchors via C++ harness for every tick.

    Mutates `states` in place: sets each tick's
    `tones_on_pct_lift` / `onspeed_fast_pct_lift` /
    `onspeed_slow_pct_lift` / `stall_warn_pct_lift` and overrides
    `flap_deg` with the lever-interpolated value (matching what the
    firmware sends on the wire).  Returns nothing — fields are
    written onto each LiveSnapshot.

    Detents are sorted by pot_value descending order based on wiring.
    """
    if not anchors_bin.exists():
        raise SystemExit(f"missing {anchors_bin}; build with build_harnesses.sh")

    # Sort detents by ascending DEGREES — same order the firmware loads
    # from the config (Flaps detents are stored in degree order).
    sorted_flaps = [flap_setpoints[k] for k in sorted(flap_setpoints.keys())]

    # Build the input.  First the table (entry count + each entry),
    # then per-tick (rawAdc, activeIndex).
    lines = [f"{len(sorted_flaps)}"]
    for fs in sorted_flaps:
        lines.append(
            f"{fs.degrees} {fs.pot_value} {fs.ldmax_aoa} {fs.onspeed_fast_aoa} "
            f"{fs.onspeed_slow_aoa} {fs.stallwarn_aoa} {fs.alpha_0} {fs.alpha_stall}"
        )

    # Resolve lever_raw + activeIndex for every state.  Fall back to
    # the closest-by-deg detent's pot_value when scenario didn't set
    # lever_raw — keeps backward compat with old scenarios.
    per_tick = []
    for s in states:
        if s.lever_raw is None:
            fs = wfb.setpoints_for_flap(s.flap_deg, flap_setpoints)
            raw = fs.pot_value
        else:
            raw = int(round(s.lever_raw))
        active = detect_active_index(raw, sorted_flaps)
        per_tick.append((raw, active))
        lines.append(f"{raw} {active}")

    proc = subprocess.run(
        [str(anchors_bin)],
        input=("\n".join(lines) + "\n").encode("ascii"),
        capture_output=True, check=True,
    )
    out_lines = proc.stdout.decode("ascii").splitlines()
    if len(out_lines) != len(states):
        raise SystemExit(
            f"display_anchors_harness returned {len(out_lines)} rows for "
            f"{len(states)} ticks"
        )

    for s, line in zip(states, out_lines):
        a, b, c, d, deg = line.split()
        s.tones_on_pct       = int(a)
        s.onspeed_fast_pct   = int(b)
        s.onspeed_slow_pct   = int(c)
        s.stall_warn_pct     = int(d)
        s.flap_deg           = int(deg)


def run_spin_detector(states: list[LiveSnapshot],
                      flap_setpoints: dict[int, wfb.FlapSetpoints],
                      detector_bin: Path) -> list[int]:
    """Run the C++ SpinDetector once per tick. Returns per-tick cue values."""
    if not detector_bin.exists():
        raise SystemExit(f"missing {detector_bin}; build with build_harnesses.sh")

    # Build the single big input string.
    lines = []
    for s in states:
        fs = wfb.setpoints_for_flap(s.flap_deg, flap_setpoints)
        # Match SpinDetector's signature: dt, yaw_dps, aoa_deg, stall_aoa_deg.
        # Use the per-flap stall body angle (alpha_stall) as the gate.
        # (SpinDetector treats this as `stall_aoa`; the firmware passes
        #  `g_Config.aFlaps[i].fSTALLAOA`, which for an unconfigured flap
        #  defaults to alpha_stall.)
        lines.append(f"0.020 {s.yaw_rate} {s.aoa} {fs.alpha_stall}")
    proc = subprocess.run(
        [str(detector_bin)],
        input="\n".join(lines).encode("ascii") + b"\n",
        capture_output=True, check=True,
    )
    cues = [int(line) for line in proc.stdout.decode("ascii").splitlines() if line.strip()]
    if len(cues) != len(states):
        raise SystemExit(f"spin detector returned {len(cues)} cues for {len(states)} states")
    return cues


def build_frames_and_audio_input(states: list[LiveSnapshot],
                                 flap_setpoints: dict[int, wfb.FlapSetpoints]
                                 ) -> tuple[bytes, bytes]:
    """Return (wire_frames_bytes, audio_input_text_bytes)."""
    wire = bytearray()
    audio_lines = []
    for s in states:
        # Audio path uses the active-detent body angles (snapped to the
        # active detent — same as the firmware, which compares
        # g_Sensors.AOA directly to g_Config.aFlaps[g_Flaps.iIndex]).
        fs = wfb.setpoints_for_flap(s.flap_deg, flap_setpoints)
        pct = wfb.compute_percent_lift(s.aoa, fs)
        frame = wfb.Frame(
            pitch_deg=s.pitch,
            roll_deg=s.roll,
            ias_kts=s.ias,
            palt_ft=s.palt,
            turnrate_dps=s.yaw_rate,
            lateral_g=s.lateral_g,
            vertical_g=s.vertical_g,
            percent_lift=pct,
            vsi_fpm=s.vsi,
            oat_c=s.oat,
            flightpath_deg=s.flight_path,
            flap_deg=s.flap_deg,            # already lever-interpolated
            # Display anchors come from ComputeDisplayPctAnchors; the
            # L/Dmax pip slides between detents while the band edges
            # snap to the active detent.  Set by run_display_anchors().
            tones_on_pct_lift=getattr(s, "tones_on_pct", 0),
            onspeed_fast_pct_lift=getattr(s, "onspeed_fast_pct", 0),
            onspeed_slow_pct_lift=getattr(s, "onspeed_slow_pct", 0),
            stall_warn_pct_lift=getattr(s, "stall_warn_pct", 0),
            flaps_min_deg=min(flap_setpoints),
            flaps_max_deg=max(flap_setpoints),
            g_onset_rate=s.g_onset,
            spin_cue=s.spin_cue,
            data_mark=s.data_mark,
        )
        wire += frame.to_bytes()
        audio_lines.append(
            f"{s.aoa:.4f} {fs.ldmax_aoa:.4f} {fs.onspeed_fast_aoa:.4f} "
            f"{fs.onspeed_slow_aoa:.4f} {fs.stallwarn_aoa:.4f} "
            f"{s.lateral_g:.4f}"
        )
    audio_text = ("\n".join(audio_lines) + "\n").encode("ascii")
    return bytes(wire), audio_text


def run_audio_harness(audio_input: bytes, audio_bin: Path, pcm_path: Path):
    if not audio_bin.exists():
        raise SystemExit(f"missing {audio_bin}; build with build_harnesses.sh")
    with pcm_path.open("wb") as f:
        subprocess.run([str(audio_bin)], input=audio_input, stdout=f, check=True)


def run_sim_recorder(wire_frames: bytes, sim_bin: Path, rgb_path: Path):
    if not sim_bin.exists():
        raise SystemExit(
            f"missing {sim_bin}; build with: cd software/OnSpeed-M5-Display "
            "&& pio run -e native_record"
        )
    subprocess.run(
        [str(sim_bin), str(rgb_path)],
        input=wire_frames, check=True,
    )


def mux_to_mp4(rgb_path: Path, pcm_path: Path, out_path: Path,
               n_frames: int, scale_to: int = 4):
    """Combine RGB video + raw PCM audio into a single MP4.

    Video is upscaled with nearest-neighbor (M5 native is 320x240; viewer
    convenience).
    """
    if shutil.which("ffmpeg") is None:
        raise SystemExit("ffmpeg not on PATH")

    width, height = 320, 240
    out_w, out_h = width * scale_to, height * scale_to

    # Convert PCM to WAV separately so ffmpeg knows the format unambiguously.
    # Audio is stereo (2 channels) — left/right gains differ when 3D pan
    # responds to lateral G.
    wav_path = out_path.with_suffix(".wav")
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error",
         "-f", "s16le", "-ar", str(SAMPLE_RATE_HZ), "-ac", "2",
         "-i", str(pcm_path), str(wav_path)],
        check=True,
    )

    duration_s = n_frames / 50.0   # tick rate

    subprocess.run([
        "ffmpeg", "-y", "-loglevel", "error",
        "-f", "rawvideo", "-pix_fmt", "rgb24",
        "-s", f"{width}x{height}", "-r", "50",
        "-i", str(rgb_path),
        "-i", str(wav_path),
        "-vf", f"scale={out_w}:{out_h}:flags=neighbor",
        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18",
        "-c:a", "aac", "-b:a", "128k",
        "-t", f"{duration_s:.3f}",
        "-shortest",
        str(out_path),
    ], check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("scenario", type=Path, help="path to scenarios/*.py")
    ap.add_argument("--out", type=Path, required=True, help="output .mp4 path")
    ap.add_argument("--cfg", type=Path, default=DEFAULT_CFG,
                    help=f"OnSpeed config XML (default: {DEFAULT_CFG})")
    ap.add_argument("--keep-intermediate", action="store_true",
                    help="retain frames.bin / frames.rgb / audio.pcm after mux")
    args = ap.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)

    # Locate built binaries.
    detector_bin = THIS_DIR / "build" / "spin_detector_harness"
    audio_bin    = THIS_DIR / "build" / "audio_harness"
    anchors_bin  = THIS_DIR / "build" / "display_anchors_harness"
    sim_bin      = REPO / "software/OnSpeed-M5-Display/.pio/build/native_record/program"

    # 1. Scenario.
    scen = import_scenario(args.scenario)
    states: list[LiveSnapshot] = list(scen.scenario())
    if not states:
        raise SystemExit("scenario produced no ticks")
    print(f"[record] scenario {args.scenario.name}: {len(states)} ticks "
          f"({len(states)/50:.2f} s)", file=sys.stderr)

    # 2. Per-flap setpoints from user config.
    flap_setpoints = wfb.load_flap_setpoints(args.cfg)
    print(f"[record] config flaps: {sorted(flap_setpoints)}", file=sys.stderr)

    # 3a. Display percent anchors via ComputeDisplayPctAnchors.  The
    #     scenario sets `lever_raw` per tick; this computes the
    #     interpolated L/Dmax pip + snapped band edges + interpolated
    #     flaps_deg the firmware would emit on the wire.  Mutates
    #     each state in place.
    run_display_anchors(states, flap_setpoints, anchors_bin)

    # 3b. Spin detector.
    cues = run_spin_detector(states, flap_setpoints, detector_bin)
    for s, cue in zip(states, cues):
        s.spin_cue = cue
    n_active = sum(1 for c in cues if c != 0)
    print(f"[record] spin cue active for {n_active} ticks", file=sys.stderr)

    # 4. Build wire frames + audio input.
    wire, audio_text = build_frames_and_audio_input(states, flap_setpoints)

    # 5. Run audio harness.
    work = args.out.parent
    pcm_path  = work / (args.out.stem + ".pcm")
    rgb_path  = work / (args.out.stem + ".rgb")
    print("[record] running audio harness...", file=sys.stderr)
    run_audio_harness(audio_text, audio_bin, pcm_path)

    # 6. Run sim recorder.
    print("[record] running sim recorder...", file=sys.stderr)
    run_sim_recorder(wire, sim_bin, rgb_path)

    # 7. Mux.
    print("[record] muxing to mp4...", file=sys.stderr)
    mux_to_mp4(rgb_path, pcm_path, args.out, n_frames=len(states))

    if not args.keep_intermediate:
        for p in (pcm_path, rgb_path, args.out.with_suffix(".wav")):
            try: p.unlink()
            except FileNotFoundError: pass

    print(f"[record] done: {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
