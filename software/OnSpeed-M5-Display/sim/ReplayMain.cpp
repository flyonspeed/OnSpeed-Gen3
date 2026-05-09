// ReplayMain.cpp — entry point for the M5 firmware → WASM "replay" target.
//
// This TU replaces SimMain.cpp's SDL-driven entry point. JS owns the
// virtual clock and feeds wire bytes; the firmware's own setup/loop run
// unchanged into in-memory sprites.
//
// Lifecycle:
//
//   1. JS loads the WASM module. WebAssembly initialization runs every
//      C++ static constructor; nothing inside the firmware's setup()
//      runs yet.
//   2. JS calls `replay_init()` once. We call the firmware's `setup()`,
//      which under `#ifdef REPLAY_TARGET` skips M5.begin / panel detect
//      / FW-update splash button hold / NVS restore (no NVS in WASM).
//      gdraw is constructed against `M5.Display` (default M5GFX, no
//      panel — Panel_NULL), and that's everything we need.
//   3. JS calls `replay_set_time(milliseconds)` before each loop tick
//      to advance the virtual `lgfx::v1::millis/micros`.
//   4. JS calls `replay_inject_byte(byte)` for each byte of a #1 wire
//      frame. The firmware's DisplayFrameAccumulator parses; on the
//      final byte of a valid frame all the SerialRead globals
//      (`PercentLift`, `Slip`, `iVSI`, ...) update.
//   5. JS calls `replay_loop()`. The firmware's `loop()` runs SerialRead
//      (a no-op when no UART bytes are queued — JS-injected bytes have
//      already been processed at step 4), advances any time-gated state
//      (the 50 ms graphics tick, 500 ms numbers snapshot, gHistory
//      sample), and renders to the in-memory sprite. JS never reads the
//      sprite — it reads the firmware's state vars via the accessors
//      below.
//   6. JS calls `replay_get_*()` to pull state. Each accessor is a
//      single read; the WASM↔JS call boundary is ~100 ns (fine at video
//      frame rates).
//
// Why no `int main()`? Emscripten's runtime auto-runs static
// constructors during instantiation — if `main()` exists Emscripten
// invokes it. We don't want that here (no main loop on this target);
// `-sNO_EXIT_RUNTIME=1` keeps the runtime alive between `replay_*` calls
// so module-level state (gdraw, accumulator, displayType) persists.

#include <cstddef>
#include <cstdint>

#include <emscripten.h>

// Pull in the firmware's public globals + entry points. SerialRead.h
// declares every state variable the accessors below need to expose.
// RenderShim.h documents the build strategy (no SDL, M5GFX kept,
// M5.begin skipped) and forward-declares `g_replay_millis_us`.
#include "ArduinoShim.h"
#include "RenderShim.h"
#include "SerialRead.h"

void setup(void);
void loop(void);

// Firmware-side globals exposed via accessors. main.cpp declares them
// as plain non-namespaced `int`/`float`; they pick up external linkage
// because the firmware is one big TU graph rather than a header-only
// surface. Re-declared here as `extern` so the accessor wrappers below
// can read them without #including all of main.cpp.

extern float           PercentLift;
extern float           Pitch;
extern float           Roll;
extern float           IAS;
extern bool            IasIsValid;
extern float           Palt;
extern float           iVSI;
extern float           VerticalG;
extern float           LateralG;
extern float           FlightPath;
extern int             FlapPos;
extern int             OAT;
extern int16_t         Slip;

extern int             TonesOnPctLift;
extern int             OnSpeedFastPctLift;
extern int             OnSpeedSlowPctLift;
extern int             StallWarnPctLift;
extern int             PipPctLift;
extern int             FlapsMinDeg;
extern int             FlapsMaxDeg;

extern float           gOnsetRate;
extern int             SpinRecoveryCue;
extern int             DataMark;
extern float           DecelRate;
extern float           SmoothedDecelRate;

extern float           gHistory[300];
extern int             gHistoryIndex;

extern float           displayIAS;
extern float           displayPalt;
extern float           displayPitch;
extern float           displayVerticalG;
extern int             displayPercentLift;
extern float           displayDecelRate;

extern int16_t         displayType;

// ===========================================================================
// Virtual clock — ReplayMain.cpp owns the storage; ArduinoShim's
// `millis()` and `micros()` forward into `lgfx::v1::millis()/micros()`,
// which we define here. The SDL-platform implementations of those
// symbols are excluded from the replay build (build_wasm.sh skips the
// sdl/ platform sources), so the linker sees only this definition.
// `g_replay_millis_us` is declared `extern` in RenderShim.h.
// ===========================================================================

uint64_t g_replay_millis_us = 0;

namespace lgfx { inline namespace v1 {
    unsigned long millis(void)
    {
        return static_cast<unsigned long>(g_replay_millis_us / 1000ULL);
    }
    unsigned long micros(void)
    {
        return static_cast<unsigned long>(g_replay_millis_us);
    }
    void delay(unsigned long /*ms*/)
    {
        // Replay has no physical wait. Anything that tries to delay() in
        // a loop relative to millis() would deadlock against virtual
        // time — but the firmware only calls delay() in initialization
        // paths gated under !REPLAY_TARGET (e.g. the FW-update splash
        // hold), so this is a no-op rather than an advance.
    }
} } // namespace lgfx::v1

// ===========================================================================
// JS-callable entry points.
//
// All declared `extern "C" EMSCRIPTEN_KEEPALIVE` and listed by name in
// build_wasm.sh's -sEXPORTED_FUNCTIONS. The leading underscore that
// EXPORTED_FUNCTIONS expects is added by Emscripten — list as
// `_replay_init`, etc.
// ===========================================================================

extern "C" {

EMSCRIPTEN_KEEPALIVE void replay_init(void)
{
    setup();
}

EMSCRIPTEN_KEEPALIVE void replay_set_time(uint64_t millis_value)
{
    g_replay_millis_us = millis_value * 1000ULL;
}

EMSCRIPTEN_KEEPALIVE void replay_loop(void)
{
    loop();
}

EMSCRIPTEN_KEEPALIVE void replay_inject_byte(char c)
{
    InjectSerialByte(c);
}

EMSCRIPTEN_KEEPALIVE void replay_set_displayType(int mode)
{
    if (mode < 0)        mode = 0;
    if (mode > 4)        mode = 4;
    displayType = static_cast<int16_t>(mode);
}

// ---- Always-populated state (every mode reads at least these) ------------

EMSCRIPTEN_KEEPALIVE float replay_get_displayIAS(void)         { return displayIAS;        }
EMSCRIPTEN_KEEPALIVE float replay_get_displayPalt(void)        { return displayPalt;       }
EMSCRIPTEN_KEEPALIVE float replay_get_displayPitch(void)       { return displayPitch;      }
EMSCRIPTEN_KEEPALIVE float replay_get_displayVerticalG(void)   { return displayVerticalG;  }
EMSCRIPTEN_KEEPALIVE int   replay_get_displayPercentLift(void) { return displayPercentLift;}
EMSCRIPTEN_KEEPALIVE float replay_get_displayDecelRate(void)   { return displayDecelRate;  }
EMSCRIPTEN_KEEPALIVE int   replay_get_Slip(void)               { return Slip;              }
EMSCRIPTEN_KEEPALIVE float replay_get_PercentLift(void)        { return PercentLift;       }
EMSCRIPTEN_KEEPALIVE float replay_get_gOnsetRate(void)         { return gOnsetRate;        }
EMSCRIPTEN_KEEPALIVE float replay_get_IAS(void)                { return IAS;               }
EMSCRIPTEN_KEEPALIVE float replay_get_Palt(void)               { return Palt;              }
EMSCRIPTEN_KEEPALIVE int   replay_get_IasIsValid(void)         { return IasIsValid ? 1 : 0;}
EMSCRIPTEN_KEEPALIVE int   replay_get_displayType(void)        { return displayType;       }
EMSCRIPTEN_KEEPALIVE float replay_get_iVSI(void)               { return iVSI;              }
EMSCRIPTEN_KEEPALIVE int   replay_get_OAT(void)                { return OAT;               }
EMSCRIPTEN_KEEPALIVE float replay_get_FlightPath(void)         { return FlightPath;        }
EMSCRIPTEN_KEEPALIVE float replay_get_Pitch(void)              { return Pitch;             }
EMSCRIPTEN_KEEPALIVE float replay_get_Roll(void)               { return Roll;              }

// ---- Mode 0 (Energy) anchors ---------------------------------------------

EMSCRIPTEN_KEEPALIVE int replay_get_TonesOnPctLift(void)     { return TonesOnPctLift;     }
EMSCRIPTEN_KEEPALIVE int replay_get_OnSpeedFastPctLift(void) { return OnSpeedFastPctLift; }
EMSCRIPTEN_KEEPALIVE int replay_get_OnSpeedSlowPctLift(void) { return OnSpeedSlowPctLift; }
EMSCRIPTEN_KEEPALIVE int replay_get_StallWarnPctLift(void)   { return StallWarnPctLift;   }
EMSCRIPTEN_KEEPALIVE int replay_get_PipPctLift(void)         { return PipPctLift;         }
EMSCRIPTEN_KEEPALIVE int replay_get_FlapsMinDeg(void)        { return FlapsMinDeg;        }
EMSCRIPTEN_KEEPALIVE int replay_get_FlapsMaxDeg(void)        { return FlapsMaxDeg;        }
EMSCRIPTEN_KEEPALIVE int replay_get_FlapPos(void)            { return FlapPos;            }

// ---- Mode 4 (Historic G) -------------------------------------------------

EMSCRIPTEN_KEEPALIVE int replay_get_gHistoryIndex(void) { return gHistoryIndex; }

// 300-element ring buffer of vertical G samples. JS reads the underlying
// memory directly via `HEAPF32.subarray(ptr/4, ptr/4 + 300)`; the
// pointer is stable for the life of the WASM module since gHistory is a
// static array. Returning a pointer (rather than copying through a
// snapshot export) is cheap and avoids the 300 × 4 byte allocation per
// frame the JS side would otherwise need.
EMSCRIPTEN_KEEPALIVE float* replay_get_gHistory_ptr(void) { return gHistory; }

// ---- Spin / DataMark -----------------------------------------------------

EMSCRIPTEN_KEEPALIVE int replay_get_SpinRecoveryCue(void) { return SpinRecoveryCue; }
EMSCRIPTEN_KEEPALIVE int replay_get_DataMark(void)        { return DataMark;        }

} // extern "C"
