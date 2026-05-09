// software/OnSpeed-M5-Display/lib/MenuModel/MenuModel.cpp
#include "MenuModel.h"

namespace m5menu {

MenuModel::MenuModel(MenuItem* items, int count)
    : items_(items), count_(count) {}

void MenuModel::onUp() {
    idleMs_ = 0;
    if (count_ <= 0) return;
    currentIndex_ = (currentIndex_ - 1 + count_) % count_;
}

void MenuModel::onDown() {
    idleMs_ = 0;
    if (count_ <= 0) return;
    currentIndex_ = (currentIndex_ + 1) % count_;
}

MenuModel::ActivateResult MenuModel::onActivate() {
    idleMs_ = 0;
    if (count_ <= 0) return ActivateResult::kStayed;
    const MenuItem& item = items_[currentIndex_];
    switch (item.type) {
        case ItemType::Toggle:
            if (item.toggleValue) *item.toggleValue = !*item.toggleValue;
            return ActivateResult::kToggled;
        case ItemType::Action:
            if (item.onActivate) {
                item.onActivate();
                return ActivateResult::kAction;
            }
            // Action with no callback = built-in Exit.
            wantsExit_ = true;
            return ActivateResult::kExit;
        case ItemType::Info:
            return ActivateResult::kStayed;
    }
    return ActivateResult::kStayed;
}

void MenuModel::onBackOrLongPressExit() {
    wantsExit_ = true;
}

void MenuModel::tick(uint32_t elapsedMs) {
    idleMs_ += elapsedMs;
    if (idleMs_ >= kIdleTimeoutMs) wantsExit_ = true;
}

void MenuModel::resetForEntry() {
    idleMs_    = 0;
    wantsExit_ = false;
}

}  // namespace m5menu
