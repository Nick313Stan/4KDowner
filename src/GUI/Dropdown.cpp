#include "Dropdown.h"

#include <algorithm>

Dropdown::Dropdown(std::vector<std::string> items)
    : items_(std::move(items))
{
}

bool Dropdown::Update(Rectangle bounds, int& selectedIndex)
{
    if (items_.empty())
    {
        return false;
    }

    const Vector2 mouse = GetMousePosition();
    const Rectangle popupBounds = GetPopupBounds(bounds);
    const float maxScroll = GetMaxScroll(bounds);

    if (isOpen_ && CheckCollisionPointRec(mouse, popupBounds))
    {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            scrollOffset_ = std::clamp(scrollOffset_ - wheel * bounds.height * 2.0f, 0.0f, maxScroll);
            return true;
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        if (CheckCollisionPointRec(mouse, bounds))
        {
            isOpen_ = !isOpen_;
            scrollOffset_ = std::clamp(scrollOffset_, 0.0f, maxScroll);
            return true;
        }

        if (isOpen_)
        {
            for (int index = 0; index < static_cast<int>(items_.size()); ++index)
            {
                const Rectangle itemBounds = {
                    bounds.x,
                    popupBounds.y + bounds.height * static_cast<float>(index) - scrollOffset_,
                    bounds.width,
                    bounds.height};
                if (CheckCollisionPointRec(mouse, popupBounds) && CheckCollisionPointRec(mouse, itemBounds))
                {
                    if (IsInactiveItem(items_[index]))
                    {
                        return true;
                    }

                    selectedIndex = index;
                    isOpen_ = false;
                    scrollOffset_ = 0.0f;
                    return true;
                }
            }
            isOpen_ = false;
            scrollOffset_ = 0.0f;
            return true;
        }
    }

    return false;
}

void Dropdown::Draw(Rectangle bounds, Font font, int selectedIndex, bool enabled) const
{
    DrawControl(bounds, font, selectedIndex, enabled);
    DrawPopup(bounds, font, selectedIndex);
}

void Dropdown::DrawControl(Rectangle bounds, Font font, int selectedIndex, bool enabled) const
{
    if (items_.empty())
    {
        return;
    }

    const int safeIndex = std::clamp(selectedIndex, 0, static_cast<int>(items_.size()) - 1);
    const bool hovered = enabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    const Color background = enabled ? (hovered ? Color{76, 92, 76, 255} : Color{54, 66, 54, 255}) : Color{24, 28, 24, 255};
    const Color text = enabled ? Color{224, 230, 224, 255} : Color{126, 136, 126, 255};
    const Color aliasColor = enabled ? Color{126, 142, 126, 255} : Color{96, 108, 96, 255};

    DrawRectangleRounded(bounds, 0.22f, 10, background);
    DrawItemLabel(font, bounds, items_[safeIndex], text, aliasColor);

    const float chevronCenterX = bounds.x + bounds.width - 14.0f;
    const float chevronCenterY = bounds.y + bounds.height * 0.5f;
    DrawLineEx({chevronCenterX - 4.0f, chevronCenterY - 2.0f}, {chevronCenterX, chevronCenterY + 2.0f}, 2.0f, text);
    DrawLineEx({chevronCenterX, chevronCenterY + 2.0f}, {chevronCenterX + 4.0f, chevronCenterY - 2.0f}, 2.0f, text);
}

void Dropdown::DrawPopup(Rectangle bounds, Font font, int selectedIndex) const
{
    if (!isOpen_ || items_.empty())
    {
        return;
    }

    const int safeIndex = std::clamp(selectedIndex, 0, static_cast<int>(items_.size()) - 1);
    const Rectangle popupBounds = GetPopupBounds(bounds);
    const float maxScroll = GetMaxScroll(bounds);
    const float roundness = 0.18f;

    DrawRectangleRounded(popupBounds, roundness, 8, Color{42, 50, 42, 255});

    BeginScissorMode(
        static_cast<int>(popupBounds.x),
        static_cast<int>(popupBounds.y),
        static_cast<int>(popupBounds.width),
        static_cast<int>(popupBounds.height));

    for (int index = 0; index < static_cast<int>(items_.size()); ++index)
    {
        const Rectangle itemBounds = {
            bounds.x,
            popupBounds.y + bounds.height * static_cast<float>(index) - scrollOffset_,
            bounds.width,
            bounds.height};
        if (itemBounds.y + itemBounds.height < popupBounds.y || itemBounds.y > popupBounds.y + popupBounds.height)
        {
            continue;
        }

        const bool hovered = !IsInactiveItem(items_[index]) && CheckCollisionPointRec(GetMousePosition(), itemBounds);
        const bool inactive = IsInactiveItem(items_[index]);
        const bool selected = !inactive && index == safeIndex;
        const Color itemBackground = Color{42, 50, 42, 255};
        const Color highlight = selected ? Color{86, 126, 86, 255} : Color{68, 84, 68, 255};
        DrawRectangleRec(itemBounds, itemBackground);
        if (index > 0)
        {
            DrawLine(
                static_cast<int>(itemBounds.x + 8.0f),
                static_cast<int>(itemBounds.y),
                static_cast<int>(itemBounds.x + itemBounds.width - 8.0f),
                static_cast<int>(itemBounds.y),
                Color{58, 68, 58, 255});
        }
        if (hovered || selected)
        {
            const Rectangle highlightBounds = {
                itemBounds.x + 4.0f,
                itemBounds.y + 3.0f,
                itemBounds.width - 8.0f,
                itemBounds.height - 6.0f};
            DrawRectangleRounded(highlightBounds, 0.22f, 8, highlight);
        }
        const Color textColor = inactive ? Color{108, 118, 108, 255} : Color{232, 236, 232, 255};
        const Color aliasColor = inactive ? Color{88, 98, 88, 255} : Color{126, 142, 126, 255};
        DrawItemLabel(font, itemBounds, items_[index], textColor, aliasColor);
    }

    EndScissorMode();

    if (maxScroll > 0.0f)
    {
        const float trackHeight = popupBounds.height - 8.0f;
        const float contentHeight = bounds.height * static_cast<float>(items_.size());
        const float thumbHeight = std::max(18.0f, trackHeight * popupBounds.height / contentHeight);
        const float thumbTravel = trackHeight - thumbHeight;
        const float thumbY = popupBounds.y + 4.0f + (scrollOffset_ / maxScroll) * thumbTravel;
        const Rectangle thumbBounds = {popupBounds.x + popupBounds.width - 6.0f, thumbY, 3.0f, thumbHeight};
        DrawRectangleRounded(thumbBounds, 0.8f, 6, Color{118, 142, 118, 210});
    }
}

void Dropdown::SetItems(std::vector<std::string> items)
{
    if (items_ == items)
    {
        return;
    }

    items_ = std::move(items);
    isOpen_ = false;
    scrollOffset_ = 0.0f;
}

void Dropdown::Close()
{
    isOpen_ = false;
    scrollOffset_ = 0.0f;
}

bool Dropdown::IsOpen() const
{
    return isOpen_;
}

bool Dropdown::IsInactiveItem(const std::string& label)
{
    const std::string suffix = " (Current)";
    return label.size() >= suffix.size() &&
        label.compare(label.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string Dropdown::GetQualityAlias(const std::string& label)
{
    int height = 0;
    for (const char c : label)
    {
        if (c >= '0' && c <= '9')
        {
            height = height * 10 + (c - '0');
        }
        else if (height > 0)
        {
            break;
        }
    }

    if (height >= 2160)
    {
        return "4K";
    }
    if (height >= 1440)
    {
        return "2K";
    }
    if (height >= 1080)
    {
        return "FullHD";
    }
    if (height >= 720)
    {
        return "HD";
    }
    return {};
}

void Dropdown::DrawItemLabel(Font font, Rectangle bounds, const std::string& label, Color primary, Color secondary) const
{
    const Vector2 primarySize = MeasureTextEx(font, label.c_str(), 15.0f, 0.0f);
    DrawTextEx(font, label.c_str(), {bounds.x + 8.0f, bounds.y + 5.0f}, 15.0f, 0.0f, primary);

    const std::string alias = GetQualityAlias(label);
    if (alias.empty())
    {
        return;
    }

    const float aliasX = bounds.x + 8.0f + primarySize.x + 8.0f;
    const float maxAliasX = bounds.x + bounds.width - 28.0f;
    if (aliasX + MeasureTextEx(font, alias.c_str(), 15.0f, 0.0f).x <= maxAliasX)
    {
        DrawTextEx(font, alias.c_str(), {aliasX, bounds.y + 5.0f}, 15.0f, 0.0f, secondary);
    }
}

Rectangle Dropdown::GetPopupBounds(Rectangle bounds) const
{
    const float contentHeight = bounds.height * static_cast<float>(items_.size());
    const float limitMinY = hasPopupLimitY_ ? popupMinY_ : 8.0f;
    const float limitMaxY = hasPopupLimitY_ ? popupMaxY_ : static_cast<float>(GetScreenHeight()) - 8.0f;
    const float availableBelow = limitMaxY - (bounds.y + bounds.height);
    const float availableAbove = bounds.y - limitMinY;
    const float maxPopupHeight = bounds.height * 7.0f;
    const bool openAbove = availableBelow < std::min(contentHeight, maxPopupHeight) && availableAbove > availableBelow;
    const float availableSpace = openAbove ? availableAbove : availableBelow;
    const float popupHeight = std::max(bounds.height, std::min({contentHeight, maxPopupHeight, availableSpace}));

    return {
        bounds.x,
        openAbove ? bounds.y - popupHeight : bounds.y + bounds.height,
        bounds.width,
        popupHeight};
}

float Dropdown::GetMaxScroll(Rectangle bounds) const
{
    const float contentHeight = bounds.height * static_cast<float>(items_.size());
    const float popupHeight = GetPopupBounds(bounds).height;
    return std::max(0.0f, contentHeight - popupHeight);
}

void Dropdown::SetPopupLimitY(float minY, float maxY)
{
    hasPopupLimitY_ = true;
    popupMinY_ = minY;
    popupMaxY_ = maxY;
}

void Dropdown::ClearPopupLimitY()
{
    hasPopupLimitY_ = false;
    popupMinY_ = 0.0f;
    popupMaxY_ = 0.0f;
}
