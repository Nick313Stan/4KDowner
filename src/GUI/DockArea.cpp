#include "DockArea.h"

#include "BrowserDiagnostics.h"
#include "DownloadFormatPredictor.h"
#include "MouseCursor.h"
#include "tinyfiledialogs.h"
#include "VideoTitle.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#include <windows.h>
#include <shellapi.h>
#undef CloseWindow
#undef ShowCursor
#undef DrawTextEx
#endif

namespace {
std::string PathUtf8(const std::filesystem::path& path)
{
    return path.u8string();
}

namespace HeaderLayout {
constexpr float kAboutX = 8.0f;
constexpr float kAboutWidth = 56.0f;
constexpr float kTabY = 3.0f;
constexpr float kTabHeight = 20.0f;
constexpr float kSeparatorX = kAboutX + kAboutWidth + 4.0f;
constexpr float kDownloaderX = kSeparatorX + 6.0f;
constexpr float kDownloaderWidth = 104.0f;
constexpr float kConverterX = kDownloaderX + kDownloaderWidth + 6.0f;
constexpr float kConverterWidth = 100.0f;

Rectangle AboutButton(float headerY)
{
    return {kAboutX, headerY + kTabY, kAboutWidth, kTabHeight};
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

struct DownloaderPanelLayout {
    float formatDropdownY = 0.0f;
    float mediaDropdownY = 0.0f;
    float qualityDropdownY = 0.0f;
    float pathRowY = 0.0f;
    float pathFieldY = 0.0f;
    float resultTitleY = 0.0f;
    float resultLineY = 0.0f;
};

DownloaderPanelLayout GetDownloaderPanelLayout(float panelY)
{
    const float firstRow = panelY + 46.0f;
    DownloaderPanelLayout layout;
    layout.formatDropdownY = firstRow;
    layout.mediaDropdownY = firstRow + 32.0f;
    layout.qualityDropdownY = firstRow + 64.0f;
    layout.pathRowY = firstRow + 96.0f;
    layout.pathFieldY = firstRow + 120.0f;
    layout.resultTitleY = firstRow + 156.0f;
    layout.resultLineY = firstRow + 176.0f;
    return layout;
}

void DrawDownloadResultPreview(Font font, const Rectangle& settingsPanel, const PredictedDownload& prediction, float titleY, float lineStartY)
{
    const Color muted = {150, 162, 150, 255};
    const Color valueColor = {168, 198, 168, 255};
    const float x = settingsPanel.x + 14.0f;
    const float valueX = settingsPanel.x + 132.0f;
    const float lineHeight = 19.0f;

    DrawTextEx(font, "Download result:", {x, titleY}, 15.0f, 0.0f, muted);

    const auto drawRow = [&](const char* label, const std::string& value, float y) {
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
    drawRow("Video container:", container, y);
    y += lineHeight;
    drawRow("Video codec:", videoCodec, y);
    y += lineHeight;
    drawRow("Audio codec:", audioCodec, y);
    y += lineHeight;
    drawRow("Resolution:", resolution, y);
}

std::filesystem::path FindExistingOutputFile(
    const std::filesystem::path& outputDirectory,
    const std::string& normalizedTitle,
    const std::string& extension)
{
    if (normalizedTitle.empty())
    {
        return {};
    }

    const std::string lowerExtension = ToLowerAscii(extension);
    const std::filesystem::path exactPath = outputDirectory / (normalizedTitle + "." + lowerExtension);
    std::error_code error;
    if (std::filesystem::exists(exactPath, error))
    {
        return exactPath;
    }

    return {};
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

void EnsureConverterDropdownIndex(int& selectedIndex, const std::vector<std::string>& items, const std::string& preferred)
{
    if (selectedIndex < 0 ||
        selectedIndex >= static_cast<int>(items.size()) ||
        IsConverterCurrentItem(items[selectedIndex]))
    {
        selectedIndex = GetDefaultConverterIndex(items, preferred);
    }
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
        return "All downloads finished — took " + FormatElapsedTime(seconds);
    }
    return "Download finished — took " + FormatElapsedTime(seconds);
}

std::string FormatConvertFinishedStatus(double seconds, bool allConversions)
{
    if (allConversions)
    {
        return "All videos converted successfully — took " + FormatElapsedTime(seconds);
    }
    return "Video converted successfully — took " + FormatElapsedTime(seconds);
}

double SumCompletedCardDownloadElapsed(const std::vector<LinkCardNode>& cards)
{
    double total = 0.0;
    for (const LinkCardNode& card : cards)
    {
        total += card.DownloadElapsedSeconds();
    }
    return total;
}

std::filesystem::path GetLogPath()
{
#ifdef _WIN32
    char* localAppData = nullptr;
    size_t localAppDataSize = 0;
    if (_dupenv_s(&localAppData, &localAppDataSize, "LOCALAPPDATA") == 0 && localAppData != nullptr && localAppData[0] != '\0')
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

void DrawWrappedText(Font font, const std::string& text, Vector2 position, float fontSize, float maxWidth, int maxLines, Color color)
{
    std::stringstream stream(text);
    std::vector<std::string> lines;
    std::string word;
    std::string currentLine;

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
        }
        else
        {
            lines.push_back(word);
            currentLine.clear();
        }

        if (static_cast<int>(lines.size()) >= maxLines)
        {
            break;
        }
    }

    if (!currentLine.empty() && static_cast<int>(lines.size()) < maxLines)
    {
        lines.push_back(currentLine);
    }

    for (int index = 0; index < static_cast<int>(lines.size()); ++index)
    {
        DrawTextEx(
            font,
            lines[index].c_str(),
            {position.x, position.y + static_cast<float>(index) * (fontSize + 3.0f)},
            fontSize,
            0.0f,
            color);
    }
}

constexpr float kAboutLinkFontSize = 15.0f;

struct AboutDialogLink {
    const char* url;
    const char* label;
    float yOffset;
};

struct AboutDialogMetrics {
    Rectangle modal{};
    Rectangle okButton{};
    float itemX = 0.0f;

    static AboutDialogMetrics FromWindow(int windowWidth, int windowHeight)
    {
        constexpr float kModalWidth = 460.0f;
        constexpr float kModalHeight = 388.0f;
        AboutDialogMetrics metrics;
        metrics.modal = {
            (static_cast<float>(windowWidth) - kModalWidth) * 0.5f,
            (static_cast<float>(windowHeight) - kModalHeight) * 0.5f,
            kModalWidth,
            kModalHeight};
        metrics.okButton = {
            metrics.modal.x + metrics.modal.width - 118.0f,
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
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#else
    (void)url;
#endif
}

void DrawAboutLink(
    Font font,
    const char* label,
    float y,
    float x,
    Color normalColor,
    Color hoverColor)
{
    const Vector2 size = MeasureTextEx(font, label, kAboutLinkFontSize, 0.0f);
    const Rectangle bounds = {x, y, size.x, size.y + 2.0f};
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    const Color color = hovered ? hoverColor : normalColor;
    DrawTextEx(font, label, {x, y}, kAboutLinkFontSize, 0.0f, color);
    if (hovered)
    {
        DrawLineEx(
            {x, y + size.y + 1.0f},
            {x + size.x, y + size.y + 1.0f},
            1.0f,
            color);
    }
}
}

DockArea::DockArea()
    : globalDownloadPath_(GetDefaultDownloadPath())
{
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
    UpdateFooterNotificationTimer();
    for (DownloadRunner& runner : downloadRunners_)
    {
        ProcessFinishedDownloadRunner(runner);
    }

    if (!pendingConvertQueue_.empty() && !isOverwritePromptOpen_ && !isAboutDialogOpen_ && FirstFreeConvertRunner() != nullptr)
    {
        StartNextPendingConvert();
    }

    if (isAboutDialogOpen_)
    {
        UpdateAboutDialog(windowWidth, windowHeight, font);
        return;
    }

    UpdateHeader();

    SyncCardProgress();

    if (isOverwritePromptOpen_)
    {
        UpdateOverwritePrompt(windowWidth, windowHeight);
        return;
    }

    if (!pendingDownloadQueue_.empty() &&
        !isOverwritePromptOpen_ &&
        FirstFreeDownloadRunner() != nullptr &&
        GetTime() >= nextDownloadStartTime_)
    {
        WriteDebugLog("starting scheduled next download");
        StartNextPendingDownload();
    }

    if (activeWorkspace_ == Workspace::Downloader)
    {
        UpdateDownloaderWorkspace(leftPanel, rightPanel, font);
        if (AnyDownloadRunning())
        {
            if (cancelDownloadButton_.Update(downloadButton))
            {
                pendingDownloadQueue_.clear();
                for (LinkCardNode& card : cards_)
                {
                    card.ClearDownloading();
                    card.ClearQueueState();
                }
                isBatchDownloading_ = false;
                overwriteAllExisting_ = false;
                CancelAllDownloads();
            }
            else if (HasDownloadableIdleCards() && downloadAllButton_.Update(downloadAllButton))
            {
                HandleDownloadAllRequest();
            }
        }
        else if (downloadButton_.Update(downloadButton, CanDownloadSelected()))
        {
            HandleDownloadRequest();
        }
        else if (downloadAllButton_.Update(downloadAllButton, HasValidDownloadCards()))
        {
            HandleDownloadAllRequest();
        }
    }
    else
    {
        UpdateConverterWorkspace(leftPanel, rightPanel, font);
        const Rectangle settingsPanel = GetRightSettingsPanel(rightPanel);
        if (AnyConvertRunning())
        {
            if (cancelDownloadButton_.Update(GetDownloadButtonBounds(settingsPanel)))
            {
                pendingConvertQueue_.clear();
                isBatchConverting_ = false;
                batchConvertElapsedTotal_ = 0.0;
                overwriteAllExisting_ = false;
                for (ConverterFileCardNode& card : converterCards_)
                {
                    if (card.IsConverting())
                    {
                        card.ClearConverting();
                    }
                }
                CancelAllConverts();
            }
        }
        else if (convertButton_.Update(
                     GetDownloadButtonBounds(settingsPanel),
                     std::any_of(
                         converterCards_.begin(),
                         converterCards_.end(),
                         [](const ConverterFileCardNode& card) { return card.HasFile() && !card.IsLoading(); })))
        {
            HandleConvertRequest();
        }
        else if (convertAllButton_.Update(
                     GetSecondaryActionButtonBounds(settingsPanel),
                     std::any_of(
                         converterCards_.begin(),
                         converterCards_.end(),
                         [](const ConverterFileCardNode& card) { return card.HasFile() && !card.IsLoading(); })))
        {
            HandleConvertAllRequest();
        }
    }

    if (!isOverwritePromptOpen_)
    {
        CollectParseFailures();
        CollectConverterLoadResults();
        UpdateFooter();
    }
}

void DockArea::Draw(int windowWidth, int windowHeight, Font font) const
{
    UiCursor::BeginFrame();

    const Color background = {26, 34, 26, 255};
    const Color border = {64, 84, 64, 255};
    const Rectangle leftPanel = GetLeftPanel(windowWidth, windowHeight);
    const Rectangle rightPanel = GetRightPanel(windowWidth, windowHeight);
    const Rectangle header = GetHeader(windowWidth);
    const Rectangle footer = GetFooter(windowWidth, windowHeight);

    const auto drawPanel = [&](Rectangle bounds) {
        const float minSide = bounds.width < bounds.height ? bounds.width : bounds.height;
        const float roundness = (kCornerRadius * 2.0f) / minSide;

        DrawRectangleRounded(bounds, roundness, 16, background);
        DrawRectangleRoundedLines(bounds, roundness, 16, border);
    };

    drawPanel(leftPanel);
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
    DrawFooter(footer, font);
    DrawOverwritePrompt(windowWidth, windowHeight, font);
    DrawAboutDialog(windowWidth, windowHeight, font);

    UiCursor::ApplyFrame();
}

void DockArea::UnloadResources()
{
    cards_.clear();
    converterCards_.clear();
}

Rectangle DockArea::GetBounds(int windowWidth, int windowHeight) const
{
    return {
        kMargin,
        kMargin,
        static_cast<float>(windowWidth) - kMargin * 2.0f,
        static_cast<float>(windowHeight) - kMargin * 2.0f};
}

Rectangle DockArea::GetLeftPanel(int windowWidth, int windowHeight) const
{
    const Rectangle area = GetBounds(windowWidth, windowHeight);
    const float leftWidth = (area.width - kGap) * kLeftPanelRatio;

    return {
        area.x,
        area.y + kHeaderHeight,
        leftWidth,
        area.height - kHeaderHeight - kFooterHeight};
}

Rectangle DockArea::GetRightPanel(int windowWidth, int windowHeight) const
{
    const Rectangle area = GetBounds(windowWidth, windowHeight);
    const float leftWidth = (area.width - kGap) * kLeftPanelRatio;
    const float rightWidth = area.width - kGap - leftWidth;

    return {
        area.x + leftWidth + kGap,
        area.y + kHeaderHeight,
        rightWidth,
        area.height - kHeaderHeight - kFooterHeight};
}

Rectangle DockArea::GetHeader(int windowWidth) const
{
    return {
        0.0f,
        0.0f,
        static_cast<float>(windowWidth),
        kHeaderHeight};
}

Rectangle DockArea::GetFooter(int windowWidth, int windowHeight) const
{
    return {
        0.0f,
        static_cast<float>(windowHeight) - kFooterHeight,
        static_cast<float>(windowWidth),
        kFooterHeight};
}

Rectangle DockArea::GetRightSettingsPanel(Rectangle rightPanel) const
{
    return {
        rightPanel.x,
        rightPanel.y,
        rightPanel.width,
        rightPanel.height - 75.0f};
}

Rectangle DockArea::GetGlobalPathPanel(Rectangle rightPanel) const
{
    return {
        rightPanel.x,
        rightPanel.y + rightPanel.height - 70.0f,
        rightPanel.width,
        70.0f};
}

Rectangle DockArea::GetInsertLinkButtonBounds(Rectangle leftPanel) const
{
    return {
        leftPanel.x + (leftPanel.width - 180.0f) * 0.5f,
        leftPanel.y + (leftPanel.height - 48.0f) * 0.5f,
        180.0f,
        48.0f};
}

Rectangle DockArea::GetChooseFileButtonBounds(Rectangle leftPanel) const
{
    return {
        leftPanel.x + (leftPanel.width - 180.0f) * 0.5f,
        leftPanel.y + (leftPanel.height - 48.0f) * 0.5f,
        180.0f,
        48.0f};
}

Rectangle DockArea::GetListActionButtonBounds(Rectangle leftPanel, int index, float scrollOffset) const
{
    const Rectangle slot = GetCardBounds(leftPanel, index, scrollOffset);
    return {
        slot.x + (slot.width - 180.0f) * 0.5f,
        slot.y + 13.0f,
        180.0f,
        48.0f};
}

Rectangle DockArea::GetDownloadButtonBounds(Rectangle settingsPanel) const
{
    return {
        settingsPanel.x + 14.0f,
        settingsPanel.y + settingsPanel.height - 34.0f - 12.0f,
        (settingsPanel.width - 42.0f) * 0.5f,
        34.0f};
}

Rectangle DockArea::GetSecondaryActionButtonBounds(Rectangle settingsPanel) const
{
    const float width = (settingsPanel.width - 42.0f) * 0.5f;
    return {
        settingsPanel.x + 28.0f + width,
        settingsPanel.y + settingsPanel.height - 34.0f - 12.0f,
        width,
        34.0f};
}

Rectangle DockArea::GetCardBounds(Rectangle leftPanel, int index, float scrollOffset) const
{
    return {
        leftPanel.x + kMargin,
        leftPanel.y + kMargin + static_cast<float>(index) * (kCardHeight + kGap) - scrollOffset,
        leftPanel.width - kMargin * 2.0f,
        kCardHeight};
}

float DockArea::GetMaxCardScroll(Rectangle leftPanel, int itemCount, float reservedBottom) const
{
    const float contentHeight = static_cast<float>(itemCount) * (kCardHeight + kGap);
    const float visibleHeight = std::max(0.0f, leftPanel.height - kMargin * 2.0f - reservedBottom);
    return std::max(0.0f, contentHeight - visibleHeight);
}

void DockArea::UpdateCardScroll(Rectangle leftPanel, int itemCount, float reservedBottom, float& scrollOffset) const
{
    const float maxScroll = GetMaxCardScroll(leftPanel, itemCount, reservedBottom);
    if (itemCount <= 0)
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
    const Rectangle downloaderTab = HeaderLayout::DownloaderTab(headerY);
    const Rectangle converterTab = HeaderLayout::ConverterTab(headerY);

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, aboutBounds))
    {
        isAboutDialogOpen_ = true;
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
    else if (!cards_.empty() && insertLinkButton_.Update(GetListActionButtonBounds(leftPanel, static_cast<int>(cards_.size()), downloaderScrollOffset_)))
    {
        HandleInsertLinkRequest();
    }

    const bool pathFieldActive = customPathField_.IsActive() || globalPathField_.IsActive();
    if (!pathFieldActive &&
        (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
        IsKeyPressed(KEY_V))
    {
        HandleInsertLinkRequest();
    }

    const int downloaderItemCount = static_cast<int>(cards_.size()) + (!cards_.empty() ? 1 : 0);
    UpdateCardScroll(leftPanel, downloaderItemCount, 0.0f, downloaderScrollOffset_);
    UpdateCards(leftPanel);
    UpdateRightPanel(rightPanel, font);
}

void DockArea::UpdateConverterWorkspace(Rectangle leftPanel, Rectangle rightPanel, Font font)
{
    const Rectangle globalPanel = GetGlobalPathPanel(rightPanel);
    const Rectangle globalPathBounds = {globalPanel.x + 10.0f, globalPanel.y + 34.0f, globalPanel.width - 20.0f, 26.0f};
    globalPathField_.Update(globalPathBounds, font, globalDownloadPath_, true);

    const int converterItemCount = static_cast<int>(converterCards_.size()) + (!converterCards_.empty() ? 1 : 0);
    UpdateCardScroll(leftPanel, converterItemCount, 0.0f, converterScrollOffset_);

    const Rectangle chooseBounds = converterCards_.empty()
        ? GetChooseFileButtonBounds(leftPanel)
        : GetListActionButtonBounds(leftPanel, static_cast<int>(converterCards_.size()), converterScrollOffset_);
    if (chooseFileButton_.Update(chooseBounds))
    {
        HandleChooseFileRequest();
    }

    int clickedIndex = -1;
    for (int index = 0; index < static_cast<int>(converterCards_.size()); ++index)
    {
        converterCards_[index].Update(GetCardBounds(leftPanel, index, converterScrollOffset_));
        const std::string inputPath = converterCards_[index].Info().filePath;
        if (converterCards_[index].WasConvertCancelClicked() &&
            FindConvertRunnerByPath(inputPath) != nullptr)
        {
            CancelConverterCard(inputPath);
            continue;
        }
        if (converterCards_[index].ShouldClose())
        {
            if (converterCards_[index].IsLoading())
            {
                converterCards_[index].CancelLoading();
            }
            else if (converterCards_[index].IsConverting() &&
                FindConvertRunnerByPath(inputPath) != nullptr)
            {
                CancelConverterCard(inputPath);
            }
            else
            {
                RemovePendingConvertsForPath(inputPath);
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
        }
        else if (ctrl)
        {
            converterCards_[clickedIndex].SetSelected(!converterCards_[clickedIndex].IsSelected());
            lastConverterSelectionAnchor_ = clickedIndex;
        }
        else
        {
            for (int index = 0; index < static_cast<int>(converterCards_.size()); ++index)
            {
                converterCards_[index].SetSelected(index == clickedIndex);
            }
            lastConverterSelectionAnchor_ = clickedIndex;
        }
    }

    converterCards_.erase(
        std::remove_if(converterCards_.begin(), converterCards_.end(), [](const ConverterFileCardNode& card) {
            return card.ShouldClose();
        }),
        converterCards_.end());

    const Rectangle settingsPanel = GetRightSettingsPanel(rightPanel);
    const Rectangle checkboxBounds1 = {settingsPanel.x + 18.0f, settingsPanel.y + 58.0f, 18.0f, 18.0f};
    const Rectangle checkboxBounds2 = {settingsPanel.x + 18.0f, settingsPanel.y + 142.0f, 18.0f, 18.0f};
    const Rectangle checkboxBounds3 = {settingsPanel.x + 18.0f, settingsPanel.y + 226.0f, 18.0f, 18.0f};
    const Rectangle containerDropdownBounds = {settingsPanel.x + 42.0f, settingsPanel.y + 84.0f, settingsPanel.width - 56.0f, 25.0f};
    const Rectangle videoDropdownBounds = {settingsPanel.x + 42.0f, settingsPanel.y + 168.0f, settingsPanel.width - 56.0f, 25.0f};
    const Rectangle audioDropdownBounds = {settingsPanel.x + 42.0f, settingsPanel.y + 252.0f, settingsPanel.width - 56.0f, 25.0f};

    ConverterFileCardNode* selectedCard = GetSelectedConverterCard();
    if (selectedCard == nullptr || selectedCard->IsLoading())
    {
        return;
    }

    const ConverterFileInfo& info = selectedCard->Info();
    const std::vector<std::string> containerItems = BuildConverterItems({"MP4", "MKV", "MOV", "WEBM"}, info.container);
    const std::vector<std::string> videoItems = BuildConverterItems({"H.264", "H.265", "AV1", "VP9"}, info.videoCodec);
    const std::vector<std::string> audioItems = BuildConverterItems({"AAC", "MP3", "Opus", "FLAC"}, info.audioCodec);
    convertContainerDropdown_.SetItems(containerItems);
    convertVideoDropdown_.SetItems(videoItems);
    convertAudioDropdown_.SetItems(audioItems);
    if (info.filePath != lastConverterDropdownCardPath_)
    {
        convertContainerIndex_ = GetDefaultConverterIndex(containerItems, "MP4");
        convertVideoIndex_ = GetDefaultConverterIndex(videoItems, "H.264");
        convertAudioIndex_ = GetDefaultConverterIndex(audioItems, "AAC");
        lastConverterDropdownCardPath_ = info.filePath;
    }
    else
    {
        EnsureConverterDropdownIndex(convertContainerIndex_, containerItems, "MP4");
        EnsureConverterDropdownIndex(convertVideoIndex_, videoItems, "H.264");
        EnsureConverterDropdownIndex(convertAudioIndex_, audioItems, "AAC");
    }
    const float popupMaxY = GetDownloadButtonBounds(settingsPanel).y - 6.0f;
    convertContainerDropdown_.SetPopupLimitY(settingsPanel.y + 8.0f, popupMaxY);
    convertVideoDropdown_.SetPopupLimitY(settingsPanel.y + 8.0f, popupMaxY);
    convertAudioDropdown_.SetPopupLimitY(settingsPanel.y + 8.0f, popupMaxY);

    bool containerConsumed = false;
    bool videoConsumed = false;
    bool audioConsumed = false;
    if (convertContainer_)
    {
        containerConsumed = convertContainerDropdown_.Update(containerDropdownBounds, convertContainerIndex_);
    }
    else
    {
        convertContainerDropdown_.Close();
    }
    if (convertVideo_)
    {
        videoConsumed = convertVideoDropdown_.Update(videoDropdownBounds, convertVideoIndex_);
    }
    else
    {
        convertVideoDropdown_.Close();
    }
    if (convertAudio_)
    {
        audioConsumed = convertAudioDropdown_.Update(audioDropdownBounds, convertAudioIndex_);
    }
    else
    {
        convertAudioDropdown_.Close();
    }

    const bool dropdownBlocksInput = convertContainerDropdown_.IsOpen() ||
                                     convertVideoDropdown_.IsOpen() ||
                                     convertAudioDropdown_.IsOpen() ||
                                     containerConsumed ||
                                     videoConsumed ||
                                     audioConsumed;
    if (!dropdownBlocksInput)
    {
        convertContainerCheckbox_.Update(checkboxBounds1, convertContainer_);
        convertVideoCheckbox_.Update(checkboxBounds2, convertVideo_);
        convertAudioCheckbox_.Update(checkboxBounds3, convertAudio_);
    }
}

void DockArea::DrawDownloaderWorkspace(Rectangle leftPanel, Rectangle rightPanel, Font font) const
{
    if (cards_.empty())
    {
        insertLinkButton_.Draw(GetInsertLinkButtonBounds(leftPanel), font);
    }

    BeginScissorMode(
        static_cast<int>(leftPanel.x),
        static_cast<int>(leftPanel.y),
        static_cast<int>(leftPanel.width),
        static_cast<int>(leftPanel.height));
    for (int index = 0; index < static_cast<int>(cards_.size()); ++index)
    {
        cards_[index].Draw(GetCardBounds(leftPanel, index, downloaderScrollOffset_), font);
    }
    if (!cards_.empty())
    {
        insertLinkButton_.Draw(GetListActionButtonBounds(leftPanel, static_cast<int>(cards_.size()), downloaderScrollOffset_), font);
    }

    EndScissorMode();

    DrawRightPanel(rightPanel, font);
}

void DockArea::DrawConverterWorkspace(Rectangle leftPanel, Rectangle rightPanel, Font font) const
{
    if (converterCards_.empty())
    {
        chooseFileButton_.Draw(GetChooseFileButtonBounds(leftPanel), font);
    }

    const Rectangle converterListClip = {
        leftPanel.x,
        leftPanel.y,
        leftPanel.width,
        leftPanel.height};
    BeginScissorMode(
        static_cast<int>(converterListClip.x),
        static_cast<int>(converterListClip.y),
        static_cast<int>(converterListClip.width),
        static_cast<int>(converterListClip.height));
    for (int index = 0; index < static_cast<int>(converterCards_.size()); ++index)
    {
        converterCards_[index].Draw(GetCardBounds(leftPanel, index, converterScrollOffset_), font);
    }
    if (!converterCards_.empty())
    {
        chooseFileButton_.Draw(GetListActionButtonBounds(leftPanel, static_cast<int>(converterCards_.size()), converterScrollOffset_), font);
    }
    EndScissorMode();

    const Rectangle settingsPanel = GetRightSettingsPanel(rightPanel);
    const Rectangle globalPanel = GetGlobalPathPanel(rightPanel);
    const Color text = {224, 230, 224, 255};
    const Color muted = {150, 162, 150, 255};

    DrawTextEx(font, "Options", {settingsPanel.x + 12.0f, settingsPanel.y + 12.0f}, 16.0f, 0.0f, text);

    const ConverterFileCardNode* selectedCard = GetSelectedConverterCard();
    if (selectedCard != nullptr && selectedCard->IsLoading())
    {
        selectedCard = nullptr;
    }
    if (selectedCard == nullptr)
    {
        DrawWrappedText(
            font,
            "Choose a media file to inspect conversion options.",
            {settingsPanel.x + 14.0f, settingsPanel.y + 58.0f},
            15.0f,
            settingsPanel.width - 28.0f,
            3,
            muted);
    }
    else
    {
        DrawTextEx(font, "Container", {settingsPanel.x + 14.0f, settingsPanel.y + 38.0f}, 15.0f, 0.0f, text);
        convertContainerCheckbox_.Draw({settingsPanel.x + 18.0f, settingsPanel.y + 58.0f, 18.0f, 18.0f}, font, "Convert format", convertContainer_);
        convertContainerDropdown_.DrawControl(
            {settingsPanel.x + 42.0f, settingsPanel.y + 84.0f, settingsPanel.width - 56.0f, 25.0f},
            font,
            convertContainerIndex_,
            convertContainer_);

        DrawTextEx(font, "Video", {settingsPanel.x + 14.0f, settingsPanel.y + 122.0f}, 15.0f, 0.0f, text);
        convertVideoCheckbox_.Draw({settingsPanel.x + 18.0f, settingsPanel.y + 142.0f, 18.0f, 18.0f}, font, "Convert video codec", convertVideo_);
        convertVideoDropdown_.DrawControl(
            {settingsPanel.x + 42.0f, settingsPanel.y + 168.0f, settingsPanel.width - 56.0f, 25.0f},
            font,
            convertVideoIndex_,
            convertVideo_);

        DrawTextEx(font, "Audio", {settingsPanel.x + 14.0f, settingsPanel.y + 206.0f}, 15.0f, 0.0f, text);
        convertAudioCheckbox_.Draw({settingsPanel.x + 18.0f, settingsPanel.y + 226.0f, 18.0f, 18.0f}, font, "Convert audio codec", convertAudio_);
        convertAudioDropdown_.DrawControl(
            {settingsPanel.x + 42.0f, settingsPanel.y + 252.0f, settingsPanel.width - 56.0f, 25.0f},
            font,
            convertAudioIndex_,
            convertAudio_);

    }

    if (AnyConvertRunning())
    {
        cancelDownloadButton_.DrawDanger(GetDownloadButtonBounds(settingsPanel), font);
    }
    else
    {
        const bool hasReadyConverterCards = std::any_of(
            converterCards_.begin(),
            converterCards_.end(),
            [](const ConverterFileCardNode& card) { return card.HasFile() && !card.IsLoading(); });
        convertButton_.Draw(GetDownloadButtonBounds(settingsPanel), font, hasReadyConverterCards);
        convertAllButton_.Draw(GetSecondaryActionButtonBounds(settingsPanel), font, hasReadyConverterCards);
    }

    if (selectedCard != nullptr)
    {
        if (convertContainer_)
        {
            convertContainerDropdown_.DrawPopup({settingsPanel.x + 42.0f, settingsPanel.y + 84.0f, settingsPanel.width - 56.0f, 25.0f}, font, convertContainerIndex_);
        }
        if (convertVideo_)
        {
            convertVideoDropdown_.DrawPopup({settingsPanel.x + 42.0f, settingsPanel.y + 168.0f, settingsPanel.width - 56.0f, 25.0f}, font, convertVideoIndex_);
        }
        if (convertAudio_)
        {
            convertAudioDropdown_.DrawPopup({settingsPanel.x + 42.0f, settingsPanel.y + 252.0f, settingsPanel.width - 56.0f, 25.0f}, font, convertAudioIndex_);
        }
    }

    DrawTextEx(font, "Global Output Path", {globalPanel.x + 10.0f, globalPanel.y + 10.0f}, 16.0f, 0.0f, text);
    globalPathField_.Draw({globalPanel.x + 10.0f, globalPanel.y + 34.0f, globalPanel.width - 20.0f, 26.0f}, font, globalDownloadPath_, true);
}

void DockArea::HandleChooseFileRequest()
{
    const char* filters[] = {"*.mp4", "*.mkv", "*.mov", "*.webm", "*.avi", "*.m4v", "*.mp3", "*.m4a", "*.wav", "*.flac", "*.opus"};
    const char* selected = tinyfd_openFileDialog("Choose media file", "", 11, filters, "Media files", 1);
    if (selected == nullptr || selected[0] == '\0')
    {
        return;
    }

    std::vector<std::string> paths;
    {
        std::string selectedText = selected;
        size_t start = 0;
        while (start <= selectedText.size())
        {
            const size_t end = selectedText.find('|', start);
            const std::string path = end == std::string::npos
                ? selectedText.substr(start)
                : selectedText.substr(start, end - start);
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
    }

    if (paths.empty())
    {
        return;
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
    }
}

void DockArea::HandleInsertLinkRequest()
{
    const char* clipboard = GetClipboardText();
    const char* url = clipboard == nullptr ? "" : clipboard;

    for (LinkCardNode& card : cards_)
    {
        if (card.HasUrl(url))
        {
            card.TriggerPulse();
            return;
        }
    }

    for (LinkCardNode& card : cards_)
    {
        card.SetSelected(false);
    }
    cards_.emplace_back(url);
    cards_.back().SetSelected(true);
    if (AnyDownloadRunning() || isBatchDownloading_)
    {
        cards_.back().SetNotInQueue();
    }
}

void DockArea::OnCardClosed(const std::string& url)
{
    pendingDownloadQueue_.erase(
        std::remove_if(
            pendingDownloadQueue_.begin(),
            pendingDownloadQueue_.end(),
            [&](const DownloadRequest& request) { return request.url == url; }),
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
        for (LinkCardNode& card : cards_)
        {
            if (card.IsDownloading() && card.HasUrl(currentUrl))
            {
                card.SetOperationProgress(progress);
            }
        }
    }
    if (!AnyDownloadRunning())
    {
        for (LinkCardNode& card : cards_)
        {
            if (!card.IsDownloading())
            {
                card.ClearOperationProgress();
            }
        }
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
    }
    for (ConverterFileCardNode& card : converterCards_)
    {
        if (!card.IsConverting())
        {
            card.ClearOperationProgress();
        }
    }
}

void DockArea::UpdateCards(Rectangle leftPanel)
{
    int clickedIndex = -1;
    for (int index = 0; index < static_cast<int>(cards_.size()); ++index)
    {
        cards_[index].Update(GetCardBounds(leftPanel, index, downloaderScrollOffset_));
        if (cards_[index].WasCopyClicked())
        {
            ShowFooterNotification("Info Copied to Clipboard");
            continue;
        }
        if (cards_[index].WasDownloadCancelClicked())
        {
            const std::string url = cards_[index].Url();
            if (DownloadRunner* runner = FindDownloadRunnerByUrl(url))
            {
                cards_[index].ClearDownloading();
                if (!isBatchDownloading_)
                {
                    pendingDownloadQueue_.clear();
                }
                runner->Cancel();
                continue;
            }
        }
        if (cards_[index].WasQueueCancelClicked())
        {
            RemoveFromDownloadQueue(cards_[index].Url());
            continue;
        }
        if (cards_[index].WasRedownloadClicked() || cards_[index].WasQueueDownloadClicked())
        {
            DownloadRequest request;
            if (BuildDownloadRequestForCard(cards_[index], request))
            {
                if (!AnyDownloadRunning() && !isBatchDownloading_)
                {
                    ClearFooterNotification();
                    overwriteAllExisting_ = false;
                }
                cards_[index].SetQueued();
                pendingDownloadQueue_.push_back(std::move(request));
                isBatchDownloading_ = true;
                nextDownloadStartTime_ = GetTime();
                StartNextPendingDownload();
            }
            continue;
        }
        if (cards_[index].ShouldClose())
        {
            OnCardClosed(cards_[index].Url());
        }
        if (cards_[index].WasClicked())
        {
            clickedIndex = index;
        }
    }

    if (clickedIndex >= 0)
    {
        for (int index = 0; index < static_cast<int>(cards_.size()); ++index)
        {
            cards_[index].SetSelected(index == clickedIndex);
        }
    }

    cards_.erase(
        std::remove_if(cards_.begin(), cards_.end(), [](const LinkCardNode& card) {
            return card.ShouldClose();
        }),
        cards_.end());
}

void DockArea::UpdateRightPanel(Rectangle rightPanel, Font font)
{
    LinkCardNode* selectedCard = GetSelectedCard();
    if (selectedCard != nullptr && !selectedCard->IsValid())
    {
        selectedCard = nullptr;
    }
    const Rectangle settingsPanel = GetRightSettingsPanel(rightPanel);
    const Rectangle globalPanel = GetGlobalPathPanel(rightPanel);
    const Rectangle globalPathBounds = {globalPanel.x + 10.0f, globalPanel.y + 34.0f, globalPanel.width - 20.0f, 26.0f};

    globalPathField_.Update(globalPathBounds, font, globalDownloadPath_, true);

    if (selectedCard == nullptr)
    {
        return;
    }

    DownloadOptions& options = selectedCard->Options();
    std::vector<std::string> availableFormats = options.mediaMode == 2
        ? selectedCard->AvailableAudioFormats()
        : selectedCard->AvailableVideoFormats();
    if (availableFormats.empty())
    {
        availableFormats.push_back(options.mediaMode == 2 ? "M4A" : "MP4");
    }
    fileFormatDropdown_.SetItems(availableFormats);
    if (options.fileFormat < 0 || options.fileFormat >= static_cast<int>(availableFormats.size()))
    {
        options.fileFormat = 0;
    }

    std::vector<std::string> availableQualities = selectedCard->AvailableQualities();
    qualityDropdown_.SetItems(availableQualities);
    if (availableQualities.empty())
    {
        options.quality = 0;
    }
    else if (options.quality < 0 || options.quality >= static_cast<int>(availableQualities.size()))
    {
        options.quality = 0;
    }

    const DownloaderPanelLayout layout = GetDownloaderPanelLayout(settingsPanel.y);
    const Rectangle formatBounds = {settingsPanel.x + 94.0f, layout.formatDropdownY, settingsPanel.width - 108.0f, 25.0f};
    const Rectangle mediaBounds = {settingsPanel.x + 94.0f, layout.mediaDropdownY, settingsPanel.width - 108.0f, 25.0f};
    const Rectangle qualityBounds = {settingsPanel.x + 94.0f, layout.qualityDropdownY, settingsPanel.width - 108.0f, 25.0f};
    const Rectangle pathFieldBounds = {settingsPanel.x + 14.0f, layout.pathFieldY, settingsPanel.width - 28.0f, 25.0f};

    const bool formatConsumed = fileFormatDropdown_.Update(formatBounds, options.fileFormat);
    if (formatConsumed)
    {
        mediaModeDropdown_.Close();
        qualityDropdown_.Close();
    }
    const bool mediaConsumed = !formatConsumed && mediaModeDropdown_.Update(mediaBounds, options.mediaMode);
    if (mediaConsumed)
    {
        fileFormatDropdown_.Close();
        qualityDropdown_.Close();
        options.fileFormat = 0;
    }
    bool qualityConsumed = false;
    if (!formatConsumed && !mediaConsumed && options.mediaMode != 2)
    {
        qualityConsumed = qualityDropdown_.Update(qualityBounds, options.quality);
        if (qualityConsumed)
        {
            fileFormatDropdown_.Close();
            mediaModeDropdown_.Close();
        }
    }

    const bool dropdownBlocksInput = fileFormatDropdown_.IsOpen() ||
                                     mediaModeDropdown_.IsOpen() ||
                                     qualityDropdown_.IsOpen() ||
                                     formatConsumed ||
                                     mediaConsumed ||
                                     qualityConsumed;
    if (!dropdownBlocksInput)
    {
        customPathCheckbox_.Update({settingsPanel.x + 94.0f, layout.pathRowY, 130.0f, 18.0f}, options.useCustomPath);
        customPathField_.Update(pathFieldBounds, font, options.customPath, options.useCustomPath);
    }
    else if (customPathField_.IsActive())
    {
        customPathField_.Update(pathFieldBounds, font, options.customPath, false);
    }
}

void DockArea::HandleDownloadRequest()
{
    LinkCardNode* selectedCard = GetSelectedCard();
    if (selectedCard == nullptr || !selectedCard->IsValid())
    {
        return;
    }

    DownloadRequest request;
    if (!BuildDownloadRequestForCard(*selectedCard, request))
    {
        return;
    }

    if (!AnyDownloadRunning() && !isBatchDownloading_)
    {
        pendingDownloadQueue_.clear();
        ClearFooterNotification();
        overwriteAllExisting_ = false;
        for (LinkCardNode& card : cards_)
        {
            card.ClearQueueState();
        }
    }

    pendingDownloadQueue_.push_back(std::move(request));
    isBatchDownloading_ = true;
    nextDownloadStartTime_ = GetTime();
    StartNextPendingDownload();
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
        for (LinkCardNode& card : cards_)
        {
            card.ClearQueueState();
        }
    }

    bool addedAny = false;
    WriteDebugLog(appendToActiveBatch ? "Download All append clicked" : "Download All clicked");
    for (LinkCardNode& card : cards_)
    {
        if (!card.IsValid() || card.IsDownloading() || card.IsInQueue())
        {
            continue;
        }

        DownloadRequest request;
        if (BuildDownloadRequestForCard(card, request))
        {
            pendingDownloadQueue_.push_back(std::move(request));
            card.SetQueued();
            addedAny = true;
        }
    }

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
}

bool DockArea::HasDownloadableIdleCards() const
{
    for (const LinkCardNode& card : cards_)
    {
        if (card.IsValid() && !card.IsDownloading() && !card.IsInQueue())
        {
            return true;
        }
    }
    return false;
}

bool DockArea::HasValidDownloadCards() const
{
    for (const LinkCardNode& card : cards_)
    {
        if (card.IsValid())
        {
            return true;
        }
    }
    return false;
}

bool DockArea::CanDownloadSelected() const
{
    const LinkCardNode* selectedCard = GetSelectedCard();
    return selectedCard != nullptr && selectedCard->IsValid();
}

void DockArea::RemoveFromDownloadQueue(const std::string& url)
{
    pendingDownloadQueue_.erase(
        std::remove_if(
            pendingDownloadQueue_.begin(),
            pendingDownloadQueue_.end(),
            [&](const DownloadRequest& request) { return request.url == url; }),
        pendingDownloadQueue_.end());

    for (LinkCardNode& card : cards_)
    {
        if (!card.HasUrl(url) || !card.IsInQueue())
        {
            continue;
        }

        if (isBatchDownloading_ && (AnyDownloadRunning() || !pendingDownloadQueue_.empty()))
        {
            card.SetNotInQueue();
        }
        else
        {
            card.ClearQueueState();
        }
        break;
    }

    StartNextPendingDownload();
}

void DockArea::ClearBatchQueueStates()
{
    for (LinkCardNode& card : cards_)
    {
        card.ClearQueueState();
    }
}

bool DockArea::BuildDownloadRequestForCard(LinkCardNode& card, DownloadRequest& request)
{
    const DownloadOptions& options = card.Options();
    std::vector<std::string> availableFormats = options.mediaMode == 2
        ? card.AvailableAudioFormats()
        : card.AvailableVideoFormats();
    if (availableFormats.empty())
    {
        availableFormats.push_back(options.mediaMode == 2 ? "M4A" : "MP4");
    }

    std::vector<std::string> availableQualities = card.AvailableQualities();

    const std::vector<std::string> mediaModes = {"Both", "Video only", "Audio only"};
    const int formatIndex = std::clamp(options.fileFormat, 0, static_cast<int>(availableFormats.size()) - 1);
    const int mediaIndex = std::clamp(options.mediaMode, 0, static_cast<int>(mediaModes.size()) - 1);
    const int qualityIndex = availableQualities.empty()
        ? 0
        : std::clamp(options.quality, 0, static_cast<int>(availableQualities.size()) - 1);

    request.url = card.Url();
    request.title = card.Title();
    request.normalizedTitle = card.NormalizedTitle();
    if (request.normalizedTitle.empty())
    {
        request.normalizedTitle = NormalizeVideoTitle(request.title);
    }
    request.outputDirectory = options.useCustomPath && !options.customPath.empty() ? options.customPath : globalDownloadPath_;
    request.fileFormat = availableFormats[formatIndex];
    request.mediaMode = mediaModes[mediaIndex];
    request.quality = availableQualities.empty() ? "2160p" : availableQualities[qualityIndex];
    return true;
}

bool DockArea::PrepareDownloadRequest(DownloadRequest& request)
{
    try
    {
        std::filesystem::path outputPath = std::filesystem::u8path(request.outputDirectory);
        if (outputPath.is_relative())
        {
            std::filesystem::path userRoot = std::filesystem::u8path(GetDefaultDownloadPath()).parent_path();
            if (userRoot.empty())
            {
                userRoot = std::filesystem::current_path();
            }
            outputPath = userRoot / outputPath;
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

        const std::filesystem::path existingFile =
            FindExistingOutputFile(outputPath, request.normalizedTitle, request.fileFormat);
        if (!existingFile.empty())
        {
            if (overwriteAllExisting_)
            {
                request.overwriteExisting = true;
                return true;
            }

            pendingOverwriteRequest_ = request;
            pendingOverwriteFileName_ = PathUtf8(existingFile.filename());
            overwritePromptIsConvert_ = false;
            isOverwritePromptOpen_ = true;
            return false;
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
        DownloadRequest request = std::move(pendingDownloadQueue_.front());
        pendingDownloadQueue_.erase(pendingDownloadQueue_.begin());
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
        const double totalElapsed = SumCompletedCardDownloadElapsed(cards_);
        isBatchDownloading_ = false;
        overwriteAllExisting_ = false;
        ClearBatchQueueStates();
        ShowFooterNotification(
            FormatDownloadFinishedStatus(totalElapsed, true),
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
    for (LinkCardNode& card : cards_)
    {
        if (card.HasUrl(url))
        {
            card.SetDownloading();
            break;
        }
    }

    try
    {
        runner->Start(std::move(request));
    }
    catch (...)
    {
        for (LinkCardNode& card : cards_)
        {
            if (card.HasUrl(url))
            {
                card.ClearDownloading();
            }
        }
        ShowFooterNotification(
            "Download failed: could not start download.",
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
    if (!convertContainer_ && !convertVideo_ && !convertAudio_)
    {
        PulseConverterFooterHint();
        return;
    }

    ClearFooterNotification();

    if (!AnyConvertRunning() && pendingConvertQueue_.empty())
    {
        pendingConvertQueue_.clear();
        overwriteAllExisting_ = false;
        isBatchConverting_ = false;
        batchConvertElapsedTotal_ = 0.0;
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
        return;
    }

    if (queued.size() > 1)
    {
        isBatchConverting_ = true;
        batchConvertElapsedTotal_ = 0.0;
    }

    for (ConvertRequest& request : queued)
    {
        pendingConvertQueue_.push_back(std::move(request));
    }
    StartNextPendingConvert();
}

void DockArea::HandleConvertAllRequest()
{
    if (!convertContainer_ && !convertVideo_ && !convertAudio_)
    {
        PulseConverterFooterHint();
        return;
    }

    ClearFooterNotification();
    if (!AnyConvertRunning())
    {
        pendingConvertQueue_.clear();
        batchConvertElapsedTotal_ = 0.0;
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
        return;
    }

    isBatchConverting_ = true;
    if (!AnyConvertRunning())
    {
        batchConvertElapsedTotal_ = 0.0;
    }
    overwriteAllExisting_ = false;
    for (ConvertRequest& request : queued)
    {
        pendingConvertQueue_.push_back(std::move(request));
    }
    StartNextPendingConvert();
}

bool DockArea::BuildConvertRequestForCard(const ConverterFileCardNode& card, ConvertRequest& request) const
{
    if (!card.HasFile() || card.IsLoading() || !card.Info().success || (!convertContainer_ && !convertVideo_ && !convertAudio_))
    {
        return false;
    }

    const ConverterFileInfo& info = card.Info();
    const std::vector<std::string> containerItems = BuildConverterItems({"MP4", "MKV", "MOV", "WEBM"}, info.container);
    const std::vector<std::string> videoItems = BuildConverterItems({"H.264", "H.265", "AV1", "VP9"}, info.videoCodec);
    const std::vector<std::string> audioItems = BuildConverterItems({"AAC", "MP3", "Opus", "FLAC"}, info.audioCodec);
    const int containerIndex = containerItems.empty() ? 0 : std::clamp(convertContainerIndex_, 0, static_cast<int>(containerItems.size()) - 1);
    const int videoIndex = videoItems.empty() ? 0 : std::clamp(convertVideoIndex_, 0, static_cast<int>(videoItems.size()) - 1);
    const int audioIndex = audioItems.empty() ? 0 : std::clamp(convertAudioIndex_, 0, static_cast<int>(audioItems.size()) - 1);

    std::filesystem::path outputPath = std::filesystem::u8path(globalDownloadPath_);
    if (outputPath.is_relative())
    {
        std::filesystem::path userRoot = std::filesystem::u8path(GetDefaultDownloadPath()).parent_path();
        if (userRoot.empty())
        {
            userRoot = std::filesystem::current_path();
        }
        outputPath = userRoot / outputPath;
    }

    std::error_code error;
    std::filesystem::create_directories(outputPath, error);
    if (error)
    {
        return false;
    }

    request.inputPath = info.filePath;
    request.outputDirectory = PathUtf8(outputPath);
    request.convertContainer = convertContainer_;
    request.convertVideo = convertVideo_;
    request.convertAudio = convertAudio_;
    request.container = containerItems.empty() ? info.container : StripCurrentLabel(containerItems[containerIndex]);
    request.videoCodec = videoItems.empty() ? info.videoCodec : StripCurrentLabel(videoItems[videoIndex]);
    request.audioCodec = audioItems.empty() ? info.audioCodec : StripCurrentLabel(audioItems[audioIndex]);
    request.sourceDurationSeconds = info.durationSeconds;
    return true;
}

bool DockArea::PrepareConvertRequest(ConvertRequest& request)
{
    try
    {
        std::filesystem::path outputPath = std::filesystem::u8path(request.outputDirectory);
        if (outputPath.is_relative())
        {
            std::filesystem::path userRoot = std::filesystem::u8path(GetDefaultDownloadPath()).parent_path();
            if (userRoot.empty())
            {
                userRoot = std::filesystem::current_path();
            }
            outputPath = userRoot / outputPath;
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
        isBatchConverting_ = false;
        overwriteAllExisting_ = false;
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

    ClearFooterNotification();

    for (ConverterFileCardNode& card : converterCards_)
    {
        if (card.HasFilePath(request.inputPath))
        {
            card.SetConverting();
            break;
        }
    }

    runner->Start(std::move(request));
}

void DockArea::RemovePendingConvertsForPath(const std::string& inputPath)
{
    pendingConvertQueue_.erase(
        std::remove_if(
            pendingConvertQueue_.begin(),
            pendingConvertQueue_.end(),
            [&](const ConvertRequest& request) { return request.inputPath == inputPath; }),
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
    const Rectangle modal = {
        (static_cast<float>(windowWidth) - modalWidth) * 0.5f,
        (static_cast<float>(windowHeight) - modalHeight) * 0.5f,
        modalWidth,
        modalHeight};

    const Rectangle replaceBounds = {modal.x + modal.width - 318.0f, modal.y + modal.height - 48.0f, 96.0f, 34.0f};
    const Rectangle cancelBounds = {modal.x + modal.width - 212.0f, modal.y + modal.height - 48.0f, 84.0f, 34.0f};
    const Rectangle cancelAllBounds = {modal.x + modal.width - 118.0f, modal.y + modal.height - 48.0f, 100.0f, 34.0f};

    if (replaceFileButton_.Update(replaceBounds))
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
    else if (cancelReplaceButton_.Update(cancelBounds) || IsKeyPressed(KEY_ESCAPE))
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
    else if (cancelAllReplaceButton_.Update(cancelAllBounds))
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

void DockArea::DrawRightPanel(Rectangle rightPanel, Font font) const
{
    const Rectangle settingsPanel = GetRightSettingsPanel(rightPanel);
    const Rectangle globalPanel = GetGlobalPathPanel(rightPanel);
    const LinkCardNode* selectedCard = GetSelectedCard();
    if (selectedCard != nullptr && !selectedCard->IsValid())
    {
        selectedCard = nullptr;
    }
    const Color text = {224, 230, 224, 255};
    const Color muted = {150, 162, 150, 255};

    DrawTextEx(font, "Options", {settingsPanel.x + 12.0f, settingsPanel.y + 12.0f}, 16.0f, 0.0f, text);

    if (selectedCard == nullptr)
    {
        DrawWrappedText(
            font,
            "Select a valid card to edit download settings.",
            {settingsPanel.x + 14.0f, settingsPanel.y + 58.0f},
            15.0f,
            settingsPanel.width - 28.0f,
            3,
            muted);
    }
    else
    {
        const DownloadOptions& options = selectedCard->Options();
        const DownloaderPanelLayout layout = GetDownloaderPanelLayout(settingsPanel.y);

        DrawTextEx(font, "Format", {settingsPanel.x + 26.0f, layout.formatDropdownY + 4.0f}, 15.0f, 0.0f, muted);
        DrawTextEx(font, "Audio/Video", {settingsPanel.x + 12.0f, layout.mediaDropdownY + 4.0f}, 15.0f, 0.0f, muted);
        DrawTextEx(font, "Quality", {settingsPanel.x + 26.0f, layout.qualityDropdownY + 4.0f}, 15.0f, 0.0f, muted);
        DrawTextEx(font, "Path", {settingsPanel.x + 46.0f, layout.pathRowY}, 15.0f, 0.0f, muted);

        const Rectangle customPathCheckboxBounds = {settingsPanel.x + 94.0f, layout.pathRowY, 18.0f, 18.0f};
        customPathCheckbox_.Draw(customPathCheckboxBounds, font, "Custom path", options.useCustomPath);
        customPathField_.Draw(
            {settingsPanel.x + 14.0f, layout.pathFieldY, settingsPanel.width - 28.0f, 25.0f},
            font,
            options.customPath,
            options.useCustomPath);

        const PredictedDownload prediction = PredictDownload(
            selectedCard->FormatStreams(),
            selectedCard->AvailableVideoFormats(),
            selectedCard->AvailableAudioFormats(),
            selectedCard->AvailableQualities(),
            options);
        DrawDownloadResultPreview(font, settingsPanel, prediction, layout.resultTitleY, layout.resultLineY);

        const Rectangle formatBounds = {settingsPanel.x + 94.0f, layout.formatDropdownY, settingsPanel.width - 108.0f, 25.0f};
        const Rectangle mediaBounds = {settingsPanel.x + 94.0f, layout.mediaDropdownY, settingsPanel.width - 108.0f, 25.0f};
        const Rectangle qualityBounds = {settingsPanel.x + 94.0f, layout.qualityDropdownY, settingsPanel.width - 108.0f, 25.0f};

        fileFormatDropdown_.DrawControl(formatBounds, font, options.fileFormat);
        mediaModeDropdown_.DrawControl(mediaBounds, font, options.mediaMode);
        qualityDropdown_.DrawControl(qualityBounds, font, options.quality, options.mediaMode != 2);

        fileFormatDropdown_.DrawPopup(formatBounds, font, options.fileFormat);
        mediaModeDropdown_.DrawPopup(mediaBounds, font, options.mediaMode);
        if (options.mediaMode != 2)
        {
            qualityDropdown_.DrawPopup(qualityBounds, font, options.quality);
        }
    }

    if (AnyDownloadRunning())
    {
        cancelDownloadButton_.DrawDanger(GetDownloadButtonBounds(settingsPanel), font);
        if (HasDownloadableIdleCards())
        {
            downloadAllButton_.Draw(GetSecondaryActionButtonBounds(settingsPanel), font);
        }
    }
    else
    {
        downloadButton_.Draw(GetDownloadButtonBounds(settingsPanel), font, CanDownloadSelected());
        downloadAllButton_.Draw(GetSecondaryActionButtonBounds(settingsPanel), font, HasValidDownloadCards());
    }

    DrawTextEx(font, "Global Download Path", {globalPanel.x + 10.0f, globalPanel.y + 10.0f}, 16.0f, 0.0f, text);
    globalPathField_.Draw({globalPanel.x + 10.0f, globalPanel.y + 34.0f, globalPanel.width - 20.0f, 26.0f}, font, globalDownloadPath_, true);
}

void DockArea::DrawHeader(Rectangle header, Font font) const
{
    DrawRectangleRec(header, Color{24, 24, 24, 255});

    const auto drawTab = [&](Rectangle bounds, const char* label, bool active) {
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
        const Color background = active
            ? Color{44, 52, 44, 255}
            : (hovered ? Color{36, 42, 36, 255} : Color{24, 24, 24, 255});
        const Color text = active ? Color{232, 238, 232, 255} : Color{168, 178, 168, 255};
        const Vector2 labelSize = MeasureTextEx(font, label, 14.0f, 0.0f);

        DrawRectangleRounded(bounds, 0.18f, 8, background);
        if (active)
        {
            DrawRectangleRec({bounds.x, bounds.y + bounds.height - 2.0f, bounds.width, 2.0f}, Color{104, 150, 104, 255});
        }
        DrawTextEx(
            font,
            label,
            {bounds.x + (bounds.width - labelSize.x) * 0.5f, bounds.y + 2.0f},
            14.0f,
            0.0f,
            text);
    };

    drawTab(HeaderLayout::AboutButton(header.y), "About", false);
    drawTab(HeaderLayout::DownloaderTab(header.y), "Downloader", activeWorkspace_ == Workspace::Downloader);
    drawTab(HeaderLayout::ConverterTab(header.y), "Converter", activeWorkspace_ == Workspace::Converter);

    const float separatorTop = header.y + 5.0f;
    const float separatorBottom = header.y + header.height - 5.0f;
    DrawLineEx(
        {HeaderLayout::kSeparatorX, separatorTop},
        {HeaderLayout::kSeparatorX, separatorBottom},
        1.0f,
        Color{72, 78, 72, 255});
}

namespace
{
enum class FooterNotificationTone {
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

    if (status == "Select an Option" ||
        status == "Download cancelled." ||
        status == "Convert cancelled.")
    {
        return FooterNotificationTone::Hint;
    }

    if (status.rfind("failed", 0) == 0 ||
        status.find("failed") != std::string::npos ||
        status.find("Skipped") != std::string::npos ||
        status.find("No videos") != std::string::npos ||
        status.find("Could not parse") != std::string::npos)
    {
        return FooterNotificationTone::Error;
    }

    return FooterNotificationTone::Success;
}

void GetFooterNotificationColors(
    FooterNotificationTone tone,
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
}

bool DockArea::IsFooterErrorStatus(const std::string& status, bool isRunning)
{
    if (isRunning)
    {
        return false;
    }

    return status.rfind("failed", 0) == 0 ||
        status.find("failed") != std::string::npos ||
        status.find("Skipped") != std::string::npos ||
        status.find("No videos") != std::string::npos ||
        status.find("Could not parse") != std::string::npos;
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

void DockArea::ShowFooterNotification(
    const std::string& text,
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

    if (!footerNotificationVisible_ &&
        footerNotificationShowTime_ > 0.0 &&
        GetTime() >= footerNotificationShowTime_)
    {
        footerNotificationVisible_ = true;
        footerNotificationShowTime_ = -1.0;
        footerNotificationHideTime_ = GetTime() + kFooterNotificationAutoHideSeconds;
    }

    if (footerNotificationVisible_ &&
        footerNotificationHideTime_ > 0.0 &&
        GetTime() >= footerNotificationHideTime_)
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

    const LinkCardNode* card = FindCardByUrl(runner.CurrentUrl());
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

const LinkCardNode* DockArea::FindCardByUrl(const std::string& url) const
{
    for (const LinkCardNode& card : cards_)
    {
        if (card.HasUrl(url))
        {
            return &card;
        }
    }

    return nullptr;
}

void DockArea::AppendFooterDiagnosticsForCard(const std::string& url, const std::string& downloadReport)
{
    const LinkCardNode* card = FindCardByUrl(url);
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
    for (LinkCardNode& card : cards_)
    {
        std::string url;
        std::string error;
        if (card.TryConsumeParseFailure(url, error))
        {
            ShowFooterNotification(
                "Could not parse link",
                FooterNotificationScope::Downloader,
                "Could not parse link: " + url + "\n\n" + error);
            continue;
        }

        if (card.TryConsumeParseSuccess(url))
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

    if (footerNotificationScope_ == FooterNotificationScope::Downloader &&
        activeWorkspace_ != Workspace::Downloader)
    {
        return false;
    }

    if (footerNotificationScope_ == FooterNotificationScope::Converter &&
        activeWorkspace_ != Workspace::Converter)
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

    if (footerCopyVisible_ &&
        CheckCollisionPointRec(GetMousePosition(), footerCopyButtonBounds_) &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        const std::string& text = !footerClipboardLog_.empty()
            ? footerClipboardLog_
            : (errorConsoleLog_.empty() ? status : errorConsoleLog_);
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
    DrawLineEx(
        {bounds.x + padding, bounds.y + padding},
        {bounds.x + bounds.width - padding, bounds.y + bounds.height - padding},
        thickness,
        crossColor);
    DrawLineEx(
        {bounds.x + bounds.width - padding, bounds.y + padding},
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
    const Rectangle backSheet = {
        bounds.x + pad + 3.0f,
        bounds.y + pad + 3.0f,
        sheetWidth,
        sheetHeight};
    const Rectangle frontSheet = {
        bounds.x + pad,
        bounds.y + pad,
        sheetWidth,
        sheetHeight};
    const float roundness = 0.18f;

    DrawRectangleRounded(backSheet, roundness, 6, Color{iconColor.r, iconColor.g, iconColor.b, 90});
    DrawRectangleRounded(frontSheet, roundness, 6, Color{iconColor.r, iconColor.g, iconColor.b, 40});
    DrawRectangleRoundedLines(frontSheet, roundness, 6, iconColor);
    DrawLineEx(
        {frontSheet.x + frontSheet.width * 0.28f, frontSheet.y + frontSheet.height * 0.62f},
        {frontSheet.x + frontSheet.width * 0.72f, frontSheet.y + frontSheet.height * 0.62f},
        1.5f,
        iconColor);
    DrawLineEx(
        {frontSheet.x + frontSheet.width * 0.28f, frontSheet.y + frontSheet.height * 0.78f},
        {frontSheet.x + frontSheet.width * 0.72f, frontSheet.y + frontSheet.height * 0.78f},
        1.5f,
        iconColor);
}

void DockArea::DrawFooter(Rectangle footer, Font font) const
{
    DrawRectangleRec(footer, Color{24, 24, 24, 255});
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
    const std::string displayStatus = TruncateTextToWidth(font, status, fontSize, maxTextAreaWidth - textPaddingX * 2.0f);
    const Vector2 labelSize = MeasureTextEx(font, displayStatus.c_str(), fontSize, 0.0f);
    const float textAreaWidth = std::min(maxTextAreaWidth, labelSize.x + textPaddingX * 2.0f);
    const float totalWidth = chromeWidth + textAreaWidth;
    const float startX = footer.x + (footer.width - totalWidth) * 0.5f;
    const float roundness = 4.0f / barHeight;

    const Rectangle closeButton = {startX, barY, barHeight, barHeight};
    const Rectangle textArea = {startX + barHeight, barY, textAreaWidth, barHeight};
    const Rectangle copyButton = {
        startX + barHeight + textAreaWidth,
        barY,
        barHeight,
        barHeight};
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
    BeginScissorMode(
        static_cast<int>(closeButton.x),
        static_cast<int>(closeButton.y),
        static_cast<int>(closeButton.width),
        static_cast<int>(closeButton.height));
    DrawRectangleRounded(combined, roundness, 10, closeBackground);
    EndScissorMode();
    DrawFooterCloseIcon(closeButton, closeHovered);

    if (showCopyButton)
    {
        BeginScissorMode(
            static_cast<int>(copyButton.x),
            static_cast<int>(copyButton.y),
            static_cast<int>(copyButton.width),
            static_cast<int>(copyButton.height));
        DrawRectangleRounded(combined, roundness, 10, copyBackground);
        EndScissorMode();
        DrawFooterCopyIcon(copyButton, copyHovered);
    }

    DrawRectangleRoundedLines(combined, roundness, 10, border);
    DrawLineEx(
        {startX + barHeight, barY + 2.0f},
        {startX + barHeight, barY + barHeight - 2.0f},
        1.0f,
        border);

    if (showCopyButton)
    {
        DrawLineEx(
            {copyButton.x, barY + 2.0f},
            {copyButton.x, barY + barHeight - 2.0f},
            1.0f,
            border);
    }

    if (isRunning)
    {
        const float progress = useConvertStatus ? 0.35f : 1.0f;
        const Rectangle progressFill = {
            textArea.x + 3.0f,
            textArea.y + textArea.height - 4.0f,
            (textArea.width - 6.0f) * progress,
            2.0f};
        DrawRectangleRounded(progressFill, 1.0f, 4, Color{120, 156, 120, 255});
    }

    const float textY = barY + (barHeight - labelSize.y) * 0.5f;
    BeginScissorMode(
        static_cast<int>(textArea.x + textPaddingX),
        static_cast<int>(textArea.y),
        static_cast<int>(textArea.width - textPaddingX * 2.0f),
        static_cast<int>(textArea.height));
    DrawTextEx(
        font,
        displayStatus.c_str(),
        {textArea.x + textPaddingX, textY},
        fontSize,
        0.0f,
        textColor);
    EndScissorMode();

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
    const Rectangle modal = {
        (static_cast<float>(windowWidth) - modalWidth) * 0.5f,
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
    DrawWrappedText(
        font,
        pendingOverwriteFileName_,
        {modal.x + 18.0f, modal.y + 68.0f},
        14.0f,
        nameMaxWidth,
        2,
        Color{218, 226, 218, 255});

    replaceFileButton_.Draw(replaceBounds, font);
    cancelReplaceButton_.DrawDanger(cancelBounds, font);
    cancelAllReplaceButton_.DrawDanger(cancelAllBounds, font);
}

void DockArea::UpdateAboutDialog(int windowWidth, int windowHeight, Font font)
{
    const AboutDialogMetrics metrics = AboutDialogMetrics::FromWindow(windowWidth, windowHeight);

    if (closeAboutButton_.Update(metrics.okButton) || IsKeyPressed(KEY_ESCAPE))
    {
        isAboutDialogOpen_ = false;
        return;
    }

    if (!IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        return;
    }

    static constexpr std::array<AboutDialogLink, 4> kLinks = {{
        {"https://ffmpeg.org", "https://ffmpeg.org", 142.0f},
        {"https://github.com/epsill0n/ytdown", "https://github.com/epsill0n/ytdown", 192.0f},
        {"https://www.raylib.com", "https://www.raylib.com", 242.0f},
        {
            "http://tinyfiledialogs.sourceforge.net",
            "http://tinyfiledialogs.sourceforge.net",
            292.0f},
    }};

    const Vector2 mouse = GetMousePosition();
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
    const float bodyWidth = modal.width - 36.0f;
    const float itemX = metrics.itemX;

    DrawRectangleRounded(modal, 0.08f, 14, Color{24, 32, 24, 255});
    DrawRectangleRoundedLines(modal, 0.08f, 14, Color{96, 126, 96, 255});
    DrawTextEx(font, "About 4KDowner", {textX, modal.y + 16.0f}, 22.0f, 0.0f, titleColor);
    DrawTextEx(font, "Version 1.0", {textX, modal.y + 44.0f}, 16.0f, 0.0f, mutedColor);
    DrawTextEx(font, "by NickStan", {textX, modal.y + 64.0f}, 15.0f, 0.0f, mutedColor);
    DrawTextEx(font, "Special thanks to:", {textX, modal.y + 92.0f}, 18.0f, 0.0f, bodyColor);

    DrawTextEx(
        font,
        "Fabrice Bellard, creator of FFmpeg",
        {itemX, modal.y + 120.0f},
        16.0f,
        0.0f,
        bodyColor);
    DrawAboutLink(font, "https://ffmpeg.org", modal.y + 142.0f, itemX, linkColor, linkHoverColor);

    DrawTextEx(
        font,
        "epsill0n, creator of ytdown",
        {itemX, modal.y + 170.0f},
        16.0f,
        0.0f,
        bodyColor);
    DrawAboutLink(
        font,
        "https://github.com/epsill0n/ytdown",
        modal.y + 192.0f,
        itemX,
        linkColor,
        linkHoverColor);

    DrawTextEx(font, "the raylib project", {itemX, modal.y + 220.0f}, 16.0f, 0.0f, bodyColor);
    DrawAboutLink(font, "https://www.raylib.com", modal.y + 242.0f, itemX, linkColor, linkHoverColor);

    DrawTextEx(font, "the tinyfiledialogs project", {itemX, modal.y + 270.0f}, 16.0f, 0.0f, bodyColor);
    DrawAboutLink(
        font,
        "http://tinyfiledialogs.sourceforge.net",
        modal.y + 292.0f,
        itemX,
        linkColor,
        linkHoverColor);

    DrawWrappedText(
        font,
        "The AI tools that helped build this app",
        {itemX, modal.y + 320.0f},
        16.0f,
        bodyWidth - 10.0f,
        2,
        bodyColor);

    closeAboutButton_.Draw(okBounds, font);
}

LinkCardNode* DockArea::GetSelectedCard()
{
    for (LinkCardNode& card : cards_)
    {
        if (card.IsSelected())
        {
            return &card;
        }
    }

    return nullptr;
}

const LinkCardNode* DockArea::GetSelectedCard() const
{
    for (const LinkCardNode& card : cards_)
    {
        if (card.IsSelected())
        {
            return &card;
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

void DockArea::ProcessFinishedDownloadRunner(DownloadRunner& runner)
{
    std::string completedDownloadUrl;
    double completedDownloadElapsed = 0.0;
    if (runner.ConsumeCompletedDownload(completedDownloadUrl, completedDownloadElapsed))
    {
        WriteDebugLog("download completed: " + completedDownloadUrl);
        for (LinkCardNode& card : cards_)
        {
            if (card.HasUrl(completedDownloadUrl))
            {
                card.SetDownloadElapsed(completedDownloadElapsed);
                card.SetDownloadBrowserReport(runner.LastDownloadBrowserReport());
                break;
            }
        }
        AppendFooterDiagnosticsForCard(
            completedDownloadUrl,
            runner.LastDownloadBrowserReport());
        if (pendingDownloadQueue_.empty() && !AnyDownloadRunning())
        {
            const double totalElapsed = SumCompletedCardDownloadElapsed(cards_);
            isBatchDownloading_ = false;
            overwriteAllExisting_ = false;
            ClearBatchQueueStates();
            ShowFooterNotification(
                FormatDownloadFinishedStatus(totalElapsed, true),
                FooterNotificationScope::Downloader,
                "",
                footerClipboardLog_);
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
        for (LinkCardNode& card : cards_)
        {
            if (card.IsDownloading() && (cancelledUrl.empty() || card.HasUrl(cancelledUrl)))
            {
                card.ClearDownloading();
            }
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
        for (LinkCardNode& card : cards_)
        {
            if (card.IsDownloading() && (failedUrl.empty() || card.HasUrl(failedUrl)))
            {
                card.SetDownloadBrowserReport(runner.LastDownloadBrowserReport());
                card.ClearDownloading();
            }
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
            ClearBatchQueueStates();
            ShowFooterNotification(
                status,
                FooterNotificationScope::Downloader,
                BuildDownloadFooterErrorLog(runner, status));
            runner.SetStatus("");
        }
        else
        {
            ShowFooterNotification(
                status,
                FooterNotificationScope::Downloader,
                BuildDownloadFooterErrorLog(runner, status));
            runner.SetStatus("");
        }
    }
}

void DockArea::ProcessFinishedConvertRunner(ConvertRunner& runner)
{
    std::string completedConvertPath;
    double completedConvertElapsed = 0.0;
    if (runner.ConsumeCompletedConvert(completedConvertPath, completedConvertElapsed))
    {
        for (ConverterFileCardNode& card : converterCards_)
        {
            if (card.HasFilePath(completedConvertPath))
            {
                card.SetConvertElapsed(completedConvertElapsed);
                break;
            }
        }
        if (isBatchConverting_)
        {
            batchConvertElapsedTotal_ += completedConvertElapsed;
        }
        if (isBatchConverting_ && pendingConvertQueue_.empty() && !AnyConvertRunning())
        {
            isBatchConverting_ = false;
            overwriteAllExisting_ = false;
            ShowFooterNotification(
                FormatConvertFinishedStatus(batchConvertElapsedTotal_, true),
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
            ShowFooterNotification(
                FormatConvertFinishedStatus(completedConvertElapsed, false),
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
            if (card.IsConverting() && (cancelledPath.empty() || card.HasFilePath(cancelledPath)))
            {
                card.ClearConverting();
            }
        }
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
        for (ConverterFileCardNode& card : converterCards_)
        {
            if (card.IsConverting() && (failedPath.empty() || card.HasFilePath(failedPath)))
            {
                card.ClearConverting();
            }
        }
        const std::string status = runner.Status();
        if (isBatchConverting_ && (!pendingConvertQueue_.empty() || AnyConvertRunning()))
        {
            runner.SetStatus("");
        }
        else if (!isBatchConverting_)
        {
            ShowFooterNotification(
                status,
                FooterNotificationScope::Converter,
                BuildConvertFooterErrorLog(runner, status));
            runner.SetStatus("");
        }
        else if (!AnyConvertRunning())
        {
            pendingConvertQueue_.clear();
            isBatchConverting_ = false;
            ShowFooterNotification(
                status,
                FooterNotificationScope::Converter,
                BuildConvertFooterErrorLog(runner, status));
            runner.SetStatus("");
        }
        else
        {
            ShowFooterNotification(
                status,
                FooterNotificationScope::Converter,
                BuildConvertFooterErrorLog(runner, status));
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
            ShowFooterNotification(
                "Could not load video",
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
    if (_dupenv_s(&userProfile, &userProfileSize, "USERPROFILE") == 0 && userProfile != nullptr && userProfile[0] != '\0')
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
