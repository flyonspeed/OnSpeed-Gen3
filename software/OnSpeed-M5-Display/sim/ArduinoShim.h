// ArduinoShim.h — minimal Arduino-API drop-ins for the desktop (SDL) and
// WASM (Emscripten) builds of the M5 display firmware.
//
// The existing OnSpeed M5 source uses a handful of Arduino/ESP32 types that
// have no counterpart outside ESP-IDF: `Serial`/`Serial2` (HardwareSerial),
// `String`, `Preferences`, `WiFi`, `WebServer`, `Update`. Instead of touching
// every call site, we provide just-enough stubs here and gate the few places
// that genuinely need the device with `#ifdef ESP_PLATFORM`.
//
// Scope deliberately tiny: only what SerialRead.cpp and main.cpp actually
// call. Values logged via Serial.print* are routed to stdout so the sim still
// echoes the same trace the device would, and Preferences is backed by a
// per-process in-memory map so the firmware's boot-time port-detection path
// stays a no-op (SerialRead's setup is skipped on native; see SimMain.cpp).

#pragma once

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#error "ArduinoShim.h is desktop-only and must not be included on ESP targets"
#endif

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

// -----------------------------------------------------------------------
// millis() / micros() / delay() — forward to M5GFX's SDL panel clock so
// the simulator and the device use identical call sites. Declared here as
// free functions in the global namespace (Arduino convention). The
// definitions live in lgfx::v1 (inline namespace); we forward-declare
// them in that namespace so the linker resolves the mangled
// `_ZN4lgfx2v16millisEv`-style symbols already exported by M5GFX's SDL
// panel backend.
// -----------------------------------------------------------------------

namespace lgfx { inline namespace v1
    {
    unsigned long millis(void);
    unsigned long micros(void);
    void          delay(unsigned long ms);
    } }

inline uint32_t      millis(void)           { return (uint32_t)lgfx::millis(); }
inline uint32_t      micros(void)           { return (uint32_t)lgfx::micros(); }
inline void          delay(unsigned long ms){ lgfx::delay(ms); }

// -----------------------------------------------------------------------
// Small Arduino-isms used by the M5 firmware. Kept header-only so every
// TU that includes ArduinoShim.h sees them.
// -----------------------------------------------------------------------

using boolean = bool;

#ifndef PI
#define PI 3.14159265358979323846
#endif
#ifndef HALF_PI
#define HALF_PI 1.57079632679489661923
#endif
#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295769
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG 57.29577951308232087680
#endif
#ifndef TWO_PI
#define TWO_PI 6.28318530717958647692
#endif

// dacWrite: muting the internal DAC on Basic's GPIO 25. Harmless no-op
// on desktop — the sim has no speaker pin at all.
inline void dacWrite(int /*pin*/, int /*value*/) {}

// ---------------------------------------------------------------------------
// String — thin wrapper over std::string with the Arduino-ish conversions
// SerialRead.cpp relies on (char-at, indexOf, concat, etc.).
// ---------------------------------------------------------------------------

class String : public std::string
    {
public:
    using std::string::string;
    String() = default;
    String(const std::string& s) : std::string(s) {}
    String(const char* s) : std::string(s ? s : "") {}
    String(int v) : std::string(std::to_string(v)) {}
    String(unsigned v) : std::string(std::to_string(v)) {}
    String(long v) : std::string(std::to_string(v)) {}
    String(float v)
        {
        char b[32];
        std::snprintf(b, sizeof(b), "%.2f", v);
        assign(b);
        }

    int indexOf(const char* needle) const
        {
        auto p = find(needle);
        return p == npos ? -1 : static_cast<int>(p);
        }
    int indexOf(const std::string& needle) const
        {
        auto p = find(needle);
        return p == npos ? -1 : static_cast<int>(p);
        }

    const char* c_str() const { return std::string::c_str(); }

    // Arduino-style accessor — GaugeWidgets calls it.
    char charAt(size_t i) const { return i < size() ? (*this)[i] : 0; }

    // No custom operator+ overloads — we inherit concatenation from
    // std::string (free functions in <string>). An explicit conversion
    // ctor for std::string keeps "String + std::string -> String"
    // assignments working in the firmware's code. Adding overloads here
    // creates ambiguity with the inherited free functions.
    };

// ---------------------------------------------------------------------------
// Serial — stdout writer with the subset of the HardwareSerial API we use.
// Serial2 (UART) is not present on the desktop; the replay path injects
// bytes directly into the parser instead.
// ---------------------------------------------------------------------------

class SerialStub
    {
public:
    void begin(unsigned long /*baud*/)                                      {}
    void begin(unsigned long, int, int, int, bool)                          {}

    void print(const char* s)         { if (s) std::fputs(s, stdout); }
    void print(const std::string& s)  { std::fputs(s.c_str(), stdout); }
    void print(int v)                 { std::printf("%d", v); }
    void print(unsigned v)            { std::printf("%u", v); }
    void print(long v)                { std::printf("%ld", v); }
    void print(float v)               { std::printf("%.2f", v); }
    void print(double v)              { std::printf("%.2f", v); }

    void println()                    { std::fputc('\n', stdout); }
    template<typename T> void println(const T& v) { print(v); println(); }

    int printf(const char* fmt, ...)
        {
        std::va_list args;
        va_start(args, fmt);
        int n = std::vprintf(fmt, args);
        va_end(args);
        return n;
        }

    // HardwareSerial write()/read()/available(): not wired on the desktop
    // sim. The serial-replay path calls InjectSerialByte() instead.
    int      available()             { return 0; }
    int      read()                  { return -1; }
    void     end()                   {}
    size_t   write(const uint8_t*, size_t n) { return n; }
    };

inline SerialStub Serial;
inline SerialStub Serial2;

// ---------------------------------------------------------------------------
// Preferences — in-memory key/value backed by a static map. The device uses
// this to remember which serial port produced #1 frames at first boot; on
// the desktop the sim skips that detection step, so the backing store is
// essentially a placeholder that keeps the firmware's call sites compiling.
// ---------------------------------------------------------------------------

class Preferences
    {
public:
    bool begin(const char* /*ns*/, bool /*readOnly*/ = false) { return true; }
    void end()                                                {}

    unsigned int getUInt(const char* key, unsigned int defaultVal = 0)
        {
        auto it = map().find(key);
        return it == map().end() ? defaultVal : it->second;
        }
    void putUInt(const char* key, unsigned int v) { map()[key] = v; }

private:
    static std::unordered_map<std::string, unsigned int>& map()
        {
        static std::unordered_map<std::string, unsigned int> m;
        return m;
        }
    };

// ---------------------------------------------------------------------------
// constrain() — matches the Arduino macro's loose semantics: permits lo/hi
// of different arithmetic types from v (e.g. uint16_t clamped with int
// literals). Always returns the promoted common type.
// ---------------------------------------------------------------------------

template<typename T, typename L, typename H>
inline auto constrain(T v, L lo, H hi) -> decltype(v + lo + hi)
    {
    using C = decltype(v + lo + hi);
    C vc = static_cast<C>(v);
    C lc = static_cast<C>(lo);
    C hc = static_cast<C>(hi);
    return vc < lc ? lc : (vc > hc ? hc : vc);
    }
