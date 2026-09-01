#pragma once

#include "raylib.h"

// Nested rounded/circle clip stack via OpenGL stencil (clipping mask).
// Works alongside UiClip scissor; flush raylib batch before stencil ops internally.
namespace UiRoundedClip
{
enum class Shape
{
    RoundedRect,
    Circle,
};

void Push(Rectangle bounds, float roundness, int segments, Shape shape = Shape::RoundedRect);
void Pop();
bool Active();
} // namespace UiRoundedClip
