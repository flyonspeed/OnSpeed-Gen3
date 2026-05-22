"""Unit tests for build-env auto-detection in capture_perf_report.py.

Pins the contract that:

1. `detect_build_env` distinguishes synth from non-synth captures using
   the two firmware markers (boot banner + subsys.synth_build line).
2. `resolve_build_env` honors an explicit --build-env, warns on disagree,
   and warns on --from-file fallback-without-marker.

These are the only pieces of logic that decide what gets written into
the "Build env:" line of every PERF report. A regression here silently
mislabels every future report and is hard to spot.
"""

import pytest

from capture_perf_report import detect_build_env, resolve_build_env


# ---------------------------------------------------------------------------
# detect_build_env — primary + secondary markers, and the empty fallback
# ---------------------------------------------------------------------------

class TestDetectBuildEnv:
    def test_synth_boot_banner_detected(self) -> None:
        """Primary signal: the .ino's ONSPEED_SYNTH_SENSORS block prints
        'synth: EFIS=... boom=boom perf-synth build active' at boot. Any
        capture that includes the boot phase will carry this string."""
        text = (
            "OnSpeed Gen3 4.22.2-dev.118+5dbd36e\n"
            "Boot #220 (fw=4.22.2-dev.118+5dbd36e, reset=POWERON)\n"
            "synth: EFIS=vn300 boom=boom perf-synth build active\n"
            "PERF streaming auto-enabled (perf-synth build)\n"
        )
        env, found = detect_build_env(text)
        assert env == "esp32s3-v4p-perf-synth"
        assert found is True

    def test_synth_subsys_detected(self) -> None:
        """Secondary signal: the perf-synth env's synthetic-frame builder
        is instrumented with a SynthBuild PerfScope. A capture that
        missed the boot banner but caught at least one PERF window still
        carries 'subsys.synth_build' in the snapshot text."""
        text = (
            "==== PERF ====\n"
            "task=Imu          loops= 208 p50=  880us p95=  912us\n"
            "subsys.synth_build    n=  100 total=  500us p50=   4us\n"
            "heap free=8113220  min=8069892  largest_block=20468\n"
        )
        env, found = detect_build_env(text)
        assert env == "esp32s3-v4p-perf-synth"
        assert found is True

    def test_non_synth_perf_capture(self) -> None:
        """A capture from esp32s3-v4p-perf has neither marker."""
        text = (
            "==== PERF ====\n"
            "task=Imu          loops= 208 p50= 1536us p95= 1536us\n"
            "task=Sensors      loops=  50 p50=  208us p95=  944us\n"
            "subsys.ekfq.correct   n=  208 total= 89860us\n"
            "heap free=8111156\n"
        )
        env, found = detect_build_env(text)
        assert env == "esp32s3-v4p-perf"
        assert found is False

    def test_empty_input_falls_back_to_non_synth(self) -> None:
        """If the capture is empty (or truncated before any PERF window),
        we have no positive signal — fall back to non-synth so callers
        with --from-file can warn about the ambiguous case."""
        env, found = detect_build_env("")
        assert env == "esp32s3-v4p-perf"
        assert found is False

    def test_banner_match_short_circuits_subsys_check(self) -> None:
        """If the boot banner is present, it's the strong signal — don't
        require the subsys.synth_build line too. Captures from the synth
        env with PERF streaming disabled (synth_status disabled by the
        user) would still match."""
        text = "synth: EFIS=vn300 boom=boom perf-synth build active\n"
        env, found = detect_build_env(text)
        assert env == "esp32s3-v4p-perf-synth"
        assert found is True


# ---------------------------------------------------------------------------
# resolve_build_env — explicit-arg + from-file warnings
# ---------------------------------------------------------------------------

class TestResolveBuildEnv:
    def test_explicit_arg_with_no_marker_no_warning(self) -> None:
        """If the user passes --build-env esp32s3-v4p-perf and the capture
        has no synth markers, detection agrees with the user. No warning."""
        env, warnings = resolve_build_env(
            raw_text="task=Imu loops=208\n",
            explicit_arg="esp32s3-v4p-perf",
            from_file=False,
        )
        assert env == "esp32s3-v4p-perf"
        assert warnings == []

    def test_explicit_arg_agrees_with_synth_marker_no_warning(self) -> None:
        """User passes --build-env esp32s3-v4p-perf-synth and the capture
        has the synth banner — they agree. No warning."""
        env, warnings = resolve_build_env(
            raw_text="synth: EFIS=vn300 boom=boom perf-synth build active\n",
            explicit_arg="esp32s3-v4p-perf-synth",
            from_file=False,
        )
        assert env == "esp32s3-v4p-perf-synth"
        assert warnings == []

    def test_explicit_arg_disagrees_with_synth_marker_warns(self) -> None:
        """User passes --build-env esp32s3-v4p-perf (the stale default
        bug) but the firmware is actually synth. Use the explicit value
        (the user is the authority), but warn loudly that the report's
        Build env line may mislabel the firmware."""
        env, warnings = resolve_build_env(
            raw_text="synth: EFIS=vn300 boom=boom perf-synth build active\n",
            explicit_arg="esp32s3-v4p-perf",
            from_file=False,
        )
        assert env == "esp32s3-v4p-perf"
        assert len(warnings) == 1
        assert "auto-detect chose" in warnings[0]
        assert "esp32s3-v4p-perf-synth" in warnings[0]
        assert "esp32s3-v4p-perf" in warnings[0]

    def test_explicit_arg_no_marker_no_warning_when_disagree(self) -> None:
        """If no marker was found, we have NO positive signal that
        contradicts the user. Don't warn — the user is the authority
        and may know something we don't (e.g. capturing mid-session
        with --from-file)."""
        env, warnings = resolve_build_env(
            raw_text="task=Imu loops=208\n",   # no synth markers
            explicit_arg="esp32s3-v4p-perf-synth",
            from_file=True,
        )
        assert env == "esp32s3-v4p-perf-synth"
        assert warnings == []

    def test_no_explicit_arg_marker_found_no_warning(self) -> None:
        """Auto-detect finds a marker, no explicit arg — clean case."""
        env, warnings = resolve_build_env(
            raw_text="synth: EFIS=vn300 boom=boom perf-synth build active\n",
            explicit_arg=None,
            from_file=False,
        )
        assert env == "esp32s3-v4p-perf-synth"
        assert warnings == []

    def test_no_explicit_arg_no_marker_live_capture_no_warning(self) -> None:
        """Live serial capture with no marker. The bench is fresh and the
        boot banner WILL be in the buffer if the firmware is the synth
        env; absence of a marker is meaningful in this case → silently
        default to non-synth."""
        env, warnings = resolve_build_env(
            raw_text="task=Imu loops=208\n",
            explicit_arg=None,
            from_file=False,
        )
        assert env == "esp32s3-v4p-perf"
        assert warnings == []

    def test_no_explicit_arg_no_marker_from_file_warns(self) -> None:
        """--from-file with no marker is ambiguous — the saved capture
        may have started mid-session, missing the synth banner. Warn so
        the user can re-run with an explicit --build-env if they know
        the env."""
        env, warnings = resolve_build_env(
            raw_text="task=Imu loops=208\n",
            explicit_arg=None,
            from_file=True,
        )
        assert env == "esp32s3-v4p-perf"
        assert len(warnings) == 1
        assert "--from-file" in warnings[0]
        assert "esp32s3-v4p-perf-synth" in warnings[0]


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
