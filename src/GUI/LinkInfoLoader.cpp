#include "LinkInfoLoader.h"

#include "BrowserDiagnostics.h"
#include "DownloadFormatPredictor.h"
#include "VideoTitle.h"
#include "WinProcess.h"
#include "YtDlpLocator.h"
#include "YtDlpYouTube.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

namespace
{
    struct CommandResult
    {
        int exitCode = 0;
        std::string output;
        bool cancelled = false;
    };

    std::vector<std::string> SplitLines(const std::string &text)
    {
        std::vector<std::string> lines;
        std::stringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (!line.empty())
            {
                lines.push_back(line);
            }
        }
        return lines;
    }

    std::filesystem::path FindExecutableInPath(const std::string &executableName)
    {
        const char *pathValue = std::getenv("PATH");
        if (pathValue != nullptr)
        {
#ifdef _WIN32
            const char delimiter = ';';
#else
            const char delimiter = ':';
#endif
            std::stringstream stream(pathValue);
            std::string segment;
            while (std::getline(stream, segment, delimiter))
            {
                if (segment.empty())
                {
                    continue;
                }

                for (const std::string &candidateName : {executableName, executableName + ".exe"})
                {
                    const std::filesystem::path candidate = std::filesystem::path(segment) / candidateName;
                    if (std::filesystem::exists(candidate))
                    {
                        return std::filesystem::absolute(candidate);
                    }
                }
            }
        }

        return {};
    }

    std::filesystem::path FindFfmpegDirectory()
    {
        const std::array<std::filesystem::path, 2> relativePaths = {
            std::filesystem::path("4kdowner.shared") / "packages" / "ffmpeg" / "bin" / "ffmpeg.exe",
            std::filesystem::path("packages") / "ffmpeg" / "bin" / "ffmpeg.exe"};
        for (const std::filesystem::path &relativePath : relativePaths)
        {
            std::filesystem::path directory = std::filesystem::current_path();
            while (!directory.empty())
            {
                const std::filesystem::path candidate = directory / relativePath;
                if (std::filesystem::exists(candidate))
                {
                    return std::filesystem::absolute(candidate.parent_path());
                }

                const std::filesystem::path parent = directory.parent_path();
                if (parent == directory)
                {
                    break;
                }
                directory = parent;
            }
        }

        const std::filesystem::path pathFromEnvironment = FindExecutableInPath("ffmpeg.exe");
        return pathFromEnvironment.empty() ? std::filesystem::path{} : pathFromEnvironment.parent_path();
    }

    std::filesystem::path FindThumbnailPath(const std::filesystem::path &cacheDirectory, const std::string &videoId)
    {
        const std::array<const char *, 4> extensions = {".jpg", ".jpeg", ".png", ".webp"};
        for (const char *extension : extensions)
        {
            const std::filesystem::path candidate = cacheDirectory / (videoId + extension);
            if (std::filesystem::exists(candidate))
            {
                return std::filesystem::absolute(candidate);
            }
        }

        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(cacheDirectory, error))
        {
            if (error || !entry.is_regular_file())
            {
                continue;
            }

            const std::string stem = entry.path().stem().string();
            if (stem == videoId || stem.rfind(videoId, 0) == 0)
            {
                return std::filesystem::absolute(entry.path());
            }
        }

        return {};
    }

    bool IsLoadableImagePath(const std::filesystem::path& path)
    {
        const std::string ext = path.extension().string();
        std::string lower = ext;
        for (char& c : lower)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return lower == ".jpg" || lower == ".jpeg" || lower == ".png" || lower == ".bmp";
    }

    bool IsJpegFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return false;
        }

        unsigned char header[3]{};
        file.read(reinterpret_cast<char*>(header), 3);
        return file.gcount() == 3 && header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF;
    }

#ifdef _WIN32
    bool DownloadHttpToFile(const std::string& url, const std::filesystem::path& destination)
    {
        if (url.rfind("https://", 0) != 0)
        {
            return false;
        }

        const std::string rest = url.substr(8);
        const size_t slash = rest.find('/');
        if (slash == std::string::npos)
        {
            return false;
        }

        const std::string host = rest.substr(0, slash);
        const std::string path = rest.substr(slash);
        const std::wstring hostWide(host.begin(), host.end());
        const std::wstring pathWide(path.begin(), path.end());

        HINTERNET session = WinHttpOpen(
            L"4KDowner/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
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

        HINTERNET request = WinHttpOpenRequest(
            connection,
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
        WinHttpAddRequestHeaders(
            request,
            L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) 4KDowner/1.0\r\n",
            static_cast<DWORD>(-1L),
            WINHTTP_ADDREQ_FLAG_ADD);

        const BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (!sent || !WinHttpReceiveResponse(request, nullptr))
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
        std::error_code existsError;
        if (!std::filesystem::exists(tempPath, existsError) || std::filesystem::file_size(tempPath, existsError) < 128)
        {
            std::filesystem::remove(tempPath, existsError);
            return false;
        }

        if (!IsJpegFile(tempPath))
        {
            std::filesystem::remove(tempPath, existsError);
            return false;
        }

        std::filesystem::rename(tempPath, destination, existsError);
        if (existsError)
        {
            std::filesystem::remove(tempPath, existsError);
            return false;
        }

        return std::filesystem::exists(destination, existsError) && IsLoadableImagePath(destination);
    }
#else
    bool DownloadHttpToFile(const std::string&, const std::filesystem::path&)
    {
        return false;
    }
#endif

    std::filesystem::path ResolveThumbnailPath(
        const std::filesystem::path& cacheDirectory,
        const std::string& videoId,
        const std::string& thumbnailUrl)
    {
        std::filesystem::path existing = FindThumbnailPath(cacheDirectory, videoId);
        if (!existing.empty() && IsLoadableImagePath(existing))
        {
            return existing;
        }

        if (videoId.empty())
        {
            return {};
        }

        const std::filesystem::path jpgPath = cacheDirectory / (videoId + ".jpg");
        const auto tryDownload = [&](const std::string& url) {
            return !url.empty() && DownloadHttpToFile(url, jpgPath);
        };

        const std::array<const char*, 2> youtubeQualities = {"hqdefault", "mqdefault"};
        for (const char* quality : youtubeQualities)
        {
            const std::string youtubeUrl = "https://i.ytimg.com/vi/" + videoId + "/" + quality + ".jpg";
            if (tryDownload(youtubeUrl))
            {
                return std::filesystem::absolute(jpgPath);
            }
        }

        if (tryDownload(thumbnailUrl))
        {
            return std::filesystem::absolute(jpgPath);
        }

        return {};
    }

    std::filesystem::path GetLinkInfoCacheDirectory()
    {
#ifdef _WIN32
        char *localAppData = nullptr;
        size_t localAppDataSize = 0;
        if (_dupenv_s(&localAppData, &localAppDataSize, "LOCALAPPDATA") == 0 && localAppData != nullptr && localAppData[0] != '\0')
        {
            const std::filesystem::path path = std::filesystem::path(localAppData) / "4KDowner" / "cache" / "link-info";
            std::free(localAppData);
            return path;
        }
        std::free(localAppData);
#else
        const char *home = std::getenv("HOME");
        if (home != nullptr && home[0] != '\0')
        {
            return std::filesystem::path(home) / ".cache" / "4KDowner" / "link-info";
        }
#endif

        std::error_code error;
        const std::filesystem::path temp = std::filesystem::temp_directory_path(error);
        return (error ? std::filesystem::path("cache") : temp / "4KDowner" / "cache") / "link-info";
    }

    std::string ValueAfterPrefix(const std::vector<std::string> &lines, const std::string &prefix)
    {
        for (const std::string &line : lines)
        {
            if (line.rfind(prefix, 0) == 0)
            {
                return line.substr(prefix.size());
            }
        }

        return {};
    }

    std::string Trim(std::string value)
    {
        while (!value.empty() && value.front() == ' ')
        {
            value.erase(value.begin());
        }
        while (!value.empty() && value.back() == ' ')
        {
            value.pop_back();
        }
        return value;
    }

    std::string ToUpper(std::string value)
    {
        for (char &c : value)
        {
            if (c >= 'a' && c <= 'z')
            {
                c = static_cast<char>(c - 'a' + 'A');
            }
        }
        return value;
    }

    std::string NormalizeDurationString(const std::string &value)
    {
        if (value.empty())
        {
            return "--:--";
        }

        if (value.find(':') != std::string::npos)
        {
            return value;
        }

        try
        {
            const int totalSeconds = std::stoi(value);
            const int minutes = totalSeconds / 60;
            const int seconds = totalSeconds % 60;
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, seconds);
            return buffer;
        }
        catch (...)
        {
            return value;
        }
    }

    std::string NormalizeCodecName(std::string value)
    {
        value = Trim(value);
        if (value.empty() || value == "none" || value == "None" || value == "NA")
        {
            return "None";
        }
        if (value.rfind("avc1", 0) == 0 || value.rfind("h264", 0) == 0)
        {
            return "H.264";
        }
        if (value.rfind("hev1", 0) == 0 || value.rfind("hvc1", 0) == 0 || value.rfind("hevc", 0) == 0 || value.rfind("h265", 0) == 0)
        {
            return "H.265";
        }
        if (value.rfind("av01", 0) == 0 || value.rfind("av1", 0) == 0)
        {
            return "AV1";
        }
        if (value.rfind("vp09", 0) == 0 || value.rfind("vp9", 0) == 0)
        {
            return "VP9";
        }
        if (value.rfind("mp4a", 0) == 0 || value.rfind("aac", 0) == 0)
        {
            return "AAC";
        }
        if (value == "opus")
        {
            return "Opus";
        }
        return value;
    }

    std::vector<std::string> ParseAvailableFormats(const std::string &commaSeparatedExts)
    {
        std::set<std::string> seen;
        std::stringstream stream(commaSeparatedExts);
        std::string item;

        while (std::getline(stream, item, ','))
        {
            std::string ext = ToUpper(Trim(item));
            if (ext.empty() || ext == "MHTML")
            {
                continue;
            }
            seen.insert(ext);
        }

        std::vector<std::string> preferred;
        const std::vector<std::string> order = {"MP4", "WEBM", "M4A", "MKV", "MP3", "OPUS"};
        for (const std::string &ext : order)
        {
            if (seen.erase(ext) > 0)
            {
                preferred.push_back(ext);
            }
        }
        for (const std::string &ext : seen)
        {
            preferred.push_back(ext);
        }

        if (preferred.empty())
        {
            preferred.push_back("MP4");
        }

        return preferred;
    }

    std::vector<std::string> SplitCommaSeparated(const std::string &value)
    {
        std::vector<std::string> items;
        std::stringstream stream(value);
        std::string item;
        while (std::getline(stream, item, ','))
        {
            items.push_back(Trim(item));
        }
        return items;
    }

    std::vector<std::string> ParseAvailableAudioFormats(
        const std::string &commaSeparatedExts,
        const std::string &commaSeparatedVideoCodecs,
        const std::string &commaSeparatedAudioCodecs)
    {
        const std::vector<std::string> exts = SplitCommaSeparated(commaSeparatedExts);
        const std::vector<std::string> videoCodecs = SplitCommaSeparated(commaSeparatedVideoCodecs);
        const std::vector<std::string> audioCodecs = SplitCommaSeparated(commaSeparatedAudioCodecs);
        std::set<std::string> seen;

        for (size_t index = 0; index < exts.size(); ++index)
        {
            const std::string videoCodec = index < videoCodecs.size() ? videoCodecs[index] : "";
            const std::string audioCodec = index < audioCodecs.size() ? audioCodecs[index] : "";
            std::string ext = ToUpper(exts[index]);
            if (ext.empty() || ext == "MHTML")
            {
                continue;
            }
            if ((videoCodec == "none" || videoCodec == "None") && audioCodec != "none" && audioCodec != "None" && !audioCodec.empty())
            {
                seen.insert(ext);
            }
        }

        std::vector<std::string> result;
        const std::vector<std::string> order = {"M4A", "MP3", "WEBM", "OPUS", "AAC", "WAV", "FLAC"};
        for (const std::string &preferred : order)
        {
            if (seen.erase(preferred) > 0)
            {
                result.push_back(preferred);
            }
        }
        for (const std::string &ext : seen)
        {
            result.push_back(ext);
        }

        if (std::find(result.begin(), result.end(), "MP3") == result.end())
        {
            result.push_back("MP3");
        }
        if (result.empty())
        {
            result.push_back("M4A");
        }

        return result;
    }

    std::vector<std::string> ParseAvailableVideoFormats(
        const std::string &commaSeparatedExts,
        const std::string &commaSeparatedVideoCodecs)
    {
        const std::vector<std::string> exts = SplitCommaSeparated(commaSeparatedExts);
        const std::vector<std::string> videoCodecs = SplitCommaSeparated(commaSeparatedVideoCodecs);
        std::set<std::string> seen;

        for (size_t index = 0; index < exts.size(); ++index)
        {
            const std::string videoCodec = index < videoCodecs.size() ? videoCodecs[index] : "";
            std::string ext = ToUpper(exts[index]);
            if (ext.empty() || ext == "MHTML")
            {
                continue;
            }
            if (videoCodec != "none" && videoCodec != "None" && !videoCodec.empty())
            {
                seen.insert(ext);
            }
        }

        std::vector<std::string> result;
        const std::vector<std::string> order = {"MP4", "MKV", "WEBM"};
        for (const std::string &preferred : order)
        {
            if (seen.erase(preferred) > 0)
            {
                result.push_back(preferred);
            }
        }
        for (const std::string &ext : seen)
        {
            result.push_back(ext);
        }

        if (result.empty())
        {
            result.push_back("MP4");
        }

        return result;
    }

    std::vector<std::string> ParseAvailableQualities(const std::string &commaSeparatedHeights)
    {
        std::set<int, std::greater<int>> heights;
        std::stringstream stream(commaSeparatedHeights);
        std::string item;

        while (std::getline(stream, item, ','))
        {
            item = Trim(item);
            if (item.empty() || item == "none" || item == "None" || item == "NA")
            {
                continue;
            }

            try
            {
                const int height = std::stoi(item);
                if (height >= 360)
                {
                    heights.insert(height);
                }
            }
            catch (...)
            {
            }
        }

        std::vector<std::string> qualities;
        qualities.reserve(heights.size());
        for (const int height : heights)
        {
            qualities.push_back(std::to_string(height) + "p");
        }

        return qualities;
    }

#ifdef _WIN32
    CommandResult RunCommand(std::string command, const std::shared_ptr<std::atomic_bool> &cancelRequested)
    {
        CommandResult result;
        SECURITY_ATTRIBUTES securityAttributes{};
        securityAttributes.nLength = sizeof(securityAttributes);
        securityAttributes.bInheritHandle = TRUE;

        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0))
        {
            result.exitCode = 1;
            result.output = "Could not create parser pipe.";
            return result;
        }
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA startupInfo{};
        PROCESS_INFORMATION processInfo{};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdOutput = writePipe;
        startupInfo.hStdError = writePipe;
        startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

        std::string mutableCommand = std::move(command);
        const BOOL started = CreateProcessA(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo);
        CloseHandle(writePipe);

        if (!started)
        {
            CloseHandle(readPipe);
            result.exitCode = 1;
            result.output = "Could not start yt-dlp.";
            return result;
        }

        const auto consumeOutput = [&]()
        {
            DWORD available = 0;
            while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0)
            {
                std::array<char, 512> buffer{};
                DWORD read = 0;
                if (!ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size() - 1), &read, nullptr) || read == 0)
                {
                    break;
                }
                result.output.append(buffer.data(), read);
            }
        };

        while (WaitForSingleObject(processInfo.hProcess, 100) == WAIT_TIMEOUT)
        {
            consumeOutput();
            if (cancelRequested != nullptr && cancelRequested->load())
            {
                KillProcessTree(processInfo.dwProcessId);
                WaitForSingleObject(processInfo.hProcess, 3000);
                result.cancelled = true;
                result.exitCode = -2;
                CloseHandle(processInfo.hThread);
                CloseHandle(processInfo.hProcess);
                CloseHandle(readPipe);
                return result;
            }
        }
        consumeOutput();

        DWORD exitCode = 0;
        GetExitCodeProcess(processInfo.hProcess, &exitCode);
        result.exitCode = static_cast<int>(exitCode);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        CloseHandle(readPipe);
        return result;
    }
#else
    CommandResult RunCommand(const std::string &command, const std::shared_ptr<std::atomic_bool> &)
    {
        CommandResult result;
        FILE *pipe = POPEN(command.c_str(), "r");
        if (pipe == nullptr)
        {
            result.exitCode = 1;
            result.output = "Could not start yt-dlp.";
            return result;
        }

        std::array<char, 512> buffer{};
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        {
            result.output += buffer.data();
        }
        result.exitCode = PCLOSE(pipe);
        return result;
    }
#endif
}

namespace
{
    template<typename T>
    void FinishFuture(std::future<T>& future)
    {
        if (!future.valid())
        {
            return;
        }

        if (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            future.wait();
        }

        try
        {
            future.get();
        }
        catch (...)
        {
        }
    }
}

void LinkInfoLoader::Start(std::string url)
{
    if (future_.valid())
    {
        Cancel();
        FinishFuture(future_);
    }

    result_ = {};
    result_.url = url;
    hasResult_ = false;
    isLoading_ = true;
    cancelRequested_ = std::make_shared<std::atomic_bool>(false);

    future_ = std::async(std::launch::async, [url = std::move(url), cancelRequested = cancelRequested_]
                         { return Load(url, cancelRequested); });
}

void LinkInfoLoader::Cancel()
{
    if (cancelRequested_ != nullptr)
    {
        cancelRequested_->store(true);
    }
}

void LinkInfoLoader::Update()
{
    if (!isLoading_ || !future_.valid())
    {
        return;
    }

    if (future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            result_ = future_.get();
        }
        catch (const std::exception &exception)
        {
            result_ = {};
            result_.error = exception.what();
        }
        catch (...)
        {
            result_ = {};
            result_.error = "Unexpected parser error.";
        }
        hasResult_ = true;
        isLoading_ = false;
        cancelRequested_.reset();
    }
}

bool LinkInfoLoader::IsLoading() const
{
    return isLoading_;
}

bool LinkInfoLoader::HasResult() const
{
    return hasResult_;
}

const LinkInfo &LinkInfoLoader::GetResult() const
{
    return result_;
}

LinkInfo LinkInfoLoader::Load(std::string url, std::shared_ptr<std::atomic_bool> cancelRequested)
{
    LinkInfo info;
    info.url = url;

    if (url.empty())
    {
        info.error = "Clipboard is empty.";
        return info;
    }

    const std::string ytDlpInvocation = BuildYtDlpCommandPrefix();
    if (ytDlpInvocation.empty())
    {
        info.error = "Could not find packages\\ytdown\\python\\python.exe or yt-dlp.";
        return info;
    }

    const std::filesystem::path ffmpegDirectory = FindFfmpegDirectory();
    const std::filesystem::path cacheDirectory = GetLinkInfoCacheDirectory();
    std::error_code cacheError;
    std::filesystem::create_directories(cacheDirectory, cacheError);
    if (cacheError)
    {
        info.error = "Could not create parser cache directory.";
        return info;
    }

    const std::string normalizedUrl = NormalizeYoutubeUrl(url);
    const std::string ffmpegArgs = ffmpegDirectory.empty() ? "" : " --ffmpeg-location " + Quote(ffmpegDirectory.string());
    const std::string jsArgs = BuildYoutubeJsRuntimeArgs();
    const std::string printArgs =
        " --no-playlist --skip-download --no-warnings --write-thumbnail --convert-thumbnails jpg -P " +
        Quote(cacheDirectory.string()) +
        " -o \"%(id)s.%(ext)s\" --print \"YTINFO_TITLE:%(title)s\" --print \"YTINFO_UPLOADER:%(uploader)s\" --print \"YTINFO_DURATION:%(duration_string)s\" --print \"YTINFO_EXT:%(ext)s\" --print \"YTINFO_VCODEC:%(vcodec)s\" --print \"YTINFO_ACODEC:%(acodec)s\" --print \"YTINFO_ID:%(id)s\" --print \"YTINFO_THUMB:%(thumbnail)s\" --print \"YTINFO_FORMAT_IDS:%(formats.:.format_id)l\" --print \"YTINFO_EXTS:%(formats.:.ext)l\" --print \"YTINFO_HEIGHTS:%(formats.:.height)l\" --print \"YTINFO_VCODECS:%(formats.:.vcodec)l\" --print \"YTINFO_ACODECS:%(formats.:.acodec)l\" " +
        Quote(normalizedUrl) + " 2>&1";

    std::string output;
    int exitCode = 1;
    bool cancelled = false;
    std::vector<std::string> browsersToTry = BuildYoutubeCookieBrowsersToTryList();

    BrowserAttemptLog parseLog;
    std::string successfulBrowser;
    for (size_t browserIndex = 0; browserIndex < browsersToTry.size(); ++browserIndex)
    {
        const std::string& browser = browsersToTry[browserIndex];
        const bool hasMoreBrowsers = browserIndex + 1 < browsersToTry.size();
        const std::string cookieArgs = BuildYoutubeCookiesArgs(browser);
        const std::string ytDlpCommand = ytDlpInvocation + ffmpegArgs + jsArgs + cookieArgs + printArgs;
        const std::string command = "cmd /S /C \"chcp 65001>nul && set PYTHONIOENCODING=utf-8 && " + ytDlpCommand + "\"";
        const CommandResult commandResult = RunCommand(command, cancelRequested);
        output = commandResult.output;
        exitCode = commandResult.exitCode;
        cancelled = commandResult.cancelled;
        if (cancelled)
        {
            break;
        }

        const std::vector<std::string> lines = SplitLines(output);
        const std::string title = ValueAfterPrefix(lines, "YTINFO_TITLE:");
        const bool success = exitCode == 0 && !title.empty();

        BrowserAttempt attempt;
        attempt.browserSpec = browser;
        attempt.success = success;
        attempt.summary = success ? "Parsed link metadata." : SummarizeBrowserAttemptOutput(output, true);
        attempt.nextAction = DescribeBrowserRetryAction(output, hasMoreBrowsers, success);
        parseLog.AddAttempt(attempt);

        if (success)
        {
            successfulBrowser = browser;
            break;
        }

        if (!browser.empty() && !ShouldRetryYoutubeWithDifferentCookies(output))
        {
            break;
        }
    }

    info.parseBrowserReport = parseLog.FormatSection("Parse");

    if (cancelled)
    {
        info.cancelled = true;
        info.error = "Parsing cancelled.";
        return info;
    }

    const std::vector<std::string> lines = SplitLines(output);

    const std::string title = ValueAfterPrefix(lines, "YTINFO_TITLE:");
    const std::string uploader = ValueAfterPrefix(lines, "YTINFO_UPLOADER:");
    const std::string duration = ValueAfterPrefix(lines, "YTINFO_DURATION:");
    const std::string container = ValueAfterPrefix(lines, "YTINFO_EXT:");
    const std::string videoCodec = ValueAfterPrefix(lines, "YTINFO_VCODEC:");
    const std::string audioCodec = ValueAfterPrefix(lines, "YTINFO_ACODEC:");
    const std::string videoId = ValueAfterPrefix(lines, "YTINFO_ID:");
    const std::string thumbnailUrl = ValueAfterPrefix(lines, "YTINFO_THUMB:");
    const std::string exts = ValueAfterPrefix(lines, "YTINFO_EXTS:");
    const std::string heights = ValueAfterPrefix(lines, "YTINFO_HEIGHTS:");
    const std::string videoCodecs = ValueAfterPrefix(lines, "YTINFO_VCODECS:");
    const std::string audioCodecs = ValueAfterPrefix(lines, "YTINFO_ACODECS:");
    const std::string formatIds = ValueAfterPrefix(lines, "YTINFO_FORMAT_IDS:");

    if (exitCode != 0 || title.empty())
    {
        info.error = SimplifyYtDlpError(output);
        info.errorLog = output;
        return info;
    }

    info.success = true;
    parseLog.SetWinner(successfulBrowser);
    info.parseBrowserReport = parseLog.FormatSection("Parse");
    SetPreferredYoutubeCookieBrowser(successfulBrowser);
    info.title = title;
    info.normalizedTitle = NormalizeVideoTitle(title);
    info.uploader = uploader;
    info.duration = NormalizeDurationString(duration);
    info.container = ToUpper(container.empty() ? "Unknown" : container);
    info.videoCodec = NormalizeCodecName(videoCodec);
    info.audioCodec = NormalizeCodecName(audioCodec);
    info.thumbnailPath = videoId.empty()
        ? ""
        : ResolveThumbnailPath(cacheDirectory, videoId, thumbnailUrl).string();
    info.availableFormats = ParseAvailableFormats(exts);
    info.availableVideoFormats = ParseAvailableVideoFormats(exts, videoCodecs);
    info.availableAudioFormats = ParseAvailableAudioFormats(exts, videoCodecs, audioCodecs);
    if (info.availableVideoFormats.empty())
    {
        info.availableVideoFormats.push_back("MP4");
    }
    if (info.availableAudioFormats.empty())
    {
        info.availableAudioFormats.push_back("M4A");
    }
    info.availableQualities = ParseAvailableQualities(heights);
    info.formatStreams = ParseLinkFormatStreams(formatIds, exts, heights, videoCodecs, audioCodecs);
    return info;
}

std::string LinkInfoLoader::Quote(const std::string &value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char c : value)
    {
        if (c == '"')
        {
            escaped += "\\\"";
        }
        else
        {
            escaped.push_back(c);
        }
    }
    return "\"" + escaped + "\"";
}
