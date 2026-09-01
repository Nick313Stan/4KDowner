#pragma once

namespace TaskbarProgress
{
// progress01 in [0, 1] shows normal progress; progress01 < 0 clears the taskbar state.
void SetProgress(float progress01);
} // namespace TaskbarProgress
