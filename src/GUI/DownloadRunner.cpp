#include "DownloadRunner.h"

#include "BrowserDiagnostics.h"
#include "DownloadFormatPredictor.h"
#include "ToolPaths.h"
#include "VideoTitle.h"
#include "WinProcess.h"
#include "YtDlpLocator.h"
#include "YtDlpYouTube.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <future>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
std::string PathUtf8(const std::filesystem::path& path)
{
    return path.u8string();
}

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
    {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 0)
    {
        return {};
    }

    std::wstring wide(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), size);
    return wide;
}
#endif

std::filesystem::path FindFromCurrentPath(const std::filesystem::path& relativePath)
{
    std::filesystem::path directory = std::filesystem::current_path();
    while (!directory.empty())
    {
        const std::filesystem::path candidate = directory / relativePath;
        if (std::filesystem::exists(candidate))
        {
            return std::filesystem::absolute(candidate);
        }

        const std::filesystem::path parent = directory.parent_path();
        if (parent == directory)
        {
            break;
        }
        directory = parent;
    }

    return {};
}

std::filesystem::path FindExecutableInPath(const std::string& executableName)
{
    const char* pathValue = std::getenv("PATH");
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

            for (const std::string& candidateName : {executableName, executableName + ".exe"})
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

std::string ToLower(std::string value)
{
    for (char& c : value)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return value;
}

std::string QualityFilter(const std::string& quality)
{
    if (quality.empty() || quality == "Max")
    {
        return "[height>=144]";
    }

    int height = 0;
    for (const char c : quality)
    {
        if (c >= '0' && c <= '9')
        {
            height = height * 10 + (c - '0');
        }
        else if (height > 0)
        {
            break;
        }
    }

    if (height <= 0)
    {
        return "[height>=144]";
    }

    // Cap by YouTube quality label (short side). Landscape uses height<=N; Shorts/portrait use width<=N.
    return "[height>=144][height<=" + std::to_string(height) + "]/[width>=144][width<=" + std::to_string(height) + "]";
}

std::string SelectorQualityCap(const DownloadRequest& request)
{
    if (!request.qualityCap.empty() && request.qualityCap != "Max")
    {
        return request.qualityCap;
    }
    if (!request.quality.empty() && request.quality != "Max")
    {
        return request.quality;
    }
    return std::string{};
}

bool IsAudioOnlyExtension(const std::string& extension)
{
    return extension == "m4a" || extension == "mp3" || extension == "opus" || extension == "wav" ||
           extension == "flac" || extension == "aac";
}

std::string BuildFormatSelector(const DownloadRequest& request)
{
    const std::string ext = ToLower(request.fileFormat);
    const std::string qualityFilter = QualityFilter(SelectorQualityCap(request));
    const std::string skipUpscaled = "[format_id!*=sr]";

    // qualityFilter may contain "/" alternatives (landscape height / portrait width). Expand so each
    // bestvideo… branch tries both.
    const auto withQuality = [&](const std::string& prefix, const std::string& suffix = "") -> std::string
    {
        std::string result;
        size_t start = 0;
        while (start <= qualityFilter.size())
        {
            const size_t slash = qualityFilter.find('/', start);
            const std::string part =
                qualityFilter.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (!result.empty())
            {
                result += '/';
            }
            result += prefix + part + suffix;
            if (slash == std::string::npos)
            {
                break;
            }
            start = slash + 1;
        }
        return result;
    };

    if (request.mediaMode == "Audio only")
    {
        if (ext == "webm")
        {
            return "bestaudio[ext=webm]/bestaudio/best";
        }
        if (IsAudioOnlyExtension(ext))
        {
            return "bestaudio[ext=" + ext + "]/bestaudio/best";
        }
        return "bestaudio/best";
    }

    if (IsAudioOnlyExtension(ext))
    {
        return "bestaudio[ext=" + ext + "]/bestaudio/best";
    }

    // Stay inside the selected quality bucket. Prefer the chosen container, then any
    // container at the same height — never silently drop to a lower resolution.
    if (request.mediaMode == "Video only")
    {
        if (ext == "mp4")
        {
            return withQuality("bestvideo", "[ext=mp4]" + skipUpscaled) + "/" + withQuality("bestvideo", skipUpscaled) +
                   "/" + withQuality("bestvideo");
        }
        return withQuality("bestvideo", "[ext=" + ext + "]") + "/" + withQuality("bestvideo");
    }

    if (ext == "mp4")
    {
        // +audio must be inside each quality alternative. Appending after withQuality()
        // yields bestvideo[h]/bestvideo[w]+ba — yt-dlp then picks the video-only first branch.
        return withQuality("bestvideo", "[ext=mp4]" + skipUpscaled + "+bestaudio[ext=m4a]") + "/" +
               withQuality("bestvideo", skipUpscaled + "+bestaudio") + "/" + withQuality("bestvideo", "+bestaudio");
    }

    if (ext == "webm")
    {
        return withQuality("bestvideo", "[ext=webm]+bestaudio[ext=webm]") + "/" +
               withQuality("bestvideo", "[ext=webm]+bestaudio") + "/" + withQuality("bestvideo", "+bestaudio");
    }

    return withQuality("bestvideo", "+bestaudio");
}

std::string BuildRelaxedFormatSelector(const DownloadRequest& request)
{
    // Loosen container/codec constraints, but NEVER drop the quality cap — otherwise a
    // "format not available" retry silently upgrades 360p to best/1080p+.
    const std::string ext = ToLower(request.fileFormat);
    const std::string qualityFilter = QualityFilter(SelectorQualityCap(request));
    if (request.mediaMode == "Audio only" || IsAudioOnlyExtension(ext))
    {
        return "bestaudio/best";
    }

    const auto withQuality = [&](const std::string& prefix, const std::string& suffix = "") -> std::string
    {
        std::string result;
        size_t start = 0;
        while (start <= qualityFilter.size())
        {
            const size_t slash = qualityFilter.find('/', start);
            const std::string part =
                qualityFilter.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (!result.empty())
            {
                result += '/';
            }
            result += prefix + part + suffix;
            if (slash == std::string::npos)
            {
                break;
            }
            start = slash + 1;
        }
        return result;
    };

    if (request.mediaMode == "Video only")
    {
        return withQuality("bestvideo") + "/" + withQuality("best");
    }
    // Never fall back to bare `best` — on YouTube that is often video-only DASH.
    // +audio must be inside each quality alternative (same as BuildFormatSelector).
    return withQuality("bestvideo", "+bestaudio") + "/" + withQuality("bv*", "+ba") + "/b";
}

// HLS-only (m3u8) at the same quality cap — used after https DASH CDN 403.
// Never fall back to https DASH here: that reselects itag 401/315 and hits the same 403.
// OutputBelowRequestedQuality still rejects undersized files.
std::string BuildHlsFormatSelector(const DownloadRequest& request)
{
    const std::string ext = ToLower(request.fileFormat);
    const std::string qualityFilter = QualityFilter(SelectorQualityCap(request));

    const auto withQuality = [&](const std::string& prefix, const std::string& suffix = "") -> std::string
    {
        std::string result;
        size_t start = 0;
        while (start <= qualityFilter.size())
        {
            const size_t slash = qualityFilter.find('/', start);
            const std::string part =
                qualityFilter.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (!result.empty())
            {
                result += '/';
            }
            result += prefix + part + suffix;
            if (slash == std::string::npos)
            {
                break;
            }
            start = slash + 1;
        }
        return result;
    };

    if (request.mediaMode == "Audio only" || IsAudioOnlyExtension(ext))
    {
        return "bestaudio/best";
    }

    if (request.mediaMode == "Video only")
    {
        return withQuality("bestvideo", "[protocol^=m3u8]");
    }

    return withQuality("bestvideo", "[protocol^=m3u8]+bestaudio") + "/" + withQuality("bv*", "[protocol^=m3u8]+ba");
}

std::string TrimLine(std::string value)
{
    while (!value.empty() && (value.front() == '\r' || value.front() == '\n' || value.front() == ' '))
    {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
    {
        value.pop_back();
    }
    return value;
}

std::string BuildDownloadFailureStatus(const std::string& lastLine, const std::string& fullOutput, int exitCode)
{
    std::string message =
        IsYtDlpDownloadProgressLine(lastLine) || IsYtDlpBenignPostProcessLine(lastLine) ? std::string{} : lastLine;
    if (message.empty() && !fullOutput.empty())
    {
        message = SimplifyYtDlpError(fullOutput);
    }
    if (message.empty())
    {
        message = ExtractLastMeaningfulYtDlpOutputLine(fullOutput);
    }
    if (message.empty())
    {
        message = "process exited with code " + std::to_string(exitCode);
    }

    if (message.rfind("Download failed", 0) == 0)
    {
        return message;
    }
    return "Download failed: " + message;
}

std::string BuildDownloadErrorLog(const DownloadRequest& request,
                                  const std::string& formatSelector,
                                  int exitCode,
                                  const std::string& fullOutput)
{
    std::ostringstream stream;
    stream << "URL: " << request.url << "\n";
    stream << "Output folder: " << request.outputDirectory << "\n";
    stream << "Title: " << (request.title.empty() ? request.normalizedTitle : request.title) << "\n";
    stream << "Format: " << request.fileFormat << " | " << request.mediaMode << " | "
           << (!request.quality.empty() ? request.quality : request.qualityCap) << "\n";
    stream << "Selector: " << formatSelector << "\n";
    stream << "Exit code: " << exitCode << "\n";
    if (!fullOutput.empty())
    {
        stream << "\n--- yt-dlp output ---\n" << FilterYtDlpProgressLinesFromOutput(fullOutput);
    }
    else
    {
        stream << "\n(No output captured from yt-dlp.)\n";
    }
    return stream.str();
}

bool ParseDownloadProgressAt(const std::string& text, size_t percentIndex, float& progress)
{
    if (percentIndex == std::string::npos || percentIndex == 0)
    {
        return false;
    }

    size_t start = percentIndex;
    while (start > 0)
    {
        const char c = text[start - 1];
        if ((c >= '0' && c <= '9') || c == '.')
        {
            --start;
            continue;
        }
        break;
    }

    if (start == percentIndex)
    {
        return false;
    }

    try
    {
        progress = std::stof(text.substr(start, percentIndex - start)) / 100.0f;
        if (progress < 0.0f)
        {
            progress = 0.0f;
        }
        if (progress > 1.0f)
        {
            progress = 1.0f;
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool ParseDownloadProgress(const std::string& text, float& progress)
{
    const size_t percent = text.find('%');
    return ParseDownloadProgressAt(text, percent, progress);
}

bool ParseLastDownloadProgress(const std::string& text, float& progress)
{
    const size_t percent = text.rfind('%');
    return ParseDownloadProgressAt(text, percent, progress);
}

bool ParseYtdlpSpeedBps(const std::string& line, double& outBytesPerSecond)
{
    const size_t at = line.find(" at ");
    if (at == std::string::npos)
    {
        return false;
    }

    const size_t speedStart = at + 4;
    const size_t eta = line.find(" ETA ", speedStart);
    const size_t speedEnd = eta == std::string::npos ? line.size() : eta;
    const std::string token = TrimLine(line.substr(speedStart, speedEnd - speedStart));
    if (token.size() < 4 || token.back() != 's')
    {
        return false;
    }

    const size_t slash = token.rfind('/');
    if (slash == std::string::npos || slash == 0)
    {
        return false;
    }

    size_t valueEnd = 0;
    while (valueEnd < slash && ((token[valueEnd] >= '0' && token[valueEnd] <= '9') || token[valueEnd] == '.'))
    {
        ++valueEnd;
    }
    if (valueEnd == 0)
    {
        return false;
    }

    double value = 0.0;
    try
    {
        value = std::stod(token.substr(0, valueEnd));
    }
    catch (...)
    {
        return false;
    }

    const std::string unit = token.substr(valueEnd, slash - valueEnd);
    double multiplier = 0.0;
    if (unit == "B")
    {
        multiplier = 1.0;
    }
    else if (unit == "KiB")
    {
        multiplier = 1024.0;
    }
    else if (unit == "MiB")
    {
        multiplier = 1024.0 * 1024.0;
    }
    else if (unit == "GiB")
    {
        multiplier = 1024.0 * 1024.0 * 1024.0;
    }
    else
    {
        return false;
    }

    if (value < 0.0)
    {
        return false;
    }

    outBytesPerSecond = value * multiplier;
    return true;
}

// Parses totals from lines like: "[download] 12.3% of ~245.67GiB at ..."
bool ParseYtDlpTotalBytes(const std::string& line, std::int64_t& outBytes)
{
    const size_t ofPos = line.find(" of ");
    if (ofPos == std::string::npos)
    {
        return false;
    }

    size_t cursor = ofPos + 4;
    while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '~'))
    {
        ++cursor;
    }

    size_t valueEnd = cursor;
    while (valueEnd < line.size() && ((line[valueEnd] >= '0' && line[valueEnd] <= '9') || line[valueEnd] == '.'))
    {
        ++valueEnd;
    }
    if (valueEnd == cursor)
    {
        return false;
    }

    double value = 0.0;
    try
    {
        value = std::stod(line.substr(cursor, valueEnd - cursor));
    }
    catch (...)
    {
        return false;
    }

    size_t unitEnd = valueEnd;
    while (unitEnd < line.size() && std::isalpha(static_cast<unsigned char>(line[unitEnd])))
    {
        ++unitEnd;
    }
    const std::string unit = line.substr(valueEnd, unitEnd - valueEnd);
    double multiplier = 0.0;
    if (unit == "B")
    {
        multiplier = 1.0;
    }
    else if (unit == "KiB")
    {
        multiplier = 1024.0;
    }
    else if (unit == "MiB")
    {
        multiplier = 1024.0 * 1024.0;
    }
    else if (unit == "GiB")
    {
        multiplier = 1024.0 * 1024.0 * 1024.0;
    }
    else if (unit == "TiB")
    {
        multiplier = 1024.0 * 1024.0 * 1024.0 * 1024.0;
    }
    else
    {
        return false;
    }

    if (value <= 0.0)
    {
        return false;
    }

    outBytes = static_cast<std::int64_t>(value * multiplier + 0.5);
    return outBytes > 0;
}

// "[download] Downloading fragment 4377 of 50000" → ratio for estimate / yt progress.
bool ParseYtDlpFragmentProgress(const std::string& line, float& outRatio)
{
    const size_t frag = line.find("fragment ");
    if (frag == std::string::npos)
    {
        return false;
    }

    size_t cursor = frag + 9;
    while (cursor < line.size() && line[cursor] == ' ')
    {
        ++cursor;
    }

    size_t numEnd = cursor;
    while (numEnd < line.size() && line[numEnd] >= '0' && line[numEnd] <= '9')
    {
        ++numEnd;
    }
    if (numEnd == cursor)
    {
        return false;
    }

    size_t ofPos = line.find(" of ", numEnd);
    if (ofPos == std::string::npos)
    {
        return false;
    }

    size_t denStart = ofPos + 4;
    while (denStart < line.size() && line[denStart] == ' ')
    {
        ++denStart;
    }
    size_t denEnd = denStart;
    while (denEnd < line.size() && line[denEnd] >= '0' && line[denEnd] <= '9')
    {
        ++denEnd;
    }
    if (denEnd == denStart)
    {
        return false;
    }

    try
    {
        const double current = std::stod(line.substr(cursor, numEnd - cursor));
        const double total = std::stod(line.substr(denStart, denEnd - denStart));
        if (current <= 0.0 || total <= 0.0 || current > total * 1.01)
        {
            return false;
        }
        outRatio = static_cast<float>(std::clamp(current / total, 0.0, 1.0));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

int CountDownloadStreams(const std::string& mediaMode)
{
    return mediaMode == "Both" ? 2 : 1;
}

bool FilenameEndsWithIgnoreCase(const std::string& value, const std::string& suffix)
{
    if (suffix.empty() || value.size() < suffix.size())
    {
        return false;
    }
    for (size_t i = 0; i < suffix.size(); ++i)
    {
        const unsigned char a = static_cast<unsigned char>(value[value.size() - suffix.size() + i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b))
        {
            return false;
        }
    }
    return true;
}

// Extensions that belong to one UI container job (video + typical audio sidecar).
std::vector<std::string> CompanionExtensionsForFileFormat(const std::string& fileFormat)
{
    const std::string ext = ToLower(fileFormat);
    if (ext.empty())
    {
        return {};
    }
    if (ext == "mp4" || ext == "m4v")
    {
        return {"mp4", "m4v", "m4a", "ts", "m2ts", "mpegts"};
    }
    if (ext == "m4a")
    {
        return {"m4a"};
    }
    if (ext == "webm")
    {
        return {"webm", "opus", "ogg", "ts", "m2ts", "mpegts"};
    }
    if (ext == "mkv")
    {
        return {"mkv", "webm", "m4a", "opus", "ogg", "mp4", "ts", "m2ts", "mpegts"};
    }
    if (ext == "mp3" || ext == "opus" || ext == "ogg" || ext == "flac" || ext == "wav" || ext == "aac")
    {
        return {ext};
    }
    return {ext};
}

bool ArtifactMatchesFileFormat(const std::string& filename, const std::string& titleStem, const std::string& fileFormat)
{
    const std::vector<std::string> allowed = CompanionExtensionsForFileFormat(fileFormat);
    if (allowed.empty())
    {
        return true;
    }
    if (titleStem.empty() || filename.size() <= titleStem.size() ||
        filename.compare(0, titleStem.size(), titleStem) != 0 || filename[titleStem.size()] != '.')
    {
        return false;
    }

    std::string rest = ToLower(filename.substr(titleStem.size()));
    while (FilenameEndsWithIgnoreCase(rest, ".part"))
    {
        rest.resize(rest.size() - 5);
    }
    while (FilenameEndsWithIgnoreCase(rest, ".ytdl"))
    {
        rest.resize(rest.size() - 5);
    }
    // yt-dlp HLS temps look like ".mp4.part-Frag141" — strip that before "-frag" matching,
    // otherwise "-frag" hits inside "part-frag" and the extension becomes "part".
    const size_t partFrag = rest.find(".part-frag");
    if (partFrag != std::string::npos)
    {
        rest.resize(partFrag);
    }
    else
    {
        const size_t frag = rest.find("-frag");
        if (frag != std::string::npos)
        {
            size_t digits = frag + 5;
            while (digits < rest.size() && rest[digits] >= '0' && rest[digits] <= '9')
            {
                ++digits;
            }
            if (digits > frag + 5 && digits == rest.size())
            {
                rest.resize(frag);
            }
        }
    }

    const size_t lastDot = rest.rfind('.');
    if (lastDot == std::string::npos || lastDot + 1 >= rest.size())
    {
        return false;
    }
    const std::string fileExt = rest.substr(lastDot + 1);
    for (const std::string& allowedExt : allowed)
    {
        if (fileExt == allowedExt)
        {
            return true;
        }
    }
    return false;
}

bool IsYtDlpDownloadArtifactName(const std::string& filename, const std::string& titleStem)
{
    if (titleStem.empty() || filename.size() <= titleStem.size() ||
        filename.compare(0, titleStem.size(), titleStem) != 0 || filename[titleStem.size()] != '.')
    {
        return false;
    }

    if (filename.find(".part") != std::string::npos)
    {
        return true;
    }

    // yt-dlp resume/control sidecar (e.g. Title.mp4.ytdl)
    if (filename.size() >= 5 && filename.compare(filename.size() - 5, 5, ".ytdl") == 0)
    {
        return true;
    }

    // yt-dlp format fragments: Title.f398.mp4 / Title.f140.m4a
    const size_t formatMark = titleStem.size() + 1;
    if (formatMark + 1 < filename.size() && filename[formatMark] == 'f' &&
        std::isdigit(static_cast<unsigned char>(filename[formatMark + 1])))
    {
        return true;
    }
    return false;
}

// HLS main accumulator: Title.ext.part without transient -Frag* or DASH Title.fNNN.* side files.
bool IsMainDownloadPartFile(const std::string& filename, const std::string& titleStem)
{
    if (titleStem.empty() || filename.size() <= titleStem.size() ||
        filename.compare(0, titleStem.size(), titleStem) != 0 || filename[titleStem.size()] != '.')
    {
        return false;
    }

    if (filename.find(".part") == std::string::npos)
    {
        return false;
    }

    if (filename.find("-Frag") != std::string::npos)
    {
        return false;
    }

    const size_t formatMark = titleStem.size() + 1;
    if (formatMark + 1 < filename.size() && filename[formatMark] == 'f' &&
        std::isdigit(static_cast<unsigned char>(filename[formatMark + 1])))
    {
        return false;
    }

    return true;
}

// directory_entry::file_size() on Windows uses FindFirstFile cache and often stays
// stale while yt-dlp still has the .part open. Open+GetFileSizeEx sees live EOF.
std::uint64_t LiveFileSizeBytes(const std::filesystem::path& path)
{
#ifdef _WIN32
    const HANDLE handle = CreateFileW(path.c_str(),
                                      FILE_READ_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL,
                                      nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    LARGE_INTEGER size{};
    const BOOL ok = GetFileSizeEx(handle, &size);
    CloseHandle(handle);
    if (!ok || size.QuadPart < 0)
    {
        return 0;
    }
    return static_cast<std::uint64_t>(size.QuadPart);
#else
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
    {
        return 0;
    }
    return static_cast<std::uint64_t>(size);
#endif
}

std::uint64_t SumDownloadArtifactBytes(const std::string& outputDirectory,
                                       const std::string& titleStem,
                                       bool singleMainPart,
                                       const std::string& fileFormat)
{
    if (outputDirectory.empty() || titleStem.empty())
    {
        return 0;
    }

    std::error_code error;
    const std::filesystem::path directory = std::filesystem::u8path(outputDirectory);
    if (!std::filesystem::is_directory(directory, error) || error)
    {
        return 0;
    }

    std::uint64_t total = 0;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
    {
        if (error)
        {
            break;
        }
        if (!entry.is_regular_file(error) || error)
        {
            continue;
        }

        const std::string filename = entry.path().filename().u8string();
        if (!ArtifactMatchesFileFormat(filename, titleStem, fileFormat))
        {
            continue;
        }
        if (singleMainPart)
        {
            if (!IsMainDownloadPartFile(filename, titleStem))
            {
                continue;
            }
        }
        else if (!IsYtDlpDownloadArtifactName(filename, titleStem))
        {
            continue;
        }
        else if (filename.find("-Frag") != std::string::npos)
        {
            // HLS temps — size is already reflected in the main .part accumulator.
            continue;
        }

        total += LiveFileSizeBytes(entry.path());
        error.clear();
    }
    return total;
}

// Final Title.ext written by ffmpeg/yt-dlp merge (not .part / .ytdl / Title.fNNN.*).
bool IsMergeOutputCandidateName(const std::string& filename,
                                const std::string& titleStem,
                                const std::string& fileFormat)
{
    if (titleStem.empty() || filename.size() <= titleStem.size() + 1 ||
        filename.compare(0, titleStem.size(), titleStem) != 0 || filename[titleStem.size()] != '.')
    {
        return false;
    }

    if (IsYtDlpDownloadArtifactName(filename, titleStem))
    {
        return false;
    }
    return ArtifactMatchesFileFormat(filename, titleStem, fileFormat);
}

std::uint64_t
SumMergeOutputBytes(const std::string& outputDirectory, const std::string& titleStem, const std::string& fileFormat)
{
    if (outputDirectory.empty() || titleStem.empty())
    {
        return 0;
    }

    std::error_code error;
    const std::filesystem::path directory = std::filesystem::u8path(outputDirectory);
    if (!std::filesystem::is_directory(directory, error) || error)
    {
        return 0;
    }

    std::uint64_t total = 0;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
    {
        if (error)
        {
            break;
        }
        if (!entry.is_regular_file(error) || error)
        {
            continue;
        }

        const std::string filename = entry.path().filename().u8string();
        if (!IsMergeOutputCandidateName(filename, titleStem, fileFormat))
        {
            continue;
        }

        total += LiveFileSizeBytes(entry.path());
        error.clear();
    }
    return total;
}

std::filesystem::path
FindDownloadOutputFile(const std::string& outputDirectory, const std::string& titleStem, const std::string& fileFormat)
{
    if (outputDirectory.empty() || titleStem.empty())
    {
        return {};
    }

    std::error_code error;
    const std::filesystem::path directory = std::filesystem::u8path(outputDirectory);
    if (!std::filesystem::is_directory(directory, error) || error)
    {
        return {};
    }

    const std::string lowerExt = ToLower(fileFormat);
    if (!lowerExt.empty())
    {
        const std::filesystem::path exact = directory / (titleStem + "." + lowerExt);
        if (std::filesystem::is_regular_file(exact, error))
        {
            return exact;
        }
    }

    std::filesystem::path bestMatch;
    std::uint64_t bestSize = 0;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
    {
        if (error || !entry.is_regular_file(error))
        {
            continue;
        }
        const std::string filename = entry.path().filename().u8string();
        if (!IsMergeOutputCandidateName(filename, titleStem, fileFormat))
        {
            continue;
        }
        const std::uint64_t size = LiveFileSizeBytes(entry.path());
        if (bestMatch.empty() || size > bestSize)
        {
            bestMatch = entry.path();
            bestSize = size;
        }
        error.clear();
    }
    return bestMatch;
}

#ifdef _WIN32
std::wstring QuoteWidePath(const std::filesystem::path& path)
{
    std::wstring value = path.wstring();
    std::wstring escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back(L'"');
    for (const wchar_t ch : value)
    {
        if (ch == L'"')
        {
            escaped += L"\\\"";
        }
        else
        {
            escaped.push_back(ch);
        }
    }
    escaped.push_back(L'"');
    return escaped;
}

std::string CaptureProcessOutputUtf8(const std::wstring& commandLine, int* exitCodeOut)
{
    if (exitCodeOut != nullptr)
    {
        *exitCodeOut = -1;
    }

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0))
    {
        return {};
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startupInfo{};
    PROCESS_INFORMATION processInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    const BOOL started = CreateProcessW(nullptr,
                                        mutableCommand.data(),
                                        nullptr,
                                        nullptr,
                                        TRUE,
                                        CREATE_NO_WINDOW,
                                        nullptr,
                                        nullptr,
                                        &startupInfo,
                                        &processInfo);
    CloseHandle(writePipe);
    if (!started)
    {
        CloseHandle(readPipe);
        return {};
    }

    std::string output;
    std::array<char, 512> buffer{};
    for (;;)
    {
        DWORD read = 0;
        if (!ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size() - 1), &read, nullptr) || read == 0)
        {
            break;
        }
        output.append(buffer.data(), read);
    }

    WaitForSingleObject(processInfo.hProcess, 15000);
    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    if (exitCodeOut != nullptr)
    {
        *exitCodeOut = static_cast<int>(exitCode);
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    CloseHandle(readPipe);
    return output;
}
#else
std::string CaptureProcessOutputUtf8(const std::string& commandLine, int* exitCodeOut)
{
    if (exitCodeOut != nullptr)
    {
        *exitCodeOut = -1;
    }
    FILE* pipe = popen(commandLine.c_str(), "r");
    if (pipe == nullptr)
    {
        return {};
    }
    std::string output;
    std::array<char, 512> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
    {
        output += buffer.data();
    }
    const int status = pclose(pipe);
    if (exitCodeOut != nullptr)
    {
        if (status < 0)
        {
            *exitCodeOut = -1;
        }
        else if (WIFEXITED(status))
        {
            *exitCodeOut = WEXITSTATUS(status);
        }
        else
        {
            *exitCodeOut = status;
        }
    }
    return output;
}
#endif

bool BothOutputMissingAudio(const DownloadRequest& request, const std::string& outputTitle)
{
    if (request.mediaMode != "Both")
    {
        return false;
    }
    const std::filesystem::path outputFile =
        FindDownloadOutputFile(request.outputDirectory, outputTitle, request.fileFormat);
    if (outputFile.empty())
    {
        return false;
    }
    return ProbeMediaFileAudio(outputFile) == MediaAudioProbeResult::MissingAudio;
}

void TryRemoveDownloadPath(const std::filesystem::path& path);

int ProbeMediaVideoShortSidePixels(const std::filesystem::path& mediaPath)
{
    std::error_code error;
    if (mediaPath.empty() || !std::filesystem::is_regular_file(mediaPath, error) ||
        std::filesystem::file_size(mediaPath, error) == 0)
    {
        return 0;
    }

    const std::filesystem::path ffprobe = FindFfprobeExecutable();
    if (ffprobe.empty())
    {
        return 0;
    }

#ifdef _WIN32
    const std::wstring commandLine =
        QuoteWidePath(ffprobe) + L" -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x " +
        QuoteWidePath(mediaPath);
    int exitCode = -1;
    const std::string output = CaptureProcessOutputUtf8(commandLine, &exitCode);
#else
    auto shellQuote = [](const std::string& value) -> std::string
    {
        std::string escaped;
        escaped.reserve(value.size() + 2);
        escaped.push_back('"');
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
        escaped.push_back('"');
        return escaped;
    };
    const std::string command = shellQuote(PathUtf8(ffprobe)) +
                                " -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x " +
                                shellQuote(PathUtf8(mediaPath)) + " 2>&1";
    int exitCode = -1;
    const std::string output = CaptureProcessOutputUtf8(command, &exitCode);
#endif
    if (exitCode != 0 || output.empty())
    {
        return 0;
    }

    int width = 0;
    int height = 0;
    const size_t sep = output.find('x');
    if (sep == std::string::npos)
    {
        const size_t comma = output.find(',');
        if (comma == std::string::npos)
        {
            return 0;
        }
        try
        {
            width = std::stoi(output.substr(0, comma));
            height = std::stoi(output.substr(comma + 1));
        }
        catch (...)
        {
            return 0;
        }
    }
    else
    {
        try
        {
            width = std::stoi(output.substr(0, sep));
            height = std::stoi(output.substr(sep + 1));
        }
        catch (...)
        {
            return 0;
        }
    }
    if (width <= 0 || height <= 0)
    {
        return 0;
    }
    return std::min(width, height);
}

double ProbeMediaDurationSeconds(const std::filesystem::path& mediaPath)
{
    std::error_code error;
    if (mediaPath.empty() || !std::filesystem::is_regular_file(mediaPath, error) ||
        std::filesystem::file_size(mediaPath, error) == 0)
    {
        return 0.0;
    }

    const std::filesystem::path ffprobe = FindFfprobeExecutable();
    if (ffprobe.empty())
    {
        return 0.0;
    }

#ifdef _WIN32
    const std::wstring commandLine =
        QuoteWidePath(ffprobe) + L" -v error -show_entries format=duration -of csv=p=0 " + QuoteWidePath(mediaPath);
    int exitCode = -1;
    const std::string output = CaptureProcessOutputUtf8(commandLine, &exitCode);
#else
    auto shellQuote = [](const std::string& value) -> std::string
    {
        std::string escaped;
        escaped.reserve(value.size() + 2);
        escaped.push_back('"');
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
        escaped.push_back('"');
        return escaped;
    };
    const std::string command = shellQuote(PathUtf8(ffprobe)) + " -v error -show_entries format=duration -of csv=p=0 " +
                                shellQuote(PathUtf8(mediaPath)) + " 2>&1";
    int exitCode = -1;
    const std::string output = CaptureProcessOutputUtf8(command, &exitCode);
#endif
    if (exitCode != 0 || output.empty())
    {
        return 0.0;
    }

    try
    {
        const double duration = std::stod(output);
        return duration > 0.0 && std::isfinite(duration) ? duration : 0.0;
    }
    catch (...)
    {
        return 0.0;
    }
}

bool HasInProgressDownloadArtifacts(const std::string& outputDirectory, const std::string& titleStem)
{
    if (outputDirectory.empty() || titleStem.empty())
    {
        return false;
    }

    std::error_code error;
    const std::filesystem::path directory = std::filesystem::u8path(outputDirectory);
    if (!std::filesystem::is_directory(directory, error) || error)
    {
        return false;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
    {
        if (error || !entry.is_regular_file(error))
        {
            continue;
        }
        const std::string filename = entry.path().filename().u8string();
        if (IsYtDlpDownloadArtifactName(filename, titleStem) && filename.find(".part") != std::string::npos)
        {
            return true;
        }
        error.clear();
    }
    return false;
}

// yt-dlp sometimes exits non-zero after merge/cleanup even when Title.ext is complete (live DASH especially).
bool OutputFileSalvageableAfterNonZeroExit(const DownloadRequest& request, const std::string& outputTitle)
{
    const std::filesystem::path outputFile =
        FindDownloadOutputFile(request.outputDirectory, outputTitle, request.fileFormat);
    if (outputFile.empty())
    {
        return false;
    }

    std::error_code error;
    const std::uint64_t size = LiveFileSizeBytes(outputFile);
    constexpr std::uint64_t kMinOutputBytes = 256 * 1024;
    if (size < kMinOutputBytes)
    {
        return false;
    }

    if (HasInProgressDownloadArtifacts(request.outputDirectory, outputTitle))
    {
        return false;
    }

    if (request.mediaMode == "Both" && ProbeMediaFileAudio(outputFile) == MediaAudioProbeResult::MissingAudio)
    {
        return false;
    }

    const double duration = ProbeMediaDurationSeconds(outputFile);
    if (duration > 1.0)
    {
        return true;
    }

    if (request.mediaMode != "Audio only")
    {
        if (ProbeMediaVideoShortSidePixels(outputFile) > 0)
        {
            return true;
        }
    }

    // ffprobe missing or inconclusive — accept only a clearly non-trivial merged file with no .part temps.
    constexpr std::uint64_t kLargeOutputFallbackBytes = 1024 * 1024;
    return size >= kLargeOutputFallbackBytes;
}

void TrySalvageNonZeroExitDownload(const DownloadRequest& request,
                                   const std::string& outputTitle,
                                   DownloadRunResult& result)
{
    if (result.status == "Download finished." || result.status == "Download cancelled.")
    {
        return;
    }
    if (result.exitCode == 0)
    {
        return;
    }
    if (!OutputFileSalvageableAfterNonZeroExit(request, outputTitle))
    {
        return;
    }
    result.status = "Download finished.";
    result.exitCode = 0;
}

int ProbeOutputVideoBucket(const DownloadRequest& request, const std::string& outputTitle)
{
    if (request.mediaMode == "Audio only")
    {
        return 0;
    }
    const std::filesystem::path outputFile =
        FindDownloadOutputFile(request.outputDirectory, outputTitle, request.fileFormat);
    if (outputFile.empty())
    {
        return 0;
    }
    const int shortSide = ProbeMediaVideoShortSidePixels(outputFile);
    if (shortSide <= 0)
    {
        return 0;
    }
    return BucketDownloadHeight(shortSide);
}

// Accept output below the validation floor when floor equals cap (cap-as-max batch semantics).
bool OutputWithinCapAsMaxAcceptance(const DownloadRequest& request, const std::string& outputTitle)
{
    const int floor = ParseQualityHeight(request.quality);
    const int cap = ParseQualityHeight(!request.qualityCap.empty() ? request.qualityCap : request.quality);
    if (floor <= 0 || cap <= 0 || floor != cap)
    {
        return false;
    }
    const int actualBucket = ProbeOutputVideoBucket(request, outputTitle);
    return actualBucket > 0 && actualBucket <= cap;
}

// True when an explicit quality floor was requested but the file's ladder bucket is lower.
// Cookie/client caps often silently settle on 1080 under height<=2160.
bool OutputBelowRequestedQuality(const DownloadRequest& request, const std::string& outputTitle)
{
    if (request.mediaMode == "Audio only")
    {
        return false;
    }
    const int requested = ParseQualityHeight(request.quality);
    if (requested <= 0)
    {
        return false; // Max / empty — no hard floor.
    }
    const std::filesystem::path outputFile =
        FindDownloadOutputFile(request.outputDirectory, outputTitle, request.fileFormat);
    if (outputFile.empty())
    {
        return false;
    }
    const int shortSide = ProbeMediaVideoShortSidePixels(outputFile);
    if (shortSide <= 0)
    {
        return false;
    }
    const int actualBucket = BucketDownloadHeight(shortSide);
    return actualBucket > 0 && actualBucket < requested;
}

void DiscardUndersizedDownloadOutput(const DownloadRequest& request, const std::string& outputTitle)
{
    const std::filesystem::path outputFile =
        FindDownloadOutputFile(request.outputDirectory, outputTitle, request.fileFormat);
    TryRemoveDownloadPath(outputFile);
}

void TryRemoveDownloadPath(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return;
    }
#ifdef _WIN32
    const std::wstring widePath = path.wstring();
    SetFileAttributesW(widePath.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (DeleteFileW(widePath.c_str()))
    {
        return;
    }
    const std::wstring trashPath = widePath + L".trash";
    if (MoveFileExW(widePath.c_str(), trashPath.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        SetFileAttributesW(trashPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(trashPath.c_str());
    }
#else
    std::error_code error;
    std::filesystem::remove(path, error);
#endif
}

// Deletes yt-dlp leftovers (.part / .ytdl / Title.fXXX.*) for this title+format. Never touches final Title.ext.
// aggressiveStemMatch: any yt-dlp temp under the stem (ignore container filter) + longer delete retries.
void RemoveDownloadArtifacts(const std::string& outputDirectory,
                             const std::string& titleStem,
                             const std::string& fileFormat,
                             bool aggressiveStemMatch = false)
{
    if (outputDirectory.empty() || titleStem.empty())
    {
        return;
    }

    std::error_code error;
    const std::filesystem::path directory = std::filesystem::u8path(outputDirectory);
    if (!std::filesystem::is_directory(directory, error) || error)
    {
        return;
    }

    std::vector<std::filesystem::path> toRemove;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
    {
        if (error)
        {
            break;
        }
        if (!entry.is_regular_file(error) || error)
        {
            continue;
        }

        const std::string filename = entry.path().filename().u8string();
        const bool isArtifact = IsYtDlpDownloadArtifactName(filename, titleStem);
        if (isArtifact && (aggressiveStemMatch || ArtifactMatchesFileFormat(filename, titleStem, fileFormat)))
        {
            toRemove.push_back(entry.path());
        }
        error.clear();
    }

    for (const std::filesystem::path& path : toRemove)
    {
        TryRemoveDownloadPath(path);
    }

#ifdef _WIN32
    // yt-dlp may release handles slightly after process kill (live Frag*.part especially).
    const int retryPasses = aggressiveStemMatch ? 8 : 1;
    const DWORD retryDelayMs = aggressiveStemMatch ? 250 : 150;
    for (int pass = 0; pass < retryPasses && !toRemove.empty(); ++pass)
    {
        Sleep(retryDelayMs);
        std::vector<std::filesystem::path> remaining;
        for (const std::filesystem::path& path : toRemove)
        {
            if (std::filesystem::exists(path, error))
            {
                TryRemoveDownloadPath(path);
                error.clear();
                if (std::filesystem::exists(path, error))
                {
                    remaining.push_back(path);
                }
            }
            error.clear();
        }
        toRemove = std::move(remaining);
    }
#endif
}

void RefreshSharedDiskProgress(const std::shared_ptr<DownloadSharedState>& sharedState,
                               const std::string& outputDirectory,
                               const std::string& titleStem,
                               std::int64_t estimatedBytes,
                               bool singleMainPart,
                               const std::string& fileFormat)
{
    if (sharedState == nullptr)
    {
        return;
    }

    // Prefer single main .part when prediction says so, or when HLS Frags appear on disk.
    bool useSingleMain = singleMainPart;
    if (!useSingleMain)
    {
        std::error_code error;
        const std::filesystem::path directory = std::filesystem::u8path(outputDirectory);
        if (std::filesystem::is_directory(directory, error) && !error)
        {
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
            {
                if (error || !entry.is_regular_file(error) || error)
                {
                    continue;
                }
                const std::string name = entry.path().filename().u8string();
                if (name.find(titleStem) == 0 && name.find("-Frag") != std::string::npos &&
                    ArtifactMatchesFileFormat(name, titleStem, fileFormat))
                {
                    useSingleMain = true;
                    break;
                }
                error.clear();
            }
        }
    }

    const std::uint64_t onDisk = SumDownloadArtifactBytes(outputDirectory, titleStem, useSingleMain, fileFormat);
    // Mispredicted HLS-only mode: DASH writes Title.fNNN.* — fall back to summing those.
    const std::uint64_t onDiskResolved = (useSingleMain && onDisk == 0)
                                             ? SumDownloadArtifactBytes(outputDirectory, titleStem, false, fileFormat)
                                             : onDisk;
    DownloadRunner::SetSharedDiskBytes(sharedState, onDiskResolved);

    std::int64_t totalBytes = estimatedBytes;
    {
        std::lock_guard<std::mutex> lock(sharedState->mutex);
        if (sharedState->estimatedBytes > totalBytes)
        {
            totalBytes = sharedState->estimatedBytes;
        }
        else if (estimatedBytes > 0 && sharedState->estimatedBytes <= 0)
        {
            sharedState->estimatedBytes = estimatedBytes;
        }
    }

    if (totalBytes <= 0)
    {
        return;
    }

    const float ratio = std::clamp(static_cast<float>(onDiskResolved) / static_cast<float>(totalBytes), 0.0f, 1.0f);
    DownloadRunner::SetSharedDiskProgress(sharedState, ratio);

    // Taskbar / overall progress may follow disk when yt-% stalls — never touch ytProgress here.
    // During merge, artifact sum is already ~complete; overwriting progress would fill the yellow bar instantly.
    {
        std::lock_guard<std::mutex> lock(sharedState->mutex);
        if (sharedState->phase == DownloadSharedState::Phase::Merging)
        {
            return;
        }
        if (ratio > sharedState->progress || sharedState->progress <= 0.05f)
        {
            sharedState->progress = ratio;
        }
    }
}

// Live catch-up: downloaded_seconds / (now - live_start). Prefer bytes/bitrate; optional ffprobe when ready.
void RefreshSharedLiveCatchupProgress(const std::shared_ptr<DownloadSharedState>& sharedState,
                                      const std::string& outputDirectory,
                                      const std::string& titleStem,
                                      bool singleMainPart,
                                      const std::string& fileFormat,
                                      std::int64_t liveStartUnix,
                                      double estimatedBitrateBps)
{
    if (sharedState == nullptr || liveStartUnix <= 0 || estimatedBitrateBps <= 0.0)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(sharedState->mutex);
        if (sharedState->phase == DownloadSharedState::Phase::Merging)
        {
            return;
        }
    }

    const std::int64_t nowUnix =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const double streamAgeSec = std::max(1.0, static_cast<double>(nowUnix - liveStartUnix));

    bool useSingleMain = singleMainPart;
    if (!useSingleMain)
    {
        std::error_code error;
        const std::filesystem::path directory = std::filesystem::u8path(outputDirectory);
        if (std::filesystem::is_directory(directory, error) && !error)
        {
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
            {
                if (error || !entry.is_regular_file(error) || error)
                {
                    continue;
                }
                const std::string name = entry.path().filename().u8string();
                if (name.find(titleStem) == 0 && name.find("-Frag") != std::string::npos &&
                    ArtifactMatchesFileFormat(name, titleStem, fileFormat))
                {
                    useSingleMain = true;
                    break;
                }
                error.clear();
            }
        }
    }

    const std::uint64_t onDisk = SumDownloadArtifactBytes(outputDirectory, titleStem, useSingleMain, fileFormat);
    const std::uint64_t onDiskResolved = (useSingleMain && onDisk == 0)
                                             ? SumDownloadArtifactBytes(outputDirectory, titleStem, false, fileFormat)
                                             : onDisk;
    DownloadRunner::SetSharedDiskBytes(sharedState, onDiskResolved);

    const double downloadedSec = (static_cast<double>(onDiskResolved) * 8.0) / estimatedBitrateBps;
    const float ratio = std::clamp(static_cast<float>(downloadedSec / streamAgeSec), 0.0f, 0.99f);
    DownloadRunner::SetSharedDiskProgress(sharedState, ratio);

    {
        std::lock_guard<std::mutex> lock(sharedState->mutex);
        sharedState->ytProgress = ratio;
        sharedState->progress = ratio;
        sharedState->status = "Downloading " + std::to_string(static_cast<int>(ratio * 100.0f + 0.5f)) + "%";
    }
}

class DownloadProgressTracker
{
public:
    explicit DownloadProgressTracker(int streamCount)
        : streamCount_(std::max(1, streamCount))
    {
    }

    float UpdateLine(const std::string& line)
    {
        if (IsMergeLine(line))
        {
            EnterMerging();
            return PhaseProgress();
        }

        if (line.find("[ExtractAudio]") != std::string::npos || line.find("Post-process") != std::string::npos)
        {
            EnterMerging();
            return PhaseProgress();
        }

        if (phase_ == DownloadSharedState::Phase::Merging)
        {
            AdvanceMergeProgress();
            return PhaseProgress();
        }

        if (line.find("[download]") != std::string::npos && line.find("Destination:") != std::string::npos)
        {
            if (sawDestination_)
            {
                streamIndex_ = std::min(streamIndex_ + 1, streamCount_ - 1);
                lastStreamProgress_ = 0.0f;
            }
            sawDestination_ = true;
            downloadProgress_ = std::max(downloadProgress_, StreamBase() + 0.02f);
            return PhaseProgress();
        }

        float streamProgress = 0.0f;
        if (!ParseDownloadProgress(line, streamProgress))
        {
            return PhaseProgress();
        }

        if (streamProgress + 0.15f < lastStreamProgress_ && streamIndex_ < streamCount_ - 1)
        {
            streamIndex_++;
            lastStreamProgress_ = 0.0f;
        }
        lastStreamProgress_ = std::max(lastStreamProgress_, streamProgress);

        downloadProgress_ = std::max(downloadProgress_, MapStreamProgress(streamProgress));
        return PhaseProgress();
    }

    float UpdateChunk(const std::string& pendingText)
    {
        if (phase_ == DownloadSharedState::Phase::Merging)
        {
            AdvanceMergeProgress();
            return PhaseProgress();
        }

        float streamProgress = 0.0f;
        if (ParseLastDownloadProgress(pendingText, streamProgress))
        {
            if (streamProgress + 0.15f < lastStreamProgress_ && streamIndex_ < streamCount_ - 1)
            {
                streamIndex_++;
                lastStreamProgress_ = 0.0f;
            }
            lastStreamProgress_ = std::max(lastStreamProgress_, streamProgress);
            downloadProgress_ = std::max(downloadProgress_, MapStreamProgress(streamProgress));
        }
        return PhaseProgress();
    }

    float PhaseProgress() const
    {
        return phase_ == DownloadSharedState::Phase::Merging ? mergeProgress_ : downloadProgress_;
    }

    DownloadSharedState::Phase Phase() const
    {
        return phase_;
    }

    float Overall() const
    {
        return PhaseProgress();
    }

    void UpdateStreamSpeed(const std::string& line)
    {
        double speedBps = -1.0;
        if (!ParseYtdlpSpeedBps(line, speedBps))
        {
            return;
        }

        if (streamIndex_ >= 0 && streamIndex_ < static_cast<int>(streamSpeedBps_.size()))
        {
            streamSpeedBps_[static_cast<size_t>(streamIndex_)] = speedBps;
        }
    }

    double TotalStreamSpeedBps() const
    {
        double total = 0.0;
        bool hasAny = false;
        const int activeStreams = std::min(streamCount_, static_cast<int>(streamSpeedBps_.size()));
        for (int index = 0; index < activeStreams; ++index)
        {
            const double speed = streamSpeedBps_[static_cast<size_t>(index)];
            if (speed >= 0.0)
            {
                total += speed;
                hasAny = true;
            }
        }
        return hasAny ? total : -1.0;
    }

    void ResetStreamSpeeds()
    {
        streamSpeedBps_.fill(-1.0);
    }

    // Prefer output-file growth vs merge inputs; fall back to size-scaled elapsed time.
    void
    UpdateMergeFromDisk(const std::string& outputDirectory, const std::string& titleStem, const std::string& fileFormat)
    {
        if (phase_ != DownloadSharedState::Phase::Merging)
        {
            return;
        }

        if (mergeInputBytes_ == 0)
        {
            std::uint64_t inputs = SumDownloadArtifactBytes(outputDirectory, titleStem, false, fileFormat);
            if (inputs == 0)
            {
                inputs = SumDownloadArtifactBytes(outputDirectory, titleStem, true, fileFormat);
            }
            mergeInputBytes_ = inputs;
        }

        float diskRatio = -1.0f;
        if (mergeInputBytes_ > 0)
        {
            const std::uint64_t outputBytes = SumMergeOutputBytes(outputDirectory, titleStem, fileFormat);
            if (outputBytes > 0)
            {
                diskRatio =
                    std::clamp(static_cast<float>(outputBytes) / static_cast<float>(mergeInputBytes_), 0.0f, 1.0f);
            }
        }

        AdvanceMergeProgress(diskRatio);
    }

private:
    static bool IsMergeLine(const std::string& line)
    {
        return line.find("[Merger]") != std::string::npos || line.find("Merging formats") != std::string::npos ||
               (line.find("[ffmpeg]") != std::string::npos && line.find("Merging") != std::string::npos);
    }

    void EnterMerging()
    {
        if (phase_ == DownloadSharedState::Phase::Merging)
        {
            AdvanceMergeProgress();
            return;
        }

        phase_ = DownloadSharedState::Phase::Merging;
        mergeStartedAt_ = std::chrono::steady_clock::now();
        mergeProgress_ = 0.04f;
        mergeInputBytes_ = 0;
        ResetStreamSpeeds();
    }

    void AdvanceMergeProgress(float diskRatio = -1.0f)
    {
        if (phase_ != DownloadSharedState::Phase::Merging)
        {
            return;
        }

        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - mergeStartedAt_).count();

        // Remux is roughly disk-bound; scale fake time by input size (~32 MiB/s), clamp 15s..30min.
        double estimatedSeconds = 90.0;
        if (mergeInputBytes_ > 0)
        {
            estimatedSeconds =
                std::clamp(static_cast<double>(mergeInputBytes_) / (32.0 * 1024.0 * 1024.0), 15.0, 1800.0);
        }
        const float fromTime =
            std::min(0.95f, 0.04f + 0.91f * static_cast<float>(elapsed / std::max(1.0, estimatedSeconds)));

        float candidate = fromTime;
        if (diskRatio >= 0.0f)
        {
            const float fromDisk = std::min(0.95f, std::max(0.04f, diskRatio));
            // Disk leads when ffmpeg writes the final file; time keeps a slow floor if output lags.
            candidate = std::max(fromDisk, fromTime * 0.25f);
        }

        mergeProgress_ = std::max(mergeProgress_, candidate);
    }

    float StreamBase() const
    {
        if (streamCount_ <= 1)
        {
            return 0.0f;
        }

        return (static_cast<float>(streamIndex_) / static_cast<float>(streamCount_));
    }

    float MapStreamProgress(float streamProgress) const
    {
        const float streamShare = streamCount_ == 1 ? 1.0f : (1.0f / static_cast<float>(streamCount_));
        return std::min(StreamBase() + streamProgress * streamShare, 0.99f);
    }

    int streamCount_;
    int streamIndex_ = 0;
    bool sawDestination_ = false;
    float lastStreamProgress_ = 0.0f;
    float downloadProgress_ = 0.04f;
    float mergeProgress_ = 0.0f;
    std::uint64_t mergeInputBytes_ = 0;
    DownloadSharedState::Phase phase_ = DownloadSharedState::Phase::Downloading;
    std::chrono::steady_clock::time_point mergeStartedAt_{};
    std::array<double, 4> streamSpeedBps_{-1.0, -1.0, -1.0, -1.0};
};

void RefreshSharedMergeProgress(const std::shared_ptr<DownloadSharedState>& sharedState,
                                DownloadProgressTracker& tracker,
                                const std::string& outputDirectory,
                                const std::string& titleStem,
                                const std::string& fileFormat)
{
    if (sharedState == nullptr || tracker.Phase() != DownloadSharedState::Phase::Merging)
    {
        return;
    }

    const float previous = tracker.PhaseProgress();
    tracker.UpdateMergeFromDisk(outputDirectory, titleStem, fileFormat);
    const float progress = tracker.PhaseProgress();
    if (progress > previous + 0.001f || previous < 0.05f)
    {
        DownloadRunner::SetSharedStatus(sharedState,
                                        "Merging " + std::to_string(static_cast<int>(progress * 100.0f)) + "%",
                                        progress,
                                        DownloadSharedState::Phase::Merging);
    }
}

std::string BuildProgressStatus(const std::string& line, float progress)
{
    const size_t at = line.find(" at ");
    const size_t eta = line.find(" ETA ");
    std::string status = "Downloading " + std::to_string(static_cast<int>(progress * 100.0f)) + "%";
    if (at != std::string::npos)
    {
        const size_t speedStart = at + 4;
        const size_t speedEnd = eta == std::string::npos ? line.size() : eta;
        const std::string speed = TrimLine(line.substr(speedStart, speedEnd - speedStart));
        if (!speed.empty())
        {
            status += "  " + speed;
        }
    }
    if (eta != std::string::npos)
    {
        const std::string etaText = TrimLine(line.substr(eta + 5));
        if (!etaText.empty())
        {
            status += "  ETA " + etaText;
        }
    }
    return status;
}

void ApplyDownloadProgress(const std::shared_ptr<DownloadSharedState>& sharedState,
                           DownloadProgressTracker& tracker,
                           const std::string& line,
                           std::string& lastMeaningfulLine,
                           bool liveCatchupMode)
{
    if (line.empty())
    {
        return;
    }

    if (!IsYtDlpDownloadProgressLine(line))
    {
        lastMeaningfulLine = line;
    }
    const float phaseProgress = tracker.UpdateLine(line);
    const DownloadSharedState::Phase phase = tracker.Phase();
    if (line.find("ERROR:") != std::string::npos)
    {
        DownloadRunner::SetSharedStatus(sharedState, line, tracker.PhaseProgress(), phase);
        return;
    }

    // Live catch-up % comes from bytes/(now-live_start), not fragment X of Y.
    if (liveCatchupMode && phase == DownloadSharedState::Phase::Downloading)
    {
        float streamProgress = 0.0f;
        if (ParseDownloadProgress(line, streamProgress))
        {
            tracker.UpdateStreamSpeed(line);
            const double totalSpeed = tracker.TotalStreamSpeedBps();
            if (totalSpeed >= 0.0)
            {
                DownloadRunner::SetSharedYtDlpSpeed(sharedState, totalSpeed);
            }
        }
        return;
    }

    float streamProgress = 0.0f;
    if (phase == DownloadSharedState::Phase::Merging)
    {
        DownloadRunner::SetSharedYtDlpSpeed(sharedState, -1.0);
        DownloadRunner::SetSharedStatus(sharedState,
                                        "Merging " + std::to_string(static_cast<int>(phaseProgress * 100.0f)) + "%",
                                        phaseProgress,
                                        phase);
    }
    else if (ParseDownloadProgress(line, streamProgress))
    {
        std::int64_t totalBytes = 0;
        if (ParseYtDlpTotalBytes(line, totalBytes))
        {
            DownloadRunner::SetSharedEstimatedBytes(sharedState, totalBytes);
        }
        tracker.UpdateStreamSpeed(line);
        const double totalSpeed = tracker.TotalStreamSpeedBps();
        if (totalSpeed >= 0.0)
        {
            DownloadRunner::SetSharedYtDlpSpeed(sharedState, totalSpeed);
        }
        DownloadRunner::SetSharedStatus(sharedState, BuildProgressStatus(line, streamProgress), phaseProgress, phase);
    }
    else if (line.find("[download]") != std::string::npos)
    {
        std::int64_t totalBytes = 0;
        if (ParseYtDlpTotalBytes(line, totalBytes))
        {
            DownloadRunner::SetSharedEstimatedBytes(sharedState, totalBytes);
        }

        float fragmentRatio = 0.0f;
        if (ParseYtDlpFragmentProgress(line, fragmentRatio))
        {
            // Fragment index is the honest yt-dlp progress for HLS (no rolling %).
            // Do NOT infer total size from disk/fragment — that makes disk% == yt% and hides the dim bar.
            DownloadRunner::SetSharedStatus(sharedState,
                                            "Downloading " + std::to_string(static_cast<int>(fragmentRatio * 100.0f)) +
                                                "%",
                                            fragmentRatio,
                                            phase);
        }
        else
        {
            DownloadRunner::SetSharedStatus(sharedState,
                                            "Downloading " + std::to_string(static_cast<int>(phaseProgress * 100.0f)) +
                                                "%",
                                            phaseProgress,
                                            phase);
        }
    }
}

void IngestDownloadOutputChunk(const std::shared_ptr<DownloadSharedState>& sharedState,
                               DownloadProgressTracker& tracker,
                               std::string& pendingText,
                               std::string& fullOutput,
                               std::string& lastMeaningfulLine,
                               const char* data,
                               size_t size,
                               bool liveCatchupMode)
{
    pendingText.append(data, size);
    fullOutput.append(data, size);
    size_t separator = pendingText.find_first_of("\r\n");
    while (separator != std::string::npos)
    {
        std::string line = TrimLine(pendingText.substr(0, separator));
        pendingText.erase(0, separator + 1);
        ApplyDownloadProgress(sharedState, tracker, line, lastMeaningfulLine, liveCatchupMode);
        separator = pendingText.find_first_of("\r\n");
    }

    if (liveCatchupMode)
    {
        return;
    }

    const float previousProgress = tracker.PhaseProgress();
    const auto previousPhase = tracker.Phase();
    tracker.UpdateChunk(pendingText);
    if (tracker.Phase() != previousPhase || tracker.PhaseProgress() > previousProgress + 0.001f)
    {
        const float progress = tracker.PhaseProgress();
        const std::string status = tracker.Phase() == DownloadSharedState::Phase::Merging
                                       ? ("Merging " + std::to_string(static_cast<int>(progress * 100.0f)) + "%")
                                       : ("Downloading " + std::to_string(static_cast<int>(progress * 100.0f)) + "%");
        DownloadRunner::SetSharedStatus(sharedState, status, progress, tracker.Phase());
    }
}

#ifdef _WIN32
DownloadRunResult RunProcess(std::string command,
                             const std::shared_ptr<std::atomic_bool>& cancelRequested,
                             const std::shared_ptr<DownloadSharedState>& sharedState,
                             int downloadStreams,
                             const std::string& outputDirectory,
                             const std::string& titleStem,
                             std::int64_t estimatedBytes,
                             bool singleMainPartDiskProgress,
                             const std::string& fileFormat,
                             bool liveFromStart,
                             std::int64_t liveStartUnix,
                             double estimatedBitrateBps)
{
    DownloadRunResult result;
    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0))
    {
        result.status = "Download failed: could not create output pipe.";
        return result;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startupInfo{};
    PROCESS_INFORMATION processInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    std::wstring wideCommand = Utf8ToWide(command);
    std::vector<wchar_t> mutableCommand(wideCommand.begin(), wideCommand.end());
    mutableCommand.push_back(L'\0');
    const BOOL started = CreateProcessW(nullptr,
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
        result.status = "Download failed: could not start yt-dlp.";
        return result;
    }

    DWORD exitCode = STILL_ACTIVE;
    std::string pendingText;
    std::string fullOutput;
    std::string lastMeaningfulLine;
    DownloadProgressTracker tracker(downloadStreams);
    DownloadRunner::SetSharedStatus(sharedState, "Downloading 4%", tracker.PhaseProgress(), tracker.Phase());
    const auto consumeOutput = [&]()
    {
        DWORD available = 0;
        while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0)
        {
            char buffer[512]{};
            DWORD read = 0;
            if (!ReadFile(readPipe, buffer, static_cast<DWORD>(sizeof(buffer) - 1), &read, nullptr) || read == 0)
            {
                break;
            }

            pendingText.append(buffer, read);
            fullOutput.append(buffer, read);
            size_t separator = pendingText.find_first_of("\r\n");
            while (separator != std::string::npos)
            {
                std::string line = TrimLine(pendingText.substr(0, separator));
                pendingText.erase(0, separator + 1);
                ApplyDownloadProgress(sharedState, tracker, line, lastMeaningfulLine, liveFromStart);
                separator = pendingText.find_first_of("\r\n");
            }

            if (liveFromStart)
            {
                continue;
            }

            const float previousProgress = tracker.PhaseProgress();
            const auto previousPhase = tracker.Phase();
            tracker.UpdateChunk(pendingText);
            if (tracker.Phase() != previousPhase || tracker.PhaseProgress() > previousProgress + 0.001f)
            {
                const float progress = tracker.PhaseProgress();
                const std::string status =
                    tracker.Phase() == DownloadSharedState::Phase::Merging
                        ? ("Merging " + std::to_string(static_cast<int>(progress * 100.0f)) + "%")
                        : ("Downloading " + std::to_string(static_cast<int>(progress * 100.0f)) + "%");
                DownloadRunner::SetSharedStatus(sharedState, status, progress, tracker.Phase());
            }
        }
    };

    const auto refreshProgress = [&]()
    {
        if (liveFromStart)
        {
            RefreshSharedLiveCatchupProgress(sharedState,
                                             outputDirectory,
                                             titleStem,
                                             singleMainPartDiskProgress,
                                             fileFormat,
                                             liveStartUnix,
                                             estimatedBitrateBps);
        }
        else
        {
            RefreshSharedDiskProgress(
                sharedState, outputDirectory, titleStem, estimatedBytes, singleMainPartDiskProgress, fileFormat);
        }
        RefreshSharedMergeProgress(sharedState, tracker, outputDirectory, titleStem, fileFormat);
    };

    while (WaitForSingleObject(processInfo.hProcess, 50) == WAIT_TIMEOUT)
    {
        consumeOutput();
        refreshProgress();
        if (cancelRequested != nullptr && cancelRequested->load())
        {
            KillProcessTree(processInfo.dwProcessId);
            WaitForSingleObject(processInfo.hProcess, 3000);
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            CloseHandle(readPipe);
            result.status = "Download cancelled.";
            return result;
        }
    }
    consumeOutput();
    for (;;)
    {
        char buffer[512]{};
        DWORD read = 0;
        if (!ReadFile(readPipe, buffer, static_cast<DWORD>(sizeof(buffer) - 1), &read, nullptr) || read == 0)
        {
            break;
        }

        pendingText.append(buffer, read);
        fullOutput.append(buffer, read);
        size_t separator = pendingText.find_first_of("\r\n");
        while (separator != std::string::npos)
        {
            std::string line = TrimLine(pendingText.substr(0, separator));
            pendingText.erase(0, separator + 1);
            ApplyDownloadProgress(sharedState, tracker, line, lastMeaningfulLine, liveFromStart);
            separator = pendingText.find_first_of("\r\n");
        }

        if (liveFromStart)
        {
            continue;
        }

        const float previousProgress = tracker.PhaseProgress();
        const auto previousPhase = tracker.Phase();
        tracker.UpdateChunk(pendingText);
        if (tracker.Phase() != previousPhase || tracker.PhaseProgress() > previousProgress + 0.001f)
        {
            const float progress = tracker.PhaseProgress();
            const std::string status =
                tracker.Phase() == DownloadSharedState::Phase::Merging
                    ? ("Merging " + std::to_string(static_cast<int>(progress * 100.0f)) + "%")
                    : ("Downloading " + std::to_string(static_cast<int>(progress * 100.0f)) + "%");
            DownloadRunner::SetSharedStatus(sharedState, status, progress, tracker.Phase());
        }
    }
    if (!pendingText.empty())
    {
        const std::string tailLine = TrimLine(pendingText);
        if (!tailLine.empty())
        {
            ApplyDownloadProgress(sharedState, tracker, tailLine, lastMeaningfulLine, liveFromStart);
        }
    }

    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    CloseHandle(readPipe);

    if (exitCode == 0)
    {
        result.status = "Download finished.";
        return result;
    }

    result.status = BuildDownloadFailureStatus(lastMeaningfulLine, fullOutput, exitCode);
    result.errorLog = FilterYtDlpProgressLinesFromOutput(fullOutput);
    result.exitCode = static_cast<int>(exitCode);
    return result;
}
#else
DownloadRunResult RunProcess(std::string command,
                             const std::shared_ptr<std::atomic_bool>& cancelRequested,
                             const std::shared_ptr<DownloadSharedState>& sharedState,
                             int downloadStreams,
                             const std::string& outputDirectory,
                             const std::string& titleStem,
                             std::int64_t estimatedBytes,
                             bool singleMainPartDiskProgress,
                             const std::string& fileFormat,
                             bool liveFromStart,
                             std::int64_t liveStartUnix,
                             double estimatedBitrateBps)
{
    DownloadRunResult result;
    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0)
    {
        result.status = "Download failed: could not create output pipe.";
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        result.status = "Download failed: could not start yt-dlp.";
        return result;
    }

    if (pid == 0)
    {
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

    std::string pendingText;
    std::string fullOutput;
    std::string lastMeaningfulLine;
    DownloadProgressTracker tracker(downloadStreams);
    DownloadRunner::SetSharedStatus(sharedState, "Downloading 4%", tracker.PhaseProgress(), tracker.Phase());

    std::array<char, 512> buffer{};
    bool childExited = false;
    int status = 0;
    while (!childExited)
    {
        if (cancelRequested != nullptr && cancelRequested->load())
        {
            KillProcessTree(static_cast<unsigned long>(pid));
            waitpid(pid, &status, 0);
            close(pipefd[0]);
            result.status = "Download cancelled.";
            return result;
        }

        pollfd pfd{};
        pfd.fd = pipefd[0];
        pfd.events = POLLIN;
        const int pollResult = poll(&pfd, 1, 50);
        if (liveFromStart)
        {
            RefreshSharedLiveCatchupProgress(sharedState,
                                             outputDirectory,
                                             titleStem,
                                             singleMainPartDiskProgress,
                                             fileFormat,
                                             liveStartUnix,
                                             estimatedBitrateBps);
        }
        else
        {
            RefreshSharedDiskProgress(
                sharedState, outputDirectory, titleStem, estimatedBytes, singleMainPartDiskProgress, fileFormat);
        }
        RefreshSharedMergeProgress(sharedState, tracker, outputDirectory, titleStem, fileFormat);
        if (pollResult > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0)
        {
            for (;;)
            {
                const ssize_t bytesRead = read(pipefd[0], buffer.data(), buffer.size());
                if (bytesRead > 0)
                {
                    IngestDownloadOutputChunk(sharedState,
                                              tracker,
                                              pendingText,
                                              fullOutput,
                                              lastMeaningfulLine,
                                              buffer.data(),
                                              static_cast<size_t>(bytesRead),
                                              liveFromStart);
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
            IngestDownloadOutputChunk(sharedState,
                                      tracker,
                                      pendingText,
                                      fullOutput,
                                      lastMeaningfulLine,
                                      buffer.data(),
                                      static_cast<size_t>(bytesRead),
                                      liveFromStart);
            continue;
        }
        break;
    }
    close(pipefd[0]);

    if (!pendingText.empty())
    {
        const std::string tailLine = TrimLine(pendingText);
        if (!tailLine.empty())
        {
            ApplyDownloadProgress(sharedState, tracker, tailLine, lastMeaningfulLine, liveFromStart);
        }
    }

    // Cancel may arrive after the child already exited; still honor it.
    if (cancelRequested != nullptr && cancelRequested->load())
    {
        result.status = "Download cancelled.";
        return result;
    }

    int exitCode = 1;
    if (WIFEXITED(status))
    {
        exitCode = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        exitCode = 128 + WTERMSIG(status);
    }

    if (exitCode == 0)
    {
        result.status = "Download finished.";
        return result;
    }

    result.status = BuildDownloadFailureStatus(lastMeaningfulLine, fullOutput, exitCode);
    result.errorLog = FilterYtDlpProgressLinesFromOutput(fullOutput);
    result.exitCode = exitCode;
    return result;
}
#endif
} // namespace

MediaAudioProbeResult ProbeMediaFileAudio(const std::filesystem::path& mediaPath)
{
    std::error_code error;
    if (mediaPath.empty() || !std::filesystem::is_regular_file(mediaPath, error) ||
        std::filesystem::file_size(mediaPath, error) == 0)
    {
        return MediaAudioProbeResult::Unavailable;
    }

    const std::filesystem::path ffprobe = FindFfprobeExecutable();
    if (ffprobe.empty())
    {
        return MediaAudioProbeResult::Unavailable;
    }

#ifdef _WIN32
    const std::wstring commandLine = QuoteWidePath(ffprobe) +
                                     L" -v error -select_streams a -show_entries stream=codec_type -of csv=p=0 " +
                                     QuoteWidePath(mediaPath);
    int exitCode = -1;
    const std::string output = CaptureProcessOutputUtf8(commandLine, &exitCode);
#else
    auto shellQuote = [](const std::string& value) -> std::string
    {
        std::string escaped;
        escaped.reserve(value.size() + 2);
        escaped.push_back('"');
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
        escaped.push_back('"');
        return escaped;
    };
    const std::string command = shellQuote(PathUtf8(ffprobe)) +
                                " -v error -select_streams a -show_entries stream=codec_type -of csv=p=0 " +
                                shellQuote(PathUtf8(mediaPath)) + " 2>&1";
    int exitCode = -1;
    const std::string output = CaptureProcessOutputUtf8(command, &exitCode);
#endif

    if (exitCode != 0)
    {
        return MediaAudioProbeResult::Unavailable;
    }

    // Empty stdout with exit 0 means no audio streams matched -select_streams a.
    for (char c : output)
    {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
        {
            return MediaAudioProbeResult::HasAudio;
        }
    }
    return MediaAudioProbeResult::MissingAudio;
}

namespace
{
template <typename T>
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
} // namespace

void DownloadRunner::Start(DownloadRequest request)
{
    if (isRunning_)
    {
        return;
    }

    FinishFuture(future_);

    status_ = "Downloading...";
    progress_ = 0.04f;
    ytProgress_ = 0.04f;
    diskProgress_ = -1.0f;
    ytDlpSpeedBps_ = -1.0;
    diskSpeedBps_ = -1.0;
    estimatedBytes_ = request.estimatedBytes > 0 ? request.estimatedBytes : 0;
    diskBytes_ = 0;
    lastDiskBytesSample_ = 0;
    lastDiskSampleAt_ = {};
    phase_ = DownloadSharedState::Phase::Downloading;
    liveFromStart_ = request.liveFromStart;
    elapsedSeconds_ = 0.0;
    currentUrl_ = request.url;
    cardInstanceId_ = request.cardInstanceId;
    outputIdentity_ = MakeOutputIdentity(request);
    lastErrorLog_.clear();
    lastDownloadBrowserReport_.clear();
    lastResolvedTitle_.clear();
    lastResolvedNormalizedTitle_.clear();
    completedUrl_.clear();
    completedElapsedSeconds_ = 0.0;
    hasCompletedDownload_ = false;
    startedAt_ = std::chrono::steady_clock::now();
    cancelRequested_ = std::make_shared<std::atomic_bool>(false);
    sharedState_ = std::make_shared<DownloadSharedState>();
    SetSharedStatus(sharedState_, status_, progress_, phase_);
    if (request.estimatedBytes > 0)
    {
        SetSharedEstimatedBytes(sharedState_, request.estimatedBytes);
    }

    try
    {
        future_ =
            std::async(std::launch::async,
                       [request = std::move(request), cancelRequested = cancelRequested_, sharedState = sharedState_]
                       {
                           return Run(request, cancelRequested, sharedState);
                       });
    }
    catch (...)
    {
        status_ = "Download failed: could not start download task.";
        isRunning_ = false;
        cancelRequested_.reset();
        sharedState_.reset();
        return;
    }

    isRunning_ = true;
}

void DownloadRunner::Cancel()
{
    if (!isRunning_)
    {
        return;
    }

    status_ = "Cancelling...";
    if (sharedState_ != nullptr)
    {
        SetSharedStatus(sharedState_, status_, progress_, phase_);
    }
    if (cancelRequested_ != nullptr)
    {
        cancelRequested_->store(true);
    }
}

void DownloadRunner::Shutdown()
{
    Cancel();
    if (future_.valid())
    {
        try
        {
            future_.wait();
            (void)future_.get();
        }
        catch (...)
        {
        }
    }
    isRunning_ = false;
    cancelRequested_.reset();
    sharedState_.reset();
}

void DownloadRunner::Update()
{
    if (!future_.valid())
    {
        if (isRunning_)
        {
            isRunning_ = false;
        }
        return;
    }

    if (future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        const bool cancelWasRequested = cancelRequested_ != nullptr && cancelRequested_->load();
        try
        {
            const DownloadRunResult result = future_.get();
            status_ = result.status;
            lastErrorLog_ = result.errorLog;
            lastDownloadBrowserReport_ = result.downloadBrowserReport;
            lastResolvedTitle_ = result.resolvedTitle;
            lastResolvedNormalizedTitle_ = result.resolvedNormalizedTitle;
        }
        catch (const std::exception& exception)
        {
            status_ = std::string("Download failed: ") + exception.what();
            lastErrorLog_ = exception.what();
        }
        catch (...)
        {
            status_ = "Download failed: unexpected error.";
            lastErrorLog_.clear();
        }
        elapsedSeconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
        if (cancelWasRequested && status_ == "Download finished.")
        {
            status_ = "Download cancelled.";
        }
        progress_ = status_ == "Download finished." ? 1.0f : progress_;
        if (status_ == "Download finished.")
        {
            completedUrl_ = currentUrl_;
            completedElapsedSeconds_ = elapsedSeconds_;
            hasCompletedDownload_ = true;
        }
        isRunning_ = false;
        cancelRequested_.reset();
        sharedState_.reset();
        return;
    }

    if (!isRunning_)
    {
        return;
    }

    if (sharedState_ != nullptr)
    {
        std::lock_guard<std::mutex> lock(sharedState_->mutex);
        status_ = sharedState_->status;
        progress_ = sharedState_->progress;
        ytProgress_ = sharedState_->ytProgress;
        diskProgress_ = sharedState_->diskProgress;
        phase_ = sharedState_->phase;
        ytDlpSpeedBps_ = sharedState_->ytDlpSpeedBps;
        estimatedBytes_ = sharedState_->estimatedBytes;
        diskBytes_ = sharedState_->diskBytes;

        const auto now = std::chrono::steady_clock::now();
        const std::uint64_t diskBytes = diskBytes_;
        if (lastDiskSampleAt_.time_since_epoch().count() == 0)
        {
            lastDiskBytesSample_ = diskBytes;
            lastDiskSampleAt_ = now;
        }
        else if (diskBytes >= lastDiskBytesSample_)
        {
            const double elapsedSeconds = std::chrono::duration<double>(now - lastDiskSampleAt_).count();
            if (elapsedSeconds >= 0.2)
            {
                diskSpeedBps_ = static_cast<double>(diskBytes - lastDiskBytesSample_) / elapsedSeconds;
                lastDiskBytesSample_ = diskBytes;
                lastDiskSampleAt_ = now;
            }
        }
        else
        {
            lastDiskBytesSample_ = diskBytes;
            lastDiskSampleAt_ = now;
            diskSpeedBps_ = -1.0;
        }
    }
    elapsedSeconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
}

bool DownloadRunner::IsRunning() const
{
    return isRunning_;
}

const std::string& DownloadRunner::Status() const
{
    return status_;
}

const std::string& DownloadRunner::CurrentUrl() const
{
    return currentUrl_;
}

std::uint64_t DownloadRunner::CardInstanceId() const
{
    return cardInstanceId_;
}

const std::string& DownloadRunner::OutputIdentity() const
{
    return outputIdentity_;
}

std::string DownloadRunner::MakeOutputIdentity(const DownloadRequest& request)
{
    return request.url + "\n" + request.outputDirectory + "\n" + request.normalizedTitle + "\n" +
           ToLower(request.fileFormat);
}

float DownloadRunner::Progress() const
{
    return progress_;
}

float DownloadRunner::YtProgress() const
{
    return ytProgress_;
}

float DownloadRunner::DiskProgress() const
{
    return diskProgress_;
}

bool DownloadRunner::LiveFromStart() const
{
    return liveFromStart_;
}

DownloadSharedState::Phase DownloadRunner::Phase() const
{
    return phase_;
}

double DownloadRunner::ElapsedSeconds() const
{
    return elapsedSeconds_;
}

double DownloadRunner::YtDlpSpeedBps() const
{
    return ytDlpSpeedBps_;
}

double DownloadRunner::DiskSpeedBps() const
{
    return diskSpeedBps_;
}

std::int64_t DownloadRunner::EstimatedBytes() const
{
    return estimatedBytes_;
}

std::uint64_t DownloadRunner::DiskBytes() const
{
    return diskBytes_;
}

bool DownloadRunner::ConsumeCompletedDownload(std::string& url, double& elapsedSeconds)
{
    if (!hasCompletedDownload_)
    {
        return false;
    }

    url = completedUrl_;
    elapsedSeconds = completedElapsedSeconds_;
    hasCompletedDownload_ = false;
    return true;
}

void DownloadRunner::SetStatus(std::string status)
{
    status_ = std::move(status);
    elapsedSeconds_ = 0.0;
    progress_ = 0.0f;
}

const std::string& DownloadRunner::LastErrorLog() const
{
    return lastErrorLog_;
}

const std::string& DownloadRunner::LastDownloadBrowserReport() const
{
    return lastDownloadBrowserReport_;
}

const std::string& DownloadRunner::LastResolvedTitle() const
{
    return lastResolvedTitle_;
}

const std::string& DownloadRunner::LastResolvedNormalizedTitle() const
{
    return lastResolvedNormalizedTitle_;
}

namespace
{
std::string ExtractNumberingPrefix(const std::string& stem)
{
    size_t index = 0;
    while (index < stem.size() && std::isdigit(static_cast<unsigned char>(stem[index])))
    {
        ++index;
    }
    if (index > 0 && index + 1 < stem.size() && stem[index] == '.' && stem[index + 1] == ' ')
    {
        return stem.substr(0, index + 2);
    }
    return {};
}

bool EndsWithDownloadedSuffix(const std::string& stem)
{
    constexpr char kSuffix[] = "_downloaded";
    constexpr size_t kSuffixLen = sizeof(kSuffix) - 1;
    return stem.size() >= kSuffixLen && stem.compare(stem.size() - kSuffixLen, kSuffixLen, kSuffix) == 0;
}

std::string StripDownloadedSuffix(std::string stem)
{
    if (EndsWithDownloadedSuffix(stem))
    {
        stem.resize(stem.size() - 11);
    }
    return stem;
}

std::string CaptureYtDlpStdout(std::string command, const std::shared_ptr<std::atomic_bool>& cancelRequested)
{
#ifdef _WIN32
    command = "cmd /S /C \"chcp 65001>nul && set PYTHONIOENCODING=utf-8 && " + command + "\"";
#else
    command = "env PYTHONIOENCODING=utf-8 PYTHONUNBUFFERED=1 " + command;
#endif

#ifdef _WIN32
    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0))
    {
        return {};
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startupInfo{};
    PROCESS_INFORMATION processInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    std::wstring wideCommand = Utf8ToWide(command);
    std::vector<wchar_t> mutableCommand(wideCommand.begin(), wideCommand.end());
    mutableCommand.push_back(L'\0');
    if (!CreateProcessW(nullptr,
                        mutableCommand.data(),
                        nullptr,
                        nullptr,
                        TRUE,
                        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                        nullptr,
                        nullptr,
                        &startupInfo,
                        &processInfo))
    {
        CloseHandle(writePipe);
        CloseHandle(readPipe);
        return {};
    }
    CloseHandle(writePipe);

    std::string output;
    while (WaitForSingleObject(processInfo.hProcess, 50) == WAIT_TIMEOUT)
    {
        if (cancelRequested != nullptr && cancelRequested->load())
        {
            KillProcessTree(processInfo.dwProcessId);
            WaitForSingleObject(processInfo.hProcess, 3000);
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            CloseHandle(readPipe);
            return {};
        }
        DWORD available = 0;
        while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0)
        {
            char buffer[512]{};
            DWORD read = 0;
            if (!ReadFile(readPipe, buffer, static_cast<DWORD>(sizeof(buffer) - 1), &read, nullptr) || read == 0)
            {
                break;
            }
            output.append(buffer, read);
        }
    }
    for (;;)
    {
        char buffer[512]{};
        DWORD read = 0;
        if (!ReadFile(readPipe, buffer, static_cast<DWORD>(sizeof(buffer) - 1), &read, nullptr) || read == 0)
        {
            break;
        }
        output.append(buffer, read);
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    CloseHandle(readPipe);
#else
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr)
    {
        return {};
    }
    std::string output;
    char buffer[512]{};
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        if (cancelRequested != nullptr && cancelRequested->load())
        {
            pclose(pipe);
            return {};
        }
        output += buffer;
    }
    pclose(pipe);
#endif

    std::string best;
    size_t lineStart = 0;
    while (lineStart < output.size())
    {
        size_t lineEnd = output.find_first_of("\r\n", lineStart);
        if (lineEnd == std::string::npos)
        {
            lineEnd = output.size();
        }
        std::string line = output.substr(lineStart, lineEnd - lineStart);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
        {
            line.pop_back();
        }
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        {
            line.erase(line.begin());
        }
        if (!line.empty() && line.rfind("WARNING:", 0) != 0 && line.rfind("ERROR:", 0) != 0 && line.rfind("[", 0) != 0)
        {
            best = line;
        }
        lineStart = lineEnd + (lineEnd < output.size() ? 1 : 0);
        while (lineStart < output.size() && (output[lineStart] == '\r' || output[lineStart] == '\n'))
        {
            ++lineStart;
        }
    }
    return best;
}

std::string QuoteArg(const std::string& value)
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

std::string ProbeOriginalYoutubeTitle(const std::string& ytDlpInvocation,
                                      const std::string& url,
                                      const std::shared_ptr<std::atomic_bool>& cancelRequested)
{
    if (ytDlpInvocation.empty() || url.empty())
    {
        return {};
    }
    const std::string base = ytDlpInvocation + BuildYoutubeJsRuntimeArgs() +
                             " --skip-download --no-playlist --no-warnings --print %(title)s ";
    std::string title = CaptureYtDlpStdout(base + QuoteArg(NormalizeYoutubeUrl(url)) + " 2>&1", cancelRequested);
    if (!title.empty() || (cancelRequested != nullptr && cancelRequested->load()))
    {
        return title;
    }
    for (const std::string& browser : BuildYoutubeCookieBrowsersToTryList())
    {
        if (browser.empty())
        {
            continue;
        }
        title = CaptureYtDlpStdout(base + BuildYoutubeCookiesArgs(browser) + " " + QuoteArg(NormalizeYoutubeUrl(url)) +
                                       " 2>&1",
                                   cancelRequested);
        if (!title.empty() || (cancelRequested != nullptr && cancelRequested->load()))
        {
            return title;
        }
    }
    return {};
}

void ApplyOriginalTitleToRequest(DownloadRequest& request, const std::string& originalTitle)
{
    if (originalTitle.empty())
    {
        return;
    }
    request.title = StripYoutubeLiveStreamTitleSuffix(originalTitle);
    const bool downloadedSuffix = EndsWithDownloadedSuffix(request.normalizedTitle);
    std::string baseStem = request.normalizedTitle.empty() ? NormalizeVideoTitle(request.title)
                                                           : StripDownloadedSuffix(request.normalizedTitle);
    if (baseStem.empty())
    {
        baseStem = NormalizeVideoTitle(request.title);
    }
    const std::string prefix = ExtractNumberingPrefix(baseStem);
    const std::string newBase = prefix + NormalizeVideoTitle(request.title);
    request.normalizedTitle = downloadedSuffix ? (newBase + "_downloaded") : newBase;
    if (!request.originalNormalizedTitle.empty())
    {
        const std::string originalPrefix = ExtractNumberingPrefix(request.originalNormalizedTitle);
        request.originalNormalizedTitle = originalPrefix + NormalizeVideoTitle(request.title);
    }
}
} // namespace

DownloadRunResult DownloadRunner::Run(DownloadRequest request,
                                      std::shared_ptr<std::atomic_bool> cancelRequested,
                                      std::shared_ptr<DownloadSharedState> sharedState)
{
    DownloadRunResult result;
    try
    {
        if (request.url.empty())
        {
            result.status = "Download failed: empty URL.";
            result.errorLog = result.status;
            return result;
        }

        if (request.outputDirectory.empty())
        {
            result.status = "Download failed: empty output path.";
            result.errorLog = result.status;
            return result;
        }

        std::error_code error;
        std::filesystem::create_directories(std::filesystem::u8path(request.outputDirectory), error);
        if (error)
        {
            result.status = "Download failed: could not create output path.";
            result.errorLog = result.status;
            return result;
        }

        const std::string ytDlpInvocation = BuildYtDlpCommandPrefix();
        if (ytDlpInvocation.empty())
        {
            result.status = "Download failed: yt-dlp not found.";
            result.errorLog = result.status + "\nLooked for packages\\ytdown\\python\\python.exe next to 4KDowner.exe.";
            return result;
        }

        DownloadRunner::SetSharedStatus(sharedState, "Resolving title...", 0.02f);
        const std::string originalTitle = ProbeOriginalYoutubeTitle(ytDlpInvocation, request.url, cancelRequested);
        if (cancelRequested != nullptr && cancelRequested->load())
        {
            result.status = "Download cancelled.";
            return result;
        }
        if (!originalTitle.empty())
        {
            ApplyOriginalTitleToRequest(request, originalTitle);
            result.resolvedTitle = originalTitle;
        }

        // Pass the native binary path (not the bin/ folder): a shared packages/
        // tree may contain both ffmpeg.exe and a Linux `ffmpeg` side by side.
        const std::filesystem::path ffmpegPath = ::FindFfmpegExecutable();
        const std::string ext = ToLower(request.fileFormat);
        const std::string formatSelector = BuildFormatSelector(request);
        const std::string relaxedFormatSelector = BuildRelaxedFormatSelector(request);
        const std::string hlsFormatSelector = BuildHlsFormatSelector(request);
        const std::string outputTitle =
            request.normalizedTitle.empty() ? NormalizeVideoTitle(request.title) : request.normalizedTitle;
        if (!result.resolvedTitle.empty())
        {
            result.resolvedNormalizedTitle = outputTitle;
        }
        auto buildCommandBase = [&](const std::string& selector,
                                    bool forceOverwrite,
                                    const std::string& youtubeRuntimeArgs = {}) -> std::string
        {
            const std::string& runtimeArgs =
                youtubeRuntimeArgs.empty() ? BuildYoutubeJsRuntimeArgs() : youtubeRuntimeArgs;
            std::string commandBase = ytDlpInvocation + runtimeArgs + " --no-playlist --no-warnings -P " +
                                      Quote(request.outputDirectory) + " -o " + Quote(outputTitle + ".%(ext)s") +
                                      " -f " + Quote(selector);
            if (request.liveFromStart)
            {
                commandBase += " --live-from-start";
            }
            commandBase += forceOverwrite ? " --force-overwrites" : " --no-overwrites";

            if (!ffmpegPath.empty())
            {
                commandBase += " --ffmpeg-location " + Quote(PathUtf8(ffmpegPath));
            }

            if (request.mediaMode == "Audio only")
            {
                commandBase += " --extract-audio --audio-format " + (ext == "webm" ? std::string("opus") : ext);
            }
            else if (IsAudioOnlyExtension(ext))
            {
                commandBase += " --extract-audio --audio-format " + ext;
            }
            else if (!ext.empty())
            {
                commandBase += " --merge-output-format " + ext;
            }
            return commandBase;
        };
        const bool preferForceOverwrite = request.overwriteExisting;
        bool forceOverwrite = preferForceOverwrite;

        const std::vector<std::string> browsersToTry = BuildYoutubeCookieBrowsersToTryList();
        BrowserAttemptLog downloadLog;
        std::string lastOutput;
        std::string successfulBrowser;
        std::string bestFailureStatus;
        std::string bestFailureLog;
        int bestFailureScore = -1; // higher = more useful for the user
        bool noCookiesAttempted = false;
        bool noCookiesHad403 = false;
        bool visionOsHlsTried = false;

        auto rememberFailure = [&](const std::string& status, const std::string& log)
        {
            int score = 1;
            if (IsYoutubeHttpForbiddenError(log) || IsYoutubeHttpForbiddenError(status))
            {
                score = 5;
            }
            else if (status.find("below requested quality") != std::string::npos ||
                     log.find("below requested quality") != std::string::npos)
            {
                score = 4;
            }
            else if (log.find("cookies database") != std::string::npos ||
                     log.find("Could not read browser cookies") != std::string::npos)
            {
                score = 0; // never prefer "Opera missing" as the headline error
            }
            if (score > bestFailureScore)
            {
                bestFailureScore = score;
                bestFailureStatus = status;
                bestFailureLog = FilterYtDlpProgressLinesFromOutput(log);
                if (bestFailureLog.empty())
                {
                    bestFailureLog = SimplifyYtDlpError(log);
                }
            }
        };

        for (size_t browserIndex = 0; browserIndex < browsersToTry.size(); ++browserIndex)
        {
            const std::string& browser = browsersToTry[browserIndex];
            const bool hasMoreBrowsers = browserIndex + 1 < browsersToTry.size();
            if (browser.empty())
            {
                noCookiesAttempted = true;
            }

            const std::string activeCommandBase = buildCommandBase(formatSelector, forceOverwrite);
            const std::string activeRelaxedCommandBase = buildCommandBase(relaxedFormatSelector, forceOverwrite);

            auto runOnce = [&](const std::string& baseCommand) -> bool
            {
                std::string command = baseCommand + BuildYoutubeCookiesArgs(browser) + " " +
                                      Quote(NormalizeYoutubeUrl(request.url)) + " 2>&1";

#ifdef _WIN32
                command = "cmd /S /C \"chcp 65001>nul && set PYTHONIOENCODING=utf-8 && set PYTHONUNBUFFERED=1 && " +
                          command + "\"";
#else
                command = "PYTHONIOENCODING=utf-8 PYTHONUNBUFFERED=1 " + command;
#endif
                result = RunProcess(command,
                                    cancelRequested,
                                    sharedState,
                                    CountDownloadStreams(request.mediaMode),
                                    request.outputDirectory,
                                    outputTitle,
                                    request.estimatedBytes,
                                    request.singleMainPartDiskProgress,
                                    request.fileFormat,
                                    request.liveFromStart,
                                    request.liveStartUnix,
                                    request.estimatedBitrateBps);
                TrySalvageNonZeroExitDownload(request, outputTitle, result);
                lastOutput = result.errorLog;
                return result.status == "Download finished.";
            };

            // VisionOS HLS works without cookies; signed-in sessions often hide 4K / reload.
            auto runVisionOsOnce = [&](const std::string& baseCommand) -> bool
            {
                std::string command = baseCommand + " " + Quote(NormalizeYoutubeUrl(request.url)) + " 2>&1";

#ifdef _WIN32
                command = "cmd /S /C \"chcp 65001>nul && set PYTHONIOENCODING=utf-8 && set PYTHONUNBUFFERED=1 && " +
                          command + "\"";
#else
                command = "PYTHONIOENCODING=utf-8 PYTHONUNBUFFERED=1 " + command;
#endif
                result = RunProcess(command,
                                    cancelRequested,
                                    sharedState,
                                    CountDownloadStreams(request.mediaMode),
                                    request.outputDirectory,
                                    outputTitle,
                                    request.estimatedBytes,
                                    request.singleMainPartDiskProgress,
                                    request.fileFormat,
                                    request.liveFromStart,
                                    request.liveStartUnix,
                                    request.estimatedBitrateBps);
                TrySalvageNonZeroExitDownload(request, outputTitle, result);
                lastOutput = result.errorLog;
                return result.status == "Download finished.";
            };

            bool success = runOnce(activeCommandBase);
            if (!success && result.status != "Download cancelled." &&
                IsYoutubeFormatUnavailableError(result.errorLog.empty() ? result.status : result.errorLog))
            {
                success = runOnce(activeRelaxedCommandBase);
            }

            // Transient CDN 403 on DASH (often after a partial .fNNN.part) — one forced retry.
            if (!success && result.status != "Download cancelled." &&
                IsYoutubeHttpForbiddenError(result.errorLog.empty() ? result.status : result.errorLog))
            {
                if (browser.empty())
                {
                    noCookiesHad403 = true;
                }
                rememberFailure(result.status, result.errorLog.empty() ? result.status : result.errorLog);
                SetSharedStatus(sharedState, "Retrying after HTTP 403...", 0.05f);
                RemoveDownloadArtifacts(request.outputDirectory, outputTitle, request.fileFormat);
                forceOverwrite = true;
                success = runOnce(buildCommandBase(formatSelector, true));
                if (!success && result.status != "Download cancelled." &&
                    IsYoutubeFormatUnavailableError(result.errorLog.empty() ? result.status : result.errorLog))
                {
                    success = runOnce(buildCommandBase(relaxedFormatSelector, true));
                }
                if (!success && browser.empty() &&
                    IsYoutubeHttpForbiddenError(result.errorLog.empty() ? result.status : result.errorLog))
                {
                    noCookiesHad403 = true;
                }
            }

            // Same requested quality via visionos HLS when https DASH CDN keeps 403'ing (no lower-res fallback).
            const bool wantVisionOsHls = request.mediaMode != "Audio only" && !IsAudioOnlyExtension(ext);
            if (!success && !visionOsHlsTried && wantVisionOsHls && result.status != "Download cancelled." &&
                (noCookiesHad403 ||
                 IsYoutubeHttpForbiddenError(result.errorLog.empty() ? result.status : result.errorLog)))
            {
                visionOsHlsTried = true;
                rememberFailure(result.status, result.errorLog.empty() ? result.status : result.errorLog);
                SetSharedStatus(sharedState, "Retrying via HLS (same quality)...", 0.05f);
                RemoveDownloadArtifacts(request.outputDirectory, outputTitle, request.fileFormat);
                forceOverwrite = true;
                const std::string visionArgs = BuildYoutubeVisionOsJsRuntimeArgs();
                success = runVisionOsOnce(buildCommandBase(hlsFormatSelector, true, visionArgs));
                if (success && !request.liveFromStart && OutputBelowRequestedQuality(request, outputTitle))
                {
                    if (OutputWithinCapAsMaxAcceptance(request, outputTitle))
                    {
                        success = true;
                    }
                    else
                    {
                        success = false;
                        result.exitCode = 1;
                        rememberFailure("Download failed: requested quality unavailable (got a lower resolution).",
                                        "HLS fallback finished below requested quality.");
                        RemoveDownloadArtifacts(request.outputDirectory, outputTitle, request.fileFormat);
                        DiscardUndersizedDownloadOutput(request, outputTitle);
                        result.status = "Download failed: requested quality unavailable (got a lower resolution).";
                        result.errorLog = "HLS fallback finished below requested quality.";
                        lastOutput = result.errorLog;
                    }
                }

                BrowserAttempt hlsAttempt;
                hlsAttempt.browserSpec = "visionos HLS (no cookies)";
                hlsAttempt.success = success;
                hlsAttempt.summary = success ? "Downloaded requested quality via visionos HLS."
                                             : SummarizeBrowserAttemptOutput(
                                                   result.errorLog.empty() ? result.status : result.errorLog, false);
                hlsAttempt.nextAction = success ? "Used HLS after DASH HTTP 403."
                                                : "HLS same-quality retry failed; continuing browser options if any.";
                downloadLog.AddAttempt(hlsAttempt);

                if (success)
                {
                    successfulBrowser = browser;
                    RemoveDownloadArtifacts(request.outputDirectory, outputTitle, request.fileFormat);
                    break;
                }
            }

            if (success && BothOutputMissingAudio(request, outputTitle))
            {
                if (!forceOverwrite)
                {
                    SetSharedStatus(sharedState, "Re-downloading (no audio in file)...", 0.05f);
                    forceOverwrite = true;
                    success = runOnce(buildCommandBase(formatSelector, true));
                    if (!success && result.status != "Download cancelled." &&
                        IsYoutubeFormatUnavailableError(result.errorLog.empty() ? result.status : result.errorLog))
                    {
                        success = runOnce(buildCommandBase(relaxedFormatSelector, true));
                    }
                }
                if (success && BothOutputMissingAudio(request, outputTitle))
                {
                    success = false;
                    result.exitCode = 1;
                    result.status = "Download failed: output has no audio.";
                    result.errorLog =
                        "Both mode produced a file without an audio stream.\nOutput: " +
                        PathUtf8(FindDownloadOutputFile(request.outputDirectory, outputTitle, request.fileFormat));
                    lastOutput = result.errorLog;
                }
            }

            if (success && !request.liveFromStart && OutputBelowRequestedQuality(request, outputTitle))
            {
                if (OutputWithinCapAsMaxAcceptance(request, outputTitle))
                {
                    // Cap-as-max: best available under the group cap is acceptable.
                }
                else
                {
                    size_t noCookieIndex = browsersToTry.size();
                    for (size_t j = 0; j < browsersToTry.size(); ++j)
                    {
                        if (browsersToTry[j].empty())
                        {
                            noCookieIndex = j;
                            break;
                        }
                    }

                    if (!browser.empty() && !noCookiesAttempted && noCookieIndex < browsersToTry.size())
                    {
                        BrowserAttempt softFail;
                        softFail.browserSpec = browser;
                        softFail.success = false;
                        softFail.summary = "Finished below requested quality; trying fuller DASH without cookies.";
                        softFail.nextAction = "Trying next browser option for requested quality.";
                        downloadLog.AddAttempt(softFail);
                        rememberFailure("Download finished below requested quality.", softFail.summary);

                        RemoveDownloadArtifacts(request.outputDirectory, outputTitle, request.fileFormat);
                        DiscardUndersizedDownloadOutput(request, outputTitle);
                        forceOverwrite = true;
                        browserIndex = noCookieIndex - 1; // loop ++ lands on no-cookies
                        continue;
                    }

                    BrowserAttempt softFail;
                    softFail.browserSpec = browser;
                    softFail.success = false;
                    softFail.summary = "Finished below requested quality; requested quality unavailable.";
                    softFail.nextAction = "Stopped — requested quality unavailable for this video/session.";
                    downloadLog.AddAttempt(softFail);
                    rememberFailure("Download failed: requested quality unavailable (got a lower resolution).",
                                    softFail.summary);
                    RemoveDownloadArtifacts(request.outputDirectory, outputTitle, request.fileFormat);
                    DiscardUndersizedDownloadOutput(request, outputTitle);
                    result.exitCode = 1;
                    result.status = "Download failed: requested quality unavailable (got a lower resolution).";
                    result.errorLog = bestFailureLog.empty() ? softFail.summary : bestFailureLog;
                    lastOutput = result.errorLog;
                    success = false;
                    break;
                }
            }

            BrowserAttempt attempt;
            attempt.browserSpec = browser;
            attempt.success = success;
            attempt.summary =
                success
                    ? "Download completed."
                    : SummarizeBrowserAttemptOutput(result.errorLog.empty() ? result.status : result.errorLog, false);
            attempt.nextAction = DescribeBrowserRetryAction(
                result.errorLog.empty() ? result.status : result.errorLog, hasMoreBrowsers, success);
            downloadLog.AddAttempt(attempt);

            if (success)
            {
                successfulBrowser = browser;
                RemoveDownloadArtifacts(request.outputDirectory, outputTitle, request.fileFormat);
                break;
            }
            if (result.status == "Download cancelled.")
            {
                RemoveDownloadArtifacts(
                    request.outputDirectory, outputTitle, request.fileFormat, request.liveFromStart);
                result.downloadBrowserReport = downloadLog.FormatSection("Download");
                return result;
            }

            rememberFailure(result.status, result.errorLog.empty() ? result.status : result.errorLog);

            // Bare high-res often 403s on CDN; one signed-in fallback is enough — don't walk every cookie DB.
            if (!browser.empty() && noCookiesHad403)
            {
                break;
            }

            if (!browser.empty() && !ShouldRetryYoutubeWithDifferentCookies(result.errorLog) &&
                !IsYoutubeHttpForbiddenError(result.errorLog.empty() ? result.status : result.errorLog))
            {
                break;
            }
        }

        result.downloadBrowserReport = downloadLog.FormatSection("Download");

        if (result.status == "Download finished.")
        {
            RemoveDownloadArtifacts(request.outputDirectory, outputTitle, request.fileFormat);
            downloadLog.SetWinner(successfulBrowser);
            result.downloadBrowserReport = downloadLog.FormatSection("Download");
            SetPreferredYoutubeCookieBrowser(successfulBrowser);
            return result;
        }

        if (result.status == "Download cancelled.")
        {
            RemoveDownloadArtifacts(request.outputDirectory, outputTitle, request.fileFormat, request.liveFromStart);
        }
        else
        {
            if (!bestFailureStatus.empty())
            {
                result.status = bestFailureStatus.rfind("Download failed", 0) == 0
                                    ? bestFailureStatus
                                    : ("Download failed: " +
                                       SimplifyYtDlpError(bestFailureLog.empty() ? bestFailureStatus : bestFailureLog));
                lastOutput = bestFailureLog.empty() ? bestFailureStatus : bestFailureLog;
            }
            result.errorLog =
                BuildDownloadErrorLog(request, formatSelector, static_cast<int>(result.exitCode), lastOutput);
        }
        return result;
    }
    catch (const std::exception& exception)
    {
        result.status = std::string("Download failed: ") + exception.what();
        result.errorLog = exception.what();
        return result;
    }
    catch (...)
    {
        result.status = "Download failed: unexpected error.";
        result.errorLog = result.status;
        return result;
    }
}

void DownloadRunner::SetSharedStatus(const std::shared_ptr<DownloadSharedState>& sharedState,
                                     const std::string& status,
                                     float progress,
                                     DownloadSharedState::Phase phase)
{
    if (sharedState == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(sharedState->mutex);
    sharedState->status = status;
    const float clamped = std::clamp(progress, 0.0f, 1.0f);
    if (phase == DownloadSharedState::Phase::Downloading)
    {
        sharedState->ytProgress = std::max(sharedState->ytProgress, clamped);
    }
    if (sharedState->phase != phase)
    {
        sharedState->phase = phase;
        sharedState->progress = clamped;
        return;
    }

    sharedState->progress = std::max(sharedState->progress, clamped);
}

void DownloadRunner::SetSharedDiskProgress(const std::shared_ptr<DownloadSharedState>& sharedState, float diskProgress)
{
    if (sharedState == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(sharedState->mutex);
    sharedState->diskProgress = std::clamp(diskProgress, 0.0f, 1.0f);
}

void DownloadRunner::SetSharedDiskBytes(const std::shared_ptr<DownloadSharedState>& sharedState,
                                        std::uint64_t diskBytes)
{
    if (sharedState == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(sharedState->mutex);
    sharedState->diskBytes = diskBytes;
}

void DownloadRunner::SetSharedEstimatedBytes(const std::shared_ptr<DownloadSharedState>& sharedState,
                                             std::int64_t estimatedBytes)
{
    if (sharedState == nullptr || estimatedBytes <= 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(sharedState->mutex);
    sharedState->estimatedBytes = std::max(sharedState->estimatedBytes, estimatedBytes);
}

void DownloadRunner::SetSharedYtDlpSpeed(const std::shared_ptr<DownloadSharedState>& sharedState, double ytDlpSpeedBps)
{
    if (sharedState == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(sharedState->mutex);
    sharedState->ytDlpSpeedBps = ytDlpSpeedBps >= 0.0 ? ytDlpSpeedBps : -1.0;
}

std::string DownloadRunner::Quote(const std::string& value)
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
