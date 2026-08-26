#include "CardChrome.h"

#include "EmojiText.h"
#include "MouseCursor.h"
#include "Tooltip.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace CardChrome
{
namespace
{
std::vector<std::string> WrapLines(Font font, const std::string& text, float fontSize, float maxWidth, int maxLines)
{
    return EmojiText::WrapLines(font, text, fontSize, maxWidth, maxLines);
}
} // namespace

Rectangle AnimatedBounds(Rectangle bounds, double pulseStartTime, double pulseSeconds)
{
    const double elapsed = GetTime() - pulseStartTime;
    if (elapsed < 0.0 || elapsed > pulseSeconds)
    {
        return bounds;
    }

    const float progress = static_cast<float>(elapsed / pulseSeconds);
    const float scale = 1.0f + std::sin(progress * 3.14159265f) * 0.035f;
    const float width = bounds.width * scale;
    const float height = bounds.height * scale;

    return {bounds.x - (width - bounds.width) * 0.5f, bounds.y - (height - bounds.height) * 0.5f, width, height};
}

void DrawStackPeekCard(Rectangle frontBounds, int layerIndex)
{
    if (layerIndex <= 0)
    {
        return;
    }

    const float offsetY = static_cast<float>(layerIndex) * 4.0f;
    const float inset = static_cast<float>(layerIndex) * 3.0f;
    Rectangle peek = {frontBounds.x + inset,
                      frontBounds.y + offsetY,
                      std::max(0.0f, frontBounds.width - inset * 2.0f),
                      frontBounds.height};
    const float minSide = peek.width < peek.height ? peek.width : peek.height;
    const float roundness = (13.0f * 2.0f) / std::max(1.0f, minSide);
    const Color fill = layerIndex >= 2 ? Color{24, 32, 24, 255} : Color{28, 38, 28, 255};
    const Color border = layerIndex >= 2 ? Color{52, 68, 52, 255} : Color{58, 76, 58, 255};
    DrawRectangleRounded(peek, roundness, 16, fill);
    DrawRectangleRoundedLines(peek, roundness, 16, border);
}

Rectangle CloseButtonBounds(Rectangle bounds)
{
    return {bounds.x + bounds.width - 28.0f, bounds.y + 9.0f, 18.0f, 18.0f};
}

Rectangle CopyButtonBounds(Rectangle bounds)
{
    return {bounds.x + bounds.width - 28.0f, bounds.y + 31.0f, 18.0f, 18.0f};
}

Rectangle OpenPathButtonBounds(Rectangle bounds)
{
    return {bounds.x + bounds.width - 28.0f, bounds.y + bounds.height - 27.0f, 18.0f, 18.0f};
}

void DrawCloseButton(Rectangle bounds, Font font)
{
    const Rectangle closeButton = CloseButtonBounds(bounds);
    const bool isHovered = CheckCollisionPointRec(GetMousePosition(), closeButton);
    const Color color = isHovered ? Color{255, 96, 86, 255} : Color{220, 72, 64, 255};
    const float padding = 4.0f;

    DrawLineEx({closeButton.x + padding, closeButton.y + padding},
               {closeButton.x + closeButton.width - padding, closeButton.y + closeButton.height - padding},
               2.0f,
               color);
    DrawLineEx({closeButton.x + closeButton.width - padding, closeButton.y + padding},
               {closeButton.x + padding, closeButton.y + closeButton.height - padding},
               2.0f,
               color);
    if (isHovered)
    {
        UiCursor::RequestHand();
    }
    Tooltip::DrawIfHovered(font, closeButton, "Close");
}

void DrawCopyButton(Rectangle bounds, Font font, bool enabled)
{
    const Rectangle copyButton = CopyButtonBounds(bounds);
    const bool isHovered = enabled && CheckCollisionPointRec(GetMousePosition(), copyButton);
    const Color iconColor =
        !enabled ? Color{90, 100, 90, 255} : (isHovered ? Color{255, 244, 242, 255} : Color{244, 244, 244, 255});

    const float pad = copyButton.width * 0.24f;
    const float sheetWidth = copyButton.width - pad * 2.0f - 3.0f;
    const float sheetHeight = copyButton.height - pad * 2.0f - 3.0f;
    const Rectangle backSheet = {copyButton.x + pad + 3.0f, copyButton.y + pad + 3.0f, sheetWidth, sheetHeight};
    const Rectangle frontSheet = {copyButton.x + pad, copyButton.y + pad, sheetWidth, sheetHeight};
    const float roundness = 0.18f;

    DrawRectangleRounded(backSheet, roundness, 6, Color{iconColor.r, iconColor.g, iconColor.b, 90});
    DrawRectangleRounded(frontSheet, roundness, 6, Color{iconColor.r, iconColor.g, iconColor.b, 40});
    DrawRectangleRoundedLines(frontSheet, roundness, 6, iconColor);
    DrawLineEx({frontSheet.x + frontSheet.width * 0.28f, frontSheet.y + frontSheet.height * 0.62f},
               {frontSheet.x + frontSheet.width * 0.72f, frontSheet.y + frontSheet.height * 0.62f},
               1.5f,
               iconColor);
    DrawLineEx({frontSheet.x + frontSheet.width * 0.28f, frontSheet.y + frontSheet.height * 0.78f},
               {frontSheet.x + frontSheet.width * 0.72f, frontSheet.y + frontSheet.height * 0.78f},
               1.5f,
               iconColor);
    if (isHovered)
    {
        UiCursor::RequestHand();
    }
    if (enabled)
    {
        Tooltip::DrawIfHovered(font, copyButton, "Copy info");
    }
}

void DrawOpenPathButton(Rectangle bounds, Font font, bool enabled)
{
    const Rectangle openButton = OpenPathButtonBounds(bounds);
    const bool isHovered = enabled && CheckCollisionPointRec(GetMousePosition(), openButton);
    const Color iconColor =
        !enabled ? Color{90, 100, 90, 255} : (isHovered ? Color{255, 244, 242, 255} : Color{244, 244, 244, 255});

    DrawRectangleLinesEx({openButton.x + 4.0f, openButton.y + 7.0f, 10.0f, 7.0f}, 1.2f, iconColor);
    DrawRectangleRec({openButton.x + 5.0f, openButton.y + 5.0f, 4.0f, 2.5f}, iconColor);
    if (isHovered)
    {
        UiCursor::RequestHand();
    }
    if (enabled)
    {
        Tooltip::DrawIfHovered(font, openButton, "Open folder");
    }
}

void DrawCompletedCheckmarkBackdrop(Rectangle cardBounds)
{
    const float left = cardBounds.x + kTextXOffset;
    const float right = cardBounds.x + cardBounds.width - 34.0f;
    if (right <= left + 8.0f || cardBounds.height <= 8.0f)
    {
        return;
    }

    const float cx = (left + right) * 0.5f;
    const float cy = cardBounds.y + cardBounds.height * 0.5f;
    const float size = std::min(cardBounds.height * 0.78f, (right - left) * 0.62f);
    const Color check = {72, 210, 110, 58};
    const float thickness = std::max(3.5f, size * 0.11f);

    const Vector2 p1 = {cx - size * 0.38f, cy + size * 0.02f};
    const Vector2 p2 = {cx - size * 0.06f, cy + size * 0.34f};
    const Vector2 p3 = {cx + size * 0.44f, cy - size * 0.32f};
    DrawLineEx(p1, p2, thickness, check);
    DrawLineEx(p2, p3, thickness, check);
}

void DrawPreviewElapsedOverlay(Font font, Rectangle previewBounds, double elapsedSeconds)
{
    if (elapsedSeconds < 0.0 || previewBounds.width < 8.0f || previewBounds.height < 8.0f)
    {
        return;
    }

    const int totalSeconds = static_cast<int>(elapsedSeconds + 0.5);
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds / 60) % 60;
    const int secs = totalSeconds % 60;
    char buffer[32]{};
    if (hours > 0)
    {
        std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes, secs);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, secs);
    }

    const float fontSize = 14.0f;
    const float barHeight = std::min(18.0f, previewBounds.height);
    const Rectangle bar = {
        previewBounds.x, previewBounds.y + previewBounds.height - barHeight, previewBounds.width, barHeight};
    DrawRectangleRec(bar, Color{0, 0, 0, 165});

    const Vector2 textSize = MeasureTextEx(font, buffer, fontSize, 0.0f);
    DrawTextEx(font,
               buffer,
               {bar.x + (bar.width - textSize.x) * 0.5f, bar.y + (bar.height - textSize.y) * 0.5f},
               fontSize,
               0.0f,
               Color{245, 248, 245, 255});
}

void DrawBottomRightRoundedBadge(Rectangle badge, float cornerRadius, Color color)
{
    const float radius = std::min({cornerRadius, badge.width * 0.5f, badge.height * 0.5f});
    if (radius <= 0.5f)
    {
        DrawRectangleRec(badge, color);
        return;
    }

    DrawRectangleRec({badge.x, badge.y, badge.width, badge.height - radius}, color);
    DrawRectangleRec({badge.x, badge.y + badge.height - radius, badge.width - radius, radius}, color);

    // Screen Y grows downward, so +x → +y is clockwise; reverse winding for raylib CCW fill.
    const Vector2 center = {badge.x + badge.width - radius, badge.y + badge.height - radius};
    constexpr int kSegments = 12;
    for (int index = 0; index < kSegments; ++index)
    {
        const float a0 = (static_cast<float>(index) / static_cast<float>(kSegments)) * (PI * 0.5f);
        const float a1 = (static_cast<float>(index + 1) / static_cast<float>(kSegments)) * (PI * 0.5f);
        const Vector2 p0 = {center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius};
        const Vector2 p1 = {center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius};
        DrawTriangle(center, p1, p0, color);
    }
}

void DrawPreviewIndexBadge(Font font, Rectangle previewBounds, int displayIndex)
{
    if (displayIndex <= 0 || previewBounds.width < 8.0f || previewBounds.height < 8.0f)
    {
        return;
    }

    const std::string label = std::to_string(displayIndex);
    const float fontSize = 14.0f;
    const float padX = 5.0f;
    const float padY = 2.0f;
    const Vector2 textSize = MeasureTextEx(font, label.c_str(), fontSize, 0.0f);
    const float badgeW = std::min(previewBounds.width, std::max(textSize.x + padX * 2.0f, textSize.y + padY * 2.0f));
    const float badgeH = std::min(previewBounds.height, textSize.y + padY * 2.0f);
    const Rectangle badge = {previewBounds.x, previewBounds.y, badgeW, badgeH};

    DrawBottomRightRoundedBadge(badge, 7.0f, Color{0, 0, 0, 170});
    DrawTextEx(font,
               label.c_str(),
               {badge.x + (badge.width - textSize.x) * 0.5f, badge.y + (badge.height - textSize.y) * 0.5f},
               fontSize,
               0.0f,
               Color{245, 248, 245, 255});
}

void DrawWrappedText(
    Font font, const std::string& text, Vector2 position, float fontSize, float maxWidth, int maxLines, Color color)
{
    EmojiText::DrawWrapped(font, text, position, fontSize, maxWidth, maxLines, color);
}

bool IsTitleTextHovered(Rectangle bounds,
                        Font font,
                        const std::string& title,
                        float titleStartY,
                        float fontSize,
                        float maxWidthInset,
                        int maxLines)
{
    if (title.empty())
    {
        return false;
    }

    const float textX = bounds.x + kTextXOffset;
    const float titleMaxWidth = bounds.width - maxWidthInset;
    const float lineStep = fontSize + 3.0f;
    const float startY = bounds.y + titleStartY;
    const std::vector<std::string> lines = WrapLines(font, title, fontSize, titleMaxWidth, maxLines);

    const Vector2 mouse = GetMousePosition();
    for (int index = 0; index < static_cast<int>(lines.size()); ++index)
    {
        const float lineWidth = EmojiText::MeasureWidth(font, lines[static_cast<size_t>(index)], fontSize);
        const Rectangle lineBounds = {textX, startY + static_cast<float>(index) * lineStep, lineWidth, fontSize + 2.0f};
        if (CheckCollisionPointRec(mouse, lineBounds))
        {
            return true;
        }
    }
    return false;
}

float MeasureWrappedTextHeight(Font font, const std::string& text, float fontSize, float maxWidth, int maxLines)
{
    return EmojiText::MeasureWrappedHeight(font, text, fontSize, maxWidth, maxLines);
}
} // namespace CardChrome
