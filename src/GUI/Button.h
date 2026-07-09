#pragma once

#include "raylib.h"

class Button {
public:
    explicit Button(const char* text);

    bool Update(Rectangle bounds, bool enabled = true);
    void Draw(Rectangle bounds, Font font, bool enabled = true) const;
    void DrawDanger(Rectangle bounds, Font font) const;
    void SetText(const char* text);

private:
    const char* text_;
    bool isHovered_ = false;
    bool isPressed_ = false;
};
