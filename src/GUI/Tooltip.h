#pragma once

#include "raylib.h"

class Tooltip
{
public:
    // Queues a tip for Flush (safe inside scissor / under other UI).
    static void Draw(Font font, Rectangle anchor, const char* text);
    static void DrawIfHovered(Font font, Rectangle anchor, const char* text);
    // Draw all queued tips on top, then clear.
    static void Flush();
    // Drop queued tips without drawing (e.g. when a modal covers the UI).
    static void Clear();

private:
    static void DrawImmediate(Font font, Rectangle anchor, const char* text);
};
