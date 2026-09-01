#pragma once

#include "raylib.h"

namespace UiCursor
{
inline int frameCursor = MOUSE_CURSOR_ARROW;
inline bool suppressHoverUntilMouseMove = false;

inline void UpdateHoverSuppression()
{
    if (suppressHoverUntilMouseMove)
    {
        const Vector2 delta = GetMouseDelta();
        if (delta.x != 0.0f || delta.y != 0.0f)
        {
            suppressHoverUntilMouseMove = false;
        }
    }
}

inline void NotifyKeyboardNavigation()
{
    suppressHoverUntilMouseMove = true;
}

inline bool PointerHoverActive()
{
    return !suppressHoverUntilMouseMove;
}

inline bool IsMouseOverRect(Rectangle bounds)
{
    return PointerHoverActive() && CheckCollisionPointRec(GetMousePosition(), bounds);
}

inline void BeginFrame()
{
    UpdateHoverSuppression();
    frameCursor = MOUSE_CURSOR_ARROW;
}

inline void RequestHand()
{
    if (PointerHoverActive())
    {
        frameCursor = MOUSE_CURSOR_POINTING_HAND;
    }
}

inline void ApplyFrame()
{
    SetMouseCursor(frameCursor);
}
} // namespace UiCursor
