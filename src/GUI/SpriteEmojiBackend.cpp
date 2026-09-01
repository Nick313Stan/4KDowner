#include "SpriteEmojiBackend.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#include <windows.h>
#include <winhttp.h>
#undef CloseWindow
#undef ShowCursor
#undef DrawTextEx
#undef LoadImage
#undef DrawText
#else
#include <cstdlib>
#endif

namespace
{
std::string ToLowerHex(char32_t cp)
{
    std::ostringstream stream;
    stream << std::hex << static_cast<unsigned int>(cp);
    return stream.str();
}
} // namespace

SpriteEmojiPack SpriteEmojiBackend::TwemojiPack()
{
    return SpriteEmojiPack{"twemoji", "https://cdn.jsdelivr.net/gh/twitter/twemoji@14.0.2/assets/72x72/", true};
}

SpriteEmojiPack SpriteEmojiBackend::NotoPack()
{
    // Noto-style PNGs; keep FE0F in names (e.g. 2764-fe0f.png for red heart).
    return SpriteEmojiPack{"noto", "https://cdn.jsdelivr.net/gh/realityripple/emoji/noto/", false};
}

SpriteEmojiBackend::SpriteEmojiBackend(std::filesystem::path bundledRoot,
                                       std::filesystem::path cacheRoot,
                                       SpriteEmojiPack pack)
    : pack_(pack)
{
    bundledDirectory_ = std::move(bundledRoot);
    cacheDirectory_ = std::move(cacheRoot);
    if (pack_.cacheSubdir != nullptr && pack_.cacheSubdir[0] != '\0')
    {
        bundledDirectory_ /= pack_.cacheSubdir;
        cacheDirectory_ /= pack_.cacheSubdir;
    }
}

SpriteEmojiBackend::~SpriteEmojiBackend()
{
    UnloadAll();
}

bool SpriteEmojiBackend::Supports(std::u32string_view sequence) const
{
    return !sequence.empty();
}

std::string SpriteEmojiBackend::SequenceToSpriteFilename(const std::u32string& sequence, bool omitFe0f)
{
    std::string name;
    for (char32_t cp : sequence)
    {
        if (omitFe0f && cp == 0xFE0F)
        {
            continue;
        }
        if (!name.empty())
        {
            name.push_back('-');
        }
        name += ToLowerHex(cp);
    }
    name += ".png";
    return name;
}

std::vector<std::string> SpriteEmojiBackend::CandidateFilenames(const std::u32string& sequence, bool preferOmitFe0f)
{
    const std::string withoutFe0f = SequenceToSpriteFilename(sequence, true);
    const std::string withFe0f = SequenceToSpriteFilename(sequence, false);
    std::vector<std::string> names;
    if (preferOmitFe0f)
    {
        names.push_back(withoutFe0f);
        if (withFe0f != withoutFe0f)
        {
            names.push_back(withFe0f);
        }
    }
    else
    {
        names.push_back(withFe0f);
        if (withoutFe0f != withFe0f)
        {
            names.push_back(withoutFe0f);
        }
    }
    // If the cluster had no FE0F but Noto still ships "*-fe0f.png" (common for U+2764),
    // try appending fe0f once.
    if (!preferOmitFe0f && withoutFe0f == withFe0f && withoutFe0f.size() > 4)
    {
        const std::string forced = withoutFe0f.substr(0, withoutFe0f.size() - 4) + "-fe0f.png";
        if (std::find(names.begin(), names.end(), forced) == names.end())
        {
            names.push_back(forced);
        }
    }
    return names;
}

bool SpriteEmojiBackend::DownloadHttpsFile(const std::string& url, const std::filesystem::path& destination)
{
#ifdef _WIN32
    if (url.rfind("https://", 0) != 0)
    {
        return false;
    }
    const std::string withoutScheme = url.substr(8);
    const size_t slash = withoutScheme.find('/');
    if (slash == std::string::npos)
    {
        return false;
    }
    const std::string host = withoutScheme.substr(0, slash);
    const std::string path = withoutScheme.substr(slash);

    const int hostWideSize = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
    const int pathWideSize = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (hostWideSize <= 0 || pathWideSize <= 0)
    {
        return false;
    }
    std::wstring hostWide(static_cast<size_t>(hostWideSize - 1), L'\0');
    std::wstring pathWide(static_cast<size_t>(pathWideSize - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, hostWide.data(), hostWideSize);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, pathWide.data(), pathWideSize);

    HINTERNET session = WinHttpOpen(
        L"4KDowner/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
    {
        return false;
    }
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    HINTERNET connection = WinHttpConnect(session, hostWide.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connection == nullptr)
    {
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connection,
                                           L"GET",
                                           pathWide.c_str(),
                                           nullptr,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (request == nullptr)
    {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    const BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sent || !WinHttpReceiveResponse(request, nullptr))
    {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &statusCode,
                             &statusSize,
                             WINHTTP_NO_HEADER_INDEX) ||
        statusCode != 200)
    {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    std::filesystem::path tempPath = destination;
    tempPath += ".part";
    std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request, &available) && available > 0)
    {
        std::vector<char> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), available, &read) || read == 0)
        {
            break;
        }
        file.write(buffer.data(), static_cast<std::streamsize>(read));
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    file.close();

    std::error_code error;
    if (!std::filesystem::exists(tempPath, error) || std::filesystem::file_size(tempPath, error) < 32)
    {
        std::filesystem::remove(tempPath, error);
        return false;
    }
    std::filesystem::remove(destination, error);
    std::filesystem::rename(tempPath, destination, error);
    if (error)
    {
        std::filesystem::remove(tempPath, error);
        return false;
    }
    return true;
#else
    std::filesystem::path tempPath = destination;
    tempPath += ".part";
    const std::string command = "curl -fsSL --max-time 20 -o \"" + tempPath.string() + "\" \"" + url + "\"";
    if (std::system(command.c_str()) != 0)
    {
        std::error_code error;
        std::filesystem::remove(tempPath, error);
        return false;
    }
    std::error_code error;
    if (!std::filesystem::exists(tempPath, error) || std::filesystem::file_size(tempPath, error) < 32)
    {
        std::filesystem::remove(tempPath, error);
        return false;
    }
    std::filesystem::remove(destination, error);
    std::filesystem::rename(tempPath, destination, error);
    return !error;
#endif
}

std::optional<EmojiSprite> SpriteEmojiBackend::TryLoadFromDisk(const EmojiKey& key,
                                                               const std::filesystem::path& diskPath)
{
    std::error_code error;
    if (!std::filesystem::exists(diskPath, error))
    {
        return std::nullopt;
    }

    Image image = LoadImage(diskPath.string().c_str());
    if (image.data == nullptr)
    {
        // Drop corrupt/non-image cache (e.g. old 403 HTML saved as .png).
        std::filesystem::remove(diskPath, error);
        return std::nullopt;
    }
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (texture.id == 0)
    {
        std::filesystem::remove(diskPath, error);
        return std::nullopt;
    }
    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);

    const std::string cacheKey = diskPath.filename().string();
    CacheEntry entry;
    entry.texture = texture;
    entry.stamp = stamp_++;
    textures_[cacheKey] = entry;
    lruOrder_.push_back(cacheKey);
    EvictIfNeeded();

    EmojiSprite sprite;
    sprite.texture = texture;
    sprite.src = {0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)};
    sprite.advance = static_cast<float>(key.pixelSize);
    sprite.ascent = static_cast<float>(key.pixelSize);
    return sprite;
}

void SpriteEmojiBackend::Touch(const std::string& cacheKey)
{
    auto it = std::find(lruOrder_.begin(), lruOrder_.end(), cacheKey);
    if (it != lruOrder_.end())
    {
        lruOrder_.erase(it);
    }
    lruOrder_.push_back(cacheKey);
    auto found = textures_.find(cacheKey);
    if (found != textures_.end())
    {
        found->second.stamp = stamp_++;
    }
}

void SpriteEmojiBackend::EvictIfNeeded()
{
    while (textures_.size() > kMaxTextures && !lruOrder_.empty())
    {
        const std::string oldest = lruOrder_.front();
        lruOrder_.pop_front();
        auto it = textures_.find(oldest);
        if (it == textures_.end())
        {
            continue;
        }
        UnloadTexture(it->second.texture);
        textures_.erase(it);
    }
}

void SpriteEmojiBackend::EnqueueDownload(const std::u32string& sequence,
                                         const std::string& filename,
                                         const std::filesystem::path& diskPath)
{
    if (inFlight_.count(filename) != 0)
    {
        return;
    }
    if (pending_.size() >= kMaxConcurrentDownloads)
    {
        return;
    }

    inFlight_.insert(filename);
    const std::string urlPrefix = pack_.urlPrefix != nullptr ? pack_.urlPrefix : "";
    const bool preferOmit = pack_.omitFe0fInFilename;
    const std::vector<std::string> candidates = CandidateFilenames(sequence, preferOmit);
    PendingLoad job;
    job.sequence = sequence;
    job.filename = filename;
    job.diskPath = diskPath;
    job.future = std::async(std::launch::async,
                            [urlPrefix, diskPath, candidates]()
                            {
                                std::error_code error;
                                std::filesystem::create_directories(diskPath.parent_path(), error);
                                if (error)
                                {
                                    return false;
                                }
                                for (const std::string& candidate : candidates)
                                {
                                    if (DownloadHttpsFile(urlPrefix + candidate, diskPath))
                                    {
                                        return true;
                                    }
                                }
                                return false;
                            });
    pending_.push_back(std::move(job));
}

std::optional<EmojiSprite> SpriteEmojiBackend::GetOrLoad(const EmojiKey& key)
{
    if (key.sequence.empty() || key.pixelSize <= 0)
    {
        return std::nullopt;
    }

    const std::vector<std::string> candidates = CandidateFilenames(key.sequence, pack_.omitFe0fInFilename);
    if (candidates.empty() || candidates.front() == ".png")
    {
        return std::nullopt;
    }

    for (const std::string& filename : candidates)
    {
        auto cached = textures_.find(filename);
        if (cached != textures_.end())
        {
            Touch(filename);
            EmojiSprite sprite;
            sprite.texture = cached->second.texture;
            sprite.src = {0.0f,
                          0.0f,
                          static_cast<float>(cached->second.texture.width),
                          static_cast<float>(cached->second.texture.height)};
            sprite.advance = static_cast<float>(key.pixelSize);
            sprite.ascent = static_cast<float>(key.pixelSize);
            return sprite;
        }
    }

    // Prefer shipped assets, then Documents CDN cache.
    for (const std::string& filename : candidates)
    {
        if (std::optional<EmojiSprite> loaded = TryLoadFromDisk(key, bundledDirectory_ / filename))
        {
            return loaded;
        }
    }
    for (const std::string& filename : candidates)
    {
        if (std::optional<EmojiSprite> loaded = TryLoadFromDisk(key, cacheDirectory_ / filename))
        {
            return loaded;
        }
    }

    // Download into the preferred candidate path under cache only (never assets/).
    const std::string& preferred = candidates.front();
    EnqueueDownload(key.sequence, preferred, cacheDirectory_ / preferred);
    return std::nullopt;
}

void SpriteEmojiBackend::Pump()
{
    for (size_t index = 0; index < pending_.size();)
    {
        PendingLoad& job = pending_[index];
        if (job.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            ++index;
            continue;
        }

        const bool ok = job.future.get();
        inFlight_.erase(job.filename);
        if (ok)
        {
            // Texture upload happens lazily on next GetOrLoad from disk.
            (void)ok;
        }
        pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void SpriteEmojiBackend::UnloadAll()
{
    for (PendingLoad& job : pending_)
    {
        if (job.future.valid())
        {
            job.future.wait();
        }
    }
    pending_.clear();
    inFlight_.clear();
    for (auto& pair : textures_)
    {
        UnloadTexture(pair.second.texture);
    }
    textures_.clear();
    lruOrder_.clear();
}
