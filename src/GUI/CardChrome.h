#pragma once

#include "raylib.h"

#include <string>

namespace CardChrome
{
constexpr float kTextXOffset = 96.0f;
constexpr float kTitleWidthInset = 130.0f;
constexpr float kTitleFontSize = 16.0f;
constexpr float kTitleStartY = 10.0f;
constexpr int kTitleMaxLines = 2;
constexpr double kDefaultPulseSeconds = 0.34;

inline float TitleMaxWidth(float cardWidth)
{
    return cardWidth - kTitleWidthInset;
}

Rectangle AnimatedBounds(Rectangle bounds, double pulseStartTime, double pulseSeconds = kDefaultPulseSeconds);

Rectangle CloseButtonBounds(Rectangle bounds);
Rectangle CopyButtonBounds(Rectangle bounds);
Rectangle OpenPathButtonBounds(Rectangle bounds);

void DrawCloseButton(Rectangle bounds, Font font);
void DrawCopyButton(Rectangle bounds, Font font, bool enabled = true);
void DrawOpenPathButton(Rectangle bounds, Font font, bool enabled = true);

// Soft green checkmark watermark centered in the text area (draw under title/meta).
void DrawCompletedCheckmarkBackdrop(Rectangle cardBounds);

// "m:ss" (or "h:mm:ss") over the preview; uses the main UI font.
void DrawPreviewElapsedOverlay(Font font, Rectangle previewBounds, double elapsedSeconds);
// 1-based index badge in the preview top-left; only the bottom-right corner is rounded.
void DrawPreviewIndexBadge(Font font, Rectangle previewBounds, int displayIndex);

void DrawWrappedText(
    Font font, const std::string& text, Vector2 position, float fontSize, float maxWidth, int maxLines, Color color);

void DrawStackPeekCard(Rectangle frontBounds, int layerIndex);

bool IsTitleTextHovered(Rectangle bounds,
                        Font font,
                        const std::string& title,
                        float titleStartY = kTitleStartY,
                        float fontSize = kTitleFontSize,
                        float maxWidthInset = kTitleWidthInset,
                        int maxLines = kTitleMaxLines);

float MeasureWrappedTextHeight(Font font, const std::string& text, float fontSize, float maxWidth, int maxLines);
} // namespace CardChrome
