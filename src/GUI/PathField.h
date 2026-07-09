#pragma once

#include "raylib.h"

#include <string>

class PathField {
public:
    void Update(Rectangle bounds, Font font, std::string& path, bool enabled = true);
    void Draw(Rectangle bounds, Font font, const std::string& path, bool enabled = true) const;
    bool IsActive() const;

private:
    void CommitPath(std::string& path);
    void EnsureScrollVisible(Font font, float textAreaWidth, const std::string& path) const;
    size_t HitTestCursorIndex(Font font, const std::string& path, float localX) const;

    static constexpr float kFontSize = 15.0f;

    bool isActive_ = false;
    bool wasEditedManually_ = false;
    bool isDraggingSelection_ = false;
    mutable bool browseHovered_ = false;
    mutable float scrollOffset_ = 0.0f;
    size_t cursorIndex_ = 0;
    size_t selectionAnchor_ = 0;
    double nextBackspaceRepeatTime_ = 0.0;
    double nextDeleteRepeatTime_ = 0.0;
    double nextLeftRepeatTime_ = 0.0;
    double nextRightRepeatTime_ = 0.0;
    double nextHomeRepeatTime_ = 0.0;
    double nextEndRepeatTime_ = 0.0;
};
