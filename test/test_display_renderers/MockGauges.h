// MockGauges.h
//
// Records every public Gauges method call the renderer code triggers.
// GaugeWidgets.cpp internally calls into the `extern M5Canvas gdraw`
// global to draw its primitives, but for PR 0 we do not exercise that
// path — we capture the renderer-to-Gauges call pattern instead. PR 1
// will reparameterize the renderers onto a shared DrawApi; the test
// then starts asserting on the full composited call trace.

#ifndef MOCK_GAUGES_H
#define MOCK_GAUGES_H

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "MockDrawApi.h"

// Pointer-type / line-end / marker constants that GaugeWidgets.h defines.
// Values match the original header so fixture code that passes BAR_LONG
// etc. records the same int the production code does.
#ifndef BAR_LONG
#define BAR_LONG   3
#endif
#ifndef BAR_SHORT
#define BAR_SHORT  4
#endif
#ifndef ARROW_OUT
#define ARROW_OUT  1
#endif
#ifndef ARROW_IN
#define ARROW_IN   2
#endif
#ifndef ROUND_DOT
#define ROUND_DOT  7
#endif
#ifndef NEEDLE
#define NEEDLE     8
#endif
#ifndef INDEX
#define INDEX      9
#endif
#ifndef NONE
#define NONE       0
#endif
#ifndef SHARP
#define SHARP      1
#endif
#ifndef ROUND
#define ROUND      2
#endif

class MockGauges
{
public:
    // ---- public API used by the M5 renderers ----
    void clearRanges()    { drawEvents().push("gauges.clearRanges", {}); }
    void clearPointers()  { drawEvents().push("gauges.clearPointers", {}); }

    void setPointer(int num, int value, int type, int color, char tag)
    {
        drawEvents().push("gauges.setPointer", {
            DrawArg::mkInt(num), DrawArg::mkInt(value),
            DrawArg::mkInt(type), DrawArg::mkInt(color),
            DrawArg::mkInt(tag),
        });
    }

    void drawArc(int x0, int y0, int radius,
                 float startAngle, float arcAngle,
                 int lineColor, int lineWidth = 1)
    {
        // Capture floats as int32 bit-casts so the hash is stable.
        int32_t sa_bits = 0, aa_bits = 0;
        std::memcpy(&sa_bits, &startAngle, 4);
        std::memcpy(&aa_bits, &arcAngle,   4);
        drawEvents().push("gauges.drawArc", {
            DrawArg::mkInt(x0), DrawArg::mkInt(y0), DrawArg::mkInt(radius),
            DrawArg::mkInt(sa_bits), DrawArg::mkInt(aa_bits),
            DrawArg::mkInt(lineColor), DrawArg::mkInt(lineWidth),
        });
    }

    // Overload used by displayAOA: `myGauges.drawArc(X0, Y0, ArcRadius, 0.0, PI, Colour, LineWidth)`
    // where 0.0 is a double literal in C++. Delegate to the float variant.
    void drawArc(int x0, int y0, int radius,
                 double startAngle, double arcAngle,
                 int lineColor, int lineWidth = 1)
    {
        drawArc(x0, y0, radius, float(startAngle), float(arcAngle), lineColor, lineWidth);
    }

    void arcGraph(int x0, int y0, int barSize, int barWidth,
                  int maxDisplay_, int minDisplay_,
                  int startAngle, int arcAngle, bool clockWise,
                  int gradMarks_ = 0)
    {
        drawEvents().push("gauges.arcGraph", {
            DrawArg::mkInt(x0), DrawArg::mkInt(y0),
            DrawArg::mkInt(barSize), DrawArg::mkInt(barWidth),
            DrawArg::mkInt(maxDisplay_), DrawArg::mkInt(minDisplay_),
            DrawArg::mkInt(startAngle), DrawArg::mkInt(arcAngle),
            DrawArg::mkInt(clockWise ? 1 : 0),
            DrawArg::mkInt(gradMarks_),
        });
    }

    // printNum matches the signature the renderer invokes. The real impl
    // rotates vector-font strokes; the host mock just records the call
    // so pitchGraph's "-85o .. 85o" label set is observable as N events.
    int printNum(const std::string& value, int x0, int y0,
                 int width, int height, int roll, int color,
                 int datum, int lineWidth = 1, int lineEnd = SHARP)
    {
        drawEvents().push("gauges.printNum", {
            DrawArg::mkTxt(value),
            DrawArg::mkInt(x0), DrawArg::mkInt(y0),
            DrawArg::mkInt(width), DrawArg::mkInt(height),
            DrawArg::mkInt(roll), DrawArg::mkInt(color),
            DrawArg::mkInt(datum), DrawArg::mkInt(lineWidth),
            DrawArg::mkInt(lineEnd),
        });
        return static_cast<int>(value.size()) * width;
    }

    // Float overloads — pitchGraph passes px4/py4 as float variables.
    int printNum(const std::string& value, float x0, float y0,
                 int width, int height, int roll, int color,
                 int datum, int lineWidth = 1, int lineEnd = SHARP)
    {
        return printNum(value, int(x0), int(y0), width, height, roll, color,
                        datum, lineWidth, lineEnd);
    }

    // Overload for when callers pass a String (Arduino-style) or any type
    // with a .c_str() member.
    template <class StringLike,
              class X, class Y,
              class = decltype(std::declval<StringLike>().c_str())>
    int printNum(const StringLike& value, X x0, Y y0,
                 int width, int height, int roll, int color,
                 int datum, int lineWidth = 1, int lineEnd = SHARP)
    {
        return printNum(std::string(value.c_str()), int(x0), int(y0), width,
                        height, roll, color, datum, lineWidth, lineEnd);
    }
};

// The renderer expects the type to be named `Gauges`. Alias it.
using Gauges = MockGauges;

#endif // MOCK_GAUGES_H
