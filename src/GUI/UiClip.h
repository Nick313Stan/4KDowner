#pragma once

#include "raylib.h"

// Nested scissor stack: Push intersects with the current clip, Pop restores parent.
// Prefer this over raw BeginScissorMode/EndScissorMode so inner widgets cannot
// accidentally clear an outer list/panel clip.
namespace UiClip
{
void Push(Rectangle bounds);
void Pop();
Rectangle Current();
bool Active();
} // namespace UiClip
