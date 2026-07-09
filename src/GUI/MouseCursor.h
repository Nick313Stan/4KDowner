#pragma once

#include "raylib.h"

namespace UiCursor {
inline int frameCursor = MOUSE_CURSOR_ARROW;

inline void BeginFrame()
{
    frameCursor = MOUSE_CURSOR_ARROW;
}

inline void RequestHand()
{
    frameCursor = MOUSE_CURSOR_POINTING_HAND;
}

inline void ApplyFrame()
{
    SetMouseCursor(frameCursor);
}
}
