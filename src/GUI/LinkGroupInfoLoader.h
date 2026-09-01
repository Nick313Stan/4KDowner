#pragma once

#include "LinkInfoLoader.h"

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

enum class LinkGroupKind
{
    Playlist,
    Channel,
};

enum class ChannelContentTab
{
    Videos = 0,
    Shorts = 1,
    Lives = 2,
};

inline constexpr int kChannelTabCount = 3;
inline constexpr int kChannelPlaylistsTabIndex = 3;

struct LinkGroupEntry
{
    std::string id;
    std::string url;
    std::string title;
    std::string duration;
    std::string thumbnailPath;
    bool metadataLoaded = false;
};

struct LinkGroupInfo
{
    bool success = false;
    bool cancelled = false;
    bool isGroup = false;
    LinkGroupKind kind = LinkGroupKind::Playlist;
    std::string url;
    std::string title;
    std::string normalizedTitle;
    std::string uploader;
    std::string duration;
    std::string thumbnailPath;
    int entryCount = 0;
    std::string error;
    std::string errorLog;
    std::string parseBrowserReport;
    std::vector<LinkGroupEntry> entries;
    std::vector<LinkGroupEntry> videoEntries;
    std::vector<LinkGroupEntry> shortEntries;
    std::vector<LinkGroupEntry> liveEntries;
    std::vector<LinkGroupEntry> playlistEntries;
    bool hasChannelTabs = false;
    bool hasPlaylistShelf = false;
    LinkInfo singleVideo;
};

bool LooksLikeGroupUrl(const std::string& url);
bool LooksLikeChannelUrl(const std::string& url);
bool LooksLikePlaylistUrl(const std::string& url);
bool LooksLikeChannelPlaylistsUrl(const std::string& url);
// True for playlist/mix/shelf ids (PL…, UU…, LL, …). False for 11-char video ids that
// merely share a prefix (e.g. LLA3PiBmaQU must stay watch?v=, not playlist?list=).
bool LooksLikeYoutubePlaylistId(const std::string& id);
std::string NormalizeYoutubeChannelBaseUrl(std::string url);

class LinkGroupInfoLoader
{
public:
    LinkGroupInfoLoader() = default;
    ~LinkGroupInfoLoader();
    LinkGroupInfoLoader(const LinkGroupInfoLoader&) = delete;
    LinkGroupInfoLoader& operator=(const LinkGroupInfoLoader&) = delete;
    LinkGroupInfoLoader(LinkGroupInfoLoader&& other) noexcept;
    LinkGroupInfoLoader& operator=(LinkGroupInfoLoader&& other) noexcept;

    void Start(std::string url);
    void Cancel();
    void Update();
    static void ReapAbandoned();

    bool IsLoading() const;
    bool HasResult() const;
    const LinkGroupInfo& GetResult() const;

private:
    static LinkGroupInfo Load(std::string url, std::shared_ptr<std::atomic_bool> cancelRequested);
    void AbandonRunningWork();

    std::future<LinkGroupInfo> future_;
    std::shared_ptr<std::atomic_bool> cancelRequested_;
    LinkGroupInfo result_;
    bool isLoading_ = false;
    bool hasResult_ = false;
};

LinkInfo BuildPartialLinkInfoFromEntry(const LinkGroupEntry& entry);
