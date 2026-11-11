#include "menu.h"

#include <windows.h>

#include <cmath>
#include <cstdio>

#include "input.h"
#include "main.h"  // WAIT
#include "natives.h"
#include "ui.h"

namespace {
// Layout (normalised screen units)
constexpr float kX = 0.020f;
constexpr float kY = 0.080f;
constexpr float kWidth = 0.250f;
constexpr float kHeaderH = 0.060f;
constexpr float kRowH = 0.034f;
constexpr float kPad = 0.008f;
constexpr float kTextScale = 0.36f;
constexpr int kMaxRows = 16;  // rows visible at once; longer menus scroll

constexpr ui::Color kDim{170, 170, 170, 255};
constexpr ui::Color kDimSel{70, 70, 70, 255};

// Game controls that share keys with the menu; disabled while it is open so
// arrows/Enter/Backspace don't also open the phone, pause menu, etc.
constexpr int kBlockedControls[] = {
    27,                                  // INPUT_PHONE
    172, 173, 174, 175, 176, 177, 178,   // INPUT_CELLPHONE_*
    199, 200,                            // INPUT_FRONTEND_PAUSE(_ALTERNATE)
    201, 202,                            // INPUT_FRONTEND_ACCEPT / CANCEL
};

std::string g_status;
ULONGLONG g_statusUntil = 0;

void Sound(const char* name) {
    AUDIO::PLAY_SOUND_FRONTEND(-1, const_cast<char*>(name),
                               const_cast<char*>("HUD_FRONTEND_DEFAULT_SOUNDSET"), 0);
}
}  // namespace

void Menu::AddAction(const std::string& label, std::function<void()> fn, const std::string& rightText) {
    MenuItem it;
    it.label = label;
    it.onSelect = std::move(fn);
    it.rightText = rightText;
    items_.push_back(std::move(it));
}

void Menu::AddToggle(const std::string& label, bool* value, std::function<void()> onChange) {
    MenuItem it;
    it.label = label;
    it.onSelect = std::move(onChange);
    it.toggle = value;
    items_.push_back(std::move(it));
}

void Menu::AddToggleSlider(const std::string& label, bool* enabled, MenuSlider slider,
                           std::function<void()> onChange) {
    MenuItem it;
    it.label = label;
    it.onSelect = std::move(onChange);
    it.toggle = enabled;
    it.slider = slider;
    items_.push_back(std::move(it));
}

Menu& Menu::AddSubmenu(const std::string& label, const std::string& title) {
    children_.push_back(std::make_unique<Menu>(title));
    MenuItem it;
    it.label = label;
    it.submenu = children_.back().get();
    items_.push_back(std::move(it));
    return *children_.back();
}

void Menu::Clear() {
    items_.clear();
    children_.clear();
    selected_ = 0;
    scroll_ = 0;
}

void Menu::Refresh() {
    if (!onOpen_) return;
    const int sel = selected_, sc = scroll_;
    items_.clear();
    children_.clear();
    onOpen_(*this);
    const int n = static_cast<int>(items_.size());
    selected_ = sel < n ? sel : (n > 0 ? n - 1 : 0);
    scroll_ = sc <= selected_ ? sc : selected_;
}

void Menu::Enter(Menu& sub) {
    back_.push_back(current_);
    current_ = &sub;
    if (!sub.opened_) {
        sub.opened_ = true;
        if (sub.onFirstOpen_) sub.onFirstOpen_(sub);
    }
    if (sub.onOpen_) {  // live menu: rebuild from scratch (the builder may Select() a row)
        sub.items_.clear();
        sub.children_.clear();
        sub.selected_ = 0;
        sub.scroll_ = 0;
        sub.onOpen_(sub);
    }
    if (sub.selected_ >= static_cast<int>(sub.items_.size())) sub.selected_ = 0;
}

void Menu::Show(Menu& sub) {
    open_ = true;
    if (current_ != &sub) Enter(sub);
}

void Menu::Back() {
    if (back_.empty()) { open_ = false; return; }
    current_ = back_.back();
    back_.pop_back();
}

void Menu::Tick() {
    HandleInput();
    if (open_) Draw();
}

void Menu::HandleInput() {
    // Poll every key every frame so edge detection stays in sync.
    const bool f5 = input::Pressed(VK_F5);
    const bool upNow = input::Pressed(VK_UP), downNow = input::Pressed(VK_DOWN);
    const bool leftNow = input::Pressed(VK_LEFT), rightNow = input::Pressed(VK_RIGHT);
    const bool backNow = input::Pressed(VK_BACK), enterNow = input::Pressed(VK_RETURN);

    if (f5) {
        open_ = !open_;
        Sound(open_ ? "SELECT" : "BACK");
    }
    if (!open_) return;
    for (int c : kBlockedControls) CONTROLS::DISABLE_CONTROL_ACTION(0, c, 1);

    // Press once = one step; hold = repeat quickly after a short delay.
    const ULONGLONG now = GetTickCount64();
    auto fire = [&](int vk, bool pressedNow) {
        if (pressedNow) { repeatKey_ = vk; nextRepeat_ = now + 350; return true; }
        if (repeatKey_ != vk) return false;
        if (!input::Down(vk)) { repeatKey_ = 0; return false; }
        if (now < nextRepeat_) return false;
        nextRepeat_ = now + 50;
        return true;
    };
    const bool up = fire(VK_UP, upNow), down = fire(VK_DOWN, downNow);
    const bool left = fire(VK_LEFT, leftNow), right = fire(VK_RIGHT, rightNow);

    if (backNow) {
        Sound("BACK");
        if (back_.empty()) { open_ = false; return; }
        current_ = back_.back();
        back_.pop_back();
        return;
    }

    Menu& m = *current_;
    const int count = static_cast<int>(m.items_.size());
    if (count == 0) return;

    if (up) { m.selected_ = (m.selected_ + count - 1) % count; Sound("NAV_UP_DOWN"); }
    if (down) { m.selected_ = (m.selected_ + 1) % count; Sound("NAV_UP_DOWN"); }

    MenuSlider& sl = m.items_[m.selected_].slider;
    if ((left || right) && sl.value) {
        // Slider row: change the value.
        float v = *sl.value + (left ? -1 : 1) * sl.step;
        v = std::round(v / sl.step) * sl.step;  // avoid float drift (2.9999...)
        v = v < sl.min ? sl.min : (v > sl.max ? sl.max : v);
        if (v != *sl.value) { *sl.value = v; Sound("NAV_UP_DOWN"); }
    } else if ((left || right) && count > kMaxRows) {
        // Long list: Left/Right jump a page.
        int s = m.selected_ + (left ? -kMaxRows : kMaxRows);
        s = s < 0 ? 0 : (s >= count ? count - 1 : s);
        if (s != m.selected_) { m.selected_ = s; Sound("NAV_UP_DOWN"); }
    }

    // Keep the selection on screen.
    if (m.selected_ < m.scroll_) m.scroll_ = m.selected_;
    if (m.selected_ >= m.scroll_ + kMaxRows) m.scroll_ = m.selected_ - kMaxRows + 1;

    if (enterNow) {
        MenuItem& item = m.items_[m.selected_];
        Sound("SELECT");
        if (item.submenu) { Enter(*item.submenu); return; }
        if (item.toggle) *item.toggle = !*item.toggle;
        // Copy: the callback may rebuild this menu (invalidating `item`).
        const std::function<void()> fn = item.onSelect;
        if (fn) fn();
    }
}

void Menu::Draw() const {
    using namespace ui;
    const Menu& m = *current_;
    const int count = static_cast<int>(m.items_.size());
    const int first = m.scroll_;
    const int last = first + kMaxRows < count ? first + kMaxRows : count;
    const int rows = last - first > 0 ? last - first : 1;

    // Header
    Rect(kX, kY, kWidth, kHeaderH, colors::Header);
    Text(m.title_, kX + kWidth * 0.5f, kY + 0.008f, 0.75f, colors::White, true, FontScript);

    // Body
    float y = kY + kHeaderH;
    Rect(kX, y, kWidth, kRowH * rows + kRowH, colors::Panel);
    if (count == 0) {
        Text("(empty)", kX + kPad, y + 0.004f, kTextScale, kDim, false, FontChalet);
        y += kRowH;
    }
    for (int i = first; i < last; ++i) {
        const MenuItem& item = m.items_[i];
        const bool sel = i == m.selected_;
        if (sel) Rect(kX, y, kWidth, kRowH, colors::Highlight);
        const Color textCol = sel ? colors::Black : colors::White;
        Text(item.label, kX + kPad, y + 0.004f, kTextScale, textCol, false, FontChalet, !sel);
        const float right = kX + kWidth - kPad;
        if (item.slider.value) {
            // e.g. "< 3.0x >", coloured ON/OFF when it is also a toggle
            char num[32], buf[48];
            std::snprintf(num, sizeof num, item.slider.format, *item.slider.value);
            std::snprintf(buf, sizeof buf, sel ? "< %s >" : "%s", num);
            Color c = textCol;
            if (item.toggle) c = *item.toggle ? colors::On : colors::Off;
            TextRight(buf, right, y + 0.004f, kTextScale, c, FontChalet);
        } else if (item.toggle) {
            const bool on = *item.toggle;
            TextRight(on ? "ON" : "OFF", right, y + 0.004f, kTextScale, on ? colors::On : colors::Off,
                      FontChalet);
        } else if (item.submenu) {
            TextRight(">", right, y + 0.004f, kTextScale, textCol, FontChalet);
        } else if (!item.rightText.empty()) {
            TextRight(item.rightText, right, y + 0.008f, 0.28f, sel ? kDimSel : kDim, FontChalet);
        }
        y += kRowH;
    }

    // Footer: hint, plus position in long lists.
    Text(back_.empty() ? "F5 close  |  Arrows  |  Enter  |  Backspace"
                       : "Backspace back  |  Arrows  |  Enter",
         kX + kPad, y + 0.008f, 0.27f, colors::White, false, FontChalet);
    if (count > kMaxRows) {
        char pos[32];
        std::snprintf(pos, sizeof pos, "%d / %d", m.selected_ + 1, count);
        TextRight(pos, kX + kWidth - kPad, y + 0.008f, 0.27f, colors::White, FontChalet);
    }
}

bool AskText(std::string& out, int maxLength, const std::string& initial) {
    GAMEPLAY::DISPLAY_ONSCREEN_KEYBOARD(TRUE, const_cast<char*>("FMMC_KEY_TIP8"), const_cast<char*>(""),
                                        const_cast<char*>(initial.c_str()), const_cast<char*>(""),
                                        const_cast<char*>(""), const_cast<char*>(""), maxLength);
    int state;
    while ((state = GAMEPLAY::UPDATE_ONSCREEN_KEYBOARD()) == 0) WAIT(0);
    if (state != 1) return false;  // cancelled
    const char* r = GAMEPLAY::GET_ONSCREEN_KEYBOARD_RESULT();
    out = r ? r : "";
    return !out.empty();
}

void ShowStatus(const std::string& text, unsigned durationMs) {
    g_status = text;
    g_statusUntil = GetTickCount64() + durationMs;
}

void DrawStatus() {
    if (g_status.empty() || GetTickCount64() > g_statusUntil) return;
    // Top centre, clear of the menu on the left and the minimap.
    ui::Rect(0.38f, 0.015f, 0.24f, 0.04f, ui::colors::Panel);
    ui::Text(g_status, 0.50f, 0.022f, 0.38f, ui::colors::White, true, ui::FontChalet);
}
