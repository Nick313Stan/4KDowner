#include "CardChrome.h"

#include "EmojiText.h"
#include "MouseCursor.h"
#include "Tooltip.h"
#include "WinAppPaths.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
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

// Match DrawPreviewIndexBadge height (14px digit + vertical padding).
float PreviewIndexBadgeHeight(Font font)
{
    constexpr float kIndexFontSize = 14.0f;
    constexpr float kBadgePadY = 2.0f;
    const Vector2 ref = MeasureTextEx(font, "1", kIndexFontSize, 0.0f);
    return ref.y + kBadgePadY * 2.0f;
}

void DrawRestoreIcon(Vector2 center, float size, Color color)
{
    const float radius = size * 0.30f;
    const float thickness = std::max(2.8f, size * 0.115f);
    const float inner = std::max(0.5f, radius - thickness * 0.5f);
    const float outer = radius + thickness * 0.5f;

    // Clockwise open ring: tail at ~4 o'clock, head at ~12 o'clock (raylib: 0=right, 90=down).
    constexpr float kTailDeg = 52.0f;
    constexpr float kHeadDeg = 272.0f;
    DrawRing(center, inner, outer, kTailDeg, kHeadDeg, 36, color);

    const float tailRad = kTailDeg * DEG2RAD;
    DrawCircleV(
        {center.x + std::cos(tailRad) * radius, center.y + std::sin(tailRad) * radius}, thickness * 0.52f, color);

    const float headRad = kHeadDeg * DEG2RAD;
    const Vector2 head = {center.x + std::cos(headRad) * radius, center.y + std::sin(headRad) * radius};
    const float ah = thickness * 0.62f;
    const float aw = thickness * 0.95f;
    const Vector2 tip = {head.x + aw, head.y};
    const Vector2 baseA = {head.x - ah * 0.35f, head.y - ah};
    const Vector2 baseB = {head.x - ah * 0.35f, head.y + ah};
    DrawTriangle(tip, baseA, baseB, color);
}

Texture2D& RestoreIconTexture()
{
    static Texture2D texture{};
    static bool loaded = false;
    if (!loaded)
    {
        loaded = true;
        const std::filesystem::path path = FindAssetPath(std::filesystem::path("assets") / "icons" / "restore.png");
        if (std::filesystem::exists(path))
        {
            texture = LoadTexture(path.string().c_str());
            if (texture.id != 0)
            {
                SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
            }
        }
    }
    return texture;
}

void DrawRestoreIconTexture(Vector2 center, float overlaySize, bool isHovered)
{
    Texture2D& texture = RestoreIconTexture();
    if (texture.id == 0)
    {
        const Color color = isHovered ? Color{140, 210, 140, 255} : Color{120, 188, 120, 255};
        DrawRestoreIcon(center, overlaySize, color);
        return;
    }

    const float scale = isHovered ? 1.05f : 1.0f;
    const float iconSize = overlaySize * 0.62f * scale;
    const Rectangle dest = {center.x - iconSize * 0.5f, center.y - iconSize * 0.5f, iconSize, iconSize};
    const Rectangle source = {0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)};
    const Color tint = isHovered ? Color{255, 255, 255, 255} : Color{220, 240, 220, 255};
    DrawTexturePro(texture, source, dest, {0.0f, 0.0f}, 0.0f, tint);
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
    const bool isHovered = UiCursor::IsMouseOverRect(closeButton);
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

Rectangle CloseButtonBoundsCompact(Rectangle bounds)
{
    constexpr float kSize = 14.0f;
    return {bounds.x + bounds.width - kSize - 6.0f, bounds.y + (bounds.height - kSize) * 0.5f, kSize, kSize};
}

Rectangle DismissOverlayBoundsCompact(Rectangle bounds)
{
    const float size = std::min(bounds.height * 0.72f, 28.0f);
    const float cx = bounds.x + bounds.width * 0.5f;
    const float cy = bounds.y + bounds.height * 0.5f;
    return {cx - size * 0.5f, cy - size * 0.5f, size, size};
}

void DrawCloseButtonCompact(Rectangle bounds, Font font)
{
    const Rectangle closeButton = CloseButtonBoundsCompact(bounds);
    const bool isHovered = UiCursor::IsMouseOverRect(closeButton);
    const Color color = isHovered ? Color{255, 96, 86, 255} : Color{220, 72, 64, 255};
    const float padding = 3.0f;

    DrawLineEx({closeButton.x + padding, closeButton.y + padding},
               {closeButton.x + closeButton.width - padding, closeButton.y + closeButton.height - padding},
               1.8f,
               color);
    DrawLineEx({closeButton.x + closeButton.width - padding, closeButton.y + padding},
               {closeButton.x + padding, closeButton.y + closeButton.height - padding},
               1.8f,
               color);
    if (isHovered)
    {
        UiCursor::RequestHand();
    }
    Tooltip::DrawIfHovered(font, closeButton, "Close");
}

void DrawDismissOverlayCompact(Rectangle cardBounds, Font font, bool showReload)
{
    const Rectangle overlay = DismissOverlayBoundsCompact(cardBounds);
    const bool isHovered = UiCursor::IsMouseOverRect(overlay);
    DrawRectangleRounded(overlay, 0.5f, 12, Color{0, 0, 0, 130});

    const Vector2 center = {overlay.x + overlay.width * 0.5f, overlay.y + overlay.height * 0.5f};
    if (showReload)
    {
        DrawRestoreIconTexture(center, overlay.width, isHovered);
    }
    else
    {
        const Color color = isHovered ? Color{255, 110, 100, 255} : Color{240, 96, 86, 255};
        const float pad = overlay.width * 0.28f;
        DrawLineEx({overlay.x + pad, overlay.y + pad},
                   {overlay.x + overlay.width - pad, overlay.y + overlay.height - pad},
                   2.0f,
                   color);
        DrawLineEx({overlay.x + overlay.width - pad, overlay.y + pad},
                   {overlay.x + pad, overlay.y + overlay.height - pad},
                   2.0f,
                   color);
    }

    if (isHovered)
    {
        UiCursor::RequestHand();
    }
    Tooltip::DrawIfHovered(font, overlay, "Restore");
}

Rectangle DismissOverlayBounds(Rectangle cardBounds)
{
    constexpr float kSize = 44.0f;
    const float cx = cardBounds.x + cardBounds.width * 0.5f;
    const float cy = cardBounds.y + cardBounds.height * 0.5f;
    return {cx - kSize * 0.5f, cy - kSize * 0.5f, kSize, kSize};
}

void DrawDismissOverlay(Rectangle cardBounds, Font font, bool showReload)
{
    const Rectangle overlay = DismissOverlayBounds(cardBounds);
    const bool isHovered = UiCursor::IsMouseOverRect(overlay);
    DrawRectangleRounded(overlay, 0.5f, 12, Color{0, 0, 0, 130});

    const Vector2 center = {overlay.x + overlay.width * 0.5f, overlay.y + overlay.height * 0.5f};
    if (showReload)
    {
        DrawRestoreIconTexture(center, overlay.width, isHovered);
    }
    else
    {
        const Color color = isHovered ? Color{255, 110, 100, 255} : Color{240, 96, 86, 255};
        const float pad = overlay.width * 0.28f;
        DrawLineEx({overlay.x + pad, overlay.y + pad},
                   {overlay.x + overlay.width - pad, overlay.y + overlay.height - pad},
                   2.5f,
                   color);
        DrawLineEx({overlay.x + overlay.width - pad, overlay.y + pad},
                   {overlay.x + pad, overlay.y + overlay.height - pad},
                   2.5f,
                   color);
    }

    if (isHovered)
    {
        UiCursor::RequestHand();
    }
    Tooltip::DrawIfHovered(font, overlay, "Restore");
}

void DrawCopyButton(Rectangle bounds, Font font, bool enabled)
{
    const Rectangle copyButton = CopyButtonBounds(bounds);
    const bool isHovered = enabled && UiCursor::IsMouseOverRect(copyButton);
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
    const bool isHovered = enabled && UiCursor::IsMouseOverRect(openButton);
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

void DrawPreviewLiveBadge(Font font, Rectangle previewBounds)
{
    if (previewBounds.width < 24.0f || previewBounds.height < 12.0f)
    {
        return;
    }

    const char* label = "LIVE";
    const float fontSize = 11.0f;
    const float padX = 5.0f;
    const float padY = 2.0f;
    const float iconW = 10.0f;
    const float gap = 3.0f;
    const Vector2 textSize = MeasureTextEx(font, label, fontSize, 0.0f);
    const float badgeH =
        std::min(previewBounds.height, std::max(PreviewIndexBadgeHeight(font), textSize.y + padY * 2.0f));
    const float badgeW = std::min(previewBounds.width, iconW + gap + textSize.x + padX * 2.0f);
    const Rectangle badge = {previewBounds.x + previewBounds.width - badgeW, previewBounds.y, badgeW, badgeH};

    DrawRectangleRounded(badge, 0.35f, 6, Color{220, 35, 35, 255});

    const float iconCx = badge.x + padX + iconW * 0.5f;
    const float iconCy = badge.y + badgeH * 0.5f;
    const Color iconColor = Color{255, 255, 255, 255};
    DrawCircleV({iconCx, iconCy}, 1.6f, iconColor);
    // Broadcast arcs above/below the dot (YouTube-style); angles rotated 90° from side-facing rings.
    DrawRing({iconCx, iconCy}, 3.2f, 4.0f, 300.0f, 420.0f, 10, iconColor);
    DrawRing({iconCx, iconCy}, 3.2f, 4.0f, 120.0f, 240.0f, 10, iconColor);
    DrawRing({iconCx, iconCy}, 5.2f, 6.0f, 300.0f, 420.0f, 12, iconColor);
    DrawRing({iconCx, iconCy}, 5.2f, 6.0f, 120.0f, 240.0f, 12, iconColor);

    DrawTextEx(font,
               label,
               {badge.x + padX + iconW + gap, badge.y + (badgeH - textSize.y) * 0.5f},
               fontSize,
               0.0f,
               Color{255, 255, 255, 255});
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

    if (!UiCursor::PointerHoverActive())
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
