/**
 * Interactive AOA Tone Simulator for OnSpeed Documentation
 *
 * Replicates the physical OnSpeed demo board: a side-view aircraft that
 * pitches with AOA, with a fan of reference lines radiating from the nose
 * into colored tone-region bands (STALL / SLOW / ONSPEED / L/Dmax).
 *
 * Audio uses a free-running pulse loop so that sliding the AOA control
 * smoothly changes pulse rate without glitches or restarts.
 *
 * Self-contained IIFE — no external dependencies.
 */
;(function () {
  "use strict"

  // ── Tone constants (ToneCalc.h / Audio.cpp) ──────────────────
  var LOW_TONE_HZ         = 400
  var HIGH_TONE_HZ        = 1600
  var HIGH_TONE_STALL_PPS = 20.0
  var HIGH_TONE_PPS_MIN   = 1.5
  var HIGH_TONE_PPS_MAX   = 6.2
  var LOW_TONE_PPS_MIN    = 1.5
  var LOW_TONE_PPS_MAX    = 8.2
  var PULSE_ON_GAIN       = 1.0
  var PULSE_OFF_GAIN      = 0.2

  // Default AOA thresholds (degrees) — representative RV-4 values
  var TH = {
    ldmax:       5.0,
    onspeedFast: 7.0,
    onspeedSlow: 9.0,
    stallWarn:  13.0,
    stall:      15.0,
  }

  // ── AOA-to-tone mapping (mirrors ToneCalc.cpp) ────────────────
  function mapfloat(x, inMin, inMax, outMin, outMax) {
    return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin
  }

  function calculateTone(aoa) {
    if (aoa >= TH.stallWarn)
      return { type: "high", pps: HIGH_TONE_STALL_PPS, region: "stall" }
    if (aoa > TH.onspeedSlow) {
      var pps = mapfloat(aoa, TH.onspeedSlow, TH.stallWarn,
                         HIGH_TONE_PPS_MIN, HIGH_TONE_PPS_MAX)
      return { type: "high", pps: pps, region: "slow" }
    }
    if (aoa >= TH.onspeedFast)
      return { type: "low", pps: 0, region: "onspeed" }
    if (aoa >= TH.ldmax) {
      var pps2 = mapfloat(aoa, TH.ldmax, TH.onspeedFast,
                          LOW_TONE_PPS_MIN, LOW_TONE_PPS_MAX)
      return { type: "low", pps: pps2, region: "fast" }
    }
    return { type: "none", pps: 0, region: "silent" }
  }

  // ── Layout constants ─────────────────────────────────────────
  var SVG_W = 820
  var SVG_H = 360

  // Colored band area (left side)
  var BAND_LEFT = 20
  var BAND_RIGHT = 200
  var BAND_TOP = 20
  var BAND_BOTTOM = 340

  // Focal point where fan lines converge (near the aircraft nose)
  // Aircraft faces LEFT (nose toward bands), so focal point is left of aircraft center
  var FOCAL_X = 380
  var FOCAL_Y = 180

  // Aircraft center
  var AC_CX = 560
  var AC_CY = 180

  // AOA range
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
    this.audioCtx = null
    this.oscillator = null
    this.gainNode = null
    this.pulseGain = null

    // Free-running pulse state — never killed/recreated by slider
    this.targetToneType = "none"   // "none", "low", "high"
    this.targetPPS = 0
    this.pulsePhase = 0            // accumulator in seconds
    this.pulseOn = true
    this.lastFrameTime = 0
    this.animFrameId = null

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
    this.soundBtn.textContent = "Sound Off"
    controls.appendChild(this.soundBtn)

    var aoaCtrl = document.createElement("div")
    aoaCtrl.className = "ts-control"
    aoaCtrl.innerHTML =
      '<label for="ts-aoa">AOA:</label>' +
      '<input type="range" id="ts-aoa" min="-2" max="18" step="0.1" value="0">' +
      '<span class="ts-val" id="ts-aoa-val">0.0\u00B0</span>'
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
  ToneSimulator.prototype.update = function () {
    var tone = calculateTone(this.state.aoa)
    this.valAOA.textContent = this.state.aoa.toFixed(1) + "\u00B0"

    // Just update the targets — the pulse loop reads them live
    this.targetPPS = tone.pps

    if (tone.type !== this.targetToneType) {
      this.targetToneType = tone.type
      this._applyToneType(tone.type)
      // Reset pulse phase on region change so it starts clean
      this.pulsePhase = 0
      this.pulseOn = true
      // Immediately slam gain to full — don't wait for rAF loop
      if (this.pulseGain && this.audioCtx) {
        var t = this.audioCtx.currentTime
        this.pulseGain.gain.cancelScheduledValues(t)
        this.pulseGain.gain.setValueAtTime(PULSE_ON_GAIN, t)
      }
    }

    // Also: if we're in solid-tone mode (pps === 0), ensure gain is full
    // even without a region change (handles fast->onspeed->fast bouncing)
    if (tone.pps === 0 && tone.type !== "none" && this.pulseGain && this.audioCtx) {
      var t2 = this.audioCtx.currentTime
      this.pulseGain.gain.cancelScheduledValues(t2)
      this.pulseGain.gain.setValueAtTime(PULSE_ON_GAIN, t2)
      this.pulseOn = true
    }

    this._draw()
  }

  // ── Apply tone type change (only on region transitions) ──────
  ToneSimulator.prototype._applyToneType = function (type) {
    if (!this.audioCtx || !this.state.audioOn) return

    if (type === "none") {
      this._killOscillator()
      return
    }

    var freq = (type === "low") ? LOW_TONE_HZ : HIGH_TONE_HZ

    // Always kill and recreate — instant frequency jump, no glide
    this._killOscillator()
    this.oscillator = this.audioCtx.createOscillator()
    this.oscillator.type = "sine"
    this.oscillator.frequency.value = freq
    this.oscillator.connect(this.pulseGain)
    this.oscillator.start()
  }

  // ── Free-running pulse loop (rAF-driven) ─────────────────────
  // This runs continuously while audio is on. It reads targetPPS
  // each frame and advances a phase accumulator. The slider never
  // kills or restarts this loop — it just changes the rate.
  ToneSimulator.prototype._startPulseLoop = function () {
    var self = this
    this.lastFrameTime = performance.now()
    this.pulsePhase = 0
    this.pulseOn = true

    function tick(now) {
      if (!self.state.audioOn) return

      var dt = (now - self.lastFrameTime) / 1000  // seconds
      self.lastFrameTime = now

      // Clamp dt to avoid jumps when tab is backgrounded
      if (dt > 0.1) dt = 0.1

      var pps = self.targetPPS

      if (self.targetToneType === "none" || !self.pulseGain) {
        self.animFrameId = requestAnimationFrame(tick)
        return
      }

      if (pps > 0 && pps <= 25) {
        // Pulsing mode: advance phase accumulator
        var halfPeriod = 1.0 / (pps * 2)  // seconds per half-cycle
        self.pulsePhase += dt

        // Check if we crossed a half-period boundary
        if (self.pulsePhase >= halfPeriod) {
          self.pulsePhase -= halfPeriod
          // Clamp in case of big jumps
          if (self.pulsePhase > halfPeriod) self.pulsePhase = 0
          self.pulseOn = !self.pulseOn

          var t = self.audioCtx.currentTime
          self.pulseGain.gain.setTargetAtTime(
            self.pulseOn ? PULSE_ON_GAIN : PULSE_OFF_GAIN,
            t, 0.003
          )
        }
      } else {
        // Solid tone (pps === 0) or out of range
        if (!self.pulseOn) {
          self.pulseOn = true
          var t2 = self.audioCtx.currentTime
          self.pulseGain.gain.setTargetAtTime(PULSE_ON_GAIN, t2, 0.01)
        }
        self.pulsePhase = 0
      }

      self.animFrameId = requestAnimationFrame(tick)
    }

    this.animFrameId = requestAnimationFrame(tick)
  }

  ToneSimulator.prototype._stopPulseLoop = function () {
    if (this.animFrameId) {
      cancelAnimationFrame(this.animFrameId)
      this.animFrameId = null
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

    this.gainNode = this.audioCtx.createGain()
    this.gainNode.gain.value = 0.35
    this.gainNode.connect(this.audioCtx.destination)

    this.pulseGain = this.audioCtx.createGain()
    this.pulseGain.gain.value = PULSE_ON_GAIN
    this.pulseGain.connect(this.gainNode)

    var tone = calculateTone(this.state.aoa)
    this.targetToneType = tone.type
    this.targetPPS = tone.pps
    this._applyToneType(tone.type)
    this._startPulseLoop()
  }

  ToneSimulator.prototype._stopAudio = function () {
    this.state.audioOn = false
    this.soundBtn.textContent = "Sound Off"
    this.soundBtn.classList.remove("active")

    this._stopPulseLoop()
    this._killOscillator()
    this.targetToneType = "none"
  }

  ToneSimulator.prototype._killOscillator = function () {
    if (this.oscillator) {
      try { this.oscillator.stop() } catch (e) { /* ok */ }
      this.oscillator.disconnect()
      this.oscillator = null
    }
  }

  // ── Drawing ──────────────────────────────────────────────────
  ToneSimulator.prototype._draw = function () {
    var svg = this.svg
    while (svg.firstChild) svg.removeChild(svg.firstChild)

    var aoa = this.state.aoa
    var tone = calculateTone(aoa)

    var defs = svgEl("defs")
    svg.appendChild(defs)

    // Background
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
    // Aircraft faces LEFT (nose toward the bands), matching the physical board.
    // Positive AOA pitches nose up = counter-clockwise rotation.
    var g = svgEl("g", {
      transform: "rotate(" + (aoa) + " " + AC_CX + " " + AC_CY + ")",
    })

    var s = 1.4
    var cx = AC_CX, cy = AC_CY
    var fuseLen = 110 * s
    var fuseH = 13 * s

    // Fuselage
    g.appendChild(svgEl("ellipse", {
      cx: cx, cy: cy, rx: fuseLen / 2, ry: fuseH / 2,
      fill: "var(--ts-aircraft)",
    }))

    // Nose (LEFT side)
    var noseX = cx - fuseLen / 2
    g.appendChild(svgEl("polygon", {
      points:
        (noseX - 5 * s) + "," + cy + " " +
        (noseX + 12 * s) + "," + (cy - fuseH / 2) + " " +
        (noseX + 12 * s) + "," + (cy + fuseH / 2),
      fill: "var(--ts-aircraft)",
    }))

    // Spinner
    g.appendChild(svgEl("ellipse", {
      cx: noseX - 6 * s, cy: cy, rx: 4 * s, ry: 5 * s,
      fill: "var(--ts-aircraft-accent)",
    }))

    // Canopy (on top, slightly toward nose)
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

    // Wing (slightly aft of center)
    var wingX = cx - 6 * s
    g.appendChild(svgEl("line", {
      x1: wingX, y1: cy - 58 * s, x2: wingX, y2: cy + 58 * s,
      stroke: "var(--ts-aircraft)",
      "stroke-width": 6 * s, "stroke-linecap": "round",
    }))

    // Vertical tail (RIGHT side = aft)
    var tailX = cx + fuseLen / 2 + 3 * s
    g.appendChild(svgEl("polygon", {
      points:
        (tailX - 18 * s) + "," + cy + " " +
        (tailX) + "," + (cy - 28 * s) + " " +
        (tailX - 22 * s) + "," + cy,
      fill: "var(--ts-aircraft)",
    }))

    // Horizontal tail
    g.appendChild(svgEl("line", {
      x1: tailX - 5 * s, y1: cy - 18 * s,
      x2: tailX - 5 * s, y2: cy + 18 * s,
      stroke: "var(--ts-aircraft)",
      "stroke-width": 4 * s, "stroke-linecap": "round",
    }))

    // Landing gear
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
