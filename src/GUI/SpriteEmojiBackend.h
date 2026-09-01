#pragma once

#include "IEmojiBackend.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <future>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct SpriteEmojiPack
{
    // Subfolder under cache/emoji/ and assets/emoji/ (e.g. "twemoji", "noto").
    const char* cacheSubdir = "twemoji";
    // Full URL prefix ending with '/', filename appended (e.g. 1f680.png).
    const char* urlPrefix = "";
    // Twemoji assets omit U+FE0F in filenames; RealityRipple Noto often keeps "-fe0f".
    bool omitFe0fInFilename = true;
};

class SpriteEmojiBackend final : public IEmojiBackend
{
public:
    // bundledRoot / cacheRoot are pack parents (…/emoji); pack.cacheSubdir is appended.
    SpriteEmojiBackend(std::filesystem::path bundledRoot, std::filesystem::path cacheRoot, SpriteEmojiPack pack);
    ~SpriteEmojiBackend() override;

    bool Supports(std::u32string_view sequence) const override;
    std::optional<EmojiSprite> GetOrLoad(const EmojiKey& key) override;
    void Pump() override;
    void UnloadAll() override;

    static SpriteEmojiPack TwemojiPack();
    static SpriteEmojiPack NotoPack();

private:
    struct CacheEntry
    {
        Texture2D texture{};
        std::uint64_t stamp = 0;
    };

    struct PendingLoad
    {
        std::u32string sequence;
        std::string filename;
        std::filesystem::path diskPath;
        std::future<bool> future;
    };

    // Hyphenated lowercase hex; optionally omit U+FE0F (Twemoji vs Noto CDN naming).
    static std::string SequenceToSpriteFilename(const std::u32string& sequence, bool omitFe0f);
    static std::vector<std::string> CandidateFilenames(const std::u32string& sequence, bool preferOmitFe0f);
    static bool DownloadHttpsFile(const std::string& url, const std::filesystem::path& destination);

    std::optional<EmojiSprite> TryLoadFromDisk(const EmojiKey& key, const std::filesystem::path& diskPath);
    void Touch(const std::string& cacheKey);
    void EvictIfNeeded();
    void
    EnqueueDownload(const std::u32string& sequence, const std::string& filename, const std::filesystem::path& diskPath);

    SpriteEmojiPack pack_{};
    std::filesystem::path bundledDirectory_;
    std::filesystem::path cacheDirectory_;
    std::unordered_map<std::string, CacheEntry> textures_;
    std::deque<std::string> lruOrder_;
    std::uint64_t stamp_ = 1;
    std::unordered_set<std::string> inFlight_;
    std::vector<PendingLoad> pending_;
    static constexpr size_t kMaxTextures = 64;
    static constexpr size_t kMaxConcurrentDownloads = 2;
};
