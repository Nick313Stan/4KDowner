#include "LinkCardNode.h"

#include "BrowserDiagnostics.h"
#include "MouseCursor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <utility>
#include <vector>

namespace {
std::string FormatElapsed(double seconds)
{
    const int totalSeconds = static_cast<int>(seconds + 0.5);
    const int minutes = totalSeconds / 60;
    const int secs = totalSeconds % 60;
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, secs);
    return buffer;
}

constexpr float kThumbnailRoundness = 0.12f;
constexpr int kThumbnailSegments = 8;
constexpr float kThumbnailWidth = 82.0f;
constexpr float kThumbnailHeight = kThumbnailWidth * 9.0f / 16.0f;
constexpr int kThumbnailPixelWidth = 164;
constexpr int kThumbnailPixelHeight = 92;

Rectangle GetThumbnailBounds(Rectangle cardBounds)
{
    return {
        cardBounds.x + 8.0f,
        cardBounds.y + (cardBounds.height - kThumbnailHeight) * 0.5f,
        kThumbnailWidth,
        kThumbnailHeight};
}

void PrepareThumbnailImage(Image& image)
{
    if (image.data == nullptr || image.width <= 0 || image.height <= 0)
    {
        return;
    }

    const float targetAspect = 16.0f / 9.0f;
    const float sourceAspect = static_cast<float>(image.width) / static_cast<float>(image.height);

    int cropX = 0;
    int cropY = 0;
    int cropW = image.width;
    int cropH = image.height;
    if (sourceAspect > targetAspect)
    {
        cropW = static_cast<int>(static_cast<float>(image.height) * targetAspect + 0.5f);
        cropW = std::clamp(cropW, 1, image.width);
        cropX = (image.width - cropW) / 2;
    }
    else if (sourceAspect < targetAspect)
    {
        cropH = static_cast<int>(static_cast<float>(image.width) / targetAspect + 0.5f);
        cropH = std::clamp(cropH, 1, image.height);
        cropY = (image.height - cropH) / 2;
    }

    if (cropW != image.width || cropH != image.height)
    {
        ImageCrop(&image, {
            static_cast<float>(cropX),
            static_cast<float>(cropY),
            static_cast<float>(cropW),
            static_cast<float>(cropH)});
    }

    if (image.data == nullptr || image.width <= 0 || image.height <= 0)
    {
        return;
    }

    ImageResize(&image, kThumbnailPixelWidth, kThumbnailPixelHeight);
}
}

LinkCardNode::LinkCardNode(LinkInfo info)
    : info_(std::move(info))
{
}

LinkCardNode::LinkCardNode(std::string url)
    : info_()
{
    info_.url = std::move(url);
    isParsing_ = true;
    loader_.Start(info_.url);
}

LinkCardNode::~LinkCardNode()
{
    if (isParsing_)
    {
        loader_.Cancel();
    }
    UnloadThumbnail();
}

LinkCardNode::LinkCardNode(LinkCardNode&& other) noexcept
    : info_(std::move(other.info_)),
      loader_(std::move(other.loader_)),
      isParsing_(other.isParsing_),
      thumbnailTexture_(other.thumbnailTexture_),
      hasThumbnailTexture_(other.hasThumbnailTexture_),
      triedLoadingThumbnail_(other.triedLoadingThumbnail_),
      isHovered_(other.isHovered_),
      isSelected_(other.isSelected_),
      wasClicked_(other.wasClicked_),
      wasDownloadCancelClicked_(other.wasDownloadCancelClicked_),
      wasRedownloadClicked_(other.wasRedownloadClicked_),
      wasQueueDownloadClicked_(other.wasQueueDownloadClicked_),
      wasQueueCancelClicked_(other.wasQueueCancelClicked_),
      wasCopyClicked_(other.wasCopyClicked_),
      shouldClose_(other.shouldClose_),
      pendingParseErrorReport_(other.pendingParseErrorReport_),
      pendingParseSuccessReport_(other.pendingParseSuccessReport_),
      downloadBrowserReport_(std::move(other.downloadBrowserReport_)),
      options_(std::move(other.options_)),
      downloadElapsedSeconds_(other.downloadElapsedSeconds_),
      hasDownloadElapsed_(other.hasDownloadElapsed_),
      queueStatus_(other.queueStatus_),
      pulseStartTime_(other.pulseStartTime_),
      operationProgress_(other.operationProgress_)
{
    other.thumbnailTexture_ = {};
    other.hasThumbnailTexture_ = false;
    other.isParsing_ = false;
}

LinkCardNode& LinkCardNode::operator=(LinkCardNode&& other) noexcept
{
    if (this != &other)
    {
        UnloadThumbnail();
        info_ = std::move(other.info_);
        loader_ = std::move(other.loader_);
        isParsing_ = other.isParsing_;
        thumbnailTexture_ = other.thumbnailTexture_;
        hasThumbnailTexture_ = other.hasThumbnailTexture_;
        triedLoadingThumbnail_ = other.triedLoadingThumbnail_;
        isHovered_ = other.isHovered_;
        isSelected_ = other.isSelected_;
        wasClicked_ = other.wasClicked_;
        wasDownloadCancelClicked_ = other.wasDownloadCancelClicked_;
        wasRedownloadClicked_ = other.wasRedownloadClicked_;
        wasQueueDownloadClicked_ = other.wasQueueDownloadClicked_;
        wasQueueCancelClicked_ = other.wasQueueCancelClicked_;
        wasCopyClicked_ = other.wasCopyClicked_;
        shouldClose_ = other.shouldClose_;
        pendingParseErrorReport_ = other.pendingParseErrorReport_;
        pendingParseSuccessReport_ = other.pendingParseSuccessReport_;
        downloadBrowserReport_ = std::move(other.downloadBrowserReport_);
        options_ = std::move(other.options_);
        downloadElapsedSeconds_ = other.downloadElapsedSeconds_;
        hasDownloadElapsed_ = other.hasDownloadElapsed_;
        queueStatus_ = other.queueStatus_;
        pulseStartTime_ = other.pulseStartTime_;
        operationProgress_ = other.operationProgress_;

        other.thumbnailTexture_ = {};
        other.hasThumbnailTexture_ = false;
        other.isParsing_ = false;
    }

    return *this;
}

void LinkCardNode::Update(Rectangle bounds)
{
    ApplyParseResultIfReady();
    LoadThumbnail();
    wasClicked_ = false;
    wasDownloadCancelClicked_ = false;
    wasRedownloadClicked_ = false;
    wasQueueDownloadClicked_ = false;
    wasQueueCancelClicked_ = false;
    wasCopyClicked_ = false;

    const Rectangle closeButton = GetCloseButtonBounds(bounds);
    isHovered_ = CheckCollisionPointRec(GetMousePosition(), bounds);
    if (hasDownloadStatusBounds_ &&
        CheckCollisionPointRec(GetMousePosition(), downloadStatusBounds_) &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        if (queueStatus_ == CardQueueStatus::Downloading)
        {
            wasDownloadCancelClicked_ = true;
            return;
        }
        if (queueStatus_ == CardQueueStatus::InQueue)
        {
            wasQueueCancelClicked_ = true;
            return;
        }
        if (queueStatus_ == CardQueueStatus::Cancelled)
        {
            wasRedownloadClicked_ = true;
            return;
        }
        if (queueStatus_ == CardQueueStatus::NotInQueue)
        {
            wasQueueDownloadClicked_ = true;
            return;
        }
    }
    if (CheckCollisionPointRec(GetMousePosition(), closeButton) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        if (isParsing_)
        {
            loader_.Cancel();
        }
        shouldClose_ = true;
        return;
    }

    if (!isParsing_ && HasBrowserDiagnostics())
    {
        const Rectangle copyButton = GetCopyButtonBounds(bounds);
        if (CheckCollisionPointRec(GetMousePosition(), copyButton) &&
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            SetClipboardText(BuildBrowserDiagnosticsReport().c_str());
            wasCopyClicked_ = true;
            return;
        }
    }

    if (isHovered_ && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        wasClicked_ = true;
    }
}

void LinkCardNode::Draw(Rectangle bounds, Font font) const
{
    const Rectangle animatedBounds = GetAnimatedBounds(bounds);
    const Color background = isSelected_ ? Color{17, 30, 17, 255} : (isHovered_ ? Color{14, 26, 14, 255} : Color{10, 18, 10, 255});
    const Color border = isSelected_ ? Color{118, 170, 118, 255} : (isHovered_ ? Color{90, 124, 90, 255} : Color{64, 84, 64, 255});
    const Color titleColor = {240, 244, 240, 255};
    const Color metaColor = {150, 170, 150, 255};
    const float minSide = animatedBounds.width < animatedBounds.height ? animatedBounds.width : animatedBounds.height;
    const float roundness = (13.0f * 2.0f) / minSide;

    DrawRectangleRounded(animatedBounds, roundness, 16, background);
    DrawBackgroundProgress(animatedBounds, roundness);
    DrawRectangleRoundedLines(animatedBounds, roundness, 16, border);
    if (isSelected_)
    {
        DrawRectangleRoundedLines({animatedBounds.x + 1.0f, animatedBounds.y + 1.0f, animatedBounds.width - 2.0f, animatedBounds.height - 2.0f}, roundness, 16, border);
    }

    const Rectangle thumbnailBounds = GetThumbnailBounds(animatedBounds);

    DrawRectangleRounded(thumbnailBounds, kThumbnailRoundness, kThumbnailSegments, {28, 40, 28, 255});
    if (hasThumbnailTexture_)
    {
        const Rectangle source = {
            0.0f,
            0.0f,
            static_cast<float>(thumbnailTexture_.width),
            static_cast<float>(thumbnailTexture_.height)};
        BeginScissorMode(
            static_cast<int>(thumbnailBounds.x),
            static_cast<int>(thumbnailBounds.y),
            static_cast<int>(thumbnailBounds.width),
            static_cast<int>(thumbnailBounds.height));
        DrawTexturePro(thumbnailTexture_, source, thumbnailBounds, {0.0f, 0.0f}, 0.0f, WHITE);
        EndScissorMode();
    }
    else if (isParsing_)
    {
        DrawMiniSpinner({
            thumbnailBounds.x + thumbnailBounds.width * 0.5f,
            thumbnailBounds.y + thumbnailBounds.height * 0.5f});
    }
    DrawRectangleRoundedLines(thumbnailBounds, kThumbnailRoundness, kThumbnailSegments, {64, 84, 64, 255});

    if (isParsing_)
    {
        const float textX = animatedBounds.x + 104.0f;
        const float titleMaxWidth = animatedBounds.width - 138.0f;
        DrawTextEx(font, "Parsing...", {textX, animatedBounds.y + 14.0f}, 18.0f, 0.0f, titleColor);
        DrawWrappedText(font, info_.url, {textX, animatedBounds.y + 38.0f}, 14.5f, titleMaxWidth, 2, metaColor);
        DrawCloseButton(animatedBounds);
        return;
    }

    if (!info_.success)
    {
        DrawTextEx(font, "Could not parse link", {animatedBounds.x + 104.0f, animatedBounds.y + 14.0f}, 18.0f, 0.0f, {232, 160, 150, 255});
        DrawCloseButton(animatedBounds);
        if (HasBrowserDiagnostics())
        {
            DrawCopyButton(animatedBounds);
        }
        return;
    }

    const float textX = animatedBounds.x + 104.0f;
    const float titleMaxWidth = animatedBounds.width - 138.0f;
    DrawWrappedText(font, info_.title, {textX, animatedBounds.y + 10.0f}, 16.0f, titleMaxWidth, 2, titleColor);

    const std::string durationText = info_.duration;
    const std::string details = "Source: YouTube";

    const bool statusHovered =
        (queueStatus_ == CardQueueStatus::Downloading ||
            queueStatus_ == CardQueueStatus::InQueue ||
            queueStatus_ == CardQueueStatus::Cancelled ||
            queueStatus_ == CardQueueStatus::NotInQueue) &&
        hasDownloadStatusBounds_ &&
        CheckCollisionPointRec(GetMousePosition(), downloadStatusBounds_);
    const bool showDownloadSpinner = queueStatus_ == CardQueueStatus::Downloading && !statusHovered;
    std::string statusText;
    switch (queueStatus_)
    {
    case CardQueueStatus::Downloading:
        statusText = statusHovered ? "Cancel" : "downloading";
        break;
    case CardQueueStatus::InQueue:
        statusText = statusHovered ? "Cancel" : "in queue";
        break;
    case CardQueueStatus::NotInQueue:
        statusText = statusHovered ? "Download" : "not in queue";
        break;
    case CardQueueStatus::Cancelled:
        statusText = statusHovered ? "Redownload" : "canceled";
        break;
    case CardQueueStatus::None:
        if (hasDownloadElapsed_)
        {
            statusText = "took " + FormatElapsed(downloadElapsedSeconds_);
        }
        break;
    }
    const bool showStatus = !statusText.empty();

    const float separatorWidth = MeasureTextEx(font, "  |  ", 14.0f, 0.0f).x;
    const float durationWidth = MeasureTextEx(font, durationText.c_str(), 14.0f, 0.0f).x;
    const float downloadingTextWidth = MeasureTextEx(font, "downloading", 14.0f, 0.0f).x;
    const float cancelTextWidth = MeasureTextEx(font, "Cancel", 14.0f, 0.0f).x;
    const float canceledTextWidth = MeasureTextEx(font, "canceled", 14.0f, 0.0f).x;
    const float redownloadTextWidth = MeasureTextEx(font, "Redownload", 14.0f, 0.0f).x;
    const float notInQueueTextWidth = MeasureTextEx(font, "not in queue", 14.0f, 0.0f).x;
    const float queueDownloadTextWidth = MeasureTextEx(font, "Download", 14.0f, 0.0f).x;
    const float inQueueTextWidth = MeasureTextEx(font, "in queue", 14.0f, 0.0f).x;
    const float downloadActionSlotWidth = std::max(downloadingTextWidth + 16.0f, cancelTextWidth);
    const float inQueueActionSlotWidth = std::max(inQueueTextWidth, cancelTextWidth);
    const float cancelledActionSlotWidth = std::max(canceledTextWidth, redownloadTextWidth);
    const float notInQueueActionSlotWidth = std::max(notInQueueTextWidth, queueDownloadTextWidth);
    const float statusTextWidth = showStatus ? MeasureTextEx(font, statusText.c_str(), 14.0f, 0.0f).x : 0.0f;
    float layoutStatusWidth = statusTextWidth;
    if (queueStatus_ == CardQueueStatus::Downloading)
    {
        layoutStatusWidth = downloadActionSlotWidth;
    }
    else if (queueStatus_ == CardQueueStatus::InQueue)
    {
        layoutStatusWidth = inQueueActionSlotWidth;
    }
    else if (queueStatus_ == CardQueueStatus::Cancelled)
    {
        layoutStatusWidth = cancelledActionSlotWidth;
    }
    else if (queueStatus_ == CardQueueStatus::NotInQueue)
    {
        layoutStatusWidth = notInQueueActionSlotWidth;
    }
    const float statusWidth = showStatus ? separatorWidth + layoutStatusWidth : 0.0f;
    const float durationBlockWidth = separatorWidth + 20.0f + durationWidth;
    const float metaMaxX = animatedBounds.x + animatedBounds.width - 34.0f;
    const float maxDetailsWidth = std::max(0.0f, metaMaxX - textX - durationBlockWidth - statusWidth);
    const float detailsWidth = std::min(MeasureTextEx(font, details.c_str(), 14.0f, 0.0f).x, maxDetailsWidth);

    DrawTextEx(font, details.c_str(), {textX, animatedBounds.y + 49.0f}, 14.0f, 0.0f, metaColor);

    const float durationSeparatorX = textX + detailsWidth;
    DrawTextEx(font, "  |  ", {durationSeparatorX, animatedBounds.y + 49.0f}, 14.0f, 0.0f, metaColor);

    const Vector2 clockCenter = {durationSeparatorX + separatorWidth + 8.0f, animatedBounds.y + 58.0f};
    DrawCircleLines(static_cast<int>(clockCenter.x), static_cast<int>(clockCenter.y), 5.0f, metaColor);
    DrawLine(static_cast<int>(clockCenter.x), static_cast<int>(clockCenter.y), static_cast<int>(clockCenter.x), static_cast<int>(clockCenter.y - 3.0f), metaColor);
    DrawLine(static_cast<int>(clockCenter.x), static_cast<int>(clockCenter.y), static_cast<int>(clockCenter.x + 2.0f), static_cast<int>(clockCenter.y + 2.0f), metaColor);

    const Vector2 durationPosition = {clockCenter.x + 12.0f, animatedBounds.y + 49.0f};
    DrawTextEx(font, durationText.c_str(), durationPosition, 14.0f, 0.0f, metaColor);

    hasDownloadStatusBounds_ = false;
    if (showStatus)
    {
        const float separatorX = durationPosition.x + durationWidth + 4.0f;
        DrawTextEx(font, "  |  ", {separatorX, animatedBounds.y + 49.0f}, 14.0f, 0.0f, metaColor);

        const float statusSlotStart = separatorX + separatorWidth + 4.0f;
        const Vector2 statusPosition = {statusSlotStart, animatedBounds.y + 49.0f};
        const Color statusColor =
            queueStatus_ == CardQueueStatus::Cancelled
                ? (statusHovered ? Color{120, 188, 120, 255} : Color{240, 96, 86, 255})
                : queueStatus_ == CardQueueStatus::NotInQueue
                    ? (statusHovered ? Color{120, 188, 120, 255} : metaColor)
                    : (statusHovered ? Color{240, 96, 86, 255} : metaColor);
        if (queueStatus_ == CardQueueStatus::Downloading ||
            queueStatus_ == CardQueueStatus::InQueue ||
            queueStatus_ == CardQueueStatus::Cancelled ||
            queueStatus_ == CardQueueStatus::NotInQueue)
        {
            const float slotWidth = queueStatus_ == CardQueueStatus::Downloading
                ? downloadActionSlotWidth
                : queueStatus_ == CardQueueStatus::InQueue
                    ? inQueueActionSlotWidth
                    : queueStatus_ == CardQueueStatus::Cancelled
                        ? cancelledActionSlotWidth
                        : notInQueueActionSlotWidth;
            downloadStatusBounds_ = {
                statusSlotStart - 3.0f,
                animatedBounds.y + 45.0f,
                slotWidth + 6.0f,
                24.0f};
            hasDownloadStatusBounds_ = true;
        }
        DrawTextEx(font, statusText.c_str(), statusPosition, 14.0f, 0.0f, statusColor);
        if (statusHovered)
        {
            UiCursor::RequestHand();
        }
        if (showDownloadSpinner)
        {
            DrawMiniSpinner({statusPosition.x + downloadingTextWidth + 8.0f, animatedBounds.y + 58.0f});
        }
    }
    DrawCloseButton(animatedBounds);
    if (HasBrowserDiagnostics())
    {
        DrawCopyButton(animatedBounds);
    }
}

void LinkCardNode::TriggerPulse()
{
    pulseStartTime_ = GetTime();
}

void LinkCardNode::SetSelected(bool selected)
{
    isSelected_ = selected;
}

void LinkCardNode::SetDownloading()
{
    queueStatus_ = CardQueueStatus::Downloading;
    hasDownloadElapsed_ = false;
    downloadElapsedSeconds_ = 0.0;
    operationProgress_ = 0.04f;
}

void LinkCardNode::SetQueued()
{
    if (queueStatus_ != CardQueueStatus::Downloading)
    {
        queueStatus_ = CardQueueStatus::InQueue;
    }
    hasDownloadElapsed_ = false;
    downloadElapsedSeconds_ = 0.0;
}

void LinkCardNode::SetNotInQueue()
{
    if (queueStatus_ == CardQueueStatus::Downloading)
    {
        return;
    }
    queueStatus_ = CardQueueStatus::NotInQueue;
}

void LinkCardNode::ClearQueueState()
{
    if (queueStatus_ == CardQueueStatus::InQueue || queueStatus_ == CardQueueStatus::NotInQueue)
    {
        queueStatus_ = CardQueueStatus::None;
    }
}

void LinkCardNode::SetDownloadElapsed(double seconds)
{
    downloadElapsedSeconds_ = seconds;
    hasDownloadElapsed_ = true;
    queueStatus_ = CardQueueStatus::None;
    operationProgress_ = -1.0f;
}

void LinkCardNode::ClearDownloading()
{
    if (queueStatus_ == CardQueueStatus::Downloading)
    {
        queueStatus_ = CardQueueStatus::Cancelled;
        hasDownloadElapsed_ = false;
    }
    operationProgress_ = -1.0f;
}

void LinkCardNode::SetOperationProgress(float progress)
{
    operationProgress_ = std::clamp(progress, 0.0f, 1.0f);
}

void LinkCardNode::ClearOperationProgress()
{
    operationProgress_ = -1.0f;
}

bool LinkCardNode::ShouldClose() const
{
    return shouldClose_;
}

bool LinkCardNode::WasClicked() const
{
    return wasClicked_;
}

bool LinkCardNode::WasDownloadCancelClicked() const
{
    return wasDownloadCancelClicked_;
}

bool LinkCardNode::WasRedownloadClicked() const
{
    return wasRedownloadClicked_;
}

bool LinkCardNode::WasQueueDownloadClicked() const
{
    return wasQueueDownloadClicked_;
}

bool LinkCardNode::WasQueueCancelClicked() const
{
    return wasQueueCancelClicked_;
}

bool LinkCardNode::WasCopyClicked() const
{
    return wasCopyClicked_;
}

bool LinkCardNode::HasUrl(const std::string& url) const
{
    return info_.url == url;
}

bool LinkCardNode::IsDownloading() const
{
    return queueStatus_ == CardQueueStatus::Downloading;
}

bool LinkCardNode::IsCancelled() const
{
    return queueStatus_ == CardQueueStatus::Cancelled;
}

bool LinkCardNode::IsInQueue() const
{
    return queueStatus_ == CardQueueStatus::InQueue;
}

bool LinkCardNode::IsNotInQueue() const
{
    return queueStatus_ == CardQueueStatus::NotInQueue;
}

bool LinkCardNode::HasCompletedDownload() const
{
    return hasDownloadElapsed_;
}

double LinkCardNode::DownloadElapsedSeconds() const
{
    return hasDownloadElapsed_ ? downloadElapsedSeconds_ : 0.0;
}

bool LinkCardNode::IsSelected() const
{
    return isSelected_;
}

bool LinkCardNode::IsValid() const
{
    return !isParsing_ && info_.success;
}

bool LinkCardNode::IsParsing() const
{
    return isParsing_;
}

bool LinkCardNode::TryConsumeParseFailure(std::string& url, std::string& error)
{
    if (!pendingParseErrorReport_)
    {
        return false;
    }

    pendingParseErrorReport_ = false;
    pendingParseSuccessReport_ = false;
    url = info_.url;
    error = info_.error.empty() ? "Unknown parse error." : info_.error;
    if (!info_.errorLog.empty())
    {
        error += "\n\n--- yt-dlp output ---\n\n";
        error += info_.errorLog;
    }
    if (!info_.parseBrowserReport.empty())
    {
        error += "\n\n";
        error += info_.parseBrowserReport;
    }
    return true;
}

bool LinkCardNode::TryConsumeParseSuccess(std::string& url)
{
    if (!pendingParseSuccessReport_)
    {
        return false;
    }

    pendingParseSuccessReport_ = false;
    url = info_.url;
    return true;
}

const std::string& LinkCardNode::ParseBrowserReport() const
{
    return info_.parseBrowserReport;
}

void LinkCardNode::SetDownloadBrowserReport(const std::string& report)
{
    downloadBrowserReport_ = report;
}

bool LinkCardNode::HasBrowserDiagnostics() const
{
    return !info_.parseBrowserReport.empty() || !downloadBrowserReport_.empty();
}

std::string LinkCardNode::BuildBrowserDiagnosticsReport() const
{
    return FormatBrowserSessionReport(
        info_.url,
        info_.title,
        info_.parseBrowserReport,
        downloadBrowserReport_);
}

const std::string& LinkCardNode::Url() const
{
    return info_.url;
}

const std::string& LinkCardNode::Title() const
{
    return info_.title;
}

const std::string& LinkCardNode::NormalizedTitle() const
{
    return info_.normalizedTitle;
}

const std::vector<std::string>& LinkCardNode::AvailableFormats() const
{
    return info_.availableFormats;
}

const std::vector<std::string>& LinkCardNode::AvailableVideoFormats() const
{
    return info_.availableVideoFormats;
}

const std::vector<std::string>& LinkCardNode::AvailableAudioFormats() const
{
    return info_.availableAudioFormats;
}

const std::vector<std::string>& LinkCardNode::AvailableQualities() const
{
    return info_.availableQualities;
}

const std::vector<LinkFormatStream>& LinkCardNode::FormatStreams() const
{
    return info_.formatStreams;
}

DownloadOptions& LinkCardNode::Options()
{
    return options_;
}

const DownloadOptions& LinkCardNode::Options() const
{
    return options_;
}

Rectangle LinkCardNode::GetAnimatedBounds(Rectangle bounds) const
{
    const double elapsed = GetTime() - pulseStartTime_;
    if (elapsed < 0.0 || elapsed > kPulseSeconds)
    {
        return bounds;
    }

    const float progress = static_cast<float>(elapsed / kPulseSeconds);
    const float scale = 1.0f + std::sin(progress * 3.14159265f) * 0.035f;
    const float width = bounds.width * scale;
    const float height = bounds.height * scale;

    return {
        bounds.x - (width - bounds.width) * 0.5f,
        bounds.y - (height - bounds.height) * 0.5f,
        width,
        height};
}

Rectangle LinkCardNode::GetCloseButtonBounds(Rectangle bounds) const
{
    return {
        bounds.x + bounds.width - 28.0f,
        bounds.y + 9.0f,
        18.0f,
        18.0f};
}

Rectangle LinkCardNode::GetCopyButtonBounds(Rectangle bounds) const
{
    return {
        bounds.x + bounds.width - 28.0f,
        bounds.y + bounds.height - 27.0f,
        18.0f,
        18.0f};
}

void LinkCardNode::DrawCopyButton(Rectangle bounds) const
{
    const Rectangle copyButton = GetCopyButtonBounds(bounds);
    const bool isHovered = CheckCollisionPointRec(GetMousePosition(), copyButton);
    const Color iconColor = isHovered ? Color{255, 244, 242, 255} : Color{244, 244, 244, 255};

    const float pad = copyButton.width * 0.24f;
    const float sheetWidth = copyButton.width - pad * 2.0f - 3.0f;
    const float sheetHeight = copyButton.height - pad * 2.0f - 3.0f;
    const Rectangle backSheet = {
        copyButton.x + pad + 3.0f,
        copyButton.y + pad + 3.0f,
        sheetWidth,
        sheetHeight};
    const Rectangle frontSheet = {
        copyButton.x + pad,
        copyButton.y + pad,
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
    if (isHovered)
    {
        UiCursor::RequestHand();
    }
}

void LinkCardNode::LoadThumbnail()
{
    if (hasThumbnailTexture_)
    {
        return;
    }

    if (!info_.success || info_.thumbnailPath.empty())
    {
        return;
    }

    std::error_code error;
    const std::filesystem::path thumbnailPath = std::filesystem::u8path(info_.thumbnailPath);
    if (!std::filesystem::exists(thumbnailPath, error))
    {
        return;
    }

    const std::string extension = thumbnailPath.extension().string();
    if (extension == ".webp" || extension == ".WEBP")
    {
        return;
    }

    Image image = LoadImage(thumbnailPath.string().c_str());
    if (image.data == nullptr)
    {
        triedLoadingThumbnail_ = true;
        return;
    }

    PrepareThumbnailImage(image);
    if (image.data == nullptr || image.width <= 0 || image.height <= 0)
    {
        UnloadImage(image);
        triedLoadingThumbnail_ = true;
        return;
    }

    thumbnailTexture_ = LoadTextureFromImage(image);
    UnloadImage(image);
    hasThumbnailTexture_ = thumbnailTexture_.id != 0;
    triedLoadingThumbnail_ = true;
    if (hasThumbnailTexture_)
    {
        SetTextureFilter(thumbnailTexture_, TEXTURE_FILTER_BILINEAR);
    }
}

void LinkCardNode::UnloadThumbnail()
{
    if (hasThumbnailTexture_)
    {
        UnloadTexture(thumbnailTexture_);
        thumbnailTexture_ = {};
        hasThumbnailTexture_ = false;
    }
}

void LinkCardNode::DrawWrappedText(Font font, const std::string& text, Vector2 position, float fontSize, float maxWidth, int maxLines, Color color) const
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
        DrawTextEx(font, lines[index].c_str(), {position.x, position.y + static_cast<float>(index) * (fontSize + 3.0f)}, fontSize, 0.0f, color);
    }
}

void LinkCardNode::DrawCloseButton(Rectangle bounds) const
{
    const Rectangle closeButton = GetCloseButtonBounds(bounds);
    const bool isHovered = CheckCollisionPointRec(GetMousePosition(), closeButton);
    const Color color = isHovered ? Color{255, 96, 86, 255} : Color{220, 72, 64, 255};
    const float padding = 4.0f;

    DrawLineEx(
        {closeButton.x + padding, closeButton.y + padding},
        {closeButton.x + closeButton.width - padding, closeButton.y + closeButton.height - padding},
        2.0f,
        color);
    DrawLineEx(
        {closeButton.x + closeButton.width - padding, closeButton.y + padding},
        {closeButton.x + padding, closeButton.y + closeButton.height - padding},
        2.0f,
        color);
    if (isHovered)
    {
        UiCursor::RequestHand();
    }
}

Rectangle LinkCardNode::GetDownloadStatusBounds(Rectangle bounds, Font font, float x, const std::string& label) const
{
    const float width = MeasureTextEx(font, label.c_str(), 15.0f, 0.0f).x;
    return {
        x - 3.0f,
        bounds.y + 45.0f,
        width + 6.0f,
        24.0f};
}

void LinkCardNode::DrawBackgroundProgress(Rectangle bounds, float roundness) const
{
    if (operationProgress_ < 0.0f)
    {
        return;
    }

    const float fillWidth = bounds.width * operationProgress_;
    if (fillWidth <= 0.5f)
    {
        return;
    }

    BeginScissorMode(
        static_cast<int>(bounds.x),
        static_cast<int>(bounds.y),
        static_cast<int>(fillWidth),
        static_cast<int>(bounds.height));
    DrawRectangleRounded(bounds, roundness, 16, Color{52, 104, 52, 120});
    EndScissorMode();
}

void LinkCardNode::DrawMiniSpinner(Vector2 center) const
{
    const double time = GetTime();
    const int segments = 8;
    for (int index = 0; index < segments; ++index)
    {
        const float angle = static_cast<float>(time * 6.0 + index * (6.2831853 / segments));
        const float alpha = static_cast<float>(index + 1) / static_cast<float>(segments);
        const Vector2 start = {
            center.x + std::cos(angle) * 4.0f,
            center.y + std::sin(angle) * 4.0f};
        const Vector2 end = {
            center.x + std::cos(angle) * 7.0f,
            center.y + std::sin(angle) * 7.0f};
        DrawLineEx(start, end, 1.5f, Color{160, 178, 160, static_cast<unsigned char>(70 + alpha * 150)});
    }
}

void LinkCardNode::ApplyParseResultIfReady()
{
    if (!isParsing_)
    {
        return;
    }

    loader_.Update();
    if (!loader_.HasResult())
    {
        return;
    }

    const LinkInfo result = loader_.GetResult();
    isParsing_ = false;
    if (result.cancelled)
    {
        shouldClose_ = true;
        return;
    }

    UnloadThumbnail();
    triedLoadingThumbnail_ = false;
    info_ = result;
    if (!result.success)
    {
        pendingParseErrorReport_ = true;
        pendingParseSuccessReport_ = false;
    }
    else
    {
        pendingParseSuccessReport_ = true;
        pendingParseErrorReport_ = false;
    }
}
