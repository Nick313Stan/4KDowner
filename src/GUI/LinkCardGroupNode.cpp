#include "LinkCardGroupNodeInclude.h"

#include "CardChrome.h"
#include "MouseCursor.h"
#include "Tooltip.h"
#include "VideoTitle.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <utility>

namespace
{
constexpr float kThumbnailWidth = 82.0f;
constexpr float kThumbnailHeight = 46.0f;
constexpr float kThumbnailRoundness = 0.12f;
constexpr int kThumbnailSegments = 8;
constexpr float kChevronWidth = 22.0f;
constexpr int kThumbnailPixelWidth = 164;
constexpr int kThumbnailPixelHeight = 92;

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

std::string FormatKindLabel(LinkGroupKind kind)
{
    return kind == LinkGroupKind::Channel ? "Channel" : "Playlist";
}

std::string FormatVideoCount(int count)
{
    return std::to_string(count) + (count == 1 ? " video" : " videos");
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
    const float time = static_cast<float>(GetTime());
    const float angle = time * 6.2831853f;
    DrawCircleLines(static_cast<int>(center.x), static_cast<int>(center.y), 7.0f, {150, 170, 150, 255});
    DrawLine(static_cast<int>(center.x),
             static_cast<int>(center.y),
             static_cast<int>(center.x + std::cos(angle) * 7.0f),
             static_cast<int>(center.y + std::sin(angle) * 7.0f),
             {210, 230, 210, 255});
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
} // namespace

LinkCardGroupNode::LinkCardGroupNode(std::string url)
    : info_()
{
    info_.url = std::move(url);
    // Heuristic until flat-parse finishes (yt-dlp often labels channel tabs as playlist).
    if (LooksLikeChannelUrl(info_.url) && !LooksLikePlaylistUrl(info_.url))
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
    UnloadHeaderThumbnail();
}

LinkCardGroupNode::LinkCardGroupNode(LinkCardGroupNode&& other) noexcept
    : kind_(other.kind_),
      info_(std::move(other.info_)),
      loader_(std::move(other.loader_)),
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
      headerThumbnail_(other.headerThumbnail_),
      hasHeaderThumbnail_(other.hasHeaderThumbnail_),
      triedHeaderThumbnail_(other.triedHeaderThumbnail_),
      sourceBounds_(other.sourceBounds_),
      hasSourceBounds_(other.hasSourceBounds_)
{
    other.isParsing_ = false;
    other.hasHeaderThumbnail_ = false;
    other.headerThumbnail_ = {};
}

LinkCardGroupNode& LinkCardGroupNode::operator=(LinkCardGroupNode&& other) noexcept
{
    if (this != &other)
    {
        if (isParsing_)
        {
            loader_.Cancel();
        }
        UnloadHeaderThumbnail();
        kind_ = other.kind_;
        info_ = std::move(other.info_);
        loader_ = std::move(other.loader_);
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
        headerThumbnail_ = other.headerThumbnail_;
        hasHeaderThumbnail_ = other.hasHeaderThumbnail_;
        triedHeaderThumbnail_ = other.triedHeaderThumbnail_;
        sourceBounds_ = other.sourceBounds_;
        hasSourceBounds_ = other.hasSourceBounds_;
        other.isParsing_ = false;
        other.hasHeaderThumbnail_ = false;
        other.headerThumbnail_ = {};
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
    PrepareThumbnailImage(image);
    if (image.data == nullptr)
    {
        return;
    }
    headerThumbnail_ = LoadTextureFromImage(image);
    UnloadImage(image);
    hasHeaderThumbnail_ = headerThumbnail_.id != 0;
}

Rectangle LinkCardGroupNode::GetExpandToggleBounds(Rectangle headerBounds) const
{
    return {headerBounds.x + 4.0f, headerBounds.y, kChevronWidth + 8.0f, headerBounds.height};
}

Rectangle LinkCardGroupNode::GetThumbnailBounds(Rectangle headerBounds) const
{
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
            pendingPromoteSingle_ = true;
            promoteSingleInfo_ = result.singleVideo;
            isParsing_ = false;
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
    pendingParseSuccessReport_ = true;
    triedHeaderThumbnail_ = false;
    LoadHeaderThumbnail();
}

void LinkCardGroupNode::MaterializePage(size_t start, size_t end)
{
    end = std::min(end, info_.entries.size());
    for (size_t index = start; index < end; ++index)
    {
        loadedCards_.emplace_back(BuildPartialLinkInfoFromEntry(info_.entries[index]));
    }
    materializedCount_ = loadedCards_.size();
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
    if (!expanded_ || !info_.success || materializedCount_ > 0)
    {
        return;
    }
    MaterializePage(0, std::min(kPageSize, info_.entries.size()));
}

void LinkCardGroupNode::LoadNextPage()
{
    if (!info_.success || materializedCount_ >= info_.entries.size())
    {
        return;
    }
    const size_t start = materializedCount_;
    const size_t end = std::min(start + kPageSize, info_.entries.size());
    MaterializePage(start, end);
}

void LinkCardGroupNode::LoadAllPages()
{
    if (!info_.success || materializedCount_ >= info_.entries.size())
    {
        return;
    }
    MaterializePage(materializedCount_, info_.entries.size());
}

float LinkCardGroupNode::CollapsedHeight() const
{
    return kCardHeight + kCollapsedExtra;
}

float LinkCardGroupNode::ExpandedHeight() const
{
    float height = kCardHeight;
    height += static_cast<float>(loadedCards_.size()) * (kCardHeight + kGap);
    if (ShowsLoadMore())
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
    return expanded_ && info_.success && materializedCount_ < info_.entries.size();
}

int LinkCardGroupNode::LoadedChildCount() const
{
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
}

bool LinkCardGroupNode::IsParsing() const
{
    return isParsing_;
}

bool LinkCardGroupNode::IsValid() const
{
    return !isParsing_ && info_.success;
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
    if (!IsValid() || !headerHovered_)
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
    ToggleExpanded();
    return true;
}

bool LinkCardGroupNode::HasUrl(const std::string& url) const
{
    if (info_.url == url)
    {
        return true;
    }
    for (const LinkCardNode& card : loadedCards_)
    {
        if (card.HasUrl(url))
        {
            return true;
        }
    }
    return false;
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

std::vector<LinkCardNode>& LinkCardGroupNode::LoadedCards()
{
    return loadedCards_;
}

const std::vector<LinkCardNode>& LinkCardGroupNode::LoadedCards() const
{
    return loadedCards_;
}

int LinkCardGroupNode::CountActiveDownloads() const
{
    int count = 0;
    for (const LinkCardNode& card : loadedCards_)
    {
        if (card.IsDownloading() || card.IsInQueue())
        {
            ++count;
        }
    }
    return count;
}

int LinkCardGroupNode::CountCompletedDownloads() const
{
    int count = 0;
    for (const LinkCardNode& card : loadedCards_)
    {
        if (card.HasCompletedDownload())
        {
            ++count;
        }
    }
    return count;
}

std::string LinkCardGroupNode::BuildAggregateStatus() const
{
    const int active = CountActiveDownloads();
    if (active <= 0)
    {
        // Show completion summary once nothing is actively downloading anymore.
        int completed = 0;
        double totalElapsed = 0.0;
        for (const LinkCardNode& card : loadedCards_)
        {
            if (!card.HasCompletedDownload())
            {
                continue;
            }
            ++completed;
            totalElapsed += card.DownloadElapsedSeconds() + card.ConvertElapsedSeconds();
        }

        if (completed <= 0)
        {
            return {};
        }

        char buffer[96]{};
        std::snprintf(buffer,
                      sizeof(buffer),
                      "%d/%d finished · took %s",
                      completed,
                      EntryCount(),
                      FormatElapsedTookTime(totalElapsed).c_str());
        return buffer;
    }
    const int total = EntryCount();
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%d/%d downloading", active, total);
    return buffer;
}

bool LinkCardGroupNode::RemoveEntryByUrl(const std::string& url, LinkGroupEntry& removedEntry, size_t& removedIndex)
{
    removedIndex = static_cast<size_t>(-1);
    if (!info_.success || info_.entries.empty())
    {
        return false;
    }

    auto it = std::find_if(info_.entries.begin(),
                           info_.entries.end(),
                           [&](const LinkGroupEntry& entry)
                           {
                               return entry.url == url;
                           });
    if (it == info_.entries.end())
    {
        return false;
    }

    removedIndex = static_cast<size_t>(std::distance(info_.entries.begin(), it));
    removedEntry = *it;
    info_.entries.erase(it);

    if (info_.entryCount > 0)
    {
        --info_.entryCount;
    }

    // Preserve existing LinkCardNode instances for remaining entries to avoid
    // breaking downloader batch queue state (queued/downloading flags live in cards_).
    auto cardIt = std::find_if(loadedCards_.begin(),
                               loadedCards_.end(),
                               [&](const LinkCardNode& card)
                               {
                                   return card.HasUrl(url);
                               });
    if (cardIt != loadedCards_.end())
    {
        loadedCards_.erase(cardIt);
    }
    materializedCount_ = loadedCards_.size();

    if (info_.entries.empty())
    {
        // Removing the last entry effectively invalidates the group.
        info_.success = false;
        shouldClose_ = true;
        return true;
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

    loader_.Update();
    ApplyParseResultIfReady();
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
        if (CheckCollisionPointRec(mouse, expandBounds) && !isParsing_)
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
    CardChrome::DrawStackPeekCard(headerBounds, 2);
    CardChrome::DrawStackPeekCard(headerBounds, 1);
}

void LinkCardGroupNode::DrawHeader(Rectangle headerBounds, Font font) const
{
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
    DrawRectangleRounded(thumbnailBounds, kThumbnailRoundness, kThumbnailSegments, {28, 40, 28, 255});
    if (hasHeaderThumbnail_)
    {
        const Rectangle source = {
            0.0f, 0.0f, static_cast<float>(headerThumbnail_.width), static_cast<float>(headerThumbnail_.height)};
        DrawTexturePro(headerThumbnail_, source, thumbnailBounds, {0.0f, 0.0f}, 0.0f, WHITE);
    }
    else if (isParsing_)
    {
        DrawMiniSpinner(
            {thumbnailBounds.x + thumbnailBounds.width * 0.5f, thumbnailBounds.y + thumbnailBounds.height * 0.5f});
    }
    DrawRectangleRoundedLines(thumbnailBounds, kThumbnailRoundness, kThumbnailSegments, {64, 84, 64, 255});

    const float textX = headerBounds.x + CardChrome::kTextXOffset + kChevronWidth;
    const float titleMaxWidth = std::max(0.0f, headerBounds.width - (CardChrome::kTitleWidthInset + kChevronWidth));

    if (isParsing_)
    {
        DrawTextEx(font, "Parsing link...", {textX, headerBounds.y + 14.0f}, 18.0f, 0.0f, titleColor);
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

        std::string meta = FormatKindLabel(kind_) + " · " + FormatVideoCount(EntryCount());
        const std::string aggregate = BuildAggregateStatus();
        if (!aggregate.empty())
        {
            meta += " · " + aggregate;
        }
        DrawTextEx(font, meta.c_str(), {textX, headerBounds.y + 49.0f}, 14.0f, 0.0f, metaColor);
        const float metaWidth = MeasureTextEx(font, meta.c_str(), 14.0f, 0.0f).x;
        sourceBounds_ = {textX, headerBounds.y + 45.0f, metaWidth, 22.0f};
        hasSourceBounds_ = true;
        Tooltip::DrawIfHovered(font, sourceBounds_, "Open Link");
    }

    CardChrome::DrawCloseButton(headerBounds, font);
    CardChrome::DrawCopyButton(headerBounds, font, !isParsing_ && info_.success);
}

void LinkCardGroupNode::DrawRail(Rectangle headerBounds, float contentBottom) const
{
    if (!expanded_ || loadedCards_.empty())
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
    std::snprintf(label, sizeof(label), "Load more (%d remaining)", RemainingEntryCount());
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
