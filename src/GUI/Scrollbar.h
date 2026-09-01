#pragma once

#include "raylib.h"

class Scrollbar
{
public:
    static constexpr float kIdleWidth = 4.0f;
    static constexpr float kHoverWidth = 8.0f;
    static constexpr float kEdgePad = 2.0f;

    // Hover, track jump, and thumb drag. Mutates scrollOffset.
    // Returns true when the pointer is on the bar or a drag is active.
    bool Update(Rectangle viewport, float& scrollOffset, float maxScroll);
    void Draw(Rectangle viewport, float scrollOffset, float maxScroll) const;

    bool IsHovered() const;
    bool IsDragging() const;
    bool CapturesPointer() const;
    void Reset();

private:
    struct Metrics
    {
        Rectangle track{};
        Rectangle thumb{};
        Rectangle hit{};
        float thumbTravel = 0.0f;
    };

    Metrics Compute(Rectangle viewport, float scrollOffset, float maxScroll, bool expanded) const;
    static void ApplyThumbY(float thumbY, const Metrics& metrics, float maxScroll, float& scrollOffset);

    bool dragging_ = false;
    bool hovered_ = false;
    float grabOffsetY_ = 0.0f;
};
