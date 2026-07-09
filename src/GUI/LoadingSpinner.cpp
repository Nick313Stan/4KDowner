#include "LoadingSpinner.h"

#include <cmath>

namespace {
constexpr int kSegmentCount = 12;
constexpr float kPi = 3.14159265358979323846f;

Color FadeSegment(int segmentIndex, int activeIndex)
{
    int distance = activeIndex - segmentIndex;
    if (distance < 0)
    {
        distance += kSegmentCount;
    }

    const unsigned char value = static_cast<unsigned char>(235 - distance * 15);
    return {value, value, value, 255};
}
}

void LoadingSpinner::Draw(Vector2 center, float radius) const
{
    const float segmentLength = radius * 0.42f;
    const float thickness = radius * 0.13f;
    const float innerRadius = radius - segmentLength;
    const int activeIndex = static_cast<int>(GetTime() * 12.0) % kSegmentCount;

    for (int index = 0; index < kSegmentCount; ++index)
    {
        const float angle = (-90.0f + static_cast<float>(index) * (360.0f / kSegmentCount)) * (kPi / 180.0f);
        const Vector2 direction = {std::cos(angle), std::sin(angle)};
        const Vector2 start = {
            center.x + direction.x * innerRadius,
            center.y + direction.y * innerRadius};
        const Vector2 end = {
            center.x + direction.x * radius,
            center.y + direction.y * radius};
        const Color color = FadeSegment(index, activeIndex);

        DrawLineEx(start, end, thickness, color);
        DrawCircleV(start, thickness * 0.5f, color);
        DrawCircleV(end, thickness * 0.5f, color);
    }
}
