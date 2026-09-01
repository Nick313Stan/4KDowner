#include "UiRoundedClip.h"

#include "external/glad.h"
#include "rlgl.h"

#include <vector>

namespace
{
struct ClipEntry
{
    Rectangle bounds{};
    float roundness = 0.0f;
    int segments = 8;
    UiRoundedClip::Shape shape = UiRoundedClip::Shape::RoundedRect;
    int depth = 0;
};

std::vector<ClipEntry>& Stack()
{
    static std::vector<ClipEntry> stack;
    return stack;
}

bool& StencilEnabled()
{
    static bool enabled = false;
    return enabled;
}

void FlushBatch()
{
    rlDrawRenderBatchActive();
}

void DrawMaskShape(Rectangle bounds, float roundness, int segments, UiRoundedClip::Shape shape)
{
    if (shape == UiRoundedClip::Shape::Circle)
    {
        const float radius = bounds.width * 0.5f;
        const Vector2 center = {bounds.x + radius, bounds.y + bounds.height * 0.5f};
        DrawCircleV(center, radius, WHITE);
        return;
    }

    if (roundness <= 0.0f)
    {
        DrawRectangleRec(bounds, WHITE);
        return;
    }

    DrawRectangleRounded(bounds, roundness, segments, WHITE);
}

void ApplyContentStencilTest(int depth)
{
    glStencilFunc(GL_EQUAL, depth, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilMask(0x00);
}
} // namespace

namespace UiRoundedClip
{
void Push(Rectangle bounds, float roundness, int segments, Shape shape)
{
    FlushBatch();

    const int parentDepth = Stack().empty() ? 0 : Stack().back().depth;
    const int newDepth = parentDepth + 1;

    if (!StencilEnabled())
    {
        glEnable(GL_STENCIL_TEST);
        StencilEnabled() = true;
    }

    glStencilMask(0xFF);
    rlColorMask(false, false, false, false);
    if (parentDepth == 0)
    {
        glStencilFunc(GL_ALWAYS, 0, 0xFF);
    }
    else
    {
        glStencilFunc(GL_EQUAL, parentDepth, 0xFF);
    }
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

    DrawMaskShape(bounds, roundness, segments, shape);
    FlushBatch();

    rlColorMask(true, true, true, true);
    ApplyContentStencilTest(newDepth);

    Stack().push_back({bounds, roundness, segments, shape, newDepth});
}

void Pop()
{
    if (Stack().empty())
    {
        return;
    }

    const ClipEntry entry = Stack().back();
    Stack().pop_back();

    FlushBatch();

    glStencilMask(0xFF);
    rlColorMask(false, false, false, false);
    glStencilFunc(GL_EQUAL, entry.depth, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);

    DrawMaskShape(entry.bounds, entry.roundness, entry.segments, entry.shape);
    FlushBatch();

    rlColorMask(true, true, true, true);

    if (Stack().empty())
    {
        glDisable(GL_STENCIL_TEST);
        StencilEnabled() = false;
        glStencilMask(0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        return;
    }

    ApplyContentStencilTest(Stack().back().depth);
}

bool Active()
{
    return !Stack().empty();
}
} // namespace UiRoundedClip
