// MockDrawApi.h
//
// Header-only mock that masquerades as an M5Canvas surface so the existing
// M5 display renderer code can be driven on native x86 inside a Unity test
// fixture. Every draw call is recorded as a DrawEvent; queries such as
// drawCallCount(), colorHistogram(), coordHash() and events() let the
// fixtures assert on structural properties instead of byte-exact pixels.
//
// This file is PR-0 baseline scaffolding. It will be retired in PR 1 when
// the DrawApi interface lands and the renderers take `DrawApi&` directly;
// at that point MockDrawApi becomes a real subclass of DrawApi.
//
// See docs/superpowers/specs/2026-04-24-huvver-display-integration-design.md
// sections "Testing strategy" and "PR sequence — PR 0".

#ifndef MOCK_DRAW_API_H
#define MOCK_DRAW_API_H

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Forward declaration — the mock's print/drawString overloads need to
// accept the Arduino-style String that our stubs/Arduino.h defines. To
// avoid a circular include we declare the class here; the definition is
// pulled in by any TU that includes stubs/Arduino.h (M5Unified.h does).
class String;

// Test-visible TextDatum values; mirror M5GFX's bit-packed integer
// encoding so the (mock, value) coord hash survives a later switch to
// onspeed::display::TextDatum (PR 1). Values match the table in the
// huVVer spec "Textdatum enum mapping" section.
enum MockTextDatum : int
{
    MOCK_top_left       =  0,
    MOCK_top_center     =  1,
    MOCK_top_right      =  2,
    MOCK_middle_left    =  4,
    MOCK_middle_center  =  5,
    MOCK_middle_right   =  6,
    MOCK_bottom_left    =  8,
    MOCK_bottom_center  =  9,
    MOCK_bottom_right   = 10,
    MOCK_baseline_left  = 16,
    MOCK_baseline_center = 17,
    MOCK_baseline_right = 18,
};

// Simple variant for argument recording: each arg is either an int or a
// string (for method name + text content). Floats are bit-cast to int32_t
// at record time, then stored as int64_t so the hash is deterministic.
struct DrawArg
{
    enum Kind : uint8_t { Int, Text } kind;
    int64_t       i = 0;
    std::string   s;
    static DrawArg mkInt(int64_t v) { DrawArg a; a.kind = Int;  a.i = v; return a; }
    static DrawArg mkTxt(std::string v) { DrawArg a; a.kind = Text; a.s = std::move(v); return a; }
};

struct DrawEvent
{
    std::string          method;
    std::vector<DrawArg> args;
};

// 32-bit FNV-1a hash. Simple, fast, no third-party dep. The spec
// mentions MurmurHash3 as a suggestion; FNV-1a is strictly adequate for
// structural trace use — collision resistance vs. accidental drift, not
// cryptographic.
struct Fnv1a32
{
    uint32_t h = 0x811c9dc5u;
    void mix(uint8_t b)        { h ^= b; h *= 0x01000193u; }
    void mix_i64(int64_t v)    { for (int i = 0; i < 8; ++i) mix(uint8_t((uint64_t(v) >> (i * 8)) & 0xff)); }
    void mix_str(const std::string& s) { for (char c : s) mix(uint8_t(c)); mix(0); }
};

// Shared recorder — both MockM5Canvas and MockGauges push into this so
// the test fixture sees the renderer's draw-order as one unified trace.
// Fixture resets it via reset() before each run.
class DrawEventRecorder
{
public:
    void push(const std::string& method,
              std::initializer_list<DrawArg> args,
              uint32_t color = 0, bool colorIsMeaningful = false)
    {
        DrawEvent e;
        e.method = method;
        e.args.reserve(args.size());
        for (auto& a : args) e.args.emplace_back(a);
        events_.push_back(std::move(e));
        if (colorIsMeaningful) colorHisto_[color] += 1;
    }

    void reset() { events_.clear(); colorHisto_.clear(); }

    const std::vector<DrawEvent>&          events()         const { return events_; }
    const std::map<uint32_t, std::size_t>& colorHistogram() const { return colorHisto_; }

    std::size_t drawCallCount(const std::string& method) const
    {
        std::size_t n = 0;
        for (const auto& e : events_) if (e.method == method) ++n;
        return n;
    }

    // Match a call where every integer argument (in order, skipping text
    // args) equals the values in `args`. The skip-text behavior lets
    // callers write containsCall("drawString", {18, 133}) to mean "the
    // drawString had int args (18, 133) somewhere in its arg tuple,"
    // regardless of whether the text arg sits before or after them.
    bool containsCall(const std::string& method,
                      std::initializer_list<int64_t> args) const
    {
        for (const auto& e : events_) {
            if (e.method != method) continue;
            std::vector<int64_t> ints;
            for (const auto& a : e.args) if (a.kind == DrawArg::Int) ints.push_back(a.i);
            if (ints.size() < args.size()) continue;
            bool ok = true; std::size_t idx = 0;
            for (auto v : args) {
                if (ints[idx] != v) { ok = false; break; }
                ++idx;
            }
            if (ok) return true;
        }
        return false;
    }

    // Exact-match variant for when the text content also matters.
    bool containsDrawString(const std::string& text, int x, int y) const
    {
        for (const auto& e : events_) {
            if (e.method != "drawString") continue;
            if (e.args.size() < 3) continue;
            if (e.args[0].kind == DrawArg::Text && e.args[0].s == text
                && e.args[1].kind == DrawArg::Int && e.args[1].i == x
                && e.args[2].kind == DrawArg::Int && e.args[2].i == y)
                return true;
        }
        return false;
    }

    std::string coordHash() const
    {
        Fnv1a32 h;
        for (const auto& e : events_) {
            h.mix_str(e.method);
            for (const auto& a : e.args) {
                if (a.kind == DrawArg::Int)  { h.mix(0); h.mix_i64(a.i); }
                else                         { h.mix(1); h.mix_str(a.s); }
            }
            h.mix(0xff);
        }
        char buf[16];
        std::snprintf(buf, sizeof buf, "0x%08x", h.h);
        return buf;
    }

private:
    std::vector<DrawEvent>          events_;
    std::map<uint32_t, std::size_t> colorHisto_;
};

// Global singleton — there is only ever one mock surface active per test
// process. Keeping this global (vs plumbing a pointer through every
// MockM5Canvas/MockGauges ctor) matches the production code's reliance
// on `extern M5Canvas gdraw;` so swapping in a real DrawApi later does
// not perturb the call sites.
inline DrawEventRecorder& drawEvents()
{
    static DrawEventRecorder r;
    return r;
}

class MockM5Canvas
{
public:
    MockM5Canvas() = default;
    // Allow the `M5Canvas gdraw(&M5.Display);` construction pattern — the
    // mock ignores the display-pointer argument. Templated so any opaque
    // pointer satisfies it.
    template <class T> explicit MockM5Canvas(T*) {}

    // ---------- sprite lifecycle ----------
    void setColorDepth(int bits)           { drawEvents().push("setColorDepth", { DrawArg::mkInt(bits) }); }
    void createSprite(int w, int h)        { drawEvents().push("createSprite",  { DrawArg::mkInt(w), DrawArg::mkInt(h) }); }
    void deleteSprite()                    { drawEvents().push("deleteSprite",  {}); }
    void fillSprite(uint32_t color)        { drawEvents().push("fillSprite",    { DrawArg::mkInt(color) }, color, true); }
    void pushSprite(int x, int y)          { drawEvents().push("pushSprite",    { DrawArg::mkInt(x), DrawArg::mkInt(y) }); }

    // ---------- rectangles ----------
    void fillRect(int x, int y, int w, int h, uint32_t color)
    {
        drawEvents().push("fillRect", { DrawArg::mkInt(x), DrawArg::mkInt(y), DrawArg::mkInt(w), DrawArg::mkInt(h), DrawArg::mkInt(color) }, color, true);
    }
    void drawRect(int x, int y, int w, int h, uint32_t color)
    {
        drawEvents().push("drawRect", { DrawArg::mkInt(x), DrawArg::mkInt(y), DrawArg::mkInt(w), DrawArg::mkInt(h), DrawArg::mkInt(color) }, color, true);
    }
    void fillRoundRect(int x, int y, int w, int h, int r, uint32_t color)
    {
        drawEvents().push("fillRoundRect", { DrawArg::mkInt(x), DrawArg::mkInt(y), DrawArg::mkInt(w), DrawArg::mkInt(h), DrawArg::mkInt(r), DrawArg::mkInt(color) }, color, true);
    }
    void drawRoundRect(int x, int y, int w, int h, int r, uint32_t color)
    {
        drawEvents().push("drawRoundRect", { DrawArg::mkInt(x), DrawArg::mkInt(y), DrawArg::mkInt(w), DrawArg::mkInt(h), DrawArg::mkInt(r), DrawArg::mkInt(color) }, color, true);
    }

    // ---------- lines / shapes (float variants via cast to int) ----------
    void drawLine(int x0, int y0, int x1, int y1, uint32_t color)
    {
        drawEvents().push("drawLine", { DrawArg::mkInt(x0), DrawArg::mkInt(y0), DrawArg::mkInt(x1), DrawArg::mkInt(y1), DrawArg::mkInt(color) }, color, true);
    }
    void drawLine(float x0, float y0, float x1, float y1, uint32_t color)
    {
        // Match M5GFX's int-cast behavior on drawLine(float,...) — the
        // renderer in AiGraph passes float coords computed from sin/cos.
        drawLine(int(x0), int(y0), int(x1), int(y1), color);
    }

    void drawFastHLine(int x, int y, int w, uint32_t color)
    {
        drawEvents().push("drawFastHLine", { DrawArg::mkInt(x), DrawArg::mkInt(y), DrawArg::mkInt(w), DrawArg::mkInt(color) }, color, true);
    }
    void drawFastVLine(int x, int y, int h, uint32_t color)
    {
        drawEvents().push("drawFastVLine", { DrawArg::mkInt(x), DrawArg::mkInt(y), DrawArg::mkInt(h), DrawArg::mkInt(color) }, color, true);
    }

    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color)
    {
        drawEvents().push("fillTriangle", { DrawArg::mkInt(x0), DrawArg::mkInt(y0), DrawArg::mkInt(x1), DrawArg::mkInt(y1), DrawArg::mkInt(x2), DrawArg::mkInt(y2), DrawArg::mkInt(color) }, color, true);
    }
    void fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color)
    {
        fillTriangle(int(x0), int(y0), int(x1), int(y1), int(x2), int(y2), color);
    }

    void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color)
    {
        drawEvents().push("drawTriangle", { DrawArg::mkInt(x0), DrawArg::mkInt(y0), DrawArg::mkInt(x1), DrawArg::mkInt(y1), DrawArg::mkInt(x2), DrawArg::mkInt(y2), DrawArg::mkInt(color) }, color, true);
    }

    void fillCircle(int x, int y, int r, uint32_t color)
    {
        drawEvents().push("fillCircle", { DrawArg::mkInt(x), DrawArg::mkInt(y), DrawArg::mkInt(r), DrawArg::mkInt(color) }, color, true);
    }
    void fillCircle(float x, float y, int r, uint32_t color)
    {
        fillCircle(int(x), int(y), r, color);
    }
    void drawCircle(int x, int y, int r, uint32_t color)
    {
        drawEvents().push("drawCircle", { DrawArg::mkInt(x), DrawArg::mkInt(y), DrawArg::mkInt(r), DrawArg::mkInt(color) }, color, true);
    }

    void drawPixel(int x, int y, uint32_t color)
    {
        drawEvents().push("drawPixel", { DrawArg::mkInt(x), DrawArg::mkInt(y), DrawArg::mkInt(color) }, color, true);
    }

    // ---------- text ----------
    // `const T*` template keeps the renderer-side signature flexible
    // (Free_Fonts.h hands us a FontTag*; M5GFX-real would hand us a
    // GFXfont*). We read the id-like field from a FontTag when one is
    // passed, otherwise hash the address low bits. Address-based is not
    // ASLR-stable, so renderer code should only ever pass FontTag*.
    template <class T>
    void setFont(const T* font)
    {
        int id = font ? static_cast<int>(font->id) : 0;
        drawEvents().push("setFont", { DrawArg::mkInt(id) });
    }
    void setTextColor(uint32_t fg)
    {
        drawEvents().push("setTextColor", { DrawArg::mkInt(fg) });
    }
    void setTextColor(uint32_t fg, uint32_t bg)
    {
        drawEvents().push("setTextColor", { DrawArg::mkInt(fg), DrawArg::mkInt(bg) });
    }
    void setTextDatum(int datum)   { drawEvents().push("setTextDatum", { DrawArg::mkInt(datum) }); }
    void setCursor(int x, int y)   { drawEvents().push("setCursor", { DrawArg::mkInt(x), DrawArg::mkInt(y) }); }
    void setCursor(float x, float y) { setCursor(int(x), int(y)); }

    // print(...) overloads — arithmetic types are converted to a canonical
    // string via snprintf so the hash is stable.
    void print(const char* s)        { drawEvents().push("print", { DrawArg::mkTxt(s) }); }
    void print(const std::string& s) { drawEvents().push("print", { DrawArg::mkTxt(s) }); }
    template <class S, class = decltype(std::declval<S>().c_str())>
    void print(const S& s)           { drawEvents().push("print", { DrawArg::mkTxt(s.c_str()) }); }
    void print(int v)                { char b[32]; std::snprintf(b, sizeof b, "%d", v);   drawEvents().push("print", { DrawArg::mkTxt(b) }); }
    void print(unsigned int v)       { char b[32]; std::snprintf(b, sizeof b, "%u", v);   drawEvents().push("print", { DrawArg::mkTxt(b) }); }
    void print(long v)               { char b[32]; std::snprintf(b, sizeof b, "%ld", v);  drawEvents().push("print", { DrawArg::mkTxt(b) }); }
    void print(float v)              { char b[32]; std::snprintf(b, sizeof b, "%.6f", (double)v); drawEvents().push("print", { DrawArg::mkTxt(b) }); }
    void print(double v)             { char b[32]; std::snprintf(b, sizeof b, "%.6f", v); drawEvents().push("print", { DrawArg::mkTxt(b) }); }

    int printf(const char* fmt, ...)
    {
        char buf[128];
        va_list ap;
        va_start(ap, fmt);
        int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        drawEvents().push("printf", { DrawArg::mkTxt(buf) });
        return n;
    }

    void drawString(const char* s, int x, int y)
    {
        drawEvents().push("drawString", { DrawArg::mkTxt(s), DrawArg::mkInt(x), DrawArg::mkInt(y) });
    }
    void drawString(const std::string& s, int x, int y)
    {
        drawEvents().push("drawString", { DrawArg::mkTxt(s), DrawArg::mkInt(x), DrawArg::mkInt(y) });
    }
    template <class S, class = decltype(std::declval<S>().c_str())>
    void drawString(const S& s, int x, int y)
    {
        drawEvents().push("drawString", { DrawArg::mkTxt(s.c_str()), DrawArg::mkInt(x), DrawArg::mkInt(y) });
    }

    // textWidth — the renderers use this in expressions like
    //    x = RIGHT_X - (int)gdraw.textWidth("IAS")
    // Return a deterministic pseudo-width (8 px per char + 4 baseline).
    int textWidth(const char* s) const
    {
        if (!s) return 0;
        return static_cast<int>(std::strlen(s)) * 8 + 4;
    }
    int textWidth(const std::string& s) const { return textWidth(s.c_str()); }
    template <class S, class = decltype(std::declval<S>().c_str())>
    int textWidth(const S& s) const { return textWidth(s.c_str()); }

    int fontHeight() const { return 16; }
    void setBrightness(int level) { drawEvents().push("setBrightness", { DrawArg::mkInt(level) }); }
};

#endif // MOCK_DRAW_API_H
