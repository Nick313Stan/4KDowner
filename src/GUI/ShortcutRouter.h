#pragma once

#include "raylib.h"

enum class UiHoverTarget
{
    None,
    LinkCard,
    ConverterCard,
    DownloadFoldout,
    AutoConvertFoldout,
    Any
};

struct ShortcutChord
{
    int key = 0;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
};

struct HoverContext
{
    UiHoverTarget target = UiHoverTarget::None;
    int index = -1;
};

namespace ShortcutRouter
{
bool CtrlDown();
bool AltDown();
bool ShiftDown();
bool Pressed(const ShortcutChord& chord);
bool PressedKey(int key);
} // namespace ShortcutRouter
