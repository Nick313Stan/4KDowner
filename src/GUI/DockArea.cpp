#include "DockArea.h"

#include "BrowserDiagnostics.h"
#include "DownloadFormatPredictor.h"
#include "FoldoutPanel.h"
#include "LinkCardGroupNodeInclude.h"
#include "LinkGroupInfoLoader.h"
#include "MouseCursor.h"
#include "ShortcutRouter.h"
#include "TaskbarProgress.h"
#include "Tooltip.h"
#include "UiClip.h"
#include "VideoTitle.h"
#include "YtDlpYouTube.h"
#include "tinyfiledialogs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef FOURKDOWNER_VERSION
#define FOURKDOWNER_VERSION "1.0.0"
#endif

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#undef CloseWindow
#undef ShowCursor
#undef DrawTextEx
#endif

namespace
{
std::string PathUtf8(const std::filesystem::path& path)
{
    return path.u8string();
}

std::string TrimAsciiWhitespace(std::string value)
{
    const auto isSpace = [](unsigned char ch)
    {
        return std::isspace(ch) != 0;
    };
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
    {
        value.erase(value.begin());
    }
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
    {
        value.pop_back();
    }
    return value;
}

bool HasEmbeddedNul(const std::string& value)
{
    return value.find('\0') != std::string::npos;
}

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& value);

bool IsWindowsReservedDeviceName(std::string name)
{
    const size_t dot = name.find('.');
    if (dot != std::string::npos)
    {
        name = name.substr(0, dot);
    }
    for (char& ch : name)
    {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    static const char* kReserved[] = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                      "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                      "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const char* reserved : kReserved)
    {
        if (name == reserved)
        {
            return true;
        }
    }
    return false;
}

bool WindowsComponentHasIllegalChars(const std::string& name)
{
    for (unsigned char ch : name)
    {
        if (ch < 32 || ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' || ch == '*' || ch == ':' ||
            ch == '/' || ch == '\\')
        {
            return true;
        }
    }
    return false;
}
#endif

bool IsValidUserOutputPath(const std::string& utf8Path)
{
    const std::string trimmed = TrimAsciiWhitespace(utf8Path);
    if (trimmed.empty() || HasEmbeddedNul(trimmed))
    {
        return false;
    }

    const std::filesystem::path path = std::filesystem::u8path(trimmed);
    if (!path.is_absolute())
    {
        return false;
    }

#ifdef _WIN32
    const std::wstring wide = Utf8ToWide(trimmed);
    if (wide.empty())
    {
        return false;
    }

    const bool isUnc = wide.size() >= 2 && wide[0] == L'\\' && wide[1] == L'\\';
    if (isUnc)
    {
        // \\server\share\...
        size_t index = 2;
        while (index < wide.size() && wide[index] == L'\\')
        {
            ++index;
        }
        const size_t serverStart = index;
        while (index < wide.size() && wide[index] != L'\\')
        {
            ++index;
        }
        if (index == serverStart)
        {
            return false;
        }
        if (index >= wide.size() || wide[index] != L'\\')
        {
            return false;
        }
        ++index;
        const size_t shareStart = index;
        while (index < wide.size() && wide[index] != L'\\')
        {
            ++index;
        }
        if (index == shareStart)
        {
            return false;
        }
    }
    else
    {
        // Drive path: C:\...
        if (wide.size() < 3 || !((wide[0] >= L'A' && wide[0] <= L'Z') || (wide[0] >= L'a' && wide[0] <= L'z')) ||
            wide[1] != L':' || (wide[2] != L'\\' && wide[2] != L'/'))
        {
            return false;
        }
        wchar_t root[] = {static_cast<wchar_t>(std::towupper(wide[0])), L':', L'\\', L'\0'};
        if (GetDriveTypeW(root) == DRIVE_NO_ROOT_DIR)
        {
            return false;
        }
    }

    for (const std::filesystem::path& part : path)
    {
        const std::string name = part.u8string();
        if (name.empty() || name == "/" || name == "\\" || name == "." || name == "..")
        {
            continue;
        }
        // Root name like "C:" or "\\server" вЂ” skip reserved/illegal checks on those.
        if (name.size() == 2 && name[1] == ':')
        {
            continue;
        }
        if (name.size() >= 2 && name[0] == '\\' && name[1] == '\\')
        {
            continue;
        }
        if (WindowsComponentHasIllegalChars(name))
        {
            return false;
        }
        if (!name.empty() && (name.back() == ' ' || name.back() == '.'))
        {
            return false;
        }
        if (IsWindowsReservedDeviceName(name))
        {
            return false;
        }
    }
#else
    if (trimmed.front() != '/')
    {
        return false;
    }
    for (unsigned char ch : trimmed)
    {
        if (ch == 0 || ch < 32)
        {
            return false;
        }
    }
#endif

    return true;
}

std::string GetDefaultVideosDirectory()
{
    std::filesystem::path videos;
#ifdef _WIN32
    char* userProfile = nullptr;
    size_t userProfileSize = 0;
    if (_dupenv_s(&userProfile, &userProfileSize, "USERPROFILE") == 0 && userProfile != nullptr &&
        userProfile[0] != '\0')
    {
        videos = std::filesystem::path(userProfile) / "Videos";
        std::free(userProfile);
    }
    else
    {
        std::free(userProfile);
        videos = "Videos";
    }
#else
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0')
    {
        videos = std::filesystem::path(home) / "Videos";
    }
    else
    {
        videos = "Videos";
    }
#endif
    std::error_code ec;
    std::filesystem::create_directories(videos, ec);
    return PathUtf8(videos);
}

std::string ResolveChooseFileStartDirectory(const std::string& remembered)
{
    namespace fs = std::filesystem;
    if (!remembered.empty())
    {
        std::error_code ec;
        if (fs::is_directory(fs::u8path(remembered), ec) && !ec)
        {
            return remembered;
        }
    }
    return GetDefaultVideosDirectory();
}

std::string ParentDirectoryUtf8(const std::string& filePath)
{
    if (filePath.empty())
    {
        return {};
    }
    const std::filesystem::path parent = std::filesystem::u8path(filePath).parent_path();
    return parent.empty() ? std::string{} : PathUtf8(parent);
}

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

std::vector<std::string> ChooseMediaFiles(const std::string& startDirectory)
{
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
    dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose media files");

    const COMDLG_FILTERSPEC filters[] = {
        {L"Media files", L"*.mp4;*.mkv;*.mov;*.webm;*.avi;*.m4v;*.mp3;*.m4a;*.wav;*.flac;*.opus"},
        {L"All Files", L"*.*"},
    };
    dialog->SetFileTypes(static_cast<UINT>(sizeof(filters) / sizeof(filters[0])), filters);
    dialog->SetFileTypeIndex(1);

    if (!startDirectory.empty())
    {
        const std::wstring wideDir = Utf8ToWide(startDirectory);
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(wideDir.c_str(), nullptr, IID_PPV_ARGS(&folder))) &&
            folder != nullptr)
        {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    std::vector<std::string> paths;
    result = dialog->Show(static_cast<HWND>(GetWindowHandle()));
    if (SUCCEEDED(result))
    {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dialog->GetResults(&items)) && items != nullptr)
        {
            DWORD count = 0;
            items->GetCount(&count);
            for (DWORD index = 0; index < count; ++index)
            {
                IShellItem* item = nullptr;
                if (FAILED(items->GetItemAt(index, &item)) || item == nullptr)
                {
                    continue;
                }
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr)
                {
                    paths.push_back(WideToUtf8(path));
                    CoTaskMemFree(path);
                }
                item->Release();
            }
            items->Release();
        }
    }

    dialog->Release();
    if (shouldUninitialize)
    {
        CoUninitialize();
    }
    return paths;
}
#else
std::vector<std::string> ChooseMediaFiles(const std::string& startDirectory)
{
    const char* filters[] = {
        "*.mp4", "*.mkv", "*.mov", "*.webm", "*.avi", "*.m4v", "*.mp3", "*.m4a", "*.wav", "*.flac", "*.opus"};
    std::string defaultPath = startDirectory;
    if (!defaultPath.empty())
    {
        const char sep = static_cast<char>(std::filesystem::path::preferred_separator);
        if (defaultPath.back() != '/' && defaultPath.back() != '\\')
        {
            defaultPath.push_back(sep);
        }
    }
    const char* selected =
        tinyfd_openFileDialog("Choose media files", defaultPath.c_str(), 11, filters, "Media files", 1);
    if (selected == nullptr || selected[0] == '\0')
    {
        return {};
    }

    std::vector<std::string> paths;
    const std::string selectedText = selected;
    size_t start = 0;
    while (start <= selectedText.size())
    {
        const size_t end = selectedText.find('|', start);
        const std::string path =
            end == std::string::npos ? selectedText.substr(start) : selectedText.substr(start, end - start);
        if (!path.empty())
        {
            paths.push_back(path);
        }
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    return paths;
}
#endif

namespace HeaderLayout
{
constexpr float kAboutX = 8.0f;
constexpr float kAboutWidth = 56.0f;
constexpr float kInfoX = kAboutX + kAboutWidth + 4.0f;
constexpr float kInfoWidth = 48.0f;
constexpr float kTabY = 3.0f;
constexpr float kTabHeight = 20.0f;
constexpr float kSeparatorX = kInfoX + kInfoWidth + 4.0f;
constexpr float kDownloaderX = kSeparatorX + 6.0f;
constexpr float kDownloaderWidth = 104.0f;
constexpr float kConverterX = kDownloaderX + kDownloaderWidth + 6.0f;
constexpr float kConverterWidth = 100.0f;

Rectangle AboutButton(float headerY)
{
    return {kAboutX, headerY + kTabY, kAboutWidth, kTabHeight};
}

Rectangle InfoButton(float headerY)
{
    return {kInfoX, headerY + kTabY, kInfoWidth, kTabHeight};
}

Rectangle DownloaderTab(float headerY)
{
    return {kDownloaderX, headerY + kTabY, kDownloaderWidth, kTabHeight};
}

Rectangle ConverterTab(float headerY)
{
    return {kConverterX, headerY + kTabY, kConverterWidth, kTabHeight};
}
} // namespace HeaderLayout

std::string ToLowerAscii(std::string value)
{
    for (char& c : value)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return value;
}

struct DownloaderPanelLayout
{
    Rectangle foldoutPanelBounds{};
    Rectangle downloadHeaderBounds{};
    Rectangle resultPanelBounds{};
    float formatDropdownY = 0.0f;
    float mediaDropdownY = 0.0f;
    float qualityDropdownY = 0.0f;
    float pathRowY = 0.0f;
    float pathFieldY = 0.0f;
    float keepIndicesRowY = 0.0f;
    float resultTitleY = 0.0f;
    float resultLineY = 0.0f;
};

constexpr float kOptionsContentTopPad = 36.0f;
constexpr float kOptionsResultLineHeight = 19.0f;
constexpr float kOptionsResultRowCount = 5.0f;
constexpr float kConverterResultRowCount = 4.0f;
constexpr float kOptionsContentBottomPad = 10.0f;
constexpr float kOptionsScrollButtonGap = 8.0f;
constexpr float kAutoConvertRowStep = 30.0f;
constexpr float kAutoConvertSectionGap = 8.0f;
constexpr float kAutoConvertExcludeRowH = 24.0f;

struct AutoConvertDockLayout
{
    Rectangle sectionFoldoutPanelBounds{};
    Rectangle sectionHeaderBounds{};
    Rectangle autoFoldoutPanelBounds{};
    Rectangle autoHeaderBounds{};
    float autoContainerY = 0.0f;
    float autoVideoY = 0.0f;
    float autoAudioY = 0.0f;
    float excludeY = 0.0f;
    Rectangle customFoldoutPanelBounds{};
    Rectangle customHeaderBounds{};
    float customContainerY = 0.0f;
    float customVideoY = 0.0f;
    float customAudioY = 0.0f;
    float nestedCheckX = 0.0f;
    float nestedDropdownX = 0.0f;
    float nestedDropdownW = 0.0f;
};

struct ConverterOptionsDockLayout
{
    Rectangle sectionFoldoutPanelBounds{};
    Rectangle sectionHeaderBounds{};
    Rectangle globalFoldoutPanelBounds{};
    Rectangle globalHeaderBounds{};
    float globalContainerY = 0.0f;
    float globalVideoY = 0.0f;
    float globalAudioY = 0.0f;
    Rectangle customFoldoutPanelBounds{};
    Rectangle customHeaderBounds{};
    float customMixedHintY = 0.0f;
    float customContainerY = 0.0f;
    float customVideoY = 0.0f;
    float customAudioY = 0.0f;
    float nestedCheckX = 0.0f;
    float nestedDropdownX = 0.0f;
    float nestedDropdownW = 0.0f;
};

AutoConvertDockLayout
GetAutoConvertDockLayout(Rectangle autoConvertPanel, bool sectionExpanded, bool autoExpanded, bool customExpanded)
{
    constexpr float kOuterPad = 10.0f;
    const float sectionX = autoConvertPanel.x + kOuterPad;
    const float sectionW = autoConvertPanel.width - kOuterPad * 2.0f;
    const float headerH = FoldoutPanel::HeaderHeight();

    const auto innerFoldoutHeight = [&](bool expanded) -> float
    {
        if (!expanded)
        {
            return headerH;
        }
        return headerH + FoldoutPanel::ContentTopPadding() + kAutoConvertRowStep * 3.0f +
               FoldoutPanel::ContentBottomPadding();
    };

    AutoConvertDockLayout layout;
    float y = autoConvertPanel.y + kOuterPad;

    float sectionH = headerH;
    if (sectionExpanded)
    {
        const float innerH = innerFoldoutHeight(autoExpanded) + kAutoConvertSectionGap + kAutoConvertExcludeRowH +
                             kAutoConvertSectionGap + innerFoldoutHeight(customExpanded);
        sectionH = headerH + FoldoutPanel::ContentTopPadding() + innerH + FoldoutPanel::ContentBottomPadding();
    }

    layout.sectionFoldoutPanelBounds = {sectionX, y, sectionW, sectionH};
    layout.sectionHeaderBounds = {sectionX, y, sectionW, headerH};

    if (!sectionExpanded)
    {
        return layout;
    }

    const float contentX = sectionX + FoldoutPanel::SidePadding();
    const float contentW = sectionW - FoldoutPanel::SidePadding() * 2.0f;
    float nestY = y + headerH + FoldoutPanel::ContentTopPadding();
    const float nestX = contentX;
    const float nestW = contentW;

    const float autoFoldoutH = innerFoldoutHeight(autoExpanded);
    layout.autoFoldoutPanelBounds = {nestX, nestY, nestW, autoFoldoutH};
    layout.autoHeaderBounds = {nestX, nestY, nestW, headerH};
    const float autoContentY = nestY + headerH + FoldoutPanel::ContentTopPadding();
    layout.autoContainerY = autoContentY;
    layout.autoVideoY = autoContentY + kAutoConvertRowStep;
    layout.autoAudioY = autoContentY + kAutoConvertRowStep * 2.0f;

    nestY += autoFoldoutH + kAutoConvertSectionGap;
    layout.excludeY = nestY;
    nestY += kAutoConvertExcludeRowH + kAutoConvertSectionGap;

    const float customFoldoutH = innerFoldoutHeight(customExpanded);
    layout.customFoldoutPanelBounds = {nestX, nestY, nestW, customFoldoutH};
    layout.customHeaderBounds = {nestX, nestY, nestW, headerH};
    const float customContentY = nestY + headerH + FoldoutPanel::ContentTopPadding();
    layout.customContainerY = customContentY;
    layout.customVideoY = customContentY + kAutoConvertRowStep;
    layout.customAudioY = customContentY + kAutoConvertRowStep * 2.0f;

    layout.nestedCheckX = nestX + 12.0f;
    layout.nestedDropdownX = nestX + 108.0f;
    layout.nestedDropdownW = nestW - 120.0f;
    return layout;
}

ConverterOptionsDockLayout GetConverterOptionsDockLayout(Rectangle defaultDockPanel,
                                                         bool sectionExpanded,
                                                         bool globalExpanded,
                                                         bool customExpanded,
                                                         bool showCustomMixedHint)
{
    constexpr float kOuterPad = 10.0f;
    constexpr float kMixedHintHeight = 36.0f;
    constexpr float kMixedHintGap = 8.0f;
    const float sectionX = defaultDockPanel.x + kOuterPad;
    const float sectionW = defaultDockPanel.width - kOuterPad * 2.0f;
    const float headerH = FoldoutPanel::HeaderHeight();

    const auto globalFoldoutHeight = [&](bool expanded) -> float
    {
        if (!expanded)
        {
            return headerH;
        }
        return headerH + FoldoutPanel::ContentTopPadding() + kAutoConvertRowStep * 3.0f +
               FoldoutPanel::ContentBottomPadding();
    };
    const auto customFoldoutHeight = [&](bool expanded) -> float
    {
        if (!expanded)
        {
            return headerH;
        }
        const float mixedExtra = showCustomMixedHint ? (kMixedHintHeight + kMixedHintGap) : 0.0f;
        return headerH + FoldoutPanel::ContentTopPadding() + mixedExtra + kAutoConvertRowStep * 3.0f +
               FoldoutPanel::ContentBottomPadding();
    };

    ConverterOptionsDockLayout layout;
    float y = defaultDockPanel.y + kOuterPad;

    float sectionH = headerH;
    if (sectionExpanded)
    {
        const float innerH =
            globalFoldoutHeight(globalExpanded) + kAutoConvertSectionGap + customFoldoutHeight(customExpanded);
        sectionH = headerH + FoldoutPanel::ContentTopPadding() + innerH + FoldoutPanel::ContentBottomPadding();
    }

    layout.sectionFoldoutPanelBounds = {sectionX, y, sectionW, sectionH};
    layout.sectionHeaderBounds = {sectionX, y, sectionW, headerH};

    if (!sectionExpanded)
    {
        return layout;
    }

    const float contentX = sectionX + FoldoutPanel::SidePadding();
    const float contentW = sectionW - FoldoutPanel::SidePadding() * 2.0f;
    float nestY = y + headerH + FoldoutPanel::ContentTopPadding();
    const float nestX = contentX;
    const float nestW = contentW;

    const float globalFoldoutH = globalFoldoutHeight(globalExpanded);
    layout.globalFoldoutPanelBounds = {nestX, nestY, nestW, globalFoldoutH};
    layout.globalHeaderBounds = {nestX, nestY, nestW, headerH};
    const float globalContentY = nestY + headerH + FoldoutPanel::ContentTopPadding();
    layout.globalContainerY = globalContentY;
    layout.globalVideoY = globalContentY + kAutoConvertRowStep;
    layout.globalAudioY = globalContentY + kAutoConvertRowStep * 2.0f;

    nestY += globalFoldoutH + kAutoConvertSectionGap;

    const float customFoldoutH = customFoldoutHeight(customExpanded);
    layout.customFoldoutPanelBounds = {nestX, nestY, nestW, customFoldoutH};
    layout.customHeaderBounds = {nestX, nestY, nestW, headerH};
    float customContentY = nestY + headerH + FoldoutPanel::ContentTopPadding();
    if (showCustomMixedHint)
    {
        layout.customMixedHintY = customContentY;
        customContentY += kMixedHintHeight + kMixedHintGap;
    }
    layout.customContainerY = customContentY;
    layout.customVideoY = customContentY + kAutoConvertRowStep;
    layout.customAudioY = customContentY + kAutoConvertRowStep * 2.0f;

    layout.nestedCheckX = nestX + 12.0f;
    layout.nestedDropdownX = nestX + 108.0f;
    layout.nestedDropdownW = nestW - 120.0f;
    return layout;
}

DownloaderPanelLayout
GetDownloaderPanelLayout(float panelX, float panelY, float panelWidth, bool downloadExpanded, float scrollOffset = 0.0f)
{
    constexpr float kRowStep = 32.0f;
    constexpr float kDropdownHeight = 25.0f;
    constexpr float kFoldoutGap = 10.0f;
    constexpr float kKeepIndicesRowH = 24.0f;

    DownloaderPanelLayout layout;
    const float foldoutX = panelX + 10.0f;
    const float foldoutW = panelWidth - 20.0f;
    layout.keepIndicesRowY = panelY + 42.0f - scrollOffset;
    const float foldoutY = layout.keepIndicesRowY + kKeepIndicesRowH + 6.0f;
    const float headerH = FoldoutPanel::HeaderHeight();

    float foldoutH = headerH;
    if (downloadExpanded)
    {
        foldoutH = headerH + FoldoutPanel::ContentTopPadding() + kRowStep * 3.0f + FoldoutPanel::ContentBottomPadding();
    }

    layout.foldoutPanelBounds = {foldoutX, foldoutY, foldoutW, foldoutH};
    layout.downloadHeaderBounds = {foldoutX, foldoutY, foldoutW, headerH};

    const float contentStartY = foldoutY + headerH + FoldoutPanel::ContentTopPadding();
    if (downloadExpanded)
    {
        layout.qualityDropdownY = contentStartY;
        layout.formatDropdownY = contentStartY + kRowStep;
        layout.mediaDropdownY = contentStartY + kRowStep * 2.0f;
    }
    else
    {
        layout.formatDropdownY = contentStartY;
        layout.mediaDropdownY = contentStartY;
        layout.qualityDropdownY = contentStartY;
    }

    layout.pathRowY = foldoutY + foldoutH + kFoldoutGap;
    layout.pathFieldY = layout.pathRowY + 24.0f;

    const float resultPanelY = layout.pathFieldY + kDropdownHeight + 12.0f;
    const float resultPanelH = headerH + FoldoutPanel::ContentTopPadding() +
                               kOptionsResultRowCount * kOptionsResultLineHeight + FoldoutPanel::ContentBottomPadding();
    layout.resultPanelBounds = {foldoutX, resultPanelY, foldoutW, resultPanelH};
    layout.resultTitleY = resultPanelY + 4.0f;
    layout.resultLineY = resultPanelY + headerH + FoldoutPanel::ContentTopPadding();
    return layout;
}

float GetDownloaderOptionsContentHeight(float panelY, bool downloadExpanded)
{
    const DownloaderPanelLayout layout = GetDownloaderPanelLayout(0.0f, panelY, 100.0f, downloadExpanded, 0.0f);
    const float contentBottom = layout.resultPanelBounds.y + layout.resultPanelBounds.height + kOptionsContentBottomPad;
    return std::max(0.0f, contentBottom - (panelY + kOptionsContentTopPad));
}

Rectangle GetOptionsScrollViewport(Rectangle settingsPanel, float buttonTopY)
{
    const float top = settingsPanel.y + kOptionsContentTopPad;
    const float bottom = buttonTopY - kOptionsScrollButtonGap;
    return {settingsPanel.x + 2.0f, top, settingsPanel.width - 4.0f, std::max(0.0f, bottom - top)};
}

Rectangle GetConverterResultFoldoutBounds(Rectangle settingsPanel, bool expanded, float scrollOffset = 0.0f)
{
    constexpr float kOuterPad = 10.0f;
    const float headerH = FoldoutPanel::HeaderHeight();
    float foldoutH = headerH;
    if (expanded)
    {
        foldoutH = headerH + FoldoutPanel::ContentTopPadding() + kConverterResultRowCount * kOptionsResultLineHeight +
                   FoldoutPanel::ContentBottomPadding();
    }

    return {settingsPanel.x + kOuterPad,
            settingsPanel.y + 42.0f - scrollOffset,
            settingsPanel.width - kOuterPad * 2.0f,
            foldoutH};
}

float GetConverterOptionsContentHeight(float panelY, bool resultExpanded)
{
    const Rectangle layoutBounds =
        GetConverterResultFoldoutBounds({0.0f, panelY, 100.0f, 1000.0f}, resultExpanded, 0.0f);
    const float contentBottom = layoutBounds.y + layoutBounds.height + kOptionsContentBottomPad;
    return std::max(0.0f, contentBottom - (panelY + kOptionsContentTopPad));
}

constexpr float kScrollbarTrackWidth = 4.0f;
constexpr float kScrollbarEdgePad = 2.0f; // gap outside track on left and right

void DrawOptionsScrollbar(Rectangle viewport, float scrollOffset, float maxScroll)
{
    if (maxScroll <= 0.0f || viewport.height <= 1.0f)
    {
        return;
    }

    const Rectangle track = {viewport.x + viewport.width - kScrollbarTrackWidth - kScrollbarEdgePad,
                             viewport.y + 4.0f,
                             kScrollbarTrackWidth,
                             viewport.height - 8.0f};
    DrawRectangleRounded(track, 1.0f, 4, Color{48, 58, 48, 255});

    const float thumbRatio = viewport.height / (viewport.height + maxScroll);
    const float thumbHeight = std::max(18.0f, track.height * thumbRatio);
    const float thumbTravel = track.height - thumbHeight;
    const float thumbY = track.y + (scrollOffset / maxScroll) * thumbTravel;
    constexpr float kThumbInset = 1.0f;
    DrawRectangleRounded({track.x + kThumbInset, thumbY, std::max(1.0f, track.width - kThumbInset * 2.0f), thumbHeight},
                         1.0f,
                         4,
                         Color{96, 118, 96, 255});
}

void DrawDownloadResultPreview(Font font,
                               const Rectangle& settingsPanel,
                               const PredictedDownload& prediction,
                               float lineStartY,
                               bool showConvertRow = true)
{
    const Color muted = {150, 162, 150, 255};
    const Color valueColor = {168, 198, 168, 255};
    const float x = settingsPanel.x + 26.0f;
    const float valueX = settingsPanel.x + 144.0f;
    const float lineHeight = kOptionsResultLineHeight;

    const auto drawRow = [&](const char* label, const std::string& value, float y)
    {
        DrawTextEx(font, label, {x, y}, 14.0f, 0.0f, muted);
        DrawTextEx(font, value.c_str(), {valueX, y}, 14.0f, 0.0f, valueColor);
    };

    std::string container = prediction.container.empty() ? "unknown" : prediction.container;
    for (char& c : container)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    const std::string videoCodec = prediction.videoCodec.empty() ? "none" : prediction.videoCodec;
    const std::string audioCodec = prediction.audioCodec.empty() ? "none" : prediction.audioCodec;
    const std::string resolution = prediction.resolution.empty() ? "n/a" : prediction.resolution;

    float y = lineStartY;
    if (showConvertRow)
    {
        drawRow("Convert:", prediction.converting ? "yes" : "no", y);
        y += lineHeight;
    }
    drawRow("Video container:", container, y);
    y += lineHeight;
    drawRow("Video codec:", videoCodec, y);
    y += lineHeight;
    drawRow("Audio codec:", audioCodec, y);
    y += lineHeight;
    drawRow("Resolution:", resolution, y);
}

void BeginOptionsContentScissor(Rectangle viewport)
{
    UiClip::Push(viewport);
}

bool MouseHitsClippedControl(Rectangle control, Rectangle clip)
{
    const Vector2 mouse = GetMousePosition();
    return CheckCollisionPointRec(mouse, clip) && CheckCollisionPointRec(mouse, control);
}

void SyncFoldoutToClip(FoldoutPanel& foldout, Rectangle panelBounds, Rectangle clip)
{
    if (panelBounds.width <= 0.0f || panelBounds.height <= 0.0f || !CheckCollisionRecs(panelBounds, clip))
    {
        foldout.SyncPanelBounds({});
        return;
    }
    foldout.SyncPanelBounds(GetCollisionRec(panelBounds, clip));
}

std::filesystem::path FindExistingOutputFile(const std::filesystem::path& outputDirectory,
                                             const std::string& normalizedTitle,
                                             const std::string& extension)
{
    if (normalizedTitle.empty())
    {
        return {};
    }

    const std::string lowerExtension = ToLowerAscii(extension);
    std::error_code error;
    if (!lowerExtension.empty())
    {
        const std::filesystem::path exactPath = outputDirectory / (normalizedTitle + "." + lowerExtension);
        if (std::filesystem::exists(exactPath, error))
        {
            return exactPath;
        }
    }

    // Fallback: any extension, and tolerate yt-dlp --restrict-filenames (spaces в†’ underscores).
    std::string underscored = normalizedTitle;
    for (char& ch : underscored)
    {
        if (ch == ' ')
        {
            ch = '_';
        }
    }

    if (!std::filesystem::is_directory(outputDirectory, error))
    {
        return {};
    }

    std::filesystem::path bestMatch;
    for (const auto& entry : std::filesystem::directory_iterator(outputDirectory, error))
    {
        if (error || !entry.is_regular_file(error))
        {
            continue;
        }
        const std::string stem = entry.path().stem().u8string();
        if (stem != normalizedTitle && stem != underscored)
        {
            continue;
        }

        if (!lowerExtension.empty())
        {
            std::string fileExt = entry.path().extension().u8string();
            if (!fileExt.empty() && fileExt.front() == '.')
            {
                fileExt.erase(fileExt.begin());
            }
            // yt-dlp --no-overwrites may produce "title.mp4 (1)" whose extension is ".mp4 (1)".
            const std::string lowerFileExt = ToLowerAscii(fileExt);
            if (lowerFileExt == lowerExtension || lowerFileExt.rfind(lowerExtension + " (", 0) == 0)
            {
                return entry.path();
            }
            // Requested a specific extension вЂ” never treat another extension as a conflict.
            continue;
        }

        if (bestMatch.empty())
        {
            bestMatch = entry.path();
        }
    }
    return bestMatch;
}

void TryRemoveFile(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return;
    }
#ifdef _WIN32
    const std::wstring widePath = path.wstring();
    SetFileAttributesW(widePath.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (DeleteFileW(widePath.c_str()))
    {
        return;
    }
    // Rename can succeed when a soft lock blocks DeleteFile; then delete the renamed file.
    const std::wstring trashPath = widePath + L".trash";
    if (MoveFileExW(widePath.c_str(), trashPath.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        SetFileAttributesW(trashPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(trashPath.c_str());
    }
#else
    std::error_code error;
    std::filesystem::remove(path, error);
#endif
}

void TryRemoveUtf8File(const std::string& utf8Path)
{
    if (utf8Path.empty())
    {
        return;
    }
    TryRemoveFile(std::filesystem::u8path(utf8Path));
}

// Wipe Documents staging leftovers for a download title. Never used for overwrite prompts.
// Only touches files under stagingDirectory вЂ” never the user's final Videos folder.
void WipeStagingFilesByStem(const std::filesystem::path& stagingDirectory, const std::string& stemWithDownloaded)
{
    if (stagingDirectory.empty() || stemWithDownloaded.empty())
    {
        return;
    }
    // Safety: require the staging marker so we never wipe Title.mp4/Title.mov in the final folder.
    if (stemWithDownloaded.size() < 11 ||
        stemWithDownloaded.compare(stemWithDownloaded.size() - 11, 11, "_downloaded") != 0)
    {
        return;
    }
    std::error_code error;
    if (!std::filesystem::is_directory(stagingDirectory, error))
    {
        return;
    }

    std::string underscored = stemWithDownloaded;
    for (char& ch : underscored)
    {
        if (ch == ' ')
        {
            ch = '_';
        }
    }
    const std::string prefix = stemWithDownloaded + ".";
    const std::string underscoredPrefix = underscored + ".";

    for (const auto& entry : std::filesystem::directory_iterator(stagingDirectory, error))
    {
        if (error || !entry.is_regular_file(error))
        {
            continue;
        }
        const std::string fileName = entry.path().filename().u8string();
        const std::string fileStem = entry.path().stem().u8string();
        if (fileStem == stemWithDownloaded || fileStem == underscored || fileName.rfind(prefix, 0) == 0 ||
            fileName.rfind(underscoredPrefix, 0) == 0)
        {
            TryRemoveFile(entry.path());
        }
    }
}

void ScheduleBackgroundFileDeletes(std::vector<std::string> paths)
{
    paths.erase(std::remove_if(paths.begin(),
                               paths.end(),
                               [](const std::string& path)
                               {
                                   return path.empty();
                               }),
                paths.end());
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    if (paths.empty())
    {
        return;
    }
    std::thread(
        [paths = std::move(paths)]()
        {
            for (int attempt = 0; attempt < 120; ++attempt)
            {
                bool anyLeft = false;
                for (const std::string& path : paths)
                {
                    // Never background-delete files outside the staging folder.
                    const std::string lower = ToLowerAscii(path);
                    if (lower.find("documents") == std::string::npos || lower.find("_downloaded") == std::string::npos)
                    {
                        continue;
                    }
                    std::error_code existsError;
                    const std::filesystem::path file = std::filesystem::u8path(path);
                    if (!std::filesystem::exists(file, existsError))
                    {
                        continue;
                    }
                    TryRemoveFile(file);
                    if (std::filesystem::exists(file, existsError))
                    {
                        anyLeft = true;
                    }
                }
                if (!anyLeft)
                {
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        })
        .detach();
}

// Removes the staging input and related yt-dlp sidecars (thumbnails, format fragments, etc.).
// Returns UTF-8 paths that still exist after the attempt (caller may retry later).
std::vector<std::string> CleanupAutoConvertStagingArtifacts(const std::filesystem::path& stagingInput)
{
    std::vector<std::string> remaining;
    if (stagingInput.empty())
    {
        return remaining;
    }

    const std::filesystem::path directory = stagingInput.parent_path();
    const std::string stem = stagingInput.stem().u8string();
    WipeStagingFilesByStem(directory, stem);

    std::error_code existsError;
    if (std::filesystem::exists(stagingInput, existsError))
    {
        remaining.push_back(PathUtf8(stagingInput));
    }
    return remaining;
}

std::vector<std::string> CleanupAutoConvertStagingForCard(const LinkCardNode& card,
                                                          const std::string& fallbackInputPath,
                                                          const std::string& stagingDirectory)
{
    std::vector<std::string> remaining;
    const auto mergeRemaining = [&](std::vector<std::string> paths)
    {
        for (std::string& path : paths)
        {
            if (path.empty())
            {
                continue;
            }
            if (std::find(remaining.begin(), remaining.end(), path) == remaining.end())
            {
                remaining.push_back(std::move(path));
            }
        }
    };

    if (!card.AutoConvertStagingPath().empty())
    {
        mergeRemaining(CleanupAutoConvertStagingArtifacts(std::filesystem::u8path(card.AutoConvertStagingPath())));
    }
    if (!fallbackInputPath.empty())
    {
        mergeRemaining(CleanupAutoConvertStagingArtifacts(std::filesystem::u8path(fallbackInputPath)));
    }

    std::string stem = card.OriginalNormalizedTitle();
    if (stem.empty())
    {
        stem = card.ExpectedNormalizedTitle();
    }
    if (stem.empty() || stagingDirectory.empty())
    {
        return remaining;
    }
    if (stem.size() < 11 || stem.compare(stem.size() - 11, 11, "_downloaded") != 0)
    {
        stem += "_downloaded";
    }

    const std::filesystem::path directory = std::filesystem::u8path(stagingDirectory);
    WipeStagingFilesByStem(directory, stem);

    std::error_code error;
    if (!std::filesystem::is_directory(directory, error))
    {
        return remaining;
    }
    std::string underscoredStem = stem;
    for (char& ch : underscoredStem)
    {
        if (ch == ' ')
        {
            ch = '_';
        }
    }
    const std::string stemPrefix = stem + ".";
    const std::string underscoredPrefix = underscoredStem + ".";
    for (const auto& entry : std::filesystem::directory_iterator(directory, error))
    {
        if (error || !entry.is_regular_file(error))
        {
            continue;
        }
        const std::string fileName = entry.path().filename().u8string();
        const std::string fileStem = entry.path().stem().u8string();
        if (fileStem == stem || fileStem == underscoredStem || fileName.rfind(stemPrefix, 0) == 0 ||
            fileName.rfind(underscoredPrefix, 0) == 0)
        {
            mergeRemaining({PathUtf8(entry.path())});
        }
    }
    return remaining;
}

std::vector<std::string> BuildConverterItems(const std::vector<std::string>& baseItems, const std::string& current)
{
    std::vector<std::string> result;
    if (!current.empty() && current != "None" && current != "Unknown")
    {
        result.push_back(current + " (Current)");
    }

    for (const std::string& item : baseItems)
    {
        if (item == current)
        {
            continue;
        }
        result.push_back(item);
    }

    return result;
}

std::string StripCurrentLabel(std::string value)
{
    const std::string suffix = " (Current)";
    const size_t position = value.find(suffix);
    if (position != std::string::npos)
    {
        value.erase(position);
    }
    return value;
}

bool IsConverterCurrentItem(const std::string& label)
{
    return Dropdown::IsInactiveItem(label);
}

int GetDefaultConverterIndex(const std::vector<std::string>& items, const std::string& preferred)
{
    for (int index = 0; index < static_cast<int>(items.size()); ++index)
    {
        if (!IsConverterCurrentItem(items[index]) && StripCurrentLabel(items[index]) == preferred)
        {
            return index;
        }
    }

    for (int index = 0; index < static_cast<int>(items.size()); ++index)
    {
        if (!IsConverterCurrentItem(items[index]))
        {
            return index;
        }
    }

    return 0;
}

void EnsureConverterDropdownIndex(int& selectedIndex,
                                  const std::vector<std::string>& items,
                                  const std::string& preferred)
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(items.size()) ||
        IsConverterCurrentItem(items[selectedIndex]))
    {
        selectedIndex = GetDefaultConverterIndex(items, preferred);
    }
}

std::vector<std::string> CompatibleVideoCodecsForContainer(const std::string& container)
{
    const std::string lower = ToLowerAscii(container);
    if (lower == "webm")
    {
        return {"VP9", "AV1"};
    }
    if (lower == "mov")
    {
        return {"H.264", "H.265"};
    }
    if (lower == "mkv")
    {
        return {"H.264", "H.265", "AV1", "VP9"};
    }
    // MP4 and fallback
    return {"H.264", "H.265", "AV1"};
}

std::vector<std::string> CompatibleAudioCodecsForContainer(const std::string& container)
{
    const std::string lower = ToLowerAscii(container);
    if (lower == "webm")
    {
        return {"Opus"};
    }
    if (lower == "mov")
    {
        return {"AAC"};
    }
    if (lower == "mkv")
    {
        return {"AAC", "MP3", "Opus", "FLAC"};
    }
    // MP4 and fallback
    return {"AAC", "MP3"};
}

const char* DefaultVideoCodecForContainer(const std::string& container)
{
    const std::string lower = ToLowerAscii(container);
    if (lower == "webm")
    {
        return "VP9";
    }
    return "H.264";
}

const char* DefaultAudioCodecForContainer(const std::string& container)
{
    const std::string lower = ToLowerAscii(container);
    if (lower == "webm")
    {
        return "Opus";
    }
    return "AAC";
}

std::string EffectiveConvertContainer(bool convertContainer,
                                      int containerIndex,
                                      const std::vector<std::string>& containerItems,
                                      const std::string& fallbackContainer)
{
    if (convertContainer && !containerItems.empty())
    {
        const int index = std::clamp(containerIndex, 0, static_cast<int>(containerItems.size()) - 1);
        return StripCurrentLabel(containerItems[index]);
    }
    if (!fallbackContainer.empty() && fallbackContainer != "None" && fallbackContainer != "Unknown")
    {
        return fallbackContainer;
    }
    return "MP4";
}

int FindCodecIndex(const std::vector<std::string>& items, const std::string& preferred, const std::string& fallback)
{
    for (int index = 0; index < static_cast<int>(items.size()); ++index)
    {
        if (StripCurrentLabel(items[index]) == preferred && !IsConverterCurrentItem(items[index]))
        {
            return index;
        }
    }
    for (int index = 0; index < static_cast<int>(items.size()); ++index)
    {
        if (StripCurrentLabel(items[index]) == fallback && !IsConverterCurrentItem(items[index]))
        {
            return index;
        }
    }
    return GetDefaultConverterIndex(items, fallback);
}

bool UiCodecInList(const std::vector<std::string>& allowed, const std::string& codec)
{
    return std::find(allowed.begin(), allowed.end(), codec) != allowed.end();
}

// Stream-copy is only predicted when the UI codec is legal in the target container.
// WEBM+AV1 is technically muxable, but Premiere and many NLEs reject it вЂ” treat AV1 as
// non-copy for auto convert unless the user explicitly picks Video в†’ AV1.
bool PredictedVideoCopiesToContainer(const std::string& videoCodec, const std::string& container, bool userChoseVideo)
{
    if (videoCodec.empty() || videoCodec == "None" || videoCodec == "Unknown")
    {
        return false;
    }
    if (!UiCodecInList(CompatibleVideoCodecsForContainer(container), videoCodec))
    {
        return false;
    }
    if (!userChoseVideo && ToLowerAscii(container) == "webm" && videoCodec == "AV1")
    {
        return false;
    }
    return true;
}

bool PredictedAudioCopiesToContainer(const std::string& audioCodec, const std::string& container)
{
    if (audioCodec.empty() || audioCodec == "None" || audioCodec == "Unknown")
    {
        return false;
    }
    return UiCodecInList(CompatibleAudioCodecsForContainer(container), audioCodec);
}

PredictedDownload ApplyAutoConvertToPrediction(PredictedDownload prediction, const AutoConvertOptions& options)
{
    if (!options.IsActive())
    {
        prediction.converting = false;
        return prediction;
    }

    prediction.converting = true;

    static const std::vector<std::string> kContainers = {"MP4", "MKV", "MOV", "WEBM"};
    const std::string fallbackContainer = prediction.container.empty() ? "MP4" : prediction.container;
    const std::string container =
        EffectiveConvertContainer(options.convertContainer, options.containerIndex, kContainers, fallbackContainer);
    const std::vector<std::string> videoCodecs = CompatibleVideoCodecsForContainer(container);
    const std::vector<std::string> audioCodecs = CompatibleAudioCodecsForContainer(container);

    if (options.convertContainer)
    {
        prediction.container = container;
    }
    if (options.convertVideo && !videoCodecs.empty())
    {
        const int index = std::clamp(options.videoIndex, 0, static_cast<int>(videoCodecs.size()) - 1);
        prediction.videoCodec = videoCodecs[index];
        if (!options.convertContainer)
        {
            prediction.container = container;
        }
    }
    else if (options.convertContainer &&
             !PredictedVideoCopiesToContainer(prediction.videoCodec, prediction.container, false))
    {
        // Container-only: show the encode defaults ConvertRunner will use (MOVв†’H.264, WEBMв†’VP9).
        prediction.videoCodec = DefaultVideoCodecForContainer(prediction.container);
    }

    if (options.convertAudio && !audioCodecs.empty())
    {
        const int index = std::clamp(options.audioIndex, 0, static_cast<int>(audioCodecs.size()) - 1);
        prediction.audioCodec = audioCodecs[index];
    }
    else if (options.convertContainer && !PredictedAudioCopiesToContainer(prediction.audioCodec, prediction.container))
    {
        prediction.audioCodec = DefaultAudioCodecForContainer(prediction.container);
    }
    return prediction;
}

std::string FormatElapsedTime(double seconds)
{
    if (seconds <= 0.0)
    {
        return "0:00";
    }

    const int totalSeconds = static_cast<int>(seconds + 0.5);
    const int minutes = totalSeconds / 60;
    const int secs = totalSeconds % 60;
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, secs);
    return buffer;
}

std::string TruncateTextToWidth(Font font, const std::string& text, float fontSize, float maxWidth)
{
    if (text.empty() || maxWidth <= 0.0f)
    {
        return "...";
    }

    if (MeasureTextEx(font, text.c_str(), fontSize, 0.0f).x <= maxWidth)
    {
        return text;
    }

    const std::string ellipsis = "...";
    if (MeasureTextEx(font, ellipsis.c_str(), fontSize, 0.0f).x > maxWidth)
    {
        return ellipsis;
    }

    size_t low = 0;
    size_t high = text.size();
    while (low < high)
    {
        const size_t mid = (low + high + 1) / 2;
        const std::string candidate = text.substr(0, mid) + ellipsis;
        if (MeasureTextEx(font, candidate.c_str(), fontSize, 0.0f).x <= maxWidth)
        {
            low = mid;
        }
        else
        {
            high = mid - 1;
        }
    }

    return low == 0 ? ellipsis : text.substr(0, low) + ellipsis;
}

std::string FormatDownloadFinishedStatus(double seconds, bool allDownloads)
{
    if (allDownloads)
    {
        return "All downloads finished вЂ” took " + FormatElapsedTime(seconds);
    }
    return "Download finished вЂ” took " + FormatElapsedTime(seconds);
}

std::string FormatConvertFinishedStatus(double seconds, bool allConversions)
{
    if (allConversions)
    {
        return "All videos converted successfully вЂ” took " + FormatElapsedTime(seconds);
    }
    return "Video converted successfully вЂ” took " + FormatElapsedTime(seconds);
}

std::string FormatDownloadConvertFinishedStatus(double seconds, bool allJobs)
{
    if (allJobs)
    {
        return "All downloads & converts finished вЂ” took " + FormatElapsedTime(seconds);
    }
    return "Download & convert finished вЂ” took " + FormatElapsedTime(seconds);
}

std::filesystem::path GetLogPath()
{
#ifdef _WIN32
    char* localAppData = nullptr;
    size_t localAppDataSize = 0;
    if (_dupenv_s(&localAppData, &localAppDataSize, "LOCALAPPDATA") == 0 && localAppData != nullptr &&
        localAppData[0] != '\0')
    {
        const std::filesystem::path path = std::filesystem::path(localAppData) / "4KDowner" / "logs" / "4kdowner.log";
        std::free(localAppData);
        return path;
    }
    std::free(localAppData);
#endif
    std::error_code error;
    const std::filesystem::path tempPath = std::filesystem::temp_directory_path(error);
    if (!error)
    {
        return tempPath / "4KDowner" / "logs" / "4kdowner.log";
    }
    return std::filesystem::path("4kdowner.log");
}

void WriteDebugLog(const std::string& message)
{
    std::error_code error;
    const std::filesystem::path path = GetLogPath();
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::app);
    if (file)
    {
        file << FormatElapsedTime(GetTime()) << " " << message << "\n";
    }
}

void DrawWrappedText(
    Font font, const std::string& text, Vector2 position, float fontSize, float maxWidth, int maxLines, Color color)
{
    if (maxWidth <= 0.0f || maxLines <= 0)
    {
        return;
    }

    const auto ellipsize = [&](const std::string& line) -> std::string
    {
        if (MeasureTextEx(font, line.c_str(), fontSize, 0.0f).x <= maxWidth)
        {
            return line;
        }
        const std::string ellipsis = "...";
        if (MeasureTextEx(font, ellipsis.c_str(), fontSize, 0.0f).x > maxWidth)
        {
            return ellipsis;
        }
        size_t low = 0;
        size_t high = line.size();
        while (low < high)
        {
            const size_t mid = (low + high + 1) / 2;
            const std::string candidate = line.substr(0, mid) + ellipsis;
            if (MeasureTextEx(font, candidate.c_str(), fontSize, 0.0f).x <= maxWidth)
            {
                low = mid;
            }
            else
            {
                high = mid - 1;
            }
        }
        return low == 0 ? ellipsis : line.substr(0, low) + ellipsis;
    };

    std::stringstream stream(text);
    std::vector<std::string> lines;
    std::string word;
    std::string currentLine;
    bool truncated = false;

    while (stream >> word)
    {
        const std::string candidate = currentLine.empty() ? word : currentLine + " " + word;
        if (MeasureTextEx(font, candidate.c_str(), fontSize, 0.0f).x <= maxWidth)
        {
            currentLine = candidate;
            continue;
        }

        if (!currentLine.empty())
        {
            lines.push_back(currentLine);
            currentLine = word;
            if (MeasureTextEx(font, currentLine.c_str(), fontSize, 0.0f).x > maxWidth)
            {
                currentLine = ellipsize(currentLine);
            }
        }
        else
        {
            lines.push_back(ellipsize(word));
            currentLine.clear();
        }

        if (static_cast<int>(lines.size()) >= maxLines)
        {
            truncated = true;
            break;
        }
    }

    if (!truncated && !currentLine.empty() && static_cast<int>(lines.size()) < maxLines)
    {
        lines.push_back(currentLine);
    }
    else if (!truncated && !currentLine.empty())
    {
        truncated = true;
    }

    // Remaining unread words mean we ran out of line slots.
    if (!truncated)
    {
        std::string leftover;
        if (stream >> leftover)
        {
            truncated = true;
        }
    }

    if (truncated && !lines.empty())
    {
        lines.back() = ellipsize(lines.back());
    }

    for (int index = 0; index < static_cast<int>(lines.size()); ++index)
    {
        DrawTextEx(font,
                   lines[index].c_str(),
                   {position.x, position.y + static_cast<float>(index) * (fontSize + 3.0f)},
                   fontSize,
                   0.0f,
                   color);
    }
}

constexpr float kAboutLinkFontSize = 15.0f;

struct AboutDialogLink
{
    const char* url;
    const char* label;
    float yOffset;
};

struct AboutDialogMetrics
{
    Rectangle modal{};
    Rectangle okButton{};
    float itemX = 0.0f;

    static AboutDialogMetrics FromWindow(int windowWidth, int windowHeight)
    {
        constexpr float kModalWidth = 460.0f;
        constexpr float kModalHeight = 356.0f;
        AboutDialogMetrics metrics;
        metrics.modal = {(static_cast<float>(windowWidth) - kModalWidth) * 0.5f,
                         (static_cast<float>(windowHeight) - kModalHeight) * 0.5f,
                         kModalWidth,
                         kModalHeight};
        metrics.okButton = {metrics.modal.x + metrics.modal.width - 118.0f,
                            metrics.modal.y + metrics.modal.height - 48.0f,
                            84.0f,
                            34.0f};
        metrics.itemX = metrics.modal.x + 28.0f;
        return metrics;
    }

    Rectangle LinkBounds(Font font, const char* label, float yOffset) const
    {
        const Vector2 size = MeasureTextEx(font, label, kAboutLinkFontSize, 0.0f);
        return {itemX, modal.y + yOffset, size.x, size.y + 2.0f};
    }
};

void OpenUrlInBrowser(const char* url)
{
    if (url == nullptr || url[0] == '\0')
    {
        return;
    }
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#else
    const std::string command = "xdg-open " + QuoteShellArgument(url) + " >/dev/null 2>&1 &";
    (void)std::system(command.c_str());
#endif
}

void RevealPathInExplorer(const std::string& pathUtf8)
{
    if (pathUtf8.empty())
    {
        return;
    }

#ifdef _WIN32
    const std::filesystem::path path = std::filesystem::u8path(pathUtf8);
    std::error_code error;
    if (std::filesystem::is_regular_file(path, error))
    {
        const std::wstring native = path.wstring();
        const std::wstring params = L"/select,\"" + native + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }

    std::filesystem::path folder = path;
    if (!std::filesystem::is_directory(folder, error))
    {
        folder = path.parent_path();
    }
    if (!folder.empty() && std::filesystem::is_directory(folder, error))
    {
        ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
#else
    std::error_code error;
    std::filesystem::path path = std::filesystem::u8path(pathUtf8);
    if (std::filesystem::is_regular_file(path, error))
    {
        path = path.parent_path();
    }
    if (path.empty())
    {
        return;
    }
    const std::string command = "xdg-open " + QuoteShellArgument(path.string()) + " >/dev/null 2>&1 &";
    (void)std::system(command.c_str());
#endif
}

void DrawAboutLink(Font font, const char* label, float y, float x, Color normalColor, Color hoverColor)
{
    const Vector2 size = MeasureTextEx(font, label, kAboutLinkFontSize, 0.0f);
    const Rectangle bounds = {x, y, size.x, size.y + 2.0f};
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    const Color color = hovered ? hoverColor : normalColor;
    DrawTextEx(font, label, {x, y}, kAboutLinkFontSize, 0.0f, color);
    if (hovered)
    {
        UiCursor::RequestHand();
        DrawLineEx({x, y + size.y + 1.0f}, {x + size.x, y + size.y + 1.0f}, 1.0f, color);
    }
}

Rectangle GlobalPathLabelBounds(Font font, Rectangle globalPanel, const char* label)
{
    const float maxWidth = std::max(4.0f, globalPanel.width - 20.0f);
    const std::string display = TruncateTextToWidth(font, label != nullptr ? label : "", 16.0f, maxWidth);
    const Vector2 size = MeasureTextEx(font, display.c_str(), 16.0f, 0.0f);
    return {globalPanel.x + 10.0f, globalPanel.y + 10.0f, size.x, std::max(16.0f, size.y + 2.0f)};
}

void DrawGlobalPathLabel(Font font, Rectangle globalPanel, const char* label, Color normalColor)
{
    const float maxWidth = std::max(4.0f, globalPanel.width - 20.0f);
    const std::string display = TruncateTextToWidth(font, label != nullptr ? label : "", 16.0f, maxWidth);
    const Rectangle bounds = {
        globalPanel.x + 10.0f, globalPanel.y + 10.0f, MeasureTextEx(font, display.c_str(), 16.0f, 0.0f).x, 18.0f};
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    const Color color = hovered ? Color{210, 255, 210, 255} : normalColor;
    DrawTextEx(font, display.c_str(), {bounds.x, globalPanel.y + 10.0f}, 16.0f, 0.0f, color);
    if (hovered)
    {
        UiCursor::RequestHand();
    }
}

bool UpdateGlobalPathLabelClick(Font font, Rectangle globalPanel, const char* label, const std::string& path)
{
    if (path.empty())
    {
        return false;
    }
    const Rectangle bounds = GlobalPathLabelBounds(font, globalPanel, label);
    if (CheckCollisionPointRec(GetMousePosition(), bounds) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        RevealPathInExplorer(path);
        return true;
    }
    return false;
}

int MapQualityCapToCardIndex(const std::vector<std::string>& cardQualities, const std::string& qualityCap)
{
    if (cardQualities.empty())
    {
        return 0;
    }
    if (qualityCap.empty() || qualityCap == "Max")
    {
        return 0; // Highest available on the card ladder.
    }

    const int requestedHeight = ParseQualityHeight(qualityCap);
    for (int i = 0; i < static_cast<int>(cardQualities.size()); ++i)
    {
        const int height = ParseQualityHeight(cardQualities[static_cast<size_t>(i)]);
        if (height > 0 && height <= requestedHeight)
        {
            return i;
        }
    }
    return static_cast<int>(cardQualities.size()) - 1;
}

// Fixed group quality caps (always offered; per-video download falls back to highest <= cap).
std::vector<std::string> BuildGroupQualityCapItems()
{
    return {"Max", "4320p", "2160p", "1440p", "1080p", "720p", "480p", "360p"};
}

std::string GroupQualityCapFromOptions(const DownloadOptions& options)
{
    const std::vector<std::string> items = BuildGroupQualityCapItems();
    const int index = std::clamp(options.quality, 0, static_cast<int>(items.size()) - 1);
    return items[static_cast<size_t>(index)];
}
} // namespace

DockArea::DockArea()
    : globalDownloadPath_(GetDefaultDownloadPath())
{
    downloadFoldout_.SetHoverToggleKey(KEY_A);
    autoConvertSectionFoldout_.SetHoverToggleKey(KEY_A);
    autoConvertFoldout_.SetHoverToggleKey(KEY_A);
    customAutoConvertFoldout_.SetHoverToggleKey(KEY_A);
    converterSectionFoldout_.SetHoverToggleKey(KEY_A);
    converterDefaultFoldout_.SetHoverToggleKey(KEY_A);
    converterCustomFoldout_.SetHoverToggleKey(KEY_A);
}

void DockArea::Update(int windowWidth, int windowHeight, Font font)
{
    if ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_R))
    {
        EnableCursor();
        UiCursor::frameCursor = MOUSE_CURSOR_ARROW;
        SetMouseCursor(MOUSE_CURSOR_ARROW);
    }

    const Rectangle leftPanel = GetLeftPanel(windowWidth, windowHeight);
    const Rectangle rightPanel = GetRightPanel(windowWidth, windowHeight);
    const Rectangle insertLinkButton = GetInsertLinkButtonBounds(leftPanel);
    const Rectangle downloadButton = GetDownloadButtonBounds(GetRightSettingsPanel(rightPanel));
    const Rectangle downloadAllButton = GetSecondaryActionButtonBounds(GetRightSettingsPanel(rightPanel));

    for (DownloadRunner& runner : downloadRunners_)
    {
        runner.Update();
    }
    for (ConvertRunner& runner : convertRunners_)
    {
        runner.Update();
    }

    for (ConvertRunner& runner : convertRunners_)
    {
        ProcessFinishedConvertRunner(runner);
    }
    ProcessPendingStagingCleanup();
    UpdateFooterNotificationTimer();
    for (DownloadRunner& runner : downloadRunners_)
    {
        ProcessFinishedDownloadRunner(runner);
    }

    if (!pendingConvertQueue_.empty() && !isOverwritePromptOpen_ && !isAboutDialogOpen_ && !isInfoDialogOpen_ &&
        FirstFreeConvertRunner() != nullptr)
    {
        StartNextPendingConvert();
    }

    if (isAboutDialogOpen_)
    {
        UpdateAboutDialog(windowWidth, windowHeight, font);
        return;
    }

    if (isInfoDialogOpen_)
    {
        UpdateInfoDialog(windowWidth, windowHeight, font);
        return;
    }

    UpdateHeader();

    SyncCardProgress();
    overlayBlocksActions_ = false;

    if (isOverwritePromptOpen_)
    {
        UpdateOverwritePrompt(windowWidth, windowHeight);
        return;
    }

    if (!pendingDownloadQueue_.empty() && !isOverwritePromptOpen_ && FirstFreeDownloadRunner() != nullptr &&
        GetTime() >= nextDownloadStartTime_)
    {
        WriteDebugLog("starting scheduled next download");
        StartNextPendingDownload();
    }

    if (activeWorkspace_ == Workspace::Downloader)
    {
        UpdateDownloaderWorkspace(leftPanel, rightPanel, font);
        if (!overlayBlocksActions_)
        {
            if (SelectedCardShowsCancel())
            {
                if (cancelDownloadButton_.Update(downloadButton))
                {
                    HandleCancelSelectedRequest();
                }
                else if (HasDownloadableIdleCards())
                {
                    if (downloadAllButton_.Update(downloadAllButton))
                    {
                        HandleDownloadAllRequest();
                    }
                }
                else if (cancelAllActionButton_.Update(downloadAllButton))
                {
                    HandleCancelAllDownloadsRequest();
                }
            }
            else if (HasActiveDownloadWorkspaceWork())
            {
                if (downloadButton_.Update(downloadButton, CanDownloadSelected()))
                {
                    HandleDownloadRequest();
                }
                else if (cancelAllActionButton_.Update(downloadAllButton))
                {
                    HandleCancelAllDownloadsRequest();
                }
            }
            else if (downloadButton_.Update(downloadButton, CanDownloadSelected()))
            {
                HandleDownloadRequest();
            }
            else if (downloadAllButton_.Update(downloadAllButton, HasDownloadableIdleCards()))
            {
                HandleDownloadAllRequest();
            }
        }
    }
    else
    {
        UpdateConverterWorkspace(leftPanel, rightPanel, font);
        const Rectangle settingsPanel = GetRightSettingsPanel(rightPanel);
        if (!overlayBlocksActions_)
        {
            const Rectangle convertLeft = GetDownloadButtonBounds(settingsPanel);
            const Rectangle convertRight = GetSecondaryActionButtonBounds(settingsPanel);
            const auto canConvertSelected = [this]()
            {
                return std::any_of(converterCards_.begin(),
                                   converterCards_.end(),
                                   [this](const ConverterFileCardNode& card)
                                   {
                                       if (!card.IsSelected())
                                       {
                                           return false;
                                       }
                                       ConvertRequest request;
                                       return BuildConvertRequestForCard(card, request);
                                   });
            };

            if (SelectedConverterShowsCancel())
            {
                if (cancelDownloadButton_.Update(convertLeft))
                {
                    HandleCancelSelectedConvertsRequest();
                }
                else if (CanBuildAnyConvertRequest())
                {
                    if (convertAllButton_.Update(convertRight))
                    {
                        HandleConvertAllRequest();
                    }
                }
                else if (cancelAllActionButton_.Update(convertRight))
                {
                    HandleCancelAllConvertsRequest();
                }
            }
            else if (HasActiveConverterWorkspaceWork())
            {
                if (convertButton_.Update(convertLeft, canConvertSelected()))
                {
                    HandleConvertRequest();
                }
                else if (cancelAllActionButton_.Update(convertRight))
                {
                    HandleCancelAllConvertsRequest();
                }
            }
            else if (convertButton_.Update(convertLeft, canConvertSelected()))
            {
                HandleConvertRequest();
            }
            else if (convertAllButton_.Update(convertRight, CanBuildAnyConvertRequest()))
            {
                HandleConvertAllRequest();
            }
        }
    }

    if (!isOverwritePromptOpen_)
    {
        CollectParseFailures();
        CollectConverterLoadResults();
        UpdateFooter();
    }
}

void DockArea::Draw(int windowWidth, int windowHeight, Font font, Font fontFooterAa) const
{
    UiCursor::BeginFrame();

    const Color background = {26, 34, 26, 255};
    const Color border = {64, 84, 64, 255};
    const Rectangle leftPanel = GetLeftPanel(windowWidth, windowHeight);
    const Rectangle rightPanel = GetRightPanel(windowWidth, windowHeight);
    const Rectangle header = GetHeader(windowWidth);
    const Rectangle footer = GetFooter(windowWidth, windowHeight);

    const auto drawPanel = [&](Rectangle bounds)
    {
        const float minSide = bounds.width < bounds.height ? bounds.width : bounds.height;
        const float roundness = (kCornerRadius * 2.0f) / minSide;

        DrawRectangleRounded(bounds, roundness, 16, background);
        DrawRectangleRoundedLines(bounds, roundness, 16, border);
    };

    drawPanel(leftPanel);
    if (activeWorkspace_ == Workspace::Downloader)
    {
        drawPanel(GetAutoConvertDockPanel(rightPanel));
    }
    else
    {
        drawPanel(GetConverterDefaultDockPanel(rightPanel));
    }
    drawPanel(GetRightSettingsPanel(rightPanel));
    drawPanel(GetGlobalPathPanel(rightPanel));
    DrawHeader(header, font);

    if (activeWorkspace_ == Workspace::Downloader)
    {
        DrawDownloaderWorkspace(leftPanel, rightPanel, font);
    }
    else
    {
        DrawConverterWorkspace(leftPanel, rightPanel, font);
    }
    DrawFooter(footer, font, fontFooterAa);
    if (isOverwritePromptOpen_ || isAboutDialogOpen_ || isInfoDialogOpen_)
    {
        Tooltip::Clear();
        UiCursor::BeginFrame();
    }
    DrawOverwritePrompt(windowWidth, windowHeight, font);
    DrawAboutDialog(windowWidth, windowHeight, font);
    DrawInfoDialog(windowWidth, windowHeight, font);
    Tooltip::Flush();

    UiCursor::ApplyFrame();
}

void DockArea::UnloadResources()
{
    cards_.clear();
    converterCards_.clear();
}

Rectangle DockArea::GetBounds(int windowWidth, int windowHeight) const
{
    return {kMargin,
            kMargin,
            static_cast<float>(windowWidth) - kMargin * 2.0f,
            static_cast<float>(windowHeight) - kMargin * 2.0f};
}

Rectangle DockArea::GetLeftPanel(int windowWidth, int windowHeight) const
{
    const Rectangle area = GetBounds(windowWidth, windowHeight);
    const float leftWidth = (area.width - kGap) * kLeftPanelRatio;

    return {area.x, area.y + kHeaderHeight, leftWidth, area.height - kHeaderHeight - kFooterHeight};
}

Rectangle DockArea::GetRightPanel(int windowWidth, int windowHeight) const
{
    const Rectangle area = GetBounds(windowWidth, windowHeight);
    const float leftWidth = (area.width - kGap) * kLeftPanelRatio;
    const float rightWidth = area.width - kGap - leftWidth;

    return {area.x + leftWidth + kGap, area.y + kHeaderHeight, rightWidth, area.height - kHeaderHeight - kFooterHeight};
}

Rectangle DockArea::GetHeader(int windowWidth) const
{
    return {0.0f, 0.0f, static_cast<float>(windowWidth), kHeaderHeight};
}

Rectangle DockArea::GetFooter(int windowWidth, int windowHeight) const
{
    return {0.0f, static_cast<float>(windowHeight) - kFooterHeight, static_cast<float>(windowWidth), kFooterHeight};
}

Rectangle DockArea::GetFooterSeedButtonBounds(Rectangle footer) const
{
    const float buttonHeight = footer.height - 4.0f;
    return {footer.x + 6.0f, footer.y + 2.0f, 42.0f, buttonHeight};
}

Rectangle DockArea::GetFooterSeed8kButtonBounds(Rectangle footer) const
{
    const Rectangle seedBounds = GetFooterSeedButtonBounds(footer);
    return {seedBounds.x + seedBounds.width + 4.0f, seedBounds.y, 42.0f, seedBounds.height};
}

Rectangle DockArea::GetRightSettingsPanel(Rectangle rightPanel) const
{
    const float bottomY = rightPanel.y + rightPanel.height - 75.0f;
    if (activeWorkspace_ == Workspace::Downloader)
    {
        const Rectangle autoConvertDock = GetAutoConvertDockPanel(rightPanel);
        const float topY = autoConvertDock.y + autoConvertDock.height + kGap;
        return {rightPanel.x, topY, rightPanel.width, std::max(0.0f, bottomY - topY)};
    }

    const Rectangle defaultConvertDock = GetConverterDefaultDockPanel(rightPanel);
    const float topY = defaultConvertDock.y + defaultConvertDock.height + kGap;
    return {rightPanel.x, topY, rightPanel.width, std::max(0.0f, bottomY - topY)};
}

Rectangle DockArea::GetConverterDefaultDockPanel(Rectangle rightPanel) const
{
    constexpr float kOuterPad = 10.0f;
    constexpr float kMixedHintHeight = 36.0f;
    constexpr float kMixedHintGap = 8.0f;
    const float headerH = FoldoutPanel::HeaderHeight();

    const auto globalFoldoutHeight = [&](bool expanded) -> float
    {
        if (!expanded)
        {
            return headerH;
        }
        return headerH + FoldoutPanel::ContentTopPadding() + kAutoConvertRowStep * 3.0f +
               FoldoutPanel::ContentBottomPadding();
    };
    const bool useDefaultForLayout = converterCardOptionsUseDefault_ && !converterCardOptionsUseDefaultMixed_;
    const bool showCustomMixedHint =
        converterCustomFoldout_.IsExpanded() && converterCardOptionsCustomMixed_ && !useDefaultForLayout;
    const auto customFoldoutHeight = [&](bool expanded) -> float
    {
        if (!expanded)
        {
            return headerH;
        }
        const float mixedExtra = showCustomMixedHint ? (kMixedHintHeight + kMixedHintGap) : 0.0f;
        return headerH + FoldoutPanel::ContentTopPadding() + mixedExtra + kAutoConvertRowStep * 3.0f +
               FoldoutPanel::ContentBottomPadding();
    };

    float sectionH = headerH;
    if (converterSectionFoldout_.IsExpanded())
    {
        const float innerH = globalFoldoutHeight(converterDefaultFoldout_.IsExpanded()) + kAutoConvertSectionGap +
                             customFoldoutHeight(converterCustomFoldout_.IsExpanded());
        sectionH = headerH + FoldoutPanel::ContentTopPadding() + innerH + FoldoutPanel::ContentBottomPadding();
    }
    return {rightPanel.x, rightPanel.y, rightPanel.width, sectionH + kOuterPad * 2.0f};
}

Rectangle DockArea::GetAutoConvertDockPanel(Rectangle rightPanel) const
{
    constexpr float kOuterPad = 10.0f;
    const float headerH = FoldoutPanel::HeaderHeight();
    const auto innerFoldoutHeight = [&](bool expanded) -> float
    {
        if (!expanded)
        {
            return headerH;
        }
        return headerH + FoldoutPanel::ContentTopPadding() + kAutoConvertRowStep * 3.0f +
               FoldoutPanel::ContentBottomPadding();
    };

    float sectionH = headerH;
    if (autoConvertSectionFoldout_.IsExpanded())
    {
        const float innerH = innerFoldoutHeight(autoConvertFoldout_.IsExpanded()) + kAutoConvertSectionGap +
                             kAutoConvertExcludeRowH + kAutoConvertSectionGap +
                             innerFoldoutHeight(customAutoConvertFoldout_.IsExpanded());
        sectionH = headerH + FoldoutPanel::ContentTopPadding() + innerH + FoldoutPanel::ContentBottomPadding();
    }
    return {rightPanel.x, rightPanel.y, rightPanel.width, sectionH + kOuterPad * 2.0f};
}

Rectangle DockArea::GetGlobalPathPanel(Rectangle rightPanel) const
{
    return {rightPanel.x, rightPanel.y + rightPanel.height - 70.0f, rightPanel.width, 70.0f};
}

Rectangle DockArea::GetInsertLinkButtonBounds(Rectangle leftPanel) const
{
    constexpr float kIdealW = 180.0f;
    constexpr float kIdealH = 48.0f;
    constexpr float kPad = 12.0f;
    const float width = std::min(kIdealW, std::max(24.0f, leftPanel.width - kPad * 2.0f));
    const float height = std::min(kIdealH, std::max(24.0f, leftPanel.height - kPad * 2.0f));
    return {leftPanel.x + (leftPanel.width - width) * 0.5f,
            leftPanel.y + (leftPanel.height - height) * 0.5f,
            width,
            height};
}

Rectangle DockArea::GetChooseFileButtonBounds(Rectangle leftPanel) const
{
    return GetInsertLinkButtonBounds(leftPanel);
}

Rectangle DockArea::GetListActionButtonBounds(Rectangle leftPanel, int index, float scrollOffset) const
{
    constexpr float kIdealW = 180.0f;
    constexpr float kIdealH = 48.0f;
    constexpr float kPad = 12.0f;
    const Rectangle slot = activeWorkspace_ == Workspace::Downloader
                               ? GetDownloaderActionSlotBounds(leftPanel, scrollOffset)
                               : GetCardBounds(leftPanel, index, scrollOffset);
    const float width = std::min(kIdealW, std::max(24.0f, slot.width - kPad * 2.0f));
    const float height = std::min(kIdealH, std::max(24.0f, slot.height - 8.0f));
    return {slot.x + (slot.width - width) * 0.5f, slot.y + (slot.height - height) * 0.5f, width, height};
}

Rectangle DockArea::GetDownloadButtonBounds(Rectangle settingsPanel) const
{
    return {settingsPanel.x + 14.0f,
            settingsPanel.y + settingsPanel.height - 34.0f - 12.0f,
            (settingsPanel.width - 42.0f) * 0.5f,
            34.0f};
}

Rectangle DockArea::GetSecondaryActionButtonBounds(Rectangle settingsPanel) const
{
    const float width = (settingsPanel.width - 42.0f) * 0.5f;
    return {settingsPanel.x + 28.0f + width, settingsPanel.y + settingsPanel.height - 34.0f - 12.0f, width, 34.0f};
}

void DockArea::RebuildDownloaderLayoutCache() const
{
    downloaderPrefixHeights_.assign(cards_.size() + 1, 0.0f);
    float running = 0.0f;
    for (size_t index = 0; index < cards_.size(); ++index)
    {
        downloaderPrefixHeights_[index] = running;
        running += cards_[index].Height();
        if (index + 1 < cards_.size() || !cards_.empty())
        {
            running += kGap;
        }
    }
    downloaderPrefixHeights_[cards_.size()] = running;
    downloaderCachedContentHeight_ = running;
    if (!cards_.empty())
    {
        downloaderCachedContentHeight_ += kGap + kCardHeight;
    }
}

float DockArea::GetDownloaderItemHeight(int index) const
{
    if (index < 0 || index >= static_cast<int>(cards_.size()))
    {
        return 0.0f;
    }
    return cards_[index].Height();
}

float DockArea::GetDownloaderListContentHeight() const
{
    RebuildDownloaderLayoutCache();
    return downloaderCachedContentHeight_;
}

float DockArea::GetDownloaderItemTop(Rectangle leftPanel, int index, float scrollOffset) const
{
    RebuildDownloaderLayoutCache();
    const float prefix = index >= 0 && index < static_cast<int>(downloaderPrefixHeights_.size())
                             ? downloaderPrefixHeights_[static_cast<size_t>(index)]
                             : GetDownloaderListContentHeight();
    return leftPanel.y + kMargin + prefix - scrollOffset;
}

float DockArea::GetDownloaderReservedRight(Rectangle leftPanel) const
{
    const float contentHeight = GetDownloaderListContentHeight();
    if (GetMaxCardScroll(leftPanel, contentHeight, 0.0f) <= 0.0f)
    {
        return 0.0f;
    }
    const float gutterFromPanelEdge = kScrollbarEdgePad + kScrollbarTrackWidth + kScrollbarEdgePad;
    return std::max(0.0f, gutterFromPanelEdge - kMargin);
}

Rectangle DockArea::GetDownloaderSingleBounds(Rectangle leftPanel, int index, float scrollOffset) const
{
    const float reservedRight = GetDownloaderReservedRight(leftPanel);
    return {leftPanel.x + kMargin,
            GetDownloaderItemTop(leftPanel, index, scrollOffset),
            leftPanel.width - kMargin * 2.0f - reservedRight,
            kCardHeight};
}

Rectangle DockArea::GetDownloaderGroupHeaderBounds(Rectangle leftPanel, int index, float scrollOffset) const
{
    const float reservedRight = GetDownloaderReservedRight(leftPanel);
    return {leftPanel.x + kMargin,
            GetDownloaderItemTop(leftPanel, index, scrollOffset),
            leftPanel.width - kMargin * 2.0f - reservedRight,
            kCardHeight};
}

Rectangle
DockArea::GetDownloaderGroupChildBounds(Rectangle leftPanel, int itemIndex, int childIndex, float scrollOffset) const
{
    if (itemIndex < 0 || itemIndex >= static_cast<int>(cards_.size()) ||
        cards_[itemIndex].kind != DownloaderListItem::Kind::Group)
    {
        return {};
    }

    const float reservedRight = GetDownloaderReservedRight(leftPanel);
    const float childTop = GetDownloaderItemTop(leftPanel, itemIndex, scrollOffset) + kCardHeight + kGap +
                           static_cast<float>(childIndex) * (kCardHeight + kGap);
    return {leftPanel.x + kMargin + LinkCardGroupNode::kChildIndent,
            childTop,
            leftPanel.width - kMargin * 2.0f - reservedRight - LinkCardGroupNode::kChildIndent,
            kCardHeight};
}

Rectangle DockArea::GetDownloaderGroupLoadMoreBounds(Rectangle leftPanel, int itemIndex, float scrollOffset) const
{
    if (itemIndex < 0 || itemIndex >= static_cast<int>(cards_.size()) ||
        cards_[itemIndex].kind != DownloaderListItem::Kind::Group || !cards_[itemIndex].group->ShowsLoadMore())
    {
        return {};
    }

    const float reservedRight = GetDownloaderReservedRight(leftPanel);
    const LinkCardGroupNode& group = *cards_[itemIndex].group;
    const float top = GetDownloaderItemTop(leftPanel, itemIndex, scrollOffset) + kCardHeight + kGap +
                      static_cast<float>(group.LoadedChildCount()) * (kCardHeight + kGap);
    return {leftPanel.x + kMargin + LinkCardGroupNode::kChildIndent,
            top,
            leftPanel.width - kMargin * 2.0f - reservedRight - LinkCardGroupNode::kChildIndent,
            LinkCardGroupNode::kLoadMoreHeight};
}

Rectangle DockArea::GetDownloaderActionSlotBounds(Rectangle leftPanel, float scrollOffset) const
{
    const int index = static_cast<int>(cards_.size());
    const float reservedRight = GetDownloaderReservedRight(leftPanel);
    return {leftPanel.x + kMargin,
            GetDownloaderItemTop(leftPanel, index, scrollOffset),
            leftPanel.width - kMargin * 2.0f - reservedRight,
            kCardHeight};
}

LinkCardNode* DockArea::FindLinkCardByUrl(const std::string& url)
{
    for (DownloaderListItem& item : cards_)
    {
        if (item.kind == DownloaderListItem::Kind::Single)
        {
            if (item.single->HasUrl(url))
            {
                return item.single.get();
            }
            continue;
        }
        for (LinkCardNode& child : item.group->LoadedCards())
        {
            if (child.HasUrl(url))
            {
                return &child;
            }
        }
    }
    return nullptr;
}

const LinkCardNode* DockArea::FindLinkCardByUrl(const std::string& url) const
{
    for (const DownloaderListItem& item : cards_)
    {
        if (item.kind == DownloaderListItem::Kind::Single)
        {
            if (item.single->HasUrl(url))
            {
                return item.single.get();
            }
            continue;
        }
        for (const LinkCardNode& child : item.group->LoadedCards())
        {
            if (child.HasUrl(url))
            {
                return &child;
            }
        }
    }
    return nullptr;
}

double DockArea::SumCompletedCardDownloadElapsed() const
{
    double total = 0.0;
    ForEachLinkCard(
        [&total](const LinkCardNode& card)
        {
            total += card.DownloadElapsedSeconds();
        });
    return total;
}

void DockArea::ForEachLinkCard(const std::function<void(LinkCardNode&)>& visitor)
{
    for (DownloaderListItem& item : cards_)
    {
        if (item.kind == DownloaderListItem::Kind::Single)
        {
            visitor(*item.single);
            continue;
        }
        for (LinkCardNode& child : item.group->LoadedCards())
        {
            visitor(child);
        }
    }
}

void DockArea::ForEachLinkCard(const std::function<void(const LinkCardNode&)>& visitor) const
{
    for (const DownloaderListItem& item : cards_)
    {
        if (item.kind == DownloaderListItem::Kind::Single)
        {
            visitor(*item.single);
            continue;
        }
        for (const LinkCardNode& child : item.group->LoadedCards())
        {
            visitor(child);
        }
    }
}

int DockArea::CountDownloaderSelectableUnits() const
{
    int count = 0;
    for (const DownloaderListItem& item : cards_)
    {
        if (item.kind == DownloaderListItem::Kind::Single)
        {
            ++count;
            continue;
        }
        ++count;
        count += item.CountSelectableChildren();
    }
    return count;
}

Rectangle DockArea::GetCardBounds(Rectangle leftPanel, int index, float scrollOffset) const
{
    const int itemCount = activeWorkspace_ == Workspace::Downloader
                              ? static_cast<int>(cards_.size()) + (!cards_.empty() ? 1 : 0)
                              : static_cast<int>(converterCards_.size()) + (!converterCards_.empty() ? 1 : 0);
    const float reservedRight = GetCardListScrollbarReserve(leftPanel, itemCount);
    return {leftPanel.x + kMargin,
            leftPanel.y + kMargin + static_cast<float>(index) * (kCardHeight + kGap) - scrollOffset,
            leftPanel.width - kMargin * 2.0f - reservedRight,
            kCardHeight};
}

float DockArea::GetCardListScrollbarReserve(Rectangle leftPanel, int itemCount) const
{
    (void)itemCount;
    if (activeWorkspace_ == Workspace::Downloader)
    {
        if (GetMaxCardScroll(leftPanel, GetDownloaderListContentHeight(), 0.0f) <= 0.0f)
        {
            return 0.0f;
        }
        const float gutterFromPanelEdge = kScrollbarEdgePad + kScrollbarTrackWidth + kScrollbarEdgePad;
        return std::max(0.0f, gutterFromPanelEdge - kMargin);
    }

    const float contentHeight = static_cast<float>(itemCount) * (kCardHeight + kGap);
    if (GetMaxCardScroll(leftPanel, contentHeight, 0.0f) <= 0.0f)
    {
        return 0.0f;
    }

    const float gutterFromPanelEdge = kScrollbarEdgePad + kScrollbarTrackWidth + kScrollbarEdgePad;
    return std::max(0.0f, gutterFromPanelEdge - kMargin);
}

float DockArea::GetMaxCardScroll(Rectangle leftPanel, float contentHeight, float reservedBottom) const
{
    const float visibleHeight = std::max(0.0f, leftPanel.height - kMargin * 2.0f - reservedBottom);
    return std::max(0.0f, contentHeight - visibleHeight);
}

void DockArea::UpdateCardScroll(Rectangle leftPanel,
                                float contentHeight,
                                float reservedBottom,
                                float& scrollOffset) const
{
    const float maxScroll = GetMaxCardScroll(leftPanel, contentHeight, reservedBottom);
    if (contentHeight <= 0.0f)
    {
        scrollOffset = 0.0f;
        return;
    }

    if (CheckCollisionPointRec(GetMousePosition(), leftPanel))
    {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            scrollOffset = std::clamp(scrollOffset - wheel * 46.0f, 0.0f, maxScroll);
        }
    }
    scrollOffset = std::clamp(scrollOffset, 0.0f, maxScroll);
}

void DockArea::UpdateHeader()
{
    const float headerY = kMargin;
    const Vector2 mouse = GetMousePosition();
    const Rectangle aboutBounds = HeaderLayout::AboutButton(headerY);
    const Rectangle infoBounds = HeaderLayout::InfoButton(headerY);
    const Rectangle downloaderTab = HeaderLayout::DownloaderTab(headerY);
    const Rectangle converterTab = HeaderLayout::ConverterTab(headerY);

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, aboutBounds))
    {
        isAboutDialogOpen_ = true;
        return;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, infoBounds))
    {
        isInfoDialogOpen_ = true;
        infoDialogScrollOffset_ = 0.0f;
        return;
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        if (CheckCollisionPointRec(mouse, downloaderTab))
        {
            activeWorkspace_ = Workspace::Downloader;
            if (footerNotificationScope_ == FooterNotificationScope::Converter)
            {
                ClearFooterNotification();
            }
        }
        else if (CheckCollisionPointRec(mouse, converterTab))
        {
            activeWorkspace_ = Workspace::Converter;
            if (footerNotificationScope_ == FooterNotificationScope::Downloader)
            {
                ClearFooterNotification();
            }
        }
    }
}

void DockArea::UpdateDownloaderWorkspace(Rectangle leftPanel, Rectangle rightPanel, Font font)
{
    if (cards_.empty() && insertLinkButton_.Update(GetInsertLinkButtonBounds(leftPanel)))
    {
        HandleInsertLinkRequest();
    }
    else if (!cards_.empty() && insertLinkButton_.Update(GetListActionButtonBounds(
                                    leftPanel, static_cast<int>(cards_.size()), downloaderScrollOffset_)))
    {
        HandleInsertLinkRequest();
    }

    UpdateCardScroll(leftPanel, GetDownloaderListContentHeight(), 0.0f, downloaderScrollOffset_);
    UpdateCards(leftPanel, font);
    const bool upperOverlay = UpdateAutoConvertDock(GetAutoConvertDockPanel(rightPanel), font);
    if (upperOverlay)
    {
        overlayBlocksActions_ = true;
    }
    UpdateRightPanel(rightPanel, font, upperOverlay);
    HandleShortcuts(leftPanel, rightPanel);

    for (int index = static_cast<int>(cards_.size()) - 1; index >= 0; --index)
    {
        if (cards_[index].kind == DownloaderListItem::Kind::Group && cards_[index].group->ShouldPromoteToSingle())
        {
            DownloaderListItem promoted = DownloaderListItem::MakeSingle("");
            promoted.single = std::make_unique<LinkCardNode>(cards_[index].group->TakePromoteSingleInfo());
            promoted.single->SetSelected(cards_[index].group->IsHeaderSelected());
            cards_[index] = std::move(promoted);
            continue;
        }
        if (cards_[index].kind == DownloaderListItem::Kind::Single && cards_[index].single->ShouldClose())
        {
            RecordLinkCardRemoval(index);
        }
        else if (cards_[index].kind == DownloaderListItem::Kind::Group && cards_[index].group->ShouldClose())
        {
            RecordLinkCardRemoval(index);
        }
    }
    cards_.erase(std::remove_if(cards_.begin(),
                                cards_.end(),
                                [](const DownloaderListItem& item)
                                {
                                    if (item.kind == DownloaderListItem::Kind::Single)
                                    {
                                        return item.single != nullptr && item.single->ShouldClose();
                                    }
                                    return item.group != nullptr && item.group->ShouldClose();
                                }),
                 cards_.end());
    FlushPendingBatchLinkRemoveUndo();
}

void DockArea::UpdateConverterWorkspace(Rectangle leftPanel, Rectangle rightPanel, Font font)
{
    const Rectangle globalPanel = GetGlobalPathPanel(rightPanel);
    const Rectangle globalPathBounds = {globalPanel.x + 10.0f, globalPanel.y + 34.0f, globalPanel.width - 20.0f, 26.0f};
    const std::string globalPathBefore = globalDownloadPath_;
    UpdateGlobalPathLabelClick(font, globalPanel, "Global Output Path", globalDownloadPath_);
    globalPathField_.Update(globalPathBounds, font, globalDownloadPath_, true);
    PushUndo(MakeGlobalPathCommand(globalPathBefore, globalDownloadPath_));

    const int converterItemCount = static_cast<int>(converterCards_.size()) + (!converterCards_.empty() ? 1 : 0);
    UpdateCardScroll(
        leftPanel, static_cast<float>(converterItemCount) * (kCardHeight + kGap), 0.0f, converterScrollOffset_);

    const Rectangle chooseBounds =
        converterCards_.empty()
            ? GetChooseFileButtonBounds(leftPanel)
            : GetListActionButtonBounds(leftPanel, static_cast<int>(converterCards_.size()), converterScrollOffset_);
    if (chooseFileButton_.Update(chooseBounds))
    {
        HandleChooseFileRequest();
    }

    int clickedIndex = -1;
    for (int index = 0; index < static_cast<int>(converterCards_.size()); ++index)
    {
        converterCards_[index].Update(GetCardBounds(leftPanel, index, converterScrollOffset_), font);
        if (converterCards_[index].WasCopyClicked())
        {
            ShowFooterNotification("Info Copied to Clipboard", FooterNotificationScope::Converter);
            continue;
        }
        if (converterCards_[index].WasOpenPathClicked())
        {
            RevealPathInExplorer(converterCards_[index].ResolvePathForReveal());
            continue;
        }
        const std::string inputPath = converterCards_[index].Info().filePath;
        if (converterCards_[index].WasConvertCancelClicked() && FindConvertRunnerByPath(inputPath) != nullptr)
        {
            CancelConverterCard(inputPath);
            continue;
        }
        if (converterCards_[index].ShouldClose())
        {
            // Always drop queued work and cancel any runner for this path, even if the
            // card flag already cleared вЂ” otherwise Cancel stays up with an empty list.
            RemovePendingConvertsForPath(inputPath);
            if (ConvertRunner* runner = FindConvertRunnerByPath(inputPath))
            {
                if (converterCards_[index].IsConverting())
                {
                    converterCards_[index].ClearConverting();
                }
                runner->Cancel();
            }
            else if (converterCards_[index].IsConverting())
            {
                converterCards_[index].ClearConverting();
            }
        }
        if (converterCards_[index].WasClicked())
        {
            clickedIndex = index;
        }
    }

    if (clickedIndex >= 0)
    {
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (shift && lastConverterSelectionAnchor_ >= 0)
        {
            const int a = std::min(lastConverterSelectionAnchor_, clickedIndex);
            const int b = std::max(lastConverterSelectionAnchor_, clickedIndex);
            for (int index = 0; index < static_cast<int>(converterCards_.size()); ++index)
            {
                converterCards_[index].SetSelected(index >= a && index <= b);
            }
            lastConverterSelectionFocus_ = clickedIndex;
        }
        else if (ctrl)
        {
            converterCards_[clickedIndex].SetSelected(!converterCards_[clickedIndex].IsSelected());
            lastConverterSelectionAnchor_ = clickedIndex;
            lastConverterSelectionFocus_ = clickedIndex;
        }
        else
        {
            for (int index = 0; index < static_cast<int>(converterCards_.size()); ++index)
            {
                converterCards_[index].SetSelected(index == clickedIndex);
            }
            lastConverterSelectionAnchor_ = clickedIndex;
            lastConverterSelectionFocus_ = clickedIndex;
        }
    }
    else if (!converterCards_.empty() && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
             CheckCollisionPointRec(GetMousePosition(), leftPanel) &&
             !CheckCollisionPointRec(GetMousePosition(), chooseBounds))
    {
        bool clickedCard = false;
        for (int index = 0; index < static_cast<int>(converterCards_.size()); ++index)
        {
            if (CheckCollisionPointRec(GetMousePosition(), GetCardBounds(leftPanel, index, converterScrollOffset_)))
            {
                clickedCard = true;
                break;
            }
        }
        if (!clickedCard)
        {
            for (ConverterFileCardNode& card : converterCards_)
            {
                card.SetSelected(false);
            }
        }
    }

    const bool upperOverlay = UpdateConverterDefaultDock(GetConverterDefaultDockPanel(rightPanel), font);
    if (upperOverlay)
    {
        overlayBlocksActions_ = true;
    }
    UpdateConverterCardOptions(GetRightSettingsPanel(rightPanel), font, upperOverlay);

    HandleShortcuts(leftPanel, rightPanel);

    for (int index = 0; index < static_cast<int>(converterCards_.size()); ++index)
    {
        if (converterCards_[index].ShouldClose())
        {
            RecordConverterCardRemoval(index);
        }
    }
    converterCards_.erase(std::remove_if(converterCards_.begin(),
                                         converterCards_.end(),
                                         [](const ConverterFileCardNode& card)
                                         {
                                             return card.ShouldClose();
                                         }),
                          converterCards_.end());
    SyncConverterBusyStateAfterCardChanges();
    FlushPendingBatchConverterRemoveUndo();
}

void DockArea::DrawDownloaderWorkspace(Rectangle leftPanel, Rectangle rightPanel, Font font) const
{
    if (cards_.empty())
    {
        insertLinkButton_.Draw(GetInsertLinkButtonBounds(leftPanel), font);
    }

    UiClip::Push(leftPanel);
    bool anyCustomEnabled = false;
    ForEachLinkCard(
        [&anyCustomEnabled](const LinkCardNode& card)
        {
            if (card.CustomAutoConvert().enabled)
            {
                anyCustomEnabled = true;
            }
        });

    int singleDisplayIndex = 0;
    for (int index = 0; index < static_cast<int>(cards_.size()); ++index)
    {
        const DownloaderListItem& item = cards_[index];
        if (item.kind == DownloaderListItem::Kind::Single)
        {
            const bool highlightCustom =
                item.single->CustomAutoConvert().enabled && !item.single->IsExcludedFromAutoConvert();
            item.single->Draw(GetDownloaderSingleBounds(leftPanel, index, downloaderScrollOffset_),
                              font,
                              globalAutoConvert_.enabled || anyCustomEnabled,
                              highlightCustom,
                              ++singleDisplayIndex);
            continue;
        }

        const LinkCardGroupNode& group = *item.group;
        const Rectangle headerBounds = GetDownloaderGroupHeaderBounds(leftPanel, index, downloaderScrollOffset_);
        group.DrawStackPeeks(headerBounds);
        group.DrawHeader(headerBounds, font);
        if (group.IsExpanded())
        {
            float contentBottom = headerBounds.y + headerBounds.height;
            for (int childIndex = 0; childIndex < group.LoadedChildCount(); ++childIndex)
            {
                const LinkCardNode& child = group.LoadedCards()[static_cast<size_t>(childIndex)];
                const Rectangle childBounds =
                    GetDownloaderGroupChildBounds(leftPanel, index, childIndex, downloaderScrollOffset_);
                contentBottom = childBounds.y + childBounds.height;
                const bool highlightCustom = child.CustomAutoConvert().enabled && !child.IsExcludedFromAutoConvert();
                // Local 1-based index within this playlist/channel card.
                child.Draw(
                    childBounds, font, globalAutoConvert_.enabled || anyCustomEnabled, highlightCustom, childIndex + 1);
            }
            group.DrawRail(headerBounds, contentBottom);
            if (group.ShowsLoadMore())
            {
                group.DrawLoadMore(GetDownloaderGroupLoadMoreBounds(leftPanel, index, downloaderScrollOffset_), font);
            }
        }
    }

    if (!cards_.empty())
    {
        insertLinkButton_.Draw(
            GetListActionButtonBounds(leftPanel, static_cast<int>(cards_.size()), downloaderScrollOffset_), font);
    }
    UiClip::Pop();

    if (!cards_.empty())
    {
        const float maxScroll = GetMaxCardScroll(leftPanel, GetDownloaderListContentHeight(), 0.0f);
        const Rectangle cardViewport = {
            leftPanel.x, leftPanel.y + kMargin, leftPanel.width, std::max(0.0f, leftPanel.height - kMargin * 2.0f)};
        DrawOptionsScrollbar(cardViewport, downloaderScrollOffset_, maxScroll);
    }

    DrawAutoConvertDock(GetAutoConvertDockPanel(rightPanel), font, true, false);
    DrawRightPanel(rightPanel, font);
    DrawAutoConvertDock(GetAutoConvertDockPanel(rightPanel), font, false, true);
}

void DockArea::DrawConverterWorkspace(Rectangle leftPanel, Rectangle rightPanel, Font font) const
{
    if (converterCards_.empty())
    {
        chooseFileButton_.Draw(GetChooseFileButtonBounds(leftPanel), font);
    }

    const Rectangle converterListClip = {leftPanel.x, leftPanel.y, leftPanel.width, leftPanel.height};
    UiClip::Push(converterListClip);
    for (int index = 0; index < static_cast<int>(converterCards_.size()); ++index)
    {
        converterCards_[index].Draw(GetCardBounds(leftPanel, index, converterScrollOffset_), font, index + 1);
    }
    if (!converterCards_.empty())
    {
        chooseFileButton_.Draw(
            GetListActionButtonBounds(leftPanel, static_cast<int>(converterCards_.size()), converterScrollOffset_),
            font);
    }
    UiClip::Pop();

    if (!converterCards_.empty())
    {
        const int itemCount = static_cast<int>(converterCards_.size()) + 1;
        const float maxScroll = GetMaxCardScroll(leftPanel, static_cast<float>(itemCount) * (kCardHeight + kGap), 0.0f);
        const Rectangle cardViewport = {
            leftPanel.x, leftPanel.y + kMargin, leftPanel.width, std::max(0.0f, leftPanel.height - kMargin * 2.0f)};
        DrawOptionsScrollbar(cardViewport, converterScrollOffset_, maxScroll);
    }

    DrawConverterDefaultDock(GetConverterDefaultDockPanel(rightPanel), font, true, false);
    DrawConverterCardOptions(GetRightSettingsPanel(rightPanel), font);
    DrawConverterDefaultDock(GetConverterDefaultDockPanel(rightPanel), font, false, true);

    const Rectangle globalPanel = GetGlobalPathPanel(rightPanel);
    const Color text = {224, 230, 224, 255};
    DrawGlobalPathLabel(font, globalPanel, "Global Output Path", text);
    globalPathField_.Draw({globalPanel.x + 10.0f, globalPanel.y + 34.0f, globalPanel.width - 20.0f, 26.0f},
                          font,
                          globalDownloadPath_,
                          true);
}

void DockArea::HandleChooseFileRequest()
{
    const std::string startDirectory = ResolveChooseFileStartDirectory(lastChooseFileDirectory_);
    const std::vector<std::string> paths = ChooseMediaFiles(startDirectory);
    if (paths.empty())
    {
        return;
    }

    const std::string chosenDirectory = ParentDirectoryUtf8(paths.front());
    if (!chosenDirectory.empty())
    {
        lastChooseFileDirectory_ = chosenDirectory;
    }

    for (ConverterFileCardNode& card : converterCards_)
    {
        card.SetSelected(false);
    }

    std::vector<int> newlyAddedIndices;
    for (const std::string& path : paths)
    {
        bool foundExisting = false;
        for (ConverterFileCardNode& card : converterCards_)
        {
            if (card.HasFilePath(path))
            {
                card.TriggerPulse();
                foundExisting = true;
                break;
            }
        }
        if (foundExisting)
        {
            continue;
        }

        converterCards_.emplace_back();
        converterCards_.back().StartLoading(path);
        newlyAddedIndices.push_back(static_cast<int>(converterCards_.size()) - 1);
    }

    for (int index : newlyAddedIndices)
    {
        converterCards_[index].SetSelected(true);
    }
    if (!newlyAddedIndices.empty())
    {
        lastConverterSelectionAnchor_ = newlyAddedIndices.front();
        lastConverterSelectionFocus_ = newlyAddedIndices.back();
    }
}

void DockArea::HandleInsertLinkRequest(bool allowDuplicate)
{
    const char* clipboard = GetClipboardText();
    const char* url = clipboard == nullptr ? "" : clipboard;

    if (!allowDuplicate)
    {
        for (const DownloaderListItem& item : cards_)
        {
            if (item.HasUrl(url))
            {
                if (item.kind == DownloaderListItem::Kind::Single)
                {
                    const_cast<DownloaderListItem&>(item).single->TriggerPulse();
                }
                return;
            }
        }
    }

    for (DownloaderListItem& item : cards_)
    {
        item.ClearSelection();
    }
    cards_.push_back(DownloaderListItem::MakeFromUrl(url));
    lastDownloaderSelectionAnchor_ = static_cast<int>(cards_.size()) - 1;
    lastDownloaderSelectionFocus_ = lastDownloaderSelectionAnchor_;
    if (cards_.back().kind == DownloaderListItem::Kind::Single)
    {
        cards_.back().single->SetSelected(true);
        if (AnyDownloadRunning() || isBatchDownloading_)
        {
            cards_.back().single->SetNotInQueue();
        }
    }
    else
    {
        cards_.back().group->SetHeaderSelected(true);
    }
}

void DockArea::HandleSeedTestLinksRequest()
{
    static const char* kTestLinks[] = {
        "https://www.youtube.com/watch?v=9dcVOmEQzKA", // Just Lose It ~4:04
        "https://www.youtube.com/watch?v=YVkUvmDQ3HY", // Without Me ~4:58
        "https://www.youtube.com/watch?v=sNPnbI1arSE", // My Name Is ~4:08
        "https://www.youtube.com/watch?v=eJO5HU_7_1w", // The Real Slim Shady ~4:44
        "https://www.youtube.com/watch?v=r_0JjYUe5jo", // Godzilla ~4:26
        "https://www.youtube.com/watch?v=8CdcCD5V-d8", // Venom ~4:56
        "https://www.youtube.com/watch?v=S9bCLPwzSC0", // Mockingbird ~4:18
        "https://www.youtube.com/watch?v=j5-yKhDd64s", // Not Afraid ~4:19
        "https://www.youtube.com/watch?v=uelHwf8o7_U", // Love The Way You Lie ~4:27
        "https://www.youtube.com/watch?v=JByDbPn6A1o", // Space Bound ~4:25
    };

    activeWorkspace_ = Workspace::Downloader;
    if (footerNotificationScope_ == FooterNotificationScope::Converter)
    {
        ClearFooterNotification();
    }

    for (DownloaderListItem& item : cards_)
    {
        item.ClearSelection();
    }

    int firstNewIndex = -1;
    int lastNewIndex = -1;
    for (const char* url : kTestLinks)
    {
        bool exists = false;
        for (DownloaderListItem& item : cards_)
        {
            if (item.kind == DownloaderListItem::Kind::Single && item.single->HasUrl(url))
            {
                item.single->TriggerPulse();
                item.single->SetSelected(true);
                exists = true;
                break;
            }
        }
        if (exists)
        {
            continue;
        }

        cards_.push_back(DownloaderListItem::MakeSingle(url));
        cards_.back().single->SetSelected(true);
        if (AnyDownloadRunning() || isBatchDownloading_)
        {
            cards_.back().single->SetNotInQueue();
        }
        lastNewIndex = static_cast<int>(cards_.size()) - 1;
        if (firstNewIndex < 0)
        {
            firstNewIndex = lastNewIndex;
        }
    }

    if (lastNewIndex >= 0)
    {
        lastDownloaderSelectionAnchor_ = firstNewIndex >= 0 ? firstNewIndex : lastNewIndex;
        lastDownloaderSelectionFocus_ = lastNewIndex;
    }

    ShowFooterNotification("Added 10 test links.", FooterNotificationScope::Downloader);
}

void DockArea::HandleSeed8kLinkRequest()
{
    static const char* kUrl = "https://youtu.be/0aVfL_4_NLw";

    activeWorkspace_ = Workspace::Downloader;
    if (footerNotificationScope_ == FooterNotificationScope::Converter)
    {
        ClearFooterNotification();
    }

    for (DownloaderListItem& item : cards_)
    {
        if (item.kind == DownloaderListItem::Kind::Single && item.single->HasUrl(kUrl))
        {
            for (DownloaderListItem& other : cards_)
            {
                other.ClearSelection();
            }
            item.single->TriggerPulse();
            item.single->SetSelected(true);
            ShowFooterNotification("8K test link already added.", FooterNotificationScope::Downloader);
            return;
        }
    }

    for (DownloaderListItem& item : cards_)
    {
        item.ClearSelection();
    }
    cards_.push_back(DownloaderListItem::MakeSingle(kUrl));
    cards_.back().single->SetSelected(true);
    lastDownloaderSelectionAnchor_ = static_cast<int>(cards_.size()) - 1;
    lastDownloaderSelectionFocus_ = lastDownloaderSelectionAnchor_;
    if (AnyDownloadRunning() || isBatchDownloading_)
    {
        cards_.back().single->SetNotInQueue();
    }
    ShowFooterNotification("Added 8K test link.", FooterNotificationScope::Downloader);
}

void DockArea::OnCardClosed(const std::string& url)
{
    softPreemptRequeueUrls_.erase(url);
    pendingDownloadQueue_.erase(std::remove_if(pendingDownloadQueue_.begin(),
                                               pendingDownloadQueue_.end(),
                                               [&](const DownloadRequest& request)
                                               {
                                                   return request.url == url;
                                               }),
                                pendingDownloadQueue_.end());

    if (isOverwritePromptOpen_ && pendingOverwriteRequest_.url == url)
    {
        pendingOverwriteRequest_ = {};
        pendingOverwriteFileName_.clear();
        isOverwritePromptOpen_ = false;
        if (isBatchDownloading_ && !pendingDownloadQueue_.empty())
        {
            nextDownloadStartTime_ = GetTime();
            ShowFooterNotification("Skipped existing file.", FooterNotificationScope::Downloader);
        }
        else if (!isBatchDownloading_)
        {
            isBatchDownloading_ = false;
            overwriteAllExisting_ = false;
            ShowFooterNotification("Download cancelled.", FooterNotificationScope::Downloader);
        }
    }

    if (DownloadRunner* runner = FindDownloadRunnerByUrl(url))
    {
        if (!isBatchDownloading_)
        {
            pendingDownloadQueue_.clear();
        }
        runner->Cancel();
    }

    ForEachLinkCard(
        [&](LinkCardNode& card)
        {
            if (card.HasUrl(url) && card.IsConverting())
            {
                CancelLinkCardConvert(card.LastDownloadedPath());
                return;
            }
        });
}

void DockArea::SyncCardProgress()
{
    for (const DownloadRunner& runner : downloadRunners_)
    {
        if (!runner.IsRunning())
        {
            continue;
        }
        const std::string& currentUrl = runner.CurrentUrl();
        const float progress = runner.Progress();
        const bool merging = runner.Phase() == DownloadSharedState::Phase::Merging;
        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                if (card.IsDownloading() && card.HasUrl(currentUrl))
                {
                    card.SetOperationProgress(progress);
                    card.SetBusyStatusLabel(merging ? "merging" : "downloading");
                }
            });
    }
    if (!AnyDownloadRunning())
    {
        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                if (!card.IsDownloading() && !card.IsConverting())
                {
                    card.ClearOperationProgress();
                }
            });
    }

    for (const ConvertRunner& runner : convertRunners_)
    {
        if (!runner.IsRunning())
        {
            continue;
        }
        const std::string& currentPath = runner.CurrentInputPath();
        const float progress = runner.Progress();
        const double elapsed = runner.ElapsedSeconds();
        for (ConverterFileCardNode& card : converterCards_)
        {
            if (card.HasFilePath(currentPath))
            {
                card.SetOperationProgress(progress);
                card.SetConvertingElapsed(elapsed);
            }
        }
        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                if (card.IsConverting() && card.HasDownloadedPath(currentPath))
                {
                    card.SetOperationProgress(progress);
                }
            });
    }
    for (ConverterFileCardNode& card : converterCards_)
    {
        if (!card.IsConverting())
        {
            card.ClearOperationProgress();
        }
    }

    float progressSum = 0.0f;
    int progressCount = 0;
    for (const DownloadRunner& runner : downloadRunners_)
    {
        if (runner.IsRunning())
        {
            progressSum += runner.Progress();
            ++progressCount;
        }
    }
    for (const ConvertRunner& runner : convertRunners_)
    {
        if (runner.IsRunning())
        {
            progressSum += runner.Progress();
            ++progressCount;
        }
    }
    if (progressCount > 0)
    {
        TaskbarProgress::SetProgress(progressSum / static_cast<float>(progressCount));
    }
    else
    {
        TaskbarProgress::SetProgress(-1.0f);
    }
}

void DockArea::UpdateCards(Rectangle leftPanel, Font font)
{
    struct ClickTarget
    {
        int itemIndex = -1;
        int childIndex = -1;
        bool isHeader = false;
    };
    ClickTarget clicked{};

    const auto processCard = [&](LinkCardNode& card)
    {
        if (card.WasCopyClicked())
        {
            ShowFooterNotification("Info Copied to Clipboard");
            return;
        }
        if (card.WasOpenPathClicked())
        {
            RevealPathInExplorer(card.ResolveOutputPathForReveal());
            return;
        }
        if (card.WasSourceClicked())
        {
            OpenUrlInBrowser(card.Url().c_str());
            return;
        }
        if (card.WasDownloadCancelClicked())
        {
            const std::string url = card.Url();
            if (DownloadRunner* runner = FindDownloadRunnerByUrl(url))
            {
                card.ClearDownloading();
                card.ClearAutoConvertSnapshot();
                card.ClearAutoConvertDelivery();
                if (!isBatchDownloading_)
                {
                    pendingDownloadQueue_.clear();
                }
                runner->Cancel();
            }
            return;
        }
        if (card.WasConvertCancelClicked())
        {
            CancelLinkCardConvert(card.LastDownloadedPath());
            return;
        }
        if (card.WasPrioritizeClicked())
        {
            PrioritizeDownload(card.Url());
            return;
        }
        if (card.WasRedownloadClicked() || card.WasQueueDownloadClicked())
        {
            card.EnsureDetailedParse();
            DownloadRequest request;
            if (BuildDownloadRequestForCard(card, request))
            {
                if (!AnyDownloadRunning() && !isBatchDownloading_)
                {
                    ClearFooterNotification();
                    overwriteAllExisting_ = false;
                }
                card.SetQueued();
                pendingDownloadQueue_.push_back(std::move(request));
                isBatchDownloading_ = true;
                nextDownloadStartTime_ = GetTime();
                StartNextPendingDownload();
            }
        }
    };

    for (int index = 0; index < static_cast<int>(cards_.size()); ++index)
    {
        DownloaderListItem& item = cards_[index];
        if (item.kind == DownloaderListItem::Kind::Single)
        {
            LinkCardNode& card = *item.single;
            card.Update(GetDownloaderSingleBounds(leftPanel, index, downloaderScrollOffset_), font);
            processCard(card);
            if (card.ShouldClose())
            {
                OnCardClosed(card.Url());
            }
            if (card.WasClicked())
            {
                clicked = {index, -1, false};
            }
            continue;
        }

        LinkCardGroupNode& group = *item.group;
        const Rectangle headerBounds = GetDownloaderGroupHeaderBounds(leftPanel, index, downloaderScrollOffset_);
        group.Update(headerBounds, font);
        group.TryToggleExpandShortcut();

        if (group.WasCopyClicked())
        {
            ShowFooterNotification("Info Copied to Clipboard");
        }
        if (group.WasSourceClicked())
        {
            OpenUrlInBrowser(group.Url().c_str());
        }
        if (group.ShouldClose())
        {
            OnCardClosed(group.Url());
        }
        if (group.WasHeaderClicked() && !group.WasExpandToggleClicked())
        {
            clicked = {index, -1, true};
        }

        if (group.IsExpanded())
        {
            for (int childIndex = 0; childIndex < group.LoadedChildCount(); ++childIndex)
            {
                LinkCardNode& child = group.LoadedCards()[static_cast<size_t>(childIndex)];
                child.Update(GetDownloaderGroupChildBounds(leftPanel, index, childIndex, downloaderScrollOffset_),
                             font);
                processCard(child);
                if (child.ShouldClose())
                {
                    OnCardClosed(child.Url());
                    child.RequestClose();
                }
                if (child.WasClicked())
                {
                    clicked = {index, childIndex, false};
                }
            }

            const Rectangle loadMoreBounds =
                GetDownloaderGroupLoadMoreBounds(leftPanel, index, downloaderScrollOffset_);
            if (loadMoreBounds.width > 0.0f && CheckCollisionPointRec(GetMousePosition(), loadMoreBounds) &&
                IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            {
                group.LoadNextPage();
            }
        }
    }

    if (clicked.itemIndex >= 0)
    {
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (shift && lastDownloaderSelectionAnchor_ >= 0)
        {
            const int a = std::min(lastDownloaderSelectionAnchor_, clicked.itemIndex);
            const int b = std::max(lastDownloaderSelectionAnchor_, clicked.itemIndex);
            for (int index = 0; index < static_cast<int>(cards_.size()); ++index)
            {
                cards_[index].ClearSelection();
                if (index >= a && index <= b)
                {
                    if (cards_[index].kind == DownloaderListItem::Kind::Single)
                    {
                        cards_[index].single->SetSelected(true);
                    }
                    else
                    {
                        cards_[index].group->SetHeaderSelected(true);
                    }
                }
            }
            lastDownloaderSelectionFocus_ = clicked.itemIndex;
        }
        else if (ctrl)
        {
            DownloaderListItem& item = cards_[clicked.itemIndex];
            if (clicked.isHeader || item.kind == DownloaderListItem::Kind::Group)
            {
                if (clicked.childIndex < 0)
                {
                    item.group->SetHeaderSelected(!item.group->IsHeaderSelected());
                }
                else
                {
                    LinkCardNode& child = item.group->LoadedCards()[static_cast<size_t>(clicked.childIndex)];
                    child.SetSelected(!child.IsSelected());
                }
            }
            else
            {
                item.single->SetSelected(!item.single->IsSelected());
            }
            lastDownloaderSelectionAnchor_ = clicked.itemIndex;
            lastDownloaderSelectionFocus_ = clicked.itemIndex;
        }
        else
        {
            for (DownloaderListItem& listItem : cards_)
            {
                listItem.ClearSelection();
            }
            DownloaderListItem& item = cards_[clicked.itemIndex];
            if (item.kind == DownloaderListItem::Kind::Single)
            {
                item.single->SetSelected(true);
                item.single->EnsureDetailedParse();
            }
            else if (clicked.childIndex >= 0)
            {
                item.group->LoadedCards()[static_cast<size_t>(clicked.childIndex)].SetSelected(true);
                item.group->LoadedCards()[static_cast<size_t>(clicked.childIndex)].EnsureDetailedParse();
            }
            else
            {
                item.group->SetHeaderSelected(true);
            }
            lastDownloaderSelectionAnchor_ = clicked.itemIndex;
            lastDownloaderSelectionFocus_ = clicked.itemIndex;
        }
    }
    else if (!cards_.empty() && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
             CheckCollisionPointRec(GetMousePosition(), leftPanel))
    {
        const Rectangle insertBounds =
            GetListActionButtonBounds(leftPanel, static_cast<int>(cards_.size()), downloaderScrollOffset_);
        if (!CheckCollisionPointRec(GetMousePosition(), insertBounds))
        {
            bool clickedAny = false;
            for (int index = 0; index < static_cast<int>(cards_.size()); ++index)
            {
                if (cards_[index].kind == DownloaderListItem::Kind::Single)
                {
                    if (CheckCollisionPointRec(GetMousePosition(),
                                               GetDownloaderSingleBounds(leftPanel, index, downloaderScrollOffset_)))
                    {
                        clickedAny = true;
                        break;
                    }
                    continue;
                }
                const Rectangle header = GetDownloaderGroupHeaderBounds(leftPanel, index, downloaderScrollOffset_);
                if (CheckCollisionPointRec(GetMousePosition(), header))
                {
                    clickedAny = true;
                    break;
                }
                if (cards_[index].group->IsExpanded())
                {
                    for (int childIndex = 0; childIndex < cards_[index].group->LoadedChildCount(); ++childIndex)
                    {
                        if (CheckCollisionPointRec(
                                GetMousePosition(),
                                GetDownloaderGroupChildBounds(leftPanel, index, childIndex, downloaderScrollOffset_)))
                        {
                            clickedAny = true;
                            break;
                        }
                    }
                }
                if (clickedAny)
                {
                    break;
                }
            }
            if (!clickedAny)
            {
                for (DownloaderListItem& item : cards_)
                {
                    item.ClearSelection();
                }
            }
        }
    }
}

void DockArea::CloseLinkCardAt(int index)
{
    if (index < 0 || index >= static_cast<int>(cards_.size()))
    {
        return;
    }

    if (cards_[index].kind == DownloaderListItem::Kind::Single)
    {
        LinkCardNode& card = *cards_[index].single;
        if (card.IsDownloading())
        {
            card.ClearDownloading();
            card.ClearAutoConvertSnapshot();
            card.ClearAutoConvertDelivery();
        }
        OnCardClosed(card.Url());
        card.RequestClose();
        return;
    }

    LinkCardGroupNode& group = *cards_[index].group;

    // If we are hovering a specific child card inside the group header, delete only that child.
    LinkCardNode* hoveredChild = nullptr;
    for (LinkCardNode& child : group.LoadedCards())
    {
        if (child.IsHovered())
        {
            hoveredChild = &child;
            break;
        }
    }

    if (hoveredChild != nullptr && !group.IsHeaderHovered())
    {
        LinkGroupChildUndoSnapshot snapshot;
        snapshot.groupUrl = group.Url();
        snapshot.groupHeaderSelected = group.IsHeaderSelected();

        snapshot.childUrl = hoveredChild->Url();
        snapshot.info = hoveredChild->Info();
        snapshot.options = hoveredChild->Options();
        snapshot.lastDownloadedPath = hoveredChild->LastDownloadedPath();
        snapshot.selected = hoveredChild->IsSelected();
        snapshot.excludeFromAutoConvert = hoveredChild->IsExcludedFromAutoConvert();
        snapshot.customAutoConvert = hoveredChild->CustomAutoConvert();

        if (hoveredChild->IsDownloading())
        {
            hoveredChild->ClearDownloading();
            hoveredChild->ClearAutoConvertSnapshot();
            hoveredChild->ClearAutoConvertDelivery();
        }

        // Stop/cancel any active queue/runner/convert first (OnCardClosed also removes it from pending queues).
        OnCardClosed(snapshot.childUrl);

        LinkGroupEntry removedEntry;
        size_t removedIndex = 0;
        if (group.RemoveEntryByUrl(snapshot.childUrl, removedEntry, removedIndex))
        {
            snapshot.entry = removedEntry;
            snapshot.entryIndex = removedIndex;
            PushUndo(MakeRemoveLinkGroupChildCommand(std::move(snapshot)));
        }
        return;
    }

    // Otherwise delete the whole group card.
    for (LinkCardNode& child : group.LoadedCards())
    {
        if (child.IsDownloading())
        {
            child.ClearDownloading();
            child.ClearAutoConvertSnapshot();
            child.ClearAutoConvertDelivery();
        }
        OnCardClosed(child.Url());
    }
    OnCardClosed(group.Url());
    group.RequestClose();
}

void DockArea::CloseConverterCardAt(int index)
{
    if (index < 0 || index >= static_cast<int>(converterCards_.size()))
    {
        return;
    }

    ConverterFileCardNode& card = converterCards_[index];
    const std::string inputPath = card.Info().filePath;
    if (card.IsLoading())
    {
        card.RequestClose();
        return;
    }

    if (card.IsConverting() || FindConvertRunnerByPath(inputPath) != nullptr)
    {
        CancelConverterCard(inputPath);
    }
    else
    {
        RemovePendingConvertsForPath(inputPath);
    }
    card.RequestClose();
}

LinkCardUndoSnapshot DockArea::CaptureLinkCardSnapshot(int index) const
{
    LinkCardUndoSnapshot snapshot;
    if (index < 0 || index >= static_cast<int>(cards_.size()))
    {
        return snapshot;
    }

    const DownloaderListItem& item = cards_[index];
    snapshot.index = index;
    if (item.kind == DownloaderListItem::Kind::Single)
    {
        const LinkCardNode& card = *item.single;
        snapshot.info = card.Info();
        snapshot.options = card.Options();
        snapshot.lastDownloadedPath = card.LastDownloadedPath();
        snapshot.selected = card.IsSelected();
        snapshot.excludeFromAutoConvert = card.IsExcludedFromAutoConvert();
        snapshot.customAutoConvert = card.CustomAutoConvert();
        return snapshot;
    }

    snapshot.info.url = item.group->Url();
    snapshot.info.title = item.group->Title();
    snapshot.info.success = item.group->IsValid();
    snapshot.selected = item.group->IsHeaderSelected();
    return snapshot;
}

ConverterCardUndoSnapshot DockArea::CaptureConverterCardSnapshot(int index) const
{
    ConverterCardUndoSnapshot snapshot;
    if (index < 0 || index >= static_cast<int>(converterCards_.size()))
    {
        return snapshot;
    }

    const ConverterFileCardNode& card = converterCards_[index];
    snapshot.index = index;
    snapshot.info = card.Info();
    snapshot.selected = card.IsSelected();
    snapshot.useDefaultConvertSettings = card.UseDefaultConvertSettings();
    snapshot.customOptions = card.CustomConvertOptions();
    return snapshot;
}

ConverterSettingsSnapshot DockArea::CaptureConverterSettings() const
{
    return {convertContainer_,
            convertVideo_,
            convertAudio_,
            convertContainerIndex_,
            convertVideoIndex_,
            convertAudioIndex_};
}

void DockArea::RecordLinkCardRemoval(int index)
{
    if (suppressUndoRecording_ || suppressCardRemovalUndo_)
    {
        return;
    }
    if (!CanRecordLinkCardRemoval(index))
    {
        return;
    }

    PushUndo(MakeRemoveLinkCardCommand(CaptureLinkCardSnapshot(index)));
}

void DockArea::RecordConverterCardRemoval(int index)
{
    if (suppressUndoRecording_ || suppressCardRemovalUndo_)
    {
        return;
    }
    if (!CanRecordConverterCardRemoval(index))
    {
        return;
    }

    PushUndo(MakeRemoveConverterCardCommand(CaptureConverterCardSnapshot(index)));
}

bool DockArea::CanRecordLinkCardRemoval(int index) const
{
    if (index < 0 || index >= static_cast<int>(cards_.size()))
    {
        return false;
    }
    // Incomplete parse dismissals are not undoable.
    const DownloaderListItem& item = cards_[index];
    if (item.kind == DownloaderListItem::Kind::Single)
    {
        return !item.single->IsParsing() && !item.single->WasDismissedDuringParse();
    }
    return !item.group->IsParsing();
}

bool DockArea::CanRecordConverterCardRemoval(int index) const
{
    if (index < 0 || index >= static_cast<int>(converterCards_.size()))
    {
        return false;
    }
    // Incomplete load dismissals are not undoable.
    return !converterCards_[index].IsLoading() && !converterCards_[index].WasDismissedDuringLoad() &&
           converterCards_[index].HasFile();
}

void DockArea::FlushPendingBatchLinkRemoveUndo()
{
    if (!suppressCardRemovalUndo_)
    {
        return;
    }

    suppressCardRemovalUndo_ = false;
    if (!pendingBatchLinkRemove_.empty())
    {
        PushUndo(MakeRemoveLinkCardsBatchCommand(std::move(pendingBatchLinkRemove_)));
    }
    pendingBatchLinkRemove_.clear();
}

void DockArea::FlushPendingBatchConverterRemoveUndo()
{
    if (!suppressCardRemovalUndo_)
    {
        return;
    }

    suppressCardRemovalUndo_ = false;
    if (!pendingBatchConverterRemove_.empty())
    {
        PushUndo(MakeRemoveConverterCardsBatchCommand(std::move(pendingBatchConverterRemove_)));
    }
    pendingBatchConverterRemove_.clear();
}

void DockArea::PushUndo(std::unique_ptr<UndoCommand> command)
{
    if (suppressUndoRecording_ || command == nullptr)
    {
        return;
    }
    undoStack_.Push(std::move(command));
}

void DockArea::PerformUndo()
{
    if (!undoStack_.CanUndo())
    {
        return;
    }
    suppressUndoRecording_ = true;
    undoStack_.Undo(*this);
    suppressUndoRecording_ = false;
}

void DockArea::PerformRedo()
{
    if (!undoStack_.CanRedo())
    {
        return;
    }
    suppressUndoRecording_ = true;
    undoStack_.Redo(*this);
    suppressUndoRecording_ = false;
}

void DockArea::UndoRestoreLinkCard(const LinkCardUndoSnapshot& snapshot)
{
    int index = std::clamp(snapshot.index, 0, static_cast<int>(cards_.size()));
    DownloaderListItem restored = LooksLikeGroupUrl(snapshot.info.url)
                                      ? DownloaderListItem::MakeGroup(snapshot.info.url)
                                      : DownloaderListItem::MakeSingle(snapshot.info.url);
    if (restored.kind == DownloaderListItem::Kind::Single && snapshot.info.success && !snapshot.info.title.empty())
    {
        restored.single = std::make_unique<LinkCardNode>(snapshot.info);
        restored.single->Options() = snapshot.options;
        restored.single->SetExcludedFromAutoConvert(snapshot.excludeFromAutoConvert);
        restored.single->SetCustomAutoConvert(snapshot.customAutoConvert);
        if (!snapshot.lastDownloadedPath.empty())
        {
            restored.single->SetLastDownloadedPath(snapshot.lastDownloadedPath);
        }
    }
    cards_.insert(cards_.begin() + index, std::move(restored));
    for (DownloaderListItem& item : cards_)
    {
        item.ClearSelection();
    }
    if (cards_[index].kind == DownloaderListItem::Kind::Single)
    {
        cards_[index].single->SetSelected(snapshot.selected);
        cards_[index].single->TriggerPulse();
    }
    else
    {
        cards_[index].group->SetHeaderSelected(snapshot.selected);
    }
    lastDownloaderSelectionAnchor_ = index;
    lastDownloaderSelectionFocus_ = index;
}

void DockArea::UndoRemoveLinkCardByUrl(const std::string& url)
{
    for (int index = 0; index < static_cast<int>(cards_.size()); ++index)
    {
        if (cards_[index].HasUrl(url))
        {
            OnCardClosed(url);
            cards_.erase(cards_.begin() + index);
            return;
        }
    }
}

void DockArea::UndoRestoreLinkGroupChild(const LinkGroupChildUndoSnapshot& snapshot)
{
    for (DownloaderListItem& item : cards_)
    {
        if (item.kind != DownloaderListItem::Kind::Group || item.group == nullptr)
        {
            continue;
        }
        if (item.group->Url() != snapshot.groupUrl)
        {
            continue;
        }

        item.group->SetHeaderSelected(snapshot.groupHeaderSelected);
        item.group->InsertEntry(snapshot.entryIndex, snapshot.entry);

        for (LinkCardNode& child : item.group->LoadedCards())
        {
            if (!child.HasUrl(snapshot.childUrl))
            {
                continue;
            }

            child.Options() = snapshot.options;
            child.SetExcludedFromAutoConvert(snapshot.excludeFromAutoConvert);
            child.SetCustomAutoConvert(snapshot.customAutoConvert);
            if (!snapshot.lastDownloadedPath.empty())
            {
                child.SetLastDownloadedPath(snapshot.lastDownloadedPath);
            }
            child.SetSelected(snapshot.selected);
            child.TriggerPulse();
            break;
        }

        return;
    }
}

void DockArea::UndoRemoveLinkGroupChild(const LinkGroupChildUndoSnapshot& snapshot)
{
    for (DownloaderListItem& item : cards_)
    {
        if (item.kind != DownloaderListItem::Kind::Group || item.group == nullptr)
        {
            continue;
        }
        if (item.group->Url() != snapshot.groupUrl)
        {
            continue;
        }

        item.group->SetHeaderSelected(snapshot.groupHeaderSelected);
        OnCardClosed(snapshot.childUrl);

        LinkGroupEntry dummyEntry;
        size_t dummyIndex = 0;
        (void)item.group->RemoveEntryByUrl(snapshot.childUrl, dummyEntry, dummyIndex);
        return;
    }
}

void DockArea::UndoRestoreConverterCard(const ConverterCardUndoSnapshot& snapshot)
{
    int index = std::clamp(snapshot.index, 0, static_cast<int>(converterCards_.size()));
    converterCards_.emplace(converterCards_.begin() + index);
    ConverterFileCardNode& card = converterCards_[index];
    card.SetInfo(snapshot.info);
    card.SetUseDefaultConvertSettings(snapshot.useDefaultConvertSettings);
    card.CustomConvertOptions() = snapshot.customOptions;
    for (ConverterFileCardNode& other : converterCards_)
    {
        other.SetSelected(false);
    }
    card.SetSelected(true);
    card.TriggerPulse();
    lastConverterSelectionAnchor_ = index;
    lastConverterSelectionFocus_ = index;
}

void DockArea::UndoRemoveConverterCardByPath(const std::string& filePath)
{
    for (int index = 0; index < static_cast<int>(converterCards_.size()); ++index)
    {
        if (!converterCards_[index].HasFilePath(filePath))
        {
            continue;
        }

        ConverterFileCardNode& card = converterCards_[index];
        if (card.IsLoading())
        {
            card.CancelLoading();
        }
        else if (card.IsConverting() && FindConvertRunnerByPath(filePath) != nullptr)
        {
            CancelConverterCard(filePath);
        }
        else
        {
            RemovePendingConvertsForPath(filePath);
        }
        converterCards_.erase(converterCards_.begin() + index);
        return;
    }
}

void DockArea::UndoApplyCardOptions(const std::string& url, const DownloadOptions& options)
{
    ForEachLinkCard(
        [&](LinkCardNode& card)
        {
            if (card.HasUrl(url))
            {
                card.Options() = options;
                card.SetSelected(true);
                ForEachLinkCard(
                    [&](LinkCardNode& other)
                    {
                        if (!other.HasUrl(url))
                        {
                            other.SetSelected(false);
                        }
                    });
                return;
            }
        });
}

void DockArea::UndoApplyConverterSettings(const ConverterSettingsSnapshot& settings)
{
    convertContainer_ = settings.convertContainer;
    convertVideo_ = settings.convertVideo;
    convertAudio_ = settings.convertAudio;
    convertContainerIndex_ = settings.convertContainerIndex;
    convertVideoIndex_ = settings.convertVideoIndex;
    convertAudioIndex_ = settings.convertAudioIndex;
}

void DockArea::UndoApplyConverterCardOptions(const std::string& filePath, const ConverterCardOptionsSnapshot& snapshot)
{
    for (ConverterFileCardNode& card : converterCards_)
    {
        if (!card.HasFilePath(filePath))
        {
            continue;
        }
        card.SetUseDefaultConvertSettings(snapshot.useDefaultConvertSettings);
        card.CustomConvertOptions() = snapshot.customOptions;
        break;
    }
}

void DockArea::UndoApplyGlobalPath(const std::string& path)
{
    globalDownloadPath_ = path;
}

HoverContext DockArea::ResolveHoverTarget(Rectangle leftPanel, Rectangle rightPanel) const
{
    HoverContext context;

    if (activeWorkspace_ == Workspace::Downloader)
    {
        for (int index = 0; index < static_cast<int>(cards_.size()); ++index)
        {
            if (cards_[index].IsHovered())
            {
                context.target = UiHoverTarget::LinkCard;
                context.index = index;
                return context;
            }
        }

        const Rectangle autoConvertDock = GetAutoConvertDockPanel(rightPanel);
        if (CheckCollisionPointRec(GetMousePosition(), autoConvertDock))
        {
            context.target = UiHoverTarget::AutoConvertFoldout;
            return context;
        }

        const LinkCardNode* selectedCard = GetSelectedCard();
        if (selectedCard != nullptr && selectedCard->IsValid())
        {
            const Rectangle settingsPanel = GetRightSettingsPanel(rightPanel);
            const DownloaderPanelLayout layout = GetDownloaderPanelLayout(settingsPanel.x,
                                                                          settingsPanel.y,
                                                                          settingsPanel.width,
                                                                          downloadFoldout_.IsExpanded(),
                                                                          optionsScrollOffset_);

            if (CheckCollisionPointRec(GetMousePosition(), layout.foldoutPanelBounds))
            {
                context.target = UiHoverTarget::DownloadFoldout;
                return context;
            }
        }
    }
    else
    {
        for (int index = 0; index < static_cast<int>(converterCards_.size()); ++index)
        {
            if (converterCards_[index].IsHovered())
            {
                context.target = UiHoverTarget::ConverterCard;
                context.index = index;
                return context;
            }
        }
    }

    (void)leftPanel;
    return context;
}

bool DockArea::HandleFoldoutHoverShortcuts()
{
    if (activeWorkspace_ == Workspace::Converter)
    {
        // More specific / lower sections first so overlapping hit tests prefer the inner section.
        FoldoutPanel* foldouts[] = {&converterCustomFoldout_, &converterDefaultFoldout_, &converterSectionFoldout_};
        for (FoldoutPanel* foldout : foldouts)
        {
            if (foldout == &converterCustomFoldout_)
            {
                if (CollectEditableSelectedConverterCards().empty())
                {
                    continue;
                }
            }
            if (!converterSectionFoldout_.IsExpanded() &&
                (foldout == &converterCustomFoldout_ || foldout == &converterDefaultFoldout_))
            {
                continue;
            }
            if (foldout->TryHoverToggleShortcut())
            {
                if (!foldout->IsExpanded())
                {
                    OnFoldoutCollapsedByShortcut(*foldout);
                }
                return true;
            }
        }
        return false;
    }

    // More specific / lower sections first so overlapping hit tests prefer the inner section.
    FoldoutPanel* foldouts[] = {
        &customAutoConvertFoldout_, &autoConvertFoldout_, &autoConvertSectionFoldout_, &downloadFoldout_};
    for (FoldoutPanel* foldout : foldouts)
    {
        if (foldout == &customAutoConvertFoldout_)
        {
            bool hasSelection = false;
            bool allExcluded = true;
            ForEachLinkCard(
                [&](const LinkCardNode& card)
                {
                    if (!card.IsSelected() || !card.IsValid())
                    {
                        return;
                    }
                    hasSelection = true;
                    if (!card.IsExcludedFromAutoConvert())
                    {
                        allExcluded = false;
                    }
                });
            if (!hasSelection || allExcluded)
            {
                continue;
            }
        }
        if (!autoConvertSectionFoldout_.IsExpanded() &&
            (foldout == &customAutoConvertFoldout_ || foldout == &autoConvertFoldout_))
        {
            continue;
        }
        if (foldout->TryHoverToggleShortcut())
        {
            if (!foldout->IsExpanded())
            {
                OnFoldoutCollapsedByShortcut(*foldout);
            }
            return true;
        }
    }
    return false;
}

void DockArea::OnFoldoutCollapsedByShortcut(FoldoutPanel& foldout)
{
    if (&foldout == &downloadFoldout_)
    {
        fileFormatDropdown_.Close();
        mediaModeDropdown_.Close();
        qualityDropdown_.Close();
        return;
    }
    if (&foldout == &autoConvertSectionFoldout_)
    {
        autoConvertContainerDropdown_.Close();
        autoConvertVideoDropdown_.Close();
        autoConvertAudioDropdown_.Close();
        customAutoConvertContainerDropdown_.Close();
        customAutoConvertVideoDropdown_.Close();
        customAutoConvertAudioDropdown_.Close();
        return;
    }
    if (&foldout == &autoConvertFoldout_)
    {
        autoConvertContainerDropdown_.Close();
        autoConvertVideoDropdown_.Close();
        autoConvertAudioDropdown_.Close();
        return;
    }
    if (&foldout == &customAutoConvertFoldout_)
    {
        customAutoConvertContainerDropdown_.Close();
        customAutoConvertVideoDropdown_.Close();
        customAutoConvertAudioDropdown_.Close();
        return;
    }
    if (&foldout == &converterSectionFoldout_)
    {
        convertContainerDropdown_.Close();
        convertVideoDropdown_.Close();
        convertAudioDropdown_.Close();
        cardConvertContainerDropdown_.Close();
        cardConvertVideoDropdown_.Close();
        cardConvertAudioDropdown_.Close();
        return;
    }
    if (&foldout == &converterDefaultFoldout_)
    {
        convertContainerDropdown_.Close();
        convertVideoDropdown_.Close();
        convertAudioDropdown_.Close();
        return;
    }
    if (&foldout == &converterCustomFoldout_)
    {
        cardConvertContainerDropdown_.Close();
        cardConvertVideoDropdown_.Close();
        cardConvertAudioDropdown_.Close();
    }
}

void DockArea::EnsureCardVisibleInList(Rectangle leftPanel, int index, float& scrollOffset) const
{
    if (index < 0 || index >= static_cast<int>(cards_.size()))
    {
        return;
    }

    const float cardTop = GetDownloaderItemTop(leftPanel, index, scrollOffset);
    const float cardBottom = cardTop + GetDownloaderItemHeight(index);
    const float viewTop = leftPanel.y;
    const float viewBottom = leftPanel.y + leftPanel.height;

    if (cardTop < viewTop)
    {
        scrollOffset -= viewTop - cardTop;
    }
    else if (cardBottom > viewBottom)
    {
        scrollOffset += cardBottom - viewBottom;
    }

    const float contentHeight = GetDownloaderListContentHeight();
    const float maxScroll = GetMaxCardScroll(leftPanel, contentHeight, 0.0f);
    scrollOffset = std::clamp(scrollOffset, 0.0f, maxScroll);
}

void DockArea::NavigateDownloaderSelection(int delta, Rectangle leftPanel, bool allowModifiers)
{
    const int count = static_cast<int>(cards_.size());
    if (count <= 0 || delta == 0)
    {
        return;
    }

    int focus = lastDownloaderSelectionFocus_;
    if (focus < 0 || focus >= count)
    {
        focus = lastDownloaderSelectionAnchor_;
    }
    if (focus < 0 || focus >= count)
    {
        for (int index = 0; index < count; ++index)
        {
            if (cards_[index].IsSelected())
            {
                focus = index;
                break;
            }
        }
    }

    bool anySelected = false;
    for (int index = 0; index < count; ++index)
    {
        if (cards_[index].IsSelected())
        {
            anySelected = true;
            break;
        }
    }
    if (!anySelected)
    {
        focus = -1;
    }

    int next = focus;
    if (focus < 0)
    {
        next = delta > 0 ? 0 : count - 1;
    }
    else
    {
        next = std::clamp(focus + delta, 0, count - 1);
    }

    const bool ctrl = allowModifiers && ShortcutRouter::CtrlDown();
    const bool shift = allowModifiers && ShortcutRouter::ShiftDown();

    if (shift)
    {
        if (lastDownloaderSelectionAnchor_ < 0 || lastDownloaderSelectionAnchor_ >= count)
        {
            lastDownloaderSelectionAnchor_ = focus >= 0 ? focus : next;
        }
        const int a = std::min(lastDownloaderSelectionAnchor_, next);
        const int b = std::max(lastDownloaderSelectionAnchor_, next);
        for (int index = 0; index < count; ++index)
        {
            cards_[index].SetSelected(index >= a && index <= b);
        }
        lastDownloaderSelectionFocus_ = next;
    }
    else if (ctrl)
    {
        cards_[next].SetSelected(!cards_[next].IsSelected());
        lastDownloaderSelectionAnchor_ = next;
        lastDownloaderSelectionFocus_ = next;
    }
    else
    {
        for (int index = 0; index < count; ++index)
        {
            cards_[index].SetSelected(index == next);
        }
        lastDownloaderSelectionAnchor_ = next;
        lastDownloaderSelectionFocus_ = next;
    }

    EnsureCardVisibleInList(leftPanel, next, downloaderScrollOffset_);
}

void DockArea::NavigateConverterSelection(int delta, Rectangle leftPanel, bool allowModifiers)
{
    const int count = static_cast<int>(converterCards_.size());
    if (count <= 0 || delta == 0)
    {
        return;
    }

    int focus = lastConverterSelectionFocus_;
    if (focus < 0 || focus >= count)
    {
        focus = lastConverterSelectionAnchor_;
    }
    if (focus < 0 || focus >= count)
    {
        for (int index = 0; index < count; ++index)
        {
            if (converterCards_[index].IsSelected())
            {
                focus = index;
                break;
            }
        }
    }

    bool anySelected = false;
    for (int index = 0; index < count; ++index)
    {
        if (converterCards_[index].IsSelected())
        {
            anySelected = true;
            break;
        }
    }
    if (!anySelected)
    {
        focus = -1;
    }

    int next = focus;
    if (focus < 0)
    {
        next = delta > 0 ? 0 : count - 1;
    }
    else
    {
        next = std::clamp(focus + delta, 0, count - 1);
    }

    const bool ctrl = allowModifiers && ShortcutRouter::CtrlDown();
    const bool shift = allowModifiers && ShortcutRouter::ShiftDown();

    if (shift)
    {
        if (lastConverterSelectionAnchor_ < 0 || lastConverterSelectionAnchor_ >= count)
        {
            lastConverterSelectionAnchor_ = focus >= 0 ? focus : next;
        }
        const int a = std::min(lastConverterSelectionAnchor_, next);
        const int b = std::max(lastConverterSelectionAnchor_, next);
        for (int index = 0; index < count; ++index)
        {
            converterCards_[index].SetSelected(index >= a && index <= b);
        }
        lastConverterSelectionFocus_ = next;
    }
    else if (ctrl)
    {
        converterCards_[next].SetSelected(!converterCards_[next].IsSelected());
        lastConverterSelectionAnchor_ = next;
        lastConverterSelectionFocus_ = next;
    }
    else
    {
        for (int index = 0; index < count; ++index)
        {
            converterCards_[index].SetSelected(index == next);
        }
        lastConverterSelectionAnchor_ = next;
        lastConverterSelectionFocus_ = next;
    }

    EnsureCardVisibleInList(leftPanel, next, converterScrollOffset_);
}

void DockArea::HandleShortcuts(Rectangle leftPanel, Rectangle rightPanel)
{
    if (isAboutDialogOpen_ || isInfoDialogOpen_ || isOverwritePromptOpen_)
    {
        return;
    }

    if (customPathField_.IsActive() || globalPathField_.IsActive())
    {
        return;
    }

    if (ShortcutRouter::Pressed({KEY_Z, true, false, false}))
    {
        PerformUndo();
        return;
    }
    if (ShortcutRouter::Pressed({KEY_Y, true, false, false}) || ShortcutRouter::Pressed({KEY_Z, true, false, true}))
    {
        PerformRedo();
        return;
    }

    const HoverContext hover = ResolveHoverTarget(leftPanel, rightPanel);

    // Contextual binds first.
    if (hover.target == UiHoverTarget::LinkCard && ShortcutRouter::Pressed({KEY_X, false, false, false}))
    {
        CloseLinkCardAt(hover.index);
        return;
    }
    if (hover.target == UiHoverTarget::ConverterCard && ShortcutRouter::Pressed({KEY_X, false, false, false}))
    {
        CloseConverterCardAt(hover.index);
        return;
    }
    if (HandleFoldoutHoverShortcuts())
    {
        return;
    }

    // Global binds.
    if (ShortcutRouter::Pressed({KEY_A, true, false, false}))
    {
        if (activeWorkspace_ == Workspace::Downloader)
        {
            const bool hoveredGroup = hover.target == UiHoverTarget::LinkCard && hover.index >= 0 &&
                                      hover.index < static_cast<int>(cards_.size()) &&
                                      cards_[hover.index].kind == DownloaderListItem::Kind::Group &&
                                      cards_[hover.index].group != nullptr;

            if (hoveredGroup)
            {
                for (DownloaderListItem& item : cards_)
                {
                    item.ClearSelection();
                }

                LinkCardGroupNode& group = *cards_[hover.index].group;
                group.SetHeaderSelected(true);
                for (LinkCardNode& child : group.LoadedCards())
                {
                    child.SetSelected(true);
                }

                lastDownloaderSelectionAnchor_ = hover.index;
                lastDownloaderSelectionFocus_ = hover.index;
            }
            else
            {
                for (int index = 0; index < static_cast<int>(cards_.size()); ++index)
                {
                    cards_[index].SetSelected(true);
                }

                if (!cards_.empty())
                {
                    lastDownloaderSelectionAnchor_ = 0;
                    lastDownloaderSelectionFocus_ = static_cast<int>(cards_.size()) - 1;
                }
                else
                {
                    lastDownloaderSelectionAnchor_ = -1;
                    lastDownloaderSelectionFocus_ = -1;
                }
            }
        }
        else
        {
            for (int index = 0; index < static_cast<int>(converterCards_.size()); ++index)
            {
                converterCards_[index].SetSelected(true);
            }
            if (!converterCards_.empty())
            {
                lastConverterSelectionAnchor_ = 0;
                lastConverterSelectionFocus_ = static_cast<int>(converterCards_.size()) - 1;
            }
            else
            {
                lastConverterSelectionAnchor_ = -1;
                lastConverterSelectionFocus_ = -1;
            }
        }
        return;
    }
    if (ShortcutRouter::Pressed({KEY_V, true, false, true}))
    {
        if (activeWorkspace_ == Workspace::Downloader)
        {
            HandleInsertLinkRequest(true);
        }
        else
        {
            HandleChooseFileRequest();
        }
        return;
    }
    if (ShortcutRouter::Pressed({KEY_V, true, false, false}))
    {
        if (activeWorkspace_ == Workspace::Downloader)
        {
            HandleInsertLinkRequest(false);
        }
        else
        {
            HandleChooseFileRequest();
        }
        return;
    }

    if (ShortcutRouter::Pressed({KEY_ONE, false, false, false}) ||
        ShortcutRouter::Pressed({KEY_KP_1, false, false, false}))
    {
        activeWorkspace_ = Workspace::Downloader;
        return;
    }
    if (ShortcutRouter::Pressed({KEY_TWO, false, false, false}) ||
        ShortcutRouter::Pressed({KEY_KP_2, false, false, false}))
    {
        activeWorkspace_ = Workspace::Converter;
        return;
    }

    if (!ShortcutRouter::AltDown() && (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_UP)))
    {
        const int delta = IsKeyPressed(KEY_DOWN) ? 1 : -1;
        if (activeWorkspace_ == Workspace::Downloader)
        {
            NavigateDownloaderSelection(delta, leftPanel);
        }
        else
        {
            NavigateConverterSelection(delta, leftPanel);
        }
        return;
    }

    if (!ShortcutRouter::AltDown() && !ShortcutRouter::CtrlDown() && IsKeyPressed(KEY_TAB))
    {
        const int delta = ShortcutRouter::ShiftDown() ? -1 : 1;
        if (activeWorkspace_ == Workspace::Downloader)
        {
            NavigateDownloaderSelection(delta, leftPanel, false);
        }
        else
        {
            NavigateConverterSelection(delta, leftPanel, false);
        }
        return;
    }

    if (activeWorkspace_ == Workspace::Downloader)
    {
        if (ShortcutRouter::Pressed({KEY_ENTER, false, true, false}))
        {
            HandleCancelAllDownloadsRequest();
            return;
        }
        if (ShortcutRouter::Pressed({KEY_ENTER, true, false, false}))
        {
            if (HasDownloadableIdleCards())
            {
                HandleDownloadAllRequest();
            }
            return;
        }
        if (ShortcutRouter::Pressed({KEY_ENTER, false, false, false}) ||
            ShortcutRouter::Pressed({KEY_SPACE, false, false, false}))
        {
            if (SelectedCardShowsCancel())
            {
                HandleCancelSelectedRequest();
            }
            else if (CanDownloadSelected())
            {
                HandleDownloadRequest();
            }
            return;
        }
        if (ShortcutRouter::Pressed({KEY_DELETE, true, false, false}) ||
            ShortcutRouter::Pressed({KEY_X, true, false, false}))
        {
            pendingBatchLinkRemove_.clear();
            for (int index = 0; index < static_cast<int>(cards_.size()); ++index)
            {
                if (CanRecordLinkCardRemoval(index))
                {
                    pendingBatchLinkRemove_.push_back(CaptureLinkCardSnapshot(index));
                }
            }
            suppressCardRemovalUndo_ = true;
            for (int index = static_cast<int>(cards_.size()) - 1; index >= 0; --index)
            {
                CloseLinkCardAt(index);
            }
            return;
        }
        if (ShortcutRouter::Pressed({KEY_DELETE, false, false, false}))
        {
            for (int index = static_cast<int>(cards_.size()) - 1; index >= 0; --index)
            {
                if (!cards_[index].IsSelected())
                {
                    continue;
                }
                CloseLinkCardAt(index);
            }
            return;
        }
        if (ShortcutRouter::Pressed({KEY_ESCAPE, false, false, false}))
        {
            ForEachLinkCard(
                [&](LinkCardNode& card)
                {
                    card.SetSelected(false);
                });
            lastDownloaderSelectionAnchor_ = -1;
            lastDownloaderSelectionFocus_ = -1;
            return;
        }
    }
    else
    {
        if (ShortcutRouter::Pressed({KEY_ENTER, false, true, false}))
        {
            HandleCancelAllConvertsRequest();
            return;
        }
        if (ShortcutRouter::Pressed({KEY_ENTER, true, false, false}))
        {
            HandleConvertAllRequest();
            return;
        }
        if (ShortcutRouter::Pressed({KEY_ENTER, false, false, false}) ||
            ShortcutRouter::Pressed({KEY_SPACE, false, false, false}))
        {
            if (SelectedConverterShowsCancel())
            {
                HandleCancelSelectedConvertsRequest();
            }
            else
            {
                HandleConvertRequest();
            }
            return;
        }
        if (ShortcutRouter::Pressed({KEY_DELETE, true, false, false}) ||
            ShortcutRouter::Pressed({KEY_X, true, false, false}))
        {
            pendingBatchConverterRemove_.clear();
            for (int index = 0; index < static_cast<int>(converterCards_.size()); ++index)
            {
                if (CanRecordConverterCardRemoval(index))
                {
                    pendingBatchConverterRemove_.push_back(CaptureConverterCardSnapshot(index));
                }
            }
            suppressCardRemovalUndo_ = true;
            for (int index = static_cast<int>(converterCards_.size()) - 1; index >= 0; --index)
            {
                CloseConverterCardAt(index);
            }
            return;
        }
        if (ShortcutRouter::Pressed({KEY_DELETE, false, false, false}))
        {
            for (int index = static_cast<int>(converterCards_.size()) - 1; index >= 0; --index)
            {
                if (!converterCards_[index].IsSelected())
                {
                    continue;
                }
                CloseConverterCardAt(index);
            }
            return;
        }
        if (ShortcutRouter::Pressed({KEY_ESCAPE, false, false, false}))
        {
            for (ConverterFileCardNode& card : converterCards_)
            {
                card.SetSelected(false);
            }
            lastConverterSelectionAnchor_ = -1;
            lastConverterSelectionFocus_ = -1;
            return;
        }
    }
}

ConverterSettingsSnapshot DockArea::ResolveConverterSettingsForCard(const ConverterFileCardNode& card) const
{
    if (card.UseDefaultConvertSettings())
    {
        return CaptureConverterSettings();
    }

    const ConverterOptions& options = card.CustomConvertOptions();
    return {options.convertContainer,
            options.convertVideo,
            options.convertAudio,
            options.containerIndex,
            options.videoIndex,
            options.audioIndex};
}

int DockArea::CountSelectedConverterCards() const
{
    int count = 0;
    for (const ConverterFileCardNode& card : converterCards_)
    {
        if (card.IsSelected())
        {
            ++count;
        }
    }
    return count;
}

bool DockArea::CanBuildAnyConvertRequest() const
{
    for (const ConverterFileCardNode& card : converterCards_)
    {
        ConvertRequest request;
        if (BuildConvertRequestForCard(card, request))
        {
            return true;
        }
    }
    return false;
}

namespace
{
const std::vector<std::string>& ConverterPanelContainers()
{
    static const std::vector<std::string> kContainers = {"MP4", "MKV", "MOV", "WEBM"};
    return kContainers;
}

ConverterOptions MapCardCustomOptionsToPanelEdit(const ConverterFileCardNode& card, const ConverterOptions& opts)
{
    const std::vector<std::string>& kContainers = ConverterPanelContainers();
    ConverterOptions edit = opts;
    const ConverterFileInfo& info = card.Info();
    const std::vector<std::string> containerItems = BuildConverterItems(kContainers, info.container);
    const std::string effectiveContainer =
        EffectiveConvertContainer(opts.convertContainer, opts.containerIndex, containerItems, info.container);
    edit.containerIndex = FindCodecIndex(kContainers, effectiveContainer, kContainers[0]);

    const std::string panelEffective =
        EffectiveConvertContainer(edit.convertContainer, edit.containerIndex, kContainers, info.container);
    const std::vector<std::string> panelVideos = CompatibleVideoCodecsForContainer(panelEffective);
    const std::vector<std::string> panelAudios = CompatibleAudioCodecsForContainer(panelEffective);
    const std::vector<std::string> cardVideos =
        BuildConverterItems(CompatibleVideoCodecsForContainer(effectiveContainer), info.videoCodec);
    const std::vector<std::string> cardAudios =
        BuildConverterItems(CompatibleAudioCodecsForContainer(effectiveContainer), info.audioCodec);
    const int videoIndex =
        cardVideos.empty() ? 0 : std::clamp(opts.videoIndex, 0, static_cast<int>(cardVideos.size()) - 1);
    const int audioIndex =
        cardAudios.empty() ? 0 : std::clamp(opts.audioIndex, 0, static_cast<int>(cardAudios.size()) - 1);
    const std::string videoName = cardVideos.empty() ? info.videoCodec : StripCurrentLabel(cardVideos[videoIndex]);
    const std::string audioName = cardAudios.empty() ? info.audioCodec : StripCurrentLabel(cardAudios[audioIndex]);
    edit.videoIndex =
        panelVideos.empty() ? 0 : FindCodecIndex(panelVideos, videoName, DefaultVideoCodecForContainer(panelEffective));
    edit.audioIndex =
        panelAudios.empty() ? 0 : FindCodecIndex(panelAudios, audioName, DefaultAudioCodecForContainer(panelEffective));
    return edit;
}

void ApplyPanelEditToCard(ConverterFileCardNode& card, bool useDefault, const ConverterOptions& edit)
{
    card.SetUseDefaultConvertSettings(useDefault);
    if (useDefault)
    {
        return;
    }

    const std::vector<std::string>& kContainers = ConverterPanelContainers();
    const ConverterFileInfo& info = card.Info();
    ConverterOptions& opts = card.CustomConvertOptions();
    opts.convertContainer = edit.convertContainer;
    opts.convertVideo = edit.convertVideo;
    opts.convertAudio = edit.convertAudio;

    const std::vector<std::string> containerItems = BuildConverterItems(kContainers, info.container);
    const int panelContainerIndex = std::clamp(edit.containerIndex, 0, static_cast<int>(kContainers.size()) - 1);
    const std::string targetContainer = kContainers[panelContainerIndex];
    opts.containerIndex = FindCodecIndex(containerItems, targetContainer, info.container);

    const std::string panelEffective =
        EffectiveConvertContainer(edit.convertContainer, edit.containerIndex, kContainers, info.container);
    const std::string cardEffective =
        EffectiveConvertContainer(opts.convertContainer, opts.containerIndex, containerItems, info.container);
    const std::vector<std::string> panelVideos = CompatibleVideoCodecsForContainer(panelEffective);
    const std::vector<std::string> panelAudios = CompatibleAudioCodecsForContainer(panelEffective);
    const std::vector<std::string> cardVideos =
        BuildConverterItems(CompatibleVideoCodecsForContainer(cardEffective), info.videoCodec);
    const std::vector<std::string> cardAudios =
        BuildConverterItems(CompatibleAudioCodecsForContainer(cardEffective), info.audioCodec);

    const int panelVideoIndex =
        panelVideos.empty() ? 0 : std::clamp(edit.videoIndex, 0, static_cast<int>(panelVideos.size()) - 1);
    const int panelAudioIndex =
        panelAudios.empty() ? 0 : std::clamp(edit.audioIndex, 0, static_cast<int>(panelAudios.size()) - 1);
    const std::string targetVideo =
        panelVideos.empty() ? DefaultVideoCodecForContainer(cardEffective) : panelVideos[panelVideoIndex];
    const std::string targetAudio =
        panelAudios.empty() ? DefaultAudioCodecForContainer(cardEffective) : panelAudios[panelAudioIndex];
    opts.videoIndex =
        cardVideos.empty() ? 0 : FindCodecIndex(cardVideos, targetVideo, DefaultVideoCodecForContainer(cardEffective));
    opts.audioIndex =
        cardAudios.empty() ? 0 : FindCodecIndex(cardAudios, targetAudio, DefaultAudioCodecForContainer(cardEffective));
}
} // namespace

std::vector<ConverterFileCardNode*> DockArea::CollectEditableSelectedConverterCards()
{
    std::vector<ConverterFileCardNode*> cards;
    for (ConverterFileCardNode& card : converterCards_)
    {
        if (!card.IsSelected() || !card.HasFile() || card.IsLoading() || card.IsConverting())
        {
            continue;
        }
        cards.push_back(&card);
    }
    return cards;
}

std::string DockArea::BuildConverterCardOptionsSelectionKey() const
{
    std::vector<std::string> paths;
    for (const ConverterFileCardNode& card : converterCards_)
    {
        if (!card.IsSelected() || !card.HasFile())
        {
            continue;
        }
        paths.push_back(card.Info().filePath);
    }
    std::sort(paths.begin(), paths.end());
    std::string key;
    for (const std::string& path : paths)
    {
        key += path;
        key += '\n';
    }
    return key;
}

void DockArea::SyncConverterCardOptionsEditFromSelection(const std::vector<ConverterFileCardNode*>& cards)
{
    if (cards.empty())
    {
        return;
    }

    bool allUseDefault = true;
    bool anyUseDefault = false;
    for (const ConverterFileCardNode* card : cards)
    {
        if (card->UseDefaultConvertSettings())
        {
            anyUseDefault = true;
        }
        else
        {
            allUseDefault = false;
        }
    }
    converterCardOptionsUseDefaultMixed_ = anyUseDefault && !allUseDefault;
    converterCardOptionsUseDefault_ = allUseDefault;

    if (allUseDefault)
    {
        converterCardOptionsCustomMixed_ = false;
        // Mirror Global Options into the edit buffer so disabled Custom rows stay accurate.
        const ConverterSettingsSnapshot global = CaptureConverterSettings();
        converterCardOptionsEdit_ = {global.convertContainer,
                                     global.convertVideo,
                                     global.convertAudio,
                                     global.convertContainerIndex,
                                     global.convertVideoIndex,
                                     global.convertAudioIndex};
        return;
    }

    const ConverterFileCardNode* reference = cards.front();
    for (const ConverterFileCardNode* card : cards)
    {
        if (!card->UseDefaultConvertSettings())
        {
            reference = card;
            break;
        }
    }
    converterCardOptionsEdit_ = MapCardCustomOptionsToPanelEdit(*reference, reference->CustomConvertOptions());

    converterCardOptionsCustomMixed_ = false;
    const ConverterOptions referenceEdit =
        MapCardCustomOptionsToPanelEdit(*reference, reference->CustomConvertOptions());
    for (const ConverterFileCardNode* card : cards)
    {
        if (card->UseDefaultConvertSettings())
        {
            converterCardOptionsCustomMixed_ = true;
            break;
        }
        const ConverterOptions cardEdit = MapCardCustomOptionsToPanelEdit(*card, card->CustomConvertOptions());
        if (cardEdit != referenceEdit)
        {
            converterCardOptionsCustomMixed_ = true;
            break;
        }
    }
}

void DockArea::ApplyConverterCardOptionsEditToCards(const std::vector<ConverterFileCardNode*>& cards)
{
    for (ConverterFileCardNode* card : cards)
    {
        ApplyPanelEditToCard(*card, converterCardOptionsUseDefault_, converterCardOptionsEdit_);
    }
}

std::vector<ConverterCardOptionsSnapshot>
DockArea::CaptureConverterCardOptionsSnapshots(const std::vector<ConverterFileCardNode*>& cards) const
{
    std::vector<ConverterCardOptionsSnapshot> snapshots;
    snapshots.reserve(cards.size());
    for (const ConverterFileCardNode* card : cards)
    {
        ConverterCardOptionsSnapshot snapshot;
        snapshot.filePath = card->Info().filePath;
        snapshot.useDefaultConvertSettings = card->UseDefaultConvertSettings();
        snapshot.customOptions = card->CustomConvertOptions();
        snapshots.push_back(std::move(snapshot));
    }
    return snapshots;
}

bool DockArea::UpdateConverterDefaultDock(Rectangle defaultDockPanel, Font font)
{
    (void)font;
    const ConverterSettingsSnapshot beforeGlobalEdit = CaptureConverterSettings();

    const bool useDefaultForLayout = converterCardOptionsUseDefault_ && !converterCardOptionsUseDefaultMixed_;
    const bool showCustomMixedHint = converterCardOptionsCustomMixed_ && !useDefaultForLayout;

    ConverterOptionsDockLayout layout = GetConverterOptionsDockLayout(defaultDockPanel,
                                                                      converterSectionFoldout_.IsExpanded(),
                                                                      converterDefaultFoldout_.IsExpanded(),
                                                                      converterCustomFoldout_.IsExpanded(),
                                                                      showCustomMixedHint);
    converterSectionFoldout_.SyncPanelBounds(layout.sectionFoldoutPanelBounds);
    converterDefaultFoldout_.SyncPanelBounds(layout.globalFoldoutPanelBounds);
    converterCustomFoldout_.SyncPanelBounds(layout.customFoldoutPanelBounds);

    static const std::vector<std::string> kContainers = {"MP4", "MKV", "MOV", "WEBM"};
    convertContainerDropdown_.SetItems(kContainers);
    cardConvertContainerDropdown_.SetItems(kContainers);
    if (convertContainerIndex_ < 0 || convertContainerIndex_ >= static_cast<int>(kContainers.size()))
    {
        convertContainerIndex_ = 0;
    }

    ConverterOptions& customOptions = converterCardOptionsEdit_;
    if (customOptions.containerIndex < 0 || customOptions.containerIndex >= static_cast<int>(kContainers.size()))
    {
        customOptions.containerIndex = 0;
    }

    const std::string containerFallback = "MP4";

    const auto capturePreferredGlobalCodec = [&](bool isVideo) -> std::string
    {
        const std::string effective =
            EffectiveConvertContainer(convertContainer_, convertContainerIndex_, kContainers, containerFallback);
        if (isVideo)
        {
            const std::vector<std::string> videos = CompatibleVideoCodecsForContainer(effective);
            if (videos.empty())
            {
                return DefaultVideoCodecForContainer(effective);
            }
            const int index = std::clamp(convertVideoIndex_, 0, static_cast<int>(videos.size()) - 1);
            return videos[index];
        }
        const std::vector<std::string> audios = CompatibleAudioCodecsForContainer(effective);
        if (audios.empty())
        {
            return DefaultAudioCodecForContainer(effective);
        }
        const int index = std::clamp(convertAudioIndex_, 0, static_cast<int>(audios.size()) - 1);
        return audios[index];
    };
    const auto capturePreferredCustomCodec = [&](bool isVideo) -> std::string
    {
        const std::string effective = EffectiveConvertContainer(
            customOptions.convertContainer, customOptions.containerIndex, kContainers, containerFallback);
        if (isVideo)
        {
            const std::vector<std::string> videos = CompatibleVideoCodecsForContainer(effective);
            if (videos.empty())
            {
                return DefaultVideoCodecForContainer(effective);
            }
            const int index = std::clamp(customOptions.videoIndex, 0, static_cast<int>(videos.size()) - 1);
            return videos[index];
        }
        const std::vector<std::string> audios = CompatibleAudioCodecsForContainer(effective);
        if (audios.empty())
        {
            return DefaultAudioCodecForContainer(effective);
        }
        const int index = std::clamp(customOptions.audioIndex, 0, static_cast<int>(audios.size()) - 1);
        return audios[index];
    };
    const std::string preferredGlobalVideoCodec = capturePreferredGlobalCodec(true);
    const std::string preferredGlobalAudioCodec = capturePreferredGlobalCodec(false);
    const std::string preferredCustomVideoCodec = capturePreferredCustomCodec(true);
    const std::string preferredCustomAudioCodec = capturePreferredCustomCodec(false);

    const auto syncGlobalCodecDropdowns = [&]()
    {
        const std::string effective =
            EffectiveConvertContainer(convertContainer_, convertContainerIndex_, kContainers, containerFallback);
        const std::vector<std::string> videos = CompatibleVideoCodecsForContainer(effective);
        const std::vector<std::string> audios = CompatibleAudioCodecsForContainer(effective);
        convertVideoDropdown_.SetItems(videos);
        convertAudioDropdown_.SetItems(audios);
        convertVideoIndex_ =
            FindCodecIndex(videos, preferredGlobalVideoCodec, DefaultVideoCodecForContainer(effective));
        convertAudioIndex_ =
            FindCodecIndex(audios, preferredGlobalAudioCodec, DefaultAudioCodecForContainer(effective));
    };
    const auto syncCustomCodecDropdowns = [&]()
    {
        const std::string effective = EffectiveConvertContainer(
            customOptions.convertContainer, customOptions.containerIndex, kContainers, containerFallback);
        const std::vector<std::string> videos = CompatibleVideoCodecsForContainer(effective);
        const std::vector<std::string> audios = CompatibleAudioCodecsForContainer(effective);
        cardConvertVideoDropdown_.SetItems(videos);
        cardConvertAudioDropdown_.SetItems(audios);
        customOptions.videoIndex =
            videos.empty()
                ? 0
                : FindCodecIndex(videos, preferredCustomVideoCodec, DefaultVideoCodecForContainer(effective));
        customOptions.audioIndex =
            audios.empty()
                ? 0
                : FindCodecIndex(audios, preferredCustomAudioCodec, DefaultAudioCodecForContainer(effective));
    };
    syncGlobalCodecDropdowns();
    syncCustomCodecDropdowns();

    const bool dropdownOpen = convertContainerDropdown_.IsOpen() || convertVideoDropdown_.IsOpen() ||
                              convertAudioDropdown_.IsOpen() || cardConvertContainerDropdown_.IsOpen() ||
                              cardConvertVideoDropdown_.IsOpen() || cardConvertAudioDropdown_.IsOpen();
    const float popupBottom = defaultDockPanel.y + defaultDockPanel.height + 400.0f;
    convertContainerDropdown_.SetPopupLimitY(defaultDockPanel.y, popupBottom);
    convertVideoDropdown_.SetPopupLimitY(defaultDockPanel.y, popupBottom);
    convertAudioDropdown_.SetPopupLimitY(defaultDockPanel.y, popupBottom);
    cardConvertContainerDropdown_.SetPopupLimitY(defaultDockPanel.y, popupBottom);
    cardConvertVideoDropdown_.SetPopupLimitY(defaultDockPanel.y, popupBottom);
    cardConvertAudioDropdown_.SetPopupLimitY(defaultDockPanel.y, popupBottom);

    if (converterSectionFoldout_.Update(layout.sectionHeaderBounds, !dropdownOpen) &&
        !converterSectionFoldout_.IsExpanded())
    {
        convertContainerDropdown_.Close();
        convertVideoDropdown_.Close();
        convertAudioDropdown_.Close();
        cardConvertContainerDropdown_.Close();
        cardConvertVideoDropdown_.Close();
        cardConvertAudioDropdown_.Close();
    }

    layout = GetConverterOptionsDockLayout(defaultDockPanel,
                                           converterSectionFoldout_.IsExpanded(),
                                           converterDefaultFoldout_.IsExpanded(),
                                           converterCustomFoldout_.IsExpanded(),
                                           showCustomMixedHint);
    converterSectionFoldout_.SyncPanelBounds(layout.sectionFoldoutPanelBounds);
    converterDefaultFoldout_.SyncPanelBounds(layout.globalFoldoutPanelBounds);
    converterCustomFoldout_.SyncPanelBounds(layout.customFoldoutPanelBounds);

    if (!converterSectionFoldout_.IsExpanded())
    {
        convertContainerDropdown_.Close();
        convertVideoDropdown_.Close();
        convertAudioDropdown_.Close();
        cardConvertContainerDropdown_.Close();
        cardConvertVideoDropdown_.Close();
        cardConvertAudioDropdown_.Close();
        PushUndo(MakeConverterSettingsCommand(beforeGlobalEdit, CaptureConverterSettings()));
        return dropdownOpen;
    }

    std::vector<ConverterFileCardNode*> editableCards = CollectEditableSelectedConverterCards();
    const std::string selectionKey = BuildConverterCardOptionsSelectionKey();
    if (selectionKey != converterCardOptionsSelectionKey_)
    {
        converterCardOptionsSelectionKey_ = selectionKey;
        if (!editableCards.empty())
        {
            SyncConverterCardOptionsEditFromSelection(editableCards);
        }
        else
        {
            converterCardOptionsUseDefault_ = true;
            converterCardOptionsUseDefaultMixed_ = false;
            converterCardOptionsCustomMixed_ = false;
            converterCardOptionsEdit_ = {};
        }
        cardConvertContainerDropdown_.Close();
        cardConvertVideoDropdown_.Close();
        cardConvertAudioDropdown_.Close();
    }

    const bool customUiAvailable = !editableCards.empty();
    const std::vector<ConverterCardOptionsSnapshot> beforeCustomEdit =
        customUiAvailable ? CaptureConverterCardOptionsSnapshots(editableCards)
                          : std::vector<ConverterCardOptionsSnapshot>{};
    const ConverterOptions customEditAtStart = converterCardOptionsEdit_;
    const bool useDefaultAtStart = converterCardOptionsUseDefault_;
    const bool useDefaultMixedAtStart = converterCardOptionsUseDefaultMixed_;

    const bool wasAllDefault = converterCardOptionsUseDefault_ && !converterCardOptionsUseDefaultMixed_;
    bool useCustomDisplay = !converterCardOptionsUseDefault_;
    if (converterCardOptionsUseDefaultMixed_)
    {
        useCustomDisplay = false;
    }

    if (!dropdownOpen && customUiAvailable)
    {
        converterUseDefaultCheckbox_.Update(
            converterCustomFoldout_.HeaderCheckboxBounds(layout.customFoldoutPanelBounds), useCustomDisplay);
    }
    if (converterCardOptionsUseDefaultMixed_)
    {
        converterCardOptionsUseDefault_ = !useCustomDisplay;
        converterCardOptionsUseDefaultMixed_ = false;
    }
    else if (customUiAvailable)
    {
        converterCardOptionsUseDefault_ = !useCustomDisplay;
    }
    if (customUiAvailable && useCustomDisplay && wasAllDefault)
    {
        const ConverterSettingsSnapshot globalSettings = CaptureConverterSettings();
        customOptions = {globalSettings.convertContainer,
                         globalSettings.convertVideo,
                         globalSettings.convertAudio,
                         globalSettings.convertContainerIndex,
                         globalSettings.convertVideoIndex,
                         globalSettings.convertAudioIndex};
        syncCustomCodecDropdowns();
        converterCustomFoldout_.SetExpanded(true);
        converterSectionFoldout_.SetExpanded(true);
    }

    if (converterDefaultFoldout_.Update(layout.globalHeaderBounds, !dropdownOpen) &&
        !converterDefaultFoldout_.IsExpanded())
    {
        convertContainerDropdown_.Close();
        convertVideoDropdown_.Close();
        convertAudioDropdown_.Close();
    }
    if (customUiAvailable && converterCustomFoldout_.Update(layout.customHeaderBounds, !dropdownOpen) &&
        !converterCustomFoldout_.IsExpanded())
    {
        cardConvertContainerDropdown_.Close();
        cardConvertVideoDropdown_.Close();
        cardConvertAudioDropdown_.Close();
    }

    const bool useDefaultForLayoutAfter = converterCardOptionsUseDefault_ && !converterCardOptionsUseDefaultMixed_;
    const bool showCustomMixedHintAfter = converterCardOptionsCustomMixed_ && !useDefaultForLayoutAfter;
    layout = GetConverterOptionsDockLayout(defaultDockPanel,
                                           converterSectionFoldout_.IsExpanded(),
                                           converterDefaultFoldout_.IsExpanded(),
                                           converterCustomFoldout_.IsExpanded(),
                                           showCustomMixedHintAfter);
    converterSectionFoldout_.SyncPanelBounds(layout.sectionFoldoutPanelBounds);
    converterDefaultFoldout_.SyncPanelBounds(layout.globalFoldoutPanelBounds);
    converterCustomFoldout_.SyncPanelBounds(layout.customFoldoutPanelBounds);

    const float dropdownX = layout.nestedDropdownX;
    const float dropdownW = layout.nestedDropdownW;
    const float checkX = layout.nestedCheckX;
    const Rectangle globalContainerBounds = {dropdownX, layout.globalContainerY, dropdownW, 25.0f};
    const Rectangle globalVideoBounds = {dropdownX, layout.globalVideoY, dropdownW, 25.0f};
    const Rectangle globalAudioBounds = {dropdownX, layout.globalAudioY, dropdownW, 25.0f};
    const Rectangle customContainerBounds = {dropdownX, layout.customContainerY, dropdownW, 25.0f};
    const Rectangle customVideoBounds = {dropdownX, layout.customVideoY, dropdownW, 25.0f};
    const Rectangle customAudioBounds = {dropdownX, layout.customAudioY, dropdownW, 25.0f};

    if (!converterDefaultFoldout_.IsExpanded())
    {
        convertContainerDropdown_.Close();
        convertVideoDropdown_.Close();
        convertAudioDropdown_.Close();
    }
    if (!customUiAvailable || !converterCustomFoldout_.IsExpanded() || converterCardOptionsUseDefault_)
    {
        cardConvertContainerDropdown_.Close();
        cardConvertVideoDropdown_.Close();
        cardConvertAudioDropdown_.Close();
    }
    if (!convertContainer_)
    {
        convertContainerDropdown_.Close();
    }
    if (!convertVideo_)
    {
        convertVideoDropdown_.Close();
    }
    if (!convertAudio_)
    {
        convertAudioDropdown_.Close();
    }
    if (!customOptions.convertContainer)
    {
        cardConvertContainerDropdown_.Close();
    }
    if (!customOptions.convertVideo)
    {
        cardConvertVideoDropdown_.Close();
    }
    if (!customOptions.convertAudio)
    {
        cardConvertAudioDropdown_.Close();
    }

    const bool globalSection = converterDefaultFoldout_.IsExpanded();
    const bool customSection =
        customUiAvailable && converterCustomFoldout_.IsExpanded() && !converterCardOptionsUseDefault_;
    Dropdown::Slot dockStack[] = {
        {&cardConvertAudioDropdown_,
         customAudioBounds,
         &customOptions.audioIndex,
         customSection && customOptions.convertAudio,
         &defaultDockPanel},
        {&cardConvertVideoDropdown_,
         customVideoBounds,
         &customOptions.videoIndex,
         customSection && customOptions.convertVideo,
         &defaultDockPanel},
        {&cardConvertContainerDropdown_,
         customContainerBounds,
         &customOptions.containerIndex,
         customSection && customOptions.convertContainer,
         &defaultDockPanel},
        {&convertAudioDropdown_,
         globalAudioBounds,
         &convertAudioIndex_,
         globalSection && convertAudio_,
         &defaultDockPanel},
        {&convertVideoDropdown_,
         globalVideoBounds,
         &convertVideoIndex_,
         globalSection && convertVideo_,
         &defaultDockPanel},
        {&convertContainerDropdown_,
         globalContainerBounds,
         &convertContainerIndex_,
         globalSection && convertContainer_,
         &defaultDockPanel},
    };

    int consumedIdx = Dropdown::UpdateOpenPopups(dockStack, 6);
    if (consumedIdx < 0)
    {
        Dropdown::Slot closedStack[] = {
            dockStack[5],
            dockStack[4],
            dockStack[3],
            dockStack[2],
            dockStack[1],
            dockStack[0],
        };
        const int closedIdx = Dropdown::UpdateClosedControls(closedStack, 6);
        if (closedIdx >= 0)
        {
            static constexpr int kClosedToOpen[] = {5, 4, 3, 2, 1, 0};
            consumedIdx = kClosedToOpen[closedIdx];
        }
    }

    const bool customContainerConsumed = consumedIdx == 2;
    const bool globalContainerConsumed = consumedIdx == 5;

    if (globalSection && (!convertContainer_ || globalContainerConsumed))
    {
        syncGlobalCodecDropdowns();
    }
    if (customSection && (!customOptions.convertContainer || customContainerConsumed))
    {
        syncCustomCodecDropdowns();
    }

    const bool dropdownBlocksInput = Dropdown::AnyOpen(dockStack, 6) || consumedIdx >= 0;
    if (!dropdownBlocksInput)
    {
        if (globalSection)
        {
            const bool prevConvertContainer = convertContainer_;
            convertContainerCheckbox_.Update({checkX, layout.globalContainerY + 3.0f, 110.0f, 18.0f},
                                             convertContainer_,
                                             &converterGlobalCodecPaint_);
            convertVideoCheckbox_.Update(
                {checkX, layout.globalVideoY + 3.0f, 90.0f, 18.0f}, convertVideo_, &converterGlobalCodecPaint_);
            convertAudioCheckbox_.Update(
                {checkX, layout.globalAudioY + 3.0f, 90.0f, 18.0f}, convertAudio_, &converterGlobalCodecPaint_);
            if (prevConvertContainer != convertContainer_ || globalContainerConsumed)
            {
                syncGlobalCodecDropdowns();
            }
        }
        if (customSection)
        {
            const bool prevConvertContainer = customOptions.convertContainer;
            cardConvertContainerCheckbox_.Update({checkX, layout.customContainerY + 3.0f, 110.0f, 18.0f},
                                                 customOptions.convertContainer,
                                                 &converterCustomCodecPaint_);
            cardConvertVideoCheckbox_.Update({checkX, layout.customVideoY + 3.0f, 90.0f, 18.0f},
                                             customOptions.convertVideo,
                                             &converterCustomCodecPaint_);
            cardConvertAudioCheckbox_.Update({checkX, layout.customAudioY + 3.0f, 90.0f, 18.0f},
                                             customOptions.convertAudio,
                                             &converterCustomCodecPaint_);
            if (prevConvertContainer != customOptions.convertContainer || customContainerConsumed)
            {
                syncCustomCodecDropdowns();
            }
        }
    }

    if (converterCardOptionsUseDefault_)
    {
        // Keep disabled rows in sync with Global Options after this frame's global edits.
        const ConverterSettingsSnapshot globalSettings = CaptureConverterSettings();
        customOptions = {globalSettings.convertContainer,
                         globalSettings.convertVideo,
                         globalSettings.convertAudio,
                         globalSettings.convertContainerIndex,
                         globalSettings.convertVideoIndex,
                         globalSettings.convertAudioIndex};
        const std::string effective = EffectiveConvertContainer(
            customOptions.convertContainer, customOptions.containerIndex, kContainers, kContainers[0]);
        cardConvertContainerDropdown_.SetItems(kContainers);
        cardConvertVideoDropdown_.SetItems(CompatibleVideoCodecsForContainer(effective));
        cardConvertAudioDropdown_.SetItems(CompatibleAudioCodecsForContainer(effective));
    }

    PushUndo(MakeConverterSettingsCommand(beforeGlobalEdit, CaptureConverterSettings()));

    // Global mirror while Use Custom is off must not count as a user edit.
    if (customUiAvailable)
    {
        const bool userEdited = (!converterCardOptionsUseDefault_ && converterCardOptionsEdit_ != customEditAtStart) ||
                                converterCardOptionsUseDefault_ != useDefaultAtStart ||
                                (useDefaultMixedAtStart && !converterCardOptionsUseDefaultMixed_);
        if (userEdited)
        {
            ApplyConverterCardOptionsEditToCards(editableCards);
            converterCardOptionsCustomMixed_ = false;
            converterCardOptionsUseDefaultMixed_ = false;

            const std::vector<ConverterCardOptionsSnapshot> afterEdit =
                CaptureConverterCardOptionsSnapshots(editableCards);
            if (editableCards.size() == 1)
            {
                PushUndo(MakeConverterCardOptionsCommand(
                    beforeCustomEdit.front().filePath, beforeCustomEdit.front(), afterEdit.front()));
            }
            else
            {
                PushUndo(MakeConverterCardOptionsBatchCommand(beforeCustomEdit, afterEdit));
            }
        }
    }

    return dropdownBlocksInput;
}

void DockArea::DrawConverterDefaultDock(Rectangle defaultDockPanel, Font font, bool drawControls, bool drawPopups) const
{
    const bool useDefaultForLayout = converterCardOptionsUseDefault_ && !converterCardOptionsUseDefaultMixed_;
    const bool showCustomMixedHint = converterCardOptionsCustomMixed_ && !useDefaultForLayout;
    const ConverterOptionsDockLayout layout = GetConverterOptionsDockLayout(defaultDockPanel,
                                                                            converterSectionFoldout_.IsExpanded(),
                                                                            converterDefaultFoldout_.IsExpanded(),
                                                                            converterCustomFoldout_.IsExpanded(),
                                                                            showCustomMixedHint);

    const bool anyGlobalDropdownOpen =
        convertContainerDropdown_.IsOpen() || convertVideoDropdown_.IsOpen() || convertAudioDropdown_.IsOpen();
    const bool anyCustomDropdownOpen = cardConvertContainerDropdown_.IsOpen() || cardConvertVideoDropdown_.IsOpen() ||
                                       cardConvertAudioDropdown_.IsOpen();
    const bool anyDockDropdownOpen = anyGlobalDropdownOpen || anyCustomDropdownOpen;

    bool customUiEnabled = false;
    for (const ConverterFileCardNode& card : converterCards_)
    {
        if (!card.IsSelected() || !card.HasFile() || card.IsLoading() || card.IsConverting())
        {
            continue;
        }
        customUiEnabled = true;
        break;
    }

    const bool useCustomDisplay = converterCardOptionsUseDefaultMixed_ ? false : !converterCardOptionsUseDefault_;
    const ConverterOptions& customOptions = converterCardOptionsEdit_;

    if (drawControls)
    {
        converterSectionFoldout_.Draw(layout.sectionFoldoutPanelBounds, font, true);

        if (converterSectionFoldout_.IsExpanded())
        {
            const float checkX = layout.nestedCheckX;
            const float dropdownX = layout.nestedDropdownX;
            const float dropdownW = layout.nestedDropdownW;

            converterDefaultFoldout_.Draw(layout.globalFoldoutPanelBounds, font, true);

            const float labelToDropdown = std::max(4.0f, dropdownX - (checkX + 18.0f + 8.0f) - 4.0f);

            if (converterDefaultFoldout_.IsExpanded())
            {
                convertContainerCheckbox_.Draw({checkX, layout.globalContainerY + 3.0f, 18.0f, 18.0f},
                                               font,
                                               "Container",
                                               convertContainer_,
                                               true,
                                               labelToDropdown);
                convertVideoCheckbox_.Draw({checkX, layout.globalVideoY + 3.0f, 18.0f, 18.0f},
                                           font,
                                           "Video",
                                           convertVideo_,
                                           true,
                                           labelToDropdown);
                convertAudioCheckbox_.Draw({checkX, layout.globalAudioY + 3.0f, 18.0f, 18.0f},
                                           font,
                                           "Audio",
                                           convertAudio_,
                                           true,
                                           labelToDropdown);

                convertContainerDropdown_.DrawControl({dropdownX, layout.globalContainerY, dropdownW, 25.0f},
                                                      font,
                                                      convertContainerIndex_,
                                                      convertContainer_,
                                                      !anyDockDropdownOpen || convertContainerDropdown_.IsOpen());
                convertVideoDropdown_.DrawControl({dropdownX, layout.globalVideoY, dropdownW, 25.0f},
                                                  font,
                                                  convertVideoIndex_,
                                                  convertVideo_,
                                                  !anyDockDropdownOpen || convertVideoDropdown_.IsOpen());
                convertAudioDropdown_.DrawControl({dropdownX, layout.globalAudioY, dropdownW, 25.0f},
                                                  font,
                                                  convertAudioIndex_,
                                                  convertAudio_,
                                                  !anyDockDropdownOpen || convertAudioDropdown_.IsOpen());
            }

            converterCustomFoldout_.Draw(layout.customFoldoutPanelBounds, font, customUiEnabled);
            converterUseDefaultCheckbox_.Draw(
                converterCustomFoldout_.HeaderCheckboxBounds(layout.customFoldoutPanelBounds),
                font,
                "",
                useCustomDisplay,
                customUiEnabled);
            if (converterCardOptionsUseDefaultMixed_)
            {
                const Color muted = {150, 162, 150, 255};
                const Rectangle checkboxBounds =
                    converterCustomFoldout_.HeaderCheckboxBounds(layout.customFoldoutPanelBounds);
                DrawTextEx(font,
                           "(mixed)",
                           {checkboxBounds.x + checkboxBounds.width + 8.0f, checkboxBounds.y},
                           13.0f,
                           0.0f,
                           muted);
            }

            if (converterCustomFoldout_.IsExpanded())
            {
                const bool customControlsEnabled = customUiEnabled && useCustomDisplay;
                if (showCustomMixedHint)
                {
                    const Color muted = {150, 162, 150, 255};
                    DrawWrappedText(font,
                                    "Mixed settings вЂ” edits apply to all selected.",
                                    {layout.customFoldoutPanelBounds.x + 12.0f, layout.customMixedHintY},
                                    13.0f,
                                    layout.customFoldoutPanelBounds.width - 24.0f,
                                    2,
                                    muted);
                }

                cardConvertContainerCheckbox_.Draw({checkX, layout.customContainerY + 3.0f, 18.0f, 18.0f},
                                                   font,
                                                   "Container",
                                                   customOptions.convertContainer,
                                                   customControlsEnabled,
                                                   labelToDropdown);
                cardConvertVideoCheckbox_.Draw({checkX, layout.customVideoY + 3.0f, 18.0f, 18.0f},
                                               font,
                                               "Video",
                                               customOptions.convertVideo,
                                               customControlsEnabled,
                                               labelToDropdown);
                cardConvertAudioCheckbox_.Draw({checkX, layout.customAudioY + 3.0f, 18.0f, 18.0f},
                                               font,
                                               "Audio",
                                               customOptions.convertAudio,
                                               customControlsEnabled,
                                               labelToDropdown);

                cardConvertContainerDropdown_.DrawControl(
                    {dropdownX, layout.customContainerY, dropdownW, 25.0f},
                    font,
                    customOptions.containerIndex,
                    customControlsEnabled && customOptions.convertContainer,
                    customUiEnabled && (!anyDockDropdownOpen || cardConvertContainerDropdown_.IsOpen()));
                cardConvertVideoDropdown_.DrawControl({dropdownX, layout.customVideoY, dropdownW, 25.0f},
                                                      font,
                                                      customOptions.videoIndex,
                                                      customControlsEnabled && customOptions.convertVideo,
                                                      customUiEnabled &&
                                                          (!anyDockDropdownOpen || cardConvertVideoDropdown_.IsOpen()));
                cardConvertAudioDropdown_.DrawControl({dropdownX, layout.customAudioY, dropdownW, 25.0f},
                                                      font,
                                                      customOptions.audioIndex,
                                                      customControlsEnabled && customOptions.convertAudio,
                                                      customUiEnabled &&
                                                          (!anyDockDropdownOpen || cardConvertAudioDropdown_.IsOpen()));
            }
        }
    }

    if (drawPopups && converterSectionFoldout_.IsExpanded())
    {
        const float dropdownX = layout.nestedDropdownX;
        const float dropdownW = layout.nestedDropdownW;
        if (converterDefaultFoldout_.IsExpanded())
        {
            if (convertContainer_)
            {
                convertContainerDropdown_.DrawPopup(
                    {dropdownX, layout.globalContainerY, dropdownW, 25.0f}, font, convertContainerIndex_);
            }
            if (convertVideo_)
            {
                convertVideoDropdown_.DrawPopup(
                    {dropdownX, layout.globalVideoY, dropdownW, 25.0f}, font, convertVideoIndex_);
            }
            if (convertAudio_)
            {
                convertAudioDropdown_.DrawPopup(
                    {dropdownX, layout.globalAudioY, dropdownW, 25.0f}, font, convertAudioIndex_);
            }
        }
        if (customUiEnabled && converterCustomFoldout_.IsExpanded() && useCustomDisplay)
        {
            if (customOptions.convertContainer)
            {
                cardConvertContainerDropdown_.DrawPopup(
                    {dropdownX, layout.customContainerY, dropdownW, 25.0f}, font, customOptions.containerIndex);
            }
            if (customOptions.convertVideo)
            {
                cardConvertVideoDropdown_.DrawPopup(
                    {dropdownX, layout.customVideoY, dropdownW, 25.0f}, font, customOptions.videoIndex);
            }
            if (customOptions.convertAudio)
            {
                cardConvertAudioDropdown_.DrawPopup(
                    {dropdownX, layout.customAudioY, dropdownW, 25.0f}, font, customOptions.audioIndex);
            }
        }
    }
}

void DockArea::UpdateConverterCardOptions(Rectangle settingsPanel, Font font, bool blockByUpperOverlay)
{
    (void)font;
    if (blockByUpperOverlay)
    {
        return;
    }

    const Rectangle convertButton = GetDownloadButtonBounds(settingsPanel);
    const Rectangle optionsViewport = GetOptionsScrollViewport(settingsPanel, convertButton.y);
    const float contentHeight = GetConverterOptionsContentHeight(settingsPanel.y, converterResultSection_.IsExpanded());
    const float maxOptionsScroll = std::max(0.0f, contentHeight - optionsViewport.height);

    if (CheckCollisionPointRec(GetMousePosition(), optionsViewport))
    {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            converterOptionsScrollOffset_ =
                std::clamp(converterOptionsScrollOffset_ - wheel * 40.0f, 0.0f, maxOptionsScroll);
        }
    }
    converterOptionsScrollOffset_ = std::clamp(converterOptionsScrollOffset_, 0.0f, maxOptionsScroll);

    const Rectangle resultFoldoutBounds = GetConverterResultFoldoutBounds(
        settingsPanel, converterResultSection_.IsExpanded(), converterOptionsScrollOffset_);
    SyncFoldoutToClip(converterResultSection_, resultFoldoutBounds, optionsViewport);

    const Rectangle resultHeaderBounds = {
        resultFoldoutBounds.x, resultFoldoutBounds.y, resultFoldoutBounds.width, FoldoutPanel::HeaderHeight()};
    if (MouseHitsClippedControl(resultHeaderBounds, optionsViewport))
    {
        converterResultSection_.Update(resultHeaderBounds);
    }

    {
        const float contentHeightAfter =
            GetConverterOptionsContentHeight(settingsPanel.y, converterResultSection_.IsExpanded());
        const float maxAfter = std::max(0.0f, contentHeightAfter - optionsViewport.height);
        converterOptionsScrollOffset_ = std::clamp(converterOptionsScrollOffset_, 0.0f, maxAfter);
    }
}

void DockArea::DrawConverterCardOptions(Rectangle settingsPanel, Font font) const
{
    const Color text = {224, 230, 224, 255};
    const Color muted = {150, 162, 150, 255};

    {
        const float optionsTitleMax = std::max(4.0f, settingsPanel.width - 24.0f);
        const std::string optionsTitle = TruncateTextToWidth(font, "Options", 16.0f, optionsTitleMax);
        DrawTextEx(font, optionsTitle.c_str(), {settingsPanel.x + 12.0f, settingsPanel.y + 12.0f}, 16.0f, 0.0f, text);
    }

    const Rectangle convertButton = GetDownloadButtonBounds(settingsPanel);
    const ConverterFileCardNode* selectedCard = GetSelectedConverterCard();

    if (selectedCard == nullptr)
    {
        const float hintX = settingsPanel.x + 14.0f;
        const float hintY = settingsPanel.y + 40.0f;
        const float hintW = std::max(4.0f, settingsPanel.width - 28.0f);
        const float hintH = std::max(0.0f, convertButton.y - 8.0f - hintY);
        const int maxLines = std::max(1, static_cast<int>(hintH / 18.0f));
        if (hintH > 4.0f)
        {
            UiClip::Push({hintX, hintY, hintW, hintH});
            DrawWrappedText(
                font, "Select a valid card to edit convert settings.", {hintX, hintY}, 15.0f, hintW, maxLines, muted);
            UiClip::Pop();
        }
    }
    else
    {
        const Rectangle optionsViewport = GetOptionsScrollViewport(settingsPanel, convertButton.y);
        const float contentHeight =
            GetConverterOptionsContentHeight(settingsPanel.y, converterResultSection_.IsExpanded());
        const float maxOptionsScroll = std::max(0.0f, contentHeight - optionsViewport.height);
        const float scrollOffset = std::clamp(converterOptionsScrollOffset_, 0.0f, maxOptionsScroll);
        const Rectangle resultFoldoutBounds =
            GetConverterResultFoldoutBounds(settingsPanel, converterResultSection_.IsExpanded(), scrollOffset);
        const float headerH = FoldoutPanel::HeaderHeight();
        const bool resultEnabled = selectedCard->HasFile() && !selectedCard->IsLoading();

        BeginOptionsContentScissor(optionsViewport);
        converterResultSection_.Draw(resultFoldoutBounds, font, resultEnabled);

        if (resultEnabled && converterResultSection_.IsExpanded())
        {
            PredictedDownload prediction;
            const ConverterFileInfo& info = selectedCard->Info();
            prediction.container = info.container;
            prediction.videoCodec = info.videoCodec;
            prediction.audioCodec = info.audioCodec;
            prediction.resolution = info.resolution;
            prediction.converting = false;

            const ConverterSettingsSnapshot settings = ResolveConverterSettingsForCard(*selectedCard);
            AutoConvertOptions convertOptions;
            convertOptions.enabled = settings.convertContainer || settings.convertVideo || settings.convertAudio;
            convertOptions.convertContainer = settings.convertContainer;
            convertOptions.convertVideo = settings.convertVideo;
            convertOptions.convertAudio = settings.convertAudio;
            convertOptions.containerIndex = settings.convertContainerIndex;
            convertOptions.videoIndex = settings.convertVideoIndex;
            convertOptions.audioIndex = settings.convertAudioIndex;
            prediction = ApplyAutoConvertToPrediction(prediction, convertOptions);

            const float resultLineY = resultFoldoutBounds.y + headerH + FoldoutPanel::ContentTopPadding();
            DrawDownloadResultPreview(font, settingsPanel, prediction, resultLineY, false);
        }

        DrawOptionsScrollbar(optionsViewport, scrollOffset, maxOptionsScroll);
        UiClip::Pop();
    }

    const bool canConvertSelected = std::any_of(converterCards_.begin(),
                                                converterCards_.end(),
                                                [this](const ConverterFileCardNode& card)
                                                {
                                                    if (!card.IsSelected())
                                                    {
                                                        return false;
                                                    }
                                                    ConvertRequest request;
                                                    return BuildConvertRequestForCard(card, request);
                                                });
    if (SelectedConverterShowsCancel())
    {
        cancelDownloadButton_.DrawDanger(convertButton, font);
        if (CanBuildAnyConvertRequest())
        {
            convertAllButton_.Draw(GetSecondaryActionButtonBounds(settingsPanel), font);
        }
        else
        {
            cancelAllActionButton_.DrawDanger(GetSecondaryActionButtonBounds(settingsPanel), font);
        }
    }
    else if (HasActiveConverterWorkspaceWork())
    {
        convertButton_.Draw(convertButton, font, canConvertSelected);
        cancelAllActionButton_.DrawDanger(GetSecondaryActionButtonBounds(settingsPanel), font);
    }
    else
    {
        convertButton_.Draw(convertButton, font, canConvertSelected);
        convertAllButton_.Draw(GetSecondaryActionButtonBounds(settingsPanel), font, CanBuildAnyConvertRequest());
    }
}

bool DockArea::UpdateAutoConvertDock(Rectangle autoConvertPanel, Font font)
{
    (void)font;
    const AutoConvertOptions beforeGlobalEdit = globalAutoConvert_;
    AutoConvertOptions& autoConvert = globalAutoConvert_;
    AutoConvertOptions& customConvert = customAutoConvert_;

    AutoConvertDockLayout layout = GetAutoConvertDockLayout(autoConvertPanel,
                                                            autoConvertSectionFoldout_.IsExpanded(),
                                                            autoConvertFoldout_.IsExpanded(),
                                                            customAutoConvertFoldout_.IsExpanded());
    autoConvertSectionFoldout_.SyncPanelBounds(layout.sectionFoldoutPanelBounds);
    autoConvertFoldout_.SyncPanelBounds(layout.autoFoldoutPanelBounds);
    customAutoConvertFoldout_.SyncPanelBounds(layout.customFoldoutPanelBounds);

    static const std::vector<std::string> kAutoContainers = {"MP4", "MKV", "MOV", "WEBM"};
    autoConvertContainerDropdown_.SetItems(kAutoContainers);
    customAutoConvertContainerDropdown_.SetItems(kAutoContainers);
    if (autoConvert.containerIndex < 0 || autoConvert.containerIndex >= static_cast<int>(kAutoContainers.size()))
    {
        autoConvert.containerIndex = 0;
    }
    if (customConvert.containerIndex < 0 || customConvert.containerIndex >= static_cast<int>(kAutoContainers.size()))
    {
        customConvert.containerIndex = 0;
    }

    std::string downloadFormatFallback = "MP4";
    if (const LinkCardNode* selectedCard = GetSelectedCard(); selectedCard != nullptr && selectedCard->IsValid())
    {
        const DownloadOptions& options = selectedCard->Options();
        std::vector<std::string> availableFormats =
            options.mediaMode == 2 ? selectedCard->AvailableAudioFormats() : selectedCard->AvailableVideoFormats();
        if (availableFormats.empty())
        {
            availableFormats.push_back(options.mediaMode == 2 ? "M4A" : "MP4");
        }
        const std::vector<std::string> availableQualities = selectedCard->AvailableQualities();
        const std::string selectedQuality =
            availableQualities.empty()
                ? std::string{}
                : availableQualities[std::clamp(options.quality, 0, static_cast<int>(availableQualities.size()) - 1)];
        availableFormats = BuildFormatItemsForQuality(
            availableFormats, selectedCard->FormatStreams(), selectedQuality, options.mediaMode);
        if (!availableFormats.empty())
        {
            const int formatIndex = std::clamp(options.fileFormat, 0, static_cast<int>(availableFormats.size()) - 1);
            downloadFormatFallback = StripFormatItemLabel(availableFormats[formatIndex]);
        }
    }

    const auto capturePreferredCodec = [&](const AutoConvertOptions& options, bool isVideo) -> std::string
    {
        const std::string effective = EffectiveConvertContainer(
            options.convertContainer, options.containerIndex, kAutoContainers, downloadFormatFallback);
        if (isVideo)
        {
            const std::vector<std::string> videos = CompatibleVideoCodecsForContainer(effective);
            if (videos.empty())
            {
                return DefaultVideoCodecForContainer(effective);
            }
            const int index = std::clamp(options.videoIndex, 0, static_cast<int>(videos.size()) - 1);
            return videos[index];
        }
        const std::vector<std::string> audios = CompatibleAudioCodecsForContainer(effective);
        if (audios.empty())
        {
            return DefaultAudioCodecForContainer(effective);
        }
        const int index = std::clamp(options.audioIndex, 0, static_cast<int>(audios.size()) - 1);
        return audios[index];
    };
    const std::string preferredAutoVideoCodec = capturePreferredCodec(autoConvert, true);
    const std::string preferredAutoAudioCodec = capturePreferredCodec(autoConvert, false);
    const std::string preferredCustomVideoCodec = capturePreferredCodec(customConvert, true);
    const std::string preferredCustomAudioCodec = capturePreferredCodec(customConvert, false);

    const auto syncAutoConvertCodecDropdowns = [&]()
    {
        const std::string effective = EffectiveConvertContainer(
            autoConvert.convertContainer, autoConvert.containerIndex, kAutoContainers, downloadFormatFallback);
        const std::vector<std::string> videos = CompatibleVideoCodecsForContainer(effective);
        const std::vector<std::string> audios = CompatibleAudioCodecsForContainer(effective);
        autoConvertVideoDropdown_.SetItems(videos);
        autoConvertAudioDropdown_.SetItems(audios);
        autoConvert.videoIndex =
            FindCodecIndex(videos, preferredAutoVideoCodec, DefaultVideoCodecForContainer(effective));
        autoConvert.audioIndex =
            FindCodecIndex(audios, preferredAutoAudioCodec, DefaultAudioCodecForContainer(effective));
    };
    const auto syncCustomAutoConvertCodecDropdowns = [&]()
    {
        const std::string effective = EffectiveConvertContainer(
            customConvert.convertContainer, customConvert.containerIndex, kAutoContainers, downloadFormatFallback);
        const std::vector<std::string> videos = CompatibleVideoCodecsForContainer(effective);
        const std::vector<std::string> audios = CompatibleAudioCodecsForContainer(effective);
        customAutoConvertVideoDropdown_.SetItems(videos);
        customAutoConvertAudioDropdown_.SetItems(audios);
        customConvert.videoIndex =
            FindCodecIndex(videos, preferredCustomVideoCodec, DefaultVideoCodecForContainer(effective));
        customConvert.audioIndex =
            FindCodecIndex(audios, preferredCustomAudioCodec, DefaultAudioCodecForContainer(effective));
    };
    syncAutoConvertCodecDropdowns();
    syncCustomAutoConvertCodecDropdowns();

    const bool dropdownOpen = autoConvertContainerDropdown_.IsOpen() || autoConvertVideoDropdown_.IsOpen() ||
                              autoConvertAudioDropdown_.IsOpen() || customAutoConvertContainerDropdown_.IsOpen() ||
                              customAutoConvertVideoDropdown_.IsOpen() || customAutoConvertAudioDropdown_.IsOpen();
    const float popupBottom = autoConvertPanel.y + autoConvertPanel.height + 400.0f;
    autoConvertContainerDropdown_.SetPopupLimitY(autoConvertPanel.y, popupBottom);
    autoConvertVideoDropdown_.SetPopupLimitY(autoConvertPanel.y, popupBottom);
    autoConvertAudioDropdown_.SetPopupLimitY(autoConvertPanel.y, popupBottom);
    customAutoConvertContainerDropdown_.SetPopupLimitY(autoConvertPanel.y, popupBottom);
    customAutoConvertVideoDropdown_.SetPopupLimitY(autoConvertPanel.y, popupBottom);
    customAutoConvertAudioDropdown_.SetPopupLimitY(autoConvertPanel.y, popupBottom);

    if (autoConvertSectionFoldout_.Update(layout.sectionHeaderBounds, !dropdownOpen) &&
        !autoConvertSectionFoldout_.IsExpanded())
    {
        autoConvertContainerDropdown_.Close();
        autoConvertVideoDropdown_.Close();
        autoConvertAudioDropdown_.Close();
        customAutoConvertContainerDropdown_.Close();
        customAutoConvertVideoDropdown_.Close();
        customAutoConvertAudioDropdown_.Close();
    }

    layout = GetAutoConvertDockLayout(autoConvertPanel,
                                      autoConvertSectionFoldout_.IsExpanded(),
                                      autoConvertFoldout_.IsExpanded(),
                                      customAutoConvertFoldout_.IsExpanded());
    autoConvertSectionFoldout_.SyncPanelBounds(layout.sectionFoldoutPanelBounds);
    autoConvertFoldout_.SyncPanelBounds(layout.autoFoldoutPanelBounds);
    customAutoConvertFoldout_.SyncPanelBounds(layout.customFoldoutPanelBounds);

    if (!autoConvertSectionFoldout_.IsExpanded())
    {
        autoConvertContainerDropdown_.Close();
        autoConvertVideoDropdown_.Close();
        autoConvertAudioDropdown_.Close();
        customAutoConvertContainerDropdown_.Close();
        customAutoConvertVideoDropdown_.Close();
        customAutoConvertAudioDropdown_.Close();
        PushUndo(MakeGlobalAutoConvertCommand(beforeGlobalEdit, globalAutoConvert_));
        return dropdownOpen;
    }

    std::vector<LinkCardNode*> selectedForCustom;
    std::string customSelectionKey;
    bool selectionExcluded = false;
    {
        bool hasSelection = false;
        bool allExcluded = true;
        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                if (!card.IsSelected() || !card.IsValid())
                {
                    return;
                }
                selectedForCustom.push_back(&card);
                customSelectionKey += card.Url();
                customSelectionKey += '\n';
                hasSelection = true;
                if (!card.IsExcludedFromAutoConvert())
                {
                    allExcluded = false;
                }
            });
        selectionExcluded = hasSelection && allExcluded;
    }

    if (customSelectionKey != customAutoConvertSelectionKey_)
    {
        customAutoConvertSelectionKey_ = customSelectionKey;
        if (!selectedForCustom.empty())
        {
            customAutoConvert_ = selectedForCustom.front()->CustomAutoConvert();
        }
        else
        {
            customAutoConvert_ = {};
        }
        customAutoConvertContainerDropdown_.Close();
        customAutoConvertVideoDropdown_.Close();
        customAutoConvertAudioDropdown_.Close();
    }

    const AutoConvertOptions customEditAtStart = customAutoConvert_;
    std::vector<LinkCustomAutoConvertSnapshot> beforeCustomSnapshots;
    beforeCustomSnapshots.reserve(selectedForCustom.size());
    for (LinkCardNode* card : selectedForCustom)
    {
        beforeCustomSnapshots.push_back({card->Url(), card->CustomAutoConvert()});
    }

    const bool prevAutoConvertEnabled = autoConvert.enabled;
    const bool prevCustomConvertEnabled = customConvert.enabled;
    const bool customUiAvailable = !selectedForCustom.empty() && !selectionExcluded;

    if (!dropdownOpen)
    {
        autoConvertEnabledCheckbox_.Update(autoConvertFoldout_.HeaderCheckboxBounds(layout.autoFoldoutPanelBounds),
                                           autoConvert.enabled);
        if (customUiAvailable)
        {
            customAutoConvertEnabledCheckbox_.Update(
                customAutoConvertFoldout_.HeaderCheckboxBounds(layout.customFoldoutPanelBounds), customConvert.enabled);
        }
    }
    if (autoConvert.enabled && !prevAutoConvertEnabled)
    {
        autoConvertFoldout_.SetExpanded(true);
        autoConvertSectionFoldout_.SetExpanded(true);
    }
    if (customUiAvailable && customConvert.enabled && !prevCustomConvertEnabled)
    {
        customAutoConvertFoldout_.SetExpanded(true);
        autoConvertSectionFoldout_.SetExpanded(true);
    }

    if (autoConvertFoldout_.Update(layout.autoHeaderBounds, !dropdownOpen) && !autoConvertFoldout_.IsExpanded())
    {
        autoConvertContainerDropdown_.Close();
        autoConvertVideoDropdown_.Close();
        autoConvertAudioDropdown_.Close();
    }
    if (customUiAvailable && customAutoConvertFoldout_.Update(layout.customHeaderBounds, !dropdownOpen) &&
        !customAutoConvertFoldout_.IsExpanded())
    {
        customAutoConvertContainerDropdown_.Close();
        customAutoConvertVideoDropdown_.Close();
        customAutoConvertAudioDropdown_.Close();
    }

    layout = GetAutoConvertDockLayout(autoConvertPanel,
                                      autoConvertSectionFoldout_.IsExpanded(),
                                      autoConvertFoldout_.IsExpanded(),
                                      customAutoConvertFoldout_.IsExpanded());
    const float autoDropdownX = layout.nestedDropdownX;
    const float autoDropdownW = layout.nestedDropdownW;
    const float checkX = layout.nestedCheckX;
    const Rectangle autoContainerBounds = {autoDropdownX, layout.autoContainerY, autoDropdownW, 25.0f};
    const Rectangle autoVideoBounds = {autoDropdownX, layout.autoVideoY, autoDropdownW, 25.0f};
    const Rectangle autoAudioBounds = {autoDropdownX, layout.autoAudioY, autoDropdownW, 25.0f};
    const Rectangle customContainerBounds = {autoDropdownX, layout.customContainerY, autoDropdownW, 25.0f};
    const Rectangle customVideoBounds = {autoDropdownX, layout.customVideoY, autoDropdownW, 25.0f};
    const Rectangle customAudioBounds = {autoDropdownX, layout.customAudioY, autoDropdownW, 25.0f};

    if (!autoConvertFoldout_.IsExpanded() || !autoConvert.enabled)
    {
        autoConvertContainerDropdown_.Close();
        autoConvertVideoDropdown_.Close();
        autoConvertAudioDropdown_.Close();
    }
    if (!customUiAvailable || !customAutoConvertFoldout_.IsExpanded() || !customConvert.enabled)
    {
        customAutoConvertContainerDropdown_.Close();
        customAutoConvertVideoDropdown_.Close();
        customAutoConvertAudioDropdown_.Close();
    }
    if (!autoConvert.convertContainer)
    {
        autoConvertContainerDropdown_.Close();
    }
    if (!autoConvert.convertVideo)
    {
        autoConvertVideoDropdown_.Close();
    }
    if (!autoConvert.convertAudio)
    {
        autoConvertAudioDropdown_.Close();
    }
    if (!customConvert.convertContainer)
    {
        customAutoConvertContainerDropdown_.Close();
    }
    if (!customConvert.convertVideo)
    {
        customAutoConvertVideoDropdown_.Close();
    }
    if (!customConvert.convertAudio)
    {
        customAutoConvertAudioDropdown_.Close();
    }

    const bool autoSection = autoConvertFoldout_.IsExpanded() && autoConvert.enabled;
    const bool customSection = customUiAvailable && customAutoConvertFoldout_.IsExpanded() && customConvert.enabled;
    // Index 0 = topmost (custom section is lower on screen).
    Dropdown::Slot dockStack[] = {
        {&customAutoConvertAudioDropdown_,
         customAudioBounds,
         &customConvert.audioIndex,
         customSection && customConvert.convertAudio,
         &autoConvertPanel},
        {&customAutoConvertVideoDropdown_,
         customVideoBounds,
         &customConvert.videoIndex,
         customSection && customConvert.convertVideo,
         &autoConvertPanel},
        {&customAutoConvertContainerDropdown_,
         customContainerBounds,
         &customConvert.containerIndex,
         customSection && customConvert.convertContainer,
         &autoConvertPanel},
        {&autoConvertAudioDropdown_,
         autoAudioBounds,
         &autoConvert.audioIndex,
         autoSection && autoConvert.convertAudio,
         &autoConvertPanel},
        {&autoConvertVideoDropdown_,
         autoVideoBounds,
         &autoConvert.videoIndex,
         autoSection && autoConvert.convertVideo,
         &autoConvertPanel},
        {&autoConvertContainerDropdown_,
         autoContainerBounds,
         &autoConvert.containerIndex,
         autoSection && autoConvert.convertContainer,
         &autoConvertPanel},
    };

    int consumedIdx = Dropdown::UpdateOpenPopups(dockStack, 6);
    if (consumedIdx < 0)
    {
        Dropdown::Slot closedStack[] = {
            dockStack[5],
            dockStack[4],
            dockStack[3],
            dockStack[2],
            dockStack[1],
            dockStack[0],
        };
        const int closedIdx = Dropdown::UpdateClosedControls(closedStack, 6);
        if (closedIdx >= 0)
        {
            static constexpr int kClosedToOpen[] = {5, 4, 3, 2, 1, 0};
            consumedIdx = kClosedToOpen[closedIdx];
        }
    }

    const bool customContainerConsumed = consumedIdx == 2;
    const bool autoContainerConsumed = consumedIdx == 5;

    if (autoSection && (!autoConvert.convertContainer || autoContainerConsumed))
    {
        syncAutoConvertCodecDropdowns();
    }
    if (customSection && (!customConvert.convertContainer || customContainerConsumed))
    {
        syncCustomAutoConvertCodecDropdowns();
    }

    const bool dropdownBlocksInput = Dropdown::AnyOpen(dockStack, 6) || consumedIdx >= 0;
    if (!dropdownBlocksInput && autoSection)
    {
        const bool prevConvertContainer = autoConvert.convertContainer;
        autoConvertContainerCheckbox_.Update({checkX, layout.autoContainerY + 3.0f, 110.0f, 18.0f},
                                             autoConvert.convertContainer,
                                             &autoConvertGlobalCodecPaint_);
        autoConvertVideoCheckbox_.Update(
            {checkX, layout.autoVideoY + 3.0f, 90.0f, 18.0f}, autoConvert.convertVideo, &autoConvertGlobalCodecPaint_);
        autoConvertAudioCheckbox_.Update(
            {checkX, layout.autoAudioY + 3.0f, 90.0f, 18.0f}, autoConvert.convertAudio, &autoConvertGlobalCodecPaint_);
        if (prevConvertContainer != autoConvert.convertContainer || autoContainerConsumed)
        {
            syncAutoConvertCodecDropdowns();
        }
    }
    if (!dropdownBlocksInput && customSection)
    {
        const bool prevConvertContainer = customConvert.convertContainer;
        customAutoConvertContainerCheckbox_.Update({checkX, layout.customContainerY + 3.0f, 110.0f, 18.0f},
                                                   customConvert.convertContainer,
                                                   &autoConvertCustomCodecPaint_);
        customAutoConvertVideoCheckbox_.Update({checkX, layout.customVideoY + 3.0f, 90.0f, 18.0f},
                                               customConvert.convertVideo,
                                               &autoConvertCustomCodecPaint_);
        customAutoConvertAudioCheckbox_.Update({checkX, layout.customAudioY + 3.0f, 90.0f, 18.0f},
                                               customConvert.convertAudio,
                                               &autoConvertCustomCodecPaint_);
        if (prevConvertContainer != customConvert.convertContainer || customContainerConsumed)
        {
            syncCustomAutoConvertCodecDropdowns();
        }
    }

    // Exclude selected: between foldouts, independent of auto/custom option undo.
    if (!dropdownBlocksInput)
    {
        std::vector<LinkCardNode*> selectedCards;
        bool anyBusy = false;
        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                if (!card.IsSelected() || !card.IsValid())
                {
                    return;
                }
                selectedCards.push_back(&card);
                if (card.IsDownloading() || card.IsConverting())
                {
                    anyBusy = true;
                }
            });

        const bool excludeEnabled =
            (autoConvert.enabled || customConvert.enabled) && !selectedCards.empty() && !anyBusy;
        bool excludeChecked = !selectedCards.empty();
        for (const LinkCardNode* card : selectedCards)
        {
            if (!card->IsExcludedFromAutoConvert())
            {
                excludeChecked = false;
                break;
            }
        }

        if (excludeEnabled)
        {
            const bool beforeChecked = excludeChecked;
            autoConvertExcludeCheckbox_.Update({checkX, layout.excludeY + 3.0f, 160.0f, 18.0f}, excludeChecked);
            if (excludeChecked != beforeChecked)
            {
                std::vector<LinkExcludeFlag> beforeFlags;
                beforeFlags.reserve(selectedCards.size());
                for (LinkCardNode* card : selectedCards)
                {
                    beforeFlags.push_back({card->Url(), card->IsExcludedFromAutoConvert()});
                    card->SetExcludedFromAutoConvert(excludeChecked);
                }
                PushUndo(MakeLinkExcludeAutoConvertCommand(std::move(beforeFlags), excludeChecked));
                if (excludeChecked)
                {
                    customAutoConvertContainerDropdown_.Close();
                    customAutoConvertVideoDropdown_.Close();
                    customAutoConvertAudioDropdown_.Close();
                }
            }
        }
    }

    PushUndo(MakeGlobalAutoConvertCommand(beforeGlobalEdit, globalAutoConvert_));
    if (customUiAvailable && customAutoConvert_ != customEditAtStart)
    {
        for (LinkCardNode* card : selectedForCustom)
        {
            card->SetCustomAutoConvert(customAutoConvert_);
        }
        PushUndo(MakeLinkCustomAutoConvertCommand(std::move(beforeCustomSnapshots), customAutoConvert_));
    }
    return dropdownBlocksInput;
}

void DockArea::UpdateRightPanel(Rectangle rightPanel, Font font, bool blockByUpperOverlay)
{
    LinkCardNode* selectedCard = GetSelectedCard();
    if (selectedCard != nullptr && !selectedCard->IsValid())
    {
        selectedCard = nullptr;
    }
    LinkCardGroupNode* selectedGroup = GetSelectedGroupHeader();
    bool editingGroup = false;
    std::vector<LinkCardNode*> groupCards;

    // Group header selection: edit shared download options for the playlist/channel.
    if (selectedCard == nullptr && selectedGroup != nullptr && selectedGroup->IsValid())
    {
        selectedGroup->LoadAllPages();
        for (LinkCardNode& child : selectedGroup->LoadedCards())
        {
            groupCards.push_back(&child);
        }
        editingGroup = true;
        if (!groupCards.empty())
        {
            selectedCard = groupCards.front();
        }
    }
    const Rectangle settingsPanel = GetRightSettingsPanel(rightPanel);
    const Rectangle globalPanel = GetGlobalPathPanel(rightPanel);
    const Rectangle globalPathBounds = {globalPanel.x + 10.0f, globalPanel.y + 34.0f, globalPanel.width - 20.0f, 26.0f};

    const std::string globalPathBefore = globalDownloadPath_;
    if (!blockByUpperOverlay)
    {
        UpdateGlobalPathLabelClick(font, globalPanel, "Global Download Path", globalDownloadPath_);
        globalPathField_.Update(globalPathBounds, font, globalDownloadPath_, true);
        PushUndo(MakeGlobalPathCommand(globalPathBefore, globalDownloadPath_));
    }

    if (!editingGroup && selectedCard == nullptr)
    {
        optionsScrollOffset_ = 0.0f;
        return;
    }

    if (blockByUpperOverlay)
    {
        return;
    }

    DownloadOptions& options = editingGroup ? selectedGroup->Options() : selectedCard->Options();

    std::vector<std::string> availableQualities;
    bool usingProvisionalQualityCaps = false;
    if (editingGroup)
    {
        // Always offer fixed caps so users can limit quality before child formats are parsed.
        availableQualities = BuildGroupQualityCapItems();
    }
    else
    {
        availableQualities = selectedCard->AvailableQualities();
        if (availableQualities.empty())
        {
            // Playlist children (and any card still in quiet detail-parse) have no ladder yet.
            // Show the same fixed caps immediately instead of a blank Quality dropdown.
            availableQualities = BuildGroupQualityCapItems();
            usingProvisionalQualityCaps = true;
            selectedCard->EnsureDetailedParse();
        }
    }
    qualityDropdown_.SetItems(availableQualities);
    if (availableQualities.empty())
    {
        options.quality = 0;
    }
    else if (options.quality < 0 || options.quality >= static_cast<int>(availableQualities.size()))
    {
        options.quality = 0;
    }

    std::vector<std::string> availableFormats;
    if (selectedCard != nullptr)
    {
        availableFormats =
            options.mediaMode == 2 ? selectedCard->AvailableAudioFormats() : selectedCard->AvailableVideoFormats();
    }
    if (availableFormats.empty())
    {
        availableFormats.push_back(options.mediaMode == 2 ? "M4A" : "MP4");
    }
    const std::string selectedQuality =
        availableQualities.empty() ? std::string{} : availableQualities[static_cast<size_t>(options.quality)];
    const std::string qualityForFormats = (selectedQuality == "Max") ? std::string{} : selectedQuality;
    availableFormats = BuildFormatItemsForQuality(availableFormats,
                                                  selectedCard != nullptr ? selectedCard->FormatStreams()
                                                                          : std::vector<LinkFormatStream>{},
                                                  qualityForFormats,
                                                  options.mediaMode);
    fileFormatDropdown_.SetItems(availableFormats);
    if (options.fileFormat < 0 || options.fileFormat >= static_cast<int>(availableFormats.size()) ||
        Dropdown::IsInactiveItem(availableFormats[options.fileFormat]))
    {
        options.fileFormat = FirstActiveFormatIndex(availableFormats);
    }

    const DownloadOptions optionsBeforeEdit = options;
    const std::string cardUrl = selectedCard != nullptr ? selectedCard->Url() : selectedGroup->Url();
    std::vector<DownloadOptions> groupOptionsBeforeEdit;
    if (editingGroup)
    {
        groupOptionsBeforeEdit.reserve(groupCards.size());
        for (LinkCardNode* card : groupCards)
        {
            groupOptionsBeforeEdit.push_back(card->Options());
        }
    }

    bool optionsLocked = false;
    if (editingGroup)
    {
        for (const LinkCardNode* card : groupCards)
        {
            if (card->IsDownloading() || card->IsConverting())
            {
                optionsLocked = true;
                break;
            }
        }
    }
    else
    {
        optionsLocked = selectedCard->IsDownloading() || selectedCard->IsConverting();
    }

    const Rectangle downloadButton = GetDownloadButtonBounds(settingsPanel);
    const Rectangle optionsViewport = GetOptionsScrollViewport(settingsPanel, downloadButton.y);
    const float contentHeight = GetDownloaderOptionsContentHeight(settingsPanel.y, downloadFoldout_.IsExpanded());
    const float maxOptionsScroll = std::max(0.0f, contentHeight - optionsViewport.height);

    const bool dropdownOpen = fileFormatDropdown_.IsOpen() || mediaModeDropdown_.IsOpen() || qualityDropdown_.IsOpen();
    const bool pathConsumesWheel = customPathField_.IsActive();

    if ((!dropdownOpen || optionsLocked) && !pathConsumesWheel &&
        CheckCollisionPointRec(GetMousePosition(), optionsViewport))
    {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            optionsScrollOffset_ = std::clamp(optionsScrollOffset_ - wheel * 40.0f, 0.0f, maxOptionsScroll);
        }
    }
    optionsScrollOffset_ = std::clamp(optionsScrollOffset_, 0.0f, maxOptionsScroll);

    if (optionsLocked)
    {
        fileFormatDropdown_.Close();
        mediaModeDropdown_.Close();
        qualityDropdown_.Close();
        if (customPathField_.IsActive())
        {
            const DownloaderPanelLayout layout = GetDownloaderPanelLayout(settingsPanel.x,
                                                                          settingsPanel.y,
                                                                          settingsPanel.width,
                                                                          downloadFoldout_.IsExpanded(),
                                                                          optionsScrollOffset_);
            customPathField_.Update({settingsPanel.x + 14.0f, layout.pathFieldY, settingsPanel.width - 28.0f, 25.0f},
                                    font,
                                    options.customPath,
                                    false);
        }
        return;
    }

    const DownloaderPanelLayout layout = GetDownloaderPanelLayout(
        settingsPanel.x, settingsPanel.y, settingsPanel.width, downloadFoldout_.IsExpanded(), optionsScrollOffset_);

    SyncFoldoutToClip(downloadFoldout_, layout.foldoutPanelBounds, optionsViewport);

    fileFormatDropdown_.SetPopupLimitY(optionsViewport.y, downloadButton.y - 6.0f);
    mediaModeDropdown_.SetPopupLimitY(optionsViewport.y, downloadButton.y - 6.0f);
    qualityDropdown_.SetPopupLimitY(optionsViewport.y, downloadButton.y - 6.0f);

    if (MouseHitsClippedControl(layout.downloadHeaderBounds, optionsViewport) &&
        downloadFoldout_.Update(layout.downloadHeaderBounds, !dropdownOpen) && !downloadFoldout_.IsExpanded())
    {
        fileFormatDropdown_.Close();
        mediaModeDropdown_.Close();
        qualityDropdown_.Close();
    }

    {
        const float contentHeightAfterToggle =
            GetDownloaderOptionsContentHeight(settingsPanel.y, downloadFoldout_.IsExpanded());
        const float maxAfterToggle = std::max(0.0f, contentHeightAfterToggle - optionsViewport.height);
        optionsScrollOffset_ = std::clamp(optionsScrollOffset_, 0.0f, maxAfterToggle);
    }

    const DownloaderPanelLayout controlsLayout = GetDownloaderPanelLayout(
        settingsPanel.x, settingsPanel.y, settingsPanel.width, downloadFoldout_.IsExpanded(), optionsScrollOffset_);
    const Rectangle formatBounds = {
        settingsPanel.x + 94.0f, controlsLayout.formatDropdownY, settingsPanel.width - 108.0f, 25.0f};
    const Rectangle mediaBounds = {
        settingsPanel.x + 94.0f, controlsLayout.mediaDropdownY, settingsPanel.width - 108.0f, 25.0f};
    const Rectangle qualityBounds = {
        settingsPanel.x + 94.0f, controlsLayout.qualityDropdownY, settingsPanel.width - 108.0f, 25.0f};
    const Rectangle pathFieldBounds = {
        settingsPanel.x + 14.0f, controlsLayout.pathFieldY, settingsPanel.width - 28.0f, 25.0f};

    bool formatConsumed = false;
    bool mediaConsumed = false;
    bool qualityConsumed = false;
    const int mediaModeBefore = options.mediaMode;
    const int qualityBefore = options.quality;

    if (!downloadFoldout_.IsExpanded())
    {
        fileFormatDropdown_.Close();
        mediaModeDropdown_.Close();
        qualityDropdown_.Close();
    }

    const bool downloadSection = downloadFoldout_.IsExpanded();
    Dropdown::Slot optionsStack[] = {
        {&mediaModeDropdown_, mediaBounds, &options.mediaMode, downloadSection, &optionsViewport},
        {&fileFormatDropdown_, formatBounds, &options.fileFormat, downloadSection, &optionsViewport},
        {&qualityDropdown_,
         qualityBounds,
         &options.quality,
         downloadSection && options.mediaMode != 2,
         &optionsViewport},
    };

    int consumedIdx = Dropdown::UpdateOpenPopups(optionsStack, 3);
    if (consumedIdx < 0)
    {
        Dropdown::Slot closedStack[] = {
            optionsStack[2],
            optionsStack[1],
            optionsStack[0],
        };
        const int closedIdx = Dropdown::UpdateClosedControls(closedStack, 3);
        if (closedIdx >= 0)
        {
            static constexpr int kClosedToOpen[] = {2, 1, 0};
            consumedIdx = kClosedToOpen[closedIdx];
        }
    }

    mediaConsumed = consumedIdx == 0;
    formatConsumed = consumedIdx == 1;
    qualityConsumed = consumedIdx == 2;

    if (options.mediaMode != mediaModeBefore || options.quality != qualityBefore)
    {
        availableFormats.clear();
        if (selectedCard != nullptr)
        {
            availableFormats =
                options.mediaMode == 2 ? selectedCard->AvailableAudioFormats() : selectedCard->AvailableVideoFormats();
        }
        if (availableFormats.empty())
        {
            availableFormats.push_back(options.mediaMode == 2 ? "M4A" : "MP4");
        }
        const std::string qualityLabel =
            availableQualities.empty() ? std::string{} : availableQualities[static_cast<size_t>(options.quality)];
        const std::string qualityForFormats = (qualityLabel == "Max") ? std::string{} : qualityLabel;
        availableFormats = BuildFormatItemsForQuality(availableFormats,
                                                      selectedCard != nullptr ? selectedCard->FormatStreams()
                                                                              : std::vector<LinkFormatStream>{},
                                                      qualityForFormats,
                                                      options.mediaMode);
        fileFormatDropdown_.SetItems(availableFormats);
        if (options.mediaMode != mediaModeBefore || options.fileFormat < 0 ||
            options.fileFormat >= static_cast<int>(availableFormats.size()) ||
            Dropdown::IsInactiveItem(availableFormats[options.fileFormat]))
        {
            options.fileFormat = FirstActiveFormatIndex(availableFormats);
        }
    }

    const bool dropdownBlocksInput =
        Dropdown::AnyOpen(optionsStack, 3) || formatConsumed || mediaConsumed || qualityConsumed;
    if (dropdownBlocksInput)
    {
        overlayBlocksActions_ = true;
    }
    if (!dropdownBlocksInput)
    {
        const Rectangle pathCheckboxBounds = {settingsPanel.x + 94.0f, controlsLayout.pathRowY, 130.0f, 18.0f};
        if (MouseHitsClippedControl(pathCheckboxBounds, optionsViewport))
        {
            customPathCheckbox_.Update(pathCheckboxBounds, options.useCustomPath);
        }
        if (customPathField_.IsActive() || MouseHitsClippedControl(pathFieldBounds, optionsViewport))
        {
            customPathField_.Update(pathFieldBounds, font, options.customPath, options.useCustomPath);
        }
        const Rectangle keepIndicesBounds = {settingsPanel.x + 14.0f, controlsLayout.keepIndicesRowY, 160.0f, 18.0f};
        if (MouseHitsClippedControl(keepIndicesBounds, optionsViewport))
        {
            keepIndicesCheckbox_.Update(keepIndicesBounds, keepDownloadIndices_);
        }
    }
    else if (customPathField_.IsActive())
    {
        customPathField_.Update(pathFieldBounds, font, options.customPath, false);
    }

    if (!editingGroup)
    {
        if (optionsBeforeEdit != options && !availableQualities.empty())
        {
            // Keep qualityCap in sync with the dropdown so downloads honor per-video overrides.
            const std::string label = availableQualities[static_cast<size_t>(
                std::clamp(options.quality, 0, static_cast<int>(availableQualities.size()) - 1))];
            options.qualityCap = (label == "Max") ? std::string{} : label;
        }
        PushUndo(MakeCardOptionsCommand(cardUrl, optionsBeforeEdit, options));
        return;
    }

    // Playlist/channel edits force the same settings onto every child video.
    // Per-video overrides are allowed afterward until the group is edited again.
    if (optionsBeforeEdit == options)
    {
        return;
    }

    options.qualityCap = [&]()
    {
        const std::string cap = GroupQualityCapFromOptions(options);
        return (cap == "Max") ? std::string{} : cap;
    }();

    const std::string groupQualityCap = GroupQualityCapFromOptions(options);
    for (size_t i = 0; i < groupCards.size(); ++i)
    {
        LinkCardNode* card = groupCards[i];
        const DownloadOptions before = groupOptionsBeforeEdit[i];
        DownloadOptions mapped = options;
        mapped.qualityCap = (groupQualityCap == "Max") ? std::string{} : groupQualityCap;
        mapped.quality = MapQualityCapToCardIndex(card->AvailableQualities(), groupQualityCap);
        if (before == mapped)
        {
            continue;
        }

        card->Options() = mapped;
        PushUndo(MakeCardOptionsCommand(card->Url(), before, mapped));
    }
}

void DockArea::PrepareGroupsForBatchDownload()
{
    for (DownloaderListItem& item : cards_)
    {
        if (item.kind != DownloaderListItem::Kind::Group || item.group == nullptr)
        {
            continue;
        }
        item.group->LoadAllPages();
    }
}

void DockArea::HandleDownloadRequest()
{
    std::vector<LinkCardNode*> selectedIdle;
    ForEachLinkCard(
        [&](LinkCardNode& card)
        {
            if (!card.IsSelected() || !card.IsValid() || card.IsDownloading() || card.IsInQueue() ||
                card.IsConverting())
            {
                return;
            }
            card.EnsureDetailedParse();
            selectedIdle.push_back(&card);
        });

    for (DownloaderListItem& item : cards_)
    {
        if (item.kind != DownloaderListItem::Kind::Group || item.group == nullptr || !item.group->IsHeaderSelected())
        {
            continue;
        }
        item.group->SetExpanded(true);
        item.group->LoadAllPages();

        for (LinkCardNode& child : item.group->LoadedCards())
        {
            if (!child.IsValid() || child.IsDownloading() || child.IsInQueue() || child.IsConverting())
            {
                continue;
            }
            child.EnsureDetailedParse();
            selectedIdle.push_back(&child);
        }
    }

    if (selectedIdle.empty())
    {
        return;
    }

    const bool appendToActiveBatch = AnyDownloadRunning() || isBatchDownloading_;
    if (!appendToActiveBatch)
    {
        pendingDownloadQueue_.clear();
        ClearFooterNotification();
        overwriteAllExisting_ = false;
        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                card.ClearQueueState();
            });
    }

    bool addedAny = false;
    for (LinkCardNode* card : selectedIdle)
    {
        DownloadRequest request;
        if (!BuildDownloadRequestForCard(*card, request))
        {
            continue;
        }
        pendingDownloadQueue_.push_back(std::move(request));
        card->SetQueued();
        addedAny = true;
    }

    if (!addedAny)
    {
        return;
    }

    if (!appendToActiveBatch)
    {
        isBatchDownloading_ = true;
        nextDownloadStartTime_ = GetTime();
        StartNextPendingDownload();
    }
}

void DockArea::HandleDownloadAllRequest()
{
    if (AnyDownloadRunning() && !HasDownloadableIdleCards())
    {
        return;
    }

    const bool appendToActiveBatch = AnyDownloadRunning();
    if (!appendToActiveBatch)
    {
        pendingDownloadQueue_.clear();
        ClearFooterNotification();
        overwriteAllExisting_ = false;
        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                card.ClearQueueState();
            });
    }

    bool addedAny = false;
    WriteDebugLog(appendToActiveBatch ? "Download All append clicked" : "Download All clicked");
    PrepareGroupsForBatchDownload();

    ForEachLinkCard(
        [&](LinkCardNode& card)
        {
            if (!card.IsValid() || card.IsDownloading() || card.IsInQueue() || card.IsConverting())
            {
                return;
            }

            card.EnsureDetailedParse();
            DownloadRequest request;
            if (BuildDownloadRequestForCard(card, request))
            {
                pendingDownloadQueue_.push_back(std::move(request));
                card.SetQueued();
                addedAny = true;
            }
        });

    if (!addedAny)
    {
        if (!AnyDownloadRunning())
        {
            ShowFooterNotification("No videos to download.", FooterNotificationScope::Downloader);
        }
        return;
    }

    if (!appendToActiveBatch)
    {
        isBatchDownloading_ = true;
        nextDownloadStartTime_ = GetTime();
        WriteDebugLog("batch start count=" + std::to_string(pendingDownloadQueue_.size()));
        StartNextPendingDownload();
    }
    else
    {
        isBatchDownloading_ = true;
        WriteDebugLog("batch append count=" + std::to_string(pendingDownloadQueue_.size()));
        StartNextPendingDownload();
    }

    PushUndo(MakeDownloadAllCommand());
}

bool DockArea::HasDownloadableIdleCards() const
{
    bool found = false;

    // Groups may not have materialized all entries yet (collapsed state / lazy pages),
    // so check group entry availability separately from the loaded children iteration.
    for (const DownloaderListItem& item : cards_)
    {
        if (item.kind != DownloaderListItem::Kind::Group || item.group == nullptr || !item.group->IsValid())
        {
            continue;
        }

        if (item.group->RemainingEntryCount() > 0)
        {
            return true;
        }
    }

    ForEachLinkCard(
        [&](const LinkCardNode& card)
        {
            if (card.IsValid() && !card.IsDownloading() && !card.IsInQueue() && !card.IsConverting())
            {
                found = true;
            }
        });
    return found;
}

bool DockArea::HasValidDownloadCards() const
{
    bool found = false;
    ForEachLinkCard(
        [&](const LinkCardNode& card)
        {
            if (card.IsValid())
            {
                found = true;
            }
        });
    return found;
}

bool DockArea::CanDownloadSelected() const
{
    bool found = false;
    ForEachLinkCard(
        [&](const LinkCardNode& card)
        {
            if (card.IsSelected() && card.IsValid() && !card.IsDownloading() && !card.IsInQueue() &&
                !card.IsConverting())
            {
                found = true;
            }
        });
    if (found)
    {
        return true;
    }
    for (const DownloaderListItem& item : cards_)
    {
        if (item.kind != DownloaderListItem::Kind::Group || item.group == nullptr || !item.group->IsHeaderSelected() ||
            !item.group->IsValid())
        {
            continue;
        }
        for (const LinkCardNode& child : item.group->LoadedCards())
        {
            if (child.IsValid() && !child.IsDownloading() && !child.IsInQueue() && !child.IsConverting())
            {
                return true;
            }
        }
        if (item.group->EntryCount() > item.group->LoadedChildCount())
        {
            return true;
        }
    }
    return false;
}

bool DockArea::SelectedCardShowsCancel() const
{
    bool showsCancel = false;
    ForEachLinkCard(
        [&](const LinkCardNode& card)
        {
            if (!card.IsSelected() || !card.IsValid())
            {
                return;
            }
            if (card.IsDownloading() || card.IsInQueue() || card.IsConverting())
            {
                showsCancel = true;
            }
        });
    return showsCancel;
}

void DockArea::HandleCancelSelectedRequest()
{
    bool cancelledAny = false;
    ForEachLinkCard(
        [&](LinkCardNode& card)
        {
            if (!card.IsSelected() || !card.IsValid())
            {
                return;
            }

            if (card.IsConverting())
            {
                CancelLinkCardConvert(card.LastDownloadedPath());
                cancelledAny = true;
                return;
            }

            if (card.IsInQueue())
            {
                RemoveFromDownloadQueue(card.Url());
                cancelledAny = true;
                return;
            }

            if (!card.IsDownloading())
            {
                return;
            }

            const std::string url = card.Url();
            card.ClearDownloading();
            card.ClearAutoConvertSnapshot();
            card.ClearAutoConvertDelivery();
            if (DownloadRunner* runner = FindDownloadRunnerByUrl(url))
            {
                runner->Cancel();
            }
            cancelledAny = true;
        });

    if (cancelledAny && !isBatchDownloading_ && !AnyDownloadRunning() && pendingDownloadQueue_.empty())
    {
        pendingDownloadQueue_.clear();
    }
}

void DockArea::HandleCancelAllDownloadsRequest()
{
    const bool hadWork = AnyDownloadRunning() || !pendingDownloadQueue_.empty() || isBatchDownloading_;
    pendingDownloadQueue_.clear();
    isBatchDownloading_ = false;
    overwriteAllExisting_ = false;

    ForEachLinkCard(
        [&](LinkCardNode& card)
        {
            if (card.IsInQueue())
            {
                card.ClearQueueState();
            }
            if (card.IsDownloading())
            {
                card.ClearDownloading();
                card.ClearAutoConvertSnapshot();
                card.ClearAutoConvertDelivery();
            }
        });

    CancelAllDownloads();

    if (hadWork)
    {
        ShowFooterNotification("All downloads cancelled.", FooterNotificationScope::Downloader);
        PushUndo(MakeCancelAllDownloadsCommand());
    }
}

void DockArea::RemoveFromDownloadQueue(const std::string& url)
{
    pendingDownloadQueue_.erase(std::remove_if(pendingDownloadQueue_.begin(),
                                               pendingDownloadQueue_.end(),
                                               [&](const DownloadRequest& request)
                                               {
                                                   return request.url == url;
                                               }),
                                pendingDownloadQueue_.end());

    ForEachLinkCard(
        [&](LinkCardNode& card)
        {
            if (!card.HasUrl(url) || !card.IsInQueue())
            {
                return;
            }

            if (isBatchDownloading_ && (AnyDownloadRunning() || !pendingDownloadQueue_.empty()))
            {
                card.SetNotInQueue();
            }
            else
            {
                card.ClearQueueState();
            }
        });

    StartNextPendingDownload();
}

void DockArea::PrioritizeDownload(const std::string& url)
{
    LinkCardNode* card = FindLinkCardByUrl(url);
    if (card == nullptr || !card->IsInQueue())
    {
        return;
    }

    DownloadRequest prioritized;
    bool foundInQueue = false;
    for (auto it = pendingDownloadQueue_.begin(); it != pendingDownloadQueue_.end(); ++it)
    {
        if (it->url == url)
        {
            prioritized = std::move(*it);
            pendingDownloadQueue_.erase(it);
            foundInQueue = true;
            break;
        }
    }
    if (!foundInQueue)
    {
        if (!BuildDownloadRequestForCard(*card, prioritized))
        {
            return;
        }
    }

    pendingDownloadQueue_.insert(pendingDownloadQueue_.begin(), std::move(prioritized));
    card->SetQueued();
    isBatchDownloading_ = true;

    if (FirstFreeDownloadRunner() != nullptr)
    {
        StartNextPendingDownload();
        ShowFooterNotification("Started " + card->Title(), FooterNotificationScope::Downloader);
        return;
    }

    DownloadRunner* victim = FindLowestProgressDownloadRunner();
    if (victim == nullptr)
    {
        ShowFooterNotification("Queued first вЂ” waiting for a free slot", FooterNotificationScope::Downloader);
        return;
    }

    const std::string victimUrl = victim->CurrentUrl();
    LinkCardNode* victimCard = FindLinkCardByUrl(victimUrl);
    if (victimCard == nullptr)
    {
        ShowFooterNotification("Queued first вЂ” waiting for a free slot", FooterNotificationScope::Downloader);
        return;
    }

    DownloadRequest victimRequest;
    if (!BuildDownloadRequestForCard(*victimCard, victimRequest))
    {
        ShowFooterNotification("Queued first вЂ” waiting for a free slot", FooterNotificationScope::Downloader);
        return;
    }

    // Keep order: [priority, preempted, ...rest]
    pendingDownloadQueue_.insert(pendingDownloadQueue_.begin() + 1, std::move(victimRequest));
    softPreemptRequeueUrls_.insert(victimUrl);
    victimCard->DemoteDownloadingToQueued();
    victimCard->ClearAutoConvertSnapshot();
    victimCard->ClearAutoConvertDelivery();
    victim->Cancel();

    const std::string priorityTitle = card->Title();
    const std::string victimTitle = victimCard->Title();
    ShowFooterNotification("Started " + priorityTitle + " вЂ” paused " + victimTitle,
                           FooterNotificationScope::Downloader);
}

DownloadRunner* DockArea::FindLowestProgressDownloadRunner()
{
    DownloadRunner* bestDownloading = nullptr;
    float bestDownloadingProgress = 2.0f;

    for (DownloadRunner& runner : downloadRunners_)
    {
        if (!runner.IsRunning() || runner.CurrentUrl().empty())
        {
            continue;
        }
        if (softPreemptRequeueUrls_.count(runner.CurrentUrl()) > 0)
        {
            continue;
        }
        if (runner.Phase() == DownloadSharedState::Phase::Merging)
        {
            continue;
        }

        const float progress = runner.Progress();
        if (progress < bestDownloadingProgress)
        {
            bestDownloadingProgress = progress;
            bestDownloading = &runner;
        }
    }

    return bestDownloading;
}

void DockArea::ClearBatchQueueStates()
{
    ForEachLinkCard(
        [&](LinkCardNode& card)
        {
            card.ClearQueueState();
        });
}

bool DockArea::BuildDownloadRequestForCard(LinkCardNode& card, DownloadRequest& request)
{
    const DownloadOptions& options = card.Options();
    std::vector<std::string> availableQualities = card.AvailableQualities();
    const int qualityIndex = availableQualities.empty()
                                 ? 0
                                 : std::clamp(options.quality, 0, static_cast<int>(availableQualities.size()) - 1);

    // Resolve an explicit height cap: card.qualityCap, else parent playlist/channel cap.
    std::string qualityCap = options.qualityCap;
    if (qualityCap.empty() || qualityCap == "Max")
    {
        for (const DownloaderListItem& item : cards_)
        {
            if (item.kind != DownloaderListItem::Kind::Group || item.group == nullptr)
            {
                continue;
            }
            bool belongs = false;
            for (const LinkCardNode& child : item.group->LoadedCards())
            {
                if (&child == &card)
                {
                    belongs = true;
                    break;
                }
            }
            if (!belongs)
            {
                continue;
            }
            qualityCap = GroupQualityCapFromOptions(item.group->Options());
            break;
        }
    }
    if (qualityCap == "Max")
    {
        qualityCap.clear();
    }

    std::string requestedQuality =
        availableQualities.empty() ? std::string{} : availableQualities[static_cast<size_t>(qualityIndex)];
    if (!qualityCap.empty())
    {
        // Group/user cap always wins over a stale per-card quality index.
        requestedQuality = qualityCap;
    }

    std::string selectedQuality = (requestedQuality == "Max") ? std::string{} : requestedQuality;
    if (!availableQualities.empty() && !selectedQuality.empty())
    {
        const int requestedHeight = ParseQualityHeight(selectedQuality);
        int bestIndex = -1;
        for (int i = 0; i < static_cast<int>(availableQualities.size()); ++i)
        {
            const int height = ParseQualityHeight(availableQualities[static_cast<size_t>(i)]);
            // Ladder is ordered descending, so the first match is the best "requestedHeight or lower".
            if (height > 0 && height <= requestedHeight)
            {
                bestIndex = i;
                break;
            }
        }
        if (bestIndex >= 0)
        {
            selectedQuality = availableQualities[static_cast<size_t>(bestIndex)];
        }
        else if (requestedHeight > 0)
        {
            // Keep the numeric cap so yt-dlp still filters with height<=cap.
            selectedQuality = qualityCap.empty() ? requestedQuality : qualityCap;
        }
        else
        {
            selectedQuality = availableQualities.back();
        }
    }
    else if (selectedQuality.empty() && !qualityCap.empty())
    {
        selectedQuality = qualityCap;
    }

    std::vector<std::string> availableFormats =
        options.mediaMode == 2 ? card.AvailableAudioFormats() : card.AvailableVideoFormats();
    if (availableFormats.empty())
    {
        availableFormats.push_back(options.mediaMode == 2 ? "M4A" : "MP4");
    }
    availableFormats =
        BuildFormatItemsForQuality(availableFormats, card.FormatStreams(), selectedQuality, options.mediaMode);

    const std::vector<std::string> mediaModes = {"Both", "Video only", "Audio only"};
    int formatIndex = std::clamp(options.fileFormat, 0, static_cast<int>(availableFormats.size()) - 1);
    if (availableFormats.empty() || Dropdown::IsInactiveItem(availableFormats[formatIndex]))
    {
        formatIndex = FirstActiveFormatIndex(availableFormats);
    }
    const int mediaIndex = std::clamp(options.mediaMode, 0, static_cast<int>(mediaModes.size()) - 1);

    request.url = card.Url();
    request.title = card.Title();
    request.normalizedTitle = card.NormalizedTitle();
    if (request.normalizedTitle.empty())
    {
        request.normalizedTitle = NormalizeVideoTitle(request.title);
    }
    if (keepDownloadIndices_)
    {
        int displayIndex = 0;
        bool found = false;
        for (const DownloaderListItem& item : cards_)
        {
            if (item.kind == DownloaderListItem::Kind::Single)
            {
                ++displayIndex;
                if (item.single.get() == &card)
                {
                    found = true;
                    break;
                }
                continue;
            }

            if (item.group == nullptr)
            {
                continue;
            }

            // Playlist/channel children use a local 1-based index inside their group.
            int localIndex = 0;
            for (const LinkCardNode& child : item.group->LoadedCards())
            {
                ++localIndex;
                if (&child == &card)
                {
                    displayIndex = localIndex;
                    found = true;
                    break;
                }
            }
            if (found)
            {
                break;
            }
        }
        if (found && displayIndex > 0)
        {
            request.normalizedTitle = std::to_string(displayIndex) + ". " + request.normalizedTitle;
        }
    }
    const std::string userOutputDirectory =
        options.useCustomPath && !options.customPath.empty() ? options.customPath : globalDownloadPath_;
    request.originalNormalizedTitle = request.normalizedTitle;
    request.finalOutputDirectory.clear();
    request.autoConvertActive = false;
    const AutoConvertOptions resolvedAutoConvert = ResolveAutoConvertOptionsForCard(card);
    if (resolvedAutoConvert.IsActive())
    {
        // Stage download under Documents/4KDownerTemp with "_downloaded" suffix; final delivery is user path.
        request.outputDirectory = GetAutoConvertStagingPath();
        request.normalizedTitle += "_downloaded";
        request.finalOutputDirectory = userOutputDirectory;
        request.autoConvertActive = true;
    }
    else
    {
        request.outputDirectory = userOutputDirectory;
    }
    request.fileFormat = availableFormats.empty() ? (options.mediaMode == 2 ? "M4A" : "MP4")
                                                  : StripFormatItemLabel(availableFormats[formatIndex]);
    request.mediaMode = mediaModes[mediaIndex];
    request.quality = selectedQuality;
    return true;
}

bool DockArea::PrepareDownloadRequest(DownloadRequest& request)
{
    try
    {
        const std::string& userPathToValidate =
            request.autoConvertActive ? request.finalOutputDirectory : request.outputDirectory;
        if (!IsValidUserOutputPath(userPathToValidate))
        {
            pendingDownloadQueue_.clear();
            isBatchDownloading_ = false;
            overwriteAllExisting_ = false;
            ClearBatchQueueStates();
            const std::string status = "Invalid download path";
            ShowFooterNotification(status, FooterNotificationScope::Downloader, status);
            return false;
        }

        std::filesystem::path outputPath = std::filesystem::u8path(request.outputDirectory);
        if (!outputPath.is_absolute())
        {
            pendingDownloadQueue_.clear();
            isBatchDownloading_ = false;
            overwriteAllExisting_ = false;
            ClearBatchQueueStates();
            const std::string status = "Invalid download path";
            ShowFooterNotification(status, FooterNotificationScope::Downloader, status);
            return false;
        }

        std::error_code error;
        std::filesystem::create_directories(outputPath, error);
        if (error)
        {
            const std::string status = "Download failed: could not create output path.";
            ShowFooterNotification(status, FooterNotificationScope::Downloader, status);
            if (!isBatchDownloading_)
            {
                pendingDownloadQueue_.clear();
            }
            return false;
        }
        request.outputDirectory = PathUtf8(outputPath);

        if (request.autoConvertActive)
        {
            // Documents staging is disposable. Never prompt about leftovers there вЂ” wipe and continue.
            WipeStagingFilesByStem(outputPath, request.normalizedTitle);
            request.overwriteExisting = true;

            if (!request.finalOutputDirectory.empty())
            {
                std::filesystem::path finalPath = std::filesystem::u8path(request.finalOutputDirectory);
                if (!finalPath.is_absolute())
                {
                    pendingDownloadQueue_.clear();
                    isBatchDownloading_ = false;
                    overwriteAllExisting_ = false;
                    ClearBatchQueueStates();
                    const std::string status = "Invalid download path";
                    ShowFooterNotification(status, FooterNotificationScope::Downloader, status);
                    return false;
                }

                LinkCardNode* card = FindLinkCardByUrl(request.url);
                const AutoConvertOptions convertOptions =
                    card != nullptr ? (card->HasAutoConvertSnapshot() ? card->AutoConvertSnapshot()
                                                                      : ResolveAutoConvertOptionsForCard(*card))
                                    : globalAutoConvert_;
                const std::string finalExt = PredictAutoConvertExtension(convertOptions, request.fileFormat);
                std::string finalTitle = request.originalNormalizedTitle;
                if (finalTitle.empty())
                {
                    finalTitle = request.normalizedTitle;
                    constexpr char kSuffix[] = "_downloaded";
                    constexpr size_t kSuffixLen = sizeof(kSuffix) - 1;
                    if (finalTitle.size() > kSuffixLen &&
                        finalTitle.compare(finalTitle.size() - kSuffixLen, kSuffixLen, kSuffix) == 0)
                    {
                        finalTitle.erase(finalTitle.size() - kSuffixLen);
                    }
                }
                // Only the predicted auto-convert output counts (e.g. .mkv).
                // A leftover .mp4 from a previous non-converted download is not a conflict.
                const std::filesystem::path existingFinalFile = FindExistingOutputFile(finalPath, finalTitle, finalExt);
                if (!existingFinalFile.empty())
                {
                    if (overwriteAllExisting_)
                    {
                        request.overwriteExisting = true;
                    }
                    else
                    {
                        pendingOverwriteRequest_ = request;
                        pendingOverwriteFileName_ = PathUtf8(existingFinalFile.filename());
                        overwritePromptIsConvert_ = false;
                        isOverwritePromptOpen_ = true;
                        overwritePromptFocusIndex_ = 0;
                        return false;
                    }
                }
            }
        }
        else
        {
            // Normal download: only the user destination matters.
            const std::filesystem::path existingFile =
                FindExistingOutputFile(outputPath, request.normalizedTitle, request.fileFormat);
            if (!existingFile.empty())
            {
                if (overwriteAllExisting_)
                {
                    request.overwriteExisting = true;
                }
                else
                {
                    pendingOverwriteRequest_ = request;
                    pendingOverwriteFileName_ = PathUtf8(existingFile.filename());
                    overwritePromptIsConvert_ = false;
                    isOverwritePromptOpen_ = true;
                    overwritePromptFocusIndex_ = 0;
                    return false;
                }
            }
        }

        return true;
    }
    catch (const std::exception& exception)
    {
        const std::string status = std::string("Download failed: ") + exception.what();
        ShowFooterNotification(status, FooterNotificationScope::Downloader, status);
        if (!isBatchDownloading_)
        {
            pendingDownloadQueue_.clear();
        }
        return false;
    }
    catch (...)
    {
        const std::string status = "Download failed: unexpected prepare error.";
        ShowFooterNotification(status, FooterNotificationScope::Downloader, status);
        if (!isBatchDownloading_)
        {
            pendingDownloadQueue_.clear();
        }
        return false;
    }
}

bool DockArea::StartNextPendingDownload()
{
    bool startedAny = false;
    while (FirstFreeDownloadRunner() != nullptr && !pendingDownloadQueue_.empty() && !isOverwritePromptOpen_)
    {
        DownloadRequest queued = std::move(pendingDownloadQueue_.front());
        pendingDownloadQueue_.erase(pendingDownloadQueue_.begin());
        const bool overwriteExisting = queued.overwriteExisting;

        DownloadRequest request;
        LinkCardNode* card = FindLinkCardByUrl(queued.url);
        if (card == nullptr || !BuildDownloadRequestForCard(*card, request))
        {
            WriteDebugLog("prepare failed, skipping: " + queued.url);
            continue;
        }
        request.overwriteExisting = overwriteExisting;

        if (!PrepareDownloadRequest(request))
        {
            if (isOverwritePromptOpen_)
            {
                WriteDebugLog("prepare paused for overwrite prompt");
                return startedAny;
            }

            WriteDebugLog("prepare failed, skipping: " + request.url);
            continue;
        }

        WriteDebugLog("start download: " + request.url);
        StartDownload(std::move(request));
        startedAny = true;
    }

    if (isBatchDownloading_ && !AnyDownloadRunning() && pendingDownloadQueue_.empty() && !isOverwritePromptOpen_)
    {
        const double totalElapsed = this->SumCompletedCardDownloadElapsed();
        isBatchDownloading_ = false;
        overwriteAllExisting_ = false;
        ClearBatchQueueStates();
        ShowFooterNotification(FormatDownloadFinishedStatus(totalElapsed, true),
                               FooterNotificationScope::Downloader,
                               "",
                               footerClipboardLog_);
    }

    return startedAny;
}

void DockArea::StartDownload(DownloadRequest request)
{
    DownloadRunner* runner = FirstFreeDownloadRunner();
    if (runner == nullptr)
    {
        pendingDownloadQueue_.insert(pendingDownloadQueue_.begin(), request);
        return;
    }

    ClearFooterNotification();

    const std::string url = request.url;
    const std::string outputDirectory = request.outputDirectory;
    const std::string fileFormat = request.fileFormat;
    const std::string normalizedTitle = request.normalizedTitle;
    ForEachLinkCard(
        [&](LinkCardNode& card)
        {
            if (card.HasUrl(url))
            {
                card.SetExpectedDownloadOutput(outputDirectory, fileFormat, normalizedTitle);
                if (request.autoConvertActive)
                {
                    card.SetAutoConvertSnapshot(ResolveAutoConvertOptionsForCard(card));
                    card.SetAutoConvertDelivery(request.finalOutputDirectory, request.originalNormalizedTitle);
                }
                else
                {
                    card.ClearAutoConvertSnapshot();
                    card.ClearAutoConvertDelivery();
                }
                card.SetDownloading();
                return;
            }
        });

    try
    {
        runner->Start(std::move(request));
    }
    catch (...)
    {
        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                if (card.HasUrl(url))
                {
                    card.ClearDownloading();
                    card.ClearAutoConvertSnapshot();
                    card.ClearAutoConvertDelivery();
                }
            });
        ShowFooterNotification("Download failed: could not start download.",
                               FooterNotificationScope::Downloader,
                               "Download failed: could not start download.");
        if (isBatchDownloading_ && !pendingDownloadQueue_.empty())
        {
            nextDownloadStartTime_ = GetTime();
        }
    }
}

void DockArea::PulseConverterFooterHint()
{
    ShowFooterNotification("Select an Option", FooterNotificationScope::Converter);
}

void DockArea::HandleConvertRequest()
{
    ClearFooterNotification();

    if (!AnyConvertRunning() && pendingConvertQueue_.empty())
    {
        pendingConvertQueue_.clear();
        overwriteAllExisting_ = false;
        isBatchConverting_ = false;
        batchConvertElapsedTotal_ = 0.0;
        batchIncludesDownloadConvert_ = false;
    }

    std::vector<ConvertRequest> queued;
    for (const ConverterFileCardNode& card : converterCards_)
    {
        if (!card.IsSelected())
        {
            continue;
        }

        ConvertRequest request;
        if (BuildConvertRequestForCard(card, request))
        {
            queued.push_back(std::move(request));
        }
    }

    if (queued.empty())
    {
        PulseConverterFooterHint();
        return;
    }

    if (queued.size() > 1)
    {
        isBatchConverting_ = true;
        batchConvertElapsedTotal_ = 0.0;
        batchIncludesDownloadConvert_ = false;
    }

    for (ConvertRequest& request : queued)
    {
        pendingConvertQueue_.push_back(std::move(request));
    }
    StartNextPendingConvert();
}

void DockArea::HandleConvertAllRequest()
{
    ClearFooterNotification();
    if (!AnyConvertRunning())
    {
        pendingConvertQueue_.clear();
        batchConvertElapsedTotal_ = 0.0;
        batchIncludesDownloadConvert_ = false;
        overwriteAllExisting_ = false;
    }

    std::vector<ConvertRequest> queued;
    for (const ConverterFileCardNode& card : converterCards_)
    {
        ConvertRequest request;
        if (BuildConvertRequestForCard(card, request))
        {
            queued.push_back(std::move(request));
        }
    }

    if (queued.empty())
    {
        PulseConverterFooterHint();
        return;
    }

    isBatchConverting_ = true;
    if (!AnyConvertRunning())
    {
        batchConvertElapsedTotal_ = 0.0;
        batchIncludesDownloadConvert_ = false;
    }
    overwriteAllExisting_ = false;
    for (ConvertRequest& request : queued)
    {
        pendingConvertQueue_.push_back(std::move(request));
    }
    StartNextPendingConvert();
    PushUndo(MakeConvertAllCommand());
}

bool DockArea::BuildConvertRequestForCard(const ConverterFileCardNode& card, ConvertRequest& request) const
{
    const ConverterSettingsSnapshot settings = ResolveConverterSettingsForCard(card);
    if (!card.HasFile() || card.IsLoading() || !card.Info().success ||
        (!settings.convertContainer && !settings.convertVideo && !settings.convertAudio))
    {
        return false;
    }

    const ConverterFileInfo& info = card.Info();
    // Global/default indices refer to the panel lists (no "Current" entries).
    // Per-card custom indices refer to BuildConverterItems() lists (source codec first).
    const bool usePanelCodecLists = card.UseDefaultConvertSettings();
    static const std::vector<std::string> kContainers = {"MP4", "MKV", "MOV", "WEBM"};
    const std::vector<std::string> containerItems =
        usePanelCodecLists ? kContainers : BuildConverterItems(kContainers, info.container);
    const int containerIndex =
        containerItems.empty()
            ? 0
            : std::clamp(settings.convertContainerIndex, 0, static_cast<int>(containerItems.size()) - 1);
    const std::string effectiveContainer = EffectiveConvertContainer(
        settings.convertContainer, settings.convertContainerIndex, containerItems, info.container);
    const std::vector<std::string> compatibleVideos = CompatibleVideoCodecsForContainer(effectiveContainer);
    const std::vector<std::string> compatibleAudios = CompatibleAudioCodecsForContainer(effectiveContainer);
    const std::vector<std::string> videoItems =
        usePanelCodecLists ? compatibleVideos : BuildConverterItems(compatibleVideos, info.videoCodec);
    const std::vector<std::string> audioItems =
        usePanelCodecLists ? compatibleAudios : BuildConverterItems(compatibleAudios, info.audioCodec);
    const int videoIndex =
        videoItems.empty() ? 0 : std::clamp(settings.convertVideoIndex, 0, static_cast<int>(videoItems.size()) - 1);
    const int audioIndex =
        audioItems.empty() ? 0 : std::clamp(settings.convertAudioIndex, 0, static_cast<int>(audioItems.size()) - 1);

    std::filesystem::path outputPath = std::filesystem::u8path(globalDownloadPath_);

    request.inputPath = info.filePath;
    request.outputDirectory = PathUtf8(outputPath);
    request.convertContainer = settings.convertContainer;
    request.convertVideo = settings.convertVideo;
    request.convertAudio = settings.convertAudio;
    request.container = containerItems.empty() ? info.container : StripCurrentLabel(containerItems[containerIndex]);
    request.videoCodec = videoItems.empty() ? info.videoCodec : StripCurrentLabel(videoItems[videoIndex]);
    request.audioCodec = audioItems.empty() ? info.audioCodec : StripCurrentLabel(audioItems[audioIndex]);
    request.sourceDurationSeconds = info.durationSeconds;
    return true;
}

bool DockArea::BuildAutoConvertRequestForCard(const LinkCardNode& card, ConvertRequest& request) const
{
    if (!card.HasAutoConvertSnapshot())
    {
        return false;
    }
    const AutoConvertOptions& autoConvert = card.AutoConvertSnapshot();
    if (!autoConvert.IsActive() || card.LastDownloadedPath().empty())
    {
        return false;
    }

    static const std::vector<std::string> kContainers = {"MP4", "MKV", "MOV", "WEBM"};
    std::string fallbackContainer = card.ExpectedFileFormat();
    if (fallbackContainer.empty())
    {
        const DownloadOptions& downloadOptions = card.Options();
        const std::vector<std::string>& formats =
            downloadOptions.mediaMode == 2 ? card.AvailableAudioFormats() : card.AvailableVideoFormats();
        if (!formats.empty())
        {
            const int formatIndex = std::clamp(downloadOptions.fileFormat, 0, static_cast<int>(formats.size()) - 1);
            fallbackContainer = formats[formatIndex];
        }
    }
    if (fallbackContainer.empty())
    {
        fallbackContainer = "MP4";
    }
    const std::string container = EffectiveConvertContainer(
        autoConvert.convertContainer, autoConvert.containerIndex, kContainers, fallbackContainer);
    const std::vector<std::string> videoCodecs = CompatibleVideoCodecsForContainer(container);
    const std::vector<std::string> audioCodecs = CompatibleAudioCodecsForContainer(container);

    const int containerIndex = std::clamp(autoConvert.containerIndex, 0, static_cast<int>(kContainers.size()) - 1);
    const int videoIndex =
        videoCodecs.empty() ? 0 : std::clamp(autoConvert.videoIndex, 0, static_cast<int>(videoCodecs.size()) - 1);
    const int audioIndex =
        audioCodecs.empty() ? 0 : std::clamp(autoConvert.audioIndex, 0, static_cast<int>(audioCodecs.size()) - 1);

    std::filesystem::path outputPath = std::filesystem::u8path(
        card.HasAutoConvertDelivery() ? card.FinalOutputDirectory() : card.ExpectedOutputDirectory());
    if (outputPath.empty())
    {
        outputPath = std::filesystem::u8path(globalDownloadPath_);
    }

    std::string outputBaseName = card.OriginalNormalizedTitle();
    if (outputBaseName.empty())
    {
        outputBaseName = card.NormalizedTitle();
    }
    if (outputBaseName.empty())
    {
        const std::filesystem::path input = std::filesystem::u8path(card.LastDownloadedPath());
        outputBaseName = input.stem().u8string();
        const std::string suffix = "_downloaded";
        if (outputBaseName.size() > suffix.size() &&
            outputBaseName.compare(outputBaseName.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            outputBaseName.erase(outputBaseName.size() - suffix.size());
        }
    }

    request.inputPath = card.LastDownloadedPath();
    // Auto-convert must read the Documents staging file (*_downloaded.*), never a final
    // Videos/Title.mp4 вЂ” wiping by stem there used to delete the fresh .mov output.
    if (card.HasAutoConvertDelivery())
    {
        const std::filesystem::path input = std::filesystem::u8path(request.inputPath);
        const std::string stem = input.stem().u8string();
        const bool looksLikeStaging = stem.size() >= 11 && stem.compare(stem.size() - 11, 11, "_downloaded") == 0;
        if (!looksLikeStaging)
        {
            return false;
        }
    }
    request.outputDirectory = PathUtf8(outputPath);
    request.outputBaseName = std::move(outputBaseName);
    request.linkCardUrl = card.Url();
    request.deleteInputOnSuccess = true;
    request.convertContainer = autoConvert.convertContainer;
    request.convertVideo = autoConvert.convertVideo;
    request.convertAudio = autoConvert.convertAudio;
    request.container = kContainers[containerIndex];
    request.videoCodec = videoCodecs.empty() ? DefaultVideoCodecForContainer(container) : videoCodecs[videoIndex];
    request.audioCodec = audioCodecs.empty() ? DefaultAudioCodecForContainer(container) : audioCodecs[audioIndex];
    request.sourceDurationSeconds = card.DurationSeconds();

    // If codecs require a different container than the download, remux even when Container is unchecked.
    if (!request.convertContainer && (request.convertVideo || request.convertAudio))
    {
        const std::filesystem::path input = std::filesystem::u8path(request.inputPath);
        std::string inputExt = input.extension().string();
        if (!inputExt.empty() && inputExt.front() == '.')
        {
            inputExt = inputExt.substr(1);
        }
        inputExt = ToLowerAscii(inputExt);
        const std::string targetExt = ToLowerAscii(container);
        if (targetExt != inputExt && (request.convertVideo || request.convertAudio))
        {
            // Align output container with codec-compatible container when Container is unchecked.
            const std::vector<std::string> inputVideos = CompatibleVideoCodecsForContainer(inputExt);
            const bool videoOkInInput =
                !request.convertVideo ||
                std::find(inputVideos.begin(), inputVideos.end(), request.videoCodec) != inputVideos.end();
            if (!videoOkInInput)
            {
                request.convertContainer = true;
                request.container = container;
            }
        }
    }
    return true;
}

bool DockArea::IsAutoConvertActiveForDownload() const
{
    if (globalAutoConvert_.IsActive())
    {
        return true;
    }
    bool active = false;
    ForEachLinkCard(
        [&](const LinkCardNode& card)
        {
            if (card.CustomAutoConvert().IsActive())
            {
                active = true;
            }
        });
    return active;
}

AutoConvertOptions DockArea::ResolveAutoConvertOptionsForCard(const LinkCardNode& card) const
{
    if (card.IsExcludedFromAutoConvert())
    {
        return {};
    }
    if (card.CustomAutoConvert().IsActive())
    {
        return card.CustomAutoConvert();
    }
    if (globalAutoConvert_.IsActive())
    {
        return globalAutoConvert_;
    }
    return {};
}

void DockArea::UndoApplyGlobalAutoConvert(const AutoConvertOptions& options)
{
    globalAutoConvert_ = options;
}

void DockArea::UndoApplyLinkCustomAutoConvert(const std::vector<LinkCustomAutoConvertSnapshot>& snapshots)
{
    for (const LinkCustomAutoConvertSnapshot& snapshot : snapshots)
    {
        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                if (card.HasUrl(snapshot.url))
                {
                    card.SetCustomAutoConvert(snapshot.options);
                }
            });
    }
    customAutoConvertSelectionKey_.clear();
}

void DockArea::UndoApplyLinkExcludeFlags(const std::vector<LinkExcludeFlag>& flags)
{
    for (const LinkExcludeFlag& flag : flags)
    {
        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                if (card.HasUrl(flag.url))
                {
                    card.SetExcludedFromAutoConvert(flag.excluded);
                }
            });
    }
}

void DockArea::UndoInvokeDownloadAll()
{
    HandleDownloadAllRequest();
}

void DockArea::UndoInvokeCancelAllDownloads()
{
    HandleCancelAllDownloadsRequest();
}

void DockArea::UndoInvokeConvertAll()
{
    HandleConvertAllRequest();
}

void DockArea::UndoInvokeCancelAllConverts()
{
    HandleCancelAllConvertsRequest();
}

std::string DockArea::PredictAutoConvertExtension(const AutoConvertOptions& options, const std::string& downloadFormat)
{
    static const std::vector<std::string> kContainers = {"MP4", "MKV", "MOV", "WEBM"};
    const std::string container = EffectiveConvertContainer(
        options.convertContainer, options.containerIndex, kContainers, downloadFormat.empty() ? "MP4" : downloadFormat);
    return ToLowerAscii(container);
}

void DockArea::QueueAutoConvertForCard(LinkCardNode& card)
{
    ConvertRequest request;
    if (!BuildAutoConvertRequestForCard(card, request))
    {
        if (card.HasAutoConvertSnapshot())
        {
            WriteDebugLog("auto-convert not queued: build failed for " + card.Url() +
                          " path=" + card.LastDownloadedPath());
        }
        return;
    }
    WriteDebugLog("auto-convert queued: " + request.inputPath + " -> " + request.outputDirectory);

    if (!AnyConvertRunning() && !isBatchConverting_)
    {
        batchConvertElapsedTotal_ = 0.0;
        batchIncludesDownloadConvert_ = false;
    }
    isBatchConverting_ = true;
    batchIncludesDownloadConvert_ = true;
    if (card.AutoConvertStagingPath().empty() && !request.inputPath.empty())
    {
        card.SetAutoConvertStagingPath(request.inputPath);
    }
    pendingConvertQueue_.push_back(std::move(request));
    card.ClearAutoConvertSnapshot();
    StartNextPendingConvert();
}

void DockArea::CleanupLinkCardAutoConvertStaging(LinkCardNode& card, const std::string& fallbackInputPath)
{
    const std::string stagingDir = GetAutoConvertStagingPath();
    std::vector<std::string> remaining = CleanupAutoConvertStagingForCard(card, fallbackInputPath, stagingDir);
    if (!fallbackInputPath.empty())
    {
        remaining.push_back(fallbackInputPath);
    }
    if (!card.AutoConvertStagingPath().empty())
    {
        remaining.push_back(card.AutoConvertStagingPath());
    }
    ScheduleBackgroundFileDeletes(std::move(remaining));
    card.ClearAutoConvertDelivery();
}

void DockArea::EnqueuePendingStagingCleanup(const std::string& path)
{
    if (path.empty())
    {
        return;
    }
    if (std::find(pendingStagingCleanupPaths_.begin(), pendingStagingCleanupPaths_.end(), path) ==
        pendingStagingCleanupPaths_.end())
    {
        pendingStagingCleanupPaths_.push_back(path);
    }
}

void DockArea::ProcessPendingStagingCleanup()
{
    if (pendingStagingCleanupPaths_.empty())
    {
        return;
    }
    // Hand off to a background thread so the UI never blocks on locked files.
    ScheduleBackgroundFileDeletes(std::move(pendingStagingCleanupPaths_));
    pendingStagingCleanupPaths_.clear();
}

void DockArea::MaybeShowDownloadConvertBatchFinished()
{
    if (!batchIncludesDownloadConvert_)
    {
        return;
    }
    if (AnyDownloadRunning() || !pendingDownloadQueue_.empty())
    {
        return;
    }
    if (AnyConvertRunning() || !pendingConvertQueue_.empty() || AnyLinkCardConverting())
    {
        return;
    }
    if (isOverwritePromptOpen_)
    {
        return;
    }

    double totalElapsed = 0.0;
    int finishedJobs = 0;
    ForEachLinkCard(
        [&](const LinkCardNode& card)
        {
            if (!card.HasCompletedDownload() || !card.HasCompletedConvert())
            {
                return;
            }
            totalElapsed += card.DownloadElapsedSeconds() + card.ConvertElapsedSeconds();
            ++finishedJobs;
        });
    if (finishedJobs <= 0)
    {
        return;
    }

    batchIncludesDownloadConvert_ = false;
    isBatchConverting_ = false;
    overwriteAllExisting_ = false;
    ShowFooterNotification(
        FormatDownloadConvertFinishedStatus(totalElapsed, true), FooterNotificationScope::Any, "", footerClipboardLog_);
}

bool DockArea::AnyLinkCardConverting() const
{
    bool converting = false;
    ForEachLinkCard(
        [&](const LinkCardNode& card)
        {
            if (card.IsConverting())
            {
                converting = true;
            }
        });
    return converting;
}

void DockArea::CancelLinkCardConvert(const std::string& inputPath)
{
    RemovePendingConvertsForPath(inputPath);
    ForEachLinkCard(
        [&](LinkCardNode& card)
        {
            if (card.IsConverting() && (inputPath.empty() || card.HasDownloadedPath(inputPath)))
            {
                card.ClearConverting();
            }
        });

    if (ConvertRunner* runner = FindConvertRunnerByPath(inputPath))
    {
        runner->Cancel();
    }
}

bool DockArea::PrepareConvertRequest(ConvertRequest& request)
{
    try
    {
        if (!IsValidUserOutputPath(request.outputDirectory))
        {
            pendingConvertQueue_.clear();
            isBatchConverting_ = false;
            overwriteAllExisting_ = false;
            const std::string status = "Invalid convert path";
            ShowFooterNotification(status, FooterNotificationScope::Converter, status);
            return false;
        }

        std::filesystem::path outputPath = std::filesystem::u8path(request.outputDirectory);
        if (!outputPath.is_absolute())
        {
            pendingConvertQueue_.clear();
            isBatchConverting_ = false;
            overwriteAllExisting_ = false;
            const std::string status = "Invalid convert path";
            ShowFooterNotification(status, FooterNotificationScope::Converter, status);
            return false;
        }

        std::error_code error;
        std::filesystem::create_directories(outputPath, error);
        if (error)
        {
            const std::string status = "Convert failed: could not create output path.";
            ShowFooterNotification(status, FooterNotificationScope::Converter, status);
            if (!isBatchConverting_)
            {
                pendingConvertQueue_.clear();
            }
            return false;
        }
        request.outputDirectory = PathUtf8(outputPath);

        const std::filesystem::path outputFile = ConvertRunner::GetOutputPath(request);
        if (outputFile.empty())
        {
            const std::string status = "Convert failed: invalid output path.";
            ShowFooterNotification(status, FooterNotificationScope::Converter, status);
            if (!isBatchConverting_)
            {
                pendingConvertQueue_.clear();
            }
            return false;
        }

        if (std::filesystem::exists(outputFile))
        {
            if (overwriteAllExisting_)
            {
                request.overwriteExisting = true;
                return true;
            }

            pendingOverwriteConvertRequest_ = request;
            pendingOverwriteFileName_ = PathUtf8(outputFile.filename());
            overwritePromptIsConvert_ = true;
            isOverwritePromptOpen_ = true;
            overwritePromptFocusIndex_ = 0;
            return false;
        }

        return true;
    }
    catch (const std::exception& exception)
    {
        const std::string status = std::string("Convert failed: ") + exception.what();
        ShowFooterNotification(status, FooterNotificationScope::Converter, status);
        if (!isBatchConverting_)
        {
            pendingConvertQueue_.clear();
        }
        return false;
    }
    catch (...)
    {
        const std::string status = "Convert failed: unexpected prepare error.";
        ShowFooterNotification(status, FooterNotificationScope::Converter, status);
        if (!isBatchConverting_)
        {
            pendingConvertQueue_.clear();
        }
        return false;
    }
}

bool DockArea::StartNextPendingConvert()
{
    bool startedAny = false;
    while (FirstFreeConvertRunner() != nullptr && !pendingConvertQueue_.empty() && !isOverwritePromptOpen_)
    {
        ConvertRequest request = std::move(pendingConvertQueue_.front());
        pendingConvertQueue_.erase(pendingConvertQueue_.begin());
        if (!PrepareConvertRequest(request))
        {
            if (isOverwritePromptOpen_)
            {
                return startedAny;
            }

            continue;
        }

        StartConvert(std::move(request));
        startedAny = true;
    }

    if (isBatchConverting_ && !AnyConvertRunning() && pendingConvertQueue_.empty() && !isOverwritePromptOpen_)
    {
        // Keep downloadв†’convert batches alive until ProcessFinishedConvertRunner reports the total.
        if (!batchIncludesDownloadConvert_)
        {
            isBatchConverting_ = false;
            overwriteAllExisting_ = false;
        }
    }

    return startedAny;
}

void DockArea::StartConvert(ConvertRequest request)
{
    ConvertRunner* runner = FirstFreeConvertRunner();
    if (runner == nullptr)
    {
        pendingConvertQueue_.insert(pendingConvertQueue_.begin(), request);
        return;
    }

    if (!batchIncludesDownloadConvert_)
    {
        ClearFooterNotification();
    }

    const std::string inputPath = request.inputPath;
    const std::string linkCardUrl = request.linkCardUrl;
    for (ConverterFileCardNode& card : converterCards_)
    {
        if (card.HasFilePath(inputPath))
        {
            card.SetConverting();
            break;
        }
    }
    ForEachLinkCard(
        [&](LinkCardNode& card)
        {
            if ((!linkCardUrl.empty() && card.HasUrl(linkCardUrl)) || card.HasDownloadedPath(inputPath))
            {
                if (card.AutoConvertStagingPath().empty() && !inputPath.empty())
                {
                    card.SetAutoConvertStagingPath(inputPath);
                }
                card.SetConverting();
            }
        });

    runner->Start(std::move(request));
}

void DockArea::RemovePendingConvertsForPath(const std::string& inputPath)
{
    pendingConvertQueue_.erase(std::remove_if(pendingConvertQueue_.begin(),
                                              pendingConvertQueue_.end(),
                                              [&](const ConvertRequest& request)
                                              {
                                                  return request.inputPath == inputPath;
                                              }),
                               pendingConvertQueue_.end());
}

void DockArea::CancelConverterCard(const std::string& inputPath)
{
    RemovePendingConvertsForPath(inputPath);
    for (ConverterFileCardNode& card : converterCards_)
    {
        if (card.HasFilePath(inputPath))
        {
            card.ClearConverting();
            break;
        }
    }

    if (ConvertRunner* runner = FindConvertRunnerByPath(inputPath))
    {
        runner->Cancel();
    }

    if (!isBatchConverting_)
    {
        pendingConvertQueue_.clear();
    }
}

void DockArea::UpdateOverwritePrompt(int windowWidth, int windowHeight)
{
    const float modalWidth = 420.0f;
    const float modalHeight = 145.0f;
    const Rectangle modal = {(static_cast<float>(windowWidth) - modalWidth) * 0.5f,
                             (static_cast<float>(windowHeight) - modalHeight) * 0.5f,
                             modalWidth,
                             modalHeight};

    const Rectangle replaceBounds = {modal.x + modal.width - 318.0f, modal.y + modal.height - 48.0f, 96.0f, 34.0f};
    const Rectangle cancelBounds = {modal.x + modal.width - 212.0f, modal.y + modal.height - 48.0f, 84.0f, 34.0f};
    const Rectangle cancelAllBounds = {modal.x + modal.width - 118.0f, modal.y + modal.height - 48.0f, 100.0f, 34.0f};

    if (IsKeyPressed(KEY_TAB))
    {
        if (ShortcutRouter::ShiftDown())
        {
            overwritePromptFocusIndex_ = (overwritePromptFocusIndex_ + 2) % 3;
        }
        else
        {
            overwritePromptFocusIndex_ = (overwritePromptFocusIndex_ + 1) % 3;
        }
    }

    const bool activateFocused = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);
    const bool clickedReplace = replaceFileButton_.Update(replaceBounds);
    const bool clickedCancel = cancelReplaceButton_.Update(cancelBounds);
    const bool clickedCancelAll = cancelAllReplaceButton_.Update(cancelAllBounds);

    if (clickedReplace || (activateFocused && overwritePromptFocusIndex_ == 0))
    {
        if (overwritePromptIsConvert_)
        {
            if (isBatchConverting_)
            {
                overwriteAllExisting_ = true;
            }
            pendingOverwriteConvertRequest_.overwriteExisting = true;
            StartConvert(std::move(pendingOverwriteConvertRequest_));
            pendingOverwriteConvertRequest_ = {};
            pendingOverwriteFileName_.clear();
            isOverwritePromptOpen_ = false;
            overwritePromptIsConvert_ = false;
        }
        else
        {
            if (isBatchDownloading_)
            {
                overwriteAllExisting_ = true;
            }
            pendingOverwriteRequest_.overwriteExisting = true;
            StartDownload(std::move(pendingOverwriteRequest_));
            pendingOverwriteRequest_ = {};
            pendingOverwriteFileName_.clear();
            isOverwritePromptOpen_ = false;
        }
    }
    else if (clickedCancel || IsKeyPressed(KEY_ESCAPE) || (activateFocused && overwritePromptFocusIndex_ == 1))
    {
        const bool wasConvertPrompt = overwritePromptIsConvert_;
        pendingOverwriteRequest_ = {};
        pendingOverwriteConvertRequest_ = {};
        pendingOverwriteFileName_.clear();
        isOverwritePromptOpen_ = false;
        overwritePromptIsConvert_ = false;
        if (wasConvertPrompt)
        {
            if (isBatchConverting_ && !pendingConvertQueue_.empty())
            {
                StartNextPendingConvert();
                ShowFooterNotification("Skipped existing file.", FooterNotificationScope::Converter);
            }
            else
            {
                pendingConvertQueue_.clear();
                isBatchConverting_ = false;
                overwriteAllExisting_ = false;
                ShowFooterNotification("Convert cancelled.", FooterNotificationScope::Converter);
            }
        }
        else if (isBatchDownloading_ && !pendingDownloadQueue_.empty())
        {
            nextDownloadStartTime_ = GetTime();
            ShowFooterNotification("Skipped existing file.", FooterNotificationScope::Downloader);
        }
        else
        {
            pendingDownloadQueue_.clear();
            isBatchDownloading_ = false;
            overwriteAllExisting_ = false;
            ClearBatchQueueStates();
            ShowFooterNotification("Download cancelled.", FooterNotificationScope::Downloader);
        }
    }
    else if (clickedCancelAll || (activateFocused && overwritePromptFocusIndex_ == 2))
    {
        const bool wasConvertPrompt = overwritePromptIsConvert_;
        pendingOverwriteRequest_ = {};
        pendingOverwriteConvertRequest_ = {};
        pendingOverwriteFileName_.clear();
        isOverwritePromptOpen_ = false;
        overwritePromptIsConvert_ = false;
        if (wasConvertPrompt)
        {
            pendingConvertQueue_.clear();
            isBatchConverting_ = false;
            overwriteAllExisting_ = false;
            ShowFooterNotification("Convert cancelled.", FooterNotificationScope::Converter);
        }
        else
        {
            pendingDownloadQueue_.clear();
            isBatchDownloading_ = false;
            overwriteAllExisting_ = false;
            ClearBatchQueueStates();
            ShowFooterNotification("Download cancelled.", FooterNotificationScope::Downloader);
        }
    }
}

void DockArea::DrawAutoConvertDock(Rectangle autoConvertPanel, Font font, bool drawControls, bool drawPopups) const
{
    const AutoConvertOptions& autoConvert = globalAutoConvert_;
    const AutoConvertOptions& customConvert = customAutoConvert_;
    const AutoConvertDockLayout layout = GetAutoConvertDockLayout(autoConvertPanel,
                                                                  autoConvertSectionFoldout_.IsExpanded(),
                                                                  autoConvertFoldout_.IsExpanded(),
                                                                  customAutoConvertFoldout_.IsExpanded());

    const bool anyAutoDropdownOpen = autoConvertContainerDropdown_.IsOpen() || autoConvertVideoDropdown_.IsOpen() ||
                                     autoConvertAudioDropdown_.IsOpen();
    const bool anyCustomDropdownOpen = customAutoConvertContainerDropdown_.IsOpen() ||
                                       customAutoConvertVideoDropdown_.IsOpen() ||
                                       customAutoConvertAudioDropdown_.IsOpen();
    const bool anyDockDropdownOpen = anyAutoDropdownOpen || anyCustomDropdownOpen;

    bool customUiEnabled = false;
    {
        bool hasSelection = false;
        bool allExcluded = true;
        ForEachLinkCard(
            [&](const LinkCardNode& card)
            {
                if (!card.IsSelected() || !card.IsValid())
                {
                    return;
                }
                hasSelection = true;
                if (!card.IsExcludedFromAutoConvert())
                {
                    allExcluded = false;
                }
            });
        customUiEnabled = hasSelection && !allExcluded;
    }

    if (drawControls)
    {
        autoConvertSectionFoldout_.Draw(layout.sectionFoldoutPanelBounds, font, true);

        if (autoConvertSectionFoldout_.IsExpanded())
        {
            const float checkX = layout.nestedCheckX;
            const float autoDropdownX = layout.nestedDropdownX;
            const float autoDropdownW = layout.nestedDropdownW;

            autoConvertFoldout_.Draw(layout.autoFoldoutPanelBounds, font, true);
            autoConvertEnabledCheckbox_.Draw(autoConvertFoldout_.HeaderCheckboxBounds(layout.autoFoldoutPanelBounds),
                                             font,
                                             "",
                                             autoConvert.enabled,
                                             true);

            const float labelToDropdown = std::max(4.0f, autoDropdownX - (checkX + 18.0f + 8.0f) - 4.0f);
            const float labelToSection =
                std::max(4.0f,
                         layout.sectionFoldoutPanelBounds.x + layout.sectionFoldoutPanelBounds.width -
                             (checkX + 18.0f + 8.0f) - 10.0f);

            if (autoConvertFoldout_.IsExpanded())
            {
                autoConvertContainerCheckbox_.Draw({checkX, layout.autoContainerY + 3.0f, 18.0f, 18.0f},
                                                   font,
                                                   "Container",
                                                   autoConvert.convertContainer,
                                                   autoConvert.enabled,
                                                   labelToDropdown);
                autoConvertVideoCheckbox_.Draw({checkX, layout.autoVideoY + 3.0f, 18.0f, 18.0f},
                                               font,
                                               "Video",
                                               autoConvert.convertVideo,
                                               autoConvert.enabled,
                                               labelToDropdown);
                autoConvertAudioCheckbox_.Draw({checkX, layout.autoAudioY + 3.0f, 18.0f, 18.0f},
                                               font,
                                               "Audio",
                                               autoConvert.convertAudio,
                                               autoConvert.enabled,
                                               labelToDropdown);

                autoConvertContainerDropdown_.DrawControl({autoDropdownX, layout.autoContainerY, autoDropdownW, 25.0f},
                                                          font,
                                                          autoConvert.containerIndex,
                                                          autoConvert.enabled && autoConvert.convertContainer,
                                                          !anyDockDropdownOpen ||
                                                              autoConvertContainerDropdown_.IsOpen());
                autoConvertVideoDropdown_.DrawControl({autoDropdownX, layout.autoVideoY, autoDropdownW, 25.0f},
                                                      font,
                                                      autoConvert.videoIndex,
                                                      autoConvert.enabled && autoConvert.convertVideo,
                                                      !anyDockDropdownOpen || autoConvertVideoDropdown_.IsOpen());
                autoConvertAudioDropdown_.DrawControl({autoDropdownX, layout.autoAudioY, autoDropdownW, 25.0f},
                                                      font,
                                                      autoConvert.audioIndex,
                                                      autoConvert.enabled && autoConvert.convertAudio,
                                                      !anyDockDropdownOpen || autoConvertAudioDropdown_.IsOpen());
            }

            {
                bool excludeChecked = false;
                bool excludeEnabled = false;
                std::vector<const LinkCardNode*> selectedCards;
                bool anyBusy = false;
                ForEachLinkCard(
                    [&](const LinkCardNode& card)
                    {
                        if (!card.IsSelected() || !card.IsValid())
                        {
                            return;
                        }
                        selectedCards.push_back(&card);
                        if (card.IsDownloading() || card.IsConverting())
                        {
                            anyBusy = true;
                        }
                    });
                excludeEnabled = (autoConvert.enabled || customConvert.enabled) && !selectedCards.empty() && !anyBusy;
                excludeChecked = !selectedCards.empty();
                for (const LinkCardNode* card : selectedCards)
                {
                    if (!card->IsExcludedFromAutoConvert())
                    {
                        excludeChecked = false;
                        break;
                    }
                }
                autoConvertExcludeCheckbox_.Draw({checkX, layout.excludeY + 3.0f, 18.0f, 18.0f},
                                                 font,
                                                 "Exclude selected",
                                                 excludeChecked,
                                                 excludeEnabled,
                                                 labelToSection);
            }

            customAutoConvertFoldout_.Draw(layout.customFoldoutPanelBounds, font, customUiEnabled);
            customAutoConvertEnabledCheckbox_.Draw(
                customAutoConvertFoldout_.HeaderCheckboxBounds(layout.customFoldoutPanelBounds),
                font,
                "",
                customConvert.enabled,
                customUiEnabled);

            if (customAutoConvertFoldout_.IsExpanded())
            {
                const bool customControlsEnabled = customUiEnabled && customConvert.enabled;
                customAutoConvertContainerCheckbox_.Draw({checkX, layout.customContainerY + 3.0f, 18.0f, 18.0f},
                                                         font,
                                                         "Container",
                                                         customConvert.convertContainer,
                                                         customControlsEnabled,
                                                         labelToDropdown);
                customAutoConvertVideoCheckbox_.Draw({checkX, layout.customVideoY + 3.0f, 18.0f, 18.0f},
                                                     font,
                                                     "Video",
                                                     customConvert.convertVideo,
                                                     customControlsEnabled,
                                                     labelToDropdown);
                customAutoConvertAudioCheckbox_.Draw({checkX, layout.customAudioY + 3.0f, 18.0f, 18.0f},
                                                     font,
                                                     "Audio",
                                                     customConvert.convertAudio,
                                                     customControlsEnabled,
                                                     labelToDropdown);

                customAutoConvertContainerDropdown_.DrawControl(
                    {autoDropdownX, layout.customContainerY, autoDropdownW, 25.0f},
                    font,
                    customConvert.containerIndex,
                    customControlsEnabled && customConvert.convertContainer,
                    customUiEnabled && (!anyDockDropdownOpen || customAutoConvertContainerDropdown_.IsOpen()));
                customAutoConvertVideoDropdown_.DrawControl(
                    {autoDropdownX, layout.customVideoY, autoDropdownW, 25.0f},
                    font,
                    customConvert.videoIndex,
                    customControlsEnabled && customConvert.convertVideo,
                    customUiEnabled && (!anyDockDropdownOpen || customAutoConvertVideoDropdown_.IsOpen()));
                customAutoConvertAudioDropdown_.DrawControl(
                    {autoDropdownX, layout.customAudioY, autoDropdownW, 25.0f},
                    font,
                    customConvert.audioIndex,
                    customControlsEnabled && customConvert.convertAudio,
                    customUiEnabled && (!anyDockDropdownOpen || customAutoConvertAudioDropdown_.IsOpen()));
            }
        }
    }

    if (drawPopups && autoConvertSectionFoldout_.IsExpanded())
    {
        const float autoDropdownX = layout.nestedDropdownX;
        const float autoDropdownW = layout.nestedDropdownW;
        if (autoConvertFoldout_.IsExpanded() && autoConvert.enabled)
        {
            if (autoConvert.convertContainer)
            {
                autoConvertContainerDropdown_.DrawPopup(
                    {autoDropdownX, layout.autoContainerY, autoDropdownW, 25.0f}, font, autoConvert.containerIndex);
            }
            if (autoConvert.convertVideo)
            {
                autoConvertVideoDropdown_.DrawPopup(
                    {autoDropdownX, layout.autoVideoY, autoDropdownW, 25.0f}, font, autoConvert.videoIndex);
            }
            if (autoConvert.convertAudio)
            {
                autoConvertAudioDropdown_.DrawPopup(
                    {autoDropdownX, layout.autoAudioY, autoDropdownW, 25.0f}, font, autoConvert.audioIndex);
            }
        }
        if (customUiEnabled && customAutoConvertFoldout_.IsExpanded() && customConvert.enabled)
        {
            if (customConvert.convertContainer)
            {
                customAutoConvertContainerDropdown_.DrawPopup(
                    {autoDropdownX, layout.customContainerY, autoDropdownW, 25.0f}, font, customConvert.containerIndex);
            }
            if (customConvert.convertVideo)
            {
                customAutoConvertVideoDropdown_.DrawPopup(
                    {autoDropdownX, layout.customVideoY, autoDropdownW, 25.0f}, font, customConvert.videoIndex);
            }
            if (customConvert.convertAudio)
            {
                customAutoConvertAudioDropdown_.DrawPopup(
                    {autoDropdownX, layout.customAudioY, autoDropdownW, 25.0f}, font, customConvert.audioIndex);
            }
        }
    }
}

void DockArea::DrawRightPanel(Rectangle rightPanel, Font font) const
{
    const Rectangle settingsPanel = GetRightSettingsPanel(rightPanel);
    const Rectangle globalPanel = GetGlobalPathPanel(rightPanel);
    const LinkCardNode* selectedCard = GetSelectedCard();
    if (selectedCard != nullptr && !selectedCard->IsValid())
    {
        selectedCard = nullptr;
    }
    const LinkCardGroupNode* selectedGroup = GetSelectedGroupHeader();
    bool drawingGroup = false;
    if (selectedCard == nullptr && selectedGroup != nullptr && selectedGroup->IsValid())
    {
        drawingGroup = true;
        if (!selectedGroup->LoadedCards().empty())
        {
            selectedCard = &selectedGroup->LoadedCards().front();
        }
    }
    const Color text = {224, 230, 224, 255};
    const Color muted = {150, 162, 150, 255};

    {
        const float optionsTitleMax = std::max(4.0f, settingsPanel.width - 24.0f);
        const std::string optionsTitle = TruncateTextToWidth(font, "Options", 16.0f, optionsTitleMax);
        DrawTextEx(font, optionsTitle.c_str(), {settingsPanel.x + 12.0f, settingsPanel.y + 12.0f}, 16.0f, 0.0f, text);
    }

    const Rectangle downloadButton = GetDownloadButtonBounds(settingsPanel);

    if (!drawingGroup && selectedCard == nullptr)
    {
        const float hintX = settingsPanel.x + 14.0f;
        const float hintY = settingsPanel.y + 40.0f;
        const float hintW = std::max(4.0f, settingsPanel.width - 28.0f);
        const float hintH = std::max(0.0f, downloadButton.y - 8.0f - hintY);
        const int maxLines = std::max(1, static_cast<int>(hintH / 18.0f));
        if (hintH > 4.0f)
        {
            UiClip::Push({hintX, hintY, hintW, hintH});
            DrawWrappedText(
                font, "Select a valid card to edit download settings.", {hintX, hintY}, 15.0f, hintW, maxLines, muted);
            UiClip::Pop();
        }
    }
    else
    {
        const DownloadOptions& options = drawingGroup ? selectedGroup->Options() : selectedCard->Options();
        bool optionsEnabled = true;
        if (drawingGroup)
        {
            for (const LinkCardNode& child : selectedGroup->LoadedCards())
            {
                if (child.IsDownloading() || child.IsConverting())
                {
                    optionsEnabled = false;
                    break;
                }
            }
        }
        else
        {
            optionsEnabled = !selectedCard->IsDownloading() && !selectedCard->IsConverting();
        }
        const Rectangle optionsViewport = GetOptionsScrollViewport(settingsPanel, downloadButton.y);
        const float contentHeight = GetDownloaderOptionsContentHeight(settingsPanel.y, downloadFoldout_.IsExpanded());
        const float maxOptionsScroll = std::max(0.0f, contentHeight - optionsViewport.height);
        const float scrollOffset = std::clamp(optionsScrollOffset_, 0.0f, maxOptionsScroll);

        const DownloaderPanelLayout layout = GetDownloaderPanelLayout(
            settingsPanel.x, settingsPanel.y, settingsPanel.width, downloadFoldout_.IsExpanded(), scrollOffset);

        BeginOptionsContentScissor(optionsViewport);

        const bool anyOptionsDropdownOpen =
            fileFormatDropdown_.IsOpen() || mediaModeDropdown_.IsOpen() || qualityDropdown_.IsOpen();

        const Rectangle keepIndicesBounds = {settingsPanel.x + 14.0f, layout.keepIndicesRowY, 18.0f, 18.0f};
        const float optionsLabelMax =
            std::max(4.0f, settingsPanel.x + settingsPanel.width - (keepIndicesBounds.x + 18.0f + 8.0f) - 12.0f);
        keepIndicesCheckbox_.Draw(
            keepIndicesBounds, font, "Keep numbering", keepDownloadIndices_, optionsEnabled, optionsLabelMax);

        downloadFoldout_.Draw(layout.foldoutPanelBounds, font, optionsEnabled);

        if (downloadFoldout_.IsExpanded())
        {
            const Color labelColor = optionsEnabled ? muted : Color{96, 108, 96, 255};
            DrawTextEx(
                font, "Quality", {settingsPanel.x + 26.0f, layout.qualityDropdownY + 4.0f}, 15.0f, 0.0f, labelColor);
            DrawTextEx(
                font, "Format", {settingsPanel.x + 26.0f, layout.formatDropdownY + 4.0f}, 15.0f, 0.0f, labelColor);
            DrawTextEx(
                font, "Audio/Video", {settingsPanel.x + 18.0f, layout.mediaDropdownY + 4.0f}, 15.0f, 0.0f, labelColor);

            const Rectangle qualityBounds = {
                settingsPanel.x + 94.0f, layout.qualityDropdownY, settingsPanel.width - 108.0f, 25.0f};
            const Rectangle formatBounds = {
                settingsPanel.x + 94.0f, layout.formatDropdownY, settingsPanel.width - 108.0f, 25.0f};
            const Rectangle mediaBounds = {
                settingsPanel.x + 94.0f, layout.mediaDropdownY, settingsPanel.width - 108.0f, 25.0f};

            qualityDropdown_.DrawControl(qualityBounds,
                                         font,
                                         options.quality,
                                         optionsEnabled && options.mediaMode != 2,
                                         !anyOptionsDropdownOpen || qualityDropdown_.IsOpen());
            fileFormatDropdown_.DrawControl(formatBounds,
                                            font,
                                            options.fileFormat,
                                            optionsEnabled,
                                            !anyOptionsDropdownOpen || fileFormatDropdown_.IsOpen());
            mediaModeDropdown_.DrawControl(mediaBounds,
                                           font,
                                           options.mediaMode,
                                           optionsEnabled,
                                           !anyOptionsDropdownOpen || mediaModeDropdown_.IsOpen());
        }

        DrawTextEx(font,
                   "Path",
                   {settingsPanel.x + 46.0f, layout.pathRowY},
                   15.0f,
                   0.0f,
                   optionsEnabled ? muted : Color{96, 108, 96, 255});

        const Rectangle customPathCheckboxBounds = {settingsPanel.x + 94.0f, layout.pathRowY, 18.0f, 18.0f};
        const float customPathLabelMax =
            std::max(4.0f, settingsPanel.x + settingsPanel.width - (customPathCheckboxBounds.x + 18.0f + 8.0f) - 12.0f);
        customPathCheckbox_.Draw(
            customPathCheckboxBounds, font, "Custom path", options.useCustomPath, optionsEnabled, customPathLabelMax);
        customPathField_.Draw({settingsPanel.x + 14.0f, layout.pathFieldY, settingsPanel.width - 28.0f, 25.0f},
                              font,
                              options.customPath,
                              optionsEnabled && options.useCustomPath,
                              &optionsViewport);

        const AutoConvertOptions predictionOptions =
            selectedCard != nullptr ? ResolveAutoConvertOptionsForCard(*selectedCard) : globalAutoConvert_;
        DownloadOptions predictDownloadOptions = options;
        PredictedDownload prediction{};
        if (selectedCard != nullptr)
        {
            std::vector<std::string> predictFormats =
                options.mediaMode == 2 ? selectedCard->AvailableAudioFormats() : selectedCard->AvailableVideoFormats();
            if (predictFormats.empty())
            {
                predictFormats.push_back(options.mediaMode == 2 ? "M4A" : "MP4");
            }
            const std::vector<std::string>& cardQualities = selectedCard->AvailableQualities();
            std::string predictQuality;
            if (drawingGroup)
            {
                predictQuality = GroupQualityCapFromOptions(options);
                if (predictQuality == "Max")
                {
                    predictQuality.clear();
                }
            }
            else if (!options.qualityCap.empty() && options.qualityCap != "Max")
            {
                predictQuality = options.qualityCap;
            }
            else if (!cardQualities.empty())
            {
                predictQuality = cardQualities[static_cast<size_t>(
                    std::clamp(options.quality, 0, static_cast<int>(cardQualities.size()) - 1))];
            }
            std::vector<std::string> predictQualities = cardQualities;
            if (drawingGroup)
            {
                predictQualities = BuildGroupQualityCapItems();
                predictDownloadOptions.quality = options.quality;
            }
            else if (!predictQuality.empty())
            {
                // Force predictor to use the explicit cap string.
                predictQualities = {predictQuality};
                predictDownloadOptions.quality = 0;
            }
            predictFormats = BuildFormatItemsForQuality(
                predictFormats, selectedCard->FormatStreams(), predictQuality, options.mediaMode);
            if (predictDownloadOptions.fileFormat < 0 ||
                predictDownloadOptions.fileFormat >= static_cast<int>(predictFormats.size()) ||
                Dropdown::IsInactiveItem(predictFormats[predictDownloadOptions.fileFormat]))
            {
                predictDownloadOptions.fileFormat = FirstActiveFormatIndex(predictFormats);
            }
            prediction = ApplyAutoConvertToPrediction(PredictDownload(selectedCard->FormatStreams(),
                                                                      selectedCard->AvailableVideoFormats(),
                                                                      selectedCard->AvailableAudioFormats(),
                                                                      predictQualities,
                                                                      predictDownloadOptions),
                                                      predictionOptions);
        }
        downloadResultSection_.Draw(layout.resultPanelBounds, font, optionsEnabled);
        DrawDownloadResultPreview(font, settingsPanel, prediction, layout.resultLineY);

        DrawOptionsScrollbar(optionsViewport, scrollOffset, maxOptionsScroll);
        UiClip::Pop();

        if (downloadFoldout_.IsExpanded())
        {
            const Rectangle qualityBounds = {
                settingsPanel.x + 94.0f, layout.qualityDropdownY, settingsPanel.width - 108.0f, 25.0f};
            const Rectangle formatBounds = {
                settingsPanel.x + 94.0f, layout.formatDropdownY, settingsPanel.width - 108.0f, 25.0f};
            const Rectangle mediaBounds = {
                settingsPanel.x + 94.0f, layout.mediaDropdownY, settingsPanel.width - 108.0f, 25.0f};

            if (options.mediaMode != 2)
            {
                qualityDropdown_.DrawPopup(qualityBounds, font, options.quality);
            }
            fileFormatDropdown_.DrawPopup(formatBounds, font, options.fileFormat);
            mediaModeDropdown_.DrawPopup(mediaBounds, font, options.mediaMode);
        }
    }

    if (SelectedCardShowsCancel())
    {
        cancelDownloadButton_.DrawDanger(GetDownloadButtonBounds(settingsPanel), font);
        if (HasDownloadableIdleCards())
        {
            downloadAllButton_.Draw(GetSecondaryActionButtonBounds(settingsPanel), font);
        }
        else
        {
            cancelAllActionButton_.DrawDanger(GetSecondaryActionButtonBounds(settingsPanel), font);
        }
    }
    else if (HasActiveDownloadWorkspaceWork())
    {
        downloadButton_.Draw(GetDownloadButtonBounds(settingsPanel), font, CanDownloadSelected());
        cancelAllActionButton_.DrawDanger(GetSecondaryActionButtonBounds(settingsPanel), font);
    }
    else
    {
        downloadButton_.Draw(GetDownloadButtonBounds(settingsPanel), font, CanDownloadSelected());
        downloadAllButton_.Draw(GetSecondaryActionButtonBounds(settingsPanel), font, HasDownloadableIdleCards());
    }

    DrawGlobalPathLabel(font, globalPanel, "Global Download Path", text);
    globalPathField_.Draw({globalPanel.x + 10.0f, globalPanel.y + 34.0f, globalPanel.width - 20.0f, 26.0f},
                          font,
                          globalDownloadPath_,
                          true);
}

void DockArea::DrawHeader(Rectangle header, Font font) const
{
    DrawRectangleRec(header, Color{24, 24, 24, 255});

    const auto drawTab = [&](Rectangle bounds, const char* label, bool active)
    {
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
        const Color background =
            active ? Color{44, 52, 44, 255} : (hovered ? Color{36, 42, 36, 255} : Color{24, 24, 24, 255});
        const Color text = active ? Color{232, 238, 232, 255} : Color{168, 178, 168, 255};
        const Vector2 labelSize = MeasureTextEx(font, label, 14.0f, 0.0f);

        DrawRectangleRounded(bounds, 0.18f, 8, background);
        if (active)
        {
            DrawRectangleRec({bounds.x, bounds.y + bounds.height - 2.0f, bounds.width, 2.0f},
                             Color{104, 150, 104, 255});
        }
        DrawTextEx(font, label, {bounds.x + (bounds.width - labelSize.x) * 0.5f, bounds.y + 2.0f}, 14.0f, 0.0f, text);
    };

    drawTab(HeaderLayout::AboutButton(header.y), "About", false);
    drawTab(HeaderLayout::InfoButton(header.y), "Info", false);
    drawTab(HeaderLayout::DownloaderTab(header.y), "Downloader", activeWorkspace_ == Workspace::Downloader);
    drawTab(HeaderLayout::ConverterTab(header.y), "Converter", activeWorkspace_ == Workspace::Converter);

    const float separatorTop = header.y + 5.0f;
    const float separatorBottom = header.y + header.height - 5.0f;
    DrawLineEx({HeaderLayout::kSeparatorX, separatorTop},
               {HeaderLayout::kSeparatorX, separatorBottom},
               1.0f,
               Color{72, 78, 72, 255});
}

namespace
{
enum class FooterNotificationTone
{
    Error,
    Success,
    Progress,
    Hint,
};

FooterNotificationTone GetFooterNotificationTone(const std::string& status, bool isRunning)
{
    if (isRunning)
    {
        return FooterNotificationTone::Progress;
    }

    if (status == "Select an Option" || status == "Download cancelled." || status == "Convert cancelled.")
    {
        return FooterNotificationTone::Hint;
    }

    if (status == "Invalid download path" || status == "Invalid convert path" || status.rfind("failed", 0) == 0 ||
        status.find("failed") != std::string::npos || status.find("Skipped") != std::string::npos ||
        status.find("No videos") != std::string::npos || status.find("Could not parse") != std::string::npos)
    {
        return FooterNotificationTone::Error;
    }

    return FooterNotificationTone::Success;
}

void GetFooterNotificationColors(FooterNotificationTone tone,
                                 bool closeHovered,
                                 Color& closeBackground,
                                 Color& textBackground,
                                 Color& border,
                                 Color& textColor)
{
    border = {188, 188, 188, 255};
    textColor = {244, 244, 244, 255};
    closeBackground = closeHovered ? Color{232, 72, 64, 255} : Color{96, 96, 96, 255};

    switch (tone)
    {
    case FooterNotificationTone::Error:
        textBackground = {72, 44, 42, 255};
        break;
    case FooterNotificationTone::Progress:
        textBackground = {38, 48, 58, 255};
        break;
    case FooterNotificationTone::Hint:
        textBackground = {142, 118, 54, 255};
        textColor = {248, 244, 228, 255};
        break;
    case FooterNotificationTone::Success:
        textBackground = {38, 54, 38, 255};
        break;
    }
}
} // namespace

bool DockArea::IsFooterErrorStatus(const std::string& status, bool isRunning)
{
    if (isRunning)
    {
        return false;
    }

    return status == "Invalid download path" || status == "Invalid convert path" || status.rfind("failed", 0) == 0 ||
           status.find("failed") != std::string::npos || status.find("Skipped") != std::string::npos ||
           status.find("No videos") != std::string::npos || status.find("Could not parse") != std::string::npos;
}

void DockArea::ClearFooterNotification()
{
    footerNotificationText_.clear();
    footerNotificationVisible_ = false;
    footerNotificationShowTime_ = -1.0;
    footerNotificationHideTime_ = -1.0;
    footerNotificationDismissed_ = false;
    footerNotificationScope_ = FooterNotificationScope::Any;
    footerClipboardLog_.clear();
    errorConsoleLog_.clear();
}

void DockArea::ShowFooterNotification(const std::string& text,
                                      FooterNotificationScope scope,
                                      const std::string& errorLog,
                                      const std::string& clipboardLog)
{
    footerNotificationText_ = text;
    footerNotificationScope_ = scope;
    footerNotificationVisible_ = false;
    footerNotificationShowTime_ = GetTime() + kFooterNotificationDelaySeconds;
    footerNotificationHideTime_ = -1.0;
    footerNotificationDismissed_ = false;
    footerClipboardLog_ = clipboardLog;

    if (errorLog.empty())
    {
        errorConsoleLog_ = text;
    }
    else if (errorLog == text || errorLog.rfind(text, 0) == 0)
    {
        errorConsoleLog_ = errorLog;
    }
    else
    {
        errorConsoleLog_ = text + "\n\n" + errorLog;
    }
}

void DockArea::UpdateFooterNotificationTimer()
{
    if (footerNotificationText_.empty())
    {
        return;
    }

    if (!footerNotificationVisible_ && footerNotificationShowTime_ > 0.0 && GetTime() >= footerNotificationShowTime_)
    {
        footerNotificationVisible_ = true;
        footerNotificationShowTime_ = -1.0;
        footerNotificationHideTime_ = GetTime() + kFooterNotificationAutoHideSeconds;
    }

    if (footerNotificationVisible_ && footerNotificationHideTime_ > 0.0 && GetTime() >= footerNotificationHideTime_)
    {
        ClearFooterNotification();
    }
}

std::string DockArea::BuildDownloadFooterErrorLog(const DownloadRunner& runner, const std::string& summary) const
{
    std::string details = runner.LastErrorLog();
    if (details.empty())
    {
        details = summary;
    }

    if (!runner.LastDownloadBrowserReport().empty())
    {
        details += "\n\n";
        details += runner.LastDownloadBrowserReport();
    }

    const LinkCardNode* card = FindLinkCardByUrl(runner.CurrentUrl());
    if (card != nullptr && !card->ParseBrowserReport().empty())
    {
        details += "\n\n";
        details += card->ParseBrowserReport();
    }

    return details;
}

std::string DockArea::BuildConvertFooterErrorLog(const ConvertRunner& runner, const std::string& summary) const
{
    std::string details = runner.LastErrorLog();
    if (details.empty())
    {
        details = summary;
    }
    return details;
}

void DockArea::AppendFooterDiagnosticsForCard(const std::string& url, const std::string& downloadReport)
{
    const LinkCardNode* card = FindLinkCardByUrl(url);
    const std::string title = card != nullptr ? card->Title() : "";
    const std::string parseReport = card != nullptr ? card->ParseBrowserReport() : "";
    const std::string report = FormatBrowserSessionReport(url, title, parseReport, downloadReport);
    if (!footerClipboardLog_.empty())
    {
        footerClipboardLog_ += "\n========================\n";
    }
    footerClipboardLog_ += report;
}

void DockArea::CollectParseFailures()
{
    ForEachLinkCard(
        [&](LinkCardNode& card)
        {
            std::string url;
            std::string error;
            if (card.TryConsumeParseFailure(url, error))
            {
                ShowFooterNotification("Could not parse link",
                                       FooterNotificationScope::Downloader,
                                       "Could not parse link: " + url + "\n\n" + error);
                return;
            }

            if (card.TryConsumeParseSuccess(url))
            {
                ShowFooterNotification("Link parsed successfully", FooterNotificationScope::Downloader);
            }
        });

    for (DownloaderListItem& item : cards_)
    {
        if (item.kind != DownloaderListItem::Kind::Group || item.group == nullptr)
        {
            continue;
        }
        std::string url;
        std::string error;
        if (item.group->TryConsumeParseFailure(url, error))
        {
            ShowFooterNotification("Could not parse link",
                                   FooterNotificationScope::Downloader,
                                   "Could not parse link: " + url + "\n\n" + error);
            continue;
        }
        if (item.group->TryConsumeParseSuccess(url))
        {
            ShowFooterNotification("Link parsed successfully", FooterNotificationScope::Downloader);
        }
    }
}

bool DockArea::BuildFooterNotification(std::string& status, bool& useConvertStatus, bool& isRunning) const
{
    if (AnyDownloadRunning() || AnyConvertRunning())
    {
        return false;
    }

    if (footerNotificationText_.empty() || !footerNotificationVisible_ || footerNotificationDismissed_)
    {
        return false;
    }

    if (footerNotificationScope_ == FooterNotificationScope::Downloader && activeWorkspace_ != Workspace::Downloader)
    {
        return false;
    }

    if (footerNotificationScope_ == FooterNotificationScope::Converter && activeWorkspace_ != Workspace::Converter)
    {
        return false;
    }

    status = footerNotificationText_;
    useConvertStatus = false;
    isRunning = false;
    return true;
}

void DockArea::UpdateFooter()
{
    const int windowWidth = GetScreenWidth();
    const int windowHeight = GetScreenHeight();
    const Rectangle footer = GetFooter(windowWidth, windowHeight);
    if (seedTestLinksButton_.Update(GetFooterSeedButtonBounds(footer)))
    {
        HandleSeedTestLinksRequest();
        return;
    }
    if (seed8kLinkButton_.Update(GetFooterSeed8kButtonBounds(footer)))
    {
        HandleSeed8kLinkRequest();
        return;
    }

    if (footerNotificationDismissed_ || !footerNotificationShown_)
    {
        return;
    }

    std::string status;
    bool useConvertStatus = false;
    bool isRunning = false;
    if (!BuildFooterNotification(status, useConvertStatus, isRunning))
    {
        return;
    }

    if (CheckCollisionPointRec(GetMousePosition(), footerCloseButtonBounds_) &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        ClearFooterNotification();
        return;
    }

    if (footerCopyVisible_ && CheckCollisionPointRec(GetMousePosition(), footerCopyButtonBounds_) &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        const std::string& text =
            !footerClipboardLog_.empty() ? footerClipboardLog_ : (errorConsoleLog_.empty() ? status : errorConsoleLog_);
        if (!text.empty())
        {
            SetClipboardText(text.c_str());
        }
    }
}

void DockArea::DrawFooterCloseIcon(Rectangle bounds, bool hovered) const
{
    const Color crossColor = hovered ? Color{255, 244, 242, 255} : Color{244, 244, 244, 255};
    const float padding = bounds.width * 0.28f;
    const float thickness = hovered ? 2.0f : 1.5f;
    DrawLineEx({bounds.x + padding, bounds.y + padding},
               {bounds.x + bounds.width - padding, bounds.y + bounds.height - padding},
               thickness,
               crossColor);
    DrawLineEx({bounds.x + bounds.width - padding, bounds.y + padding},
               {bounds.x + padding, bounds.y + bounds.height - padding},
               thickness,
               crossColor);
}

void DockArea::DrawFooterCopyIcon(Rectangle bounds, bool hovered) const
{
    const Color iconColor = hovered ? Color{255, 244, 242, 255} : Color{244, 244, 244, 255};
    const float pad = bounds.width * 0.24f;
    const float sheetWidth = bounds.width - pad * 2.0f - 3.0f;
    const float sheetHeight = bounds.height - pad * 2.0f - 3.0f;
    const Rectangle backSheet = {bounds.x + pad + 3.0f, bounds.y + pad + 3.0f, sheetWidth, sheetHeight};
    const Rectangle frontSheet = {bounds.x + pad, bounds.y + pad, sheetWidth, sheetHeight};
    const float roundness = 0.18f;

    DrawRectangleRounded(backSheet, roundness, 6, Color{iconColor.r, iconColor.g, iconColor.b, 90});
    DrawRectangleRounded(frontSheet, roundness, 6, Color{iconColor.r, iconColor.g, iconColor.b, 40});
    DrawRectangleRoundedLines(frontSheet, roundness, 6, iconColor);
    DrawLineEx({frontSheet.x + frontSheet.width * 0.28f, frontSheet.y + frontSheet.height * 0.62f},
               {frontSheet.x + frontSheet.width * 0.72f, frontSheet.y + frontSheet.height * 0.62f},
               1.5f,
               iconColor);
    DrawLineEx({frontSheet.x + frontSheet.width * 0.28f, frontSheet.y + frontSheet.height * 0.78f},
               {frontSheet.x + frontSheet.width * 0.72f, frontSheet.y + frontSheet.height * 0.78f},
               1.5f,
               iconColor);
}

void DockArea::DrawFooter(Rectangle footer, Font font, Font fontFooterAa) const
{
    DrawRectangleRec(footer, Color{24, 24, 24, 255});
    seedTestLinksButton_.Draw(GetFooterSeedButtonBounds(footer), font);
    seed8kLinkButton_.Draw(GetFooterSeed8kButtonBounds(footer), font);
    Tooltip::DrawIfHovered(font, GetFooterSeedButtonBounds(footer), "Paste 10 test links");
    Tooltip::DrawIfHovered(font, GetFooterSeed8kButtonBounds(footer), "Paste 8K video");

    {
        fpsFrameCounter_ += 1;
        fpsElapsedSeconds_ += GetFrameTime();
        if (fpsElapsedSeconds_ >= 0.25f)
        {
            displayFps_ = static_cast<int>(static_cast<float>(fpsFrameCounter_) / fpsElapsedSeconds_ + 0.5f);
            fpsFrameCounter_ = 0;
            fpsElapsedSeconds_ = 0.0f;
        }

        char meta[64];
        std::snprintf(meta, sizeof(meta), "%d FPS | %s", displayFps_, FOURKDOWNER_VERSION);
        const float fontSize = 12.0f;
        const Color metaColor = {168, 174, 168, 255};
        const bool hasAa = fontFooterAa.glyphCount > 0 && fontFooterAa.texture.id != 0;
        const Font& metaFont = hasAa ? fontFooterAa : font;
        const Vector2 metaSize = MeasureTextEx(metaFont, meta, fontSize, 0.0f);
        DrawTextEx(metaFont,
                   meta,
                   {footer.x + footer.width - metaSize.x - 10.0f, footer.y + (footer.height - metaSize.y) * 0.5f},
                   fontSize,
                   0.0f,
                   metaColor);
    }

    footerNotificationShown_ = false;

    std::string status;
    bool useConvertStatus = false;
    bool isRunning = false;
    if (!BuildFooterNotification(status, useConvertStatus, isRunning) || footerNotificationDismissed_)
    {
        return;
    }

    const float barHeight = footer.height - kFooterNotificationMargin * 2.0f;
    const float barY = footer.y + kFooterNotificationMargin;
    const float fontSize = 15.0f;
    const float textPaddingX = 10.0f;
    const FooterNotificationTone tone = GetFooterNotificationTone(status, isRunning);
    const bool showCopyButton = tone == FooterNotificationTone::Error ||
                                (tone == FooterNotificationTone::Success && !footerClipboardLog_.empty());
    const float maxPillWidth = footer.width * 0.5f;
    const float chromeWidth = barHeight + (showCopyButton ? barHeight : 0.0f);
    const float maxTextAreaWidth = std::max(48.0f, maxPillWidth - chromeWidth);
    const std::string displayStatus =
        TruncateTextToWidth(font, status, fontSize, maxTextAreaWidth - textPaddingX * 2.0f);
    const Vector2 labelSize = MeasureTextEx(font, displayStatus.c_str(), fontSize, 0.0f);
    const float textAreaWidth = std::min(maxTextAreaWidth, labelSize.x + textPaddingX * 2.0f);
    const float totalWidth = chromeWidth + textAreaWidth;
    const float startX = footer.x + (footer.width - totalWidth) * 0.5f;
    const float roundness = 4.0f / barHeight;

    const Rectangle closeButton = {startX, barY, barHeight, barHeight};
    const Rectangle textArea = {startX + barHeight, barY, textAreaWidth, barHeight};
    const Rectangle copyButton = {startX + barHeight + textAreaWidth, barY, barHeight, barHeight};
    const bool closeHovered = CheckCollisionPointRec(GetMousePosition(), closeButton);
    const bool copyHovered = showCopyButton && CheckCollisionPointRec(GetMousePosition(), copyButton);

    Color closeBackground{};
    Color textBackground{};
    Color border{};
    Color textColor{};
    GetFooterNotificationColors(tone, closeHovered, closeBackground, textBackground, border, textColor);

    Color copyBackground = Color{142, 118, 54, 255};
    if (tone == FooterNotificationTone::Success && !footerClipboardLog_.empty())
    {
        copyBackground = Color{84, 124, 84, 255};
    }
    if (copyHovered)
    {
        copyBackground = tone == FooterNotificationTone::Success && !footerClipboardLog_.empty()
                             ? Color{104, 148, 104, 255}
                             : Color{162, 136, 66, 255};
        UiCursor::RequestHand();
    }
    if (closeHovered)
    {
        UiCursor::RequestHand();
    }

    const Rectangle combined = {startX, barY, totalWidth, barHeight};
    DrawRectangleRounded(combined, roundness, 10, textBackground);
    UiClip::Push(closeButton);
    DrawRectangleRounded(combined, roundness, 10, closeBackground);
    UiClip::Pop();
    DrawFooterCloseIcon(closeButton, closeHovered);

    if (showCopyButton)
    {
        UiClip::Push(copyButton);
        DrawRectangleRounded(combined, roundness, 10, copyBackground);
        UiClip::Pop();
        DrawFooterCopyIcon(copyButton, copyHovered);
    }

    DrawRectangleRoundedLines(combined, roundness, 10, border);
    DrawLineEx({startX + barHeight, barY + 2.0f}, {startX + barHeight, barY + barHeight - 2.0f}, 1.0f, border);

    if (showCopyButton)
    {
        DrawLineEx({copyButton.x, barY + 2.0f}, {copyButton.x, barY + barHeight - 2.0f}, 1.0f, border);
    }

    if (isRunning)
    {
        const float progress = useConvertStatus ? 0.35f : 1.0f;
        const Rectangle progressFill = {
            textArea.x + 3.0f, textArea.y + textArea.height - 4.0f, (textArea.width - 6.0f) * progress, 2.0f};
        DrawRectangleRounded(progressFill, 1.0f, 4, Color{120, 156, 120, 255});
    }

    const float textY = barY + (barHeight - labelSize.y) * 0.5f;
    UiClip::Push({textArea.x + textPaddingX, textArea.y, textArea.width - textPaddingX * 2.0f, textArea.height});
    DrawTextEx(font, displayStatus.c_str(), {textArea.x + textPaddingX, textY}, fontSize, 0.0f, textColor);
    UiClip::Pop();

    footerCloseButtonBounds_ = closeButton;
    footerCopyButtonBounds_ = copyButton;
    footerCopyVisible_ = showCopyButton;
    footerNotificationShown_ = true;
}

void DockArea::DrawOverwritePrompt(int windowWidth, int windowHeight, Font font) const
{
    if (!isOverwritePromptOpen_)
    {
        return;
    }

    DrawRectangle(0, 0, windowWidth, windowHeight, Color{0, 0, 0, 120});

    const float modalWidth = 420.0f;
    const float modalHeight = 158.0f;
    const Rectangle modal = {(static_cast<float>(windowWidth) - modalWidth) * 0.5f,
                             (static_cast<float>(windowHeight) - modalHeight) * 0.5f,
                             modalWidth,
                             modalHeight};
    const Rectangle replaceBounds = {modal.x + modal.width - 318.0f, modal.y + modal.height - 48.0f, 96.0f, 34.0f};
    const Rectangle cancelBounds = {modal.x + modal.width - 212.0f, modal.y + modal.height - 48.0f, 84.0f, 34.0f};
    const Rectangle cancelAllBounds = {modal.x + modal.width - 118.0f, modal.y + modal.height - 48.0f, 100.0f, 34.0f};

    DrawRectangleRounded(modal, 0.08f, 14, Color{24, 32, 24, 255});
    DrawRectangleRoundedLines(modal, 0.08f, 14, Color{96, 126, 96, 255});
    DrawTextEx(font, "File already exists", {modal.x + 18.0f, modal.y + 16.0f}, 18.0f, 0.0f, Color{232, 238, 232, 255});
    DrawTextEx(font, "Replace this file?", {modal.x + 18.0f, modal.y + 45.0f}, 15.0f, 0.0f, Color{178, 192, 178, 255});

    const float nameMaxWidth = modal.width - 36.0f;
    DrawWrappedText(font,
                    pendingOverwriteFileName_,
                    {modal.x + 18.0f, modal.y + 68.0f},
                    14.0f,
                    nameMaxWidth,
                    2,
                    Color{218, 226, 218, 255});

    replaceFileButton_.Draw(replaceBounds, font, true, overwritePromptFocusIndex_ == 0);
    cancelReplaceButton_.DrawDanger(cancelBounds, font, overwritePromptFocusIndex_ == 1);
    cancelAllReplaceButton_.DrawDanger(cancelAllBounds, font, overwritePromptFocusIndex_ == 2);
}

void DockArea::UpdateAboutDialog(int windowWidth, int windowHeight, Font font)
{
    const AboutDialogMetrics metrics = AboutDialogMetrics::FromWindow(windowWidth, windowHeight);

    if (closeAboutButton_.Update(metrics.okButton) || IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) ||
        IsKeyPressed(KEY_SPACE))
    {
        isAboutDialogOpen_ = false;
        return;
    }

    if (!IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        return;
    }

    const Vector2 mouse = GetMousePosition();
    if (!CheckCollisionPointRec(mouse, metrics.modal))
    {
        isAboutDialogOpen_ = false;
        return;
    }

    static constexpr std::array<AboutDialogLink, 4> kLinks = {{
        {"https://github.com/epsill0n/ytdown", "https://github.com/epsill0n/ytdown", 142.0f},
        {"https://ffmpeg.org", "https://ffmpeg.org", 192.0f},
        {"https://www.raylib.com", "https://www.raylib.com", 242.0f},
        {"http://tinyfiledialogs.sourceforge.net", "http://tinyfiledialogs.sourceforge.net", 292.0f},
    }};

    for (const AboutDialogLink& link : kLinks)
    {
        if (CheckCollisionPointRec(mouse, metrics.LinkBounds(font, link.label, link.yOffset)))
        {
            OpenUrlInBrowser(link.url);
            break;
        }
    }
}

void DockArea::DrawAboutDialog(int windowWidth, int windowHeight, Font font) const
{
    if (!isAboutDialogOpen_)
    {
        return;
    }

    DrawRectangle(0, 0, windowWidth, windowHeight, Color{0, 0, 0, 120});

    const AboutDialogMetrics metrics = AboutDialogMetrics::FromWindow(windowWidth, windowHeight);
    const Rectangle& modal = metrics.modal;
    const Rectangle& okBounds = metrics.okButton;
    const Color titleColor = {232, 238, 232, 255};
    const Color bodyColor = {196, 208, 196, 255};
    const Color mutedColor = {150, 168, 150, 255};
    const Color linkColor = {120, 188, 120, 255};
    const Color linkHoverColor = {164, 228, 164, 255};
    const float textX = modal.x + 18.0f;
    const float itemX = metrics.itemX;

    DrawRectangleRounded(modal, 0.08f, 14, Color{24, 32, 24, 255});
    DrawRectangleRoundedLines(modal, 0.08f, 14, Color{96, 126, 96, 255});
    DrawTextEx(font, "About 4KDowner", {textX, modal.y + 16.0f}, 22.0f, 0.0f, titleColor);
    DrawTextEx(font, "Version " FOURKDOWNER_VERSION, {textX, modal.y + 44.0f}, 16.0f, 0.0f, mutedColor);
    DrawTextEx(font, "by NickStan", {textX, modal.y + 64.0f}, 15.0f, 0.0f, mutedColor);
    DrawTextEx(font, "Special thanks to:", {textX, modal.y + 92.0f}, 18.0f, 0.0f, bodyColor);

    DrawTextEx(font, "epsill0n, creator of ytdown", {itemX, modal.y + 120.0f}, 16.0f, 0.0f, bodyColor);
    DrawAboutLink(font, "https://github.com/epsill0n/ytdown", modal.y + 142.0f, itemX, linkColor, linkHoverColor);

    DrawTextEx(font, "Fabrice Bellard, creator of FFmpeg", {itemX, modal.y + 170.0f}, 16.0f, 0.0f, bodyColor);
    DrawAboutLink(font, "https://ffmpeg.org", modal.y + 192.0f, itemX, linkColor, linkHoverColor);

    DrawTextEx(font, "the raylib project", {itemX, modal.y + 220.0f}, 16.0f, 0.0f, bodyColor);
    DrawAboutLink(font, "https://www.raylib.com", modal.y + 242.0f, itemX, linkColor, linkHoverColor);

    DrawTextEx(font, "the tinyfiledialogs project", {itemX, modal.y + 270.0f}, 16.0f, 0.0f, bodyColor);
    DrawAboutLink(font, "http://tinyfiledialogs.sourceforge.net", modal.y + 292.0f, itemX, linkColor, linkHoverColor);

    closeAboutButton_.Draw(okBounds, font);
}

namespace
{
constexpr float kInfoModalWidth = 520.0f;
constexpr float kInfoModalHeight = 480.0f;
constexpr float kInfoTitleBlock = 48.0f;
constexpr float kInfoFooterBlock = 56.0f;
constexpr float kInfoSectionGap = 10.0f;
constexpr float kInfoLineStep = 22.0f;
constexpr float kInfoSectionHeaderStep = 26.0f;
// Selecting(5) + Queue(4) + Shortcuts(17) + Handy(6) = 32 lines; 4 sections; 3 gaps between sections.
constexpr float kInfoSectionCount = 4.0f;
constexpr float kInfoLineCount = 32.0f;
constexpr float kInfoSectionGaps = 3.0f;

float InfoDialogContentHeight()
{
    return kInfoSectionHeaderStep * kInfoSectionCount + kInfoLineStep * kInfoLineCount +
           kInfoSectionGap * kInfoSectionGaps + 8.0f;
}
} // namespace

void DockArea::UpdateInfoDialog(int windowWidth, int windowHeight, Font font)
{
    (void)font;
    const Rectangle modal = {(static_cast<float>(windowWidth) - kInfoModalWidth) * 0.5f,
                             (static_cast<float>(windowHeight) - kInfoModalHeight) * 0.5f,
                             kInfoModalWidth,
                             kInfoModalHeight};
    const Rectangle okButton = {modal.x + modal.width - 118.0f, modal.y + modal.height - 48.0f, 84.0f, 34.0f};
    const Rectangle contentViewport = {modal.x + 12.0f,
                                       modal.y + kInfoTitleBlock,
                                       modal.width - 24.0f,
                                       modal.height - kInfoTitleBlock - kInfoFooterBlock};

    const float maxScroll = std::max(0.0f, InfoDialogContentHeight() - contentViewport.height);

    if (closeInfoButton_.Update(okButton) || IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) ||
        IsKeyPressed(KEY_SPACE))
    {
        isInfoDialogOpen_ = false;
        infoDialogScrollOffset_ = 0.0f;
        return;
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(GetMousePosition(), modal))
    {
        isInfoDialogOpen_ = false;
        infoDialogScrollOffset_ = 0.0f;
        return;
    }

    if (CheckCollisionPointRec(GetMousePosition(), modal))
    {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            infoDialogScrollOffset_ = std::clamp(infoDialogScrollOffset_ - wheel * 40.0f, 0.0f, maxScroll);
        }
    }
    infoDialogScrollOffset_ = std::clamp(infoDialogScrollOffset_, 0.0f, maxScroll);
}

void DockArea::DrawInfoDialog(int windowWidth, int windowHeight, Font font) const
{
    if (!isInfoDialogOpen_)
    {
        return;
    }

    DrawRectangle(0, 0, windowWidth, windowHeight, Color{0, 0, 0, 120});

    const Rectangle modal = {(static_cast<float>(windowWidth) - kInfoModalWidth) * 0.5f,
                             (static_cast<float>(windowHeight) - kInfoModalHeight) * 0.5f,
                             kInfoModalWidth,
                             kInfoModalHeight};
    const Rectangle okBounds = {modal.x + modal.width - 118.0f, modal.y + modal.height - 48.0f, 84.0f, 34.0f};
    const Rectangle contentViewport = {modal.x + 12.0f,
                                       modal.y + kInfoTitleBlock,
                                       modal.width - 24.0f,
                                       modal.height - kInfoTitleBlock - kInfoFooterBlock};

    const float maxScroll = std::max(0.0f, InfoDialogContentHeight() - contentViewport.height);
    const float scroll = std::clamp(infoDialogScrollOffset_, 0.0f, maxScroll);

    const Color titleColor = {232, 238, 232, 255};
    const Color sectionColor = {220, 232, 220, 255};
    const Color bodyColor = {186, 200, 186, 255};
    const float textX = modal.x + 20.0f;

    DrawRectangleRounded(modal, 0.08f, 14, Color{24, 32, 24, 255});
    DrawRectangleRoundedLines(modal, 0.08f, 14, Color{96, 126, 96, 255});
    DrawTextEx(font, "Info", {textX, modal.y + 14.0f}, 24.0f, 0.0f, titleColor);

    UiClip::Push(contentViewport);
    float y = contentViewport.y - scroll;

    const auto drawSection = [&](const char* title)
    {
        DrawTextEx(font, title, {textX, y}, 18.0f, 0.0f, sectionColor);
        y += kInfoSectionHeaderStep;
    };
    const auto drawLine = [&](const char* line)
    {
        DrawTextEx(font, line, {textX, y}, 16.0f, 0.0f, bodyColor);
        y += kInfoLineStep;
    };

    drawSection("Selecting cards");
    drawLine("Click a card to select it");
    drawLine("Ctrl+click to select several");
    drawLine("Shift+click to grab a whole range");
    drawLine("Ctrl+A - select every card");
    drawLine("Playlist/channel cards expand with chevron or A on the header");
    y += kInfoSectionGap;

    drawSection("Queue");
    drawLine("Hover \"in queue\", then click Prioritize");
    drawLine("That download jumps ahead right away");
    drawLine("It may pause whichever one is least finished");
    drawLine("To drop something from the queue, use Cancel");
    y += kInfoSectionGap;

    drawSection("Shortcuts");
    drawLine("1 / 2 - switch Downloader / Converter");
    drawLine("Up / Down - move selection");
    drawLine("Tab / Shift+Tab - move selection");
    drawLine("Shift+Up / Down - select a range");
    drawLine("Ctrl+Up / Down - add or remove from selection");
    drawLine("Ctrl+V - paste a link / choose files");
    drawLine("Ctrl+Shift+V - paste the same link again");
    drawLine("Enter / Space - start or stop the selected item(s)");
    drawLine("Ctrl+Enter - download/convert all");
    drawLine("Alt+Enter / Cancel All - cancel every download/convert and clear the queue");
    drawLine("Esc - clear selection");
    drawLine("Delete - stop and remove the selected card(s)");
    drawLine("Ctrl+Delete / Ctrl+X - stop and remove every card");
    drawLine("X - close the card under the mouse");
    drawLine("A - fold or unfold the section title under the mouse");
    drawLine("Ctrl+Z / Ctrl+Y - undo / redo (including all / cancel all)");
    drawLine("Ctrl+Q - quit");
    y += kInfoSectionGap;

    drawSection("Handy stuff");
    drawLine("Click the thumbnail to open the output folder");
    drawLine("Exclude selected skips convert (gold border); Custom uses a blue border");
    drawLine("Auto Convert holds Global Convert, Exclude, and Custom Convert");
    drawLine("Custom Convert: per-card override for the selected video(s)");
    drawLine("Large playlists show Load more (+50) at the bottom when expanded");
    drawLine("Download All materializes every playlist entry before queuing");

    UiClip::Pop();
    DrawOptionsScrollbar(contentViewport, scroll, maxScroll);
    closeInfoButton_.Draw(okBounds, font);
}

LinkCardNode* DockArea::GetSelectedCard()
{
    for (DownloaderListItem& item : cards_)
    {
        if (item.kind == DownloaderListItem::Kind::Single && item.single->IsSelected())
        {
            return item.single.get();
        }
        if (item.kind == DownloaderListItem::Kind::Group)
        {
            for (LinkCardNode& child : item.group->LoadedCards())
            {
                if (child.IsSelected())
                {
                    return &child;
                }
            }
        }
    }

    return nullptr;
}

const LinkCardNode* DockArea::GetSelectedCard() const
{
    for (const DownloaderListItem& item : cards_)
    {
        if (item.kind == DownloaderListItem::Kind::Single && item.single->IsSelected())
        {
            return item.single.get();
        }
        if (item.kind == DownloaderListItem::Kind::Group)
        {
            for (const LinkCardNode& child : item.group->LoadedCards())
            {
                if (child.IsSelected())
                {
                    return &child;
                }
            }
        }
    }

    return nullptr;
}

LinkCardGroupNode* DockArea::GetSelectedGroupHeader()
{
    for (DownloaderListItem& item : cards_)
    {
        if (item.kind == DownloaderListItem::Kind::Group && item.group != nullptr && item.group->IsHeaderSelected())
        {
            return item.group.get();
        }
    }
    return nullptr;
}

const LinkCardGroupNode* DockArea::GetSelectedGroupHeader() const
{
    for (const DownloaderListItem& item : cards_)
    {
        if (item.kind == DownloaderListItem::Kind::Group && item.group != nullptr && item.group->IsHeaderSelected())
        {
            return item.group.get();
        }
    }
    return nullptr;
}

ConverterFileCardNode* DockArea::GetSelectedConverterCard()
{
    for (ConverterFileCardNode& card : converterCards_)
    {
        if (card.IsSelected())
        {
            return &card;
        }
    }

    return nullptr;
}

const ConverterFileCardNode* DockArea::GetSelectedConverterCard() const
{
    for (const ConverterFileCardNode& card : converterCards_)
    {
        if (card.IsSelected())
        {
            return &card;
        }
    }

    return nullptr;
}

bool DockArea::AnyDownloadRunning() const
{
    for (const DownloadRunner& runner : downloadRunners_)
    {
        if (runner.IsRunning())
        {
            return true;
        }
    }
    return false;
}

bool DockArea::AnyConvertRunning() const
{
    for (const ConvertRunner& runner : convertRunners_)
    {
        if (runner.IsRunning())
        {
            return true;
        }
    }
    return false;
}

int DockArea::RunningDownloadCount() const
{
    int count = 0;
    for (const DownloadRunner& runner : downloadRunners_)
    {
        if (runner.IsRunning())
        {
            ++count;
        }
    }
    return count;
}

int DockArea::RunningConvertCount() const
{
    int count = 0;
    for (const ConvertRunner& runner : convertRunners_)
    {
        if (runner.IsRunning())
        {
            ++count;
        }
    }
    return count;
}

DownloadRunner* DockArea::FirstFreeDownloadRunner()
{
    for (DownloadRunner& runner : downloadRunners_)
    {
        if (!runner.IsRunning())
        {
            return &runner;
        }
    }
    return nullptr;
}

ConvertRunner* DockArea::FirstFreeConvertRunner()
{
    for (ConvertRunner& runner : convertRunners_)
    {
        if (!runner.IsRunning())
        {
            return &runner;
        }
    }
    return nullptr;
}

DownloadRunner* DockArea::FindDownloadRunnerByUrl(const std::string& url)
{
    for (DownloadRunner& runner : downloadRunners_)
    {
        if (runner.IsRunning() && runner.CurrentUrl() == url)
        {
            return &runner;
        }
    }
    return nullptr;
}

ConvertRunner* DockArea::FindConvertRunnerByPath(const std::string& inputPath)
{
    for (ConvertRunner& runner : convertRunners_)
    {
        if (runner.IsRunning() && runner.CurrentInputPath() == inputPath)
        {
            return &runner;
        }
    }
    return nullptr;
}

void DockArea::CancelAllDownloads()
{
    for (DownloadRunner& runner : downloadRunners_)
    {
        if (runner.IsRunning())
        {
            runner.Cancel();
        }
    }
}

void DockArea::CancelAllConverts()
{
    for (ConvertRunner& runner : convertRunners_)
    {
        if (runner.IsRunning())
        {
            runner.Cancel();
        }
    }
}

void DockArea::HandleCancelAllConvertsRequest()
{
    const bool hadWork = AnyConvertRunning() || !pendingConvertQueue_.empty() || isBatchConverting_;
    pendingConvertQueue_.clear();
    isBatchConverting_ = false;
    batchConvertElapsedTotal_ = 0.0;
    batchIncludesDownloadConvert_ = false;
    overwriteAllExisting_ = false;

    for (ConverterFileCardNode& card : converterCards_)
    {
        if (card.IsConverting())
        {
            card.ClearConverting();
        }
    }

    CancelAllConverts();

    if (hadWork)
    {
        ShowFooterNotification("All converts cancelled.", FooterNotificationScope::Converter);
        PushUndo(MakeCancelAllConvertsCommand());
    }
}

bool DockArea::IsConverterCardQueued(const std::string& inputPath) const
{
    for (const ConvertRequest& request : pendingConvertQueue_)
    {
        if (request.inputPath == inputPath)
        {
            return true;
        }
    }
    return false;
}

bool DockArea::HasActiveDownloadWorkspaceWork() const
{
    return AnyDownloadRunning() || !pendingDownloadQueue_.empty() || isBatchDownloading_;
}

bool DockArea::HasActiveConverterWorkspaceWork() const
{
    for (const ConverterFileCardNode& card : converterCards_)
    {
        if (!card.HasFile())
        {
            continue;
        }
        if (card.IsConverting() || IsConverterCardQueued(card.Info().filePath))
        {
            return true;
        }
    }
    return false;
}

void DockArea::SyncConverterBusyStateAfterCardChanges()
{
    if (HasActiveConverterWorkspaceWork() || AnyLinkCardConverting())
    {
        return;
    }

    // Drop batch UI state once no visible convert work remains. Orphaned runners may
    // still be winding down after Cancel(); they must not keep the Cancel button up.
    if (pendingConvertQueue_.empty())
    {
        isBatchConverting_ = false;
        overwriteAllExisting_ = false;
    }
}

bool DockArea::SelectedConverterShowsCancel() const
{
    for (const ConverterFileCardNode& card : converterCards_)
    {
        if (!card.IsSelected() || !card.HasFile())
        {
            continue;
        }
        if (card.IsConverting() || IsConverterCardQueued(card.Info().filePath))
        {
            return true;
        }
    }
    return false;
}

void DockArea::HandleCancelSelectedConvertsRequest()
{
    std::vector<std::string> paths;
    for (const ConverterFileCardNode& card : converterCards_)
    {
        if (!card.IsSelected() || !card.HasFile())
        {
            continue;
        }
        const std::string& path = card.Info().filePath;
        if (card.IsConverting() || IsConverterCardQueued(path))
        {
            paths.push_back(path);
        }
    }

    if (paths.empty())
    {
        return;
    }

    for (const std::string& path : paths)
    {
        CancelConverterCard(path);
    }

    ShowFooterNotification("Convert cancelled.", FooterNotificationScope::Converter);
}

void DockArea::ProcessFinishedDownloadRunner(DownloadRunner& runner)
{
    std::string completedDownloadUrl;
    double completedDownloadElapsed = 0.0;
    if (runner.ConsumeCompletedDownload(completedDownloadUrl, completedDownloadElapsed))
    {
        WriteDebugLog("download completed: " + completedDownloadUrl);
        LinkCardNode* completedCard = nullptr;
        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                if (card.HasUrl(completedDownloadUrl))
                {
                    card.SetDownloadElapsed(completedDownloadElapsed);
                    card.SetDownloadBrowserReport(runner.LastDownloadBrowserReport());

                    const std::filesystem::path outputDir = std::filesystem::u8path(card.ExpectedOutputDirectory());
                    const std::string expectedTitle = card.ExpectedNormalizedTitle().empty()
                                                          ? card.NormalizedTitle()
                                                          : card.ExpectedNormalizedTitle();
                    std::filesystem::path found =
                        FindExistingOutputFile(outputDir, expectedTitle, card.ExpectedFileFormat());
                    if (found.empty())
                    {
                        // yt-dlp may write a different container than the UI format label.
                        found = FindExistingOutputFile(outputDir, expectedTitle, {});
                    }
                    if (!found.empty())
                    {
                        const std::string foundPath = PathUtf8(found);
                        card.SetLastDownloadedPath(foundPath);
                        if (card.HasAutoConvertDelivery())
                        {
                            card.SetAutoConvertStagingPath(foundPath);
                        }
                    }
                    else if (card.HasAutoConvertDelivery())
                    {
                        WriteDebugLog("auto-convert skipped: staging file not found for " + completedDownloadUrl);
                        card.ClearAutoConvertSnapshot();
                        card.ClearAutoConvertDelivery();
                    }
                    completedCard = &card;
                }
            });
        AppendFooterDiagnosticsForCard(completedDownloadUrl, runner.LastDownloadBrowserReport());

        if (completedCard != nullptr)
        {
            QueueAutoConvertForCard(*completedCard);
        }

        if (pendingDownloadQueue_.empty() && !AnyDownloadRunning())
        {
            isBatchDownloading_ = false;
            ClearBatchQueueStates();
            if (!batchIncludesDownloadConvert_)
            {
                overwriteAllExisting_ = false;
                const double totalElapsed = this->SumCompletedCardDownloadElapsed();
                ShowFooterNotification(FormatDownloadFinishedStatus(totalElapsed, true),
                                       FooterNotificationScope::Downloader,
                                       "",
                                       footerClipboardLog_);
            }
            else
            {
                MaybeShowDownloadConvertBatchFinished();
            }
            runner.SetStatus("");
        }
        else
        {
            nextDownloadStartTime_ = GetTime();
            runner.SetStatus("");
            WriteDebugLog("next download scheduled");
        }
        return;
    }

    if (runner.IsRunning())
    {
        return;
    }

    if (runner.Status() == "Download cancelled.")
    {
        const std::string cancelledUrl = runner.CurrentUrl();
        const bool softPreempt = !cancelledUrl.empty() && softPreemptRequeueUrls_.erase(cancelledUrl) > 0;

        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                if (!cancelledUrl.empty() && !card.HasUrl(cancelledUrl))
                {
                    return;
                }
                if (softPreempt)
                {
                    if (card.IsDownloading())
                    {
                        card.DemoteDownloadingToQueued();
                        card.ClearAutoConvertSnapshot();
                        card.ClearAutoConvertDelivery();
                    }
                    else if (!card.IsInQueue())
                    {
                        card.SetQueued();
                    }
                    return;
                }
                if (card.IsDownloading())
                {
                    card.ClearDownloading();
                    card.ClearAutoConvertSnapshot();
                    card.ClearAutoConvertDelivery();
                }
            });

        if (softPreempt)
        {
            nextDownloadStartTime_ = GetTime();
            runner.SetStatus("");
            StartNextPendingDownload();
            return;
        }

        const std::string status = runner.Status();
        if (isBatchDownloading_ && (!pendingDownloadQueue_.empty() || AnyDownloadRunning()))
        {
            nextDownloadStartTime_ = GetTime();
            runner.SetStatus("");
        }
        else if (!AnyDownloadRunning())
        {
            pendingDownloadQueue_.clear();
            isBatchDownloading_ = false;
            overwriteAllExisting_ = false;
            ClearBatchQueueStates();
            ShowFooterNotification(status, FooterNotificationScope::Downloader);
            runner.SetStatus("");
        }
        else
        {
            runner.SetStatus("");
        }
        return;
    }

    if (runner.Status().rfind("Download failed", 0) == 0)
    {
        const std::string failedUrl = runner.CurrentUrl();
        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                if (card.IsDownloading() && (failedUrl.empty() || card.HasUrl(failedUrl)))
                {
                    card.SetDownloadBrowserReport(runner.LastDownloadBrowserReport());
                    card.ClearDownloading();
                    card.ClearAutoConvertSnapshot();
                    card.ClearAutoConvertDelivery();
                }
            });
        const std::string status = runner.Status();
        if (isBatchDownloading_ && (!pendingDownloadQueue_.empty() || AnyDownloadRunning()))
        {
            nextDownloadStartTime_ = GetTime();
            runner.SetStatus("");
        }
        else if (!AnyDownloadRunning())
        {
            pendingDownloadQueue_.clear();
            isBatchDownloading_ = false;
            ClearBatchQueueStates();
            ShowFooterNotification(
                status, FooterNotificationScope::Downloader, BuildDownloadFooterErrorLog(runner, status));
            runner.SetStatus("");
        }
        else
        {
            ShowFooterNotification(
                status, FooterNotificationScope::Downloader, BuildDownloadFooterErrorLog(runner, status));
            runner.SetStatus("");
        }
    }
}

void DockArea::ProcessFinishedConvertRunner(ConvertRunner& runner)
{
    std::string completedConvertPath;
    std::string completedConvertOutputPath;
    std::string completedLinkCardUrl;
    double completedConvertElapsed = 0.0;
    bool completedDeleteInput = false;
    if (runner.ConsumeCompletedConvert(completedConvertPath,
                                       completedConvertOutputPath,
                                       completedConvertElapsed,
                                       completedLinkCardUrl,
                                       completedDeleteInput))
    {
        for (ConverterFileCardNode& card : converterCards_)
        {
            if (card.HasFilePath(completedConvertPath))
            {
                card.SetConvertElapsed(completedConvertElapsed);
                card.SetLastConvertedPath(completedConvertOutputPath);
                break;
            }
        }

        bool linkAutoConvertJob = false;
        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                const bool matchesUrl = !completedLinkCardUrl.empty() && card.HasUrl(completedLinkCardUrl);
                const bool matchesPath = card.HasDownloadedPath(completedConvertPath);
                if (!matchesUrl && !matchesPath)
                {
                    return;
                }

                linkAutoConvertJob =
                    card.HasAutoConvertDelivery() || completedDeleteInput || !completedLinkCardUrl.empty();
                card.SetConvertElapsed(completedConvertElapsed);
                if (!completedConvertOutputPath.empty())
                {
                    card.SetLastDownloadedPath(completedConvertOutputPath);
                }
                CleanupLinkCardAutoConvertStaging(card, completedConvertPath);
            });

        if (completedDeleteInput && !completedConvertPath.empty())
        {
            std::vector<std::string> leftovers =
                CleanupAutoConvertStagingArtifacts(std::filesystem::u8path(completedConvertPath));
            leftovers.push_back(completedConvertPath);
            ScheduleBackgroundFileDeletes(std::move(leftovers));
        }

        if (isBatchConverting_ && !linkAutoConvertJob)
        {
            batchConvertElapsedTotal_ += completedConvertElapsed;
        }

        if (batchIncludesDownloadConvert_ || linkAutoConvertJob)
        {
            MaybeShowDownloadConvertBatchFinished();
            runner.SetStatus("");
        }
        else if (isBatchConverting_ && pendingConvertQueue_.empty() && !AnyConvertRunning())
        {
            isBatchConverting_ = false;
            overwriteAllExisting_ = false;
            ShowFooterNotification(FormatConvertFinishedStatus(batchConvertElapsedTotal_, true),
                                   FooterNotificationScope::Converter,
                                   "",
                                   footerClipboardLog_);
            runner.SetStatus("");
        }
        else if (!pendingConvertQueue_.empty() || AnyConvertRunning())
        {
            runner.SetStatus("");
        }
        else if (!isBatchConverting_)
        {
            ShowFooterNotification(FormatConvertFinishedStatus(completedConvertElapsed, false),
                                   FooterNotificationScope::Converter);
            runner.SetStatus("");
        }
        return;
    }

    if (runner.IsRunning())
    {
        return;
    }

    if (runner.Status() == "Convert cancelled.")
    {
        const std::string cancelledPath = runner.CurrentInputPath();
        for (ConverterFileCardNode& card : converterCards_)
        {
            if (!cancelledPath.empty() && card.IsConverting() && card.HasFilePath(cancelledPath))
            {
                card.ClearConverting();
            }
        }
        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                if (!cancelledPath.empty() && card.IsConverting() && card.HasDownloadedPath(cancelledPath))
                {
                    card.ClearConverting();
                }
            });
        const std::string status = runner.Status();
        if (isBatchConverting_ && (!pendingConvertQueue_.empty() || AnyConvertRunning()))
        {
            runner.SetStatus("");
        }
        else if (!AnyConvertRunning())
        {
            pendingConvertQueue_.clear();
            isBatchConverting_ = false;
            overwriteAllExisting_ = false;
            ShowFooterNotification(status, FooterNotificationScope::Converter);
            runner.SetStatus("");
        }
        else
        {
            runner.SetStatus("");
        }
        return;
    }

    if (runner.Status().rfind("Convert failed", 0) == 0)
    {
        const std::string failedPath = runner.CurrentInputPath();
        const std::string status = runner.Status();
        const std::string errorLog = BuildConvertFooterErrorLog(runner, status);
        for (ConverterFileCardNode& card : converterCards_)
        {
            if (!failedPath.empty() && card.IsConverting() && card.HasFilePath(failedPath))
            {
                card.ClearConverting();
                card.SetLastError(errorLog);
            }
        }
        ForEachLinkCard(
            [&](LinkCardNode& card)
            {
                if (!card.IsConverting())
                {
                    return;
                }
                if (failedPath.empty() || card.HasDownloadedPath(failedPath) ||
                    card.AutoConvertStagingPath() == failedPath)
                {
                    card.ClearConverting();
                }
            });
        if (isBatchConverting_ && (!pendingConvertQueue_.empty() || AnyConvertRunning()))
        {
            runner.SetStatus("");
        }
        else if (!isBatchConverting_)
        {
            ShowFooterNotification(status, FooterNotificationScope::Converter, errorLog);
            runner.SetStatus("");
        }
        else if (!AnyConvertRunning())
        {
            pendingConvertQueue_.clear();
            isBatchConverting_ = false;
            ShowFooterNotification(status, FooterNotificationScope::Converter, errorLog);
            runner.SetStatus("");
        }
        else
        {
            ShowFooterNotification(status, FooterNotificationScope::Converter, errorLog);
            runner.SetStatus("");
        }
    }
}
void DockArea::CollectConverterLoadResults()
{
    for (ConverterFileCardNode& card : converterCards_)
    {
        if (card.TryConsumeLoadSuccess())
        {
            ShowFooterNotification("Video loaded successfully", FooterNotificationScope::Converter);
            continue;
        }

        std::string error;
        if (card.TryConsumeLoadFailure(error))
        {
            ShowFooterNotification("Could not load video",
                                   FooterNotificationScope::Converter,
                                   error.empty() ? "Could not load video." : error);
        }
    }
}

std::string DockArea::GetDefaultDownloadPath()
{
#ifdef _WIN32
    char* userProfile = nullptr;
    size_t userProfileSize = 0;
    if (_dupenv_s(&userProfile, &userProfileSize, "USERPROFILE") == 0 && userProfile != nullptr &&
        userProfile[0] != '\0')
    {
        const std::string path = PathUtf8(std::filesystem::path(userProfile) / "Videos" / "4kDowner");
        std::free(userProfile);
        return path;
    }
    std::free(userProfile);
#else
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0')
    {
        return PathUtf8(std::filesystem::path(home) / "Videos" / "4kDowner");
    }
#endif

    return "Videos/4kDowner";
}

std::string DockArea::GetAutoConvertStagingPath()
{
    std::filesystem::path documentsDir;
#ifdef _WIN32
    char* userProfile = nullptr;
    size_t userProfileSize = 0;
    if (_dupenv_s(&userProfile, &userProfileSize, "USERPROFILE") == 0 && userProfile != nullptr &&
        userProfile[0] != '\0')
    {
        documentsDir = std::filesystem::path(userProfile) / "Documents";
        std::free(userProfile);
    }
    else
    {
        std::free(userProfile);
    }
#else
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0')
    {
        documentsDir = std::filesystem::path(home) / "Documents";
    }
#endif

    if (documentsDir.empty())
    {
        documentsDir = std::filesystem::path("Documents");
    }

    std::error_code error;
    const std::filesystem::path staging = documentsDir / "4KDownerTemp";
    std::filesystem::create_directories(staging, error);
    return PathUtf8(staging);
}
