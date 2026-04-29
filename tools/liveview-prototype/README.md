# LiveView Prototype

A standalone browser harness for iterating the OnSpeed five-mode SVG indexer
without flashing firmware. Renders the same SVG that will eventually live in
`software/OnSpeed-Gen3-ESP32/Web/html_liveview.h`, driven by a synthetic
data generator and validated side-by-side against the M5's wasm-live sim.

## Quick start

```bash
# 1. Build the wasm-live sim (once; outputs to sim/build/wasm-live/)
cd software/OnSpeed-M5-Display && ./sim/build_wasm.sh --target live

# 2. Symlink the sim build into the prototype dir
cd tools/liveview-prototype
ln -sfn ../../software/OnSpeed-M5-Display/sim/build/wasm-live wasm-live

# 3. Serve
python3 -m http.server 8080

# 4. Open http://localhost:8080/ in a browser
```

The prototype shows two panels side-by-side: the new SVG indexer (left)
and the wasm-live sim (right). Both consume the same synthetic data feed
(JS objects to the SVG, `#1` ASCII binary frames to the WASM via
`_inject_serial_byte`).

Use the scenario buttons at the top to drive the data through canned
manoeuvres (ground idle, cruise, approach, stall warning, recovery).

## Tests

Open `http://localhost:8080/test/runner.html` to run the unit tests in
the browser. The pure JS modules in `lib/` (color decisions, percent-to-y
mapping, slip ball math, etc.) are all covered.
