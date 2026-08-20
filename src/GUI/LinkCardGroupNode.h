#pragma once

#include "LinkCardNode.h"
#include "LinkGroupInfoLoader.h"

#include "raylib.h"

#include <cstddef>
#include <string>
#include <vector>

class LinkCardGroupNode
{
public:
    static constexpr float kStackPeek = 4.0f;
    static constexpr float kCollapsedExtra = 8.0f;
    static constexpr float kChildIndent = 12.0f;
    static constexpr float kLoadMoreHeight = 36.0f;
    static constexpr float kCardHeight = 75.0f;
    static constexpr float kGap = 5.0f;
    static constexpr size_t kPageSize = 50;

    explicit LinkCardGroupNode(std::string url);
    ~LinkCardGroupNode();
    LinkCardGroupNode(const LinkCardGroupNode&) = delete;
    LinkCardGroupNode& operator=(const LinkCardGroupNode&) = delete;
    LinkCardGroupNode(LinkCardGroupNode&& other) noexcept;
    LinkCardGroupNode& operator=(LinkCardGroupNode&& other) noexcept;

    void Update(Rectangle headerBounds, Font font);
    void DrawStackPeeks(Rectangle headerBounds) const;
    void DrawHeader(Rectangle headerBounds, Font font) const;
    void DrawRail(Rectangle headerBounds, float contentBottom) const;
    void DrawLoadMore(Rectangle bounds, Font font) const;

    float CollapsedHeight() const;
    float ExpandedHeight() const;
    float TotalHeight() const;
    bool ShowsLoadMore() const;
    int LoadedChildCount() const;
    int RemainingEntryCount() const;

    bool IsExpanded() const;
    void SetExpanded(bool expanded);
    void ToggleExpanded();
    bool IsHeaderSelected() const;
    void SetHeaderSelected(bool selected);
    bool IsParsing() const;
    bool IsValid() const;
    bool ShouldClose() const;
    void RequestClose();
    bool WasHeaderClicked() const;
    bool WasExpandToggleClicked() const;
    bool WasLoadMoreClicked() const;
    bool WasCopyClicked() const;
    bool WasSourceClicked() const;
    bool IsHeaderHovered() const;
    bool TryToggleExpandShortcut();

    bool HasUrl(const std::string& url) const;
    const std::string& Url() const;
    const std::string& Title() const;
    LinkGroupKind Kind() const;
    int EntryCount() const;

    std::vector<LinkCardNode>& LoadedCards();
    const std::vector<LinkCardNode>& LoadedCards() const;
    void LoadNextPage();
    void LoadAllPages();
    void EnsureFirstPageLoaded();

    std::string BuildAggregateStatus() const;
    int CountActiveDownloads() const;
    int CountCompletedDownloads() const;

    // Removes exactly one playlist/channel entry (video) by its card URL.
    // Also keeps the group card itself alive (unless it becomes empty).
    bool RemoveEntryByUrl(const std::string& url, LinkGroupEntry& removedEntry, size_t& removedIndex);
    void InsertEntry(size_t index, const LinkGroupEntry& entry);

    DownloadOptions& Options();
    const DownloadOptions& Options() const;

    bool TryConsumeParseFailure(std::string& url, std::string& error);
    bool TryConsumeParseSuccess(std::string& url);

    bool ShouldPromoteToSingle() const;
    LinkInfo TakePromoteSingleInfo();

private:
    void ApplyParseResultIfReady();
    void MaterializePage(size_t start, size_t end);
    Rectangle GetExpandToggleBounds(Rectangle headerBounds) const;
    Rectangle GetThumbnailBounds(Rectangle headerBounds) const;
    void LoadHeaderThumbnail();
    void UnloadHeaderThumbnail();

    LinkGroupKind kind_ = LinkGroupKind::Playlist;
    LinkGroupInfo info_;
    LinkGroupInfoLoader loader_;
    bool isParsing_ = true;
    bool expanded_ = false;
    bool headerSelected_ = false;
    bool headerHovered_ = false;
    bool shouldClose_ = false;
    bool wasHeaderClicked_ = false;
    bool wasExpandToggleClicked_ = false;
    bool wasLoadMoreClicked_ = false;
    bool wasCopyClicked_ = false;
    bool wasSourceClicked_ = false;
    bool pendingParseErrorReport_ = false;
    bool pendingParseSuccessReport_ = false;
    bool pendingPromoteSingle_ = false;
    LinkInfo promoteSingleInfo_;
    DownloadOptions options_;
    size_t materializedCount_ = 0;
    std::vector<LinkCardNode> loadedCards_;
    Texture2D headerThumbnail_{};
    bool hasHeaderThumbnail_ = false;
    bool triedHeaderThumbnail_ = false;
    mutable Rectangle sourceBounds_{};
    mutable bool hasSourceBounds_ = false;
};
