#include "Tooltip.h"

#include <string>
#include <vector>

namespace
{
struct PendingTooltip
{
    Font font{};
    Rectangle anchor{};
    std::string text;
};

std::vector<PendingTooltip>& PendingQueue()
{
    static std::vector<PendingTooltip> queue;
    return queue;
}
} // namespace

void Tooltip::DrawImmediate(Font font, Rectangle anchor, const char* text)
{
    if (text == nullptr || text[0] == '\0')
    {
        return;
    }

    const float fontSize = 15.0f;
    const float padX = 8.0f;
    const float padY = 4.0f;
    const Vector2 textSize = MeasureTextEx(font, text, fontSize, 0.0f);
    const float tipWidth = textSize.x + padX * 2.0f;
    const float tipHeight = textSize.y + padY * 2.0f;
    Rectangle tip = {
        anchor.x + anchor.width * 0.5f - tipWidth * 0.5f, anchor.y - tipHeight - 6.0f, tipWidth, tipHeight};

    if (tip.y < 4.0f)
    {
        tip.y = anchor.y + anchor.height + 6.0f;
    }
    if (tip.x < 4.0f)
    {
        tip.x = 4.0f;
    }
    if (tip.x + tip.width > static_cast<float>(GetRenderWidth()) - 4.0f)
    {
        tip.x = static_cast<float>(GetRenderWidth()) - tip.width - 4.0f;
    }

    DrawRectangleRounded(tip, 0.22f, 8, Color{38, 48, 38, 245});
    DrawRectangleRoundedLines(tip, 0.22f, 8, Color{96, 126, 96, 255});
    DrawTextEx(font, text, {tip.x + padX, tip.y + padY}, fontSize, 0.0f, Color{224, 232, 224, 255});
}

void Tooltip::Draw(Font font, Rectangle anchor, const char* text)
{
    if (text == nullptr || text[0] == '\0')
    {
        return;
    }
    PendingQueue().push_back(PendingTooltip{font, anchor, text});
}

void Tooltip::DrawIfHovered(Font font, Rectangle anchor, const char* text)
{
    if (CheckCollisionPointRec(GetMousePosition(), anchor))
    {
        Draw(font, anchor, text);
    }
}

void Tooltip::Flush()
{
    for (const PendingTooltip& tip : PendingQueue())
    {
        DrawImmediate(tip.font, tip.anchor, tip.text.c_str());
    }
    PendingQueue().clear();
}

void Tooltip::Clear()
{
    PendingQueue().clear();
}
