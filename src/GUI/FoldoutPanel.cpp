#include "FoldoutPanel.h"

#include <algorithm>
#include <string>

namespace
{
std::string TruncateLabelToWidth(Font font, const char* text, float fontSize, float maxWidth)
{
    if (text == nullptr || text[0] == '\0')
    {
        return {};
    }
    if (maxWidth <= 0.0f)
    {
        return "...";
    }
    if (MeasureTextEx(font, text, fontSize, 0.0f).x <= maxWidth)
    {
        return text;
    }

    const std::string ellipsis = "...";
    if (MeasureTextEx(font, ellipsis.c_str(), fontSize, 0.0f).x > maxWidth)
    {
        return ellipsis;
    }

    const std::string full = text;
    size_t low = 0;
    size_t high = full.size();
    while (low < high)
    {
        const size_t mid = (low + high + 1) / 2;
        const std::string candidate = full.substr(0, mid) + ellipsis;
        if (MeasureTextEx(font, candidate.c_str(), fontSize, 0.0f).x <= maxWidth)
        {
            low = mid;
        }
        else
        {
            high = mid - 1;
        }
    }
    return low == 0 ? ellipsis : full.substr(0, low) + ellipsis;
}
} // namespace

FoldoutPanel::FoldoutPanel(const char* title, bool expanded, bool headerCheckboxSlot, bool collapsible)
    : title_(title),
      expanded_(collapsible ? expanded : true),
      headerCheckboxSlot_(headerCheckboxSlot),
      collapsible_(collapsible)
{
}

bool FoldoutPanel::Update(Rectangle headerBounds, bool enabled)
{
    if (!collapsible_ || !enabled)
    {
        isHovered_ = false;
        return false;
    }

    const Rectangle header = HeaderBounds(headerBounds);
    isHovered_ = CheckCollisionPointRec(GetMousePosition(), header);
    if (headerCheckboxSlot_ && CheckCollisionPointRec(GetMousePosition(), HeaderCheckboxBounds(headerBounds)))
    {
        return false;
    }

    if (!isHovered_ || !IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        return false;
    }

    expanded_ = !expanded_;
    return true;
}

void FoldoutPanel::Draw(Rectangle panelBounds, Font font, bool enabled) const
{
    // Nested panel chrome (Blender-like): slightly lighter fill + thin outline.
    const Color fill = !enabled ? Color{30, 36, 30, 255}
                                : (!collapsible_ ? Color{36, 44, 36, 255}
                                                 : (isHovered_ ? Color{44, 52, 44, 255} : Color{36, 44, 36, 255}));
    const Color border = enabled ? Color{78, 94, 78, 255} : Color{54, 64, 54, 255};
    const Color titleColor = enabled ? Color{220, 228, 220, 255} : Color{120, 130, 120, 255};
    const Color chevronColor = enabled ? Color{168, 184, 168, 255} : Color{96, 108, 96, 255};

    const float minSide = panelBounds.width < panelBounds.height ? panelBounds.width : panelBounds.height;
    const float roundness = minSide > 0.0f ? (8.0f / minSide) : 0.1f;
    DrawRectangleRounded(panelBounds, roundness, 8, fill);
    DrawRectangleRoundedLines(panelBounds, roundness, 8, border);

    const Rectangle header = HeaderBounds(panelBounds);
    if (collapsible_)
    {
        const float chevronX = header.x + 11.0f;
        const float chevronY = header.y + header.height * 0.5f;
        DrawChevron(chevronX, chevronY, chevronColor);
    }
    constexpr float kTitleFontSize = 15.0f;
    const float titleX = header.x + TitleOffsetX();
    const float titleMaxWidth = std::max(4.0f, header.x + header.width - titleX - SidePadding());
    const std::string title = TruncateLabelToWidth(font, title_, kTitleFontSize, titleMaxWidth);
    DrawTextEx(font, title.c_str(), {titleX, header.y + 4.0f}, kTitleFontSize, 0.0f, titleColor);
}

bool FoldoutPanel::IsExpanded() const
{
    return expanded_;
}

void FoldoutPanel::SetExpanded(bool expanded)
{
    if (!collapsible_)
    {
        expanded_ = true;
        return;
    }
    expanded_ = expanded;
}

void FoldoutPanel::ToggleExpanded()
{
    if (!collapsible_)
    {
        return;
    }
    expanded_ = !expanded_;
}

bool FoldoutPanel::IsHovered() const
{
    return isHovered_;
}

bool FoldoutPanel::IsCollapsible() const
{
    return collapsible_;
}

void FoldoutPanel::SetHoverToggleKey(int key)
{
    hoverToggleKey_ = key;
}

int FoldoutPanel::HoverToggleKey() const
{
    return hoverToggleKey_;
}

void FoldoutPanel::SyncPanelBounds(Rectangle panelBounds)
{
    panelBounds_ = panelBounds;
    hasPanelBounds_ = true;
}

bool FoldoutPanel::ContainsMouse() const
{
    return hasPanelBounds_ && CheckCollisionPointRec(GetMousePosition(), panelBounds_);
}

bool FoldoutPanel::TryHoverToggleShortcut()
{
    if (!collapsible_ || hoverToggleKey_ == 0 || !hasPanelBounds_)
    {
        return false;
    }

    const Rectangle header = HeaderBounds(panelBounds_);
    if (!CheckCollisionPointRec(GetMousePosition(), header))
    {
        return false;
    }

    if (!IsKeyPressed(hoverToggleKey_))
    {
        return false;
    }

    // Letter/section shortcuts are unmodified only (PathField / Ctrl chords stay free).
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_ALT) ||
        IsKeyDown(KEY_RIGHT_ALT) || IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
    {
        return false;
    }

    expanded_ = !expanded_;
    return true;
}

bool FoldoutPanel::HasHeaderCheckboxSlot() const
{
    return headerCheckboxSlot_;
}

float FoldoutPanel::HeaderHeight()
{
    return 22.0f;
}

float FoldoutPanel::ContentTopPadding()
{
    return 6.0f;
}

float FoldoutPanel::ContentBottomPadding()
{
    return 8.0f;
}

float FoldoutPanel::SidePadding()
{
    return 8.0f;
}

float FoldoutPanel::HeaderCheckboxSize()
{
    return 16.0f;
}

Rectangle FoldoutPanel::HeaderBounds(Rectangle panelBounds) const
{
    return {panelBounds.x, panelBounds.y, panelBounds.width, HeaderHeight()};
}

Rectangle FoldoutPanel::HeaderCheckboxBounds(Rectangle panelBounds) const
{
    if (!headerCheckboxSlot_)
    {
        return {};
    }

    const Rectangle header = HeaderBounds(panelBounds);
    const float size = HeaderCheckboxSize();
    return {header.x + 22.0f, header.y + (header.height - size) * 0.5f, size, size};
}

Rectangle FoldoutPanel::HeaderClickBounds(Rectangle panelBounds) const
{
    const Rectangle header = HeaderBounds(panelBounds);
    if (!headerCheckboxSlot_)
    {
        return header;
    }

    const Rectangle checkbox = HeaderCheckboxBounds(panelBounds);
    // Clickable expand area is everything except the checkbox (with a small pad).
    const float splitX = checkbox.x + checkbox.width + 4.0f;
    return {splitX, header.y, std::max(0.0f, header.x + header.width - splitX), header.height};
}

Rectangle FoldoutPanel::ContentArea(Rectangle panelBounds) const
{
    const float top = panelBounds.y + HeaderHeight() + ContentTopPadding();
    const float bottomPad = ContentBottomPadding();
    const float height = panelBounds.height - HeaderHeight() - ContentTopPadding() - bottomPad;
    return {
        panelBounds.x + SidePadding(), top, panelBounds.width - SidePadding() * 2.0f, height > 0.0f ? height : 0.0f};
}

float FoldoutPanel::TitleOffsetX() const
{
    if (!collapsible_)
    {
        return 12.0f;
    }
    if (!headerCheckboxSlot_)
    {
        return 22.0f;
    }
    return 22.0f + HeaderCheckboxSize() + 8.0f;
}

void FoldoutPanel::DrawChevron(float centerX, float centerY, Color color) const
{
    // Always drawn; direction depends on expanded state.
    // Vertices must be counter-clockwise or raylib culls the triangle.
    if (expanded_)
    {
        DrawTriangle(
            {centerX - 4.0f, centerY - 2.0f}, {centerX, centerY + 4.0f}, {centerX + 4.0f, centerY - 2.0f}, color);
    }
    else
    {
        DrawTriangle(
            {centerX - 2.0f, centerY - 4.0f}, {centerX - 2.0f, centerY + 4.0f}, {centerX + 4.0f, centerY}, color);
    }
}
