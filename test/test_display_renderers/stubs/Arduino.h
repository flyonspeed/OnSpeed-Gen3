// Arduino.h (host stub)
//
// Minimal host-side replacement for <Arduino.h>. Only contains what the
// M5 display renderer functions and GaugeWidgets call out to.

#ifndef RENDERTEST_ARDUINO_H
#define RENDERTEST_ARDUINO_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using boolean = bool;

// Math constants Arduino.h provides.
#ifndef PI
#define PI        3.1415926535897932384626433832795f
#endif
#ifndef HALF_PI
#define HALF_PI   1.5707963267948966192313216916398f
#endif
#ifndef TWO_PI
#define TWO_PI    6.283185307179586476925286766559f
#endif
#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295769236907684886f
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG 57.295779513082320876798154814105f
#endif

// constrain(x, lo, hi) — the Arduino classic.
#ifndef constrain
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
#endif

// round() is in <cmath> but Arduino exposes it as a macro sometimes; we
// just rely on std::round. abs() is handled by std::abs for int/float.
// Map the Arduino `abs()` macro to std::abs() if tests compile on a host
// where <cmath>'s abs template exists.
using std::abs;

// A tiny String class to satisfy usage like `String(FlapPos)` or
// `"prefix " + String(value) + " suffix"`. Only string-concat operators
// and integer conversions are needed.
class String
{
public:
    String() = default;
    String(const char* s)     : s_(s ? s : "") {}
    String(const std::string& s) : s_(s) {}
    String(int v)             { char b[24]; std::snprintf(b, sizeof b, "%d", v); s_ = b; }
    String(unsigned int v)    { char b[24]; std::snprintf(b, sizeof b, "%u", v); s_ = b; }
    String(long v)            { char b[24]; std::snprintf(b, sizeof b, "%ld", v); s_ = b; }
    String(unsigned long v)   { char b[24]; std::snprintf(b, sizeof b, "%lu", v); s_ = b; }
    String(float v)           { char b[32]; std::snprintf(b, sizeof b, "%g", (double)v); s_ = b; }
    String(double v)          { char b[32]; std::snprintf(b, sizeof b, "%g", v); s_ = b; }

    const char* c_str() const { return s_.c_str(); }
    std::size_t length() const { return s_.size(); }
    operator const std::string&() const { return s_; }

    String  operator+(const String& o) const { String r; r.s_ = s_ + o.s_; return r; }
    String  operator+(const char* o)   const { String r; r.s_ = s_ + (o ? o : ""); return r; }
    String& operator+=(const String& o)      { s_ += o.s_; return *this; }

    friend String operator+(const char* a, const String& b) { String r; r.s_ = std::string(a ? a : "") + b.s_; return r; }

    const std::string& str() const { return s_; }
private:
    std::string s_;
};

// millis() — returns a fixed value in host tests. The renderer functions
// themselves don't call millis(); it's only used in loop() which we
// don't exercise. Kept here as a safety net.
inline uint32_t millis() { return 0; }

#endif // RENDERTEST_ARDUINO_H
