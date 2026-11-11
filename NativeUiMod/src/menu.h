#pragma once
// A small keyboard-driven menu drawn entirely with game natives.
// Supports nested submenus (Enter opens, Backspace goes back) and long scrolling lists.
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Menu;

struct MenuSlider {
    float* value = nullptr;       // Left/Right changes it by step, clamped to [min, max]
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.1f;
    const char* format = "%.1f";  // printf format for the value
};

struct MenuItem {
    std::string label;
    std::function<void()> onSelect;  // called on Enter
    bool* toggle = nullptr;          // if set, Enter flips it and the row shows ON/OFF
    MenuSlider slider;               // if slider.value is set, Left/Right adjusts it
    Menu* submenu = nullptr;         // if set, Enter opens it
    std::string rightText;           // small grey text on the right (e.g. a model name)
};

class Menu {
public:
    explicit Menu(std::string title) : title_(std::move(title)) {}

    void AddAction(const std::string& label, std::function<void()> fn,
                   const std::string& rightText = {});
    void AddToggle(const std::string& label, bool* value, std::function<void()> onChange = {});
    // A toggle with a value: Enter switches it on/off, Left/Right changes the value.
    void AddToggleSlider(const std::string& label, bool* enabled, MenuSlider slider,
                         std::function<void()> onChange = {});
    // Adds a row that opens a new child menu (owned by this one) and returns it.
    Menu& AddSubmenu(const std::string& label, const std::string& title);
    // Runs once, the first time this menu is opened (for menus that need the game loaded to fill).
    void SetOnFirstOpen(std::function<void(Menu&)> fn) { onFirstOpen_ = std::move(fn); }
    // Runs every time this menu is opened: the menu is emptied and rebuilt (for live state).
    void SetOnOpen(std::function<void(Menu&)> fn) { onOpen_ = std::move(fn); }
    // Rebuild now with the SetOnOpen builder, keeping the cursor where it is.
    void Refresh();
    void Select(int index) { selected_ = index < 0 ? 0 : index; }
    void Clear();
    size_t Count() const { return items_.size(); }
    const std::string& Title() const { return title_; }
    void SetTitle(std::string t) { title_ = std::move(t); }

    // Root menu only: open/close, jump straight to a submenu, and per-frame update.
    bool IsOpen() const { return open_; }
    void SetOpen(bool open) { open_ = open; }
    void Show(Menu& sub);  // navigate to `sub` (Backspace returns to where we were)
    void Back();           // same as pressing Backspace
    void Tick();           // reads input, then draws if open

private:
    void HandleInput();
    void Draw() const;
    void Enter(Menu& sub);

    std::string title_;
    std::vector<MenuItem> items_;
    std::vector<std::unique_ptr<Menu>> children_;
    std::function<void(Menu&)> onFirstOpen_;
    std::function<void(Menu&)> onOpen_;
    bool opened_ = false;
    int selected_ = 0;
    int scroll_ = 0;             // first visible row

    // Root-only state
    bool open_ = false;
    Menu* current_ = this;
    std::vector<Menu*> back_;    // where Backspace goes
    unsigned long long nextRepeat_ = 0;
    int repeatKey_ = 0;
};

// Opens the game's on-screen keyboard and waits for it. False if cancelled or empty.
bool AskText(std::string& out, int maxLength = 40, const std::string& initial = {});

// Short message shown at the top of the screen for a few seconds.
void ShowStatus(const std::string& text, unsigned durationMs = 2500);
void DrawStatus();
