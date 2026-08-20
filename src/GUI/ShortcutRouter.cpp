#include "ShortcutRouter.h"

namespace ShortcutRouter
{
bool CtrlDown()
{
    return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
}

bool AltDown()
{
    return IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
}

bool ShiftDown()
{
    return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
}

bool Pressed(const ShortcutChord& chord)
{
    if (!IsKeyPressed(chord.key))
    {
        return false;
    }

    return CtrlDown() == chord.ctrl && AltDown() == chord.alt && ShiftDown() == chord.shift;
}

bool PressedKey(int key)
{
    return IsKeyPressed(key);
}
} // namespace ShortcutRouter
