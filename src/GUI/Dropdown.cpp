#include "Dropdown.h"

#include "DownloadFormatPredictor.h"
#include "Scrollbar.h"
#include "UiClip.h"

#include <algorithm>
#include <cmath>

Dropdown::Dropdown(std::vector<std::string> items)
    : items_(std::move(items))
{
}

bool Dropdown::Update(Rectangle bounds, int& selectedIndex, const Rectangle* hitClip)
{
    if (items_.empty())
    {
        return false;
    }

    const Vector2 mouse = GetMousePosition();
    const Rectangle popupBounds = GetPopupBounds(bounds);
    const float maxScroll = GetMaxScroll(bounds);

    const auto mouseInControl = [&]()
    {
        if (!CheckCollisionPointRec(mouse, bounds))
        {
            return false;
        }
        if (hitClip != nullptr && !CheckCollisionPointRec(mouse, *hitClip))
        {
            return false;
        }
        return true;
    };

    if (isOpen_)
    {
        if (maxScroll > 0.0f && (scrollbar_.IsDragging() || CheckCollisionPointRec(mouse, popupBounds)))
        {
            if (scrollbar_.Update(popupBounds, scrollOffset_, maxScroll))
            {
                return true;
            }
        }
        if (CheckCollisionPointRec(mouse, popupBounds))
        {
            const float wheel = GetMouseWheelMove();
            if (wheel != 0.0f)
            {
                scrollOffset_ = std::clamp(scrollOffset_ - wheel * bounds.height * 2.0f, 0.0f, maxScroll);
                return true;
            }
        }
    }

    // Closed control: Ctrl + wheel steps options without opening the popup (no wrap).
    if (!isOpen_ && mouseInControl() && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)))
    {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            const int itemCount = static_cast<int>(items_.size());
            if (itemCount <= 0)
            {
                return false;
            }

            const int start = std::clamp(selectedIndex, 0, itemCount - 1);
            const int step = wheel > 0.0f ? -1 : 1;
            for (int candidate = start + step; candidate >= 0 && candidate < itemCount; candidate += step)
            {
                if (IsInactiveItem(items_[static_cast<size_t>(candidate)]))
                {
                    continue;
                }
                selectedIndex = candidate;
                return true;
            }
            return true; // at end / only inactive beyond — consume wheel, keep selection
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        if (mouseInControl())
        {
            isOpen_ = !isOpen_;
            scrollOffset_ = std::clamp(scrollOffset_, 0.0f, maxScroll);
            return true;
        }

        if (isOpen_)
        {
            for (int index = 0; index < static_cast<int>(items_.size()); ++index)
            {
                const Rectangle itemBounds = {bounds.x,
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
                    Close();
                    return true;
                }
            }
            Close();
            return true;
        }
    }

    return false;
}

bool Dropdown::CapturesPoint(Rectangle bounds, Vector2 point) const
{
    if (!isOpen_)
    {
        return false;
    }
    return CheckCollisionPointRec(point, GetPopupBounds(bounds)) || CheckCollisionPointRec(point, bounds);
}

bool Dropdown::AnyCapturesPoint(const Slot* slots, int count, Vector2 point)
{
    for (int i = 0; i < count; ++i)
    {
        if (!slots[i].enabled || slots[i].dropdown == nullptr || !slots[i].dropdown->IsOpen())
        {
            continue;
        }
        if (slots[i].dropdown->CapturesPoint(slots[i].bounds, point))
        {
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

void Dropdown::DrawControl(Rectangle bounds, Font font, int selectedIndex, bool enabled, bool allowHover) const
{
    if (items_.empty())
    {
        return;
    }

    const int safeIndex = std::clamp(selectedIndex, 0, static_cast<int>(items_.size()) - 1);
    const bool hovered = enabled && allowHover && CheckCollisionPointRec(GetMousePosition(), bounds);
    const Color background =
        enabled ? (hovered ? Color{76, 92, 76, 255} : Color{54, 66, 54, 255}) : Color{24, 28, 24, 255};
    const Color text = enabled ? Color{224, 230, 224, 255} : Color{126, 136, 126, 255};
    const Color aliasColor = enabled ? Color{126, 142, 126, 255} : Color{96, 108, 96, 255};

    DrawRectangleRounded(bounds, 0.22f, 10, background);
    DrawItemLabel(font, bounds, items_[safeIndex], text, aliasColor);

    const float chevronCenterX = bounds.x + bounds.width - 14.0f;
    const float chevronCenterY = bounds.y + bounds.height * 0.5f;
    DrawLineEx({chevronCenterX - 4.0f, chevronCenterY - 2.0f}, {chevronCenterX, chevronCenterY + 2.0f}, 2.0f, text);
    DrawLineEx({chevronCenterX, chevronCenterY + 2.0f}, {chevronCenterX + 4.0f, chevronCenterY - 2.0f}, 2.0f, text);
}

void Dropdown::DrawBusyControl(Rectangle bounds, Font font, const std::string& label) const
{
    const Color background = Color{24, 28, 24, 255};
    const Color text = Color{126, 136, 126, 255};
    DrawRectangleRounded(bounds, 0.22f, 10, background);

    const Vector2 labelSize = MeasureTextEx(font, label.c_str(), 15.0f, 0.0f);
    DrawTextEx(font, label.c_str(), {bounds.x + 8.0f, bounds.y + 5.0f}, 15.0f, 0.0f, text);

    const Vector2 spinnerCenter = {bounds.x + 8.0f + labelSize.x + 14.0f, bounds.y + bounds.height * 0.5f};
    const double time = GetTime();
    constexpr int kSegments = 8;
    for (int index = 0; index < kSegments; ++index)
    {
        const float angle = static_cast<float>(time * 6.0 + index * (6.2831853 / kSegments));
        const float alpha = static_cast<float>(index + 1) / static_cast<float>(kSegments);
        const Vector2 start = {spinnerCenter.x + std::cos(angle) * 3.5f, spinnerCenter.y + std::sin(angle) * 3.5f};
        const Vector2 end = {spinnerCenter.x + std::cos(angle) * 6.0f, spinnerCenter.y + std::sin(angle) * 6.0f};
        DrawLineEx(start, end, 1.5f, Color{160, 178, 160, static_cast<unsigned char>(70 + alpha * 150)});
    }
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

    UiClip::Push(popupBounds);

    for (int index = 0; index < static_cast<int>(items_.size()); ++index)
    {
        const Rectangle itemBounds = {bounds.x,
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
            DrawLine(static_cast<int>(itemBounds.x + 8.0f),
                     static_cast<int>(itemBounds.y),
                     static_cast<int>(itemBounds.x + itemBounds.width - 8.0f),
                     static_cast<int>(itemBounds.y),
                     Color{58, 68, 58, 255});
        }
        if (hovered || selected)
        {
            const Rectangle highlightBounds = {
                itemBounds.x + 4.0f, itemBounds.y + 3.0f, itemBounds.width - 8.0f, itemBounds.height - 6.0f};
            DrawRectangleRounded(highlightBounds, 0.22f, 8, highlight);
        }
        const Color textColor = inactive ? Color{108, 118, 108, 255} : Color{232, 236, 232, 255};
        const Color aliasColor = inactive ? Color{88, 98, 88, 255} : Color{126, 142, 126, 255};
        DrawItemLabel(font, itemBounds, items_[index], textColor, aliasColor);
    }

    UiClip::Pop();

    if (maxScroll > 0.0f)
    {
        scrollbar_.Draw(popupBounds, scrollOffset_, maxScroll);
    }
}

void Dropdown::SetItems(std::vector<std::string> items)
{
    if (items_ == items)
    {
        return;
    }

    items_ = std::move(items);
    Close();
}

void Dropdown::Close()
{
    isOpen_ = false;
    scrollOffset_ = 0.0f;
    scrollbar_.Reset();
}

bool Dropdown::IsOpen() const
{
    return isOpen_;
}

void Dropdown::CloseOthers(Slot* slots, int count, const Dropdown* keep)
{
    for (int i = 0; i < count; ++i)
    {
        if (slots[i].dropdown != nullptr && slots[i].dropdown != keep)
        {
            slots[i].dropdown->Close();
        }
    }
}

void Dropdown::CloseDisabled(Slot* slots, int count)
{
    for (int i = 0; i < count; ++i)
    {
        if (!slots[i].enabled && slots[i].dropdown != nullptr)
        {
            slots[i].dropdown->Close();
        }
    }
}

bool Dropdown::AnyOpen(const Slot* slots, int count)
{
    for (int i = 0; i < count; ++i)
    {
        if (slots[i].enabled && slots[i].dropdown != nullptr && slots[i].dropdown->IsOpen())
        {
            return true;
        }
    }
    return false;
}

int Dropdown::UpdateOpenPopups(Slot* slots, int count)
{
    CloseDisabled(slots, count);
    for (int i = 0; i < count; ++i)
    {
        Slot& slot = slots[i];
        if (!slot.enabled || slot.dropdown == nullptr || slot.selectedIndex == nullptr)
        {
            continue;
        }
        if (!slot.dropdown->IsOpen())
        {
            continue;
        }
        if (slot.dropdown->Update(slot.bounds, *slot.selectedIndex, slot.hitClip))
        {
            CloseOthers(slots, count, slot.dropdown);
            return i;
        }
    }
    return -1;
}

int Dropdown::UpdateClosedControls(Slot* slots, int count)
{
    CloseDisabled(slots, count);
    for (int i = 0; i < count; ++i)
    {
        Slot& slot = slots[i];
        if (!slot.enabled || slot.dropdown == nullptr || slot.selectedIndex == nullptr)
        {
            continue;
        }
        if (slot.dropdown->IsOpen())
        {
            continue;
        }
        if (slot.dropdown->Update(slot.bounds, *slot.selectedIndex, slot.hitClip))
        {
            CloseOthers(slots, count, slot.dropdown);
            return i;
        }
    }
    return -1;
}

bool Dropdown::IsInactiveItem(const std::string& label)
{
    return IsInactiveFormatItem(label);
}

std::string Dropdown::GetQualityAlias(const std::string& label)
{
    // Only quality rows like "4320p" — never format rows ("WEBM", "MP4 (Unavailable)").
    if (label.empty() || label.front() < '0' || label.front() > '9')
    {
        return {};
    }

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

    if (height >= 4320)
    {
        return "8K";
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

void Dropdown::DrawItemLabel(
    Font font, Rectangle bounds, const std::string& label, Color primary, Color secondary) const
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
    const float limitMaxY = hasPopupLimitY_ ? popupMaxY_ : static_cast<float>(GetRenderHeight()) - 8.0f;
    const float availableBelow = limitMaxY - (bounds.y + bounds.height);
    const float availableAbove = bounds.y - limitMinY;
    const float maxPopupHeight = bounds.height * 7.0f;
    const bool openAbove = availableBelow < std::min(contentHeight, maxPopupHeight) && availableAbove > availableBelow;
    const float availableSpace = openAbove ? availableAbove : availableBelow;
    const float popupHeight = std::max(bounds.height, std::min({contentHeight, maxPopupHeight, availableSpace}));

    return {bounds.x, openAbove ? bounds.y - popupHeight : bounds.y + bounds.height, bounds.width, popupHeight};
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
