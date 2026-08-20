#pragma once

#include "raylib.h"

// Blender-style collapsible section: header with always-visible chevron + panel chrome.
// Optional header checkbox slot (Raytracing-style). Content is drawn by the caller
// inside ContentArea(panelBounds) when expanded.
//
// Hover toggle: SetHoverToggleKey(KEY_A) + SyncPanelBounds each frame; then
// TryHoverToggleShortcut() collapses/expands when the mouse is over the section header.
class FoldoutPanel
{
public:
    explicit FoldoutPanel(const char* title,
                          bool expanded = true,
                          bool headerCheckboxSlot = false,
                          bool collapsible = true);

    // Toggle on header click (excludes checkbox slot). Returns true if expand state changed.
    // No-op when the panel is not collapsible.
    bool Update(Rectangle headerBounds, bool enabled = true);

    // Draws panel background/border and the header row (chevron only if collapsible).
    void Draw(Rectangle panelBounds, Font font, bool enabled = true) const;

    bool IsExpanded() const;
    void SetExpanded(bool expanded);
    void ToggleExpanded();
    bool IsHovered() const;
    bool IsCollapsible() const;
    bool HasHeaderCheckboxSlot() const;

    void SetHoverToggleKey(int key);
    int HoverToggleKey() const;
    void SyncPanelBounds(Rectangle panelBounds);
    bool ContainsMouse() const;
    // Returns true if this foldout consumed a hover-toggle key press.
    bool TryHoverToggleShortcut();

    static float HeaderHeight();
    static float ContentTopPadding();
    static float ContentBottomPadding();
    static float SidePadding();
    static float HeaderCheckboxSize();

    Rectangle HeaderBounds(Rectangle panelBounds) const;
    Rectangle HeaderClickBounds(Rectangle panelBounds) const;
    Rectangle HeaderCheckboxBounds(Rectangle panelBounds) const;
    Rectangle ContentArea(Rectangle panelBounds) const;

private:
    void DrawChevron(float centerX, float centerY, Color color) const;
    float TitleOffsetX() const;

    const char* title_;
    bool expanded_ = true;
    bool headerCheckboxSlot_ = false;
    bool collapsible_ = true;
    bool isHovered_ = false;
    int hoverToggleKey_ = 0;
    Rectangle panelBounds_{};
    bool hasPanelBounds_ = false;
};
