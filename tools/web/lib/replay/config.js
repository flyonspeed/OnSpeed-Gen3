// config.js — thin WASM wrapper for OnSpeed config XML parsing.
//
// Pre-PLAN_WASM_CORE.md Step 1 this file contained a JS port of the C++
// config parser including the V1/V2 detection logic and the <3DAUDIO>
// regex rewrite needed because browsers' DOMParser rejects digit-prefixed
// tags.  It now delegates entirely to the WASM build of onspeed_core,
// so the Replay tool and the firmware parse configs with the exact same
// compiled code.
//
// The <3DAUDIO> → <_3DAUDIO> XML preprocessing trick is handled by
// ConfigV1Parse.cpp's string-search parser, which locates tags by text
// scan and therefore accepts the digit-prefixed tag names that are invalid
// XML.  The JS-side regex rewrite is no longer needed.
//
// Public API (unchanged from the hand-port — callers don't need to change):
//
//   parseConfigXml(xmlText) -> Promise<object>
//
// The returned object has the same field names as host_main's parse_config
// JSON output:
//   { aoaSmoothing, pressureSmoothing, muteUnderIas, dataSource,
//     volumeControl, defaultVolume, audio3D, overGWarning, efisType,
//     serialOutFormat, pitchBias, rollBias, gxBias, gyBias, gzBias,
//     pstaticBias, loadLimitPositive, loadLimitNegative, iAhrsAlgorithm,
//     sdLogging, boomConvertData, logRate, vno, acGrossWeight,
//     acBestGlideIAS, acVfe,
//     flaps: [{ degrees, potPosition, ldmaxAoa, onSpeedFastAoa,
//               onSpeedSlowAoa, stallWarnAoa, stallAoa, manAoa,
//               alpha0, alphaStall, kFit }, ...]
//   }
//
// On parse error the returned object has an `error` string field.

import { getWasmCore } from './wasm_core.js';

// Parse a V1 or V2 OnSpeed config XML string.
//
// xmlText — raw XML text from an .cfg file.  Both V1 (<CONFIG> root) and
//           V2 (<CONFIG2> root) formats are supported.  The C++ parser
//           auto-detects the format via IsV1Format().
//
// Returns a Promise resolving to the parsed config object.  On parse
// error the object contains an `error` field with a description string.
export async function parseConfigXml(xmlText) {
    const w = await getWasmCore();
    return w.parse_config(xmlText);
}
