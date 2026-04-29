// Mode 2 — "narrow AOA and slip indicator" (case 2 in main.cpp:626).
//
// In the M5 source, Mode 2 calls the same displayAOA() body as Mode 0
// but with `numericDisplay = false`. We mirror that exactly: Mode 2 is
// `mountAoa(rootEl, { numericDisplay: false })`.
//
// Shared composition lives in modes/aoa.js so the two modes can never
// drift apart — same widgets, same geometry, single rendering path.
import { mountAoa } from './aoa.js';

export function mountIndexerOnly(rootEl) {
  return mountAoa(rootEl, { numericDisplay: false });
}
