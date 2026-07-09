#include "LinkInfoLoader.h"

#include "BrowserDiagnostics.h"
#include "DownloadFormatPredictor.h"
#include "LinkGroupInfoLoader.h"
#include "ToolPaths.h"
#include "VideoTitle.h"
#include "WinProcess.h"
#include "YtDlpLocator.h"
#include "YtDlpYouTube.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
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
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
struct CommandResult
{
    int exitCode = 0;
    std::string output;
    bool cancelled = false;
};

std::vector<std::string> SplitLines(const std::string& text)
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

std::filesystem::path FindThumbnailPath(const std::filesystem::path& cacheDirectory, const std::string& videoId)
{
    const std::array<const char*, 4> extensions = {".jpg", ".jpeg", ".png", ".webp"};
    for (const char* extension : extensions)
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
    WinHttpAddRequestHeaders(request,
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
bool DownloadHttpToFile(const std::string& url, const std::filesystem::path& destination)
{
    if (url.empty() || destination.empty())
    {
        return false;
    }
    const std::string command = "curl -fsSL --max-time 30 -o " + QuoteShellArgument(destination.string()) + " " +
                                QuoteShellArgument(url) + " 2>/dev/null";
    if (std::system(command.c_str()) != 0)
    {
        std::error_code error;
        std::filesystem::remove(destination, error);
        return false;
    }
    std::error_code existsError;
    return std::filesystem::exists(destination, existsError) && IsLoadableImagePath(destination);
}
#endif

std::filesystem::path ResolveThumbnailPath(const std::filesystem::path& cacheDirectory,
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
    const auto tryDownload = [&](const std::string& url)
    {
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
    char* localAppData = nullptr;
    size_t localAppDataSize = 0;
    if (_dupenv_s(&localAppData, &localAppDataSize, "LOCALAPPDATA") == 0 && localAppData != nullptr &&
        localAppData[0] != '\0')
    {
        const std::filesystem::path path = std::filesystem::path(localAppData) / "4KDowner" / "cache" / "link-info";
        std::free(localAppData);
        return path;
    }
    std::free(localAppData);
#else
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0')
    {
        return std::filesystem::path(home) / ".cache" / "4KDowner" / "link-info";
    }
#endif

    std::error_code error;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(error);
    return (error ? std::filesystem::path("cache") : temp / "4KDowner" / "cache") / "link-info";
}

std::string ValueAfterPrefix(const std::vector<std::string>& lines, const std::string& prefix)
{
    for (const std::string& line : lines)
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
    for (char& c : value)
    {
        if (c >= 'a' && c <= 'z')
        {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return value;
}

std::string NormalizeDurationString(const std::string& value)
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
        if (totalSeconds >= 3600)
        {
            const int hours = totalSeconds / 3600;
            const int remainder = totalSeconds % 3600;
            const int minutes = remainder / 60;
            const int seconds = remainder % 60;
            char buffer[32]{};
            // UI expects a single time token; for durations >= 1h display as H:MM:SS.
            std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes, seconds);
            return buffer;
        }

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
    if (value.rfind("hev1", 0) == 0 || value.rfind("hvc1", 0) == 0 || value.rfind("hevc", 0) == 0 ||
        value.rfind("h265", 0) == 0)
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

std::vector<std::string> ParseAvailableFormats(const std::string& commaSeparatedExts)
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
    for (const std::string& ext : order)
    {
        if (seen.erase(ext) > 0)
        {
            preferred.push_back(ext);
        }
    }
    for (const std::string& ext : seen)
    {
        preferred.push_back(ext);
    }

    if (preferred.empty())
    {
        preferred.push_back("MP4");
    }

    return preferred;
}

std::vector<std::string> SplitCommaSeparated(const std::string& value)
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

std::vector<std::string> ParseAvailableAudioFormats(const std::string& commaSeparatedExts,
                                                    const std::string& commaSeparatedVideoCodecs,
                                                    const std::string& commaSeparatedAudioCodecs)
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
        if ((videoCodec == "none" || videoCodec == "None") && audioCodec != "none" && audioCodec != "None" &&
            !audioCodec.empty())
        {
            seen.insert(ext);
        }
    }

    std::vector<std::string> result;
    const std::vector<std::string> order = {"M4A", "MP3", "WEBM", "OPUS", "AAC", "WAV", "FLAC"};
    for (const std::string& preferred : order)
    {
        if (seen.erase(preferred) > 0)
        {
            result.push_back(preferred);
        }
    }
    for (const std::string& ext : seen)
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

std::vector<std::string> ParseAvailableVideoFormats(const std::string& commaSeparatedExts,
                                                    const std::string& commaSeparatedVideoCodecs)
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
    for (const std::string& preferred : order)
    {
        if (seen.erase(preferred) > 0)
        {
            result.push_back(preferred);
        }
    }
    for (const std::string& ext : seen)
    {
        result.push_back(ext);
    }

    if (result.empty())
    {
        result.push_back("MP4");
    }

    return result;
}

std::vector<std::string> ParseAvailableQualities(const std::string& commaSeparatedHeights)
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
            const int bucket = BucketDownloadHeight(std::stoi(item));
            if (bucket > 0)
            {
                heights.insert(bucket);
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
CommandResult RunCommand(std::string command, const std::shared_ptr<std::atomic_bool>& cancelRequested)
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
    const BOOL started = CreateProcessA(nullptr,
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
CommandResult RunCommand(const std::string& command, const std::shared_ptr<std::atomic_bool>& cancelRequested)
{
    CommandResult result;
    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0)
    {
        result.exitCode = 1;
        result.output = "Could not start yt-dlp.";
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        result.exitCode = 1;
        result.output = "Could not start yt-dlp.";
        return result;
    }

    if (pid == 0)
    {
        // Own process group so cancel can kill the whole shell/yt-dlp tree.
        setpgid(0, 0);
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        if (pipefd[1] != STDOUT_FILENO && pipefd[1] != STDERR_FILENO)
        {
            close(pipefd[1]);
        }
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    setpgid(pid, pid);
    close(pipefd[1]);

    const int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0)
    {
        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    std::array<char, 512> buffer{};
    bool childExited = false;
    int status = 0;
    while (!childExited)
    {
        if (cancelRequested != nullptr && cancelRequested->load())
        {
            kill(-pid, SIGTERM);
            usleep(100000);
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
            close(pipefd[0]);
            result.cancelled = true;
            result.exitCode = -2;
            return result;
        }

        pollfd pfd{};
        pfd.fd = pipefd[0];
        pfd.events = POLLIN;
        const int pollResult = poll(&pfd, 1, 100);
        if (pollResult > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0)
        {
            for (;;)
            {
                const ssize_t bytesRead = read(pipefd[0], buffer.data(), buffer.size());
                if (bytesRead > 0)
                {
                    result.output.append(buffer.data(), static_cast<size_t>(bytesRead));
                    continue;
                }
                if (bytesRead == 0)
                {
                    break;
                }
                if (errno == EINTR)
                {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                break;
            }
        }

        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid)
        {
            childExited = true;
        }
        else if (waited < 0 && errno != EINTR)
        {
            childExited = true;
            status = 1;
        }
    }

    for (;;)
    {
        const ssize_t bytesRead = read(pipefd[0], buffer.data(), buffer.size());
        if (bytesRead > 0)
        {
            result.output.append(buffer.data(), static_cast<size_t>(bytesRead));
            continue;
        }
        break;
    }
    close(pipefd[0]);

    if (WIFEXITED(status))
    {
        result.exitCode = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        result.exitCode = 128 + WTERMSIG(status);
    }
    else
    {
        result.exitCode = 1;
    }
    return result;
}
#endif
} // namespace

namespace
{
std::mutex g_abandonedLinkFuturesMutex;
std::vector<std::future<LinkInfo>> g_abandonedLinkFutures;

void AbandonLinkFuture(std::future<LinkInfo>& future)
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

    std::lock_guard<std::mutex> lock(g_abandonedLinkFuturesMutex);
    g_abandonedLinkFutures.push_back(std::move(future));
}
} // namespace

LinkInfoLoader::~LinkInfoLoader()
{
    AbandonRunningWork();
}

LinkInfoLoader::LinkInfoLoader(LinkInfoLoader&& other) noexcept
    : future_(std::move(other.future_)),
      cancelRequested_(std::move(other.cancelRequested_)),
      result_(std::move(other.result_)),
      isLoading_(other.isLoading_),
      hasResult_(other.hasResult_)
{
    other.isLoading_ = false;
    other.hasResult_ = false;
}

LinkInfoLoader& LinkInfoLoader::operator=(LinkInfoLoader&& other) noexcept
{
    if (this != &other)
    {
        AbandonRunningWork();
        future_ = std::move(other.future_);
        cancelRequested_ = std::move(other.cancelRequested_);
        result_ = std::move(other.result_);
        isLoading_ = other.isLoading_;
        hasResult_ = other.hasResult_;
        other.isLoading_ = false;
        other.hasResult_ = false;
    }
    return *this;
}

void LinkInfoLoader::AbandonRunningWork()
{
    if (cancelRequested_ != nullptr)
    {
        cancelRequested_->store(true);
    }
    AbandonLinkFuture(future_);
    cancelRequested_.reset();
    isLoading_ = false;
}

void LinkInfoLoader::ReapAbandoned()
{
    std::lock_guard<std::mutex> lock(g_abandonedLinkFuturesMutex);
    auto it = g_abandonedLinkFutures.begin();
    while (it != g_abandonedLinkFutures.end())
    {
        if (!it->valid() || it->wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            try
            {
                if (it->valid())
                {
                    it->get();
                }
            }
            catch (...)
            {
            }
            it = g_abandonedLinkFutures.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void LinkInfoLoader::Start(std::string url)
{
    AbandonRunningWork();

    result_ = {};
    result_.url = url;
    hasResult_ = false;
    isLoading_ = true;
    cancelRequested_ = std::make_shared<std::atomic_bool>(false);

    future_ = std::async(std::launch::async,
                         [url = std::move(url), cancelRequested = cancelRequested_]
                         {
                             return Load(url, cancelRequested);
                         });
}

void LinkInfoLoader::Cancel()
{
    AbandonRunningWork();
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
        catch (const std::exception& exception)
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

const LinkInfo& LinkInfoLoader::GetResult() const
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

    // Native binary path — not bin/ — so yt-dlp does not pick a sibling OS binary.
    const std::filesystem::path ffmpegPath = ::FindFfmpegExecutable();
    const std::filesystem::path cacheDirectory = GetLinkInfoCacheDirectory();
    std::error_code cacheError;
    std::filesystem::create_directories(cacheDirectory, cacheError);
    if (cacheError)
    {
        info.error = "Could not create parser cache directory.";
        return info;
    }

    const std::string normalizedUrl = NormalizeYoutubeUrl(url);
    const std::string ffmpegArgs = ffmpegPath.empty() ? "" : " --ffmpeg-location " + Quote(ffmpegPath.string());
    const std::string jsArgs = BuildYoutubeJsRuntimeArgs();
    const std::string printArgs =
        " --no-playlist --skip-download --no-warnings --write-thumbnail --convert-thumbnails jpg -P " +
        Quote(cacheDirectory.string()) +
        " -o \"%(id)s.%(ext)s\" --print \"YTINFO_TITLE:%(title)s\" --print \"YTINFO_UPLOADER:%(uploader)s\" --print "
        "\"YTINFO_DURATION:%(duration_string)s\" --print \"YTINFO_EXT:%(ext)s\" --print \"YTINFO_VCODEC:%(vcodec)s\" "
        "--print \"YTINFO_ACODEC:%(acodec)s\" --print \"YTINFO_ID:%(id)s\" --print \"YTINFO_THUMB:%(thumbnail)s\" "
        "--print \"YTINFO_FORMATS:%(formats.:.{format_id,ext,height,width,resolution,format_note,vcodec,acodec})j\" " +
        Quote(normalizedUrl) + " 2>&1";

    std::string output;
    int exitCode = 1;
    bool cancelled = false;
    std::vector<std::string> browsersToTry = BuildYoutubeCookieBrowsersToTryList();

    BrowserAttemptLog parseLog;
    std::string successfulBrowser;
    for (size_t browserIndex = 0; browserIndex < browsersToTry.size(); ++browserIndex)
    {
        if (cancelRequested != nullptr && cancelRequested->load())
        {
            cancelled = true;
            break;
        }

        const std::string& browser = browsersToTry[browserIndex];
        const bool hasMoreBrowsers = browserIndex + 1 < browsersToTry.size();
        const std::string cookieArgs = BuildYoutubeCookiesArgs(browser);
        const std::string ytDlpCommand = ytDlpInvocation + ffmpegArgs + jsArgs + cookieArgs + printArgs;
#ifdef _WIN32
        const std::string command =
            "cmd /S /C \"chcp 65001>nul && set PYTHONIOENCODING=utf-8 && " + ytDlpCommand + "\"";
#else
        const std::string command = "env PYTHONIOENCODING=utf-8 PYTHONUNBUFFERED=1 " + ytDlpCommand;
#endif
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
    const std::string formatsJson = ValueAfterPrefix(lines, "YTINFO_FORMATS:");

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
    info.thumbnailPath = videoId.empty() ? "" : ResolveThumbnailPath(cacheDirectory, videoId, thumbnailUrl).string();
    info.formatStreams = ParseLinkFormatStreamsJson(formatsJson);
    info.availableFormats = FormatsFromStreams(info.formatStreams);
    info.availableVideoFormats = VideoFormatsFromStreams(info.formatStreams);
    info.availableAudioFormats = AudioFormatsFromStreams(info.formatStreams);
    info.availableQualities = QualitiesFromStreams(info.formatStreams);
    if (info.availableVideoFormats.empty())
    {
        info.availableVideoFormats.push_back("MP4");
    }
    if (info.availableAudioFormats.empty())
    {
        info.availableAudioFormats.push_back("M4A");
    }
    return info;
}

LinkInfo LinkInfoLoader::LoadVideo(std::string url, std::shared_ptr<std::atomic_bool> cancelRequested)
{
    return Load(std::move(url), cancelRequested);
}

std::string LinkInfoLoader::Quote(const std::string& value)
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

std::string ToLowerAsciiCopy(std::string value)
{
    for (char& c : value)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

bool LooksLikeChannelUrl(const std::string& url)
{
    const std::string lower = ToLowerAsciiCopy(url);
    return lower.find("/@") != std::string::npos || lower.find("/channel/") != std::string::npos ||
           lower.find("/c/") != std::string::npos || lower.find("/user/") != std::string::npos;
}

bool LooksLikePlaylistUrl(const std::string& url)
{
    const std::string lower = ToLowerAsciiCopy(url);
    return lower.find("list=") != std::string::npos || lower.find("/playlist") != std::string::npos;
}

bool LooksLikeGroupUrl(const std::string& url)
{
    return LooksLikePlaylistUrl(url) || LooksLikeChannelUrl(url);
}

void AppendUtf8Codepoint(std::string& out, unsigned int codepoint)
{
    if (codepoint <= 0x7Fu)
    {
        out.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint <= 0x7FFu)
    {
        out.push_back(static_cast<char>(0xC0u | ((codepoint >> 6) & 0x1Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
    else if (codepoint <= 0xFFFFu)
    {
        out.push_back(static_cast<char>(0xE0u | ((codepoint >> 12) & 0x0Fu)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
    else if (codepoint <= 0x10FFFFu)
    {
        out.push_back(static_cast<char>(0xF0u | ((codepoint >> 18) & 0x07u)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
}

bool ParseHexNibble(char ch, unsigned int& nibble)
{
    if (ch >= '0' && ch <= '9')
    {
        nibble = static_cast<unsigned int>(ch - '0');
        return true;
    }
    if (ch >= 'a' && ch <= 'f')
    {
        nibble = static_cast<unsigned int>(10 + (ch - 'a'));
        return true;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        nibble = static_cast<unsigned int>(10 + (ch - 'A'));
        return true;
    }
    return false;
}

bool ParseJsonUnicodeEscape(const std::string& json, size_t& cursor, unsigned int& codepoint)
{
    if (cursor + 4 >= json.size())
    {
        return false;
    }
    unsigned int value = 0;
    for (int i = 0; i < 4; ++i)
    {
        unsigned int nibble = 0;
        if (!ParseHexNibble(json[cursor + 1 + i], nibble))
        {
            return false;
        }
        value = (value << 4) | nibble;
    }
    cursor += 4;
    codepoint = value;
    return true;
}

std::string ExtractJsonStringValue(const std::string& json, const std::string& key, size_t searchFrom = 0)
{
    const std::string needle = "\"" + key + "\":";
    const size_t pos = json.find(needle, searchFrom);
    if (pos == std::string::npos)
    {
        return {};
    }

    size_t cursor = pos + needle.size();
    while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t'))
    {
        ++cursor;
    }
    if (cursor >= json.size() || json[cursor] != '"')
    {
        return {};
    }

    ++cursor;
    std::string result;
    for (; cursor < json.size(); ++cursor)
    {
        const char ch = json[cursor];
        if (ch == '"')
        {
            break;
        }
        if (ch != '\\')
        {
            result.push_back(ch);
            continue;
        }
        if (cursor + 1 >= json.size())
        {
            break;
        }
        const char escaped = json[++cursor];
        switch (escaped)
        {
        case '"':
        case '\\':
        case '/':
            result.push_back(escaped);
            break;
        case 'b':
            result.push_back('\b');
            break;
        case 'f':
            result.push_back('\f');
            break;
        case 'n':
            result.push_back('\n');
            break;
        case 'r':
            result.push_back('\r');
            break;
        case 't':
            result.push_back('\t');
            break;
        case 'u':
        {
            unsigned int codepoint = 0;
            if (!ParseJsonUnicodeEscape(json, cursor, codepoint))
            {
                result.push_back('u');
                break;
            }
            // Handle UTF-16 surrogate pairs.
            if (codepoint >= 0xD800u && codepoint <= 0xDBFFu && cursor + 6 < json.size() && json[cursor + 1] == '\\' &&
                json[cursor + 2] == 'u')
            {
                size_t lowCursor = cursor + 2;
                unsigned int low = 0;
                if (ParseJsonUnicodeEscape(json, lowCursor, low) && low >= 0xDC00u && low <= 0xDFFFu)
                {
                    codepoint = 0x10000u + ((codepoint - 0xD800u) << 10) + (low - 0xDC00u);
                    cursor = lowCursor;
                }
            }
            AppendUtf8Codepoint(result, codepoint);
            break;
        }
        default:
            result.push_back(escaped);
            break;
        }
    }
    return result;
}

int ExtractJsonIntValue(const std::string& json, const std::string& key)
{
    const std::string needle = "\"" + key + "\":";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos)
    {
        return 0;
    }

    size_t cursor = pos + needle.size();
    while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t'))
    {
        ++cursor;
    }

    int sign = 1;
    if (cursor < json.size() && json[cursor] == '-')
    {
        sign = -1;
        ++cursor;
    }

    int value = 0;
    bool foundDigit = false;
    for (; cursor < json.size(); ++cursor)
    {
        const char ch = json[cursor];
        if (ch >= '0' && ch <= '9')
        {
            value = value * 10 + (ch - '0');
            foundDigit = true;
            continue;
        }
        break;
    }
    return foundDigit ? value * sign : 0;
}

std::vector<std::string> ExtractJsonObjectsFromArray(const std::string& json, const std::string& arrayKey)
{
    std::vector<std::string> objects;
    const std::string needle = "\"" + arrayKey + "\":";
    const size_t arrayPos = json.find(needle);
    if (arrayPos == std::string::npos)
    {
        return objects;
    }

    size_t cursor = json.find('[', arrayPos);
    if (cursor == std::string::npos)
    {
        return objects;
    }
    ++cursor;

    int depth = 0;
    size_t objectStart = std::string::npos;
    for (; cursor < json.size(); ++cursor)
    {
        const char ch = json[cursor];
        if (ch == '{')
        {
            if (depth == 0)
            {
                objectStart = cursor;
            }
            ++depth;
            continue;
        }
        if (ch == '}')
        {
            --depth;
            if (depth == 0 && objectStart != std::string::npos)
            {
                objects.push_back(json.substr(objectStart, cursor - objectStart + 1));
                objectStart = std::string::npos;
            }
            continue;
        }
        if (ch == ']' && depth == 0)
        {
            break;
        }
    }
    return objects;
}

LinkGroupEntry ParseFlatEntryObject(const std::string& objectJson, const std::filesystem::path& cacheDirectory)
{
    LinkGroupEntry entry;
    entry.id = ExtractJsonStringValue(objectJson, "id");
    entry.title = ExtractJsonStringValue(objectJson, "title");
    // yt-dlp flat-playlist JSON contains both:
    // - webpage_url: stable watch URL for the entry video
    // - url: sometimes points at a thumbnail/media URL depending on extractor
    // We always prefer webpage_url for downloading.
    entry.url = ExtractJsonStringValue(objectJson, "webpage_url");
    if (entry.url.empty())
    {
        entry.url = ExtractJsonStringValue(objectJson, "url");
    }

    // Sometimes yt-dlp flat-playlist returns thumbnail URLs (e.g. .../vi/<videoId>/hqdefault.jpg)
    // instead of a stable watch URL. If we see a thumbnail URL, extract <videoId> and rebuild watch URL.
    if (!entry.url.empty() && entry.url.find("i.ytimg.com/vi/") != std::string::npos)
    {
        const size_t viPos = entry.url.find("/vi/");
        if (viPos != std::string::npos)
        {
            const size_t idStart = viPos + 4;
            const size_t idEnd = entry.url.find('/', idStart);
            if (idEnd != std::string::npos && idEnd > idStart)
            {
                const std::string videoId = entry.url.substr(idStart, idEnd - idStart);
                if (!videoId.empty())
                {
                    entry.id = entry.id.empty() || entry.id == "hqdefault" ? videoId : entry.id;
                    entry.url = "https://www.youtube.com/watch?v=" + videoId;
                }
            }
        }
    }

    if (entry.url.empty() && !entry.id.empty())
    {
        entry.url = "https://www.youtube.com/watch?v=" + entry.id;
    }
    entry.duration = NormalizeDurationString(ExtractJsonStringValue(objectJson, "duration_string"));
    if (entry.duration == "--:--")
    {
        const int seconds = ExtractJsonIntValue(objectJson, "duration");
        if (seconds > 0)
        {
            entry.duration = NormalizeDurationString(std::to_string(seconds));
        }
    }
    if (!entry.id.empty())
    {
        entry.thumbnailPath = ResolveThumbnailPath(cacheDirectory, entry.id, "").string();
    }
    entry.metadataLoaded = !entry.url.empty();
    return entry;
}

LinkGroupInfo LinkGroupInfoLoader::Load(std::string url, std::shared_ptr<std::atomic_bool> cancelRequested)
{
    LinkGroupInfo info;
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

    const std::filesystem::path cacheDirectory = GetLinkInfoCacheDirectory();
    std::error_code cacheError;
    std::filesystem::create_directories(cacheDirectory, cacheError);
    if (cacheError)
    {
        info.error = "Could not create parser cache directory.";
        return info;
    }

    const std::string normalizedUrl = NormalizeYoutubeUrl(url);
    const std::string ffmpegPath = ::FindFfmpegExecutable().string();
    const std::string ffmpegArgs = ffmpegPath.empty() ? "" : " --ffmpeg-location " + LinkInfoLoader::Quote(ffmpegPath);
    const std::string jsArgs = BuildYoutubeJsRuntimeArgs();
    const std::string flatArgs = " --flat-playlist --dump-single-json --skip-download --no-warnings " +
                                 LinkInfoLoader::Quote(normalizedUrl) + " 2>&1";

    std::string output;
    int exitCode = 1;
    bool cancelled = false;
    std::vector<std::string> browsersToTry = BuildYoutubeCookieBrowsersToTryList();
    BrowserAttemptLog parseLog;
    std::string successfulBrowser;
    for (size_t browserIndex = 0; browserIndex < browsersToTry.size(); ++browserIndex)
    {
        if (cancelRequested != nullptr && cancelRequested->load())
        {
            cancelled = true;
            break;
        }

        const std::string& browser = browsersToTry[browserIndex];
        const bool hasMoreBrowsers = browserIndex + 1 < browsersToTry.size();
        const std::string cookieArgs = BuildYoutubeCookiesArgs(browser);
        const std::string ytDlpCommand = ytDlpInvocation + ffmpegArgs + jsArgs + cookieArgs + flatArgs;
#ifdef _WIN32
        const std::string command =
            "cmd /S /C \"chcp 65001>nul && set PYTHONIOENCODING=utf-8 && " + ytDlpCommand + "\"";
#else
        const std::string command = "env PYTHONIOENCODING=utf-8 PYTHONUNBUFFERED=1 " + ytDlpCommand;
#endif
        const CommandResult commandResult = RunCommand(command, cancelRequested);
        output = commandResult.output;
        exitCode = commandResult.exitCode;
        cancelled = commandResult.cancelled;
        if (cancelled)
        {
            break;
        }

        const std::string title = ExtractJsonStringValue(output, "title");
        const bool success = exitCode == 0 && !title.empty();
        BrowserAttempt attempt;
        attempt.browserSpec = browser;
        attempt.success = success;
        attempt.summary = success ? "Parsed playlist metadata." : SummarizeBrowserAttemptOutput(output, true);
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

    const std::string contentType = ExtractJsonStringValue(output, "_type");
    const std::vector<std::string> entryObjects = ExtractJsonObjectsFromArray(output, "entries");
    const bool isExplicitGroupType =
        contentType == "playlist" || contentType == "channel" || contentType == "multi_video";
    const bool treatAsGroup =
        entryObjects.size() > 1 || ((isExplicitGroupType || LooksLikeGroupUrl(url)) && !entryObjects.empty());

    if (!treatAsGroup || entryObjects.empty())
    {
        info.isGroup = false;
        info.singleVideo = LinkInfoLoader::LoadVideo(url, cancelRequested);
        if (!info.singleVideo.parseBrowserReport.empty())
        {
            info.parseBrowserReport = info.singleVideo.parseBrowserReport;
        }
        return info;
    }

    if (exitCode != 0)
    {
        info.error = SimplifyYtDlpError(output);
        info.errorLog = output;
        return info;
    }

    info.success = true;
    info.isGroup = true;
    parseLog.SetWinner(successfulBrowser);
    info.parseBrowserReport = parseLog.FormatSection("Parse");
    SetPreferredYoutubeCookieBrowser(successfulBrowser);
    info.title = ExtractJsonStringValue(output, "title");
    info.normalizedTitle = NormalizeVideoTitle(info.title);
    info.uploader = ExtractJsonStringValue(output, "uploader");
    if (info.uploader.empty())
    {
        info.uploader = ExtractJsonStringValue(output, "channel");
    }
    info.duration = NormalizeDurationString(ExtractJsonStringValue(output, "duration_string"));
    const std::string playlistId = ExtractJsonStringValue(output, "id");
    const std::string thumbnailUrl = ExtractJsonStringValue(output, "thumbnail");

    // Channel tabs (/@handle/videos, /channel/UC…/videos) often come back as _type=playlist
    // from yt-dlp (YoutubeTab). Prefer the URL shape over extractor _type.
    if ((LooksLikeChannelUrl(url) && !LooksLikePlaylistUrl(url)) || contentType == "channel")
    {
        info.kind = LinkGroupKind::Channel;
    }
    else
    {
        info.kind = LinkGroupKind::Playlist;
    }

    for (const std::string& objectJson : entryObjects)
    {
        LinkGroupEntry entry = ParseFlatEntryObject(objectJson, cacheDirectory);
        if (!entry.url.empty() || !entry.id.empty())
        {
            if (entry.url.empty() && !entry.id.empty())
            {
                entry.url = "https://www.youtube.com/watch?v=" + entry.id;
            }
            info.entries.push_back(std::move(entry));
        }
    }

    // Playlist/channel ids are not YouTube video ids — prefer JSON thumbnail URL, else first entry thumb.
    std::string thumbSeedId;
    if (!info.entries.empty() && !info.entries.front().id.empty())
    {
        thumbSeedId = info.entries.front().id;
    }
    else
    {
        thumbSeedId = playlistId;
    }
    info.thumbnailPath = ResolveThumbnailPath(cacheDirectory, thumbSeedId, thumbnailUrl).string();
    if (info.thumbnailPath.empty() && !info.entries.empty() && !info.entries.front().thumbnailPath.empty())
    {
        info.thumbnailPath = info.entries.front().thumbnailPath;
    }

    const int playlistCount = ExtractJsonIntValue(output, "playlist_count");
    info.entryCount = playlistCount > 0 ? playlistCount : static_cast<int>(info.entries.size());
    if (info.entryCount == 0)
    {
        info.entryCount = static_cast<int>(info.entries.size());
    }
    return info;
}

LinkInfo BuildPartialLinkInfoFromEntry(const LinkGroupEntry& entry)
{
    LinkInfo info;
    info.success = true;
    info.url = entry.url;
    info.title = entry.title.empty() ? entry.url : entry.title;
    info.normalizedTitle = NormalizeVideoTitle(info.title);
    info.duration = entry.duration.empty() ? "--:--" : entry.duration;
    info.thumbnailPath = entry.thumbnailPath;
    info.container = "Unknown";
    info.videoCodec = "None";
    info.audioCodec = "None";
    info.availableVideoFormats.push_back("MP4");
    info.availableAudioFormats.push_back("M4A");
    return info;
}
