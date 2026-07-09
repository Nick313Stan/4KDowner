#include "DownloadRunner.h"

#include "BrowserDiagnostics.h"
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
#include <future>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
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

    std::filesystem::path FindFromCurrentPath(const std::filesystem::path &relativePath)
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
            const std::filesystem::path ffmpeg = FindFromCurrentPath(relativePath);
            if (!ffmpeg.empty())
            {
                return ffmpeg.parent_path();
            }
        }

        const std::filesystem::path pathFromEnvironment = FindExecutableInPath("ffmpeg.exe");
        return pathFromEnvironment.empty() ? std::filesystem::path{} : pathFromEnvironment.parent_path();
    }

    std::string ToLower(std::string value)
    {
        for (char &c : value)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return value;
    }

    std::string QualityFilter(const std::string &quality)
    {
        if (quality.empty())
        {
            return "[height>=360]";
        }

        std::string digits;
        for (const char c : quality)
        {
            if (c >= '0' && c <= '9')
            {
                digits.push_back(c);
            }
        }

        return digits.empty() ? "[height>=360]" : "[height>=360][height<=" + digits + "]";
    }

    bool IsAudioOnlyExtension(const std::string &extension)
    {
        return extension == "m4a" || extension == "mp3" || extension == "opus" || extension == "wav" || extension == "flac" || extension == "aac";
    }

    std::string BuildFormatSelector(const DownloadRequest &request)
    {
        const std::string ext = ToLower(request.fileFormat);
        const std::string height = QualityFilter(request.quality);
        const std::string skipUpscaledMp4 = "[format_id!*=sr]";

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

        if (request.mediaMode == "Video only")
        {
            if (ext == "mp4")
            {
                return "bestvideo[ext=mp4]" + skipUpscaledMp4 + height + "/bestvideo" + skipUpscaledMp4 + height + "/bestvideo";
            }
            return "bestvideo[ext=" + ext + "]" + height + "/bestvideo" + height + "/bestvideo";
        }

        if (ext == "mp4")
        {
            return "bestvideo[ext=mp4]" + skipUpscaledMp4 + height + "+bestaudio[ext=m4a]/best[ext=mp4]" + skipUpscaledMp4 + height + "/best" + height + "/best";
        }

        if (ext == "webm")
        {
            return "bestvideo[ext=webm]" + height + "+bestaudio[ext=webm]/best[ext=webm]" + height + "/best" + height + "/best";
        }

        return "bestvideo" + height + "+bestaudio/best" + height + "/best";
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

    std::string BuildDownloadFailureStatus(const std::string& lastLine, const std::string& fullOutput, DWORD exitCode)
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

    std::string BuildDownloadErrorLog(
        const DownloadRequest& request,
        const std::string& formatSelector,
        DWORD exitCode,
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

    bool ParseDownloadProgressAt(const std::string &text, size_t percentIndex, float &progress)
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

    bool ParseDownloadProgress(const std::string &text, float &progress)
    {
        const size_t percent = text.find('%');
        return ParseDownloadProgressAt(text, percent, progress);
    }

    bool ParseLastDownloadProgress(const std::string &text, float &progress)
    {
        const size_t percent = text.rfind('%');
        return ParseDownloadProgressAt(text, percent, progress);
    }

    int CountDownloadStreams(const std::string &mediaMode)
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

        float UpdateLine(const std::string &line)
        {
            if (line.find("[Merger]") != std::string::npos ||
                line.find("Merging formats") != std::string::npos ||
                (line.find("[ffmpeg]") != std::string::npos && line.find("Merging") != std::string::npos))
            {
                overall_ = std::max(overall_, 0.93f);
                return overall_;
            }

            if (line.find("[ExtractAudio]") != std::string::npos ||
                line.find("Post-process") != std::string::npos)
            {
                overall_ = std::max(overall_, 0.97f);
                return overall_;
            }

            if (line.find("[download]") != std::string::npos && line.find("Destination:") != std::string::npos)
            {
                if (sawDestination_)
                {
                    streamIndex_ = std::min(streamIndex_ + 1, streamCount_ - 1);
                    lastStreamProgress_ = 0.0f;
                }
                sawDestination_ = true;
                overall_ = std::max(overall_, StreamBase() + 0.02f);
                return overall_;
            }

            float streamProgress = 0.0f;
            if (!ParseDownloadProgress(line, streamProgress))
            {
                return overall_;
            }

            if (streamProgress + 0.15f < lastStreamProgress_ && streamIndex_ < streamCount_ - 1)
            {
                streamIndex_++;
                lastStreamProgress_ = 0.0f;
            }
            lastStreamProgress_ = std::max(lastStreamProgress_, streamProgress);

            const float mapped = MapStreamProgress(streamProgress);
            overall_ = std::max(overall_, mapped);
            return overall_;
        }

        float UpdateChunk(const std::string &pendingText)
        {
            float streamProgress = 0.0f;
            if (ParseLastDownloadProgress(pendingText, streamProgress))
            {
                if (streamProgress + 0.15f < lastStreamProgress_ && streamIndex_ < streamCount_ - 1)
                {
                    streamIndex_++;
                    lastStreamProgress_ = 0.0f;
                }
                lastStreamProgress_ = std::max(lastStreamProgress_, streamProgress);
                overall_ = std::max(overall_, MapStreamProgress(streamProgress));
            }
            return overall_;
        }

        float Overall() const
        {
            return overall_;
        }

    private:
        float StreamBase() const
        {
            if (streamCount_ <= 1)
            {
                return 0.04f;
            }

            return 0.04f + (static_cast<float>(streamIndex_) / static_cast<float>(streamCount_)) * 0.86f;
        }

        float MapStreamProgress(float streamProgress) const
        {
            const float streamShare = streamCount_ == 1 ? 0.88f : 0.41f;
            return std::min(StreamBase() + streamProgress * streamShare, 0.92f);
        }

        int streamCount_;
        int streamIndex_ = 0;
        bool sawDestination_ = false;
        float lastStreamProgress_ = 0.0f;
        float overall_ = 0.04f;
    };

    std::string BuildProgressStatus(const std::string &line, float progress)
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

#ifdef _WIN32
    void ApplyDownloadProgress(
        const std::shared_ptr<DownloadSharedState> &sharedState,
        DownloadProgressTracker &tracker,
        const std::string &line,
        std::string &lastMeaningfulLine)
    {
        if (line.empty())
        {
            return;
        }

        lastMeaningfulLine = line;
        const float overall = tracker.UpdateLine(line);
        if (line.find("ERROR:") != std::string::npos)
        {
            DownloadRunner::SetSharedStatus(sharedState, line, tracker.Overall());
            return;
        }

        float streamProgress = 0.0f;
        if (ParseDownloadProgress(line, streamProgress))
        {
            DownloadRunner::SetSharedStatus(sharedState, BuildProgressStatus(line, streamProgress), overall);
        }
        else if (line.find("[Merger]") != std::string::npos || line.find("Merging") != std::string::npos)
        {
            DownloadRunner::SetSharedStatus(sharedState, "Merging audio and video...", overall);
        }
        else if (line.find("[download]") != std::string::npos)
        {
            DownloadRunner::SetSharedStatus(
                sharedState,
                "Downloading " + std::to_string(static_cast<int>(overall * 100.0f)) + "%",
                overall);
        }
    }

    DownloadRunResult RunProcess(
        std::string command,
        const std::shared_ptr<std::atomic_bool> &cancelRequested,
        const std::shared_ptr<DownloadSharedState> &sharedState,
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
        const BOOL started = CreateProcessW(
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
            result.status = "Download failed: could not start yt-dlp.";
            return result;
        }

        DWORD exitCode = STILL_ACTIVE;
        std::string pendingText;
        std::string fullOutput;
        std::string lastMeaningfulLine;
        DownloadProgressTracker tracker(downloadStreams);
        DownloadRunner::SetSharedStatus(sharedState, "Downloading 4%", tracker.Overall());
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

                const float previousOverall = tracker.Overall();
                tracker.UpdateChunk(pendingText);
                if (tracker.Overall() > previousOverall + 0.001f)
                {
                    DownloadRunner::SetSharedStatus(
                        sharedState,
                        "Downloading " + std::to_string(static_cast<int>(tracker.Overall() * 100.0f)) + "%",
                        tracker.Overall());
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

            const float previousOverall = tracker.Overall();
            tracker.UpdateChunk(pendingText);
            if (tracker.Overall() > previousOverall + 0.001f)
            {
                DownloadRunner::SetSharedStatus(
                    sharedState,
                    "Downloading " + std::to_string(static_cast<int>(tracker.Overall() * 100.0f)) + "%",
                    tracker.Overall());
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

void DownloadRunner::Start(DownloadRequest request)
{
    if (isRunning_)
    {
        return;
    }

    FinishFuture(future_);

    status_ = "Downloading...";
    progress_ = 0.04f;
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
    SetSharedStatus(sharedState_, status_, progress_);

    try
    {
        future_ = std::async(std::launch::async, [request = std::move(request), cancelRequested = cancelRequested_, sharedState = sharedState_]
                             { return Run(request, cancelRequested, sharedState); });
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
        SetSharedStatus(sharedState_, status_, progress_);
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
        try
        {
            const DownloadRunResult result = future_.get();
            status_ = result.status;
            lastErrorLog_ = result.errorLog;
            lastDownloadBrowserReport_ = result.downloadBrowserReport;
        }
        catch (const std::exception &exception)
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
    }
    elapsedSeconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
}

bool DownloadRunner::IsRunning() const
{
    return isRunning_;
}

const std::string &DownloadRunner::Status() const
{
    return status_;
}

const std::string &DownloadRunner::CurrentUrl() const
{
    return currentUrl_;
}

float DownloadRunner::Progress() const
{
    return progress_;
}

double DownloadRunner::ElapsedSeconds() const
{
    return elapsedSeconds_;
}

bool DownloadRunner::ConsumeCompletedDownload(std::string &url, double &elapsedSeconds)
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

DownloadRunResult DownloadRunner::Run(
    DownloadRequest request,
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

        const std::filesystem::path ffmpegDirectory = FindFfmpegDirectory();
        const std::string ext = ToLower(request.fileFormat);
        const std::string formatSelector = BuildFormatSelector(request);
        const std::string outputTitle = request.normalizedTitle.empty() ? NormalizeVideoTitle(request.title) : request.normalizedTitle;
        std::string commandBase = ytDlpInvocation +
                                  BuildYoutubeJsRuntimeArgs() +
                                  " --no-playlist --no-warnings --restrict-filenames -P " + Quote(request.outputDirectory) +
                                  " -o " + Quote(outputTitle + ".%(ext)s") + " -f " + Quote(formatSelector);
        commandBase += request.overwriteExisting ? " --force-overwrites" : " --no-overwrites";

        if (!ffmpegDirectory.empty())
        {
            commandBase += " --ffmpeg-location " + Quote(PathUtf8(ffmpegDirectory));
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

        const std::vector<std::string> browsersToTry = BuildYoutubeCookieBrowsersToTryList();
        BrowserAttemptLog downloadLog;
        std::string lastOutput;
        std::string successfulBrowser;
        for (size_t browserIndex = 0; browserIndex < browsersToTry.size(); ++browserIndex)
        {
            const std::string& browser = browsersToTry[browserIndex];
            const bool hasMoreBrowsers = browserIndex + 1 < browsersToTry.size();
            std::string command = commandBase + BuildYoutubeCookiesArgs(browser) +
                                  " " + Quote(NormalizeYoutubeUrl(request.url)) + " 2>&1";

#ifdef _WIN32
            command = "cmd /S /C \"chcp 65001>nul && set PYTHONIOENCODING=utf-8 && set PYTHONUNBUFFERED=1 && " + command + "\"";
            result = RunProcess(command, cancelRequested, sharedState, CountDownloadStreams(request.mediaMode));
#else
            FILE *pipe = POPEN(command.c_str(), "r");
            if (pipe == nullptr)
            {
                result.status = "Download failed: could not start yt-dlp.";
                result.errorLog = result.status;
                return result;
            }

            std::string output;
            std::array<char, 512> buffer{};
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            {
                output += buffer.data();
            }

            const int exitCode = PCLOSE(pipe);
            result.exitCode = exitCode;
            if (exitCode != 0)
            {
                result.status = BuildDownloadFailureStatus(ExtractLastOutputLine(output), output, static_cast<DWORD>(exitCode));
                result.errorLog = output;
            }
            else
            {
                result.status = "Download finished.";
                result.errorLog.clear();
            }
#endif
            lastOutput = result.errorLog;
            const bool success = result.status == "Download finished.";

            BrowserAttempt attempt;
            attempt.browserSpec = browser;
            attempt.success = success;
            attempt.summary = success ? "Download completed." : SummarizeBrowserAttemptOutput(
                result.errorLog.empty() ? result.status : result.errorLog,
                false);
            attempt.nextAction = DescribeBrowserRetryAction(
                result.errorLog.empty() ? result.status : result.errorLog,
                hasMoreBrowsers,
                success);
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
            result.errorLog = BuildDownloadErrorLog(
                request,
                formatSelector,
                static_cast<DWORD>(result.exitCode),
                lastOutput);
        }
        return result;
    }
    catch (const std::exception &exception)
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

void DownloadRunner::SetSharedStatus(const std::shared_ptr<DownloadSharedState> &sharedState, const std::string &status, float progress)
{
    if (sharedState == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(sharedState->mutex);
    sharedState->status = status;
    sharedState->progress = std::max(sharedState->progress, progress);
}

std::string DownloadRunner::Quote(const std::string &value)
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
