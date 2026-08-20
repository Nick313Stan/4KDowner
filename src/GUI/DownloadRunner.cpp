#include "DownloadRunner.h"

#include "BrowserDiagnostics.h"
#include "ToolPaths.h"
#include "VideoTitle.h"
#include "WinProcess.h"
#include "YtDlpLocator.h"
#include "YtDlpYouTube.h"

#include <algorithm>
#include <array>
#include <chrono>
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

    // Cap semantics: best available at or below the selected height.
    return "[height>=144][height<=" + std::to_string(height) + "]";
}

bool IsAudioOnlyExtension(const std::string& extension)
{
    return extension == "m4a" || extension == "mp3" || extension == "opus" || extension == "wav" ||
           extension == "flac" || extension == "aac";
}

std::string BuildFormatSelector(const DownloadRequest& request)
{
    const std::string ext = ToLower(request.fileFormat);
    const std::string height = QualityFilter(request.quality);
    const std::string skipUpscaled = "[format_id!*=sr]";

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
            return "bestvideo" + height + "[ext=mp4]" + skipUpscaled + "/bestvideo" + height + skipUpscaled +
                   "/bestvideo" + height;
        }
        return "bestvideo" + height + "[ext=" + ext + "]" + "/bestvideo" + height;
    }

    if (ext == "mp4")
    {
        return "bestvideo" + height + "[ext=mp4]" + skipUpscaled + "+bestaudio[ext=m4a]" + "/bestvideo" + height +
               skipUpscaled + "+bestaudio" + "/bestvideo" + height + "+bestaudio";
    }

    if (ext == "webm")
    {
        return "bestvideo" + height + "[ext=webm]+bestaudio[ext=webm]" + "/bestvideo" + height +
               "[ext=webm]+bestaudio" + "/bestvideo" + height + "+bestaudio";
    }

    return "bestvideo" + height + "+bestaudio";
}

std::string BuildRelaxedFormatSelector(const DownloadRequest& request)
{
    // Loosen container/codec constraints, but NEVER drop the quality cap — otherwise a
    // "format not available" retry silently upgrades 360p to best/1080p+.
    const std::string ext = ToLower(request.fileFormat);
    const std::string height = QualityFilter(request.quality);
    if (request.mediaMode == "Audio only" || IsAudioOnlyExtension(ext))
    {
        return "bestaudio/best";
    }
    if (request.mediaMode == "Video only")
    {
        return "bestvideo" + height + "/bestvideo" + height + "/best" + height;
    }
    return "bestvideo" + height + "+bestaudio/bestvideo" + height + "+bestaudio/best" + height;
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

std::string ExtractLastOutputLine(const std::string& output)
{
    size_t end = output.size();
    while (end > 0 && (output[end - 1] == '\r' || output[end - 1] == '\n' || output[end - 1] == ' '))
    {
        --end;
    }
    size_t start = end;
    while (start > 0 && output[start - 1] != '\r' && output[start - 1] != '\n')
    {
        --start;
    }
    return TrimLine(output.substr(start, end - start));
}

std::string BuildDownloadFailureStatus(const std::string& lastLine, const std::string& fullOutput, int exitCode)
{
    std::string message = lastLine;
    if (message.empty() && !fullOutput.empty())
    {
        message = SimplifyYtDlpError(fullOutput);
    }
    if (message.empty())
    {
        message = ExtractLastOutputLine(fullOutput);
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
    stream << "Format: " << request.fileFormat << " | " << request.mediaMode << " | " << request.quality << "\n";
    stream << "Selector: " << formatSelector << "\n";
    stream << "Exit code: " << exitCode << "\n";
    if (!fullOutput.empty())
    {
        stream << "\n--- yt-dlp output ---\n" << fullOutput;
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

int CountDownloadStreams(const std::string& mediaMode)
{
    return mediaMode == "Both" ? 2 : 1;
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
    }

    void AdvanceMergeProgress()
    {
        if (phase_ != DownloadSharedState::Phase::Merging)
        {
            return;
        }

        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - mergeStartedAt_).count();
        // yt-dlp/ffmpeg rarely report merge %, so advance by elapsed time up to ~95%.
        mergeProgress_ = std::min(0.95f, 0.04f + static_cast<float>(elapsed) * 0.12f);
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
    DownloadSharedState::Phase phase_ = DownloadSharedState::Phase::Downloading;
    std::chrono::steady_clock::time_point mergeStartedAt_{};
};

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
                           std::string& lastMeaningfulLine)
{
    if (line.empty())
    {
        return;
    }

    lastMeaningfulLine = line;
    const float phaseProgress = tracker.UpdateLine(line);
    const DownloadSharedState::Phase phase = tracker.Phase();
    if (line.find("ERROR:") != std::string::npos)
    {
        DownloadRunner::SetSharedStatus(sharedState, line, tracker.PhaseProgress(), phase);
        return;
    }

    float streamProgress = 0.0f;
    if (phase == DownloadSharedState::Phase::Merging)
    {
        DownloadRunner::SetSharedStatus(sharedState,
                                        "Merging " + std::to_string(static_cast<int>(phaseProgress * 100.0f)) + "%",
                                        phaseProgress,
                                        phase);
    }
    else if (ParseDownloadProgress(line, streamProgress))
    {
        DownloadRunner::SetSharedStatus(sharedState, BuildProgressStatus(line, streamProgress), phaseProgress, phase);
    }
    else if (line.find("[download]") != std::string::npos)
    {
        DownloadRunner::SetSharedStatus(sharedState,
                                        "Downloading " + std::to_string(static_cast<int>(phaseProgress * 100.0f)) + "%",
                                        phaseProgress,
                                        phase);
    }
}

void IngestDownloadOutputChunk(const std::shared_ptr<DownloadSharedState>& sharedState,
                               DownloadProgressTracker& tracker,
                               std::string& pendingText,
                               std::string& fullOutput,
                               std::string& lastMeaningfulLine,
                               const char* data,
                               size_t size)
{
    pendingText.append(data, size);
    fullOutput.append(data, size);
    size_t separator = pendingText.find_first_of("\r\n");
    while (separator != std::string::npos)
    {
        std::string line = TrimLine(pendingText.substr(0, separator));
        pendingText.erase(0, separator + 1);
        ApplyDownloadProgress(sharedState, tracker, line, lastMeaningfulLine);
        separator = pendingText.find_first_of("\r\n");
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
                             int downloadStreams)
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
                ApplyDownloadProgress(sharedState, tracker, line, lastMeaningfulLine);
                separator = pendingText.find_first_of("\r\n");
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

    while (WaitForSingleObject(processInfo.hProcess, 50) == WAIT_TIMEOUT)
    {
        consumeOutput();
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
            ApplyDownloadProgress(sharedState, tracker, line, lastMeaningfulLine);
            separator = pendingText.find_first_of("\r\n");
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
            ApplyDownloadProgress(sharedState, tracker, tailLine, lastMeaningfulLine);
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
    result.errorLog = fullOutput;
    result.exitCode = static_cast<int>(exitCode);
    return result;
}
#else
DownloadRunResult RunProcess(std::string command,
                             const std::shared_ptr<std::atomic_bool>& cancelRequested,
                             const std::shared_ptr<DownloadSharedState>& sharedState,
                             int downloadStreams)
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
                                              static_cast<size_t>(bytesRead));
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
                                      static_cast<size_t>(bytesRead));
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
            ApplyDownloadProgress(sharedState, tracker, tailLine, lastMeaningfulLine);
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
    result.errorLog = fullOutput;
    result.exitCode = exitCode;
    return result;
}
#endif
} // namespace

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
    phase_ = DownloadSharedState::Phase::Downloading;
    elapsedSeconds_ = 0.0;
    currentUrl_ = request.url;
    lastErrorLog_.clear();
    lastDownloadBrowserReport_.clear();
    completedUrl_.clear();
    completedElapsedSeconds_ = 0.0;
    hasCompletedDownload_ = false;
    startedAt_ = std::chrono::steady_clock::now();
    cancelRequested_ = std::make_shared<std::atomic_bool>(false);
    sharedState_ = std::make_shared<DownloadSharedState>();
    SetSharedStatus(sharedState_, status_, progress_, phase_);

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
        phase_ = sharedState_->phase;
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

float DownloadRunner::Progress() const
{
    return progress_;
}

DownloadSharedState::Phase DownloadRunner::Phase() const
{
    return phase_;
}

double DownloadRunner::ElapsedSeconds() const
{
    return elapsedSeconds_;
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

        // Pass the native binary path (not the bin/ folder): a shared packages/
        // tree may contain both ffmpeg.exe and a Linux `ffmpeg` side by side.
        const std::filesystem::path ffmpegPath = ::FindFfmpegExecutable();
        const std::string ext = ToLower(request.fileFormat);
        const std::string formatSelector = BuildFormatSelector(request);
        const std::string relaxedFormatSelector = BuildRelaxedFormatSelector(request);
        const std::string outputTitle =
            request.normalizedTitle.empty() ? NormalizeVideoTitle(request.title) : request.normalizedTitle;
        auto buildCommandBase = [&](const std::string& selector) -> std::string
        {
            std::string commandBase = ytDlpInvocation + BuildYoutubeJsRuntimeArgs() +
                                      " --no-playlist --no-warnings -P " + Quote(request.outputDirectory) + " -o " +
                                      Quote(outputTitle + ".%(ext)s") + " -f " + Quote(selector);
            commandBase += request.overwriteExisting ? " --force-overwrites" : " --no-overwrites";

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
        const std::string commandBase = buildCommandBase(formatSelector);
        const std::string relaxedCommandBase = buildCommandBase(relaxedFormatSelector);

        const std::vector<std::string> browsersToTry = BuildYoutubeCookieBrowsersToTryList();
        BrowserAttemptLog downloadLog;
        std::string lastOutput;
        std::string successfulBrowser;
        for (size_t browserIndex = 0; browserIndex < browsersToTry.size(); ++browserIndex)
        {
            const std::string& browser = browsersToTry[browserIndex];
            const bool hasMoreBrowsers = browserIndex + 1 < browsersToTry.size();

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
                result = RunProcess(command, cancelRequested, sharedState, CountDownloadStreams(request.mediaMode));
                lastOutput = result.errorLog;
                return result.status == "Download finished.";
            };

            bool success = runOnce(commandBase);
            if (!success && result.status != "Download cancelled." &&
                IsYoutubeFormatUnavailableError(result.errorLog.empty() ? result.status : result.errorLog))
            {
                // Cookies worked; only the exact quality/container combo was missing.
                success = runOnce(relaxedCommandBase);
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
                break;
            }
            if (result.status == "Download cancelled.")
            {
                result.downloadBrowserReport = downloadLog.FormatSection("Download");
                return result;
            }

            if (!browser.empty() && !ShouldRetryYoutubeWithDifferentCookies(result.errorLog))
            {
                break;
            }
        }

        result.downloadBrowserReport = downloadLog.FormatSection("Download");

        if (result.status == "Download finished.")
        {
            downloadLog.SetWinner(successfulBrowser);
            result.downloadBrowserReport = downloadLog.FormatSection("Download");
            SetPreferredYoutubeCookieBrowser(successfulBrowser);
            return result;
        }

        if (result.status != "Download cancelled.")
        {
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
    if (sharedState->phase != phase)
    {
        sharedState->phase = phase;
        sharedState->progress = std::clamp(progress, 0.0f, 1.0f);
        return;
    }

    sharedState->progress = std::max(sharedState->progress, std::clamp(progress, 0.0f, 1.0f));
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
