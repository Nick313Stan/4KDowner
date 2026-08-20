#include "Checkbox.h"

#include <algorithm>
#include <string>

namespace
{
std::string TruncateLabelToWidth(Font font, const char* text, float fontSize, float maxWidth)
{
    if (text == nullptr || text[0] == '\0')
    {
        return {};
    }
    if (maxWidth <= 0.0f)
    {
        return "...";
    }
    if (MeasureTextEx(font, text, fontSize, 0.0f).x <= maxWidth)
    {
        return text;
    }

    const std::string ellipsis = "...";
    if (MeasureTextEx(font, ellipsis.c_str(), fontSize, 0.0f).x > maxWidth)
    {
        return ellipsis;
    }

    const std::string full = text;
    size_t low = 0;
    size_t high = full.size();
    while (low < high)
    {
        const size_t mid = (low + high + 1) / 2;
        const std::string candidate = full.substr(0, mid) + ellipsis;
        if (MeasureTextEx(font, candidate.c_str(), fontSize, 0.0f).x <= maxWidth)
        {
            low = mid;
        }
        else
        {
            high = mid - 1;
        }
    }
    return low == 0 ? ellipsis : full.substr(0, low) + ellipsis;
}
} // namespace

void Checkbox::Update(Rectangle bounds, bool& value, CheckboxPaintSession* paint)
{
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    const bool down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);

    if (paint != nullptr)
    {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hovered)
        {
            value = !value;
            paint->active = true;
            paint->value = value;
        }
        else if (paint->active && down && hovered)
        {
            value = paint->value;
        }

        if (!down)
        {
            paint->active = false;
        }
        armed_ = false;
        return;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hovered)
    {
        armed_ = true;
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        if (armed_ && hovered)
        {
            value = !value;
        }
        armed_ = false;
    }
    else if (!down)
    {
        armed_ = false;
    }
}

void Checkbox::Draw(Rectangle bounds, Font font, const char* label, bool value, bool enabled, float labelMaxWidth) const
{
    const bool hovered = enabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    const Color box = enabled ? (hovered ? Color{72, 86, 72, 255} : Color{42, 48, 42, 255}) : Color{28, 32, 28, 255};
    const Color border =
        !enabled ? (value ? Color{62, 78, 62, 255} : Color{48, 54, 48, 255})
                 : (value ? Color{118, 172, 118, 255} : (hovered ? Color{120, 148, 120, 255} : Color{72, 82, 72, 255}));
    const Color check = enabled ? Color{128, 178, 128, 255} : Color{72, 88, 72, 255};
    const Color text =
        enabled ? (hovered ? Color{238, 244, 238, 255} : Color{220, 226, 220, 255}) : Color{120, 130, 120, 255};

    DrawRectangleRounded(bounds, 0.18f, 8, box);
    DrawRectangleRoundedLines(bounds, 0.18f, 8, border);
    if (value)
    {
        DrawLineEx({bounds.x + 4.0f, bounds.y + 9.0f}, {bounds.x + 8.0f, bounds.y + 13.0f}, 2.0f, check);
        DrawLineEx({bounds.x + 8.0f, bounds.y + 13.0f}, {bounds.x + 15.0f, bounds.y + 5.0f}, 2.0f, check);
    }

    if (label == nullptr || label[0] == '\0')
    {
        return;
    }

    constexpr float kLabelFontSize = 15.0f;
    const float labelX = bounds.x + bounds.width + 8.0f;
    const float maxWidth = labelMaxWidth >= 0.0f ? labelMaxWidth : 10000.0f;
    const std::string display = TruncateLabelToWidth(font, label, kLabelFontSize, maxWidth);
    DrawTextEx(font, display.c_str(), {labelX, bounds.y - 1.0f}, kLabelFontSize, 0.0f, text);
}
