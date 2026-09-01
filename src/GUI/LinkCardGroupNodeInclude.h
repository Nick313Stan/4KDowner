#pragma once

#include "LinkCardNode.h"
#include "LinkGroupInfoLoader.h"

#include "raylib.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct EntryCompletionUndoState;

class LinkCardGroupNode
{
public:
    static constexpr float kStackPeek = 4.0f;
    static constexpr float kCollapsedExtra = 8.0f;
    static constexpr float kChildIndent = 12.0f;
    static constexpr float kLoadMoreHeight = 36.0f;
    static constexpr float kCardHeight = 75.0f;
    static constexpr float kChannelTabHeaderHeight = kCardHeight * 0.5f;
    static constexpr float kPlaylistShelfItemHeight = 58.0f;
    static constexpr float kGap = 5.0f;
    static constexpr size_t kPageSize = 50;
    // Prefetch full quality/format metadata for the first N playlist videos (not channels).
    static constexpr size_t kPlaylistDetailPrefetchCount = 21;
    static constexpr int kPlaylistDetailPrefetchParallel = 2;
    // Selected-card quality/format parse for channel /playlists shelf videos.
    static constexpr int kShelfSelectedDetailParseParallel = 3;
    // Legacy cap for bulk shelf prefetch (unused — shelf playlists load lazily on expand).
    static constexpr int kShelfPrefetchPlaylistLimit = 0;

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
    void DrawCollapseToFirstPage(Rectangle bounds, Font font, bool enabled) const;

    float CollapsedHeight() const;
    float ExpandedHeight() const;
    float TotalHeight() const;
    bool ShowsLoadMore() const;
    bool ShowsCollapseToFirstPage() const;
    bool CanCollapseToFirstPage() const;
    int LoadedChildCount() const;
    int RemainingEntryCount() const;

    bool IsExpanded() const;
    void SetExpanded(bool expanded);
    void ToggleExpanded();
    bool IsHeaderSelected() const;
    void SetHeaderSelected(bool selected);
    bool IsParsing() const;
    bool IsChannelPresentationPending() const;
    bool IsShelfBootstrapPending() const;
    int ShelfBootstrapCompletedCount() const;
    int ShelfBootstrapTotalCount() const;
    bool IsValid() const;
    const std::string& Error() const;
    bool ShouldClose() const;
    void RequestClose();
    bool WasHeaderClicked() const;
    bool WasExpandToggleClicked() const;
    bool WasLoadMoreClicked() const;
    bool WasCopyClicked() const;
    bool WasSourceClicked() const;
    bool IsHeaderHovered() const;
    bool TryToggleExpandShortcut();

    void TriggerPulse();
    bool HasUrl(const std::string& url) const;
    const std::string& Url() const;
    const std::string& Title() const;
    LinkGroupKind Kind() const;
    int EntryCount() const;
    const LinkGroupEntry* EntryAt(int index) const;
    const LinkGroupEntry* ChannelTabEntryAt(int tab, int index) const;
    LinkCardNode* FindLoadedCardByUrl(const std::string& url);
    const LinkCardNode* FindLoadedCardByUrl(const std::string& url) const;

    std::vector<LinkCardNode>& LoadedCards();
    const std::vector<LinkCardNode>& LoadedCards() const;
    void ForEachLoadedCard(const std::function<void(LinkCardNode&)>& visitor);
    void ForEachLoadedCard(const std::function<void(const LinkCardNode&)>& visitor) const;
    void LoadNextPage();
    void LoadAllPages();
    void CollapseToFirstPage();
    void EnsureFirstPageLoaded();

    bool UsesChannelTabs() const;
    bool UsesPlaylistShelf() const;
    bool IsPlaylistsTabExpanded() const;
    bool IsPlaylistsTabSelected() const;
    bool IsPlaylistsTabHovered() const;
    bool WasPlaylistsTabClicked() const;
    bool WasPlaylistsTabExpandClicked() const;
    void SetPlaylistsTabSelected(bool selected);
    void SetPlaylistsTabExpanded(bool expanded);
    void TogglePlaylistsTabExpanded();
    void ClearPlaylistsTabSelection();
    float PlaylistsTabBlockHeight() const;
    float PlaylistsTabOffsetFromHeader() const;
    void UpdatePlaylistsTab(Rectangle bounds, Font font);
    void DrawPlaylistsTab(Rectangle bounds, Font font) const;
    int PlaylistShelfCount() const;
    int PlaylistShelfLoadedCount() const;
    bool PlaylistShelfShowsLoadMore() const;
    int PlaylistShelfRemainingCount() const;
    bool IsPlaylistShelfItemExpanded(int index) const;
    bool IsPlaylistShelfItemSelected(int index) const;
    bool IsPlaylistShelfItemHovered(int index) const;
    bool WasPlaylistShelfItemClicked(int index) const;
    bool WasPlaylistShelfItemExpandClicked(int index) const;
    bool IsPlaylistShelfItemDismissed(int index) const;
    bool WasPlaylistShelfItemCloseClicked(int index) const;
    bool WasPlaylistShelfItemRestoreClicked(int index) const;
    void DismissPlaylistShelfItem(int index);
    void RestorePlaylistShelfItem(int index);
    void SetPlaylistShelfItemSelected(int index, bool selected);
    void ClearPlaylistShelfSelection();
    int SelectedPlaylistShelfIndex() const;
    void SetPlaylistShelfItemExpanded(int index, bool expanded);
    void TogglePlaylistShelfItemExpanded(int index);
    LinkCardGroupNode* PlaylistShelfPlaylist(int index);
    const LinkCardGroupNode* PlaylistShelfPlaylist(int index) const;
    const LinkGroupEntry* PlaylistShelfEntry(int index) const;
    float PlaylistShelfItemBlockHeight(int index) const;
    float PlaylistShelfItemOffsetFromHeader(int index) const;
    void UpdatePlaylistShelfItem(int index, Rectangle bounds, Font font);
    void DrawPlaylistShelfItem(int index, Rectangle bounds, Font font) const;
    void EnsurePlaylistShelfItemLoaded(int index);
    void LoadAllPlaylistShelfPlaylists();

    bool IsPlaylistShelfReady() const;
    // True while parse, lazy shelf load, or expanded-group background work may be needed.
    bool NeedsBackgroundWork() const;

    int ChannelTabEntryCount(int tab) const;
    bool IsChannelTabExpanded(int tab) const;
    bool IsChannelTabSelected(int tab) const;
    bool IsChannelTabHovered(int tab) const;
    bool WasChannelTabClicked(int tab) const;
    bool WasChannelTabExpandClicked(int tab) const;
    bool IsChannelTabDismissed(int tab) const;
    bool WasChannelTabCloseClicked(int tab) const;
    bool WasChannelTabRestoreClicked(int tab) const;
    void DismissChannelTab(int tab);
    void RestoreChannelTab(int tab);
    bool IsPlaylistsTabDismissed() const;
    bool WasPlaylistsTabCloseClicked() const;
    bool WasPlaylistsTabRestoreClicked() const;
    void DismissPlaylistsTab();
    void RestorePlaylistsTab();
    bool IsUrlInDismissedTab(const std::string& url) const;
    void SetChannelTabSelected(int tab, bool selected);
    void ClearChannelTabSelection();
    void ClearLoadedCardSelection();
    bool AnyChannelTabSelected() const;
    int SelectedChannelTab() const;
    void SetChannelTabExpanded(int tab, bool expanded);
    void ToggleChannelTabExpanded(int tab);
    std::vector<LinkCardNode>& ChannelTabLoadedCards(int tab);
    const std::vector<LinkCardNode>& ChannelTabLoadedCards(int tab) const;
    int ChannelTabLoadedCount(int tab) const;
    bool ChannelTabShowsLoadMore(int tab) const;
    bool ChannelTabShowsCollapseToFirstPage(int tab) const;
    bool ChannelTabCanCollapseToFirstPage(int tab) const;
    int ChannelTabRemainingCount(int tab) const;
    void LoadNextPageForTab(int tab);
    void LoadAllPagesForTab(int tab);
    void CollapseToFirstPageForTab(int tab);
    void SetDurationFillSuspended(bool suspended);
    // Advance flat-parse completion, duration fill, and playlist detail prefetch without UI hit-testing.
    void PumpBackgroundWork();
    float ChannelTabBlockHeight(int tab) const;
    float ChannelTabOffsetFromHeader(int tab) const;
    void UpdateChannelTab(int tab, Rectangle bounds, Font font);
    void DrawChannelTab(int tab, Rectangle bounds, Font font) const;
    void DrawChannelTabLoadMore(int tab, Rectangle bounds, Font font) const;
    void DrawChannelTabCollapseToFirstPage(int tab, Rectangle bounds, Font font) const;

    std::string BuildAggregateStatus() const;
    int CountActiveDownloads() const;
    int CountCompletedDownloads() const;
    int CountChannelTabEntries() const;
    // Download All / batch queue: track expected vs finished beyond the loaded first page.
    void RegisterBatchDownload(const std::string& url);
    void NotifyBatchDownloadFinished(const std::string& url, double elapsedSeconds);
    void ClearDownloadBatch();
    bool HasActiveDownloadBatch() const;
    bool HasUnfinishedChannelTabBatchWork() const;
    bool HasPendingPlaylistShelfEnqueue() const;
    void RequestPlaylistShelfForceParse(int index);
    bool WasPlaylistShelfForceParseClicked(int index);
    bool IsShelfLoadAllRequested() const;
    struct PlaylistShelfTabActivity
    {
        enum class Kind
        {
            None,
            Parsing,
            Queued,
            Downloading
        };
        Kind kind = Kind::None;
    };
    PlaylistShelfTabActivity GetPlaylistShelfTabActivity() const;
    PlaylistShelfTabActivity GetPlaylistShelfItemActivity(int index) const;
    double BatchDownloadElapsedSum() const;
    // Persist completion for entries downloaded via ephemeral hosts (beyond loaded pages)
    // so Load more can restore checkmarks / took time / output path.
    void RememberEntryDownloadCompletion(const std::string& url, double elapsedSeconds, const std::string& outputPath);
    void ForgetEntryDownloadCompletion(const std::string& url);
    void RememberEntryConvertCompletion(const std::string& url, double elapsedSeconds, const std::string& outputPath);
    void CaptureCardCompletion(const LinkCardNode& card);
    void UpdateEntryTitleByUrl(const std::string& url, const std::string& title);
    // Materialize group entries so batch enqueue binds queue/download state to visible cards.
    void MaterializeEntriesForBatchDownload();

    bool IsEntryDismissed(const std::string& url) const;
    bool TryGetEntryCompletionUndoState(const std::string& url, EntryCompletionUndoState& out) const;
    void DismissEntry(const std::string& url);
    void RestoreEntry(const std::string& url);

    bool RemoveEntryByUrl(const std::string& url, LinkGroupEntry& removedEntry, size_t& removedIndex);
    bool
    RemoveChannelTabEntryByUrl(int tab, const std::string& url, LinkGroupEntry& removedEntry, size_t& removedIndex);
    bool RemovePlaylistShelfChildEntryByUrl(int shelfIndex,
                                            const std::string& url,
                                            LinkGroupEntry& removedEntry,
                                            size_t& removedIndex);
    void InsertEntry(size_t index, const LinkGroupEntry& entry);
    void InsertChannelTabEntry(int tab, size_t index, const LinkGroupEntry& entry);
    void InsertPlaylistShelfChildEntry(int shelfIndex, size_t index, const LinkGroupEntry& entry);
    void SnapshotBatchEntryBeforeRemove(const std::string& url, bool& wasInBatch, bool& wasFinished) const;
    void RestoreBatchEntryOnUndo(const std::string& url, bool wasInBatch, bool wasFinished, double elapsedSeconds);
    void RestoreEntryCompletionOnUndo(const std::string& url,
                                      double elapsedSeconds,
                                      bool hasElapsed,
                                      const std::string& outputPath);

    DownloadOptions& Options();
    const DownloadOptions& Options() const;
    bool& ChannelTabKeepNumbering(int tab);
    bool ChannelTabKeepNumbering(int tab) const;
    bool& ChannelTabInverseNumbering(int tab);
    bool ChannelTabInverseNumbering(int tab) const;

    bool TryConsumeParseFailure(std::string& url, std::string& error);
    bool TryConsumeParseSuccess(std::string& url);

    bool ShouldPromoteToSingle() const;
    LinkInfo TakePromoteSingleInfo();

private:
    struct AdoptParsedNestedPlaylistTag
    {
    };

    enum class ShelfPrefetchState
    {
        None,
        Running,
        Ready,
    };

    struct ChannelTabUi
    {
        std::vector<LinkGroupEntry> entries;
        std::vector<LinkCardNode> loaded;
        size_t materialized = 0;
        bool expanded = false;
        bool selected = false;
        bool hovered = false;
        bool wasClicked = false;
        bool wasExpandClicked = false;
        bool dismissed = false;
        bool wasCloseClicked = false;
        bool wasRestoreClicked = false;
        bool dismissOverlayHovered = false;
        bool keepNumbering = true;
        bool inverseNumbering = false;
    };

    struct PlaylistShelfItem
    {
        LinkGroupEntry entry;
        std::unique_ptr<LinkCardGroupNode> playlist;
        bool expanded = false;
        bool selected = false;
        bool hovered = false;
        bool wasClicked = false;
        bool wasExpandClicked = false;
        bool dismissed = false;
        bool wasCloseClicked = false;
        bool wasRestoreClicked = false;
        bool dismissOverlayHovered = false;
        Texture2D entryThumbnail_{};
        bool hasEntryThumbnail_ = false;
        bool triedEntryThumbnail_ = false;
        bool forceDetailParseRequested_ = false;
        bool wasForceParseClicked_ = false;
        std::vector<size_t> forceDetailParseQueue_;
        mutable Rectangle forceParseBounds_{};
        mutable bool hasForceParseBounds_ = false;
    };

    struct EntryCompletionState
    {
        bool hasDownloadElapsed = false;
        double downloadElapsedSeconds = 0.0;
        bool hasConvertElapsed = false;
        double convertElapsedSeconds = 0.0;
        std::string lastDownloadedPath;
    };

    void ApplyParseResultIfReady();
    void MaterializePage(size_t start, size_t end);
    void MaterializeTabPage(int tab, size_t start, size_t end);
    void MaterializePlaylistShelfPage(size_t start, size_t end);
    void ApplyRememberedCompletionToCard(LinkCardNode& card) const;
    void ApplyDismissedStateToCard(LinkCardNode& card) const;
    const EntryCompletionState* FindEntryCompletion(const std::string& url) const;
    ChannelTabUi* TabAt(int tab);
    const ChannelTabUi* TabAt(int tab) const;
    PlaylistShelfItem* ShelfItemAt(int index);
    const PlaylistShelfItem* ShelfItemAt(int index) const;
    void EnsureTabFirstPageLoaded(int tab);
    void FinalizeLazyPlaylistShelf();
    void QueueShelfPreviewLoads();
    void QueueAllShelfPreviewLoads();
    void StartChannelShelfBootstrap();
    void TryFinishShelfBootstrap();
    void PumpShelfLazyLoad();
    void StartShelfLazyLoadAt(int index);
    void AttachShelfLazyLoadResult(int index, LinkGroupInfo result);
    void LoadPlaylistShelfEntryThumbnail(int index);
    void UnloadPlaylistShelfEntryThumbnail(PlaylistShelfItem& item);
    void UnloadAllPlaylistShelfEntryThumbnails();
    void PumpShelfSelectedDetailParse();
    void PumpPlaylistForceDetailParse();
    void PumpChannelTabDetailPrefetch();
    static std::string NormalizeShelfPlaylistUrl(const LinkGroupEntry& entry);
    static std::unique_ptr<LinkCardGroupNode> CreateAdoptedNestedPlaylist(LinkGroupInfo info);
    explicit LinkCardGroupNode(LinkGroupInfo info, AdoptParsedNestedPlaylistTag);
    void CancelDurationFill();
    void PumpDurationFill();
    void PumpDetailPrefetch();
    void SyncEntryTitlesFromLoadedCards();
    void ForgetEntryTracking(const std::string& url);
    void RebuildFlatEntriesFromTabs();
    bool GroupHasNoRemainingContent() const;
    std::vector<LinkGroupEntry>* CategoryEntriesForTab(int tab);
    Rectangle GetExpandToggleBounds(Rectangle headerBounds) const;
    Rectangle GetThumbnailBounds(Rectangle headerBounds) const;
    void LoadHeaderThumbnail();
    void UnloadHeaderThumbnail();

    LinkGroupKind kind_ = LinkGroupKind::Playlist;
    LinkGroupInfo info_;
    LinkGroupInfoLoader loader_;
    LinkGroupInfoLoader shelfPrefetchLoader_;
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
    std::array<ChannelTabUi, kChannelTabCount> channelTabs_{};
    bool usesChannelTabs_ = false;
    bool usesPlaylistShelf_ = false;
    bool playlistTabExpanded_ = false;
    bool playlistTabSelected_ = false;
    bool playlistTabHovered_ = false;
    bool playlistTabWasClicked_ = false;
    bool playlistTabWasExpandClicked_ = false;
    bool playlistTabDismissed_ = false;
    bool playlistTabWasCloseClicked_ = false;
    bool playlistTabWasRestoreClicked_ = false;
    bool playlistTabDismissOverlayHovered_ = false;
    ShelfPrefetchState shelfPrefetchState_ = ShelfPrefetchState::None;
    int shelfLazyLoadIndex_ = -1;
    std::vector<int> shelfLazyLoadQueue_;
    bool shelfLoadAllRequested_ = false;
    bool shelfPreviewQueueStarted_ = false;
    bool shelfBootstrapPending_ = false;
    bool previewOnlyGroup_ = false;
    std::vector<PlaylistShelfItem> playlistShelf_;
    Texture2D headerThumbnail_{};
    bool hasHeaderThumbnail_ = false;
    bool triedHeaderThumbnail_ = false;
    mutable Rectangle sourceBounds_{};
    mutable bool hasSourceBounds_ = false;
    double pulseStartTime_ = -10.0;
    std::future<std::vector<std::pair<std::string, std::string>>> durationFillFuture_;
    std::shared_ptr<std::atomic_bool> durationFillCancel_;
    std::vector<std::string> durationFillBatchUrls_;
    double durationFillNextAllowedAt_ = 0.0;
    bool durationFillSuspended_ = false;
    bool detailPrefetchSuspended_ = false;
    int batchDownloadExpected_ = 0;
    int batchDownloadCompleted_ = 0;
    double batchDownloadElapsedSum_ = 0.0;
    std::unordered_set<std::string> batchDownloadUrls_;
    std::unordered_set<std::string> batchDownloadFinishedUrls_;
    std::unordered_map<std::string, EntryCompletionState> entryCompletions_;
    std::unordered_set<std::string> dismissedEntryUrls_;
};
