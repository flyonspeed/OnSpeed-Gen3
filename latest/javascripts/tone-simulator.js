/**
 * Interactive AOA Tone Simulator for OnSpeed Documentation
 *
 * Renders a side-view aircraft that pitches with AOA over a fan of
 * reference lines into colored tone-region bands (STALL / SLOW /
 * ONSPEED / L/Dmax) and produces audio matching the OnSpeed audio
 * tone spec at docs/site/docs/reference/audio-tone-spec.md.
 *
 * Audio path implements:
 *   - Tone-decision regions (spec §1)
 *   - Per-pulse DAHDR envelope with inter-pulse Gap (spec §2)
 *   - Unconditional 60.97 ms entry delay on solid tones (spec §2 + §4)
 *   - Per-PPS carrier-amplitude ramp on APPROACH-STALL (spec §5)
 *   - Click-free transitions: spec changes land at pulse boundaries,
 *     and amplitude transitions ride out the silent_delay window
 *     (spec §8)
 *
 * Free-running scheduler: pulses are scheduled ahead in audio-context
 * time via the Web Audio AudioParam timeline.  Slider movement just
 * updates the target spec; the running pulse finishes naturally.
 *
 * Self-contained IIFE — no external dependencies.
 */
;(function () {
  "use strict"

  // ── Tone constants (mirrors onspeed_core ToneCalc + Audio) ──────
  var LOW_TONE_HZ         = 400
  var HIGH_TONE_HZ        = 1600
  var HIGH_TONE_STALL_PPS = 20.0
  var HIGH_TONE_PPS_MIN   = 1.5
  var HIGH_TONE_PPS_MAX   = 6.2
  var LOW_TONE_PPS_MIN    = 1.5
  var LOW_TONE_PPS_MAX    = 8.2

  // Carrier amplitude levels (spec §5).
  var STALL_VOL_MIN = 0.25
  var STALL_VOL_MAX = 1.0

  // Envelope ramp times (spec §2).
  var TONE_RAMP_MS  = 15
  var STALL_RAMP_MS = 5

  // Inter-pulse silent gap (spec §2).
  var GAP_MS = 3.0

  // Unconditional silent delay on solid-tone entry (spec §2/§4):
  //   1000 / LOW_TONE_PPS_MAX / 2  ≈ 60.97 ms.
  var SOLID_TRANSITION_DELAY_MS = 1000.0 / LOW_TONE_PPS_MAX / 2.0

  // Master output gain so the page volume is comfortable on headphones
  // without clipping at maximum carrier amplitude.
  var MASTER_GAIN = 0.35

  // Default AOA thresholds (degrees) — representative RV-4 values.
  var TH = {
    ldmax:       5.0,
    onspeedFast: 7.0,
    onspeedSlow: 9.0,
    stallWarn:  13.0,
    stall:      15.0,
  }

  // ── AOA → tone decision (mirrors ToneCalc::calculateTone) ──────
  function mapfloat(x, inMin, inMax, outMin, outMax) {
    if (inMax === inMin) return outMin
    return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin
  }

  // Returns:
  //   { type: "none" | "low" | "high",
  //     pps:  Number    (0 means solid),
  //     volumeMult: Number  (carrier amp in [0, 1]),
  //     region: "silent" | "fast" | "onspeed" | "slow" | "stall" }
  function decideTone(aoa) {
    if (aoa >= TH.stallWarn)
      return { type: "high", pps: HIGH_TONE_STALL_PPS,
               volumeMult: STALL_VOL_MAX, region: "stall" }
    if (aoa > TH.onspeedSlow) {
      var pps = mapfloat(aoa, TH.onspeedSlow, TH.stallWarn,
                         HIGH_TONE_PPS_MIN, HIGH_TONE_PPS_MAX)
      var vol = mapfloat(aoa, TH.onspeedSlow, TH.stallWarn,
                         STALL_VOL_MIN, STALL_VOL_MAX)
      return { type: "high", pps: pps, volumeMult: vol, region: "slow" }
    }
    if (aoa >= TH.onspeedFast)
      return { type: "low", pps: 0, volumeMult: STALL_VOL_MIN,
               region: "onspeed" }
    if (aoa >= TH.ldmax && TH.ldmax < TH.onspeedFast) {
      var pps2 = mapfloat(aoa, TH.ldmax, TH.onspeedFast,
                          LOW_TONE_PPS_MIN, LOW_TONE_PPS_MAX)
      return { type: "low", pps: pps2, volumeMult: STALL_VOL_MIN,
               region: "fast" }
    }
    return { type: "none", pps: 0, volumeMult: STALL_VOL_MIN,
             region: "silent" }
  }

  // ── DAHDR envelope spec builders ──────────────────────────────
  // All times in seconds (Web Audio timeline).
  //
  // Pulse spec (PULSE_TONE):
  //   delay (silent) → attack → hold → decay → gap → loop
  //
  // The full pulse_period equals 1000/pps ms.  tone_length =
  //   pulse_period - 3 ms is the envelope-active window; the remaining
  //   3 ms is the inter-pulse silent gap.  `fromSolid` swaps the
  //   default delay for the 60.97 ms transition delay so the first
  //   high-tone pulse arrives sooner after a solid tone.

  function makePulseSpec(pps, isStall, fromSolid) {
    var pulsePeriodMs = 1000.0 / pps
    var toneLengthMs  = pulsePeriodMs - GAP_MS
    var rampMs        = isStall ? STALL_RAMP_MS : TONE_RAMP_MS
    var delayMs       = fromSolid ? SOLID_TRANSITION_DELAY_MS
                                  : (toneLengthMs * 0.5)
    var holdMs        = (toneLengthMs * 0.5) - 2 * rampMs
    if (holdMs < 0) holdMs = 0
    return {
      isSolid: false,
      delay:   delayMs   / 1000.0,
      attack:  rampMs    / 1000.0,
      hold:    holdMs    / 1000.0,
      decay:   rampMs    / 1000.0,
      gap:     GAP_MS    / 1000.0,
      release: rampMs    / 1000.0,
    }
  }

  function makeSolidSpec() {
    return {
      isSolid: true,
      delay:   SOLID_TRANSITION_DELAY_MS / 1000.0,
      attack:  TONE_RAMP_MS / 1000.0,
      hold:    0,
      decay:   0,
      gap:     0,
      release: TONE_RAMP_MS / 1000.0,
    }
  }

  function sameSpec(a, b) {
    if (!a || !b) return false
    var TOL = 1e-4  // 100 µs
    return a.isSolid === b.isSolid
        && Math.abs(a.delay   - b.delay)   <= TOL
        && Math.abs(a.attack  - b.attack)  <= TOL
        && Math.abs(a.hold    - b.hold)    <= TOL
        && Math.abs(a.decay   - b.decay)   <= TOL
        && Math.abs(a.gap     - b.gap)     <= TOL
        && Math.abs(a.release - b.release) <= TOL
  }

  // ── Layout constants ─────────────────────────────────────────
  var SVG_W = 820
  var SVG_H = 480

  var BAND_LEFT = 20
  var BAND_RIGHT = 200
  var BAND_TOP = 20
  var BAND_BOTTOM = 460

  var FOCAL_X = 380
  var FOCAL_Y = 240

  var AC_CX = 560
  var AC_CY = 240

  var AOA_MIN = -2
  var AOA_MAX = 18

  // ── CSS ──────────────────────────────────────────────────────
  var STYLE_ID = "tone-sim-styles"

  function injectStyles() {
    if (document.getElementById(STYLE_ID)) return
    var css = [
      "[data-md-color-scheme='default'] .ts-wrap, :root .ts-wrap {",
      "  --ts-bg: #f5f5f0;",
      "  --ts-text: #333333;",
      "  --ts-text-dim: #888888;",
      "  --ts-border: #d0d0d0;",
      "  --ts-silent-fill: #dce8f5;",
      "  --ts-fast-fill: #b3d4f7;",
      "  --ts-onspeed-fill: #66bb6a;",
      "  --ts-slow-fill: #ffcc80;",
      "  --ts-stall-fill: #ef9a9a;",
      "  --ts-aircraft: #1565c0;",
      "  --ts-aircraft-accent: #ef5350;",
      "  --ts-fan-line: #999999;",
      "  --ts-fan-active: #333333;",
      "  --ts-band-stroke: #bbbbbb;",
      "}",
      "[data-md-color-scheme='slate'] .ts-wrap {",
      "  --ts-bg: #1e2128;",
      "  --ts-text: #cccccc;",
      "  --ts-text-dim: #777777;",
      "  --ts-border: #3a3a3a;",
      "  --ts-silent-fill: #263040;",
      "  --ts-fast-fill: #1a4a7a;",
      "  --ts-onspeed-fill: #2e7d32;",
      "  --ts-slow-fill: #a06000;",
      "  --ts-stall-fill: #b71c1c;",
      "  --ts-aircraft: #42a5f5;",
      "  --ts-aircraft-accent: #ef5350;",
      "  --ts-fan-line: #555555;",
      "  --ts-fan-active: #eeeeee;",
      "  --ts-band-stroke: #444444;",
      "}",
      ".ts-wrap { position: relative; max-width: 860px; margin: 0 auto; }",
      ".ts-wrap svg { display: block; width: 100%; height: auto;",
      "  border-radius: 8px; border: 1px solid var(--ts-border); }",
      ".ts-controls { display: flex; flex-wrap: wrap; gap: 12px 24px;",
      "  padding: 12px 0 4px; align-items: center; }",
      ".ts-control { display: flex; align-items: center; gap: 8px;",
      "  font-size: 0.85em; color: var(--ts-text); }",
      ".ts-control label { white-space: nowrap; }",
      ".ts-control input[type=range] { width: 300px; cursor: pointer; }",
      "@media (max-width: 600px) {",
      "  .ts-control input[type=range] { width: 180px; }",
      "}",
      ".ts-control .ts-val { min-width: 44px; font-variant-numeric: tabular-nums;",
      "  font-family: var(--md-code-font-family, monospace); font-size: 0.95em; }",
      ".ts-sound-btn { display: inline-flex; align-items: center; gap: 6px;",
      "  padding: 8px 18px; border-radius: 6px; border: 2px solid var(--ts-border);",
      "  background: var(--ts-bg); color: var(--ts-text); cursor: pointer;",
      "  font-size: 0.9em; font-weight: 600; font-family: inherit;",
      "  transition: all 0.15s; user-select: none; }",
      ".ts-sound-btn:hover { border-color: var(--ts-text-dim); }",
      ".ts-sound-btn.active { background: #388e3c; color: #fff;",
      "  border-color: #388e3c; }",
    ].join("\n")
    var el = document.createElement("style")
    el.id = STYLE_ID
    el.textContent = css
    document.head.appendChild(el)
  }

  // ── SVG helpers ──────────────────────────────────────────────
  var NS = "http://www.w3.org/2000/svg"
  function svgEl(tag, attrs) {
    var el = document.createElementNS(NS, tag)
    if (attrs) Object.keys(attrs).forEach(function (k) { el.setAttribute(k, attrs[k]) })
    return el
  }

  function aoaToY(aoa) {
    var frac = (aoa - AOA_MIN) / (AOA_MAX - AOA_MIN)
    return BAND_BOTTOM - frac * (BAND_BOTTOM - BAND_TOP)
  }

  // ── Main class ───────────────────────────────────────────────
  function ToneSimulator(container) {
    this.container = container
    this.state = { aoa: 0, audioOn: false }

    // Audio nodes
    this.audioCtx     = null
    this.masterGain   = null   // shared output gain (master volume)
    this.lowOsc       = null   // 400 Hz oscillator
    this.lowGain      = null   // envelope * carrier amplitude on low
    this.highOsc      = null   // 1600 Hz oscillator
    this.highGain     = null   // envelope * carrier amplitude on high

    // Free-running scheduler state.
    //
    // Pulses are emitted as a sequence of timeline writes on
    // {low,high}Gain.gain.  `nextEventTime` is the audio-context
    // timestamp at which the next pulse cycle (or solid arm) should
    // begin.  `currentSpec` and `currentTone` describe what is
    // playing right now; `currentVolume` is the carrier amplitude
    // applied at the next pulse boundary.  `pendingTone` /
    // `pendingSpec` capture an AOA-region change that arrived
    // mid-pulse — they're consumed at the next boundary so the
    // running pulse finishes uninterrupted.
    this.currentTone   = "none"   // "none" | "low" | "high"
    this.currentSpec   = null
    this.currentVolume = STALL_VOL_MIN
    this.pendingTone   = null
    this.pendingSpec   = null
    this.pendingVolume = STALL_VOL_MIN
    this.targetTone    = "none"
    this.targetSpec    = null
    this.targetVolume  = STALL_VOL_MIN
    this.nextEventTime = 0
    this.solidArmed    = false   // true after a solid spec is scheduled
    this.schedTimerId  = null

    // Scheduler tick interval (ms).  At this cadence the scheduler
    // wakes up, checks whether the next pulse is due, and writes its
    // events to the timeline.  Must be well below the smallest
    // pulse_period (50 ms at STALL) so we always have a tick or two
    // of headroom to schedule the next pulse before its start time.
    this.schedTickMs = 10

    this._buildDOM()
    this._bindControls()
    this._observeTheme()
    this.update()
  }

  ToneSimulator.prototype._buildDOM = function () {
    this.container.classList.add("ts-wrap")

    this.svg = svgEl("svg", {
      viewBox: "0 0 " + SVG_W + " " + SVG_H,
      preserveAspectRatio: "xMidYMid meet",
    })
    this.container.appendChild(this.svg)

    var controls = document.createElement("div")
    controls.className = "ts-controls"

    this.soundBtn = document.createElement("button")
    this.soundBtn.className = "ts-sound-btn"
    this.soundBtn.textContent = "Sound On"
    controls.appendChild(this.soundBtn)

    var aoaCtrl = document.createElement("div")
    aoaCtrl.className = "ts-control"
    aoaCtrl.innerHTML =
      '<label for="ts-aoa">AOA:</label>' +
      '<input type="range" id="ts-aoa" min="-2" max="18" step="0.1" value="0">' +
      '<span class="ts-val" id="ts-aoa-val">0.0°</span>'
    controls.appendChild(aoaCtrl)

    this.container.appendChild(controls)

    this.sliderAOA = this.container.querySelector("#ts-aoa")
    this.valAOA = this.container.querySelector("#ts-aoa-val")
  }

  ToneSimulator.prototype._bindControls = function () {
    var self = this
    this.sliderAOA.addEventListener("input", function () {
      self.state.aoa = parseFloat(self.sliderAOA.value)
      self.update()
    })
    this.soundBtn.addEventListener("click", function () {
      if (self.state.audioOn) self._stopAudio()
      else self._startAudio()
      self.update()
    })
  }

  ToneSimulator.prototype._observeTheme = function () {
    var self = this
    var obs = new MutationObserver(function () { self._draw() })
    obs.observe(document.body, { attributes: true, attributeFilter: ["data-md-color-scheme"] })
  }

  // ── Update (called on every slider change) ───────────────────
  // Captures the new target tone/spec/volume and redraws.  All
  // scheduling happens in the free-running tick.
  ToneSimulator.prototype.update = function () {
    var tone = decideTone(this.state.aoa)
    this.valAOA.textContent = this.state.aoa.toFixed(1) + "°"

    this.targetTone   = tone.type
    this.targetVolume = tone.volumeMult
    if (tone.type === "none") {
      this.targetSpec = null
    } else if (tone.pps === 0) {
      this.targetSpec = makeSolidSpec()
    } else {
      var isStall   = (tone.pps >= HIGH_TONE_STALL_PPS - 0.5)
      var fromSolid = (this.currentSpec && this.currentSpec.isSolid)
      this.targetSpec = makePulseSpec(tone.pps, isStall, fromSolid)
    }

    if (this.audioCtx && this.state.audioOn) {
      this._maybeAdoptTarget()
    }

    this._draw()
  }

  // Decide whether the target tone/spec should take effect now or be
  // queued until the current pulse completes.
  //
  // Mirrors the C++ Envelope NoteOn policy:
  //   - same tone + same spec while running → no-op
  //   - currently silent → arm immediately
  //   - currently solid + new spec → fire the new spec at solid-end
  //   - currently mid-pulse + new spec → queue pending
  ToneSimulator.prototype._maybeAdoptTarget = function () {
    if (this.targetTone === "none") {
      // Drop into silence at the next pulse boundary.  If we're solid,
      // schedule a release and clear the loop timeline.
      if (this.currentTone === "none") return
      this.pendingTone   = "none"
      this.pendingSpec   = null
      this.pendingVolume = this.targetVolume
      if (this.solidArmed) this._releaseSolidNow()
      return
    }

    if (this.currentTone === "none") {
      // Silent → arm immediately.  Start at the later of (now + small
      // lead) and the previously-scheduled release end so any leftover
      // release tail from a recent solid-stop completes naturally
      // before the new spec's silent_delay begins.
      var lead   = this.audioCtx.currentTime + 0.005
      var armAt  = (this.nextEventTime > lead) ? this.nextEventTime : lead
      this.currentTone   = this.targetTone
      this.currentSpec   = this.targetSpec
      this.currentVolume = this.targetVolume
      this.pendingTone   = null
      this.pendingSpec   = null
      this.nextEventTime = armAt
      this._kickScheduler()
      return
    }

    var sameTone = (this.currentTone === this.targetTone)
    if (sameTone && sameSpec(this.currentSpec, this.targetSpec)
                 && Math.abs(this.currentVolume - this.targetVolume) < 1e-3) {
      // No-op — same shape, same level.
      this.pendingTone = null
      this.pendingSpec = null
      return
    }

    // Tone or spec or volume changed.  Queue for next pulse boundary.
    // (For a solid currently playing, we trigger an immediate release
    // and let the pending spec arm on the release tail.)
    this.pendingTone   = this.targetTone
    this.pendingSpec   = this.targetSpec
    this.pendingVolume = this.targetVolume
    if (this.solidArmed) this._releaseSolidNow()
  }

  // ── Scheduler tick ───────────────────────────────────────────
  // Runs every `schedTickMs`.  At each tick, ensures at most one
  // pulse is scheduled ahead of the playback head — the next pulse
  // is committed only when its start time is imminent.  Matches the
  // C++ Envelope's behaviour of consulting the live spec on every
  // sample, never locking in future pulses with stale spec.
  ToneSimulator.prototype._kickScheduler = function () {
    if (this.schedTimerId !== null) return
    var self = this
    function tick() {
      if (!self.state.audioOn || !self.audioCtx) {
        self.schedTimerId = null
        return
      }
      self._scheduleAhead()
      self.schedTimerId = setTimeout(tick, self.schedTickMs)
    }
    this.schedTimerId = setTimeout(tick, 0)
  }

  // Scheduling policy: we keep at most ONE pulse-cycle in flight on
  // the timeline at a time.  When the playback head reaches (or is
  // about to reach) the end of the in-flight pulse, the next pulse is
  // committed using the *current* spec — which reflects the latest
  // AOA-region change because `_maybeAdoptTarget` has had a chance to
  // promote any pending spec into `currentSpec` in the meantime.
  //
  // This mirrors the C++ Envelope's behaviour: the running pulse
  // finishes naturally; the next pulse uses whatever spec is current
  // at the boundary.  No further pulses are pre-committed.
  ToneSimulator.prototype._scheduleAhead = function () {
    if (!this.audioCtx) return
    if (this.currentTone === "none") return
    if (!this.currentSpec) return

    var now = this.audioCtx.currentTime

    // Solid: scheduled exactly once when armed.  Sustain holds level
    // 1.0 in the timeline until something else cancels it.
    if (this.currentSpec.isSolid) {
      if (!this.solidArmed) {
        this._scheduleSolid(this.nextEventTime, this.currentSpec,
                            this.currentVolume, this.currentTone)
        this.solidArmed = true
      }
      return
    }

    // Lookahead: schedule the next pulse only when its start is within
    // one tick + a small safety margin away.  Until then, the
    // currently-running pulse is the only one on the timeline, and
    // `_maybeAdoptTarget` has free rein to mutate `currentSpec`
    // before the next pulse is committed.
    var lookaheadSec = (this.schedTickMs * 2) / 1000.0
    if (this.nextEventTime > now + lookaheadSec) return

    // Promote any pending spec into current before committing — the
    // pending hand-off normally happens at pulse-end here, but if
    // _maybeAdoptTarget queued one between ticks (with no pulse
    // boundary having been reached), this is the boundary.
    if (this.pendingTone !== null) {
      this.currentTone   = this.pendingTone
      this.currentSpec   = this.pendingSpec
      this.currentVolume = this.pendingVolume
      this.pendingTone   = null
      this.pendingSpec   = null
      if (this.currentTone === "none") return
      if (this.currentSpec && this.currentSpec.isSolid) {
        this._scheduleSolid(this.nextEventTime, this.currentSpec,
                            this.currentVolume, this.currentTone)
        this.solidArmed = true
        return
      }
    }

    var spec   = this.currentSpec
    var tone   = this.currentTone
    var vol    = this.currentVolume
    var period = spec.delay + spec.attack + spec.hold + spec.decay + spec.gap

    this._schedulePulse(this.nextEventTime, spec, vol, tone)
    this.nextEventTime += period
  }

  // Schedule a single pulse at startTime on the given tone's gain.
  // Writes timeline events for delay → attack → hold → decay → gap.
  // The other tone's gain is silenced at startTime so a carrier
  // switch (low ↔ high) is clean.
  ToneSimulator.prototype._schedulePulse = function (startTime, spec, volume, tone) {
    var activeGain = (tone === "high") ? this.highGain : this.lowGain
    var otherGain  = (tone === "high") ? this.lowGain  : this.highGain
    if (!activeGain) return

    // Force the other carrier to silence at the start of this pulse
    // so a low → high (or high → low) switch is sample-accurate.
    otherGain.gain.cancelScheduledValues(startTime)
    otherGain.gain.setValueAtTime(0, startTime)

    var p = activeGain.gain
    p.cancelScheduledValues(startTime)
    p.setValueAtTime(0, startTime)

    var t = startTime + spec.delay
    // Attack: ramp 0 → volume.
    p.setValueAtTime(0, t)
    t += spec.attack
    p.linearRampToValueAtTime(volume, t)
    // Hold at volume.
    if (spec.hold > 0) {
      t += spec.hold
      p.linearRampToValueAtTime(volume, t)
    }
    // Decay: ramp volume → 0.
    t += spec.decay
    p.linearRampToValueAtTime(0, t)
    // Gap (silence) — no event needed; level is already 0 and stays 0
    // until the next pulse's setValueAtTime at the cycle boundary.
  }

  // Schedule a solid tone arming at startTime.  Delay → attack →
  // sustain at `volume` indefinitely.  Caller must invoke
  // _releaseSolidNow() to start the release ramp on a tone change.
  ToneSimulator.prototype._scheduleSolid = function (startTime, spec, volume, tone) {
    var activeGain = (tone === "high") ? this.highGain : this.lowGain
    var otherGain  = (tone === "high") ? this.lowGain  : this.highGain
    if (!activeGain) return

    otherGain.gain.cancelScheduledValues(startTime)
    otherGain.gain.setValueAtTime(0, startTime)

    var p = activeGain.gain
    p.cancelScheduledValues(startTime)
    p.setValueAtTime(0, startTime)

    var t = startTime + spec.delay
    p.setValueAtTime(0, t)
    t += spec.attack
    p.linearRampToValueAtTime(volume, t)
    // Sustain: leave the timeline empty after this point so the value
    // latches at `volume` until something cancels it.
  }

  // Trigger a release ramp on the currently-sustaining solid tone.
  // The release tail rides out under the next spec's silent_delay,
  // so the audible gap before the new tone is roughly delay − release.
  ToneSimulator.prototype._releaseSolidNow = function () {
    if (!this.audioCtx || !this.solidArmed) return
    var activeGain = (this.currentTone === "high") ? this.highGain : this.lowGain
    if (!activeGain) return

    var now = this.audioCtx.currentTime
    var releaseSec = this.currentSpec ? this.currentSpec.release
                                      : (TONE_RAMP_MS / 1000.0)

    var p = activeGain.gain
    p.cancelScheduledValues(now)
    // Start the release from the current value (linearRampToValueAtTime
    // needs an explicit anchor on the current value to ramp from).
    p.setValueAtTime(p.value, now)
    p.linearRampToValueAtTime(0, now + releaseSec)

    this.solidArmed = false

    // Adopt the pending spec on the release tail.  The new pulse
    // schedule starts at `now + releaseSec` so its silent delay
    // overlaps the tail of the release window.
    if (this.pendingTone !== null) {
      this.currentTone   = this.pendingTone
      this.currentSpec   = this.pendingSpec
      this.currentVolume = this.pendingVolume
      this.pendingTone   = null
      this.pendingSpec   = null
      this.nextEventTime = now + releaseSec
      if (this.currentTone === "none") return
    } else {
      this.nextEventTime = now + releaseSec
      this.currentTone = "none"
      this.currentSpec = null
    }
  }

  // ── Audio start/stop ─────────────────────────────────────────
  ToneSimulator.prototype._startAudio = function () {
    this.state.audioOn = true
    this.soundBtn.textContent = "Sound On"
    this.soundBtn.classList.add("active")

    if (!this.audioCtx) {
      this.audioCtx = new (window.AudioContext || window.webkitAudioContext)()
    }
    if (this.audioCtx.state === "suspended") {
      this.audioCtx.resume()
    }

    this.masterGain = this.audioCtx.createGain()
    this.masterGain.gain.value = MASTER_GAIN
    this.masterGain.connect(this.audioCtx.destination)

    // Continuous-phase carriers — both run for the lifetime of the
    // audio session.  Their gates ({low,high}Gain) hold at 0 except
    // during the active phases of a pulse, so they're inaudible when
    // the other carrier is in use.
    this.lowGain = this.audioCtx.createGain()
    this.lowGain.gain.value = 0
    this.lowGain.connect(this.masterGain)
    this.lowOsc = this.audioCtx.createOscillator()
    this.lowOsc.type = "sine"
    this.lowOsc.frequency.value = LOW_TONE_HZ
    this.lowOsc.connect(this.lowGain)
    this.lowOsc.start()

    this.highGain = this.audioCtx.createGain()
    this.highGain.gain.value = 0
    this.highGain.connect(this.masterGain)
    this.highOsc = this.audioCtx.createOscillator()
    this.highOsc.type = "sine"
    this.highOsc.frequency.value = HIGH_TONE_HZ
    this.highOsc.connect(this.highGain)
    this.highOsc.start()

    this.currentTone   = "none"
    this.currentSpec   = null
    this.currentVolume = STALL_VOL_MIN
    this.pendingTone   = null
    this.pendingSpec   = null
    this.solidArmed    = false
    this.nextEventTime = this.audioCtx.currentTime

    // Adopt whatever the slider says now.
    this._maybeAdoptTarget()
    this._kickScheduler()
  }

  ToneSimulator.prototype._stopAudio = function () {
    this.state.audioOn = false
    this.soundBtn.textContent = "Sound On"
    this.soundBtn.classList.remove("active")

    if (this.schedTimerId !== null) {
      clearTimeout(this.schedTimerId)
      this.schedTimerId = null
    }

    var nodes = [this.lowOsc, this.highOsc]
    for (var i = 0; i < nodes.length; i++) {
      if (nodes[i]) {
        try { nodes[i].stop() } catch (e) { /* ok */ }
        nodes[i].disconnect()
      }
    }
    if (this.lowGain)    { this.lowGain.disconnect();    this.lowGain    = null }
    if (this.highGain)   { this.highGain.disconnect();   this.highGain   = null }
    if (this.masterGain) { this.masterGain.disconnect(); this.masterGain = null }
    this.lowOsc      = null
    this.highOsc     = null

    this.currentTone   = "none"
    this.currentSpec   = null
    this.pendingTone   = null
    this.pendingSpec   = null
    this.solidArmed    = false
  }

  // ── Drawing ──────────────────────────────────────────────────
  ToneSimulator.prototype._draw = function () {
    var svg = this.svg
    while (svg.firstChild) svg.removeChild(svg.firstChild)

    var aoa = this.state.aoa
    var tone = decideTone(aoa)

    var defs = svgEl("defs")
    svg.appendChild(defs)

    svg.appendChild(svgEl("rect", {
      x: 0, y: 0, width: SVG_W, height: SVG_H,
      fill: "var(--ts-bg)",
    }))

    this._drawBands(svg, tone)
    this._drawFanLines(svg)
    this._drawActiveIndicator(svg, aoa, tone)
    this._drawAircraft(svg, aoa)
  }

  ToneSimulator.prototype._drawBands = function (svg, tone) {
    var bands = [
      { aLo: TH.stallWarn, aHi: AOA_MAX, fill: "var(--ts-stall-fill)",
        label: "STALL", region: "stall" },
      { aLo: TH.onspeedSlow, aHi: TH.stallWarn, fill: "var(--ts-slow-fill)",
        label: "SLOW", region: "slow" },
      { aLo: TH.onspeedFast, aHi: TH.onspeedSlow, fill: "var(--ts-onspeed-fill)",
        label: "ONSPEED", region: "onspeed" },
      { aLo: TH.ldmax, aHi: TH.onspeedFast, fill: "var(--ts-fast-fill)",
        label: "L/Dmax", region: "fast" },
      { aLo: AOA_MIN, aHi: TH.ldmax, fill: "var(--ts-silent-fill)",
        label: "", region: "silent" },
    ]

    for (var i = 0; i < bands.length; i++) {
      var b = bands[i]
      var yTop = aoaToY(b.aHi)
      var yBot = aoaToY(b.aLo)
      var active = (b.region === tone.region)

      svg.appendChild(svgEl("rect", {
        x: BAND_LEFT, y: yTop,
        width: BAND_RIGHT - BAND_LEFT, height: yBot - yTop,
        fill: b.fill,
        stroke: "var(--ts-band-stroke)",
        "stroke-width": 0.5,
        opacity: active ? 1.0 : 0.35,
      }))

      if (b.label) {
        var labelY = (yTop + yBot) / 2 + 5
        svg.appendChild(svgEl("text", {
          x: BAND_LEFT + 10, y: labelY,
          fill: active ? "var(--ts-text)" : "var(--ts-text-dim)",
          "font-size": active ? "0.9em" : "0.78em",
          "font-weight": active ? "800" : "600",
        })).textContent = b.label
      }
    }
  }

  ToneSimulator.prototype._drawFanLines = function (svg) {
    var angles = []
    for (var a = AOA_MIN; a <= AOA_MAX; a += 1) angles.push(a)
    var ths = [TH.ldmax, TH.onspeedFast, TH.onspeedSlow, TH.stallWarn, TH.stall]
    for (var t = 0; t < ths.length; t++) {
      if (angles.indexOf(ths[t]) < 0) angles.push(ths[t])
    }

    for (var i = 0; i < angles.length; i++) {
      var y = aoaToY(angles[i])
      svg.appendChild(svgEl("line", {
        x1: BAND_RIGHT, y1: y,
        x2: FOCAL_X, y2: FOCAL_Y,
        stroke: "var(--ts-fan-line)",
        "stroke-width": 0.5,
        opacity: 0.4,
      }))
    }
  }

  ToneSimulator.prototype._drawActiveIndicator = function (svg, aoa, tone) {
    var y = aoaToY(aoa)
    var colors = {
      stall: "var(--ts-stall-fill)", slow: "var(--ts-slow-fill)",
      onspeed: "var(--ts-onspeed-fill)", fast: "var(--ts-fast-fill)",
      silent: "var(--ts-fan-line)",
    }
    var color = colors[tone.region] || "var(--ts-fan-line)"

    svg.appendChild(svgEl("line", {
      x1: BAND_RIGHT, y1: y, x2: FOCAL_X, y2: FOCAL_Y,
      stroke: "var(--ts-fan-active)", "stroke-width": 2.5,
    }))
    svg.appendChild(svgEl("circle", {
      cx: BAND_RIGHT, cy: y, r: 5,
      fill: color, stroke: "var(--ts-fan-active)", "stroke-width": 1.5,
    }))
    svg.appendChild(svgEl("circle", {
      cx: FOCAL_X, cy: FOCAL_Y, r: 3,
      fill: "var(--ts-fan-active)",
    }))
  }

  ToneSimulator.prototype._drawAircraft = function (svg, aoa) {
    // Aircraft faces LEFT (nose toward the bands).
    // Positive AOA pitches nose up = counter-clockwise rotation.
    var g = svgEl("g", {
      transform: "rotate(" + (aoa) + " " + AC_CX + " " + AC_CY + ")",
    })

    var s = 1.4
    var cx = AC_CX, cy = AC_CY
    var fuseLen = 110 * s
    var fuseH = 13 * s

    g.appendChild(svgEl("ellipse", {
      cx: cx, cy: cy, rx: fuseLen / 2, ry: fuseH / 2,
      fill: "var(--ts-aircraft)",
    }))

    var noseX = cx - fuseLen / 2
    g.appendChild(svgEl("polygon", {
      points:
        (noseX - 5 * s) + "," + cy + " " +
        (noseX + 12 * s) + "," + (cy - fuseH / 2) + " " +
        (noseX + 12 * s) + "," + (cy + fuseH / 2),
      fill: "var(--ts-aircraft)",
    }))

    g.appendChild(svgEl("ellipse", {
      cx: noseX - 6 * s, cy: cy, rx: 4 * s, ry: 5 * s,
      fill: "var(--ts-aircraft-accent)",
    }))

    var canopyX = cx - 12 * s
    g.appendChild(svgEl("ellipse", {
      cx: canopyX, cy: cy - fuseH / 2,
      rx: 16 * s, ry: 7 * s,
      fill: "var(--ts-aircraft)",
      stroke: "var(--ts-bg)", "stroke-width": 1.5,
    }))
    g.appendChild(svgEl("ellipse", {
      cx: canopyX, cy: cy - fuseH / 2 - 1,
      rx: 13 * s, ry: 5 * s,
      fill: "var(--ts-bg)", opacity: 0.3,
    }))

    var wingX = cx - 6 * s
    g.appendChild(svgEl("line", {
      x1: wingX, y1: cy - 58 * s, x2: wingX, y2: cy + 58 * s,
      stroke: "var(--ts-aircraft)",
      "stroke-width": 6 * s, "stroke-linecap": "round",
    }))

    var tailX = cx + fuseLen / 2 + 3 * s
    g.appendChild(svgEl("polygon", {
      points:
        (tailX - 18 * s) + "," + cy + " " +
        (tailX) + "," + (cy - 28 * s) + " " +
        (tailX - 22 * s) + "," + cy,
      fill: "var(--ts-aircraft)",
    }))

    g.appendChild(svgEl("line", {
      x1: tailX - 5 * s, y1: cy - 18 * s,
      x2: tailX - 5 * s, y2: cy + 18 * s,
      stroke: "var(--ts-aircraft)",
      "stroke-width": 4 * s, "stroke-linecap": "round",
    }))

    var gearX = cx - 18 * s
    var gearY = cy + fuseH / 2
    g.appendChild(svgEl("line", {
      x1: gearX, y1: gearY, x2: gearX, y2: gearY + 16 * s,
      stroke: "var(--ts-text-dim)", "stroke-width": 2,
    }))
    g.appendChild(svgEl("circle", {
      cx: gearX, cy: gearY + 18 * s, r: 5 * s,
      fill: "var(--ts-text-dim)",
    }))

    svg.appendChild(g)
  }

  // ── Cleanup ──────────────────────────────────────────────────
  ToneSimulator.prototype.destroy = function () {
    this._stopAudio()
    if (this.audioCtx) {
      this.audioCtx.close()
      this.audioCtx = null
    }
  }

  // ── Init ─────────────────────────────────────────────────────
  var activeSimulator = null

  function init() {
    var container = document.getElementById("tone-simulator")
    if (!container) {
      if (activeSimulator) {
        activeSimulator.destroy()
        activeSimulator = null
      }
      return
    }
    if (container.querySelector("svg")) return
    injectStyles()
    if (activeSimulator) activeSimulator.destroy()
    activeSimulator = new ToneSimulator(container)
  }

  if (typeof document$ !== "undefined") {
    document$.subscribe(function () { init() })
  } else {
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", init)
    } else {
      init()
    }
  }
})()
