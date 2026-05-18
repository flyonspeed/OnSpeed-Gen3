// software/OnSpeed-M5-Display/lib/MenuModel/MenuModel.h
//
// Pure-logic menu state machine. No Arduino, no M5GFX, no <chrono>.
// All time is injected via tick(elapsedMs). Lives inside the M5 sub-project
// (not onspeed_core) because it's a UI concern, not flight math.
#pragma once
#include <cstdint>

namespace m5menu {

enum class ItemType { Toggle, Action, Info, Choice };

// Tagged-union item: extra fields are valid only for the matching type.
struct MenuItem {
    const char* label;
    ItemType    type;
    // Toggle-only: pointer to the bool the toggle controls; labels for off/on.
    bool*       toggleValue;
    const char* toggleLabelOff;
    const char* toggleLabelOn;
    // Action-only: callback invoked on activate. May be null for the built-in
    // Exit action (the model returns kExit and the caller handles it).
    void      (*onActivate)();
    // Info-only: callback that returns a string to display on the right.
    const char* (*getInfoValue)();
    // Choice-only: pointer to the int holding the current 0-based selection;
    // choiceCount is the number of options; choiceLabels is an array of
    // C-string labels of length choiceCount. Activate cycles the value
    // modulo choiceCount and returns kToggled.
    int*               choiceValue;
    int                choiceCount;
    const char* const* choiceLabels;
};

class MenuModel {
public:
    enum class ActivateResult { kStayed, kToggled, kAction, kExit };

    static constexpr uint32_t kIdleTimeoutMs = 30'000;

    // items: pointer to caller-owned array (the M5 sub-project keeps the
    // canonical static array in SettingsMenu.cpp). count: number of items.
    MenuModel(const MenuItem* items, int count);

    void onUp();      // wraps to last item from index 0
    void onDown();    // wraps to 0 from last item
    ActivateResult onActivate();    // dispatches by current item's type

    // Called by the platform layer on the entry-symmetric gesture
    // (BtnB long-hold on M5, BtnA click on huVVer).
    void onBackOrLongPressExit();

    // Advances the idle timer. Caller passes elapsed millis since last call.
    // If the timer crosses kIdleTimeoutMs, sets wantsExit() to true.
    void tick(uint32_t elapsedMs);

    bool wantsExit() const  { return wantsExit_; }
    int  currentIndex() const { return currentIndex_; }
    int  itemCount() const    { return count_; }
    const MenuItem& itemAt(int idx) const { return items_[idx]; }

    // Reset entry-time state: cursor returns to the first item, idle
    // timer to zero, exit flag cleared. Called by the platform layer
    // when the menu is freshly entered so each visit starts predictably
    // at the top of the list.
    void resetForEntry();

private:
    const MenuItem* items_;
    int       count_;
    int       currentIndex_   = 0;
    uint32_t  idleMs_         = 0;
    bool      wantsExit_      = false;
};

}  // namespace m5menu
