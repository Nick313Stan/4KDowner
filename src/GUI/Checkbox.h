#pragma once

#include "raylib.h"

// Shared across a checkbox group (e.g. Container/Video/Audio) so drag-painting
// applies the same on/off state started on the first pressed box.
struct CheckboxPaintSession
{
    bool active = false;
    bool value = false;
};

class Checkbox
{
public:
    // When paint != nullptr, press toggles and starts a drag-paint; neighbors under
    // the cursor while LMB is held take the same value. Without paint, click must
    // press+release on the same control.
    void Update(Rectangle bounds, bool& value, CheckboxPaintSession* paint = nullptr);
    void Draw(Rectangle bounds,
              Font font,
              const char* label,
              bool value,
              bool enabled = true,
              float labelMaxWidth = -1.0f) const;

private:
    bool armed_ = false;
};
