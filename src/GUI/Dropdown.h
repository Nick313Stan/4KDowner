#pragma once

#include "raylib.h"

#include <string>
#include <vector>

class Dropdown
{
public:
    explicit Dropdown(std::vector<std::string> items);

    bool Update(Rectangle bounds, int& selectedIndex, const Rectangle* hitClip = nullptr);
    void Draw(Rectangle bounds, Font font, int selectedIndex, bool enabled = true) const;
    void DrawControl(Rectangle bounds, Font font, int selectedIndex, bool enabled = true, bool allowHover = true) const;
    void DrawPopup(Rectangle bounds, Font font, int selectedIndex) const;
    void SetPopupLimitY(float minY, float maxY);
    void ClearPopupLimitY();
    void SetItems(std::vector<std::string> items);
    void Close();
    bool IsOpen() const;
    Rectangle GetPopupBounds(Rectangle bounds) const;
    bool CapturesPoint(Rectangle bounds, Vector2 point) const;

    // One entry in a z-ordered dropdown stack (index 0 = topmost / drawn last).
    struct Slot
    {
        Dropdown* dropdown = nullptr;
        Rectangle bounds{};
        int* selectedIndex = nullptr;
        bool enabled = true;
        const Rectangle* hitClip = nullptr; // if set, control clicks require mouse inside clip
    };

    // Close slots with enabled == false.
    static void CloseDisabled(Slot* slots, int count);
    static bool AnyOpen(const Slot* slots, int count);
    static bool AnyCapturesPoint(const Slot* slots, int count, Vector2 point);
    // Hit-test currently open popups, top → bottom. Returns consuming slot index or -1.
    static int UpdateOpenPopups(Slot* slots, int count);
    // Hit-test closed controls, top → bottom. Returns consuming slot index or -1.
    static int UpdateClosedControls(Slot* slots, int count);

    static bool IsInactiveItem(const std::string& label);
    static std::string GetQualityAlias(const std::string& label);

private:
    void DrawItemLabel(Font font, Rectangle bounds, const std::string& label, Color primary, Color secondary) const;
    float GetMaxScroll(Rectangle bounds) const;
    static void CloseOthers(Slot* slots, int count, const Dropdown* keep);

    std::vector<std::string> items_;
    bool isOpen_ = false;
    float scrollOffset_ = 0.0f;
    bool hasPopupLimitY_ = false;
    float popupMinY_ = 0.0f;
    float popupMaxY_ = 0.0f;
};
