#include "Button.h"

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

void Button::Draw(Rectangle bounds, Font font, bool enabled) const
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
        background = isPressed_ ? Color{42, 60, 42, 255} : (isHovered_ ? Color{76, 96, 76, 255} : Color{54, 74, 54, 255});
        border = isPressed_ ? Color{132, 166, 132, 255} : (isHovered_ ? Color{112, 140, 112, 255} : Color{88, 112, 88, 255});
        text = {232, 238, 232, 255};
    }
    float fontSize = bounds.height < 38.0f ? 16.0f : 20.0f;
    Vector2 textSize = MeasureTextEx(font, text_, fontSize, 0.0f);
    while (fontSize > 13.0f && textSize.x > bounds.width - 12.0f)
    {
        fontSize -= 1.0f;
        textSize = MeasureTextEx(font, text_, fontSize, 0.0f);
    }
    const float textX = bounds.x + (bounds.width - textSize.x) * 0.5f;
    const float textY = bounds.y + (bounds.height - textSize.y) * 0.5f;
    const float minSide = bounds.width < bounds.height ? bounds.width : bounds.height;
    const float roundness = 12.0f * 2.0f / minSide;

    DrawRectangleRounded(bounds, roundness, 12, background);
    DrawRectangleRoundedLines(bounds, roundness, 12, border);
    DrawTextEx(font, text_, {textX, textY + ((enabled && isPressed_) ? 1.0f : 0.0f)}, fontSize, 0.0f, text);
}

void Button::DrawDanger(Rectangle bounds, Font font) const
{
    const Color background = isPressed_ ? Color{94, 36, 34, 255} : (isHovered_ ? Color{130, 52, 48, 255} : Color{54, 74, 54, 255});
    const Color border = isPressed_ ? Color{204, 88, 78, 255} : (isHovered_ ? Color{224, 104, 92, 255} : Color{88, 112, 88, 255});
    const Color text = {232, 238, 232, 255};
    float fontSize = bounds.height < 38.0f ? 16.0f : 20.0f;
    Vector2 textSize = MeasureTextEx(font, text_, fontSize, 0.0f);
    while (fontSize > 13.0f && textSize.x > bounds.width - 12.0f)
    {
        fontSize -= 1.0f;
        textSize = MeasureTextEx(font, text_, fontSize, 0.0f);
    }
    const float textX = bounds.x + (bounds.width - textSize.x) * 0.5f;
    const float textY = bounds.y + (bounds.height - textSize.y) * 0.5f;
    const float minSide = bounds.width < bounds.height ? bounds.width : bounds.height;
    const float roundness = 12.0f * 2.0f / minSide;

    DrawRectangleRounded(bounds, roundness, 12, background);
    DrawRectangleRoundedLines(bounds, roundness, 12, border);
    DrawTextEx(font, text_, {textX, textY + (isPressed_ ? 1.0f : 0.0f)}, fontSize, 0.0f, text);
}

void Button::SetText(const char* text)
{
    text_ = text;
}
