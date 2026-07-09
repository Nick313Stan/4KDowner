#include "Button.h"

#include <algorithm>
#include <string>

namespace
{
std::string TruncateLabelToWidth(Font font, const char* text, float fontSize, float maxWidth)
{
    if (text == nullptr || text[0] == '\0' || maxWidth <= 0.0f)
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

void DrawButtonChrome(
    Rectangle bounds, Font font, const char* label, Color background, Color border, Color textColor, bool pressed)
{
    const float fontSize = bounds.height < 38.0f ? 16.0f : 20.0f;
    const float maxTextWidth = std::max(4.0f, bounds.width - 12.0f);
    const std::string display = TruncateLabelToWidth(font, label, fontSize, maxTextWidth);
    const Vector2 textSize = MeasureTextEx(font, display.c_str(), fontSize, 0.0f);
    const float textX = bounds.x + (bounds.width - textSize.x) * 0.5f;
    const float textY = bounds.y + (bounds.height - textSize.y) * 0.5f;
    const float minSide = std::max(1.0f, bounds.width < bounds.height ? bounds.width : bounds.height);
    const float roundness = 12.0f * 2.0f / minSide;

    DrawRectangleRounded(bounds, roundness, 12, background);
    DrawRectangleRoundedLines(bounds, roundness, 12, border);
    DrawTextEx(font, display.c_str(), {textX, textY + (pressed ? 1.0f : 0.0f)}, fontSize, 0.0f, textColor);
}
} // namespace

Button::Button(const char* text)
    : text_(text)
{
}

bool Button::Update(Rectangle bounds, bool enabled)
{
    if (!enabled)
    {
        isHovered_ = false;
        isPressed_ = false;
        return false;
    }

    const Vector2 mousePosition = GetMousePosition();
    isHovered_ = CheckCollisionPointRec(mousePosition, bounds);
    isPressed_ = isHovered_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT);

    return isHovered_ && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

void Button::ClearInteraction()
{
    isHovered_ = false;
    isPressed_ = false;
}

void Button::Draw(Rectangle bounds, Font font, bool enabled, bool focused) const
{
    Color background{};
    Color border{};
    Color text{};
    if (!enabled)
    {
        background = Color{28, 34, 28, 255};
        border = Color{54, 64, 54, 255};
        text = Color{118, 128, 118, 255};
    }
    else
    {
        const bool highlight = isHovered_ || focused;
        background =
            isPressed_ ? Color{42, 60, 42, 255} : (highlight ? Color{76, 96, 76, 255} : Color{54, 74, 54, 255});
        border = isPressed_ ? Color{132, 166, 132, 255}
                            : (focused ? Color{148, 198, 148, 255}
                                       : (isHovered_ ? Color{112, 140, 112, 255} : Color{88, 112, 88, 255}));
        text = {232, 238, 232, 255};
    }

    DrawButtonChrome(bounds, font, text_, background, border, text, enabled && isPressed_);
}

void Button::DrawDanger(Rectangle bounds, Font font, bool focused) const
{
    const bool highlight = isHovered_ || focused;
    const Color background =
        isPressed_ ? Color{94, 36, 34, 255} : (highlight ? Color{130, 52, 48, 255} : Color{54, 74, 54, 255});
    const Color border = isPressed_ ? Color{204, 88, 78, 255}
                                    : (focused ? Color{224, 140, 120, 255}
                                               : (isHovered_ ? Color{224, 104, 92, 255} : Color{88, 112, 88, 255}));
    const Color text = {232, 238, 232, 255};
    DrawButtonChrome(bounds, font, text_, background, border, text, isPressed_);
}

void Button::SetText(const char* text)
{
    text_ = text;
}
