# Credits

This sub-project is the OnSpeed display firmware. It builds for three
targets from a single source tree:

| Target | Build | Hardware |
|---|---|---|
| M5Stack Basic | `pio run -e m5stack-core-esp32` | ESP32 + 320×240 ILI9342C |
| M5Stack Core2 | `pio run -e m5stack-core2` | ESP32 + 320×240 ILI9342C + capacitive touch |
| huVVer-AVI | `pio run -e huvver-avi` | ESP32 + 240×320 ST7789, panel-mount avionics instrument |
| Native sim (SDL2) | `pio run -e native` | macOS / Linux desktop, 320×240 SDL window |

## V.R. ("Voltar") Little — huVVer-AVI hardware, GaugeWidgets, original Gen2 sketch

V.R. Little (huVVer.tech) designed the **huVVer-AVI** instrument hardware
and wrote the original ESP32 firmware that drives it. He also authored
the **GaugeWidgets** drawing library that all three targets use today
(`lib/GaugeWidgets/`), and the original Gen2 OnSpeed display sketch from
which `src/main.cpp` and `src/SerialRead.cpp` are derived.

His upstream repositories:
- `github.com/huVVer/OnSpeed_huVVer_display` — the standalone OnSpeed
  display sketch for huVVer-AVI.
- `huvver.tech` — hardware reference, datasheets, and additional
  firmware variants (transponder controller, instrument cluster).

His license terms apply to derived code:

> "Permission is hereby granted, free of charge, to any person provided
> a copy of this software and associated documentation files (the
> 'Software') to use, copy, modify, or merge copies of the Software for
> **non-commercial purposes** ..."

This is **not** the MIT license that covers the rest of this repo
(`LICENSE` at repo root). It's a custom non-commercial-use license. Each
derived file (in `lib/GaugeWidgets/`, the renderer code in
`src/main.cpp`, and `src/SerialRead.cpp`) carries Vern's original
copyright notice verbatim. The OnSpeed project's own use is non-
commercial — open-source, freely distributed — so the terms are
respected.

## Lenny Iszak — Gen2 OnSpeed adaptation (2021)

Lenny Iszak (FlyOnSpeed.org, `lenny@flyonspeed.org`) adapted V.R.
Little's original M5Stack/huVVer sketches for OnSpeed in 2021. His
work introduced the five-mode display structure (AOA indexer, attitude,
narrow indexer, deceleration gauge, G history), the OnSpeed `#1`
serial protocol parser, and the Savitzky-Golay IAS-derivative
smoothing for the deceleration gauge.

The current `src/main.cpp` is a direct descendant of Lenny's Gen2
sketch, with subsequent polish by the OnSpeed-Gen3 contributors
(see `git log` and audit findings 021–053).

## OnSpeed-Gen3 contributions

Audit fixes, M5GFX/M5Unified port, native SDL2 sim, WebAssembly
browser sim, huVVer-AVI compat layer (`sim/HuvverShim.h`), and
ongoing maintenance: see `git log software/OnSpeed-M5-Display/`.

## Why M5GFX drives huVVer too

The huVVer-AVI runs an ESP32 with an ST7789 SPI panel — exactly the
hardware shape `lgfx::Panel_ST7789` (M5GFX's underlying LovyanGFX
library) was designed for. Rather than introduce TFT_eSPI as a second
graphics library and shim its API to look like M5GFX's, the huVVer
target instantiates a custom `LGFX_Device` subclass for huVVer's pins
and uses M5GFX everywhere. Renderer code, fonts, datums, and
`M5Canvas` usage are unchanged across all three production targets.

V.R. Little's upstream sketch uses TFT_eSPI directly. Our build does
not — the divergence is intentional (M5GFX has cleaner text-baseline
semantics, which the M5 port was tuned against), but it means the
binary we produce for huVVer-AVI is **not a drop-in replacement** for
Vern's upstream firmware. The protocol, hardware pin map, and visual
layout are equivalent; the underlying graphics library is different.

## Compat layer attribution chain

`sim/HuvverShim.h` (added 2026-04-25, PR #292) is OnSpeed-Gen3
original work (MIT). The pin-map values it hardcodes are sourced from
V.R. Little's `my_custom_setup.h` in the huVVer upstream repo —
those values trace back to his hardware design and carry his license.
