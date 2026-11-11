#pragma once
// Thin helpers over the game's own drawing natives.
// All coordinates are normalised screen space: (0,0) = top-left, (1,1) = bottom-right.
// Everything here must be called every frame (from the script loop) to stay visible.
#include <string>

namespace ui {

struct Color { int r, g, b, a; };

namespace colors {
constexpr Color White{255, 255, 255, 255};
constexpr Color Black{0, 0, 0, 255};
constexpr Color Panel{10, 10, 14, 190};
constexpr Color Header{36, 110, 200, 235};
constexpr Color Highlight{235, 235, 235, 230};
constexpr Color On{90, 200, 110, 255};
constexpr Color Off{200, 80, 80, 255};
}

// Game fonts: 0 = Chalet London, 1 = House Script, 4 = Chalet Comprime (menu style), 7 = Pricedown
enum Font { FontChalet = 0, FontScript = 1, FontCondensed = 4, FontPricedown = 7 };

// Rectangle with its top-left corner at (x, y). DRAW_RECT itself takes the centre point.
void Rect(float x, float y, float w, float h, Color c);

// Text with its top-left (or top-centre if centre=true) at (x, y).
void Text(const std::string& text, float x, float y, float scale, Color c,
          bool centre = false, Font font = FontCondensed, bool shadow = true);

// Right-aligned text ending at x.
void TextRight(const std::string& text, float x, float y, float scale, Color c,
               Font font = FontCondensed);

}  // namespace ui
