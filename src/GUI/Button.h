#pragma once

#include "raylib.h"

class Button
{
public:
    explicit Button(const char* text);

    bool Update(Rectangle bounds, bool enabled = true);
    void Draw(Rectangle bounds, Font font, bool enabled = true, bool focused = false) const;
    void DrawDanger(Rectangle bounds, Font font, bool focused = false) const;
    void SetText(const char* text);
    void ClearInteraction();

private:
    const char* text_;
    bool isHovered_ = false;
    bool isPressed_ = false;
};
