/**
 * Interactive V-n Diagram for OnSpeed Documentation
 *
 * Visual style matches Figure 5 from "Using Angle of Attack for
 * Maneuvering and Aircraft Control for Pilots" (Vaccaro, 2026).
 *
 * Self-contained SVG + vanilla JS — no external dependencies.
 */
;(function () {
  "use strict"

  // ── RV-4 defaults ──────────────────────────────────────────────
  var DEFAULTS = {
    Vs:       62,     // clean stall speed at max gross, KIAS
    Wmax:     1375,   // max gross weight, lbs
    Vno:      165,    // max structural cruising speed, KIAS
    Vne:      200,    // never exceed speed, KIAS
    Vd:       230,    // design dive speed, KIAS
    nPos:     6.0,    // positive G limit
    nNeg:    -3.0,    // negative G limit
    // Normalized AOA fractions (from lift-equation fit)
    naoaStall:      1.000,
    naoaStallWarn:  0.900,
    naoaOnspeedSlow: 0.640,
    naoaOnspeedFast: 0.592,
    naoaLdmax:      0.549,
  }

  // ── Layout constants ───────────────────────────────────────────
  var SVG_W   = 820
  var SVG_H   = 540
  var PAD     = { top: 40, right: 30, bottom: 55, left: 60 }
  var PLOT_X  = PAD.left
  var PLOT_Y  = PAD.top
  var PLOT_W  = SVG_W - PAD.left - PAD.right
  var PLOT_H  = SVG_H - PAD.top - PAD.bottom
  var V_MIN   = 0
  var V_MAX   = 250   // matches PDF x-axis
  var N_MIN   = -4
  var N_MAX   = 9     // matches PDF y-axis

  // ── Helpers ────────────────────────────────────────────────────
  function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }

  function vToX(v) { return PLOT_X + (v - V_MIN) / (V_MAX - V_MIN) * PLOT_W }
  function nToY(n) { return PLOT_Y + (N_MAX - n) / (N_MAX - N_MIN) * PLOT_H }
  function xToV(x) { return V_MIN + (x - PLOT_X) / PLOT_W * (V_MAX - V_MIN) }
  function yToN(y) { return N_MAX - (y - PLOT_Y) / PLOT_H * (N_MAX - N_MIN) }

  function vStall(vs1g, n) { return vs1g * Math.sqrt(Math.abs(n)) }

  function vAtNaoa(vs1g, n, naoa) {
    if (naoa <= 0) return Infinity
    return vs1g * Math.sqrt(Math.abs(n) / naoa)
  }

  function vs1gAtWeight(w) {
    return DEFAULTS.Vs * Math.sqrt(w / DEFAULTS.Wmax)
  }

  function nFromBank(bankDeg) {
    var rad = bankDeg * Math.PI / 180
    var cosB = Math.cos(rad)
    if (cosB < 0.01) return 100
    return 1 / cosB
  }

  // ── Color palette — matches Figure 5 from Vaccaro PDF ─────────
  var STYLE_ID = "vn-diagram-styles"

  function injectStyles() {
    if (document.getElementById(STYLE_ID)) return
    var css = [
      // Light theme — exact PDF colors
      "[data-md-color-scheme='default'] .vn-wrap, :root .vn-wrap {",
      "  --vn-bg: #ffffff;",
      "  --vn-grid: #d0d0d0;",
      "  --vn-axis: #000000;",
      "  --vn-text: #000000;",
      "  --vn-text-dim: #555555;",
      // Tone region fills — PDF Figure 5 colors
      "  --vn-stall-fill: rgba(230, 50, 50, 0.30);",       // red/pink
      "  --vn-slow-fill: rgba(255, 230, 0, 0.40);",         // yellow
      "  --vn-onspeed-fill: rgba(50, 180, 50, 0.40);",      // green
      "  --vn-fast-fill: rgba(100, 180, 240, 0.30);",       // light blue
      "  --vn-caution-fill: rgba(255, 230, 0, 0.25);",      // yellow caution (Vno–Vne)
      // Curve strokes
      "  --vn-stall-curve: #000000;",
      "  --vn-aoa-curve: #000000;",
      // Structural
      "  --vn-hatch-stroke: #cc3333;",                       // red hatching
      "  --vn-structural-line: #cc3333;",
      // Landmark colors
      "  --vn-1g: #000000;",
      "  --vn-bank: #9c27b0;",
      "  --vn-vno: #000000;",
      "  --vn-vne: #cc3333;",
      "  --vn-vd: #cc3333;",
      "  --vn-tp-fill: none;",
      "  --vn-tp-stroke: #cc3333;",
      // Draggable point
      "  --vn-point: #cc3333;",
      "  --vn-point-stroke: #ffffff;",
      // Labels
      "  --vn-label-stall: #cc3333;",
      "  --vn-label-onspeed: #1a7a1a;",
      "  --vn-label-ldmax: #1a7a1a;",
      "  --vn-label-slow: #b8860b;",
      "  --vn-label-fast: #2266aa;",
      "  --vn-label-tp: #cc3333;",
      "  --vn-label-vref: #cc3333;",
      "}",
      // Dark theme
      "[data-md-color-scheme='slate'] .vn-wrap {",
      "  --vn-bg: #1e1e1e;",
      "  --vn-grid: #3a3a3a;",
      "  --vn-axis: #cccccc;",
      "  --vn-text: #cccccc;",
      "  --vn-text-dim: #999999;",
      "  --vn-stall-fill: rgba(239, 83, 80, 0.30);",
      "  --vn-slow-fill: rgba(255, 235, 59, 0.30);",
      "  --vn-onspeed-fill: rgba(102, 187, 106, 0.35);",
      "  --vn-fast-fill: rgba(100, 181, 246, 0.25);",
      "  --vn-caution-fill: rgba(255, 235, 59, 0.15);",
      "  --vn-stall-curve: #cccccc;",
      "  --vn-aoa-curve: #cccccc;",
      "  --vn-hatch-stroke: #ef5350;",
      "  --vn-structural-line: #ef5350;",
      "  --vn-1g: #cccccc;",
      "  --vn-bank: #ce93d8;",
      "  --vn-vno: #cccccc;",
      "  --vn-vne: #ef5350;",
      "  --vn-vd: #ef5350;",
      "  --vn-tp-fill: none;",
      "  --vn-tp-stroke: #ef5350;",
      "  --vn-point: #ef5350;",
      "  --vn-point-stroke: #1e1e1e;",
      "  --vn-label-stall: #ef5350;",
      "  --vn-label-onspeed: #66bb6a;",
      "  --vn-label-ldmax: #66bb6a;",
      "  --vn-label-slow: #fdd835;",
      "  --vn-label-fast: #64b5f6;",
      "  --vn-label-tp: #ef5350;",
      "  --vn-label-vref: #ef5350;",
      "}",
      // Layout
      ".vn-wrap { position: relative; max-width: 860px; margin: 0 auto; }",
      ".vn-wrap svg { display: block; width: 100%; height: auto; }",
      ".vn-controls { display: flex; flex-wrap: wrap; gap: 12px 24px; ",
      "  padding: 12px 0 4px; align-items: center; }",
      ".vn-control { display: flex; align-items: center; gap: 8px; ",
      "  font-size: 0.85em; color: var(--vn-text); }",
      ".vn-control label { white-space: nowrap; min-width: 90px; }",
      ".vn-control input[type=range] { width: 140px; }",
      ".vn-control .vn-val { min-width: 62px; font-variant-numeric: tabular-nums;",
      "  font-family: var(--md-code-font-family, monospace); font-size: 0.95em; }",
      ".vn-control input[type=checkbox] { width: 16px; height: 16px; }",
      ".vn-info-panel { position: absolute; top: 46px; right: 36px;",
      "  background: var(--vn-bg); border: 1px solid var(--vn-grid);",
      "  border-radius: 6px; padding: 8px 12px; font-size: 0.82em;",
      "  line-height: 1.55; color: var(--vn-text); pointer-events: none;",
      "  font-family: var(--md-code-font-family, monospace);",
      "  box-shadow: 0 2px 8px rgba(0,0,0,0.10);",
      "  font-variant-numeric: tabular-nums; min-width: 180px; }",
      ".vn-info-panel.hidden { display: none; }",
      ".vn-info-panel dt { font-weight: 600; display: inline; }",
      ".vn-info-panel dd { display: inline; margin: 0; }",
      ".vn-info-panel .vn-row { margin-bottom: 2px; }",
      ".vn-info-panel .vn-tone-badge { display: inline-block; padding: 1px 6px;",
      "  border-radius: 3px; font-weight: 600; font-size: 0.9em; margin-left: 2px; }",
    ].join("\n")
    var el = document.createElement("style")
    el.id = STYLE_ID
    el.textContent = css
    document.head.appendChild(el)
  }

  // ── SVG namespace helper ───────────────────────────────────────
  var NS = "http://www.w3.org/2000/svg"
  function svgEl(tag, attrs) {
    var el = document.createElementNS(NS, tag)
    if (attrs) Object.keys(attrs).forEach(function (k) { el.setAttribute(k, attrs[k]) })
    return el
  }

  // ── Main diagram class ────────────────────────────────────────
  function VnDiagram(container) {
    this.container = container
    this.state = {
      weight: DEFAULTS.Wmax,
      bank: 0,
      showTones: true,
      showTrafficPattern: true,
      showPoint: false,
      pointV: 100,
      pointN: 1.0,
    }
    this.dragging = false
    this._buildDOM()
    this._bindControls()
    this._observeTheme()
    this.draw()
  }

  VnDiagram.prototype._buildDOM = function () {
    this.container.classList.add("vn-wrap")

    this.svg = svgEl("svg", {
      viewBox: "0 0 " + SVG_W + " " + SVG_H,
      preserveAspectRatio: "xMidYMid meet",
    })
    this.container.insertBefore(this.svg, this.container.firstChild)

    // Defs
    var defs = svgEl("defs")
    this.svg.appendChild(defs)

    // Clip rect for plot area
    var clip = svgEl("clipPath", { id: "vn-clip" })
    clip.appendChild(svgEl("rect", { x: PLOT_X, y: PLOT_Y, width: PLOT_W, height: PLOT_H }))
    defs.appendChild(clip)

    // Red hatch pattern (light) — matches PDF structural limits
    var hatchL = svgEl("pattern", {
      id: "hatch-light", width: 6, height: 6,
      patternUnits: "userSpaceOnUse", patternTransform: "rotate(45)",
    })
    hatchL.appendChild(svgEl("line", {
      x1: 0, y1: 0, x2: 0, y2: 6, stroke: "#cc3333", "stroke-width": 1.2,
    }))
    defs.appendChild(hatchL)

    // Red hatch pattern (dark)
    var hatchD = svgEl("pattern", {
      id: "hatch-dark", width: 6, height: 6,
      patternUnits: "userSpaceOnUse", patternTransform: "rotate(45)",
    })
    hatchD.appendChild(svgEl("line", {
      x1: 0, y1: 0, x2: 0, y2: 6, stroke: "#ef5350", "stroke-width": 1.2,
    }))
    defs.appendChild(hatchD)

    // Drawing layers (bottom to top)
    this.layerGrid    = svgEl("g", { class: "l-grid" })
    this.layerCaution = svgEl("g", { class: "l-caution", "clip-path": "url(#vn-clip)" })
    this.layerFills   = svgEl("g", { class: "l-fills", "clip-path": "url(#vn-clip)" })
    this.layerStruct  = svgEl("g", { class: "l-struct", "clip-path": "url(#vn-clip)" })
    this.layerCurves  = svgEl("g", { class: "l-curves", "clip-path": "url(#vn-clip)" })
    this.layerVspeeds = svgEl("g", { class: "l-vspeeds" })
    this.layerTP      = svgEl("g", { class: "l-tp", "clip-path": "url(#vn-clip)" })
    this.layer1G      = svgEl("g", { class: "l-1g" })
    this.layerBank    = svgEl("g", { class: "l-bank" })
    this.layerPoint   = svgEl("g", { class: "l-point" })
    this.layerAxes    = svgEl("g", { class: "l-axes" })
    this.layerLabels  = svgEl("g", { class: "l-labels" })

    var layers = [
      this.layerGrid, this.layerCaution, this.layerFills, this.layerStruct,
      this.layerCurves, this.layerVspeeds, this.layerTP, this.layer1G,
      this.layerBank, this.layerPoint, this.layerAxes, this.layerLabels,
    ]
    for (var i = 0; i < layers.length; i++) this.svg.appendChild(layers[i])

    // Grab control references from the page HTML
    var wrap = this.container
    this.sliderWeight = wrap.querySelector("#vn-weight")
    this.sliderBank   = wrap.querySelector("#vn-bank")
    this.cbTones      = wrap.querySelector("#vn-show-tones")
    this.cbTP         = wrap.querySelector("#vn-show-tp")
    this.cbPoint      = wrap.querySelector("#vn-show-point")
    this.valWeight    = wrap.querySelector("#vn-weight-val")
    this.valBank      = wrap.querySelector("#vn-bank-val")

    // Info panel
    this.infoPanel = document.createElement("div")
    this.infoPanel.className = "vn-info-panel hidden"
    this.container.appendChild(this.infoPanel)
  }

  VnDiagram.prototype._bindControls = function () {
    var self = this

    function onInput() {
      if (self.sliderWeight) {
        self.state.weight = +self.sliderWeight.value
        self.valWeight.textContent = self.state.weight + " lb"
      }
      if (self.sliderBank) {
        self.state.bank = +self.sliderBank.value
        var n = nFromBank(self.state.bank)
        self.valBank.textContent = self.state.bank + "\u00B0  (" + n.toFixed(2) + " G)"
      }
      if (self.cbTones)  self.state.showTones = self.cbTones.checked
      if (self.cbTP)     self.state.showTrafficPattern = self.cbTP.checked
      if (self.cbPoint) {
        self.state.showPoint = self.cbPoint.checked
        if (self.state.showPoint) {
          self.infoPanel.classList.remove("hidden")
        } else {
          self.infoPanel.classList.add("hidden")
        }
      }
      self.draw()
    }

    var inputs = [this.sliderWeight, this.sliderBank, this.cbTones, this.cbTP, this.cbPoint]
    for (var i = 0; i < inputs.length; i++) {
      if (inputs[i]) inputs[i].addEventListener("input", onInput)
    }

    // Draggable point
    this.svg.addEventListener("mousedown", function (e) { self._dragStart(e) })
    this.svg.addEventListener("mousemove", function (e) { self._dragMove(e) })
    this.svg.addEventListener("mouseup",   function ()  { self._dragEnd() })
    this.svg.addEventListener("mouseleave", function () { self._dragEnd() })
    this.svg.addEventListener("touchstart", function (e) { self._dragStart(e) }, { passive: false })
    this.svg.addEventListener("touchmove",  function (e) { self._dragMove(e) },  { passive: false })
    this.svg.addEventListener("touchend",   function ()  { self._dragEnd() })
  }

  VnDiagram.prototype._svgCoords = function (e) {
    var pt = this.svg.createSVGPoint()
    var touch = e.touches ? e.touches[0] : e
    pt.x = touch.clientX
    pt.y = touch.clientY
    var ctm = this.svg.getScreenCTM()
    if (ctm) pt = pt.matrixTransform(ctm.inverse())
    return pt
  }

  VnDiagram.prototype._dragStart = function (e) {
    if (!this.state.showPoint) return
    var pt = this._svgCoords(e)
    var dx = pt.x - vToX(this.state.pointV)
    var dy = pt.y - nToY(this.state.pointN)
    if (Math.sqrt(dx * dx + dy * dy) < 18) {
      this.dragging = true
      e.preventDefault()
    }
  }

  VnDiagram.prototype._dragMove = function (e) {
    if (!this.dragging) return
    e.preventDefault()
    var pt = this._svgCoords(e)
    this.state.pointV = clamp(xToV(pt.x), V_MIN, V_MAX)
    this.state.pointN = clamp(yToN(pt.y), N_MIN, N_MAX)
    this.draw()
  }

  VnDiagram.prototype._dragEnd = function () {
    this.dragging = false
  }

  VnDiagram.prototype._observeTheme = function () {
    var self = this
    var observer = new MutationObserver(function () { self.draw() })
    observer.observe(document.body, { attributes: true, attributeFilter: ["data-md-color-scheme"] })
  }

  // ── Drawing ────────────────────────────────────────────────────
  VnDiagram.prototype.draw = function () {
    var s = this.state
    var vs1g = vs1gAtWeight(s.weight)

    // Clear all layers
    var layers = [
      this.layerGrid, this.layerCaution, this.layerFills, this.layerStruct,
      this.layerCurves, this.layerVspeeds, this.layerTP, this.layer1G,
      this.layerBank, this.layerPoint, this.layerAxes, this.layerLabels,
    ]
    for (var i = 0; i < layers.length; i++) {
      while (layers[i].firstChild) layers[i].removeChild(layers[i].firstChild)
    }

    this._drawGrid()
    this._drawCautionBand()
    if (s.showTones) this._drawToneFills(vs1g)
    this._drawStructuralLimits()
    this._drawCurves(vs1g)
    this._drawVspeeds(vs1g)
    if (s.showTrafficPattern) this._drawTrafficPattern(vs1g)
    this._draw1G()
    if (s.bank > 0) this._drawBankLine(vs1g)
    if (s.showPoint) this._drawPoint()
    this._drawAxes()
    this._drawTitle()
    this._drawCurveLabels(vs1g)

    if (s.showPoint) this._updateInfoPanel(vs1g)
  }

  // ── Grid ───────────────────────────────────────────────────────
  VnDiagram.prototype._drawGrid = function () {
    var g = this.layerGrid
    for (var v = 0; v <= V_MAX; v += 50) {
      g.appendChild(svgEl("line", {
        x1: vToX(v), y1: PLOT_Y, x2: vToX(v), y2: PLOT_Y + PLOT_H,
        stroke: "var(--vn-grid)", "stroke-width": 0.5,
      }))
    }
    for (var n = N_MIN; n <= N_MAX; n++) {
      g.appendChild(svgEl("line", {
        x1: PLOT_X, y1: nToY(n), x2: PLOT_X + PLOT_W, y2: nToY(n),
        stroke: "var(--vn-grid)", "stroke-width": n === 0 ? 1 : 0.5,
      }))
    }
  }

  // ── Yellow caution band between Vno and Vne (PDF style) ───────
  VnDiagram.prototype._drawCautionBand = function () {
    var g = this.layerCaution
    var D = DEFAULTS
    var x1 = vToX(D.Vno)
    var x2 = vToX(D.Vne)
    if (x1 >= PLOT_X + PLOT_W) return
    g.appendChild(svgEl("rect", {
      x: x1, y: PLOT_Y, width: x2 - x1, height: PLOT_H,
      fill: "var(--vn-caution-fill)", stroke: "none",
    }))
  }

  // ── Tone region fills ─────────────────────────────────────────
  VnDiagram.prototype._naoaPath = function (vs1g, naoa, nLo, nHi, steps) {
    steps = steps || 100
    var pts = []
    for (var i = 0; i <= steps; i++) {
      var n = nLo + (nHi - nLo) * i / steps
      if (n <= 0) continue
      var v = vAtNaoa(vs1g, n, naoa)
      if (v > V_MAX * 1.1) continue
      pts.push([v, n])
    }
    return pts
  }

  VnDiagram.prototype._naoaPathNeg = function (vs1g, naoa, nLo, nHi, steps) {
    steps = steps || 100
    var pts = []
    for (var i = 0; i <= steps; i++) {
      var n = nLo + (nHi - nLo) * i / steps
      if (n >= 0) continue
      var v = vAtNaoa(vs1g, n, naoa)
      if (v > V_MAX * 1.1) continue
      pts.push([v, n])
    }
    return pts
  }

  function ptsToSvgPath(pts) {
    if (pts.length === 0) return ""
    var d = "M " + vToX(pts[0][0]).toFixed(2) + " " + nToY(pts[0][1]).toFixed(2)
    for (var i = 1; i < pts.length; i++) {
      d += " L " + vToX(pts[i][0]).toFixed(2) + " " + nToY(pts[i][1]).toFixed(2)
    }
    return d
  }

  VnDiagram.prototype._drawToneFills = function (vs1g) {
    var g = this.layerFills
    var D = DEFAULTS
    var nHi = D.nPos
    var nLo = 0.01

    var curveStall = this._naoaPath(vs1g, D.naoaStall, nLo, nHi)
    var curveSW    = this._naoaPath(vs1g, D.naoaStallWarn, nLo, nHi)
    var curveOS    = this._naoaPath(vs1g, D.naoaOnspeedSlow, nLo, nHi)
    var curveOF    = this._naoaPath(vs1g, D.naoaOnspeedFast, nLo, nHi)
    var curveLd    = this._naoaPath(vs1g, D.naoaLdmax, nLo, nHi)

    // Stall warning: red/pink (between stall and stall-warn)
    this._fillBetweenCurves(g, curveStall, curveSW, "var(--vn-stall-fill)")
    // Slow tone: yellow (between stall-warn and onspeed-slow)
    this._fillBetweenCurves(g, curveSW, curveOS, "var(--vn-slow-fill)")
    // ONSPEED: green (between onspeed-slow and onspeed-fast)
    this._fillBetweenCurves(g, curveOS, curveOF, "var(--vn-onspeed-fill)")
    // Fast tone: light blue (between onspeed-fast and L/Dmax)
    this._fillBetweenCurves(g, curveOF, curveLd, "var(--vn-fast-fill)")
  }

  VnDiagram.prototype._fillBetweenCurves = function (parent, leftCurve, rightCurve, fill) {
    if (leftCurve.length < 2 || rightCurve.length < 2) return
    var d = ptsToSvgPath(leftCurve)
    var rev = rightCurve.slice().reverse()
    for (var i = 0; i < rev.length; i++) {
      d += " L " + vToX(rev[i][0]).toFixed(2) + " " + nToY(rev[i][1]).toFixed(2)
    }
    d += " Z"
    parent.appendChild(svgEl("path", { d: d, fill: fill, stroke: "none" }))
  }

  // ── Structural limits — red hatching like PDF ─────────────────
  VnDiagram.prototype._drawStructuralLimits = function () {
    var g = this.layerStruct
    var D = DEFAULTS
    var isDark = document.body.getAttribute("data-md-color-scheme") === "slate"
    var hatch = isDark ? "url(#hatch-dark)" : "url(#hatch-light)"

    // Above +G limit
    g.appendChild(svgEl("rect", {
      x: PLOT_X, y: PLOT_Y, width: PLOT_W, height: nToY(D.nPos) - PLOT_Y,
      fill: hatch, opacity: 0.7,
    }))
    // Below -G limit
    var yNeg = nToY(D.nNeg)
    g.appendChild(svgEl("rect", {
      x: PLOT_X, y: yNeg, width: PLOT_W, height: PLOT_Y + PLOT_H - yNeg,
      fill: hatch, opacity: 0.7,
    }))
    // Right of Vne
    var xVne = vToX(D.Vne)
    if (xVne < PLOT_X + PLOT_W) {
      g.appendChild(svgEl("rect", {
        x: xVne, y: PLOT_Y, width: PLOT_X + PLOT_W - xVne, height: PLOT_H,
        fill: hatch, opacity: 0.7,
      }))
    }
  }

  // ── AOA curves — dashed black, matching PDF ───────────────────
  VnDiagram.prototype._drawCurves = function (vs1g) {
    var g = this.layerCurves
    var D = DEFAULTS

    // All constant-AOA curves are dashed black lines in the PDF
    var curves = [
      { naoa: D.naoaStall,       width: 2.5, dash: "6 3" },
      { naoa: D.naoaStallWarn,   width: 1.5, dash: "5 3" },
      { naoa: D.naoaOnspeedSlow, width: 1.5, dash: "5 3" },
      { naoa: D.naoaOnspeedFast, width: 1.5, dash: "5 3" },
      { naoa: D.naoaLdmax,       width: 1.5, dash: "5 3" },
    ]

    for (var c = 0; c < curves.length; c++) {
      var curve = curves[c]
      // Positive G side
      var pts = this._naoaPath(vs1g, curve.naoa, 0.01, N_MAX)
      if (pts.length > 1) {
        g.appendChild(svgEl("path", {
          d: ptsToSvgPath(pts), fill: "none",
          stroke: "var(--vn-aoa-curve)", "stroke-width": curve.width,
          "stroke-dasharray": curve.dash,
        }))
      }
      // Negative G side (stall curve only)
      if (curve.naoa === D.naoaStall) {
        var ptsNeg = this._naoaPathNeg(vs1g, curve.naoa, D.nNeg - 1, -0.01)
        if (ptsNeg.length > 1) {
          g.appendChild(svgEl("path", {
            d: ptsToSvgPath(ptsNeg), fill: "none",
            stroke: "var(--vn-aoa-curve)", "stroke-width": curve.width,
            "stroke-dasharray": curve.dash,
          }))
        }
      }
    }
  }

  // ── V-speed markers ───────────────────────────────────────────
  VnDiagram.prototype._drawVspeeds = function (vs1g) {
    var g = this.layerVspeeds
    var D = DEFAULTS

    // Vref label on x-axis (speed at NAOA onspeed, 1G)
    var vRef = vAtNaoa(vs1g, 1, (D.naoaOnspeedSlow + D.naoaOnspeedFast) / 2)
    if (vRef < V_MAX) {
      this._addLabel(g, vToX(vRef), PLOT_Y + PLOT_H + 20,
        "V\u0280\u1d07\u1da0", "var(--vn-label-vref)", "0.78em", "middle", true)
    }

    // Vno on x-axis
    this._addLabel(g, vToX(D.Vno), PLOT_Y + PLOT_H + 20,
      "V\u2099\u2092", "var(--vn-text)", "0.78em", "middle", true)

    // Vne on x-axis
    this._addLabel(g, vToX(D.Vne), PLOT_Y + PLOT_H + 20,
      "V\u2099\u1d07", "var(--vn-vne)", "0.78em", "middle", true)

    // Vd on x-axis
    this._addLabel(g, vToX(D.Vd), PLOT_Y + PLOT_H + 20,
      "V\u1d05", "var(--vn-vd)", "0.78em", "middle", true)
  }

  // ── Traffic pattern box — red dashed rectangle like PDF ───────
  VnDiagram.prototype._drawTrafficPattern = function (vs1g) {
    var g = this.layerTP
    var D = DEFAULTS
    // PDF shows traffic pattern box roughly from Vs to ~120kt, n≈1 to ~4.5
    var vLo = vAtNaoa(vs1g, 1, D.naoaStall) - 5
    var vHi = vAtNaoa(vs1g, 1, D.naoaLdmax) + 15
    var nLo = 0.8
    var nHi = 4.2

    g.appendChild(svgEl("rect", {
      x: vToX(vLo), y: nToY(nHi),
      width: vToX(vHi) - vToX(vLo),
      height: nToY(nLo) - nToY(nHi),
      fill: "var(--vn-tp-fill)",
      stroke: "var(--vn-tp-stroke)",
      "stroke-width": 2,
      "stroke-dasharray": "6 3",
    }))

    // Label inside box, upper-left area
    var labelX = vToX(vLo) + 8
    var labelY = nToY(nHi) + 18
    var tp = svgEl("text", {
      x: labelX, y: labelY,
      fill: "var(--vn-label-tp)", "font-size": "0.85em",
      "font-weight": "700", "font-style": "italic",
    })
    tp.textContent = "Traffic Pattern"
    g.appendChild(tp)
  }

  // ── 1G line — heavy black like PDF ────────────────────────────
  VnDiagram.prototype._draw1G = function () {
    var g = this.layer1G
    var y = nToY(1)
    g.appendChild(svgEl("line", {
      x1: PLOT_X, y1: y, x2: PLOT_X + PLOT_W, y2: y,
      stroke: "var(--vn-1g)", "stroke-width": 2.5,
    }))
  }

  // ── Bank angle indicator ──────────────────────────────────────
  VnDiagram.prototype._drawBankLine = function (vs1g) {
    var g = this.layerBank
    var nBank = nFromBank(this.state.bank)
    if (nBank > N_MAX) return
    var y = nToY(nBank)

    g.appendChild(svgEl("line", {
      x1: PLOT_X, y1: y, x2: PLOT_X + PLOT_W, y2: y,
      stroke: "var(--vn-bank)", "stroke-width": 1.5, "stroke-dasharray": "6 3",
    }))

    var vsBanked = vStall(vs1g, nBank)
    var label = this.state.bank + "\u00B0 bank = " + nBank.toFixed(2) + "G"
    if (vsBanked < V_MAX) {
      label += "  (Vs = " + Math.round(vsBanked) + " kt)"
    }
    this._addLabel(g, PLOT_X + 4, y - 5, label, "var(--vn-bank)", "0.72em")
  }

  // ── Draggable point ───────────────────────────────────────────
  VnDiagram.prototype._drawPoint = function () {
    var g = this.layerPoint
    var s = this.state
    var px = vToX(s.pointV)
    var py = nToY(s.pointN)

    g.appendChild(svgEl("line", {
      x1: px, y1: PLOT_Y, x2: px, y2: PLOT_Y + PLOT_H,
      stroke: "var(--vn-point)", "stroke-width": 0.5, "stroke-dasharray": "3 3", opacity: 0.5,
    }))
    g.appendChild(svgEl("line", {
      x1: PLOT_X, y1: py, x2: PLOT_X + PLOT_W, y2: py,
      stroke: "var(--vn-point)", "stroke-width": 0.5, "stroke-dasharray": "3 3", opacity: 0.5,
    }))
    g.appendChild(svgEl("circle", {
      cx: px, cy: py, r: 8,
      fill: "var(--vn-point)", stroke: "var(--vn-point-stroke)",
      "stroke-width": 2, cursor: "grab",
    }))
  }

  // ── Axes ──────────────────────────────────────────────────────
  VnDiagram.prototype._drawAxes = function () {
    var g = this.layerAxes

    // X axis
    g.appendChild(svgEl("line", {
      x1: PLOT_X, y1: PLOT_Y + PLOT_H, x2: PLOT_X + PLOT_W, y2: PLOT_Y + PLOT_H,
      stroke: "var(--vn-axis)", "stroke-width": 1.5,
    }))
    // Y axis
    g.appendChild(svgEl("line", {
      x1: PLOT_X, y1: PLOT_Y, x2: PLOT_X, y2: PLOT_Y + PLOT_H,
      stroke: "var(--vn-axis)", "stroke-width": 1.5,
    }))

    // X ticks every 50 kt
    for (var v = 0; v <= V_MAX; v += 50) {
      var x = vToX(v)
      g.appendChild(svgEl("line", {
        x1: x, y1: PLOT_Y + PLOT_H, x2: x, y2: PLOT_Y + PLOT_H + 6,
        stroke: "var(--vn-axis)", "stroke-width": 1,
      }))
      this._addLabel(g, x, PLOT_Y + PLOT_H + 20, String(v),
        "var(--vn-text)", "0.78em", "middle")
    }

    // Y ticks every 1 G
    for (var n = N_MIN; n <= N_MAX; n++) {
      var y = nToY(n)
      g.appendChild(svgEl("line", {
        x1: PLOT_X - 6, y1: y, x2: PLOT_X, y2: y,
        stroke: "var(--vn-axis)", "stroke-width": 1,
      }))
      this._addLabel(g, PLOT_X - 10, y + 4, String(n),
        "var(--vn-text)", "0.78em", "end")
    }

    // X axis title
    this._addLabel(g, PLOT_X + PLOT_W / 2, PLOT_Y + PLOT_H + 46,
      "Velocity", "var(--vn-text)", "0.95em", "middle", true)

    // Y axis title (rotated)
    var yt = svgEl("text", {
      x: PLOT_X - 44, y: PLOT_Y + PLOT_H / 2,
      fill: "var(--vn-text)", "font-size": "0.95em", "font-weight": "700",
      "text-anchor": "middle",
      transform: "rotate(-90 " + (PLOT_X - 44) + " " + (PLOT_Y + PLOT_H / 2) + ")",
    })
    yt.textContent = "Load Factor (n)"
    g.appendChild(yt)
  }

  // ── Title ─────────────────────────────────────────────────────
  VnDiagram.prototype._drawTitle = function () {
    var g = this.layerLabels
    this._addLabel(g, PLOT_X + PLOT_W / 2, PLOT_Y - 10,
      "V-n Diagram", "var(--vn-text)", "1.15em", "middle", true)
  }

  // ── Curve labels — rotated along curves, matching PDF ─────────
  VnDiagram.prototype._drawCurveLabels = function (vs1g) {
    var g = this.layerLabels
    var D = DEFAULTS

    // Rotated labels along the AOA curves (PDF style: text follows curve slope)
    // Place at a specific n-level where they're readable
    var labelCurves = [
      { naoa: D.naoaStallWarn, n: 7, text: "\u03B1Stall Warning",
        color: "var(--vn-label-stall)", size: "0.72em" },
      { naoa: D.naoaOnspeedSlow, n: 2.2, text: "\u03B1ONSPEED",
        color: "var(--vn-label-onspeed)", size: "0.78em" },
      { naoa: D.naoaLdmax, n: 0.5, text: "\u03B1L/Dmax",
        color: "var(--vn-label-ldmax)", size: "0.78em" },
    ]

    for (var i = 0; i < labelCurves.length; i++) {
      var lc = labelCurves[i]
      var v1 = vAtNaoa(vs1g, lc.n, lc.naoa)
      var v2 = vAtNaoa(vs1g, lc.n + 0.5, lc.naoa)
      if (v1 > V_MAX || v2 > V_MAX) continue

      var x1 = vToX(v1), y1 = nToY(lc.n)
      var x2 = vToX(v2), y2 = nToY(lc.n + 0.5)
      var angle = Math.atan2(y2 - y1, x2 - x1) * 180 / Math.PI

      var el = svgEl("text", {
        x: x1, y: y1 - 5,
        fill: lc.color, "font-size": lc.size,
        "font-weight": "700", "font-style": "italic",
        transform: "rotate(" + angle.toFixed(1) + " " + x1.toFixed(1) + " " + (y1 - 5).toFixed(1) + ")",
      })
      el.textContent = lc.text
      g.appendChild(el)
    }

    // "Slow Tone" and "Fast Tone" rotated labels in the fill regions
    this._drawRotatedZoneLabel(g, vs1g,
      D.naoaStallWarn, D.naoaOnspeedSlow, 5,
      "Slow Tone", "var(--vn-label-slow)")
    this._drawRotatedZoneLabel(g, vs1g,
      D.naoaOnspeedFast, D.naoaLdmax, 3.5,
      "Fast Tone", "var(--vn-label-fast)")

    // "Curved Lines are Constant Angle-of-Attack" annotation (lower right)
    this._addLabel(g, vToX(160), nToY(-1.5),
      "Curved Lines are Constant", "var(--vn-text)", "0.78em", "middle", true)
    this._addLabel(g, vToX(160), nToY(-1.5) + 16,
      "Angle-of-Attack", "var(--vn-text)", "0.78em", "middle", true)
  }

  VnDiagram.prototype._drawRotatedZoneLabel = function (g, vs1g, naoaL, naoaR, nLevel, text, color) {
    // Place label in the middle of a tone zone at the given G level
    var naoaMid = (naoaL + naoaR) / 2
    var v1 = vAtNaoa(vs1g, nLevel, naoaMid)
    var v2 = vAtNaoa(vs1g, nLevel + 0.8, naoaMid)
    if (v1 > V_MAX || v2 > V_MAX) return

    var x1 = vToX(v1), y1 = nToY(nLevel)
    var x2 = vToX(v2), y2 = nToY(nLevel + 0.8)
    var angle = Math.atan2(y2 - y1, x2 - x1) * 180 / Math.PI

    var el = svgEl("text", {
      x: x1, y: y1,
      fill: color, "font-size": "0.82em",
      "font-weight": "700", "font-style": "italic",
      transform: "rotate(" + angle.toFixed(1) + " " + x1.toFixed(1) + " " + y1.toFixed(1) + ")",
    })
    el.textContent = text
    g.appendChild(el)
  }

  VnDiagram.prototype._addLabel = function (parent, x, y, text, fill, size, anchor, bold) {
    var attrs = {
      x: x, y: y, fill: fill, "font-size": size || "0.75em",
      "text-anchor": anchor || "start",
    }
    if (bold) attrs["font-weight"] = "700"
    var el = svgEl("text", attrs)
    el.textContent = text
    parent.appendChild(el)
  }

  // ── Info panel ─────────────────────────────────────────────────
  VnDiagram.prototype._updateInfoPanel = function (vs1g) {
    var s = this.state
    var D = DEFAULTS
    var v = s.pointV
    var n = s.pointN

    var vRatio = v / vs1g
    var naoa = (vRatio > 0) ? Math.abs(n) / (vRatio * vRatio) : 0
    var vsAtN = vStall(vs1g, n)
    var stallMargin = v - vsAtN
    var tone = this._toneRegion(naoa)

    var structural = ""
    if (n > D.nPos) structural = "ABOVE +G LIMIT"
    else if (n < D.nNeg) structural = "BELOW -G LIMIT"
    if (v > D.Vne) structural = (structural ? structural + ", " : "") + "ABOVE Vne"

    var html = [
      '<div class="vn-row"><dt>Airspeed:</dt> <dd>' + Math.round(v) + ' kt</dd></div>',
      '<div class="vn-row"><dt>Load factor:</dt> <dd>' + n.toFixed(2) + ' G</dd></div>',
      '<div class="vn-row"><dt>NAOA:</dt> <dd>' + (naoa * 100).toFixed(1) + '%</dd></div>',
      '<div class="vn-row"><dt>Tone:</dt> <dd>' + tone.badge + '</dd></div>',
      '<div class="vn-row"><dt>Stall speed:</dt> <dd>' + Math.round(vsAtN) + ' kt</dd></div>',
      '<div class="vn-row"><dt>Stall margin:</dt> <dd>' + Math.round(stallMargin) + ' kt</dd></div>',
    ]
    if (structural) {
      html.push('<div class="vn-row" style="color:var(--vn-label-stall);font-weight:700;">' + structural + '</div>')
    }
    this.infoPanel.innerHTML = html.join("")
  }

  VnDiagram.prototype._toneRegion = function (naoa) {
    var D = DEFAULTS
    if (naoa >= D.naoaStallWarn) {
      return { name: "Stall Warning",
        badge: '<span class="vn-tone-badge" style="background:#d32f2f;color:#fff;">Stall Warning</span>' }
    }
    if (naoa >= D.naoaOnspeedSlow) {
      return { name: "Slow",
        badge: '<span class="vn-tone-badge" style="background:#e6b800;color:#000;">Slow Tone</span>' }
    }
    if (naoa >= D.naoaOnspeedFast) {
      return { name: "ONSPEED",
        badge: '<span class="vn-tone-badge" style="background:#2e7d32;color:#fff;">ONSPEED</span>' }
    }
    if (naoa >= D.naoaLdmax) {
      return { name: "Fast",
        badge: '<span class="vn-tone-badge" style="background:#4499cc;color:#fff;">Fast Tone</span>' }
    }
    return { name: "Silent",
      badge: '<span class="vn-tone-badge" style="background:#ccc;color:#333;">Silent</span>' }
  }

  // ── Initialization ─────────────────────────────────────────────
  function init() {
    var container = document.getElementById("vn-diagram")
    if (!container) return
    // Guard against double-init on instant navigation
    if (container.querySelector("svg")) return
    injectStyles()
    new VnDiagram(container)
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
