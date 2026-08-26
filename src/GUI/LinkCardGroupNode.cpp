#include "LinkCardGroupNodeInclude.h"

#include "CardChrome.h"
#include "LinkInfoLoader.h"
#include "MouseCursor.h"
#include "Tooltip.h"
#include "VideoTitle.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace
{
constexpr float kThumbnailWidth = 82.0f;
constexpr float kThumbnailHeight = 46.0f;
constexpr float kThumbnailRoundness = 0.12f;
constexpr int kThumbnailSegments = 8;
constexpr float kChevronWidth = 22.0f;
constexpr int kThumbnailPixelWidth = 164;
constexpr int kThumbnailPixelHeight = 92;
constexpr int kChannelAvatarPixelSize = 96;

std::string SanitizeSingleLineForUi(std::string text)
{
    for (char& c : text)
    {
        if (c == '\n' || c == '\r' || c == '\t')
        {
            c = ' ';
        }
    }
    return text;
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

std::string FormatKindLabel(LinkGroupKind kind, bool playlistShelf = false)
{
    if (playlistShelf)
    {
        return "Playlists";
    }
    return kind == LinkGroupKind::Channel ? "Channel" : "Playlist";
}

std::string FormatVideoCount(int count)
{
    return std::to_string(count) + (count == 1 ? " video" : " videos");
}

std::string FormatPlaylistCount(int count)
{
    return std::to_string(count) + (count == 1 ? " playlist" : " playlists");
}

std::string FormatElapsedTookTime(double seconds)
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

Rectangle GetGroupThumbnailBounds(Rectangle cardBounds)
{
    return {cardBounds.x + 8.0f + kChevronWidth,
            cardBounds.y + (cardBounds.height - kThumbnailHeight) * 0.5f,
            kThumbnailWidth,
            kThumbnailHeight};
}

void DrawMiniSpinner(Vector2 center)
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

    if (image.data != nullptr && image.width > 0 && image.height > 0)
    {
        ImageResize(&image, kThumbnailPixelWidth, kThumbnailPixelHeight);
    }
}

// Keep channel logos square, then punch a circular alpha mask (YouTube-style avatar).
void PrepareChannelAvatarImage(Image& image)
{
    if (image.data == nullptr || image.width <= 0 || image.height <= 0)
    {
        return;
    }

    const int side = std::min(image.width, image.height);
    if (side <= 0)
    {
        return;
    }
    if (side != image.width || side != image.height)
    {
        ImageCrop(&image,
                  {static_cast<float>((image.width - side) / 2),
                   static_cast<float>((image.height - side) / 2),
                   static_cast<float>(side),
                   static_cast<float>(side)});
    }
    if (image.data == nullptr || image.width <= 0 || image.height <= 0)
    {
        return;
    }
    ImageResize(&image, kChannelAvatarPixelSize, kChannelAvatarPixelSize);
    if (image.data == nullptr || image.width <= 0 || image.height <= 0)
    {
        return;
    }

    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Color* pixels = LoadImageColors(image);
    if (pixels == nullptr)
    {
        return;
    }

    const int w = image.width;
    const int h = image.height;
    const float cx = (static_cast<float>(w) - 1.0f) * 0.5f;
    const float cy = (static_cast<float>(h) - 1.0f) * 0.5f;
    const float radius = std::min(cx, cy);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float dist = std::sqrt(dx * dx + dy * dy);
            Color& pixel = pixels[y * w + x];
            if (dist > radius)
            {
                pixel.a = 0;
            }
            else if (dist > radius - 1.25f)
            {
                // Soft edge so the circle doesn't look jagged at UI scale.
                const float t = radius - dist;
                pixel.a = static_cast<unsigned char>(
                    std::clamp(static_cast<int>(static_cast<float>(pixel.a) * (t / 1.25f) + 0.5f), 0, 255));
            }
        }
    }
    std::memcpy(image.data, pixels, static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(Color));
    UnloadImageColors(pixels);
}

void FormatLoadMoreLabel(char* label, size_t labelSize, int remaining)
{
    if (remaining <= 0)
    {
        std::snprintf(label, labelSize, "Load more");
        return;
    }
    if (remaining < static_cast<int>(LinkCardGroupNode::kPageSize))
    {
        std::snprintf(label, labelSize, "Load last %d", remaining);
        return;
    }
    std::snprintf(label, labelSize, "Load %zu more (%d remaining)", LinkCardGroupNode::kPageSize, remaining);
}
} // namespace

namespace
{
std::mutex g_abandonedDurationFuturesMutex;
std::vector<std::future<std::vector<std::pair<std::string, std::string>>>> g_abandonedDurationFutures;

void AbandonDurationFillFuture(std::future<std::vector<std::pair<std::string, std::string>>>& future)
{
    if (!future.valid())
    {
        return;
    }

    if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            future.get();
        }
        catch (...)
        {
        }
        return;
    }

    std::lock_guard<std::mutex> lock(g_abandonedDurationFuturesMutex);
    g_abandonedDurationFutures.push_back(std::move(future));
}
} // namespace

LinkCardGroupNode::LinkCardGroupNode(std::string url)
    : info_()
{
    info_.url = std::move(url);
    // Playlists/channels: number files by default (singles keep DownloadOptions default off).
    options_.keepNumbering = true;
    // Heuristic until flat-parse finishes (yt-dlp often labels channel tabs as playlist).
    if (LooksLikeChannelPlaylistsUrl(info_.url))
    {
        kind_ = LinkGroupKind::Channel;
        info_.kind = LinkGroupKind::Channel;
        info_.hasPlaylistShelf = true;
    }
    else if (LooksLikeChannelUrl(info_.url) && !LooksLikePlaylistUrl(NormalizeYoutubeChannelBaseUrl(info_.url)))
    {
        kind_ = LinkGroupKind::Channel;
        info_.kind = LinkGroupKind::Channel;
    }
    isParsing_ = true;
    loader_.Start(info_.url);
}

LinkCardGroupNode::~LinkCardGroupNode()
{
    if (isParsing_)
    {
        loader_.Cancel();
    }
    shelfPrefetchLoader_.Cancel();
    CancelDurationFill();
    UnloadHeaderThumbnail();
}

LinkCardGroupNode::LinkCardGroupNode(LinkCardGroupNode&& other) noexcept
    : kind_(other.kind_),
      info_(std::move(other.info_)),
      loader_(std::move(other.loader_)),
      shelfPrefetchLoader_(std::move(other.shelfPrefetchLoader_)),
      isParsing_(other.isParsing_),
      expanded_(other.expanded_),
      headerSelected_(other.headerSelected_),
      headerHovered_(other.headerHovered_),
      shouldClose_(other.shouldClose_),
      wasHeaderClicked_(other.wasHeaderClicked_),
      wasExpandToggleClicked_(other.wasExpandToggleClicked_),
      wasLoadMoreClicked_(other.wasLoadMoreClicked_),
      wasCopyClicked_(other.wasCopyClicked_),
      wasSourceClicked_(other.wasSourceClicked_),
      pendingParseErrorReport_(other.pendingParseErrorReport_),
      pendingParseSuccessReport_(other.pendingParseSuccessReport_),
      pendingPromoteSingle_(other.pendingPromoteSingle_),
      promoteSingleInfo_(std::move(other.promoteSingleInfo_)),
      options_(std::move(other.options_)),
      materializedCount_(other.materializedCount_),
      loadedCards_(std::move(other.loadedCards_)),
      channelTabs_(std::move(other.channelTabs_)),
      usesChannelTabs_(other.usesChannelTabs_),
      usesPlaylistShelf_(other.usesPlaylistShelf_),
      shelfPrefetchState_(other.shelfPrefetchState_),
      shelfPrefetchTotal_(other.shelfPrefetchTotal_),
      shelfPrefetchCompleted_(other.shelfPrefetchCompleted_),
      shelfPrefetchWaitingResult_(other.shelfPrefetchWaitingResult_),
      shelfPrefetchLoadStartedAt_(other.shelfPrefetchLoadStartedAt_),
      shelfPrefetchResults_(std::move(other.shelfPrefetchResults_)),
      playlistShelf_(std::move(other.playlistShelf_)),
      headerThumbnail_(other.headerThumbnail_),
      hasHeaderThumbnail_(other.hasHeaderThumbnail_),
      triedHeaderThumbnail_(other.triedHeaderThumbnail_),
      sourceBounds_(other.sourceBounds_),
      hasSourceBounds_(other.hasSourceBounds_),
      pulseStartTime_(other.pulseStartTime_),
      durationFillFuture_(std::move(other.durationFillFuture_)),
      durationFillCancel_(std::move(other.durationFillCancel_)),
      durationFillBatchUrls_(std::move(other.durationFillBatchUrls_)),
      durationFillNextAllowedAt_(other.durationFillNextAllowedAt_),
      durationFillSuspended_(other.durationFillSuspended_),
      detailPrefetchSuspended_(other.detailPrefetchSuspended_),
      batchDownloadExpected_(other.batchDownloadExpected_),
      batchDownloadCompleted_(other.batchDownloadCompleted_),
      batchDownloadElapsedSum_(other.batchDownloadElapsedSum_),
      batchDownloadUrls_(std::move(other.batchDownloadUrls_)),
      batchDownloadFinishedUrls_(std::move(other.batchDownloadFinishedUrls_)),
      entryCompletions_(std::move(other.entryCompletions_))
{
    other.isParsing_ = false;
    other.shelfPrefetchState_ = ShelfPrefetchState::None;
    other.shelfPrefetchTotal_ = 0;
    other.shelfPrefetchCompleted_ = 0;
    other.shelfPrefetchWaitingResult_ = false;
    other.shelfPrefetchLoadStartedAt_ = 0.0;
    other.hasHeaderThumbnail_ = false;
    other.headerThumbnail_ = {};
    other.durationFillNextAllowedAt_ = 0.0;
    other.durationFillSuspended_ = false;
    other.detailPrefetchSuspended_ = false;
    other.batchDownloadExpected_ = 0;
    other.batchDownloadCompleted_ = 0;
    other.batchDownloadElapsedSum_ = 0.0;
}

LinkCardGroupNode& LinkCardGroupNode::operator=(LinkCardGroupNode&& other) noexcept
{
    if (this != &other)
    {
        if (isParsing_)
        {
            loader_.Cancel();
        }
        shelfPrefetchLoader_.Cancel();
        CancelDurationFill();
        UnloadHeaderThumbnail();
        kind_ = other.kind_;
        info_ = std::move(other.info_);
        loader_ = std::move(other.loader_);
        shelfPrefetchLoader_ = std::move(other.shelfPrefetchLoader_);
        isParsing_ = other.isParsing_;
        expanded_ = other.expanded_;
        headerSelected_ = other.headerSelected_;
        headerHovered_ = other.headerHovered_;
        shouldClose_ = other.shouldClose_;
        wasHeaderClicked_ = other.wasHeaderClicked_;
        wasExpandToggleClicked_ = other.wasExpandToggleClicked_;
        wasLoadMoreClicked_ = other.wasLoadMoreClicked_;
        wasCopyClicked_ = other.wasCopyClicked_;
        wasSourceClicked_ = other.wasSourceClicked_;
        pendingParseErrorReport_ = other.pendingParseErrorReport_;
        pendingParseSuccessReport_ = other.pendingParseSuccessReport_;
        pendingPromoteSingle_ = other.pendingPromoteSingle_;
        promoteSingleInfo_ = std::move(other.promoteSingleInfo_);
        options_ = std::move(other.options_);
        materializedCount_ = other.materializedCount_;
        loadedCards_ = std::move(other.loadedCards_);
        channelTabs_ = std::move(other.channelTabs_);
        usesChannelTabs_ = other.usesChannelTabs_;
        usesPlaylistShelf_ = other.usesPlaylistShelf_;
        shelfPrefetchState_ = other.shelfPrefetchState_;
        shelfPrefetchTotal_ = other.shelfPrefetchTotal_;
        shelfPrefetchCompleted_ = other.shelfPrefetchCompleted_;
        shelfPrefetchWaitingResult_ = other.shelfPrefetchWaitingResult_;
        shelfPrefetchLoadStartedAt_ = other.shelfPrefetchLoadStartedAt_;
        shelfPrefetchResults_ = std::move(other.shelfPrefetchResults_);
        playlistShelf_ = std::move(other.playlistShelf_);
        headerThumbnail_ = other.headerThumbnail_;
        hasHeaderThumbnail_ = other.hasHeaderThumbnail_;
        triedHeaderThumbnail_ = other.triedHeaderThumbnail_;
        sourceBounds_ = other.sourceBounds_;
        hasSourceBounds_ = other.hasSourceBounds_;
        pulseStartTime_ = other.pulseStartTime_;
        durationFillFuture_ = std::move(other.durationFillFuture_);
        durationFillCancel_ = std::move(other.durationFillCancel_);
        durationFillBatchUrls_ = std::move(other.durationFillBatchUrls_);
        durationFillNextAllowedAt_ = other.durationFillNextAllowedAt_;
        durationFillSuspended_ = other.durationFillSuspended_;
        detailPrefetchSuspended_ = other.detailPrefetchSuspended_;
        batchDownloadExpected_ = other.batchDownloadExpected_;
        batchDownloadCompleted_ = other.batchDownloadCompleted_;
        batchDownloadElapsedSum_ = other.batchDownloadElapsedSum_;
        batchDownloadUrls_ = std::move(other.batchDownloadUrls_);
        batchDownloadFinishedUrls_ = std::move(other.batchDownloadFinishedUrls_);
        entryCompletions_ = std::move(other.entryCompletions_);
        other.isParsing_ = false;
        other.shelfPrefetchState_ = ShelfPrefetchState::None;
        other.shelfPrefetchTotal_ = 0;
        other.shelfPrefetchCompleted_ = 0;
        other.shelfPrefetchWaitingResult_ = false;
        other.shelfPrefetchLoadStartedAt_ = 0.0;
        other.hasHeaderThumbnail_ = false;
        other.headerThumbnail_ = {};
        other.durationFillNextAllowedAt_ = 0.0;
        other.durationFillSuspended_ = false;
        other.detailPrefetchSuspended_ = false;
        other.batchDownloadExpected_ = 0;
        other.batchDownloadCompleted_ = 0;
        other.batchDownloadElapsedSum_ = 0.0;
    }
    return *this;
}

void LinkCardGroupNode::UnloadHeaderThumbnail()
{
    if (hasHeaderThumbnail_)
    {
        UnloadTexture(headerThumbnail_);
        headerThumbnail_ = {};
        hasHeaderThumbnail_ = false;
    }
}

void LinkCardGroupNode::LoadHeaderThumbnail()
{
    if (triedHeaderThumbnail_ || isParsing_)
    {
        return;
    }
    triedHeaderThumbnail_ = true;
    if (info_.thumbnailPath.empty())
    {
        return;
    }

    std::error_code error;
    if (!std::filesystem::exists(info_.thumbnailPath, error))
    {
        return;
    }

    Image image = LoadImage(info_.thumbnailPath.c_str());
    if (image.data == nullptr)
    {
        return;
    }
    if (kind_ == LinkGroupKind::Channel)
    {
        PrepareChannelAvatarImage(image);
    }
    else
    {
        PrepareThumbnailImage(image);
    }
    if (image.data == nullptr)
    {
        return;
    }
    headerThumbnail_ = LoadTextureFromImage(image);
    UnloadImage(image);
    hasHeaderThumbnail_ = headerThumbnail_.id != 0;
    if (hasHeaderThumbnail_)
    {
        SetTextureFilter(headerThumbnail_, TEXTURE_FILTER_BILINEAR);
    }
}

Rectangle LinkCardGroupNode::GetExpandToggleBounds(Rectangle headerBounds) const
{
    return {headerBounds.x + 4.0f, headerBounds.y, kChevronWidth + 8.0f, headerBounds.height};
}

Rectangle LinkCardGroupNode::GetThumbnailBounds(Rectangle headerBounds) const
{
    if (kind_ == LinkGroupKind::Channel)
    {
        // Diameter matches the old rectangular preview height.
        const float diameter = kThumbnailHeight;
        return {headerBounds.x + 8.0f + kChevronWidth,
                headerBounds.y + (headerBounds.height - diameter) * 0.5f,
                diameter,
                diameter};
    }
    return GetGroupThumbnailBounds(headerBounds);
}

void LinkCardGroupNode::ApplyParseResultIfReady()
{
    if (!isParsing_ || !loader_.HasResult())
    {
        return;
    }

    const LinkGroupInfo result = loader_.GetResult();
    isParsing_ = false;
    if (!result.isGroup)
    {
        if (result.singleVideo.success)
        {
            // Never leave nested shelf rows in empty "Unavailable" via top-level promote.
            // A group loader that resolved one watchable item becomes a 1-entry playlist.
            info_.success = true;
            info_.isGroup = true;
            info_.kind = LinkGroupKind::Playlist;
            info_.hasChannelTabs = false;
            info_.hasPlaylistShelf = false;
            info_.title = result.singleVideo.title.empty() ? info_.url : result.singleVideo.title;
            info_.normalizedTitle = NormalizeVideoTitle(info_.title);
            info_.uploader = result.singleVideo.uploader;
            info_.duration = result.singleVideo.duration;
            info_.thumbnailPath = result.singleVideo.thumbnailPath;
            info_.error.clear();
            LinkGroupEntry entry;
            entry.url = result.singleVideo.url.empty() ? info_.url : result.singleVideo.url;
            entry.title = result.singleVideo.title;
            entry.duration = result.singleVideo.duration;
            entry.thumbnailPath = result.singleVideo.thumbnailPath;
            entry.metadataLoaded = true;
            info_.entries.clear();
            info_.entries.push_back(std::move(entry));
            info_.entryCount = 1;
            usesChannelTabs_ = false;
            usesPlaylistShelf_ = false;
            playlistShelf_.clear();
            materializedCount_ = 0;
            loadedCards_.clear();
            entryCompletions_.clear();
            pendingParseSuccessReport_ = true;
            triedHeaderThumbnail_ = false;
            LoadHeaderThumbnail();
            return;
        }
        info_.success = false;
        info_.error = result.singleVideo.error.empty() ? "Not a playlist or channel." : result.singleVideo.error;
        pendingParseErrorReport_ = true;
        return;
    }

    if (!result.success)
    {
        info_ = result;
        pendingParseErrorReport_ = true;
        return;
    }

    info_ = result;
    kind_ = result.kind;
    usesChannelTabs_ = result.hasChannelTabs;
    usesPlaylistShelf_ = result.hasPlaylistShelf;
    playlistShelf_.clear();
    materializedCount_ = 0;
    loadedCards_.clear();
    entryCompletions_.clear();
    if (usesChannelTabs_)
    {
        channelTabs_[static_cast<size_t>(ChannelContentTab::Videos)].entries = result.videoEntries;
        channelTabs_[static_cast<size_t>(ChannelContentTab::Shorts)].entries = result.shortEntries;
        channelTabs_[static_cast<size_t>(ChannelContentTab::Lives)].entries = result.liveEntries;
    }
    else if (usesPlaylistShelf_)
    {
        BeginShelfPrefetch();
    }
    pendingParseSuccessReport_ = true;
    triedHeaderThumbnail_ = false;
    LoadHeaderThumbnail();
}

void LinkCardGroupNode::MaterializePage(size_t start, size_t end)
{
    if (usesPlaylistShelf_)
    {
        if (!IsPlaylistShelfReady())
        {
            return;
        }
        MaterializePlaylistShelfPage(start, end);
        return;
    }
    end = std::min(end, info_.entries.size());
    for (size_t index = start; index < end; ++index)
    {
        loadedCards_.emplace_back(BuildPartialLinkInfoFromEntry(info_.entries[index]));
        loadedCards_.back().Options() = options_;
        ApplyRememberedCompletionToCard(loadedCards_.back());
    }
    materializedCount_ = loadedCards_.size();
}

void LinkCardGroupNode::MaterializePlaylistShelfPage(size_t start, size_t end)
{
    end = std::min(end, info_.entries.size());
    for (size_t index = start; index < end; ++index)
    {
        PlaylistShelfItem item;
        item.entry = info_.entries[index];
        playlistShelf_.push_back(std::move(item));
    }
    materializedCount_ = playlistShelf_.size();
}

bool LinkCardGroupNode::ShouldPromoteToSingle() const
{
    return pendingPromoteSingle_;
}

LinkInfo LinkCardGroupNode::TakePromoteSingleInfo()
{
    pendingPromoteSingle_ = false;
    return std::move(promoteSingleInfo_);
}

void LinkCardGroupNode::EnsureFirstPageLoaded()
{
    if (usesChannelTabs_)
    {
        return;
    }
    if (usesPlaylistShelf_ && !IsPlaylistShelfReady())
    {
        return;
    }
    if (!expanded_ || !info_.success)
    {
        return;
    }
    const size_t firstPageEnd = std::min(kPageSize, info_.entries.size());
    if (materializedCount_ >= firstPageEnd)
    {
        return;
    }
    MaterializePage(materializedCount_, firstPageEnd);
}

void LinkCardGroupNode::LoadNextPage()
{
    if (usesChannelTabs_ || !info_.success || materializedCount_ >= info_.entries.size())
    {
        return;
    }
    const size_t start = materializedCount_;
    const size_t end = std::min(start + kPageSize, info_.entries.size());
    MaterializePage(start, end);
}

void LinkCardGroupNode::LoadAllPages()
{
    if (usesChannelTabs_)
    {
        for (int tab = 0; tab < kChannelTabCount; ++tab)
        {
            if (channelTabs_[static_cast<size_t>(tab)].entries.empty())
            {
                continue;
            }
            LoadAllPagesForTab(tab);
        }
        return;
    }
    if (!info_.success || materializedCount_ >= info_.entries.size())
    {
        return;
    }
    MaterializePage(materializedCount_, info_.entries.size());
}

void LinkCardGroupNode::CollapseToFirstPage()
{
    if (usesChannelTabs_ || !info_.success || !CanCollapseToFirstPage())
    {
        return;
    }
    const size_t keep = std::min(kPageSize, info_.entries.size());
    if (usesPlaylistShelf_)
    {
        if (playlistShelf_.size() > keep)
        {
            playlistShelf_.erase(playlistShelf_.begin() + static_cast<std::ptrdiff_t>(keep), playlistShelf_.end());
        }
        materializedCount_ = playlistShelf_.size();
        return;
    }
    if (loadedCards_.size() > keep)
    {
        for (size_t index = keep; index < loadedCards_.size(); ++index)
        {
            CaptureCardCompletion(loadedCards_[index]);
        }
        loadedCards_.erase(loadedCards_.begin() + static_cast<std::ptrdiff_t>(keep), loadedCards_.end());
    }
    materializedCount_ = loadedCards_.size();
}

float LinkCardGroupNode::CollapsedHeight() const
{
    return kCardHeight + kCollapsedExtra;
}

float LinkCardGroupNode::ExpandedHeight() const
{
    float height = kCardHeight;
    if (usesChannelTabs_)
    {
        for (int tab = 0; tab < kChannelTabCount; ++tab)
        {
            height += ChannelTabBlockHeight(tab);
        }
        return height;
    }
    if (usesPlaylistShelf_)
    {
        if (!IsPlaylistShelfReady())
        {
            return height;
        }
        for (int index = 0; index < static_cast<int>(playlistShelf_.size()); ++index)
        {
            height += PlaylistShelfItemBlockHeight(index);
        }
        if (ShowsLoadMore() || ShowsCollapseToFirstPage())
        {
            height += kGap + kLoadMoreHeight;
        }
        return height;
    }
    height += static_cast<float>(loadedCards_.size()) * (kCardHeight + kGap);
    if (ShowsLoadMore() || ShowsCollapseToFirstPage())
    {
        height += kGap + kLoadMoreHeight;
    }
    return height;
}

float LinkCardGroupNode::TotalHeight() const
{
    return expanded_ ? ExpandedHeight() : CollapsedHeight();
}

bool LinkCardGroupNode::ShowsLoadMore() const
{
    if (usesChannelTabs_)
    {
        return false;
    }
    if (usesPlaylistShelf_ && !IsPlaylistShelfReady())
    {
        return false;
    }
    return expanded_ && info_.success && materializedCount_ < info_.entries.size();
}

bool LinkCardGroupNode::ShowsCollapseToFirstPage() const
{
    if (usesChannelTabs_)
    {
        return false;
    }
    if (usesPlaylistShelf_ && !IsPlaylistShelfReady())
    {
        return false;
    }
    return expanded_ && info_.success && info_.entries.size() > kPageSize;
}

bool LinkCardGroupNode::CanCollapseToFirstPage() const
{
    return ShowsCollapseToFirstPage() && materializedCount_ > kPageSize;
}

int LinkCardGroupNode::LoadedChildCount() const
{
    if (usesPlaylistShelf_)
    {
        return static_cast<int>(playlistShelf_.size());
    }
    return static_cast<int>(loadedCards_.size());
}

int LinkCardGroupNode::RemainingEntryCount() const
{
    return std::max(0, static_cast<int>(info_.entries.size()) - static_cast<int>(materializedCount_));
}

bool LinkCardGroupNode::IsExpanded() const
{
    return expanded_;
}

void LinkCardGroupNode::SetExpanded(bool expanded)
{
    if (expanded_ == expanded)
    {
        return;
    }
    expanded_ = expanded;
    if (expanded_)
    {
        EnsureFirstPageLoaded();
    }
}

void LinkCardGroupNode::ToggleExpanded()
{
    SetExpanded(!expanded_);
}

bool LinkCardGroupNode::IsHeaderSelected() const
{
    return headerSelected_;
}

void LinkCardGroupNode::SetHeaderSelected(bool selected)
{
    headerSelected_ = selected;
    if (selected)
    {
        ClearChannelTabSelection();
        ClearPlaylistShelfSelection();
        ClearLoadedCardSelection();
    }
}

bool LinkCardGroupNode::IsParsing() const
{
    return isParsing_ || shelfPrefetchState_ == ShelfPrefetchState::Running;
}

bool LinkCardGroupNode::IsValid() const
{
    if (usesPlaylistShelf_ && shelfPrefetchState_ != ShelfPrefetchState::Ready)
    {
        return false;
    }
    return !isParsing_ && info_.success;
}

bool LinkCardGroupNode::IsPlaylistShelfReady() const
{
    return usesPlaylistShelf_ && shelfPrefetchState_ == ShelfPrefetchState::Ready;
}

const std::string& LinkCardGroupNode::Error() const
{
    return info_.error;
}

bool LinkCardGroupNode::ShouldClose() const
{
    return shouldClose_;
}

void LinkCardGroupNode::RequestClose()
{
    shouldClose_ = true;
    if (isParsing_)
    {
        loader_.Cancel();
    }
    if (shelfPrefetchState_ == ShelfPrefetchState::Running)
    {
        shelfPrefetchLoader_.Cancel();
    }
}

bool LinkCardGroupNode::WasHeaderClicked() const
{
    return wasHeaderClicked_;
}

bool LinkCardGroupNode::WasExpandToggleClicked() const
{
    return wasExpandToggleClicked_;
}

bool LinkCardGroupNode::WasLoadMoreClicked() const
{
    return wasLoadMoreClicked_;
}

bool LinkCardGroupNode::WasCopyClicked() const
{
    return wasCopyClicked_;
}

bool LinkCardGroupNode::WasSourceClicked() const
{
    return wasSourceClicked_;
}

bool LinkCardGroupNode::IsHeaderHovered() const
{
    return headerHovered_;
}

bool LinkCardGroupNode::TryToggleExpandShortcut()
{
    if (!IsValid() || IsParsing())
    {
        return false;
    }
    // Avoid conflicting shortcuts with multi-select (Ctrl+A) and other modifiers.
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_ALT) ||
        IsKeyDown(KEY_RIGHT_ALT) || IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
    {
        return false;
    }
    if (!IsKeyPressed(KEY_A))
    {
        return false;
    }
    if (usesChannelTabs_ && expanded_)
    {
        for (int tab = 0; tab < kChannelTabCount; ++tab)
        {
            if (channelTabs_[static_cast<size_t>(tab)].hovered)
            {
                ToggleChannelTabExpanded(tab);
                return true;
            }
        }
    }
    if (usesPlaylistShelf_ && expanded_)
    {
        for (int index = 0; index < static_cast<int>(playlistShelf_.size()); ++index)
        {
            if (playlistShelf_[static_cast<size_t>(index)].hovered)
            {
                TogglePlaylistShelfItemExpanded(index);
                return true;
            }
        }
    }
    if (!headerHovered_)
    {
        return false;
    }
    ToggleExpanded();
    return true;
}

bool LinkCardGroupNode::HasUrl(const std::string& url) const
{
    if (info_.url == url)
    {
        return true;
    }

    // Channel / playlists shelf: treat @handle and /videos|/playlists variants as the same card.
    if (kind_ == LinkGroupKind::Channel && LooksLikeChannelUrl(url))
    {
        const bool urlIsShelf = LooksLikeChannelPlaylistsUrl(url);
        const bool selfIsShelf = usesPlaylistShelf_ || LooksLikeChannelPlaylistsUrl(info_.url);
        if (urlIsShelf == selfIsShelf)
        {
            return NormalizeYoutubeChannelBaseUrl(info_.url) == NormalizeYoutubeChannelBaseUrl(url);
        }
    }

    bool found = false;
    ForEachLoadedCard(
        [&](const LinkCardNode& card)
        {
            if (card.HasUrl(url))
            {
                found = true;
            }
        });
    return found;
}

void LinkCardGroupNode::TriggerPulse()
{
    pulseStartTime_ = GetTime();
}

const std::string& LinkCardGroupNode::Url() const
{
    return info_.url;
}

const std::string& LinkCardGroupNode::Title() const
{
    return info_.title;
}

LinkGroupKind LinkCardGroupNode::Kind() const
{
    return kind_;
}

int LinkCardGroupNode::EntryCount() const
{
    return info_.entryCount > 0 ? info_.entryCount : static_cast<int>(info_.entries.size());
}

const LinkGroupEntry* LinkCardGroupNode::EntryAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(info_.entries.size()))
    {
        return nullptr;
    }
    return &info_.entries[static_cast<size_t>(index)];
}

const LinkGroupEntry* LinkCardGroupNode::ChannelTabEntryAt(int tab, int index) const
{
    const ChannelTabUi* state = TabAt(tab);
    if (state == nullptr || index < 0 || index >= static_cast<int>(state->entries.size()))
    {
        return nullptr;
    }
    return &state->entries[static_cast<size_t>(index)];
}

LinkCardNode* LinkCardGroupNode::FindLoadedCardByUrl(const std::string& url)
{
    LinkCardNode* found = nullptr;
    ForEachLoadedCard(
        [&](LinkCardNode& card)
        {
            if (found == nullptr && card.HasUrl(url))
            {
                found = &card;
            }
        });
    return found;
}

const LinkCardNode* LinkCardGroupNode::FindLoadedCardByUrl(const std::string& url) const
{
    const LinkCardNode* found = nullptr;
    ForEachLoadedCard(
        [&](const LinkCardNode& card)
        {
            if (found == nullptr && card.HasUrl(url))
            {
                found = &card;
            }
        });
    return found;
}

std::vector<LinkCardNode>& LinkCardGroupNode::LoadedCards()
{
    return loadedCards_;
}

const std::vector<LinkCardNode>& LinkCardGroupNode::LoadedCards() const
{
    return loadedCards_;
}

void LinkCardGroupNode::ForEachLoadedCard(const std::function<void(LinkCardNode&)>& visitor)
{
    if (usesChannelTabs_)
    {
        for (auto& tab : channelTabs_)
        {
            for (auto& card : tab.loaded)
            {
                visitor(card);
            }
        }
        return;
    }
    if (usesPlaylistShelf_)
    {
        for (auto& item : playlistShelf_)
        {
            if (item.playlist == nullptr)
            {
                continue;
            }
            for (LinkCardNode& card : item.playlist->LoadedCards())
            {
                visitor(card);
            }
        }
        return;
    }
    for (auto& card : loadedCards_)
    {
        visitor(card);
    }
}

void LinkCardGroupNode::ForEachLoadedCard(const std::function<void(const LinkCardNode&)>& visitor) const
{
    if (usesChannelTabs_)
    {
        for (const auto& tab : channelTabs_)
        {
            for (const auto& card : tab.loaded)
            {
                visitor(card);
            }
        }
        return;
    }
    if (usesPlaylistShelf_)
    {
        for (const auto& item : playlistShelf_)
        {
            if (item.playlist == nullptr)
            {
                continue;
            }
            for (const LinkCardNode& card : item.playlist->LoadedCards())
            {
                visitor(card);
            }
        }
        return;
    }
    for (const auto& card : loadedCards_)
    {
        visitor(card);
    }
}

bool LinkCardGroupNode::UsesChannelTabs() const
{
    return usesChannelTabs_;
}

bool LinkCardGroupNode::UsesPlaylistShelf() const
{
    return usesPlaylistShelf_;
}

LinkCardGroupNode::PlaylistShelfItem* LinkCardGroupNode::ShelfItemAt(int index)
{
    if (index < 0 || index >= static_cast<int>(playlistShelf_.size()))
    {
        return nullptr;
    }
    return &playlistShelf_[static_cast<size_t>(index)];
}

const LinkCardGroupNode::PlaylistShelfItem* LinkCardGroupNode::ShelfItemAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(playlistShelf_.size()))
    {
        return nullptr;
    }
    return &playlistShelf_[static_cast<size_t>(index)];
}

int LinkCardGroupNode::PlaylistShelfCount() const
{
    return static_cast<int>(info_.entries.size());
}

int LinkCardGroupNode::PlaylistShelfLoadedCount() const
{
    if (!IsPlaylistShelfReady())
    {
        return 0;
    }
    return static_cast<int>(playlistShelf_.size());
}

bool LinkCardGroupNode::PlaylistShelfShowsLoadMore() const
{
    return ShowsLoadMore();
}

int LinkCardGroupNode::PlaylistShelfRemainingCount() const
{
    return RemainingEntryCount();
}

bool LinkCardGroupNode::IsPlaylistShelfItemExpanded(int index) const
{
    const PlaylistShelfItem* item = ShelfItemAt(index);
    return item != nullptr && item->expanded;
}

bool LinkCardGroupNode::IsPlaylistShelfItemSelected(int index) const
{
    const PlaylistShelfItem* item = ShelfItemAt(index);
    return item != nullptr && item->selected;
}

bool LinkCardGroupNode::IsPlaylistShelfItemHovered(int index) const
{
    const PlaylistShelfItem* item = ShelfItemAt(index);
    return item != nullptr && item->hovered;
}

bool LinkCardGroupNode::WasPlaylistShelfItemClicked(int index) const
{
    const PlaylistShelfItem* item = ShelfItemAt(index);
    return item != nullptr && item->wasClicked;
}

bool LinkCardGroupNode::WasPlaylistShelfItemExpandClicked(int index) const
{
    const PlaylistShelfItem* item = ShelfItemAt(index);
    return item != nullptr && item->wasExpandClicked;
}

void LinkCardGroupNode::SetPlaylistShelfItemSelected(int index, bool selected)
{
    PlaylistShelfItem* item = ShelfItemAt(index);
    if (item == nullptr)
    {
        return;
    }
    item->selected = selected;
    if (selected)
    {
        headerSelected_ = false;
        ClearLoadedCardSelection();
        for (int i = 0; i < static_cast<int>(playlistShelf_.size()); ++i)
        {
            if (i != index)
            {
                playlistShelf_[static_cast<size_t>(i)].selected = false;
            }
        }
        ClearChannelTabSelection();
    }
}

void LinkCardGroupNode::ClearPlaylistShelfSelection()
{
    for (auto& item : playlistShelf_)
    {
        item.selected = false;
    }
}

int LinkCardGroupNode::SelectedPlaylistShelfIndex() const
{
    for (int i = 0; i < static_cast<int>(playlistShelf_.size()); ++i)
    {
        if (playlistShelf_[static_cast<size_t>(i)].selected)
        {
            return i;
        }
    }
    return -1;
}

void LinkCardGroupNode::EnsurePlaylistShelfItemLoaded(int index)
{
    (void)index;
}

std::string LinkCardGroupNode::NormalizeShelfPlaylistUrl(const LinkGroupEntry& entry)
{
    if (LooksLikeYoutubePlaylistId(entry.id))
    {
        return "https://www.youtube.com/playlist?list=" + entry.id;
    }
    if (entry.url.find("list=") != std::string::npos || entry.url.find("/playlist") != std::string::npos)
    {
        return entry.url;
    }
    return entry.url;
}

LinkCardGroupNode::LinkCardGroupNode(LinkGroupInfo info, AdoptParsedNestedPlaylistTag)
    : kind_(info.kind),
      info_(std::move(info)),
      isParsing_(false),
      expanded_(false)
{
    usesChannelTabs_ = false;
    usesPlaylistShelf_ = false;
    shelfPrefetchState_ = ShelfPrefetchState::None;

    // Group loader may fall through to a single-video parse (empty error, success=false).
    if (!info_.success && info_.singleVideo.success)
    {
        info_.success = true;
        info_.isGroup = true;
        info_.kind = LinkGroupKind::Playlist;
        info_.title = info_.singleVideo.title.empty() ? info_.url : info_.singleVideo.title;
        info_.normalizedTitle = NormalizeVideoTitle(info_.title);
        info_.uploader = info_.singleVideo.uploader;
        info_.duration = info_.singleVideo.duration;
        info_.thumbnailPath = info_.singleVideo.thumbnailPath;
        info_.error.clear();
        LinkGroupEntry entry;
        entry.url = info_.singleVideo.url.empty() ? info_.url : info_.singleVideo.url;
        entry.title = info_.singleVideo.title;
        entry.duration = info_.singleVideo.duration;
        entry.thumbnailPath = info_.singleVideo.thumbnailPath;
        entry.metadataLoaded = true;
        info_.entries.clear();
        info_.entries.push_back(std::move(entry));
        info_.entryCount = 1;
    }
    if (!info_.success && info_.error.empty())
    {
        info_.error = info_.singleVideo.error.empty() ? "Could not load playlist videos." : info_.singleVideo.error;
    }

    if (info_.success && info_.isGroup && !info_.entries.empty())
    {
        MaterializePage(0, std::min(kPageSize, info_.entries.size()));
    }
}

std::unique_ptr<LinkCardGroupNode> LinkCardGroupNode::CreateAdoptedNestedPlaylist(LinkGroupInfo info)
{
    return std::unique_ptr<LinkCardGroupNode>(new LinkCardGroupNode(std::move(info), AdoptParsedNestedPlaylistTag{}));
}

void LinkCardGroupNode::BeginShelfPrefetch()
{
    shelfPrefetchState_ = ShelfPrefetchState::Running;
    shelfPrefetchTotal_ = static_cast<int>(info_.entries.size());
    if (kShelfPrefetchPlaylistLimit > 0)
    {
        shelfPrefetchTotal_ = std::min(shelfPrefetchTotal_, kShelfPrefetchPlaylistLimit);
    }
    shelfPrefetchCompleted_ = 0;
    shelfPrefetchWaitingResult_ = false;
    shelfPrefetchResults_.clear();
    shelfPrefetchResults_.reserve(static_cast<size_t>(shelfPrefetchTotal_));
    playlistShelf_.clear();
    materializedCount_ = 0;
    if (shelfPrefetchTotal_ <= 0)
    {
        shelfPrefetchState_ = ShelfPrefetchState::Ready;
        return;
    }
    StartShelfPrefetchAt(0);
}

void LinkCardGroupNode::StartShelfPrefetchAt(int index)
{
    if (index < 0 || index >= shelfPrefetchTotal_)
    {
        return;
    }
    const std::string url = NormalizeShelfPlaylistUrl(info_.entries[static_cast<size_t>(index)]);
    if (url.empty())
    {
        LinkGroupInfo skipped;
        skipped.url = info_.entries[static_cast<size_t>(index)].url;
        skipped.success = false;
        skipped.error = "Missing playlist URL.";
        shelfPrefetchResults_.push_back(std::move(skipped));
        ++shelfPrefetchCompleted_;
        if (shelfPrefetchCompleted_ >= shelfPrefetchTotal_)
        {
            FinalizePlaylistShelfFromPrefetch();
        }
        else
        {
            StartShelfPrefetchAt(shelfPrefetchCompleted_);
        }
        return;
    }
    shelfPrefetchLoader_.Start(url);
    shelfPrefetchWaitingResult_ = true;
    shelfPrefetchLoadStartedAt_ = GetTime();
}

void LinkCardGroupNode::RecordShelfPrefetchFailure(int index)
{
    LinkGroupInfo failed;
    if (index >= 0 && index < shelfPrefetchTotal_)
    {
        failed.url = NormalizeShelfPlaylistUrl(info_.entries[static_cast<size_t>(index)]);
    }
    failed.isGroup = true;
    failed.success = false;
    failed.error = "Could not prefetch playlist videos.";
    shelfPrefetchResults_.push_back(std::move(failed));
    ++shelfPrefetchCompleted_;
    shelfPrefetchWaitingResult_ = false;
    if (shelfPrefetchCompleted_ >= shelfPrefetchTotal_)
    {
        FinalizePlaylistShelfFromPrefetch();
    }
    else
    {
        StartShelfPrefetchAt(shelfPrefetchCompleted_);
    }
}

void LinkCardGroupNode::PumpShelfPrefetch()
{
    if (shelfPrefetchState_ != ShelfPrefetchState::Running)
    {
        return;
    }

    shelfPrefetchLoader_.Update();
    if (shelfPrefetchLoader_.IsLoading())
    {
        return;
    }

    if (shelfPrefetchLoader_.HasResult())
    {
        shelfPrefetchResults_.push_back(shelfPrefetchLoader_.GetResult());
        ++shelfPrefetchCompleted_;
        shelfPrefetchWaitingResult_ = false;
        if (shelfPrefetchCompleted_ >= shelfPrefetchTotal_)
        {
            FinalizePlaylistShelfFromPrefetch();
            return;
        }
        StartShelfPrefetchAt(shelfPrefetchCompleted_);
        return;
    }

    if (shelfPrefetchWaitingResult_)
    {
        RecordShelfPrefetchFailure(shelfPrefetchCompleted_);
    }
}

void LinkCardGroupNode::FinalizePlaylistShelfFromPrefetch()
{
    playlistShelf_.clear();
    for (int index = 0; index < shelfPrefetchTotal_; ++index)
    {
        PlaylistShelfItem item;
        item.entry = info_.entries[static_cast<size_t>(index)];
        if (index < static_cast<int>(shelfPrefetchResults_.size()))
        {
            item.playlist = CreateAdoptedNestedPlaylist(std::move(shelfPrefetchResults_[static_cast<size_t>(index)]));
            item.expanded = false;
            if (item.playlist != nullptr)
            {
                item.playlist->SetExpanded(false);
                if (item.playlist->info_.thumbnailPath.empty() && !item.playlist->info_.entries.empty())
                {
                    item.playlist->info_.thumbnailPath = item.playlist->info_.entries.front().thumbnailPath;
                }
                item.playlist->LoadHeaderThumbnail();
            }
        }
        playlistShelf_.push_back(std::move(item));
    }
    materializedCount_ = playlistShelf_.size();
    shelfPrefetchState_ = ShelfPrefetchState::Ready;
    shelfPrefetchWaitingResult_ = false;
    shelfPrefetchResults_.clear();
    shelfPrefetchLoader_.Cancel();
}

void LinkCardGroupNode::PumpShelfSelectedDetailParse()
{
    if (!IsPlaylistShelfReady() || detailPrefetchSuspended_)
    {
        return;
    }

    int inFlight = 0;
    std::vector<LinkCardNode*> selected;
    selected.reserve(32);
    ForEachLoadedCard(
        [&](LinkCardNode& card)
        {
            card.ApplyParseResultIfReady();
            if (card.IsParsing())
            {
                ++inFlight;
            }
            if (card.IsSelected())
            {
                selected.push_back(&card);
            }
        });

    if (inFlight >= kShelfSelectedDetailParseParallel)
    {
        return;
    }

    for (LinkCardNode* card : selected)
    {
        if (inFlight >= kShelfSelectedDetailParseParallel)
        {
            break;
        }
        if (!card->NeedsDetailedParse() || card->IsParsing())
        {
            continue;
        }
        card->EnsureDetailedParse();
        if (card->IsParsing())
        {
            ++inFlight;
        }
    }
}

void LinkCardGroupNode::SetPlaylistShelfItemExpanded(int index, bool expanded)
{
    PlaylistShelfItem* item = ShelfItemAt(index);
    if (item == nullptr)
    {
        return;
    }
    item->expanded = expanded;
    if (expanded && item->playlist != nullptr)
    {
        item->playlist->SetExpanded(true);
    }
    else if (!expanded && item->playlist != nullptr)
    {
        item->playlist->SetExpanded(false);
    }
}

void LinkCardGroupNode::TogglePlaylistShelfItemExpanded(int index)
{
    if (ShelfItemAt(index) == nullptr)
    {
        return;
    }
    SetPlaylistShelfItemExpanded(index, !IsPlaylistShelfItemExpanded(index));
}

LinkCardGroupNode* LinkCardGroupNode::PlaylistShelfPlaylist(int index)
{
    PlaylistShelfItem* item = ShelfItemAt(index);
    return item != nullptr ? item->playlist.get() : nullptr;
}

const LinkCardGroupNode* LinkCardGroupNode::PlaylistShelfPlaylist(int index) const
{
    const PlaylistShelfItem* item = ShelfItemAt(index);
    return item != nullptr ? item->playlist.get() : nullptr;
}

const LinkGroupEntry* LinkCardGroupNode::PlaylistShelfEntry(int index) const
{
    const PlaylistShelfItem* item = ShelfItemAt(index);
    return item != nullptr ? &item->entry : nullptr;
}

float LinkCardGroupNode::PlaylistShelfItemBlockHeight(int index) const
{
    const PlaylistShelfItem* item = ShelfItemAt(index);
    if (item == nullptr)
    {
        return 0.0f;
    }
    float height = kPlaylistShelfItemHeight + kGap;
    if (!IsPlaylistShelfItemExpanded(index) || item->playlist == nullptr)
    {
        return height;
    }
    const LinkCardGroupNode& playlist = *item->playlist;
    height += static_cast<float>(playlist.LoadedChildCount()) * (kCardHeight + kGap);
    if (playlist.ShowsLoadMore() || playlist.ShowsCollapseToFirstPage())
    {
        height += kLoadMoreHeight;
    }
    return height;
}

float LinkCardGroupNode::PlaylistShelfItemOffsetFromHeader(int index) const
{
    float offset = 0.0f;
    const int limit = std::min(index, static_cast<int>(playlistShelf_.size()));
    for (int i = 0; i < limit; ++i)
    {
        offset += PlaylistShelfItemBlockHeight(i);
    }
    return offset;
}

void LinkCardGroupNode::UpdatePlaylistShelfItem(int index, Rectangle bounds, Font font)
{
    (void)font;
    PlaylistShelfItem* item = ShelfItemAt(index);
    if (item == nullptr)
    {
        return;
    }
    item->wasClicked = false;
    item->wasExpandClicked = false;
    if (item->playlist != nullptr)
    {
        item->playlist->PumpBackgroundWork();
        item->playlist->LoadHeaderThumbnail();
    }
    const Vector2 mouse = GetMousePosition();
    item->hovered = CheckCollisionPointRec(mouse, bounds);
    if (!item->hovered)
    {
        return;
    }
    UiCursor::RequestHand();
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        const Rectangle expandBounds = GetExpandToggleBounds(bounds);
        if (CheckCollisionPointRec(mouse, expandBounds))
        {
            item->wasExpandClicked = true;
        }
        else
        {
            item->wasClicked = true;
        }
    }
}

void LinkCardGroupNode::DrawPlaylistShelfItem(int index, Rectangle bounds, Font font) const
{
    const PlaylistShelfItem* item = ShelfItemAt(index);
    if (item == nullptr)
    {
        return;
    }

    const Color background =
        item->selected ? Color{17, 30, 17, 255} : (item->hovered ? Color{14, 26, 14, 255} : Color{12, 20, 12, 255});
    const Color border =
        item->selected ? Color{118, 170, 118, 255} : (item->hovered ? Color{90, 124, 90, 255} : Color{64, 84, 64, 255});
    const float minSide = bounds.width < bounds.height ? bounds.width : bounds.height;
    const float roundness = (13.0f * 2.0f) / std::max(1.0f, minSide);
    DrawRectangleRounded(bounds, roundness, 16, background);
    DrawRectangleRoundedLines(bounds, roundness, 16, border);

    const Color chevronColor = {196, 204, 220, 255};
    const float chevronX = bounds.x + 8.0f + kChevronWidth * 0.35f;
    const float chevronY = bounds.y + bounds.height * 0.5f;
    if (IsPlaylistShelfItemExpanded(index))
    {
        DrawTriangle({chevronX - 4.0f, chevronY - 2.0f},
                     {chevronX, chevronY + 4.0f},
                     {chevronX + 4.0f, chevronY - 2.0f},
                     chevronColor);
    }
    else
    {
        DrawTriangle({chevronX - 2.0f, chevronY - 4.0f},
                     {chevronX - 2.0f, chevronY + 4.0f},
                     {chevronX + 4.0f, chevronY},
                     chevronColor);
    }

    const float thumbW = 70.0f;
    const float thumbH = 40.0f;
    const Rectangle thumbnailBounds = {
        bounds.x + 8.0f + kChevronWidth, bounds.y + (bounds.height - thumbH) * 0.5f, thumbW, thumbH};
    DrawRectangleRounded(thumbnailBounds, kThumbnailRoundness, kThumbnailSegments, {28, 40, 28, 255});
    if (item->playlist != nullptr && item->playlist->hasHeaderThumbnail_)
    {
        const Texture2D& texture = item->playlist->headerThumbnail_;
        const Rectangle source = {0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)};
        DrawTexturePro(texture, source, thumbnailBounds, {0.0f, 0.0f}, 0.0f, WHITE);
    }
    DrawRectangleRoundedLines(thumbnailBounds, kThumbnailRoundness, kThumbnailSegments, {64, 84, 64, 255});

    const std::string title = item->entry.title.empty() ? item->entry.url : item->entry.title;
    const std::string status = [&]() -> std::string
    {
        if (item->playlist == nullptr)
        {
            return "Playlist";
        }
        if (!item->playlist->IsValid())
        {
            const std::string& err = item->playlist->Error();
            if (!err.empty())
            {
                return TruncateTextToWidth(font, err, 13.0f, bounds.width - 40.0f);
            }
            return "Unavailable";
        }
        return "Videos: " + std::to_string(item->playlist->EntryCount());
    }();

    const float textX = thumbnailBounds.x + thumbnailBounds.width + 10.0f;
    const float titleMaxWidth = std::max(0.0f, bounds.x + bounds.width - 12.0f - textX);
    DrawTextEx(font,
               TruncateTextToWidth(font, title, 16.0f, titleMaxWidth).c_str(),
               {textX, bounds.y + 10.0f},
               16.0f,
               0.0f,
               Color{240, 244, 240, 255});
    DrawTextEx(font,
               TruncateTextToWidth(font, status, 14.0f, titleMaxWidth).c_str(),
               {textX, bounds.y + 32.0f},
               14.0f,
               0.0f,
               Color{150, 170, 150, 255});
}

void LinkCardGroupNode::LoadAllPlaylistShelfPlaylists()
{
    // Nested playlists are prefetched before shelf rows are shown.
    (void)0;
}

LinkCardGroupNode::ChannelTabUi* LinkCardGroupNode::TabAt(int tab)
{
    if (tab < 0 || tab >= kChannelTabCount)
    {
        return nullptr;
    }
    return &channelTabs_[static_cast<size_t>(tab)];
}

const LinkCardGroupNode::ChannelTabUi* LinkCardGroupNode::TabAt(int tab) const
{
    if (tab < 0 || tab >= kChannelTabCount)
    {
        return nullptr;
    }
    return &channelTabs_[static_cast<size_t>(tab)];
}

int LinkCardGroupNode::ChannelTabEntryCount(int tab) const
{
    const ChannelTabUi* state = TabAt(tab);
    return state != nullptr ? static_cast<int>(state->entries.size()) : 0;
}

bool LinkCardGroupNode::IsChannelTabExpanded(int tab) const
{
    const ChannelTabUi* state = TabAt(tab);
    return state != nullptr && state->expanded;
}

bool LinkCardGroupNode::IsChannelTabSelected(int tab) const
{
    const ChannelTabUi* state = TabAt(tab);
    return state != nullptr && state->selected;
}

bool LinkCardGroupNode::IsChannelTabHovered(int tab) const
{
    const ChannelTabUi* state = TabAt(tab);
    return state != nullptr && state->hovered;
}

bool LinkCardGroupNode::WasChannelTabClicked(int tab) const
{
    const ChannelTabUi* state = TabAt(tab);
    return state != nullptr && state->wasClicked;
}

bool LinkCardGroupNode::WasChannelTabExpandClicked(int tab) const
{
    const ChannelTabUi* state = TabAt(tab);
    return state != nullptr && state->wasExpandClicked;
}

void LinkCardGroupNode::SetChannelTabSelected(int tab, bool selected)
{
    ChannelTabUi* state = TabAt(tab);
    if (state == nullptr)
    {
        return;
    }
    state->selected = selected;
    if (selected)
    {
        headerSelected_ = false;
        ClearLoadedCardSelection();
        for (int other = 0; other < kChannelTabCount; ++other)
        {
            if (other != tab)
            {
                channelTabs_[static_cast<size_t>(other)].selected = false;
            }
        }
    }
}

void LinkCardGroupNode::ClearChannelTabSelection()
{
    for (auto& tab : channelTabs_)
    {
        tab.selected = false;
    }
}

void LinkCardGroupNode::ClearLoadedCardSelection()
{
    ForEachLoadedCard(
        [](LinkCardNode& card)
        {
            card.SetSelected(false);
        });
}

bool LinkCardGroupNode::AnyChannelTabSelected() const
{
    for (const auto& tab : channelTabs_)
    {
        if (tab.selected)
        {
            return true;
        }
    }
    return false;
}

int LinkCardGroupNode::SelectedChannelTab() const
{
    for (int tab = 0; tab < kChannelTabCount; ++tab)
    {
        if (channelTabs_[static_cast<size_t>(tab)].selected)
        {
            return tab;
        }
    }
    return -1;
}

void LinkCardGroupNode::SetChannelTabExpanded(int tab, bool expanded)
{
    ChannelTabUi* state = TabAt(tab);
    if (state == nullptr)
    {
        return;
    }
    state->expanded = expanded;
    if (expanded)
    {
        EnsureTabFirstPageLoaded(tab);
    }
}

void LinkCardGroupNode::ToggleChannelTabExpanded(int tab)
{
    ChannelTabUi* state = TabAt(tab);
    if (state == nullptr)
    {
        return;
    }
    SetChannelTabExpanded(tab, !state->expanded);
}

std::vector<LinkCardNode>& LinkCardGroupNode::ChannelTabLoadedCards(int tab)
{
    ChannelTabUi* state = TabAt(tab);
    static std::vector<LinkCardNode> empty;
    return state != nullptr ? state->loaded : empty;
}

const std::vector<LinkCardNode>& LinkCardGroupNode::ChannelTabLoadedCards(int tab) const
{
    const ChannelTabUi* state = TabAt(tab);
    static const std::vector<LinkCardNode> empty;
    return state != nullptr ? state->loaded : empty;
}

int LinkCardGroupNode::ChannelTabLoadedCount(int tab) const
{
    const ChannelTabUi* state = TabAt(tab);
    return state != nullptr ? static_cast<int>(state->loaded.size()) : 0;
}

bool LinkCardGroupNode::ChannelTabShowsLoadMore(int tab) const
{
    const ChannelTabUi* state = TabAt(tab);
    return state != nullptr && state->expanded && state->materialized < state->entries.size();
}

bool LinkCardGroupNode::ChannelTabShowsCollapseToFirstPage(int tab) const
{
    const ChannelTabUi* state = TabAt(tab);
    return state != nullptr && state->expanded && state->entries.size() > kPageSize;
}

bool LinkCardGroupNode::ChannelTabCanCollapseToFirstPage(int tab) const
{
    const ChannelTabUi* state = TabAt(tab);
    return ChannelTabShowsCollapseToFirstPage(tab) && state != nullptr && state->materialized > kPageSize;
}

int LinkCardGroupNode::ChannelTabRemainingCount(int tab) const
{
    const ChannelTabUi* state = TabAt(tab);
    if (state == nullptr || state->materialized >= state->entries.size())
    {
        return 0;
    }
    return static_cast<int>(state->entries.size() - state->materialized);
}

void LinkCardGroupNode::MaterializeTabPage(int tab, size_t start, size_t end)
{
    ChannelTabUi* state = TabAt(tab);
    if (state == nullptr)
    {
        return;
    }
    end = std::min(end, state->entries.size());
    for (size_t index = start; index < end; ++index)
    {
        state->loaded.emplace_back(BuildPartialLinkInfoFromEntry(state->entries[index]));
        state->loaded.back().Options() = options_;
        ApplyRememberedCompletionToCard(state->loaded.back());
    }
    state->materialized = state->loaded.size();
}

void LinkCardGroupNode::CancelDurationFill()
{
    if (durationFillCancel_ != nullptr)
    {
        durationFillCancel_->store(true);
    }
    AbandonDurationFillFuture(durationFillFuture_);
    durationFillCancel_.reset();

    for (const std::string& url : durationFillBatchUrls_)
    {
        ForEachLoadedCard(
            [&](LinkCardNode& card)
            {
                if (card.Url() == url && card.IsDurationLookupPending())
                {
                    card.ClearDurationLookupStarted();
                }
            });
    }
    durationFillBatchUrls_.clear();
}

void LinkCardGroupNode::SetDurationFillSuspended(bool suspended)
{
    durationFillSuspended_ = suspended;
    detailPrefetchSuspended_ = suspended;
    if (suspended)
    {
        CancelDurationFill();
    }
    if (usesPlaylistShelf_)
    {
        for (auto& item : playlistShelf_)
        {
            if (item.playlist != nullptr)
            {
                item.playlist->SetDurationFillSuspended(suspended);
            }
        }
    }
}

void LinkCardGroupNode::PumpBackgroundWork()
{
    loader_.Update();
    ApplyParseResultIfReady();
    PumpDurationFill();
    PumpDetailPrefetch();
    SyncEntryTitlesFromLoadedCards();
    if (usesPlaylistShelf_)
    {
        PumpShelfPrefetch();
        if (IsPlaylistShelfReady())
        {
            for (auto& item : playlistShelf_)
            {
                if (item.playlist != nullptr)
                {
                    item.playlist->PumpBackgroundWork();
                }
            }
            PumpShelfSelectedDetailParse();
        }
    }
}

void LinkCardGroupNode::SyncEntryTitlesFromLoadedCards()
{
    auto syncOne = [](LinkGroupEntry& entry, const LinkCardNode& card)
    {
        // Detail parse finished when qualities/streams are filled (NeedsDetailedParse is cleared earlier).
        if (card.AvailableQualities().empty() && card.FormatStreams().empty())
        {
            return;
        }
        if (card.Title().empty() || entry.title == card.Title())
        {
            return;
        }
        entry.title = card.Title();
    };

    if (usesChannelTabs_)
    {
        for (int tab = 0; tab < kChannelTabCount; ++tab)
        {
            ChannelTabUi* state = TabAt(tab);
            if (state == nullptr)
            {
                continue;
            }
            const size_t count = std::min(state->loaded.size(), state->entries.size());
            for (size_t index = 0; index < count; ++index)
            {
                syncOne(state->entries[index], state->loaded[index]);
            }
        }
        return;
    }

    const size_t count = std::min(loadedCards_.size(), info_.entries.size());
    for (size_t index = 0; index < count; ++index)
    {
        syncOne(info_.entries[index], loadedCards_[index]);
    }
}

void LinkCardGroupNode::PumpDetailPrefetch()
{
    if (usesChannelTabs_ || usesPlaylistShelf_ || detailPrefetchSuspended_ || isParsing_ || !info_.success)
    {
        return;
    }

    // Prefer having the first prefetch window materialized (even if the UI page is larger).
    const size_t want = std::min(kPlaylistDetailPrefetchCount, info_.entries.size());
    if (want > 0 && materializedCount_ < want)
    {
        MaterializePage(materializedCount_, want);
    }

    const size_t prefetchLimit = std::min(kPlaylistDetailPrefetchCount, loadedCards_.size());
    if (prefetchLimit == 0)
    {
        return;
    }

    auto tryStartDetailParse = [](LinkCardNode& card) -> bool
    {
        if (!card.NeedsDetailedParse() || card.IsParsing())
        {
            return false;
        }
        card.EnsureDetailedParse();
        return card.IsParsing();
    };

    int inFlight = 0;
    std::vector<size_t> order;
    order.reserve(prefetchLimit);
    for (size_t index = 0; index < prefetchLimit; ++index)
    {
        if (loadedCards_[index].IsSelected())
        {
            order.push_back(index);
        }
    }
    for (size_t index = 0; index < prefetchLimit; ++index)
    {
        if (!loadedCards_[index].IsSelected())
        {
            order.push_back(index);
        }
    }

    for (const size_t index : order)
    {
        LinkCardNode& card = loadedCards_[index];
        card.ApplyParseResultIfReady();
        if (card.IsParsing())
        {
            ++inFlight;
        }
    }

    if (inFlight >= kPlaylistDetailPrefetchParallel)
    {
        return;
    }

    for (const size_t index : order)
    {
        if (tryStartDetailParse(loadedCards_[index]))
        {
            ++inFlight;
            if (inFlight >= kPlaylistDetailPrefetchParallel)
            {
                break;
            }
        }
    }
}

void LinkCardGroupNode::PumpDurationFill()
{
    // One URL at a time: Shorts need JS/player extract; big batches fail together and spin-retry.
    static constexpr size_t kDurationFillBatchSize = 1;

    if (durationFillSuspended_)
    {
        return;
    }

    if (durationFillFuture_.valid())
    {
        if (durationFillFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            return;
        }

        std::vector<std::pair<std::string, std::string>> filled;
        try
        {
            filled = durationFillFuture_.get();
        }
        catch (...)
        {
            filled.clear();
        }
        durationFillCancel_.reset();

        for (const auto& [videoId, duration] : filled)
        {
            if (videoId.empty() || duration.empty())
            {
                continue;
            }
            ForEachLoadedCard(
                [&](LinkCardNode& card)
                {
                    if (card.Url().find(videoId) == std::string::npos)
                    {
                        return;
                    }
                    card.FillDurationIfMissing(duration);
                });
        }

        bool anyStillMissing = false;
        for (const std::string& url : durationFillBatchUrls_)
        {
            ForEachLoadedCard(
                [&](LinkCardNode& card)
                {
                    if (card.Url() == url && card.IsDurationLookupPending())
                    {
                        card.NoteDurationLookupFailure();
                        anyStillMissing = true;
                    }
                });
        }
        durationFillBatchUrls_.clear();
        if (anyStillMissing && filled.empty())
        {
            durationFillNextAllowedAt_ = GetTime() + 1.5;
        }
    }

    if (durationFillFuture_.valid() || isParsing_)
    {
        return;
    }

    if (GetTime() < durationFillNextAllowedAt_)
    {
        return;
    }

    std::vector<std::string> missingUrls;
    missingUrls.reserve(kDurationFillBatchSize);
    const auto collectMissing = [&](LinkCardNode& card)
    {
        if (missingUrls.size() >= kDurationFillBatchSize)
        {
            return;
        }
        if (!card.NeedsDurationLookup() || card.IsParsing())
        {
            return;
        }
        card.MarkDurationLookupStarted();
        missingUrls.push_back(card.Url());
    };

    if (usesChannelTabs_)
    {
        for (ChannelTabUi& tab : channelTabs_)
        {
            if (!tab.expanded)
            {
                continue;
            }
            for (LinkCardNode& card : tab.loaded)
            {
                collectMissing(card);
                if (missingUrls.size() >= kDurationFillBatchSize)
                {
                    break;
                }
            }
            if (missingUrls.size() >= kDurationFillBatchSize)
            {
                break;
            }
        }
    }
    else if (usesPlaylistShelf_)
    {
        for (PlaylistShelfItem& item : playlistShelf_)
        {
            if (!item.expanded || item.playlist == nullptr)
            {
                continue;
            }
            for (LinkCardNode& card : item.playlist->LoadedCards())
            {
                collectMissing(card);
                if (missingUrls.size() >= kDurationFillBatchSize)
                {
                    break;
                }
            }
            if (missingUrls.size() >= kDurationFillBatchSize)
            {
                break;
            }
        }
    }
    else if (expanded_)
    {
        for (LinkCardNode& card : loadedCards_)
        {
            collectMissing(card);
            if (missingUrls.size() >= kDurationFillBatchSize)
            {
                break;
            }
        }
    }

    if (missingUrls.empty())
    {
        return;
    }

    durationFillBatchUrls_ = missingUrls;
    durationFillCancel_ = std::make_shared<std::atomic_bool>(false);
    durationFillFuture_ = std::async(std::launch::async,
                                     [urls = std::move(missingUrls), cancelRequested = durationFillCancel_]
                                     {
                                         return LinkInfoLoader::LoadDurationsByUrl(urls, cancelRequested);
                                     });
}

void LinkCardGroupNode::EnsureTabFirstPageLoaded(int tab)
{
    ChannelTabUi* state = TabAt(tab);
    if (state == nullptr || !state->expanded || state->materialized > 0 || state->entries.empty())
    {
        return;
    }
    MaterializeTabPage(tab, 0, std::min(kPageSize, state->entries.size()));
}

void LinkCardGroupNode::LoadNextPageForTab(int tab)
{
    ChannelTabUi* state = TabAt(tab);
    if (state == nullptr || state->materialized >= state->entries.size())
    {
        return;
    }
    const size_t start = state->materialized;
    const size_t end = std::min(start + kPageSize, state->entries.size());
    MaterializeTabPage(tab, start, end);
}

void LinkCardGroupNode::LoadAllPagesForTab(int tab)
{
    ChannelTabUi* state = TabAt(tab);
    if (state == nullptr)
    {
        return;
    }
    while (state->materialized < state->entries.size())
    {
        LoadNextPageForTab(tab);
    }
}

void LinkCardGroupNode::CollapseToFirstPageForTab(int tab)
{
    ChannelTabUi* state = TabAt(tab);
    if (state == nullptr || !ChannelTabCanCollapseToFirstPage(tab))
    {
        return;
    }
    const size_t keep = std::min(kPageSize, state->entries.size());
    if (state->loaded.size() > keep)
    {
        for (size_t index = keep; index < state->loaded.size(); ++index)
        {
            CaptureCardCompletion(state->loaded[index]);
        }
        state->loaded.erase(state->loaded.begin() + static_cast<std::ptrdiff_t>(keep), state->loaded.end());
    }
    state->materialized = state->loaded.size();
}

float LinkCardGroupNode::ChannelTabBlockHeight(int tab) const
{
    const ChannelTabUi* state = TabAt(tab);
    if (state == nullptr)
    {
        return 0.0f;
    }
    float height = kCardHeight + kGap;
    if (state->expanded)
    {
        height += static_cast<float>(state->loaded.size()) * (kCardHeight + kGap);
        if (ChannelTabShowsLoadMore(tab) || ChannelTabShowsCollapseToFirstPage(tab))
        {
            height += kLoadMoreHeight;
        }
    }
    return height;
}

float LinkCardGroupNode::ChannelTabOffsetFromHeader(int tab) const
{
    float offset = 0.0f;
    for (int index = 0; index < tab && index < kChannelTabCount; ++index)
    {
        offset += ChannelTabBlockHeight(index);
    }
    return offset;
}

void LinkCardGroupNode::UpdateChannelTab(int tab, Rectangle bounds, Font font)
{
    (void)font;
    ChannelTabUi* state = TabAt(tab);
    if (state == nullptr)
    {
        return;
    }
    state->wasClicked = false;
    state->wasExpandClicked = false;
    const Vector2 mouse = GetMousePosition();
    state->hovered = CheckCollisionPointRec(mouse, bounds);
    if (!state->hovered)
    {
        return;
    }
    UiCursor::RequestHand();
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        const Rectangle expandBounds = GetExpandToggleBounds(bounds);
        if (CheckCollisionPointRec(mouse, expandBounds))
        {
            state->wasExpandClicked = true;
        }
        else
        {
            state->wasClicked = true;
        }
    }
}

void LinkCardGroupNode::DrawChannelTab(int tab, Rectangle bounds, Font font) const
{
    const ChannelTabUi* state = TabAt(tab);
    if (state == nullptr)
    {
        return;
    }

    const Color background =
        state->selected ? Color{17, 30, 17, 255} : (state->hovered ? Color{14, 26, 14, 255} : Color{12, 20, 12, 255});
    const Color border = state->selected ? Color{118, 170, 118, 255}
                                         : (state->hovered ? Color{90, 124, 90, 255} : Color{64, 84, 64, 255});
    const float minSide = bounds.width < bounds.height ? bounds.width : bounds.height;
    const float roundness = (13.0f * 2.0f) / std::max(1.0f, minSide);
    DrawRectangleRounded(bounds, roundness, 16, background);
    DrawRectangleRoundedLines(bounds, roundness, 16, border);

    const Color chevronColor = {196, 204, 220, 255};
    const float chevronX = bounds.x + 8.0f + kChevronWidth * 0.35f;
    const float chevronY = bounds.y + bounds.height * 0.5f;
    if (state->expanded)
    {
        DrawTriangle({chevronX - 4.0f, chevronY - 2.0f},
                     {chevronX, chevronY + 4.0f},
                     {chevronX + 4.0f, chevronY - 2.0f},
                     chevronColor);
    }
    else
    {
        DrawTriangle({chevronX - 2.0f, chevronY - 4.0f},
                     {chevronX - 2.0f, chevronY + 4.0f},
                     {chevronX + 4.0f, chevronY},
                     chevronColor);
    }

    const char* label = "Videos";
    if (tab == static_cast<int>(ChannelContentTab::Shorts))
    {
        label = "Shorts";
    }
    else if (tab == static_cast<int>(ChannelContentTab::Lives))
    {
        label = "Lives";
    }
    const std::string title = std::string(label) + ": " + std::to_string(state->entries.size());
    const Color titleColor = {240, 244, 240, 255};
    const float titleX = bounds.x + 32.0f;
    const float titleY = bounds.y + 26.0f;
    DrawTextEx(font, title.c_str(), {titleX, titleY}, 16.0f, 0.0f, titleColor);

    bool anyDownloading = false;
    bool anyQueued = false;
    for (const LinkCardNode& card : state->loaded)
    {
        if (card.IsDownloading())
        {
            anyDownloading = true;
            break;
        }
        if (card.IsInQueue())
        {
            anyQueued = true;
        }
    }
    if (anyDownloading || anyQueued)
    {
        const char* statusText = anyDownloading ? "downloading" : "in queue";
        const float titleWidth = MeasureTextEx(font, title.c_str(), 16.0f, 0.0f).x;
        const float statusX = titleX + titleWidth + 12.0f;
        const Color statusColor = {150, 170, 150, 255};
        DrawTextEx(font, statusText, {statusX, titleY + 1.0f}, 14.0f, 0.0f, statusColor);
        if (anyDownloading)
        {
            const float statusWidth = MeasureTextEx(font, statusText, 14.0f, 0.0f).x;
            DrawMiniSpinner({statusX + statusWidth + 10.0f, bounds.y + bounds.height * 0.5f});
        }
    }
}

void LinkCardGroupNode::DrawChannelTabLoadMore(int tab, Rectangle bounds, Font font) const
{
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    const Color fill = hovered ? Color{22, 34, 22, 255} : Color{16, 24, 16, 255};
    const Color border = hovered ? Color{96, 128, 96, 255} : Color{72, 92, 72, 255};
    DrawRectangleRec(bounds, fill);
    DrawRectangleLinesEx(bounds, 1.0f, border);
    const int remaining = ChannelTabRemainingCount(tab);
    char label[64]{};
    FormatLoadMoreLabel(label, sizeof(label), remaining);
    const Vector2 size = MeasureTextEx(font, label, 14.0f, 0.0f);
    DrawTextEx(font,
               label,
               {bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + (bounds.height - size.y) * 0.5f},
               14.0f,
               0.0f,
               Color{196, 204, 220, 255});
}

void LinkCardGroupNode::DrawChannelTabCollapseToFirstPage(int tab, Rectangle bounds, Font font) const
{
    DrawCollapseToFirstPage(bounds, font, ChannelTabCanCollapseToFirstPage(tab));
}

int LinkCardGroupNode::CountActiveDownloads() const
{
    int count = 0;
    ForEachLoadedCard(
        [&](const LinkCardNode& card)
        {
            if (card.IsDownloading() || card.IsInQueue())
            {
                ++count;
            }
        });
    return count;
}

int LinkCardGroupNode::CountCompletedDownloads() const
{
    int count = 0;
    ForEachLoadedCard(
        [&](const LinkCardNode& card)
        {
            if (card.HasCompletedDownload())
            {
                ++count;
            }
        });
    return count;
}

void LinkCardGroupNode::RegisterBatchDownload(const std::string& url)
{
    if (url.empty())
    {
        return;
    }
    if (batchDownloadExpected_ > 0 && batchDownloadCompleted_ >= batchDownloadExpected_)
    {
        ClearDownloadBatch();
    }
    if (!batchDownloadUrls_.insert(url).second)
    {
        return;
    }
    if (batchDownloadExpected_ <= 0)
    {
        batchDownloadCompleted_ = 0;
        batchDownloadElapsedSum_ = 0.0;
        batchDownloadFinishedUrls_.clear();
    }
    ++batchDownloadExpected_;
}

void LinkCardGroupNode::NotifyBatchDownloadFinished(const std::string& url, double elapsedSeconds)
{
    if (url.empty() || batchDownloadExpected_ <= 0 || batchDownloadUrls_.count(url) == 0)
    {
        return;
    }
    if (!batchDownloadFinishedUrls_.insert(url).second)
    {
        return;
    }
    if (batchDownloadCompleted_ >= batchDownloadExpected_)
    {
        return;
    }
    ++batchDownloadCompleted_;
    if (elapsedSeconds > 0.0)
    {
        batchDownloadElapsedSum_ += elapsedSeconds;
    }
}

void LinkCardGroupNode::ClearDownloadBatch()
{
    batchDownloadExpected_ = 0;
    batchDownloadCompleted_ = 0;
    batchDownloadElapsedSum_ = 0.0;
    batchDownloadUrls_.clear();
    batchDownloadFinishedUrls_.clear();
}

void LinkCardGroupNode::RememberEntryDownloadCompletion(const std::string& url,
                                                        double elapsedSeconds,
                                                        const std::string& outputPath)
{
    if (url.empty())
    {
        return;
    }
    EntryCompletionState& state = entryCompletions_[url];
    state.hasDownloadElapsed = true;
    if (elapsedSeconds > 0.0)
    {
        state.downloadElapsedSeconds = elapsedSeconds;
    }
    if (!outputPath.empty())
    {
        state.lastDownloadedPath = outputPath;
    }
}

void LinkCardGroupNode::RememberEntryConvertCompletion(const std::string& url,
                                                       double elapsedSeconds,
                                                       const std::string& outputPath)
{
    if (url.empty())
    {
        return;
    }
    EntryCompletionState& state = entryCompletions_[url];
    state.hasConvertElapsed = true;
    state.convertElapsedSeconds = elapsedSeconds > 0.0 ? elapsedSeconds : state.convertElapsedSeconds;
    if (!outputPath.empty())
    {
        state.lastDownloadedPath = outputPath;
    }
}

void LinkCardGroupNode::CaptureCardCompletion(const LinkCardNode& card)
{
    if (card.Url().empty())
    {
        return;
    }
    if (!card.HasDownloadElapsedTime() && !card.HasConvertElapsedTime() && card.LastDownloadedPath().empty())
    {
        return;
    }
    EntryCompletionState& state = entryCompletions_[card.Url()];
    if (card.HasDownloadElapsedTime())
    {
        state.hasDownloadElapsed = true;
        state.downloadElapsedSeconds = card.DownloadElapsedSeconds();
    }
    if (card.HasConvertElapsedTime())
    {
        state.hasConvertElapsed = true;
        state.convertElapsedSeconds = card.ConvertElapsedSeconds();
    }
    if (!card.LastDownloadedPath().empty())
    {
        state.lastDownloadedPath = card.LastDownloadedPath();
    }
}

void LinkCardGroupNode::UpdateEntryTitleByUrl(const std::string& url, const std::string& title)
{
    if (url.empty() || title.empty())
    {
        return;
    }
    auto tryUpdate = [&](LinkGroupEntry& entry) -> bool
    {
        if (entry.url != url)
        {
            return false;
        }
        entry.title = title;
        return true;
    };
    if (usesChannelTabs_)
    {
        for (int tab = 0; tab < kChannelTabCount; ++tab)
        {
            ChannelTabUi* state = TabAt(tab);
            if (state == nullptr)
            {
                continue;
            }
            for (LinkGroupEntry& entry : state->entries)
            {
                if (tryUpdate(entry))
                {
                    return;
                }
            }
        }
        return;
    }
    for (LinkGroupEntry& entry : info_.entries)
    {
        if (tryUpdate(entry))
        {
            return;
        }
    }
}

const LinkCardGroupNode::EntryCompletionState* LinkCardGroupNode::FindEntryCompletion(const std::string& url) const
{
    if (url.empty())
    {
        return nullptr;
    }
    const auto exact = entryCompletions_.find(url);
    if (exact != entryCompletions_.end())
    {
        return &exact->second;
    }
    return nullptr;
}

void LinkCardGroupNode::ApplyRememberedCompletionToCard(LinkCardNode& card) const
{
    const EntryCompletionState* state = FindEntryCompletion(card.Url());
    if (state == nullptr)
    {
        for (const auto& [key, value] : entryCompletions_)
        {
            if (card.HasUrl(key))
            {
                state = &value;
                break;
            }
        }
    }
    if (state == nullptr)
    {
        return;
    }
    if (state->hasDownloadElapsed && !card.HasDownloadElapsedTime())
    {
        card.SetDownloadElapsed(state->downloadElapsedSeconds);
    }
    if (state->hasConvertElapsed && !card.HasConvertElapsedTime())
    {
        card.SetConvertElapsed(state->convertElapsedSeconds);
    }
    if (!state->lastDownloadedPath.empty() && card.LastDownloadedPath().empty())
    {
        card.SetLastDownloadedPath(state->lastDownloadedPath);
    }
}

bool LinkCardGroupNode::HasActiveDownloadBatch() const
{
    return batchDownloadExpected_ > 0 && batchDownloadCompleted_ < batchDownloadExpected_;
}

double LinkCardGroupNode::BatchDownloadElapsedSum() const
{
    return batchDownloadElapsedSum_;
}

std::string LinkCardGroupNode::BuildAggregateStatus() const
{
    const int totalEntries = EntryCount();
    if (batchDownloadExpected_ > 0)
    {
        if (batchDownloadCompleted_ < batchDownloadExpected_)
        {
            // Show full batch size (not only loaded page cards) so Download All isn't "50/204".
            const int showTotal = totalEntries > 0 ? totalEntries : batchDownloadExpected_;
            char buffer[64]{};
            std::snprintf(buffer, sizeof(buffer), "%d/%d downloading", batchDownloadExpected_, showTotal);
            return buffer;
        }

        char buffer[96]{};
        std::snprintf(buffer,
                      sizeof(buffer),
                      "%d/%d finished · took %s",
                      batchDownloadCompleted_,
                      totalEntries > 0 ? totalEntries : batchDownloadExpected_,
                      FormatElapsedTookTime(batchDownloadElapsedSum_).c_str());
        return buffer;
    }

    const int active = CountActiveDownloads();
    if (active <= 0)
    {
        int completed = 0;
        double totalElapsed = 0.0;
        ForEachLoadedCard(
            [&](const LinkCardNode& card)
            {
                if (!card.HasCompletedDownload())
                {
                    return;
                }
                ++completed;
                totalElapsed += card.DownloadElapsedSeconds() + card.ConvertElapsedSeconds();
            });

        if (completed <= 0)
        {
            return {};
        }

        char buffer[96]{};
        std::snprintf(buffer,
                      sizeof(buffer),
                      "%d/%d finished · took %s",
                      completed,
                      totalEntries,
                      FormatElapsedTookTime(totalElapsed).c_str());
        return buffer;
    }
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%d/%d downloading", active, totalEntries);
    return buffer;
}

bool LinkCardGroupNode::RemoveEntryByUrl(const std::string& url, LinkGroupEntry& removedEntry, size_t& removedIndex)
{
    removedIndex = static_cast<size_t>(-1);
    if (!info_.success)
    {
        return false;
    }

    // Capture completion before erase — cancel after delete never reaches NotifyBatchDownloadFinished
    // (entry is gone), and queued removals never notify either. Shrink the batch so "5/3" / stuck
    // spinner cannot happen when the user deletes children mid-download.
    bool removedHadCompletedDownload = false;
    const auto rememberCompletionBeforeErase = [&](const std::vector<LinkCardNode>& loaded)
    {
        auto cardIt = std::find_if(loaded.begin(),
                                   loaded.end(),
                                   [&](const LinkCardNode& card)
                                   {
                                       return card.HasUrl(url);
                                   });
        if (cardIt != loaded.end())
        {
            removedHadCompletedDownload = cardIt->HasCompletedDownload();
            return;
        }
        const auto completionIt = entryCompletions_.find(url);
        if (completionIt != entryCompletions_.end())
        {
            removedHadCompletedDownload = completionIt->second.hasDownloadElapsed;
        }
    };

    const auto eraseFrom =
        [&](std::vector<LinkGroupEntry>& entries, std::vector<LinkCardNode>& loaded, size_t& materialized) -> bool
    {
        auto it = std::find_if(entries.begin(),
                               entries.end(),
                               [&](const LinkGroupEntry& entry)
                               {
                                   return entry.url == url;
                               });
        if (it == entries.end())
        {
            return false;
        }
        rememberCompletionBeforeErase(loaded);
        removedIndex = static_cast<size_t>(std::distance(entries.begin(), it));
        removedEntry = *it;
        entries.erase(it);
        auto cardIt = std::find_if(loaded.begin(),
                                   loaded.end(),
                                   [&](const LinkCardNode& card)
                                   {
                                       return card.HasUrl(url);
                                   });
        if (cardIt != loaded.end())
        {
            loaded.erase(cardIt);
        }
        materialized = loaded.size();
        return true;
    };

    bool removed = false;
    if (usesChannelTabs_)
    {
        for (auto& tab : channelTabs_)
        {
            if (eraseFrom(tab.entries, tab.loaded, tab.materialized))
            {
                removed = true;
                break;
            }
        }
    }
    if (!removed)
    {
        if (info_.entries.empty() || !eraseFrom(info_.entries, loadedCards_, materializedCount_))
        {
            return false;
        }
        removed = true;
    }
    else
    {
        auto it = std::find_if(info_.entries.begin(),
                               info_.entries.end(),
                               [&](const LinkGroupEntry& entry)
                               {
                                   return entry.url == url;
                               });
        if (it != info_.entries.end())
        {
            info_.entries.erase(it);
        }
    }

    if (info_.entryCount > 0)
    {
        --info_.entryCount;
    }

    entryCompletions_.erase(url);

    // Drop this URL from the active download batch (queued/cancelled deletes never notify).
    if (batchDownloadUrls_.erase(url) > 0)
    {
        if (batchDownloadExpected_ > 0)
        {
            --batchDownloadExpected_;
        }
        const bool wasFinished = batchDownloadFinishedUrls_.erase(url) > 0 || removedHadCompletedDownload;
        if (wasFinished && batchDownloadCompleted_ > 0)
        {
            --batchDownloadCompleted_;
        }
        if (batchDownloadExpected_ <= 0)
        {
            ClearDownloadBatch();
        }
        else if (batchDownloadCompleted_ > batchDownloadExpected_)
        {
            batchDownloadCompleted_ = batchDownloadExpected_;
        }
    }

    const bool empty = usesChannelTabs_ ? (channelTabs_[0].entries.empty() && channelTabs_[1].entries.empty() &&
                                           channelTabs_[2].entries.empty())
                                        : info_.entries.empty();
    if (empty)
    {
        info_.success = false;
        shouldClose_ = true;
    }
    return true;
}

void LinkCardGroupNode::InsertEntry(size_t index, const LinkGroupEntry& entry)
{
    if (!info_.success)
    {
        info_.success = true;
        shouldClose_ = false;
    }

    index = std::min(index, info_.entries.size());
    info_.entries.insert(info_.entries.begin() + static_cast<std::ptrdiff_t>(index), entry);

    if (info_.entryCount > 0)
    {
        ++info_.entryCount;
    }
    else
    {
        info_.entryCount = static_cast<int>(info_.entries.size());
    }

    loadedCards_.clear();
    materializedCount_ = 0;
    LoadAllPages();
}

DownloadOptions& LinkCardGroupNode::Options()
{
    return options_;
}

const DownloadOptions& LinkCardGroupNode::Options() const
{
    return options_;
}

bool& LinkCardGroupNode::ChannelTabKeepNumbering(int tab)
{
    const int clamped = std::clamp(tab, 0, kChannelTabCount - 1);
    return channelTabs_[static_cast<size_t>(clamped)].keepNumbering;
}

bool LinkCardGroupNode::ChannelTabKeepNumbering(int tab) const
{
    const int clamped = std::clamp(tab, 0, kChannelTabCount - 1);
    return channelTabs_[static_cast<size_t>(clamped)].keepNumbering;
}

bool& LinkCardGroupNode::ChannelTabInverseNumbering(int tab)
{
    const int clamped = std::clamp(tab, 0, kChannelTabCount - 1);
    return channelTabs_[static_cast<size_t>(clamped)].inverseNumbering;
}

bool LinkCardGroupNode::ChannelTabInverseNumbering(int tab) const
{
    const int clamped = std::clamp(tab, 0, kChannelTabCount - 1);
    return channelTabs_[static_cast<size_t>(clamped)].inverseNumbering;
}

bool LinkCardGroupNode::TryConsumeParseFailure(std::string& url, std::string& error)
{
    if (!pendingParseErrorReport_)
    {
        return false;
    }
    pendingParseErrorReport_ = false;
    url = info_.url;
    error = info_.error.empty() ? "Could not parse playlist or channel." : info_.error;
    return true;
}

bool LinkCardGroupNode::TryConsumeParseSuccess(std::string& url)
{
    if (!pendingParseSuccessReport_)
    {
        return false;
    }
    pendingParseSuccessReport_ = false;
    url = info_.url;
    return true;
}

void LinkCardGroupNode::Update(Rectangle headerBounds, Font font)
{
    wasHeaderClicked_ = false;
    wasExpandToggleClicked_ = false;
    wasLoadMoreClicked_ = false;
    wasCopyClicked_ = false;
    wasSourceClicked_ = false;
    headerHovered_ = false;
    hasSourceBounds_ = false;

    PumpBackgroundWork();
    LoadHeaderThumbnail();

    const Vector2 mouse = GetMousePosition();
    headerHovered_ = CheckCollisionPointRec(mouse, headerBounds);
    if (headerHovered_)
    {
        UiCursor::RequestHand();
    }

    const Rectangle expandBounds = GetExpandToggleBounds(headerBounds);
    const Rectangle closeButton = CardChrome::CloseButtonBounds(headerBounds);
    const Rectangle copyButton = CardChrome::CopyButtonBounds(headerBounds);

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        if (CheckCollisionPointRec(mouse, closeButton))
        {
            RequestClose();
            return;
        }
        if (!isParsing_ && CheckCollisionPointRec(mouse, copyButton))
        {
            SetClipboardText(info_.url.c_str());
            wasCopyClicked_ = true;
            return;
        }
        if (!isParsing_ && hasSourceBounds_ && CheckCollisionPointRec(mouse, sourceBounds_))
        {
            wasSourceClicked_ = true;
            return;
        }
        if (CheckCollisionPointRec(mouse, expandBounds) && !IsParsing())
        {
            ToggleExpanded();
            wasExpandToggleClicked_ = true;
            return;
        }
        if (headerHovered_)
        {
            wasHeaderClicked_ = true;
        }
    }
}

void LinkCardGroupNode::DrawStackPeeks(Rectangle headerBounds) const
{
    if (expanded_ || isParsing_ || !info_.success)
    {
        return;
    }
    const Rectangle animatedBounds = CardChrome::AnimatedBounds(headerBounds, pulseStartTime_);
    CardChrome::DrawStackPeekCard(animatedBounds, 2);
    CardChrome::DrawStackPeekCard(animatedBounds, 1);
}

void LinkCardGroupNode::DrawHeader(Rectangle headerBounds, Font font) const
{
    headerBounds = CardChrome::AnimatedBounds(headerBounds, pulseStartTime_);
    const Color background =
        headerSelected_ ? Color{17, 30, 17, 255} : (headerHovered_ ? Color{14, 26, 14, 255} : Color{10, 18, 10, 255});
    const Color border = headerSelected_ ? Color{118, 170, 118, 255}
                                         : (headerHovered_ ? Color{90, 124, 90, 255} : Color{64, 84, 64, 255});
    const Color titleColor = {240, 244, 240, 255};
    const Color metaColor = {150, 170, 150, 255};
    const float minSide = headerBounds.width < headerBounds.height ? headerBounds.width : headerBounds.height;
    const float roundness = (13.0f * 2.0f) / std::max(1.0f, minSide);

    DrawRectangleRounded(headerBounds, roundness, 16, background);
    DrawRectangleRoundedLines(headerBounds, roundness, 16, border);
    if (headerSelected_)
    {
        DrawRectangleRoundedLines(
            {headerBounds.x + 1.0f, headerBounds.y + 1.0f, headerBounds.width - 2.0f, headerBounds.height - 2.0f},
            roundness,
            16,
            border);
    }

    // Geometric chevron — app font has no ▶/▼ glyphs (they rendered as "?").
    const float chevronX = headerBounds.x + 8.0f + kChevronWidth * 0.35f;
    const float chevronY = headerBounds.y + headerBounds.height * 0.5f;
    if (expanded_)
    {
        DrawTriangle({chevronX - 4.0f, chevronY - 2.0f},
                     {chevronX, chevronY + 4.0f},
                     {chevronX + 4.0f, chevronY - 2.0f},
                     metaColor);
    }
    else
    {
        DrawTriangle({chevronX - 2.0f, chevronY - 4.0f},
                     {chevronX - 2.0f, chevronY + 4.0f},
                     {chevronX + 4.0f, chevronY},
                     metaColor);
    }

    const Rectangle thumbnailBounds = GetThumbnailBounds(headerBounds);
    const bool channelAvatar = kind_ == LinkGroupKind::Channel;
    if (channelAvatar)
    {
        const float cx = thumbnailBounds.x + thumbnailBounds.width * 0.5f;
        const float cy = thumbnailBounds.y + thumbnailBounds.height * 0.5f;
        const float radius = thumbnailBounds.width * 0.5f;
        DrawCircleV({cx, cy}, radius, Color{18, 32, 22, 255});
        if (hasHeaderThumbnail_)
        {
            const Rectangle source = {
                0.0f, 0.0f, static_cast<float>(headerThumbnail_.width), static_cast<float>(headerThumbnail_.height)};
            DrawTexturePro(headerThumbnail_, source, thumbnailBounds, {0.0f, 0.0f}, 0.0f, WHITE);
        }
        else if (IsParsing())
        {
            DrawMiniSpinner({cx, cy});
        }
        // Thin YouTube-like ring.
        DrawRing({cx, cy}, radius - 0.75f, radius + 0.25f, 0.0f, 360.0f, 48, Color{90, 100, 90, 220});
    }
    else
    {
        DrawRectangleRounded(thumbnailBounds, kThumbnailRoundness, kThumbnailSegments, {28, 40, 28, 255});
        if (hasHeaderThumbnail_)
        {
            const Rectangle source = {
                0.0f, 0.0f, static_cast<float>(headerThumbnail_.width), static_cast<float>(headerThumbnail_.height)};
            DrawTexturePro(headerThumbnail_, source, thumbnailBounds, {0.0f, 0.0f}, 0.0f, WHITE);
        }
        else if (IsParsing())
        {
            DrawMiniSpinner(
                {thumbnailBounds.x + thumbnailBounds.width * 0.5f, thumbnailBounds.y + thumbnailBounds.height * 0.5f});
        }
        DrawRectangleRoundedLines(thumbnailBounds, kThumbnailRoundness, kThumbnailSegments, {64, 84, 64, 255});
    }

    const float textX = channelAvatar ? (thumbnailBounds.x + thumbnailBounds.width + 10.0f)
                                      : (headerBounds.x + CardChrome::kTextXOffset + kChevronWidth);
    const float titleMaxWidth = std::max(0.0f, headerBounds.x + headerBounds.width - 12.0f - textX - 52.0f);

    if (IsParsing())
    {
        if (usesPlaylistShelf_ && shelfPrefetchState_ == ShelfPrefetchState::Running)
        {
            char progressLabel[64];
            std::snprintf(progressLabel,
                          sizeof(progressLabel),
                          "Loading playlists (%d/%d)...",
                          shelfPrefetchCompleted_,
                          shelfPrefetchTotal_);
            DrawTextEx(font, progressLabel, {textX, headerBounds.y + 14.0f}, 18.0f, 0.0f, titleColor);
        }
        else
        {
            DrawTextEx(font, "Parsing link...", {textX, headerBounds.y + 14.0f}, 18.0f, 0.0f, titleColor);
        }
        const std::string truncatedUrl = TruncateTextToWidth(font, info_.url, 14.5f, titleMaxWidth);
        DrawTextEx(font, truncatedUrl.c_str(), {textX, headerBounds.y + 38.0f}, 14.5f, 0.0f, metaColor);
    }
    else if (!info_.success)
    {
        DrawTextEx(font, "Could not parse group", {textX, headerBounds.y + 14.0f}, 18.0f, 0.0f, {232, 160, 150, 255});
    }
    else
    {
        const std::string title = info_.title.empty() ? "Untitled group" : info_.title;
        CardChrome::DrawWrappedText(font,
                                    title,
                                    {textX, headerBounds.y + CardChrome::kTitleStartY},
                                    CardChrome::kTitleFontSize,
                                    titleMaxWidth,
                                    2,
                                    titleColor);

        std::string meta = FormatKindLabel(kind_, usesPlaylistShelf_) + " · " +
                           (usesPlaylistShelf_ ? FormatPlaylistCount(EntryCount()) : FormatVideoCount(EntryCount()));
        const std::string aggregate = BuildAggregateStatus();
        if (!aggregate.empty())
        {
            meta += " · " + aggregate;
        }
        DrawTextEx(font, meta.c_str(), {textX, headerBounds.y + 49.0f}, 14.0f, 0.0f, metaColor);
        const float metaWidth = MeasureTextEx(font, meta.c_str(), 14.0f, 0.0f).x;
        if (CountActiveDownloads() > 0 || HasActiveDownloadBatch())
        {
            const float spinnerX = textX + metaWidth + 10.0f;
            DrawMiniSpinner({spinnerX, headerBounds.y + 56.0f});

            const int entryTotal = EntryCount();
            const int total = entryTotal > 0 ? entryTotal : (batchDownloadExpected_ > 0 ? batchDownloadExpected_ : 0);
            const int done = HasActiveDownloadBatch() ? batchDownloadCompleted_ : CountCompletedDownloads();
            if (total > 0)
            {
                char doneBuf[48]{};
                std::snprintf(doneBuf, sizeof(doneBuf), "done: %d/%d", done, total);
                DrawTextEx(font, doneBuf, {spinnerX + 16.0f, headerBounds.y + 49.0f}, 14.0f, 0.0f, metaColor);
            }
        }
        // Meta line is status text only — do not reuse single-card "Source" open-link hitbox/tooltip.
    }

    CardChrome::DrawCloseButton(headerBounds, font);
    CardChrome::DrawCopyButton(headerBounds, font, !isParsing_ && info_.success);
}

void LinkCardGroupNode::DrawRail(Rectangle headerBounds, float contentBottom) const
{
    if (!expanded_ || (!usesChannelTabs_ && !usesPlaylistShelf_ && loadedCards_.empty()))
    {
        return;
    }
    const float railX = headerBounds.x + kChildIndent * 0.5f;
    const float top = headerBounds.y + headerBounds.height;
    const float bottom = contentBottom - kGap;
    if (bottom > top)
    {
        DrawLine(static_cast<int>(railX),
                 static_cast<int>(top),
                 static_cast<int>(railX),
                 static_cast<int>(bottom),
                 {64, 84, 64, 255});
    }
}

void LinkCardGroupNode::DrawLoadMore(Rectangle bounds, Font font) const
{
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    const Color fill = hovered ? Color{22, 34, 22, 255} : Color{16, 24, 16, 255};
    const Color border = hovered ? Color{96, 128, 96, 255} : Color{72, 92, 72, 255};
    const float roundness = 0.18f;
    DrawRectangleRounded(bounds, roundness, 8, fill);
    DrawRectangleRoundedLines(bounds, roundness, 8, border);

    char label[96]{};
    FormatLoadMoreLabel(label, sizeof(label), RemainingEntryCount());
    const Vector2 size = MeasureTextEx(font, label, 14.0f, 0.0f);
    DrawTextEx(font,
               label,
               {bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + (bounds.height - size.y) * 0.5f},
               14.0f,
               0.0f,
               {210, 225, 210, 255});
    if (hovered)
    {
        UiCursor::RequestHand();
    }
}

void LinkCardGroupNode::DrawCollapseToFirstPage(Rectangle bounds, Font font, bool enabled) const
{
    const bool hovered = enabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    const Color fill = !enabled ? Color{14, 20, 14, 255} : (hovered ? Color{22, 34, 22, 255} : Color{16, 24, 16, 255});
    const Color border =
        !enabled ? Color{48, 60, 48, 255} : (hovered ? Color{96, 128, 96, 255} : Color{72, 92, 72, 255});
    const Color text = !enabled ? Color{110, 120, 110, 255} : Color{210, 225, 210, 255};
    const float roundness = 0.18f;
    DrawRectangleRounded(bounds, roundness, 8, fill);
    DrawRectangleRoundedLines(bounds, roundness, 8, border);

    char label[64]{};
    std::snprintf(label, sizeof(label), "Collapse to first %zu", kPageSize);
    const Vector2 size = MeasureTextEx(font, label, 14.0f, 0.0f);
    DrawTextEx(font,
               label,
               {bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + (bounds.height - size.y) * 0.5f},
               14.0f,
               0.0f,
               text);
    if (hovered)
    {
        UiCursor::RequestHand();
    }
}
