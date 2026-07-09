#include "Checkbox.h"

void Checkbox::Update(Rectangle bounds, bool& value)
{
    if (CheckCollisionPointRec(GetMousePosition(), bounds) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        value = !value;
    }
}

void Checkbox::Draw(Rectangle bounds, Font font, const char* label, bool value, bool enabled) const
{
    const bool hovered = enabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    const Color box = enabled ? (hovered ? Color{72, 86, 72, 255} : Color{42, 48, 42, 255}) : Color{28, 32, 28, 255};
    const Color border = value ? Color{118, 172, 118, 255} : (hovered ? Color{120, 148, 120, 255} : Color{72, 82, 72, 255});
    const Color text = enabled ? (hovered ? Color{238, 244, 238, 255} : Color{220, 226, 220, 255}) : Color{120, 130, 120, 255};

    DrawRectangleRounded(bounds, 0.18f, 8, box);
    DrawRectangleRoundedLines(bounds, 0.18f, 8, border);
    if (value)
    {
        DrawLineEx({bounds.x + 4.0f, bounds.y + 9.0f}, {bounds.x + 8.0f, bounds.y + 13.0f}, 2.0f, Color{128, 178, 128, 255});
        DrawLineEx({bounds.x + 8.0f, bounds.y + 13.0f}, {bounds.x + 15.0f, bounds.y + 5.0f}, 2.0f, Color{128, 178, 128, 255});
    }
    DrawTextEx(font, label, {bounds.x + bounds.width + 8.0f, bounds.y - 1.0f}, 15.0f, 0.0f, text);
}
