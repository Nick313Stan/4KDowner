#pragma once

#include "raylib.h"

#include <string>
#include <vector>

class Dropdown {
public:
    explicit Dropdown(std::vector<std::string> items);

    bool Update(Rectangle bounds, int& selectedIndex);
    void Draw(Rectangle bounds, Font font, int selectedIndex, bool enabled = true) const;
    void DrawControl(Rectangle bounds, Font font, int selectedIndex, bool enabled = true) const;
    void DrawPopup(Rectangle bounds, Font font, int selectedIndex) const;
    void SetPopupLimitY(float minY, float maxY);
    void ClearPopupLimitY();
    void SetItems(std::vector<std::string> items);
    void Close();
    bool IsOpen() const;

    static bool IsInactiveItem(const std::string& label);
    static std::string GetQualityAlias(const std::string& label);

private:
    void DrawItemLabel(Font font, Rectangle bounds, const std::string& label, Color primary, Color secondary) const;
    Rectangle GetPopupBounds(Rectangle bounds) const;
    float GetMaxScroll(Rectangle bounds) const;

    std::vector<std::string> items_;
    bool isOpen_ = false;
    float scrollOffset_ = 0.0f;
    bool hasPopupLimitY_ = false;
    float popupMinY_ = 0.0f;
    float popupMaxY_ = 0.0f;
};
