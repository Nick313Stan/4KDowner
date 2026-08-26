#include "LinkCardNode.h"

#include "BrowserDiagnostics.h"
#include "CardChrome.h"
#include "MouseCursor.h"
#include "Tooltip.h"
#include "UiClip.h"
#include "VideoTitle.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
std::string SourceSiteName(const std::string& url)
{
    std::string host;
    const auto schemeEnd = url.find("://");
    const size_t hostStart = schemeEnd == std::string::npos ? 0 : schemeEnd + 3;
    const size_t hostEnd = url.find_first_of("/?#", hostStart);
    host = url.substr(hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart);
    for (char& c : host)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (host.rfind("www.", 0) == 0)
    {
        host.erase(0, 4);
    }

    if (host == "youtu.be" || host == "youtube.com" || host == "m.youtube.com" || host == "music.youtube.com" ||
        host.find("youtube.") == 0 || host == "i.ytimg.com" || host.rfind("ytimg.", 0) == 0)
    {
        return "YouTube";
    }
    if (host == "vimeo.com" || host.rfind("vimeo.", 0) == 0)
    {
        return "Vimeo";
    }
    if (host == "twitch.tv" || host.rfind("twitch.", 0) == 0)
    {
        return "Twitch";
    }
    if (host.empty())
    {
        return "Link";
    }

    if (host[0] >= 'a' && host[0] <= 'z')
    {
        host[0] = static_cast<char>(host[0] - 'a' + 'A');
    }
    const auto dot = host.find('.');
    if (dot != std::string::npos)
    {
        host.resize(dot);
    }
    return host;
}

std::string FormatElapsed(double seconds)
{
    if (seconds <= 0.0)
    {
        return "0:00";
    }

    const int totalSeconds = static_cast<int>(seconds + 0.5);
    const int hours = totalSeconds / 3600;
    const int remainder = totalSeconds % 3600;
    const int minutes = remainder / 60;
    const int secs = remainder % 60;
    char buffer[32]{};
    if (hours > 0)
    {
        std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes, secs);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, secs);
    }
    return buffer;
}

double ParseDurationSeconds(const std::string& text)
{
    if (text.empty() || text == "--:--" || text == "NA" || text == "N/A" || text == "na" || text == "n/a")
    {
        return 0.0;
    }

    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    int matched = 0;
#ifdef _MSC_VER
    matched = sscanf_s(text.c_str(), "%d:%d:%d", &hours, &minutes, &seconds);
#else
    matched = std::sscanf(text.c_str(), "%d:%d:%d", &hours, &minutes, &seconds);
#endif
    if (matched == 3)
    {
        return static_cast<double>(hours * 3600 + minutes * 60 + seconds);
    }
#ifdef _MSC_VER
    matched = sscanf_s(text.c_str(), "%d:%d", &minutes, &seconds);
#else
    matched = std::sscanf(text.c_str(), "%d:%d", &minutes, &seconds);
#endif
    if (matched == 2)
    {
        return static_cast<double>(minutes * 60 + seconds);
    }

    try
    {
        const double parsed = std::stod(text);
        return parsed > 0.0 ? parsed : 0.0;
    }
    catch (...)
    {
        return 0.0;
    }
}

constexpr float kThumbnailRoundness = 0.12f;
constexpr int kThumbnailSegments = 8;
constexpr float kThumbnailWidth = 82.0f;
constexpr float kThumbnailHeight = kThumbnailWidth * 9.0f / 16.0f;
constexpr int kThumbnailPixelWidth = 164;
constexpr int kThumbnailPixelHeight = 92;

// Clipboard paste can be multiline junk; DrawTextEx treats '\n' as real line breaks.
std::string SanitizeSingleLineForUi(std::string text)
{
    for (char& ch : text)
    {
        if (ch == '\n' || ch == '\r' || ch == '\t')
        {
            ch = ' ';
        }
    }

    std::string collapsed;
    collapsed.reserve(text.size());
    bool lastWasSpace = false;
    for (const char ch : text)
    {
        if (ch == ' ')
        {
            if (lastWasSpace || collapsed.empty())
            {
                continue;
            }
            lastWasSpace = true;
            collapsed.push_back(' ');
            continue;
        }
        lastWasSpace = false;
        collapsed.push_back(ch);
    }
    while (!collapsed.empty() && collapsed.back() == ' ')
    {
        collapsed.pop_back();
    }
    return collapsed;
}

std::string TruncateTextToWidth(Font font, const std::string& text, float fontSize, float maxWidth)
{
    const std::string singleLine = SanitizeSingleLineForUi(text);
    if (singleLine.empty() || maxWidth <= 0.0f)
    {
        return "...";
    }

    if (MeasureTextEx(font, singleLine.c_str(), fontSize, 0.0f).x <= maxWidth)
    {
        return singleLine;
    }

    const std::string ellipsis = "...";
    if (MeasureTextEx(font, ellipsis.c_str(), fontSize, 0.0f).x > maxWidth)
    {
        return ellipsis;
    }

    size_t low = 0;
    size_t high = singleLine.size();
    while (low < high)
    {
        const size_t mid = (low + high + 1) / 2;
        const std::string candidate = singleLine.substr(0, mid) + ellipsis;
        if (MeasureTextEx(font, candidate.c_str(), fontSize, 0.0f).x <= maxWidth)
        {
            low = mid;
        }
        else
        {
            high = mid - 1;
        }
    }

    return low == 0 ? ellipsis : singleLine.substr(0, low) + ellipsis;
}

Rectangle GetThumbnailBounds(Rectangle cardBounds)
{
    return {cardBounds.x + 8.0f,
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
        ImageCrop(&image,
                  {static_cast<float>(cropX),
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

// Flat Shorts entries sometimes store i.ytimg.com/vi_webp/<id>/... as url — rebuild a watch URL.
void RepairYoutubeThumbnailUrl(LinkInfo& info)
{
    if (info.url.find("ytimg.com/") == std::string::npos && info.url.find("ggpht.com/") == std::string::npos)
    {
        return;
    }

    const auto isLikelyYoutubeVideoId = [](const std::string& id)
    {
        if (id.size() != 11)
        {
            return false;
        }
        for (const char ch : id)
        {
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' ||
                  ch == '_'))
            {
                return false;
            }
        }
        return true;
    };

    std::string videoId;
    static constexpr const char* kMarkers[] = {"/vi_webp/", "/vi/"};
    for (const char* marker : kMarkers)
    {
        const size_t markerPos = info.url.find(marker);
        if (markerPos == std::string::npos)
        {
            continue;
        }
        const size_t idStart = markerPos + std::char_traits<char>::length(marker);
        const size_t idEnd = info.url.find_first_of("/?&", idStart);
        videoId = idEnd == std::string::npos ? info.url.substr(idStart) : info.url.substr(idStart, idEnd - idStart);
        break;
    }

    if (!isLikelyYoutubeVideoId(videoId))
    {
        return;
    }

    info.url = "https://www.youtube.com/watch?v=" + videoId;
}
} // namespace

LinkCardNode::LinkCardNode(LinkInfo info)
    : info_(std::move(info))
{
    RepairYoutubeThumbnailUrl(info_);
    if (info_.success && info_.formatStreams.empty())
    {
        needsDetailedParse_ = true;
    }
}

LinkCardNode::LinkCardNode(std::string url)
    : info_()
{
    info_.url = std::move(url);
    RepairYoutubeThumbnailUrl(info_);
    isParsing_ = true;
    loader_.Start(info_.url);
}

LinkCardNode::~LinkCardNode()
{
    if (isParsing_ || isDetailParsing_)
    {
        loader_.Cancel();
    }
    UnloadThumbnail();
}

LinkCardNode::LinkCardNode(LinkCardNode&& other) noexcept
    : info_(std::move(other.info_)),
      loader_(std::move(other.loader_)),
      isParsing_(other.isParsing_),
      dismissedDuringParse_(other.dismissedDuringParse_),
      thumbnailTexture_(other.thumbnailTexture_),
      hasThumbnailTexture_(other.hasThumbnailTexture_),
      triedLoadingThumbnail_(other.triedLoadingThumbnail_),
      isHovered_(other.isHovered_),
      isSelected_(other.isSelected_),
      wasClicked_(other.wasClicked_),
      wasDownloadCancelClicked_(other.wasDownloadCancelClicked_),
      wasConvertCancelClicked_(other.wasConvertCancelClicked_),
      wasRedownloadClicked_(other.wasRedownloadClicked_),
      wasQueueDownloadClicked_(other.wasQueueDownloadClicked_),
      wasPrioritizeClicked_(other.wasPrioritizeClicked_),
      wasCopyClicked_(other.wasCopyClicked_),
      wasOpenPathClicked_(other.wasOpenPathClicked_),
      wasSourceClicked_(other.wasSourceClicked_),
      shouldClose_(other.shouldClose_),
      pendingParseErrorReport_(other.pendingParseErrorReport_),
      pendingParseSuccessReport_(other.pendingParseSuccessReport_),
      downloadBrowserReport_(std::move(other.downloadBrowserReport_)),
      lastDownloadedPath_(std::move(other.lastDownloadedPath_)),
      expectedOutputDirectory_(std::move(other.expectedOutputDirectory_)),
      expectedFileFormat_(std::move(other.expectedFileFormat_)),
      expectedNormalizedTitle_(std::move(other.expectedNormalizedTitle_)),
      finalOutputDirectory_(std::move(other.finalOutputDirectory_)),
      originalNormalizedTitle_(std::move(other.originalNormalizedTitle_)),
      autoConvertStagingPath_(std::move(other.autoConvertStagingPath_)),
      hasAutoConvertDelivery_(other.hasAutoConvertDelivery_),
      hasAutoConvertSnapshot_(other.hasAutoConvertSnapshot_),
      excludeFromAutoConvert_(other.excludeFromAutoConvert_),
      customAutoConvert_(std::move(other.customAutoConvert_)),
      autoConvertSnapshot_(other.autoConvertSnapshot_),
      options_(std::move(other.options_)),
      downloadElapsedSeconds_(other.downloadElapsedSeconds_),
      convertElapsedSeconds_(other.convertElapsedSeconds_),
      hasDownloadElapsed_(other.hasDownloadElapsed_),
      hasConvertElapsed_(other.hasConvertElapsed_),
      isConverting_(other.isConverting_),
      queueStatus_(other.queueStatus_),
      busyStatusLabel_(std::move(other.busyStatusLabel_)),
      pulseStartTime_(other.pulseStartTime_),
      operationProgress_(other.operationProgress_),
      diskProgress_(other.diskProgress_),
      busySessionStart_(other.busySessionStart_),
      needsDetailedParse_(other.needsDetailedParse_),
      isDetailParsing_(other.isDetailParsing_),
      durationLookupStarted_(other.durationLookupStarted_),
      durationLookupAttempts_(other.durationLookupAttempts_)
{
    other.thumbnailTexture_ = {};
    other.hasThumbnailTexture_ = false;
    other.isParsing_ = false;
    other.isDetailParsing_ = false;
    other.needsDetailedParse_ = false;
    other.durationLookupStarted_ = false;
    other.durationLookupAttempts_ = 0;
    other.dismissedDuringParse_ = false;
    other.isConverting_ = false;
    other.hasAutoConvertDelivery_ = false;
    other.hasAutoConvertSnapshot_ = false;
    other.excludeFromAutoConvert_ = false;
    other.customAutoConvert_ = {};
    other.autoConvertStagingPath_.clear();
    other.autoConvertSnapshot_ = {};
    other.busySessionStart_ = -1.0;
}

LinkCardNode& LinkCardNode::operator=(LinkCardNode&& other) noexcept
{
    if (this != &other)
    {
        UnloadThumbnail();
        info_ = std::move(other.info_);
        loader_ = std::move(other.loader_);
        isParsing_ = other.isParsing_;
        dismissedDuringParse_ = other.dismissedDuringParse_;
        thumbnailTexture_ = other.thumbnailTexture_;
        hasThumbnailTexture_ = other.hasThumbnailTexture_;
        triedLoadingThumbnail_ = other.triedLoadingThumbnail_;
        isHovered_ = other.isHovered_;
        isSelected_ = other.isSelected_;
        wasClicked_ = other.wasClicked_;
        wasDownloadCancelClicked_ = other.wasDownloadCancelClicked_;
        wasConvertCancelClicked_ = other.wasConvertCancelClicked_;
        wasRedownloadClicked_ = other.wasRedownloadClicked_;
        wasQueueDownloadClicked_ = other.wasQueueDownloadClicked_;
        wasPrioritizeClicked_ = other.wasPrioritizeClicked_;
        wasCopyClicked_ = other.wasCopyClicked_;
        wasOpenPathClicked_ = other.wasOpenPathClicked_;
        wasSourceClicked_ = other.wasSourceClicked_;
        shouldClose_ = other.shouldClose_;
        pendingParseErrorReport_ = other.pendingParseErrorReport_;
        pendingParseSuccessReport_ = other.pendingParseSuccessReport_;
        downloadBrowserReport_ = std::move(other.downloadBrowserReport_);
        lastDownloadedPath_ = std::move(other.lastDownloadedPath_);
        expectedOutputDirectory_ = std::move(other.expectedOutputDirectory_);
        expectedFileFormat_ = std::move(other.expectedFileFormat_);
        expectedNormalizedTitle_ = std::move(other.expectedNormalizedTitle_);
        finalOutputDirectory_ = std::move(other.finalOutputDirectory_);
        originalNormalizedTitle_ = std::move(other.originalNormalizedTitle_);
        autoConvertStagingPath_ = std::move(other.autoConvertStagingPath_);
        hasAutoConvertDelivery_ = other.hasAutoConvertDelivery_;
        hasAutoConvertSnapshot_ = other.hasAutoConvertSnapshot_;
        excludeFromAutoConvert_ = other.excludeFromAutoConvert_;
        customAutoConvert_ = std::move(other.customAutoConvert_);
        autoConvertSnapshot_ = other.autoConvertSnapshot_;
        options_ = std::move(other.options_);
        downloadElapsedSeconds_ = other.downloadElapsedSeconds_;
        convertElapsedSeconds_ = other.convertElapsedSeconds_;
        hasDownloadElapsed_ = other.hasDownloadElapsed_;
        hasConvertElapsed_ = other.hasConvertElapsed_;
        isConverting_ = other.isConverting_;
        queueStatus_ = other.queueStatus_;
        busyStatusLabel_ = std::move(other.busyStatusLabel_);
        pulseStartTime_ = other.pulseStartTime_;
        operationProgress_ = other.operationProgress_;
        diskProgress_ = other.diskProgress_;
        busySessionStart_ = other.busySessionStart_;

        needsDetailedParse_ = other.needsDetailedParse_;
        isDetailParsing_ = other.isDetailParsing_;
        durationLookupStarted_ = other.durationLookupStarted_;
        durationLookupAttempts_ = other.durationLookupAttempts_;

        other.thumbnailTexture_ = {};
        other.hasThumbnailTexture_ = false;
        other.isParsing_ = false;
        other.isDetailParsing_ = false;
        other.needsDetailedParse_ = false;
        other.durationLookupStarted_ = false;
        other.durationLookupAttempts_ = 0;
        other.dismissedDuringParse_ = false;
        other.isConverting_ = false;
        other.hasAutoConvertDelivery_ = false;
        other.hasAutoConvertSnapshot_ = false;
        other.excludeFromAutoConvert_ = false;
        other.customAutoConvert_ = {};
        other.autoConvertStagingPath_.clear();
        other.autoConvertSnapshot_ = {};
        other.busySessionStart_ = -1.0;
    }

    return *this;
}

void LinkCardNode::Update(Rectangle bounds, Font font)
{
    (void)font;
    const std::string urlBefore = info_.url;
    RepairYoutubeThumbnailUrl(info_);
    if (info_.url != urlBefore && info_.success && info_.formatStreams.empty())
    {
        needsDetailedParse_ = true;
    }
    ApplyParseResultIfReady();
    LoadThumbnail();
    wasClicked_ = false;
    wasDownloadCancelClicked_ = false;
    wasConvertCancelClicked_ = false;
    wasRedownloadClicked_ = false;
    wasQueueDownloadClicked_ = false;
    wasPrioritizeClicked_ = false;
    wasCopyClicked_ = false;
    wasOpenPathClicked_ = false;
    wasSourceClicked_ = false;

    const Rectangle closeButton = CardChrome::CloseButtonBounds(bounds);
    isHovered_ = CheckCollisionPointRec(GetMousePosition(), bounds);
    if (hasSourceBounds_ && CheckCollisionPointRec(GetMousePosition(), sourceBounds_) &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !info_.url.empty())
    {
        wasSourceClicked_ = true;
        return;
    }
    if (hasDownloadStatusBounds_ && CheckCollisionPointRec(GetMousePosition(), downloadStatusBounds_) &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        if (isConverting_)
        {
            wasConvertCancelClicked_ = true;
            return;
        }
        if (queueStatus_ == CardQueueStatus::Downloading)
        {
            wasDownloadCancelClicked_ = true;
            return;
        }
        if (queueStatus_ == CardQueueStatus::InQueue)
        {
            wasPrioritizeClicked_ = true;
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
        RequestClose();
        return;
    }

    if (!isParsing_ && !IsDownloading() && !IsConverting() && HasBrowserDiagnostics())
    {
        const Rectangle copyButton = CardChrome::CopyButtonBounds(bounds);
        if (CheckCollisionPointRec(GetMousePosition(), copyButton) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            SetClipboardText(BuildBrowserDiagnosticsReport().c_str());
            wasCopyClicked_ = true;
            return;
        }
    }

    if (!isParsing_ && CanRevealOutputPath())
    {
        const Rectangle openPathButton = CardChrome::OpenPathButtonBounds(bounds);
        const Rectangle thumbnailBounds = GetThumbnailBounds(bounds);
        const Vector2 mouse = GetMousePosition();
        if ((CheckCollisionPointRec(mouse, openPathButton) || CheckCollisionPointRec(mouse, thumbnailBounds)) &&
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            wasOpenPathClicked_ = true;
            return;
        }
    }

    if (isHovered_ && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        wasClicked_ = true;
    }
}

void LinkCardNode::Draw(
    Rectangle bounds, Font font, bool highlightExcluded, bool highlightCustom, int displayIndex) const
{
    const Rectangle animatedBounds = CardChrome::AnimatedBounds(bounds, pulseStartTime_, kPulseSeconds);
    const Color background =
        isSelected_ ? Color{17, 30, 17, 255} : (isHovered_ ? Color{14, 26, 14, 255} : Color{10, 18, 10, 255});
    Color border =
        isSelected_ ? Color{118, 170, 118, 255} : (isHovered_ ? Color{90, 124, 90, 255} : Color{64, 84, 64, 255});
    if (excludeFromAutoConvert_ && highlightExcluded)
    {
        if (isSelected_)
        {
            border = Color{230, 200, 80, 255};
        }
        else if (isHovered_)
        {
            border = Color{215, 185, 70, 255};
        }
        else
        {
            border = Color{200, 170, 60, 255};
        }
    }
    else if (highlightCustom)
    {
        if (isSelected_)
        {
            border = Color{90, 150, 230, 255};
        }
        else if (isHovered_)
        {
            border = Color{80, 130, 210, 255};
        }
        else
        {
            border = Color{70, 115, 190, 255};
        }
    }
    const Color titleColor = {240, 244, 240, 255};
    const Color metaColor = {150, 170, 150, 255};
    const float minSide = animatedBounds.width < animatedBounds.height ? animatedBounds.width : animatedBounds.height;
    const float roundness = (13.0f * 2.0f) / minSide;

    DrawRectangleRounded(animatedBounds, roundness, 16, background);
    DrawBackgroundProgress(animatedBounds, roundness);
    if (HasCompletedDownload() && !IsDownloading())
    {
        CardChrome::DrawCompletedCheckmarkBackdrop(animatedBounds);
    }
    DrawRectangleRoundedLines(animatedBounds, roundness, 16, border);
    if (isSelected_)
    {
        DrawRectangleRoundedLines({animatedBounds.x + 1.0f,
                                   animatedBounds.y + 1.0f,
                                   animatedBounds.width - 2.0f,
                                   animatedBounds.height - 2.0f},
                                  roundness,
                                  16,
                                  border);
    }

    const Rectangle thumbnailBounds = GetThumbnailBounds(animatedBounds);

    DrawRectangleRounded(thumbnailBounds, kThumbnailRoundness, kThumbnailSegments, {28, 40, 28, 255});
    if (hasThumbnailTexture_)
    {
        const Rectangle source = {
            0.0f, 0.0f, static_cast<float>(thumbnailTexture_.width), static_cast<float>(thumbnailTexture_.height)};
        DrawTexturePro(thumbnailTexture_, source, thumbnailBounds, {0.0f, 0.0f}, 0.0f, WHITE);
    }
    else if (isParsing_)
    {
        DrawMiniSpinner(
            {thumbnailBounds.x + thumbnailBounds.width * 0.5f, thumbnailBounds.y + thumbnailBounds.height * 0.5f});
    }
    DrawRectangleRoundedLines(thumbnailBounds, kThumbnailRoundness, kThumbnailSegments, {64, 84, 64, 255});
    if ((IsDownloading() || IsConverting()) && busySessionStart_ >= 0.0)
    {
        CardChrome::DrawPreviewElapsedOverlay(font, thumbnailBounds, GetTime() - busySessionStart_);
    }
    CardChrome::DrawPreviewIndexBadge(font, thumbnailBounds, displayIndex);

    if (isParsing_)
    {
        const float textX = animatedBounds.x + CardChrome::kTextXOffset;
        const float titleMaxWidth = CardChrome::TitleMaxWidth(bounds.width);
        const std::string truncatedUrl = TruncateTextToWidth(font, info_.url, 14.5f, titleMaxWidth);
        DrawTextEx(font, "Parsing...", {textX, animatedBounds.y + 14.0f}, 18.0f, 0.0f, titleColor);
        DrawTextEx(font, truncatedUrl.c_str(), {textX, animatedBounds.y + 38.0f}, 14.5f, 0.0f, metaColor);
        CardChrome::DrawCloseButton(animatedBounds, font);
        CardChrome::DrawCopyButton(animatedBounds, font, false);
        CardChrome::DrawOpenPathButton(animatedBounds, font, false);
        hasSourceBounds_ = false;
        hasDownloadStatusBounds_ = false;
        return;
    }

    if (!info_.success)
    {
        DrawTextEx(font,
                   "Could not parse link",
                   {animatedBounds.x + CardChrome::kTextXOffset, animatedBounds.y + 14.0f},
                   18.0f,
                   0.0f,
                   {232, 160, 150, 255});
        CardChrome::DrawCloseButton(animatedBounds, font);
        CardChrome::DrawCopyButton(animatedBounds, font, HasBrowserDiagnostics());
        CardChrome::DrawOpenPathButton(animatedBounds, font, false);
        hasSourceBounds_ = false;
        hasDownloadStatusBounds_ = false;
        return;
    }

    const float textX = animatedBounds.x + CardChrome::kTextXOffset;
    // Title wrap must use the unscaled card width. Animated width is slightly larger during
    // pulse and would reflow the last word onto line 1 for a fraction of a second.
    const float titleMaxWidth = CardChrome::TitleMaxWidth(bounds.width);
    const bool revealPathAvailable = CanRevealOutputPath();
    const Rectangle titleHitBounds = {animatedBounds.x, animatedBounds.y, bounds.width, animatedBounds.height};
    const bool titleHovered = CardChrome::IsTitleTextHovered(titleHitBounds, font, info_.title);
    const bool thumbnailHovered = revealPathAvailable && CheckCollisionPointRec(GetMousePosition(), thumbnailBounds);
    const Color drawTitleColor = titleHovered ? Color{210, 255, 210, 255} : titleColor;
    CardChrome::DrawWrappedText(
        font, info_.title, {textX, animatedBounds.y + 10.0f}, 16.0f, titleMaxWidth, 2, drawTitleColor);
    if (thumbnailHovered)
    {
        UiCursor::RequestHand();
    }
    if (revealPathAvailable)
    {
        Tooltip::DrawIfHovered(font, thumbnailBounds, "Open folder");
    }

    const std::string durationText = info_.duration;
    const bool durationMissing = ParseDurationSeconds(info_.duration) <= 0.0;
    const bool durationFillParsing =
        durationMissing && !info_.url.empty() && (IsDurationLookupPending() || NeedsDurationLookup());
    // Detail/full parse (qualities etc.) — show parsing near duration even when duration is already known.
    const bool detailParsing = isParsing_ || isDetailParsing_;
    const bool durationSlotParsing = durationFillParsing && !detailParsing;
    const bool showParsingStatus = detailParsing || durationFillParsing;
    const std::string details = "Source: " + SourceSiteName(info_.url);

    const bool sourceHovered = hasSourceBounds_ && CheckCollisionPointRec(GetMousePosition(), sourceBounds_);
    const bool statusHovered =
        (isConverting_ || queueStatus_ == CardQueueStatus::Downloading || queueStatus_ == CardQueueStatus::InQueue ||
         queueStatus_ == CardQueueStatus::Cancelled || queueStatus_ == CardQueueStatus::NotInQueue) &&
        hasDownloadStatusBounds_ && CheckCollisionPointRec(GetMousePosition(), downloadStatusBounds_);
    const bool showDownloadSpinner =
        ((isConverting_ || queueStatus_ == CardQueueStatus::Downloading) && !statusHovered);
    std::string statusText;
    if (isConverting_)
    {
        statusText = statusHovered ? "Cancel" : busyStatusLabel_;
    }
    else
    {
        switch (queueStatus_)
        {
        case CardQueueStatus::Downloading:
            statusText = statusHovered ? "Cancel" : busyStatusLabel_;
            break;
        case CardQueueStatus::InQueue:
            statusText = statusHovered ? "Prioritize" : "in queue";
            break;
        case CardQueueStatus::NotInQueue:
            statusText = statusHovered ? "Download" : "not in queue";
            break;
        case CardQueueStatus::Cancelled:
            statusText = statusHovered ? "Redownload" : "canceled";
            break;
        case CardQueueStatus::None:
            if (hasConvertElapsed_ && hasDownloadElapsed_)
            {
                statusText = "took " + FormatElapsed(downloadElapsedSeconds_ + convertElapsedSeconds_);
            }
            else if (hasConvertElapsed_)
            {
                statusText = "took " + FormatElapsed(convertElapsedSeconds_);
            }
            else if (hasDownloadElapsed_)
            {
                statusText = "took " + FormatElapsed(downloadElapsedSeconds_);
            }
            break;
        }
    }
    const bool showStatus = !statusText.empty();

    const float separatorWidth = MeasureTextEx(font, " | ", 14.0f, 0.0f).x;
    const float parsingTextWidth = MeasureTextEx(font, "parsing", 14.0f, 0.0f).x;
    const float durationWidth =
        durationSlotParsing ? (parsingTextWidth + 16.0f) : MeasureTextEx(font, durationText.c_str(), 14.0f, 0.0f).x;
    // When duration is already shown, append "| parsing" + spinner during detail parse.
    const float parsingStatusWidth =
        (showParsingStatus && !durationSlotParsing) ? (separatorWidth + parsingTextWidth + 16.0f) : 0.0f;
    const float downloadingTextWidth = MeasureTextEx(font, "downloading", 14.0f, 0.0f).x;
    const float mergingTextWidth = MeasureTextEx(font, "merging", 14.0f, 0.0f).x;
    const float convertingTextWidth = MeasureTextEx(font, "converting", 14.0f, 0.0f).x;
    const float cancelTextWidth = MeasureTextEx(font, "Cancel", 14.0f, 0.0f).x;
    const float canceledTextWidth = MeasureTextEx(font, "canceled", 14.0f, 0.0f).x;
    const float redownloadTextWidth = MeasureTextEx(font, "Redownload", 14.0f, 0.0f).x;
    const float notInQueueTextWidth = MeasureTextEx(font, "not in queue", 14.0f, 0.0f).x;
    const float queueDownloadTextWidth = MeasureTextEx(font, "Download", 14.0f, 0.0f).x;
    const float inQueueTextWidth = MeasureTextEx(font, "in queue", 14.0f, 0.0f).x;
    const float prioritizeTextWidth = MeasureTextEx(font, "Prioritize", 14.0f, 0.0f).x;
    const float downloadActionSlotWidth = std::max({downloadingTextWidth, mergingTextWidth, cancelTextWidth}) + 16.0f;
    const float convertActionSlotWidth = std::max(convertingTextWidth + 16.0f, cancelTextWidth);
    const float inQueueActionSlotWidth = std::max(inQueueTextWidth, prioritizeTextWidth);
    const float cancelledActionSlotWidth = std::max(canceledTextWidth, redownloadTextWidth);
    const float notInQueueActionSlotWidth = std::max(notInQueueTextWidth, queueDownloadTextWidth);
    const float statusTextWidth = showStatus ? MeasureTextEx(font, statusText.c_str(), 14.0f, 0.0f).x : 0.0f;
    float layoutStatusWidth = statusTextWidth;
    if (isConverting_)
    {
        layoutStatusWidth = convertActionSlotWidth;
    }
    else if (queueStatus_ == CardQueueStatus::Downloading)
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
    const float durationBlockWidth = separatorWidth + 16.0f + durationWidth + parsingStatusWidth;
    const float metaMaxX = animatedBounds.x + animatedBounds.width - 34.0f;
    const float maxDetailsWidth = std::max(0.0f, metaMaxX - textX - durationBlockWidth - statusWidth);
    const float detailsWidth = std::min(MeasureTextEx(font, details.c_str(), 14.0f, 0.0f).x, maxDetailsWidth);

    const Color sourceColor = sourceHovered ? Color{210, 255, 210, 255} : metaColor;
    DrawTextEx(font, details.c_str(), {textX, animatedBounds.y + 49.0f}, 14.0f, 0.0f, sourceColor);
    sourceBounds_ = {textX, animatedBounds.y + 45.0f, detailsWidth, 22.0f};
    hasSourceBounds_ = detailsWidth > 0.0f && !info_.url.empty();
    if (sourceHovered && hasSourceBounds_)
    {
        UiCursor::RequestHand();
    }

    const float durationSeparatorX = textX + detailsWidth;
    DrawTextEx(font, " | ", {durationSeparatorX, animatedBounds.y + 49.0f}, 14.0f, 0.0f, metaColor);

    const Vector2 clockCenter = {durationSeparatorX + separatorWidth + 6.0f, animatedBounds.y + 58.0f};
    DrawCircleLines(static_cast<int>(clockCenter.x), static_cast<int>(clockCenter.y), 5.0f, metaColor);
    DrawLine(static_cast<int>(clockCenter.x),
             static_cast<int>(clockCenter.y),
             static_cast<int>(clockCenter.x),
             static_cast<int>(clockCenter.y - 3.0f),
             metaColor);
    DrawLine(static_cast<int>(clockCenter.x),
             static_cast<int>(clockCenter.y),
             static_cast<int>(clockCenter.x + 2.0f),
             static_cast<int>(clockCenter.y + 2.0f),
             metaColor);

    const Vector2 durationPosition = {clockCenter.x + 10.0f, animatedBounds.y + 49.0f};
    if (durationSlotParsing)
    {
        DrawTextEx(font, "parsing", durationPosition, 14.0f, 0.0f, metaColor);
        DrawMiniSpinner({durationPosition.x + parsingTextWidth + 8.0f, animatedBounds.y + 58.0f});
    }
    else
    {
        DrawTextEx(font, durationText.c_str(), durationPosition, 14.0f, 0.0f, metaColor);
    }

    float afterDurationX = durationPosition.x + durationWidth;
    if (showParsingStatus && !durationSlotParsing)
    {
        DrawTextEx(font, " | ", {afterDurationX + 2.0f, animatedBounds.y + 49.0f}, 14.0f, 0.0f, metaColor);
        const Vector2 parsingPosition = {afterDurationX + 2.0f + separatorWidth, animatedBounds.y + 49.0f};
        DrawTextEx(font, "parsing", parsingPosition, 14.0f, 0.0f, metaColor);
        DrawMiniSpinner({parsingPosition.x + parsingTextWidth + 8.0f, animatedBounds.y + 58.0f});
        afterDurationX = parsingPosition.x + parsingTextWidth + 16.0f;
    }

    const float metaY = animatedBounds.y + 49.0f;
    const float metaHeight = 18.0f;
    Tooltip::DrawIfHovered(font, sourceBounds_, "Open Link");
    Tooltip::DrawIfHovered(font,
                           {clockCenter.x - 6.0f, metaY - 2.0f, afterDurationX - (clockCenter.x - 6.0f), metaHeight},
                           showParsingStatus ? "Parsing video info" : "Duration");

    hasDownloadStatusBounds_ = false;
    if (showStatus)
    {
        const float separatorX = afterDurationX + 2.0f;
        DrawTextEx(font, " | ", {separatorX, animatedBounds.y + 49.0f}, 14.0f, 0.0f, metaColor);

        const float statusSlotStart = separatorX + separatorWidth + 2.0f;
        const Vector2 statusPosition = {statusSlotStart, animatedBounds.y + 49.0f};
        const Color statusColor =
            queueStatus_ == CardQueueStatus::Cancelled && !isConverting_
                ? (statusHovered ? Color{120, 188, 120, 255} : Color{240, 96, 86, 255})
            : (queueStatus_ == CardQueueStatus::NotInQueue || queueStatus_ == CardQueueStatus::InQueue) &&
                    !isConverting_
                ? (statusHovered ? Color{120, 188, 120, 255} : metaColor)
                : (statusHovered ? Color{240, 96, 86, 255} : metaColor);
        if (isConverting_ || queueStatus_ == CardQueueStatus::Downloading || queueStatus_ == CardQueueStatus::InQueue ||
            queueStatus_ == CardQueueStatus::Cancelled || queueStatus_ == CardQueueStatus::NotInQueue)
        {
            const float slotWidth = isConverting_                                  ? convertActionSlotWidth
                                    : queueStatus_ == CardQueueStatus::Downloading ? downloadActionSlotWidth
                                    : queueStatus_ == CardQueueStatus::InQueue     ? inQueueActionSlotWidth
                                    : queueStatus_ == CardQueueStatus::Cancelled   ? cancelledActionSlotWidth
                                                                                   : notInQueueActionSlotWidth;
            downloadStatusBounds_ = {statusSlotStart - 3.0f, animatedBounds.y + 45.0f, slotWidth + 6.0f, 24.0f};
            hasDownloadStatusBounds_ = true;
        }
        DrawTextEx(font, statusText.c_str(), statusPosition, 14.0f, 0.0f, statusColor);
        if (statusHovered)
        {
            UiCursor::RequestHand();
        }
        if (queueStatus_ == CardQueueStatus::InQueue && !isConverting_)
        {
            Tooltip::DrawIfHovered(font, downloadStatusBounds_, "Start now (pauses lowest-progress download)");
        }
        if (showDownloadSpinner)
        {
            const float spinnerTextWidth = MeasureTextEx(font, statusText.c_str(), 14.0f, 0.0f).x;
            DrawMiniSpinner({statusPosition.x + spinnerTextWidth + 8.0f, animatedBounds.y + 58.0f});
        }
    }
    CardChrome::DrawCloseButton(animatedBounds, font);
    CardChrome::DrawCopyButton(animatedBounds, font, !IsDownloading() && !IsConverting() && HasBrowserDiagnostics());
    CardChrome::DrawOpenPathButton(animatedBounds, font, CanRevealOutputPath());
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
    busyStatusLabel_ = "downloading";
    hasDownloadElapsed_ = false;
    downloadElapsedSeconds_ = 0.0;
    operationProgress_ = 0.04f;
    busySessionStart_ = GetTime();
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
    if (!isConverting_)
    {
        operationProgress_ = -1.0f;
    }
}

void LinkCardNode::ClearDownloading()
{
    if (queueStatus_ == CardQueueStatus::Downloading)
    {
        queueStatus_ = CardQueueStatus::Cancelled;
        hasDownloadElapsed_ = false;
    }
    if (!isConverting_)
    {
        operationProgress_ = -1.0f;
        diskProgress_ = -1.0f;
        busySessionStart_ = -1.0;
    }
}

void LinkCardNode::DemoteDownloadingToQueued()
{
    if (queueStatus_ == CardQueueStatus::Downloading)
    {
        queueStatus_ = CardQueueStatus::InQueue;
        hasDownloadElapsed_ = false;
        downloadElapsedSeconds_ = 0.0;
    }
    if (!isConverting_)
    {
        operationProgress_ = -1.0f;
        diskProgress_ = -1.0f;
        busySessionStart_ = -1.0;
    }
}

void LinkCardNode::SetConverting()
{
    isConverting_ = true;
    busyStatusLabel_ = "converting";
    hasConvertElapsed_ = false;
    convertElapsedSeconds_ = 0.0;
    operationProgress_ = 0.04f;
    // Keep download session clock when auto-convert follows download.
    if (busySessionStart_ < 0.0)
    {
        busySessionStart_ = GetTime();
    }
}

void LinkCardNode::ClearConverting()
{
    isConverting_ = false;
    operationProgress_ = -1.0f;
    if (queueStatus_ != CardQueueStatus::Downloading)
    {
        busySessionStart_ = -1.0;
    }
}

void LinkCardNode::SetBusyStatusLabel(std::string label)
{
    if (label.empty())
    {
        return;
    }
    busyStatusLabel_ = std::move(label);
}

const std::string& LinkCardNode::BusyStatusLabel() const
{
    return busyStatusLabel_;
}

void LinkCardNode::SetConvertElapsed(double seconds)
{
    isConverting_ = false;
    hasConvertElapsed_ = true;
    convertElapsedSeconds_ = seconds;
    operationProgress_ = -1.0f;
    busySessionStart_ = -1.0;
}

void LinkCardNode::SetExpectedDownloadOutput(std::string directory, std::string fileFormat, std::string normalizedTitle)
{
    expectedOutputDirectory_ = std::move(directory);
    expectedFileFormat_ = std::move(fileFormat);
    expectedNormalizedTitle_ = std::move(normalizedTitle);
}

void LinkCardNode::SetAutoConvertDelivery(std::string finalDirectory, std::string originalNormalizedTitle)
{
    finalOutputDirectory_ = std::move(finalDirectory);
    originalNormalizedTitle_ = std::move(originalNormalizedTitle);
    hasAutoConvertDelivery_ = true;
}

void LinkCardNode::ClearAutoConvertDelivery()
{
    finalOutputDirectory_.clear();
    originalNormalizedTitle_.clear();
    autoConvertStagingPath_.clear();
    hasAutoConvertDelivery_ = false;
}

void LinkCardNode::SetAutoConvertStagingPath(std::string path)
{
    autoConvertStagingPath_ = std::move(path);
}

const std::string& LinkCardNode::AutoConvertStagingPath() const
{
    return autoConvertStagingPath_;
}

void LinkCardNode::SetAutoConvertSnapshot(AutoConvertOptions options)
{
    autoConvertSnapshot_ = std::move(options);
    hasAutoConvertSnapshot_ = true;
}

void LinkCardNode::ClearAutoConvertSnapshot()
{
    autoConvertSnapshot_ = {};
    hasAutoConvertSnapshot_ = false;
}

bool LinkCardNode::HasAutoConvertSnapshot() const
{
    return hasAutoConvertSnapshot_;
}

const AutoConvertOptions& LinkCardNode::AutoConvertSnapshot() const
{
    return autoConvertSnapshot_;
}

void LinkCardNode::SetLastDownloadedPath(std::string path)
{
    lastDownloadedPath_ = std::move(path);
}

void LinkCardNode::ApplyOriginalTitle(std::string title)
{
    if (title.empty())
    {
        return;
    }
    info_.title = std::move(title);
    info_.normalizedTitle = NormalizeVideoTitle(info_.title);
}

void LinkCardNode::SetOperationProgress(float progress)
{
    operationProgress_ = std::clamp(progress, 0.0f, 1.0f);
}

void LinkCardNode::SetDiskProgress(float progress)
{
    if (progress < 0.0f)
    {
        diskProgress_ = -1.0f;
        return;
    }
    diskProgress_ = std::clamp(progress, 0.0f, 1.0f);
}

void LinkCardNode::ClearOperationProgress()
{
    operationProgress_ = -1.0f;
    diskProgress_ = -1.0f;
}

float LinkCardNode::OperationProgress() const
{
    return operationProgress_;
}

bool LinkCardNode::ShouldClose() const
{
    return shouldClose_;
}

void LinkCardNode::RequestClose()
{
    if (isParsing_ || isDetailParsing_)
    {
        dismissedDuringParse_ = isParsing_;
        loader_.Cancel();
        isParsing_ = false;
        isDetailParsing_ = false;
    }
    shouldClose_ = true;
}

bool LinkCardNode::IsHovered() const
{
    return isHovered_;
}

bool LinkCardNode::WasClicked() const
{
    return wasClicked_;
}

bool LinkCardNode::WasDownloadCancelClicked() const
{
    return wasDownloadCancelClicked_;
}

bool LinkCardNode::WasConvertCancelClicked() const
{
    return wasConvertCancelClicked_;
}

bool LinkCardNode::WasRedownloadClicked() const
{
    return wasRedownloadClicked_;
}

bool LinkCardNode::WasQueueDownloadClicked() const
{
    return wasQueueDownloadClicked_;
}

bool LinkCardNode::WasPrioritizeClicked() const
{
    return wasPrioritizeClicked_;
}

bool LinkCardNode::WasCopyClicked() const
{
    return wasCopyClicked_;
}

bool LinkCardNode::WasOpenPathClicked() const
{
    return wasOpenPathClicked_;
}

bool LinkCardNode::WasSourceClicked() const
{
    return wasSourceClicked_;
}

bool LinkCardNode::HasUrl(const std::string& url) const
{
    return info_.url == url;
}

bool LinkCardNode::HasDownloadedPath(const std::string& path) const
{
    if (path.empty())
    {
        return false;
    }
    return lastDownloadedPath_ == path;
}

bool LinkCardNode::IsDownloading() const
{
    return queueStatus_ == CardQueueStatus::Downloading;
}

bool LinkCardNode::IsConverting() const
{
    return isConverting_;
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
    // Elapsed is set when a download finishes in-session. Path alone covers Load more
    // rematerialization / disk restore after ephemeral host cards were released.
    return hasDownloadElapsed_ || !lastDownloadedPath_.empty();
}

bool LinkCardNode::HasCompletedConvert() const
{
    return hasConvertElapsed_;
}

bool LinkCardNode::HasDownloadElapsedTime() const
{
    return hasDownloadElapsed_;
}

bool LinkCardNode::HasConvertElapsedTime() const
{
    return hasConvertElapsed_;
}

double LinkCardNode::DownloadElapsedSeconds() const
{
    return hasDownloadElapsed_ ? downloadElapsedSeconds_ : 0.0;
}

double LinkCardNode::ConvertElapsedSeconds() const
{
    return hasConvertElapsed_ ? convertElapsedSeconds_ : 0.0;
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
    return isParsing_ || isDetailParsing_;
}

bool LinkCardNode::WasDismissedDuringParse() const
{
    return dismissedDuringParse_;
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
    return FormatBrowserSessionReport(info_.url, info_.title, info_.parseBrowserReport, downloadBrowserReport_);
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

const std::string& LinkCardNode::LastDownloadedPath() const
{
    return lastDownloadedPath_;
}

const std::string& LinkCardNode::ExpectedOutputDirectory() const
{
    return expectedOutputDirectory_;
}

const std::string& LinkCardNode::ExpectedFileFormat() const
{
    return expectedFileFormat_;
}

const std::string& LinkCardNode::ExpectedNormalizedTitle() const
{
    return expectedNormalizedTitle_;
}

const std::string& LinkCardNode::FinalOutputDirectory() const
{
    return finalOutputDirectory_;
}

const std::string& LinkCardNode::OriginalNormalizedTitle() const
{
    return originalNormalizedTitle_;
}

bool LinkCardNode::HasAutoConvertDelivery() const
{
    return hasAutoConvertDelivery_;
}

bool LinkCardNode::IsExcludedFromAutoConvert() const
{
    return excludeFromAutoConvert_;
}

void LinkCardNode::SetExcludedFromAutoConvert(bool excluded)
{
    excludeFromAutoConvert_ = excluded;
}

const AutoConvertOptions& LinkCardNode::CustomAutoConvert() const
{
    return customAutoConvert_;
}

void LinkCardNode::SetCustomAutoConvert(AutoConvertOptions options)
{
    customAutoConvert_ = std::move(options);
}

double LinkCardNode::DurationSeconds() const
{
    return ParseDurationSeconds(info_.duration);
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

const LinkInfo& LinkCardNode::Info() const
{
    return info_;
}

bool LinkCardNode::CanRevealOutputPath() const
{
    // Auto-convert is part of the same job: wait for convert to finish before reveal.
    if ((hasAutoConvertDelivery_ || isConverting_) && !hasConvertElapsed_)
    {
        return false;
    }

    if (!hasDownloadElapsed_ && !hasConvertElapsed_ && lastDownloadedPath_.empty())
    {
        return false;
    }
    return !ResolveOutputPathForReveal().empty();
}

std::string LinkCardNode::ResolveOutputPathForReveal() const
{
    std::error_code error;

    const auto tryFindByStem = [&](const std::filesystem::path& dir, const std::string& stem) -> std::string
    {
        if (stem.empty() || !std::filesystem::is_directory(dir, error))
        {
            return {};
        }
        std::string underscored = stem;
        for (char& ch : underscored)
        {
            if (ch == ' ')
            {
                ch = '_';
            }
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir, error))
        {
            if (error || !entry.is_regular_file(error))
            {
                continue;
            }
            const std::string fileStem = entry.path().stem().u8string();
            if (fileStem == stem || fileStem == underscored)
            {
                return entry.path().u8string();
            }
        }
        return {};
    };

    // After auto-convert, prefer the final delivery path over staging in Documents.
    if (hasAutoConvertDelivery_ && hasConvertElapsed_ && !finalOutputDirectory_.empty())
    {
        const std::filesystem::path finalDir = std::filesystem::u8path(finalOutputDirectory_);
        const std::string converted = tryFindByStem(finalDir, originalNormalizedTitle_);
        if (!converted.empty())
        {
            return converted;
        }
        if (std::filesystem::is_directory(finalDir, error))
        {
            return finalOutputDirectory_;
        }
    }

    if (!lastDownloadedPath_.empty())
    {
        const std::filesystem::path downloaded = std::filesystem::u8path(lastDownloadedPath_);
        if (std::filesystem::exists(downloaded, error))
        {
            return lastDownloadedPath_;
        }

        const std::filesystem::path parent = downloaded.parent_path();
        if (!parent.empty() && std::filesystem::is_directory(parent, error))
        {
            // Don't fall back to staging Documents folder after a completed auto-convert.
            if (!(hasAutoConvertDelivery_ && hasConvertElapsed_))
            {
                return parent.u8string();
            }
        }
    }

    // Prefer converted output in the final delivery folder when present (pre-elapsed edge cases).
    if (hasAutoConvertDelivery_ && !finalOutputDirectory_.empty())
    {
        const std::filesystem::path finalDir = std::filesystem::u8path(finalOutputDirectory_);
        const std::string converted = tryFindByStem(finalDir, originalNormalizedTitle_);
        if (!converted.empty())
        {
            return converted;
        }
    }

    // Staging / expected download folder (auto-convert intermediate or normal download).
    if (!expectedOutputDirectory_.empty())
    {
        const std::filesystem::path expectedDir = std::filesystem::u8path(expectedOutputDirectory_);
        const std::string staged = tryFindByStem(expectedDir, expectedNormalizedTitle_);
        if (!staged.empty())
        {
            return staged;
        }
        if (!expectedNormalizedTitle_.empty() && !expectedFileFormat_.empty() &&
            std::filesystem::is_directory(expectedDir, error))
        {
            std::string ext = expectedFileFormat_;
            for (char& c : ext)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            const std::filesystem::path exact = expectedDir / (expectedNormalizedTitle_ + "." + ext);
            if (std::filesystem::exists(exact, error))
            {
                return exact.u8string();
            }
        }
        if (!(hasAutoConvertDelivery_ && hasConvertElapsed_) && std::filesystem::is_directory(expectedDir, error))
        {
            return expectedOutputDirectory_;
        }
    }

    if (hasAutoConvertDelivery_ && !finalOutputDirectory_.empty())
    {
        const std::filesystem::path finalDir = std::filesystem::u8path(finalOutputDirectory_);
        if (std::filesystem::is_directory(finalDir, error))
        {
            return finalOutputDirectory_;
        }
    }

    return {};
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

Rectangle LinkCardNode::GetDownloadStatusBounds(Rectangle bounds, Font font, float x, const std::string& label) const
{
    const float width = MeasureTextEx(font, label.c_str(), 15.0f, 0.0f).x;
    return {x - 3.0f, bounds.y + 45.0f, width + 6.0f, 24.0f};
}

void LinkCardNode::DrawBackgroundProgress(Rectangle bounds, float roundness) const
{
    if (operationProgress_ < 0.0f && diskProgress_ < 0.0f)
    {
        return;
    }

    const auto drawFill = [&](float progress, Color fillColor)
    {
        if (progress < 0.0f)
        {
            return;
        }

        float fillWidth = bounds.width * progress;
        if (progress > 0.0f && fillWidth < 2.0f)
        {
            fillWidth = 2.0f;
        }
        if (fillWidth <= 0.5f)
        {
            return;
        }

        UiClip::Push({bounds.x, bounds.y, fillWidth, bounds.height});
        DrawRectangleRounded(bounds, roundness, 16, fillColor);
        UiClip::Pop();
    };

    // Underlay: bytes on disk vs estimate (weight).
    if (diskProgress_ >= 0.0f && !isConverting_ && busyStatusLabel_ != "converting")
    {
        drawFill(diskProgress_, Color{54, 74, 54, 255});
    }

    // yt-dlp % overlay disabled for downloads. Convert + merge fills kept.
    if (operationProgress_ >= 0.0f)
    {
        if (isConverting_ || busyStatusLabel_ == "converting")
        {
            drawFill(operationProgress_, Color{70, 120, 180, 180});
        }
        else if (busyStatusLabel_ == "merging")
        {
            // Dark muted mustard (same weight as download forest green).
            drawFill(operationProgress_, Color{74, 68, 40, 255});
        }
    }
}

void LinkCardNode::DrawMiniSpinner(Vector2 center) const
{
    const double time = GetTime();
    const int segments = 8;
    for (int index = 0; index < segments; ++index)
    {
        const float angle = static_cast<float>(time * 6.0 + index * (6.2831853 / segments));
        const float alpha = static_cast<float>(index + 1) / static_cast<float>(segments);
        const Vector2 start = {center.x + std::cos(angle) * 4.0f, center.y + std::sin(angle) * 4.0f};
        const Vector2 end = {center.x + std::cos(angle) * 7.0f, center.y + std::sin(angle) * 7.0f};
        DrawLineEx(start, end, 1.5f, Color{160, 178, 160, static_cast<unsigned char>(70 + alpha * 150)});
    }
}

void LinkCardNode::EnsureDetailedParse()
{
    RepairYoutubeThumbnailUrl(info_);
    if (info_.url.empty())
    {
        needsDetailedParse_ = false;
        return;
    }
    if (!needsDetailedParse_ || isParsing_ || isDetailParsing_ || loader_.IsLoading())
    {
        return;
    }
    needsDetailedParse_ = false;
    isDetailParsing_ = true;
    loader_.Start(info_.url);
}

bool LinkCardNode::NeedsDetailedParse() const
{
    return needsDetailedParse_;
}

void LinkCardNode::FillDurationIfMissing(const std::string& duration)
{
    if (ParseDurationSeconds(info_.duration) > 0.0 || ParseDurationSeconds(duration) <= 0.0)
    {
        return;
    }
    info_.duration = duration;
    durationLookupStarted_ = false;
    durationLookupAttempts_ = 0;
}

bool LinkCardNode::NeedsDurationLookup() const
{
    return !durationLookupStarted_ && durationLookupAttempts_ < 3 && ParseDurationSeconds(info_.duration) <= 0.0 &&
           !info_.url.empty();
}

bool LinkCardNode::IsDurationLookupPending() const
{
    return durationLookupStarted_ && durationLookupAttempts_ < 3 && ParseDurationSeconds(info_.duration) <= 0.0;
}

void LinkCardNode::MarkDurationLookupStarted()
{
    durationLookupStarted_ = true;
}

void LinkCardNode::ClearDurationLookupStarted()
{
    durationLookupStarted_ = false;
}

void LinkCardNode::NoteDurationLookupFailure()
{
    ++durationLookupAttempts_;
    if (durationLookupAttempts_ < 3)
    {
        durationLookupStarted_ = false;
    }
    // After 3 failures keep started=true so we stop spinning and leave "--:--".
}

void LinkCardNode::ApplyParseResultIfReady()
{
    if (!isParsing_ && !isDetailParsing_)
    {
        return;
    }

    loader_.Update();
    if (!loader_.HasResult())
    {
        return;
    }

    const LinkInfo result = loader_.GetResult();

    if (isDetailParsing_)
    {
        isDetailParsing_ = false;
        if (result.cancelled || !result.success)
        {
            return;
        }
        // Flat playlist listings often return translated titles (account language).
        // Full video extract prefers the uploader's original title — use that when present.
        // Keep the flat thumbnail unless the listing had none.
        info_.formatStreams = result.formatStreams;
        info_.availableFormats = result.availableFormats;
        info_.availableVideoFormats = result.availableVideoFormats;
        info_.availableAudioFormats = result.availableAudioFormats;
        info_.availableQualities = result.availableQualities;
        if (!result.title.empty())
        {
            info_.title = result.title;
            info_.normalizedTitle =
                result.normalizedTitle.empty() ? NormalizeVideoTitle(result.title) : result.normalizedTitle;
        }
        FillDurationIfMissing(result.duration);
        if (info_.thumbnailPath.empty() && !result.thumbnailPath.empty())
        {
            UnloadThumbnail();
            triedLoadingThumbnail_ = false;
            info_.thumbnailPath = result.thumbnailPath;
        }
        return;
    }

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
