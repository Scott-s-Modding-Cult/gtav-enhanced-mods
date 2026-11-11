#pragma once
// Keyboard edge detection that only reacts while the game window has focus.
#include <windows.h>

namespace input {

inline bool GameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// True every frame the key is held.
inline bool Down(int vk) {
    return GameHasFocus() && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// True only on the frame the key goes down.
inline bool Pressed(int vk) {
    static bool down[256] = {};
    const bool now = GameHasFocus() && (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool pressed = now && !down[vk & 0xFF];
    down[vk & 0xFF] = now;
    return pressed;
}

}  // namespace input
