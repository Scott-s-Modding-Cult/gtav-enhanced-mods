#include "ui.h"

#include "natives.h"

namespace ui {

void Rect(float x, float y, float w, float h, Color c) {
    GRAPHICS::DRAW_RECT(x + w * 0.5f, y + h * 0.5f, w, h, c.r, c.g, c.b, c.a);
}

static void BeginText(float scale, Color c, Font font, bool shadow) {
    UI::SET_TEXT_FONT(font);
    UI::SET_TEXT_SCALE(0.0f, scale);
    UI::SET_TEXT_COLOUR(c.r, c.g, c.b, c.a);
    if (shadow) {
        UI::SET_TEXT_DROPSHADOW(1, 0, 0, 0, 205);
    } else {
        UI::SET_TEXT_DROPSHADOW(0, 0, 0, 0, 0);
    }
    UI::SET_TEXT_EDGE(0, 0, 0, 0, 0);
}

static void EndText(const std::string& text, float x, float y) {
    UI::_SET_TEXT_ENTRY("STRING");
    UI::_ADD_TEXT_COMPONENT_STRING(const_cast<char*>(text.c_str()));
    UI::_DRAW_TEXT(x, y);
}

void Text(const std::string& text, float x, float y, float scale, Color c, bool centre, Font font,
          bool shadow) {
    BeginText(scale, c, font, shadow);
    UI::SET_TEXT_CENTRE(centre ? 1 : 0);
    EndText(text, x, y);
}

void TextRight(const std::string& text, float x, float y, float scale, Color c, Font font) {
    BeginText(scale, c, font, true);
    UI::SET_TEXT_RIGHT_JUSTIFY(1);
    UI::SET_TEXT_WRAP(0.0f, x);
    EndText(text, x, y);
}

}  // namespace ui
