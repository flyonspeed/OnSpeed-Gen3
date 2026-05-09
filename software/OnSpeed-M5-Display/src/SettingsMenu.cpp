// software/OnSpeed-M5-Display/src/SettingsMenu.cpp
//
// Platform layer: drives the pure MenuModel from lib/MenuModel/ against
// M5GFX rendering, the M5Unified Preferences NVS shim, and per-target
// button polling. See SettingsMenu.h for lifecycle.

#include "SettingsMenu.h"
#include "RenderConfig.h"

// Mirror main.cpp's include pattern: GaugeWidgets pulls in <M5GFX.h>
// (where M5Canvas / fonts come from) regardless of target.
#include <GaugeWidgets.h>
#if defined(HUVVER)
#include "../sim/HuvverShim.h"
#else
#include <M5Unified.h>
#endif

#include <Free_Fonts.h>

// On the desktop SDL build, the ESP32 Preferences class doesn't exist.
// The M5 sub-project's existing pattern is to include <Preferences.h>
// only behind ESP_PLATFORM and stub it out elsewhere via the sim shim.
#if defined(ESP_PLATFORM)
#include <Preferences.h>
extern Preferences preferences;   // defined in main.cpp
#endif

#include "MenuModel.h"

using m5menu::MenuModel;
using m5menu::MenuItem;
using m5menu::ItemType;

// Off-screen sprite shared with main.cpp. Defined in main.cpp at top scope.
extern M5Canvas gdraw;

// Public global — read by main.cpp's IAS render block. Default false
// (KTS) — pilots flip via the settings menu and the choice persists in
// NVS across reboots.
bool g_speedInMph = false;

namespace {

// ---------------------------------------------------------------------------
// Menu item table.
//
// Caller-owned static storage: MenuModel just holds a pointer. The Exit
// action has a null callback — MenuModel treats that as the built-in Exit.
// To add a new setting, add an entry here and (if it's a Toggle) wire the
// flip in toggleCallback below.
// ---------------------------------------------------------------------------
MenuItem g_items[] = {
    { "Speed Units", ItemType::Toggle, &g_speedInMph, "KTS", "MPH",
      nullptr, nullptr },
    { "Exit",        ItemType::Action, nullptr, nullptr, nullptr,
      nullptr, nullptr },
};
constexpr int kItemCount = sizeof(g_items) / sizeof(g_items[0]);

MenuModel g_model{g_items, kItemCount};
bool      g_active = false;

// Track last tick time for MenuModel::tick(elapsedMs).
uint32_t  g_lastTickMs = 0;

// ---------------------------------------------------------------------------
// NVS helpers
// ---------------------------------------------------------------------------
void persistSpeedUnits() {
#if defined(ESP_PLATFORM)
    preferences.begin("OnSpeed", false);
    preferences.putBool("SpeedMph", g_speedInMph);
    preferences.end();
#endif
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
constexpr int kTitleY      = 20;
constexpr int kDividerTopY = 38;
constexpr int kFirstRowY   = 70;
constexpr int kRowSpacing  = 30;
constexpr int kDividerBotY = 200;
constexpr int kFooterY     = 220;

constexpr uint16_t kColorTitle      = TFT_WHITE;
constexpr uint16_t kColorBody       = TFT_WHITE;
constexpr uint16_t kColorHighlight  = 0x07FF;   // cyan band
constexpr uint16_t kColorFooterText = 0x7BEF;   // grey
constexpr uint16_t kColorDivider    = 0x4208;   // darker grey

const char* toggleValueLabel(const MenuItem& item) {
    if (item.type != ItemType::Toggle || !item.toggleValue) return "";
    return *item.toggleValue ? item.toggleLabelOn : item.toggleLabelOff;
}

void renderMenu() {
    gdraw.setColorDepth(XPLANE_PLUGIN_DEPTH);
    gdraw.createSprite(320, 240);
    gdraw.fillSprite(TFT_BLACK);
    gdraw.setTextDatum(textdatum_t::baseline_left);

    // Title
    gdraw.setFont(FSSB12);
    gdraw.setTextColor(kColorTitle);
    {
        const char* title = "OnSpeed Settings";
        int w = gdraw.textWidth(title);
        gdraw.setCursor((320 - w) / 2, kTitleY);
        gdraw.print(title);
    }
    gdraw.drawFastHLine(40, kDividerTopY, 240, kColorDivider);

    // Items
    gdraw.setFont(FSS12);
    for (int i = 0; i < g_model.itemCount(); ++i) {
        int y = kFirstRowY + i * kRowSpacing;
        const MenuItem& item = g_model.itemAt(i);

        if (i == g_model.currentIndex()) {
            // Highlight band reaches near the right panel edge so the
            // right-anchored value column ("[ MPH ]") sits inside the
            // band rather than dangling past it.
            gdraw.fillRect(15, y - 18, 305, 24, kColorHighlight);
            gdraw.setTextColor(TFT_BLACK);
        } else {
            gdraw.setTextColor(kColorBody);
        }

        gdraw.setCursor(28, y);
        gdraw.print(item.label);

        const char* rhs = nullptr;
        char rhs_buf[32];
        if (item.type == ItemType::Toggle) {
            snprintf(rhs_buf, sizeof(rhs_buf), "[ %s ]",
                     toggleValueLabel(item));
            rhs = rhs_buf;
        } else if (item.type == ItemType::Info && item.getInfoValue) {
            rhs = item.getInfoValue();
        }
        if (rhs) {
            int rhsW = gdraw.textWidth(rhs);
            // Right-anchor at x=308 so the value sits a few px inside
            // the highlight band's right edge (kBandRight=320).
            gdraw.setCursor(308 - rhsW, y);
            gdraw.print(rhs);
        }
    }

    // Footer
    gdraw.drawFastHLine(40, kDividerBotY, 240, kColorDivider);
    gdraw.setTextColor(kColorFooterText);
    gdraw.setFont(FSS12);

    // Glyph-from-primitives helpers (FreeFonts may not carry ▲ ● ▼ ☐ ◀ ▶)
    auto drawTriangleUp   = [](int cx, int cy, int s) {
        gdraw.fillTriangle(cx - s, cy + s, cx + s, cy + s, cx, cy - s,
                           kColorFooterText);
    };
    auto drawTriangleDown = [](int cx, int cy, int s) {
        gdraw.fillTriangle(cx - s, cy - s, cx + s, cy - s, cx, cy + s,
                           kColorFooterText);
    };
    auto drawTriangleLeft = [](int cx, int cy, int s) {
        gdraw.fillTriangle(cx + s, cy - s, cx + s, cy + s, cx - s, cy,
                           kColorFooterText);
    };
    auto drawTriangleRight = [](int cx, int cy, int s) {
        gdraw.fillTriangle(cx - s, cy - s, cx - s, cy + s, cx + s, cy,
                           kColorFooterText);
    };
    auto drawCircle = [](int cx, int cy, int r) {
        gdraw.fillCircle(cx, cy, r, kColorFooterText);
    };
    auto drawSquare = [](int cx, int cy, int s) {
        gdraw.drawRect(cx - s, cy - s, 2 * s + 1, 2 * s + 1, kColorFooterText);
    };

    // Footer chunks. Direction is conveyed by triangle orientation, so
    // ▲ ▼ ◀ ▶ are bare glyphs without "UP"/"DOWN" labels (those are
    // noise once you can see the arrow). Only the abstract glyphs (●
    // and ☐) carry text labels.
    //
    // Layout: each chunk's width is glyph + (iconTextGap + textWidth)
    // when it has a label, or just the glyph otherwise. Chunks are
    // distributed evenly across the 320-px panel via textWidth() so
    // the FSS12 metrics drive the layout instead of magic numbers.
    constexpr int kIconY        = kFooterY - 6;
    constexpr int kIconRadius   = 6;
    constexpr int kIconTextGap  = 6;
    constexpr int kPanelW       = 320;

    enum class Glyph { Square, TriUp, TriDown, TriLeft, TriRight, Circle };
    struct Chunk { Glyph glyph; const char* label; };  // label==nullptr: glyph only

#if defined(HUVVER)
    // ☐ BACK    ◀    ● SEL    ▶
    static const Chunk kChunks[] = {
        { Glyph::Square,    "BACK" },
        { Glyph::TriLeft,   nullptr },
        { Glyph::Circle,    "SEL"  },
        { Glyph::TriRight,  nullptr },
    };
#else
    // ▲    ● SEL    ▼
    static const Chunk kChunks[] = {
        { Glyph::TriUp,    nullptr },
        { Glyph::Circle,   "SEL"  },
        { Glyph::TriDown,  nullptr },
    };
#endif
    constexpr int kChunkCount = sizeof(kChunks) / sizeof(kChunks[0]);

    // Sum chunk widths so we know how much horizontal room is left for
    // the gaps between chunks.
    int chunkW[kChunkCount];
    int chunkSum = 0;
    for (int i = 0; i < kChunkCount; ++i) {
        chunkW[i] = kIconRadius * 2;
        if (kChunks[i].label) {
            chunkW[i] += kIconTextGap + gdraw.textWidth(kChunks[i].label);
        }
        chunkSum += chunkW[i];
    }

    // Distribute remaining horizontal space evenly: half a gap on each
    // outer edge, full gap between chunks. (kChunkCount + 1 gap slots.)
    int gap = (kPanelW - chunkSum) / (kChunkCount + 1);
    if (gap < 4) gap = 4;
    int x = gap;
    for (int i = 0; i < kChunkCount; ++i) {
        const int iconCx = x + kIconRadius;
        switch (kChunks[i].glyph) {
            case Glyph::Square:   drawSquare      (iconCx, kIconY, kIconRadius); break;
            case Glyph::TriUp:    drawTriangleUp  (iconCx, kIconY, kIconRadius); break;
            case Glyph::TriDown:  drawTriangleDown(iconCx, kIconY, kIconRadius); break;
            case Glyph::TriLeft:  drawTriangleLeft(iconCx, kIconY, kIconRadius); break;
            case Glyph::TriRight: drawTriangleRight(iconCx, kIconY, kIconRadius); break;
            case Glyph::Circle:   drawCircle      (iconCx, kIconY, kIconRadius); break;
        }
        if (kChunks[i].label) {
            gdraw.setCursor(x + (kIconRadius * 2) + kIconTextGap, kFooterY);
            gdraw.print(kChunks[i].label);
        }
        x += chunkW[i] + gap;
    }

    gdraw.pushSprite(0, 0);
    gdraw.deleteSprite();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
//
// Polls per-target buttons for menu navigation.
void pollMenuInput() {
#if defined(HUVVER)
    // huVVer: BtnA = back/exit, BtnB = activate, BtnC = down, BtnD = up
    if (M5.BtnA.wasClicked()) g_model.onBackOrLongPressExit();
    if (M5.BtnB.wasClicked()) {
        auto r = g_model.onActivate();
        if (r == MenuModel::ActivateResult::kToggled) persistSpeedUnits();
    }
    if (M5.BtnC.wasClicked()) g_model.onDown();
    if (M5.BtnD.wasClicked()) g_model.onUp();
#else
    // M5: BtnA = up, BtnB = activate (long-hold = exit), BtnC = down
    if (M5.BtnA.wasClicked()) g_model.onUp();
    if (M5.BtnC.wasClicked()) g_model.onDown();
    if (M5.BtnB.wasHold())    g_model.onBackOrLongPressExit();
    else if (M5.BtnB.wasClicked()) {
        auto r = g_model.onActivate();
        if (r == MenuModel::ActivateResult::kToggled) persistSpeedUnits();
    }
#endif
}

}  // namespace

// ---------------------------------------------------------------------------
// Public surface
// ---------------------------------------------------------------------------

void initSettingsMenu() {
#if defined(ESP_PLATFORM)
    preferences.begin("OnSpeed", true);   // read-only
    g_speedInMph = preferences.getBool("SpeedMph", false);  // default KTS
    preferences.end();
#endif
}

void enterSettingsMenu() {
    if (g_active) return;
    g_model.resetForEntry();
    g_lastTickMs = millis();
    g_active = true;
}

void tickSettingsMenu() {
    if (!g_active) return;

    uint32_t now = millis();
    uint32_t elapsed = now - g_lastTickMs;
    g_lastTickMs = now;

    pollMenuInput();
    g_model.tick(elapsed);
    renderMenu();

    if (g_model.wantsExit()) {
        g_active = false;
        // Sprite was already pushed and deleted by renderMenu().
        // Next loop() iteration will fall through to live-mode rendering.
    }
}

bool isSettingsMenuActive() {
    return g_active;
}
