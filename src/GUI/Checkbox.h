#pragma once

#include "raylib.h"

class Checkbox {
public:
    void Update(Rectangle bounds, bool& value);
    void Draw(Rectangle bounds, Font font, const char* label, bool value, bool enabled = true) const;
};
