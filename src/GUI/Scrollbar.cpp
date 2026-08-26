#include "Scrollbar.h"

#include "MouseCursor.h"

#include <algorithm>

namespace
{
constexpr float kTrackInsetY = 4.0f;
constexpr float kThumbInset = 1.0f;
constexpr float kMinThumbHeight = 18.0f;
constexpr float kHitSlop = 6.0f;
} // namespace

Scrollbar::Metrics Scrollbar::Compute(Rectangle viewport, float scrollOffset, float maxScroll, bool expanded) const
{
    Metrics metrics{};
    if (maxScroll <= 0.0f || viewport.height <= 1.0f)
    {
        return metrics;
    }

    const float width = expanded ? kHoverWidth : kIdleWidth;
    metrics.track = {viewport.x + viewport.width - width - kEdgePad,
                     viewport.y + kTrackInsetY,
                     width,
                     std::max(0.0f, viewport.height - kTrackInsetY * 2.0f)};
    if (metrics.track.height <= 1.0f)
    {
        return {};
    }

    const float thumbRatio = viewport.height / (viewport.height + maxScroll);
    const float thumbHeight = std::max(kMinThumbHeight, metrics.track.height * thumbRatio);
    metrics.thumbTravel = std::max(0.0f, metrics.track.height - thumbHeight);
    const float t = maxScroll > 0.0f ? std::clamp(scrollOffset / maxScroll, 0.0f, 1.0f) : 0.0f;
    const float thumbY = metrics.track.y + t * metrics.thumbTravel;
    metrics.thumb = {
        metrics.track.x + kThumbInset, thumbY, std::max(1.0f, metrics.track.width - kThumbInset * 2.0f), thumbHeight};

    const float hitWidth = kHoverWidth + kHitSlop;
    metrics.hit = {viewport.x + viewport.width - hitWidth - kEdgePad, metrics.track.y, hitWidth, metrics.track.height};
    return metrics;
}

void Scrollbar::ApplyThumbY(float thumbY, const Metrics& metrics, float maxScroll, float& scrollOffset)
{
    if (metrics.thumbTravel <= 0.0f || maxScroll <= 0.0f)
    {
        scrollOffset = 0.0f;
        return;
    }
    const float clampedY = std::clamp(thumbY, metrics.track.y, metrics.track.y + metrics.thumbTravel);
    scrollOffset = ((clampedY - metrics.track.y) / metrics.thumbTravel) * maxScroll;
}

bool Scrollbar::Update(Rectangle viewport, float& scrollOffset, float maxScroll)
{
    if (maxScroll <= 0.0f || viewport.height <= 1.0f)
    {
        Reset();
        scrollOffset = 0.0f;
        return false;
    }

    scrollOffset = std::clamp(scrollOffset, 0.0f, maxScroll);
    const bool expanded = dragging_ || hovered_;
    Metrics metrics = Compute(viewport, scrollOffset, maxScroll, expanded);
    const Vector2 mouse = GetMousePosition();
    hovered_ = CheckCollisionPointRec(mouse, metrics.hit);

    if (hovered_ || dragging_)
    {
        UiCursor::RequestHand();
    }

    if (dragging_)
    {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            dragging_ = false;
            return true;
        }
        metrics = Compute(viewport, scrollOffset, maxScroll, true);
        ApplyThumbY(mouse.y - grabOffsetY_, metrics, maxScroll, scrollOffset);
        return true;
    }

    if (!hovered_ || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        return hovered_;
    }

    metrics = Compute(viewport, scrollOffset, maxScroll, true);
    if (CheckCollisionPointRec(mouse, metrics.thumb))
    {
        grabOffsetY_ = mouse.y - metrics.thumb.y;
    }
    else
    {
        grabOffsetY_ = metrics.thumb.height * 0.5f;
        ApplyThumbY(mouse.y - grabOffsetY_, metrics, maxScroll, scrollOffset);
        metrics = Compute(viewport, scrollOffset, maxScroll, true);
        grabOffsetY_ = mouse.y - metrics.thumb.y;
    }
    dragging_ = true;
    return true;
}

void Scrollbar::Draw(Rectangle viewport, float scrollOffset, float maxScroll) const
{
    if (maxScroll <= 0.0f || viewport.height <= 1.0f)
    {
        return;
    }

    const bool expanded = dragging_ || hovered_;
    const Metrics metrics = Compute(viewport, scrollOffset, maxScroll, expanded);
    if (metrics.track.height <= 1.0f)
    {
        return;
    }

    if (expanded)
    {
        UiCursor::RequestHand();
    }

    const Color trackColor = expanded ? Color{58, 70, 58, 255} : Color{48, 58, 48, 255};
    const Color thumbColor = expanded ? Color{132, 158, 132, 255} : Color{96, 118, 96, 255};
    DrawRectangleRounded(metrics.track, 1.0f, 4, trackColor);
    DrawRectangleRounded(metrics.thumb, 1.0f, 4, thumbColor);
}

bool Scrollbar::IsHovered() const
{
    return hovered_;
}

bool Scrollbar::IsDragging() const
{
    return dragging_;
}

bool Scrollbar::CapturesPointer() const
{
    return dragging_ || hovered_;
}

void Scrollbar::Reset()
{
    dragging_ = false;
    hovered_ = false;
    grabOffsetY_ = 0.0f;
}
