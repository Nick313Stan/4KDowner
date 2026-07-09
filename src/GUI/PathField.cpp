#include "PathField.h"

#include "MouseCursor.h"
#include "UiClip.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#undef CloseWindow
#undef ShowCursor
#undef DrawTextEx
#else
#include "tinyfiledialogs.h"
#endif

#include <algorithm>

namespace
{
#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
    {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 0)
    {
        return {};
    }

    std::wstring wide(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), size);
    return wide;
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return {};
    }

    std::string utf8(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, utf8.data(), size, nullptr, nullptr);
    return utf8;
}

#endif

std::string SelectFolder(const std::string& currentPath)
{
#ifdef _WIN32
    HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninitialize = SUCCEEDED(initResult);
    if (initResult == RPC_E_CHANGED_MODE)
    {
        initResult = S_OK;
    }
    if (FAILED(initResult))
    {
        return {};
    }

    IFileOpenDialog* dialog = nullptr;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(result))
    {
        if (shouldUninitialize)
        {
            CoUninitialize();
        }
        return {};
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose download folder");

    std::wstring initialPath = Utf8ToWide(currentPath);
    if (!initialPath.empty())
    {
        IShellItem* initialFolder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initialPath.c_str(), nullptr, IID_PPV_ARGS(&initialFolder))))
        {
            dialog->SetFolder(initialFolder);
            initialFolder->Release();
        }
    }

    std::string selectedPath;
    result = dialog->Show(static_cast<HWND>(GetWindowHandle()));
    if (SUCCEEDED(result))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)))
        {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            {
                selectedPath = WideToUtf8(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }

    dialog->Release();
    if (shouldUninitialize)
    {
        CoUninitialize();
    }

    return selectedPath;
#else
    const char* selected =
        tinyfd_selectFolderDialog("Choose download folder", currentPath.empty() ? nullptr : currentPath.c_str());
    return selected == nullptr ? std::string{} : std::string(selected);
#endif
}

std::string CodepointToUtf8(int codepoint)
{
    std::string result;
    if (codepoint <= 0x7F)
    {
        result.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint <= 0x7FF)
    {
        result.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint <= 0xFFFF)
    {
        result.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint <= 0x10FFFF)
    {
        result.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }

    return result;
}

size_t PreviousUtf8Index(const std::string& value, size_t cursorIndex)
{
    if (cursorIndex == 0 || value.empty())
    {
        return 0;
    }

    size_t index = std::min(cursorIndex, value.size()) - 1;
    while (index > 0 && (static_cast<unsigned char>(value[index]) & 0xC0) == 0x80)
    {
        --index;
    }
    return index;
}

size_t NextUtf8Index(const std::string& value, size_t cursorIndex)
{
    if (cursorIndex >= value.size())
    {
        return value.size();
    }

    size_t index = cursorIndex + 1;
    while (index < value.size() && (static_cast<unsigned char>(value[index]) & 0xC0) == 0x80)
    {
        ++index;
    }
    return index;
}
} // namespace

void PathField::Update(Rectangle bounds, Font font, std::string& path, bool enabled)
{
    // Defer blur by one frame so DockArea::HandleShortcuts still sees IsActive() on the
    // same Enter/Escape press that commits the path (updates run before shortcuts).
    if (pendingBlur_)
    {
        isActive_ = false;
        pendingBlur_ = false;
        isDraggingSelection_ = false;
    }

    cursorIndex_ = std::min(cursorIndex_, path.size());
    selectionAnchor_ = std::min(selectionAnchor_, path.size());

    if (!enabled)
    {
        CommitPath(path);
        isActive_ = false;
        pendingBlur_ = false;
        isDraggingSelection_ = false;
        return;
    }

    const Rectangle browseBounds = {bounds.x + bounds.width - 26.0f, bounds.y + 2.0f, 22.0f, bounds.height - 4.0f};
    const Rectangle textBounds = {bounds.x + 8.0f, bounds.y, bounds.width - 40.0f, bounds.height};
    browseHovered_ = enabled && CheckCollisionPointRec(GetMousePosition(), browseBounds);

    const auto cursorXFromMouse = [&]()
    {
        return GetMousePosition().x - textBounds.x + scrollOffset_;
    };

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        const bool onBrowse = CheckCollisionPointRec(GetMousePosition(), browseBounds);
        const bool inText = CheckCollisionPointRec(GetMousePosition(), textBounds);
        const bool inField = CheckCollisionPointRec(GetMousePosition(), bounds) && !onBrowse;

        if (isActive_ && !inField)
        {
            CommitPath(path);
        }

        isActive_ = inField;
        pendingBlur_ = false;
        if (inField)
        {
            if (inText)
            {
                const size_t hitIndex = HitTestCursorIndex(font, path, cursorXFromMouse());
                cursorIndex_ = hitIndex;
                selectionAnchor_ = hitIndex;
                isDraggingSelection_ = true;
            }
            else
            {
                cursorIndex_ = path.size();
                selectionAnchor_ = cursorIndex_;
                isDraggingSelection_ = false;
            }
        }
        else
        {
            isDraggingSelection_ = false;
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        isDraggingSelection_ = false;
    }

    if (CheckCollisionPointRec(GetMousePosition(), browseBounds) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        CommitPath(path);
        const std::string selected = SelectFolder(path);
        if (!selected.empty())
        {
            path = selected;
            cursorIndex_ = path.size();
            selectionAnchor_ = cursorIndex_;
            wasEditedManually_ = false;
        }
        isActive_ = false;
        pendingBlur_ = false;
        return;
    }

    if (!isActive_)
    {
        return;
    }

    if (isDraggingSelection_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        cursorIndex_ = HitTestCursorIndex(font, path, cursorXFromMouse());
    }

    const bool controlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const auto hasSelection = [&]()
    {
        return cursorIndex_ != selectionAnchor_;
    };
    const auto selectionStart = [&]()
    {
        return std::min(cursorIndex_, selectionAnchor_);
    };
    const auto selectionEnd = [&]()
    {
        return std::max(cursorIndex_, selectionAnchor_);
    };
    const auto clearSelection = [&]()
    {
        selectionAnchor_ = cursorIndex_;
    };
    const auto deleteSelection = [&]()
    {
        if (!hasSelection())
        {
            return false;
        }

        const size_t start = selectionStart();
        const size_t end = selectionEnd();
        path.erase(start, end - start);
        cursorIndex_ = start;
        selectionAnchor_ = start;
        wasEditedManually_ = true;
        return true;
    };
    const auto insertText = [&](const std::string& text)
    {
        deleteSelection();
        path.insert(cursorIndex_, text);
        cursorIndex_ += text.size();
        selectionAnchor_ = cursorIndex_;
        wasEditedManually_ = true;
    };
    const auto shouldRepeatKey = [](KeyboardKey key, double& nextRepeatTime)
    {
        const double now = GetTime();
        if (IsKeyPressed(key))
        {
            nextRepeatTime = now + 0.34;
            return true;
        }
        if (!IsKeyDown(key))
        {
            nextRepeatTime = 0.0;
            return false;
        }
        if (nextRepeatTime > 0.0 && now >= nextRepeatTime)
        {
            nextRepeatTime = now + 0.035;
            return true;
        }
        return false;
    };

    if (controlDown && IsKeyPressed(KEY_A))
    {
        cursorIndex_ = path.size();
        selectionAnchor_ = 0;
    }

    const auto clipboardSlice = [&]() -> std::string
    {
        if (hasSelection())
        {
            return path.substr(selectionStart(), selectionEnd() - selectionStart());
        }
        return path;
    };

    if (controlDown && IsKeyPressed(KEY_C))
    {
        const std::string text = clipboardSlice();
        if (!text.empty())
        {
            SetClipboardText(text.c_str());
        }
    }

    if (controlDown && IsKeyPressed(KEY_X))
    {
        const std::string text = clipboardSlice();
        if (!text.empty())
        {
            SetClipboardText(text.c_str());
            if (hasSelection())
            {
                deleteSelection();
            }
            else
            {
                path.clear();
                cursorIndex_ = 0;
                selectionAnchor_ = 0;
                wasEditedManually_ = true;
            }
        }
    }

    if (controlDown && IsKeyPressed(KEY_V))
    {
        const char* clipboard = GetClipboardText();
        if (clipboard != nullptr && clipboard[0] != '\0')
        {
            std::string text = clipboard;
            text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
            text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
            if (!text.empty())
            {
                insertText(text);
            }
        }
    }

    if (!controlDown)
    {
        int key = GetCharPressed();
        while (key > 0)
        {
            if (key >= 32)
            {
                insertText(CodepointToUtf8(key));
            }
            key = GetCharPressed();
        }
    }

    if (shouldRepeatKey(KEY_BACKSPACE, nextBackspaceRepeatTime_))
    {
        if (!deleteSelection() && cursorIndex_ > 0)
        {
            const size_t previousIndex = PreviousUtf8Index(path, cursorIndex_);
            path.erase(previousIndex, cursorIndex_ - previousIndex);
            cursorIndex_ = previousIndex;
            selectionAnchor_ = cursorIndex_;
            wasEditedManually_ = true;
        }
    }

    if (shouldRepeatKey(KEY_DELETE, nextDeleteRepeatTime_))
    {
        if (!deleteSelection() && cursorIndex_ < path.size())
        {
            const size_t nextIndex = NextUtf8Index(path, cursorIndex_);
            path.erase(cursorIndex_, nextIndex - cursorIndex_);
            selectionAnchor_ = cursorIndex_;
            wasEditedManually_ = true;
        }
    }

    if (shouldRepeatKey(KEY_LEFT, nextLeftRepeatTime_))
    {
        if (!shiftDown && hasSelection())
        {
            cursorIndex_ = selectionStart();
            selectionAnchor_ = cursorIndex_;
        }
        else if (shiftDown)
        {
            cursorIndex_ = PreviousUtf8Index(path, cursorIndex_);
        }
        else
        {
            cursorIndex_ = PreviousUtf8Index(path, cursorIndex_);
            clearSelection();
        }
    }
    if (shouldRepeatKey(KEY_RIGHT, nextRightRepeatTime_))
    {
        if (!shiftDown && hasSelection())
        {
            cursorIndex_ = selectionEnd();
            selectionAnchor_ = cursorIndex_;
        }
        else if (shiftDown)
        {
            cursorIndex_ = NextUtf8Index(path, cursorIndex_);
        }
        else
        {
            cursorIndex_ = NextUtf8Index(path, cursorIndex_);
            clearSelection();
        }
    }
    if (shouldRepeatKey(KEY_HOME, nextHomeRepeatTime_))
    {
        cursorIndex_ = 0;
        if (!shiftDown)
        {
            clearSelection();
        }
    }
    if (shouldRepeatKey(KEY_END, nextEndRepeatTime_))
    {
        cursorIndex_ = path.size();
        if (!shiftDown)
        {
            clearSelection();
        }
    }

    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE))
    {
        CommitPath(path);
        pendingBlur_ = true;
    }

    if (CheckCollisionPointRec(GetMousePosition(), textBounds) &&
        !CheckCollisionPointRec(GetMousePosition(), browseBounds))
    {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            scrollOffset_ -= wheel * 24.0f;
            if (scrollOffset_ < 0.0f)
            {
                scrollOffset_ = 0.0f;
            }
        }
    }
}

size_t PathField::HitTestCursorIndex(Font font, const std::string& path, float localX) const
{
    if (path.empty())
    {
        return 0;
    }

    if (localX <= 0.0f)
    {
        return 0;
    }

    const float totalWidth = MeasureTextEx(font, path.c_str(), kFontSize, 0.0f).x;
    if (localX >= totalWidth)
    {
        return path.size();
    }

    size_t index = 0;
    while (index < path.size())
    {
        const size_t nextIndex = NextUtf8Index(path, index);
        const std::string before = path.substr(0, index);
        const std::string through = path.substr(0, nextIndex);
        const float left = MeasureTextEx(font, before.c_str(), kFontSize, 0.0f).x;
        const float right = MeasureTextEx(font, through.c_str(), kFontSize, 0.0f).x;
        const float midpoint = (left + right) * 0.5f;
        if (localX < midpoint)
        {
            return index;
        }
        index = nextIndex;
    }

    return path.size();
}

void PathField::EnsureScrollVisible(Font font, float textAreaWidth, const std::string& path) const
{
    if (path.empty() || textAreaWidth <= 0.0f)
    {
        scrollOffset_ = 0.0f;
        return;
    }

    // Keep a gap so the caret stays visible and does not sit under the browse button clip edge.
    constexpr float kCaretPad = 12.0f;
    const float usableWidth = std::max(1.0f, textAreaWidth - kCaretPad);
    const float textWidth = MeasureTextEx(font, path.c_str(), kFontSize, 0.0f).x;
    const float maxScroll = std::max(0.0f, textWidth - usableWidth);
    if (textWidth <= usableWidth)
    {
        scrollOffset_ = 0.0f;
        return;
    }

    scrollOffset_ = std::min(scrollOffset_, maxScroll);

    const std::string beforeCursor = path.substr(0, std::min(cursorIndex_, path.size()));
    const float cursorX = MeasureTextEx(font, beforeCursor.c_str(), kFontSize, 0.0f).x;
    constexpr float leftPadding = 6.0f;

    if (cursorX - scrollOffset_ > usableWidth)
    {
        scrollOffset_ = cursorX - usableWidth;
    }
    if (cursorX - scrollOffset_ < leftPadding)
    {
        scrollOffset_ = cursorX - leftPadding;
    }

    scrollOffset_ = std::max(0.0f, std::min(scrollOffset_, maxScroll));
}

void PathField::Draw(
    Rectangle bounds, Font font, const std::string& path, bool enabled, const Rectangle* parentScissor) const
{
    const Color background = enabled ? Color{26, 30, 26, 255} : Color{18, 22, 18, 255};
    const Color border = isActive_ ? Color{106, 144, 106, 255} : Color{64, 76, 64, 255};
    const Color text = enabled ? Color{230, 234, 230, 255} : Color{118, 128, 118, 255};
    const Rectangle browseBounds = {bounds.x + bounds.width - 26.0f, bounds.y + 2.0f, 22.0f, bounds.height - 4.0f};
    (void)parentScissor;

    DrawRectangleRounded(bounds, 0.28f, 10, background);
    DrawRectangleRoundedLines(bounds, 0.28f, 10, border);

    const Rectangle textBounds = {bounds.x + 8.0f, bounds.y, bounds.width - 40.0f, bounds.height};
    EnsureScrollVisible(font, textBounds.width, path);
    const float textX = textBounds.x - scrollOffset_;
    UiClip::Push(textBounds);
    if (path.empty())
    {
        DrawTextEx(font, "Enter path...", {textBounds.x, bounds.y + 5.0f}, kFontSize, 0.0f, text);
    }
    else
    {
        if (isActive_ && cursorIndex_ != selectionAnchor_)
        {
            const size_t start = std::min(cursorIndex_, selectionAnchor_);
            const size_t end = std::max(cursorIndex_, selectionAnchor_);
            const std::string beforeSelection = path.substr(0, start);
            const std::string selectedText = path.substr(start, end - start);
            const float selectionX = textX + MeasureTextEx(font, beforeSelection.c_str(), kFontSize, 0.0f).x;
            const float selectionWidth = MeasureTextEx(font, selectedText.c_str(), kFontSize, 0.0f).x;
            DrawRectangleRec({selectionX, bounds.y + 4.0f, selectionWidth, bounds.height - 8.0f},
                             Color{86, 126, 86, 210});
        }
        DrawTextEx(font, path.c_str(), {textX, bounds.y + 5.0f}, kFontSize, 0.0f, text);
    }
    if (isActive_)
    {
        const std::string beforeCursor = path.substr(0, std::min(cursorIndex_, path.size()));
        const float cursorX = textX + MeasureTextEx(font, beforeCursor.c_str(), kFontSize, 0.0f).x + 1.0f;
        DrawLineEx(
            {cursorX, bounds.y + 5.0f}, {cursorX, bounds.y + bounds.height - 5.0f}, 1.0f, Color{220, 238, 220, 255});
    }
    UiClip::Pop();

    DrawRectangleRounded(browseBounds,
                         0.28f,
                         8,
                         enabled ? (browseHovered_ ? Color{104, 122, 104, 255} : Color{72, 82, 72, 255})
                                 : Color{40, 46, 40, 255});
    const Color folderColor =
        enabled ? (browseHovered_ ? Color{244, 248, 244, 255} : Color{224, 230, 224, 255}) : Color{118, 128, 118, 255};
    DrawRectangleLinesEx({browseBounds.x + 5.0f, browseBounds.y + 8.0f, 12.0f, 8.0f}, 1.0f, folderColor);
    DrawRectangleRec({browseBounds.x + 6.0f, browseBounds.y + 6.0f, 5.0f, 3.0f}, folderColor);
    if (browseHovered_)
    {
        UiCursor::RequestHand();
    }
}

void PathField::CommitPath(std::string& path)
{
    (void)path;
    wasEditedManually_ = false;
}

bool PathField::IsActive() const
{
    return isActive_;
}
