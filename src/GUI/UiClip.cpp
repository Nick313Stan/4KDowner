#include "UiClip.h"

#include <algorithm>
#include <vector>

namespace
{
std::vector<Rectangle>& Stack()
{
    static std::vector<Rectangle> stack;
    return stack;
}

Rectangle Intersect(Rectangle a, Rectangle b)
{
    const float left = std::max(a.x, b.x);
    const float top = std::max(a.y, b.y);
    const float right = std::min(a.x + a.width, b.x + b.width);
    const float bottom = std::min(a.y + a.height, b.y + b.height);
    return {left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
}

void Apply(Rectangle clip)
{
    if (clip.width <= 0.0f || clip.height <= 0.0f)
    {
        // Empty intersection: clip everything out.
        BeginScissorMode(0, 0, 0, 0);
        return;
    }

    BeginScissorMode(static_cast<int>(clip.x),
                     static_cast<int>(clip.y),
                     static_cast<int>(clip.width),
                     static_cast<int>(clip.height));
}
} // namespace

namespace UiClip
{
void Push(Rectangle bounds)
{
    Rectangle clip = bounds;
    if (!Stack().empty())
    {
        clip = Intersect(Stack().back(), bounds);
    }
    Stack().push_back(clip);
    Apply(clip);
}

void Pop()
{
    if (Stack().empty())
    {
        return;
    }

    Stack().pop_back();
    if (Stack().empty())
    {
        EndScissorMode();
        return;
    }
    Apply(Stack().back());
}

Rectangle Current()
{
    if (Stack().empty())
    {
        return {0.0f, 0.0f, static_cast<float>(GetRenderWidth()), static_cast<float>(GetRenderHeight())};
    }
    return Stack().back();
}

bool Active()
{
    return !Stack().empty();
}
} // namespace UiClip
