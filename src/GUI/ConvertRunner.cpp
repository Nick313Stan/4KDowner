#include "ConvertRunner.h"

#include "ToolPaths.h"
#include "WinProcess.h"

#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define POPEN _popen
#define PCLOSE _pclose
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#define POPEN popen
#define PCLOSE pclose
#endif

namespace
{
std::string PathUtf8(const std::filesystem::path& path)
{
    return path.u8string();
}

std::string QuoteShell(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 2);
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

#ifdef _WIN32
std::wstring QuoteArgument(const std::wstring& value)
{
    if (value.empty())
    {
        return L"\"\"";
    }

    std::wstring escaped = L"\"";
    for (const wchar_t c : value)
    {
        if (c == L'"')
        {
            escaped += L'\\';
        }
        escaped += c;
    }
    escaped += L'"';
    return escaped;
}

std::wstring ToWide(const std::string& value)
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

bool TryDeleteUtf8Path(const std::string& utf8Path)
{
    if (utf8Path.empty())
    {
        return true;
    }
    const std::filesystem::path path = std::filesystem::u8path(utf8Path);
#ifdef _WIN32
    const std::wstring widePath = path.wstring();
    SetFileAttributesW(widePath.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (DeleteFileW(widePath.c_str()))
    {
        return true;
    }
    const DWORD lastError = GetLastError();
    if (lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_PATH_NOT_FOUND)
    {
        return true;
    }
    const std::wstring trashPath = widePath + L".trash";
    if (MoveFileExW(widePath.c_str(), trashPath.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        SetFileAttributesW(trashPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileW(trashPath.c_str()))
        {
            return true;
        }
    }
#else
    std::error_code error;
    std::filesystem::remove(path, error);
#endif
    std::error_code existsError;
    return !std::filesystem::exists(path, existsError);
}

void TryRemovePath(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return;
    }
    TryDeleteUtf8Path(PathUtf8(path));
}

#ifdef _WIN32
int RunProcessCaptureOutput(const std::wstring& commandLine,
                            std::string& lastLine,
                            std::string& fullOutput,
                            const std::shared_ptr<std::atomic_bool>& cancelRequested,
                            const std::function<void(const std::string&)>& onOutput)
{
    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0))
    {
        return -1;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startupInfo{};
    PROCESS_INFORMATION processInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
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
                                        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                                        nullptr,
                                        nullptr,
                                        &startupInfo,
                                        &processInfo);
    CloseHandle(writePipe);

    if (!started)
    {
        CloseHandle(readPipe);
        return -1;
    }

    std::string pendingText;
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
            pendingText.append(buffer.data(), read);
            fullOutput.append(buffer.data(), read);
            if (onOutput)
            {
                onOutput(std::string(buffer.data(), read));
            }
        }
    };

    while (WaitForSingleObject(processInfo.hProcess, 100) == WAIT_TIMEOUT)
    {
        consumeOutput();
        if (cancelRequested != nullptr && cancelRequested->load())
        {
            KillProcessTree(processInfo.dwProcessId);
            WaitForSingleObject(processInfo.hProcess, 3000);
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            CloseHandle(readPipe);
            return -2;
        }
    }
    consumeOutput();

    size_t separator = pendingText.find_last_of("\r\n");
    if (separator != std::string::npos)
    {
        lastLine = pendingText.substr(separator + 1);
        while (!lastLine.empty() && (lastLine.back() == '\r' || lastLine.back() == '\n'))
        {
            lastLine.pop_back();
        }
    }
    else
    {
        lastLine = pendingText;
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    CloseHandle(readPipe);
    return static_cast<int>(exitCode);
}
#else
int RunProcessCaptureOutput(const std::string& commandLine,
                            std::string& lastLine,
                            std::string& fullOutput,
                            const std::shared_ptr<std::atomic_bool>& cancelRequested,
                            const std::function<void(const std::string&)>& onOutput)
{
    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0)
    {
        return -1;
    }

    const pid_t pid = fork();
    if (pid < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
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
        execl("/bin/sh", "sh", "-c", commandLine.c_str(), static_cast<char*>(nullptr));
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
            return -2;
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
                    pendingText.append(buffer.data(), static_cast<size_t>(bytesRead));
                    fullOutput.append(buffer.data(), static_cast<size_t>(bytesRead));
                    if (onOutput)
                    {
                        onOutput(std::string(buffer.data(), static_cast<size_t>(bytesRead)));
                    }
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
            pendingText.append(buffer.data(), static_cast<size_t>(bytesRead));
            fullOutput.append(buffer.data(), static_cast<size_t>(bytesRead));
            if (onOutput)
            {
                onOutput(std::string(buffer.data(), static_cast<size_t>(bytesRead)));
            }
            continue;
        }
        break;
    }
    close(pipefd[0]);

    if (cancelRequested != nullptr && cancelRequested->load())
    {
        return -2;
    }

    size_t separator = pendingText.find_last_of("\r\n");
    if (separator != std::string::npos)
    {
        lastLine = pendingText.substr(separator + 1);
        while (!lastLine.empty() && (lastLine.back() == '\r' || lastLine.back() == '\n'))
        {
            lastLine.pop_back();
        }
    }
    else
    {
        lastLine = pendingText;
    }

    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status))
    {
        return 128 + WTERMSIG(status);
    }
    return status;
}
#endif

bool ParseFfmpegTimeValue(const std::string& text, size_t start, double& seconds)
{
    if (start >= text.size())
    {
        return false;
    }

    unsigned hours = 0;
    unsigned minutes = 0;
    double secs = 0.0;
    const int matched = std::sscanf(text.c_str() + start, "%u:%u:%lf", &hours, &minutes, &secs);
    if (matched < 3)
    {
        return false;
    }

    seconds = static_cast<double>(hours) * 3600.0 + static_cast<double>(minutes) * 60.0 + secs;
    return seconds >= 0.0;
}

void UpdateConvertProgressFromOutput(const std::string& chunk,
                                     double sourceDurationSeconds,
                                     const std::shared_ptr<ConvertSharedState>& sharedState)
{
    if (sharedState == nullptr || sourceDurationSeconds <= 0.0)
    {
        return;
    }

    size_t position = 0;
    while ((position = chunk.find("time=", position)) != std::string::npos)
    {
        double currentSeconds = 0.0;
        if (ParseFfmpegTimeValue(chunk, position + 5, currentSeconds))
        {
            float progress = static_cast<float>(currentSeconds / sourceDurationSeconds);
            if (progress < 0.0f)
            {
                progress = 0.0f;
            }
            if (progress > 1.0f)
            {
                progress = 1.0f;
            }

            std::lock_guard<std::mutex> lock(sharedState->mutex);
            if (progress > sharedState->progress)
            {
                sharedState->progress = progress;
            }
        }
        position += 5;
    }
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

std::filesystem::path FindFfmpegPath()
{
    return ::FindFfmpegExecutable();
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

std::string ContainerExtension(const std::string& container)
{
    std::string value = ToLower(container);
    const size_t space = value.find(' ');
    if (space != std::string::npos)
    {
        value = value.substr(0, space);
    }
    return value == "unknown" || value.empty() ? "mp4" : value;
}

std::string VideoEncoder(const std::string& codec)
{
    if (codec == "H.264")
    {
        return "libx264";
    }
    if (codec == "H.265")
    {
        return "libx265";
    }
    if (codec == "AV1")
    {
        return "libaom-av1";
    }
    if (codec == "VP9")
    {
        return "libvpx-vp9";
    }
    return "copy";
}

std::string AudioEncoder(const std::string& codec)
{
    if (codec == "AAC")
    {
        return "aac";
    }
    if (codec == "MP3")
    {
        return "libmp3lame";
    }
    if (codec == "Opus")
    {
        return "libopus";
    }
    if (codec == "FLAC")
    {
        return "flac";
    }
    return "copy";
}

struct InputStreamCodecs
{
    std::string video; // ffprobe names: h264, hevc, av1, vp9, ...
    std::string audio; // aac, opus, mp3, ...
};

bool VideoCodecFitsContainer(const std::string& codec, const std::string& container)
{
    const std::string c = ToLower(container);
    const std::string v = ToLower(codec);
    if (v.empty())
    {
        return true;
    }
    if (c == "mov")
    {
        return v == "h264" || v == "hevc" || v == "prores" || v == "mjpeg";
    }
    if (c == "webm")
    {
        return v == "vp8" || v == "vp9" || v == "av1";
    }
    if (c == "mp4")
    {
        return v == "h264" || v == "hevc" || v == "av1" || v == "mpeg4";
    }
    // mkv (and unknown): accept common streams
    return true;
}

bool AudioCodecFitsContainer(const std::string& codec, const std::string& container)
{
    const std::string c = ToLower(container);
    const std::string a = ToLower(codec);
    if (a.empty())
    {
        return true;
    }
    if (c == "mov")
    {
        return a == "aac" || a == "alac" || a.rfind("pcm_", 0) == 0;
    }
    if (c == "webm")
    {
        return a == "opus" || a == "vorbis";
    }
    if (c == "mp4")
    {
        return a == "aac" || a == "mp3" || a == "ac3" || a == "eac3";
    }
    return true;
}

std::string NormalizeUiVideoCodec(const std::string& codec)
{
    if (codec == "H.264" || codec == "H.265" || codec == "AV1" || codec == "VP9")
    {
        return codec;
    }
    const std::string lower = ToLower(codec);
    if (lower == "h264" || lower == "avc1")
    {
        return "H.264";
    }
    if (lower == "hevc" || lower == "h265")
    {
        return "H.265";
    }
    if (lower == "av1")
    {
        return "AV1";
    }
    if (lower == "vp9" || lower == "vp8")
    {
        return "VP9";
    }
    return "H.264";
}

std::string NormalizeUiAudioCodec(const std::string& codec)
{
    if (codec == "AAC" || codec == "MP3" || codec == "Opus" || codec == "FLAC")
    {
        return codec;
    }
    const std::string lower = ToLower(codec);
    if (lower == "aac")
    {
        return "AAC";
    }
    if (lower == "mp3")
    {
        return "MP3";
    }
    if (lower == "opus")
    {
        return "Opus";
    }
    if (lower == "flac")
    {
        return "FLAC";
    }
    return "AAC";
}

std::string DefaultUiVideoForContainer(const std::string& container)
{
    return ToLower(container) == "webm" ? "VP9" : "H.264";
}

std::string DefaultUiAudioForContainer(const std::string& container)
{
    return ToLower(container) == "webm" ? "Opus" : "AAC";
}

InputStreamCodecs ProbeInputCodecs(const std::filesystem::path& input)
{
    InputStreamCodecs codecs;
    const std::filesystem::path ffprobe = FindFfprobeExecutable();
    if (ffprobe.empty() || input.empty())
    {
        return codecs;
    }

    std::string lastLine;
    std::string fullOutput;
#ifdef _WIN32
    std::wstring commandLine = QuoteArgument(ffprobe.wstring()) +
                               L" -v error -show_entries stream=codec_type,codec_name -of csv=p=0 " +
                               QuoteArgument(input.wstring());
    RunProcessCaptureOutput(commandLine, lastLine, fullOutput, nullptr, nullptr);
#else
    std::string command = QuoteShell(PathUtf8(ffprobe)) +
                          " -v error -show_entries stream=codec_type,codec_name -of csv=p=0 " +
                          QuoteShell(PathUtf8(input)) + " 2>&1";
    RunProcessCaptureOutput(command, lastLine, fullOutput, nullptr, nullptr);
#endif

    std::stringstream stream(fullOutput);
    std::string line;
    while (std::getline(stream, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        {
            line.pop_back();
        }
        // csv: codec_type,codec_name  OR codec_name,codec_type depending on show_entries order
        const size_t comma = line.find(',');
        if (comma == std::string::npos)
        {
            continue;
        }
        const std::string left = ToLower(line.substr(0, comma));
        const std::string right = ToLower(line.substr(comma + 1));
        if (left == "video")
        {
            codecs.video = right;
        }
        else if (left == "audio")
        {
            codecs.audio = right;
        }
        else if (right == "video")
        {
            codecs.video = left;
        }
        else if (right == "audio")
        {
            codecs.audio = left;
        }
    }
    return codecs;
}

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

void ConvertRunner::Start(ConvertRequest request)
{
    if (isRunning_)
    {
        return;
    }

    FinishFuture(future_);

    status_ = "Converting...";
    progress_ = 0.0f;
    lastErrorLog_.clear();
    currentInputPath_ = request.inputPath;
    activeRequest_ = request;
    sourceDurationSeconds_ = request.sourceDurationSeconds;
    startedAt_ = std::chrono::steady_clock::now();
    isRunning_ = true;
    cancelRequested_ = std::make_shared<std::atomic_bool>(false);
    sharedState_ = std::make_shared<ConvertSharedState>();
    future_ = std::async(std::launch::async,
                         [request = std::move(request), cancelRequested = cancelRequested_, sharedState = sharedState_]
                         {
                             return Run(request, cancelRequested, sharedState);
                         });
}

void ConvertRunner::Cancel()
{
    if (!isRunning_)
    {
        return;
    }

    status_ = "Cancelling...";
    if (cancelRequested_ != nullptr)
    {
        cancelRequested_->store(true);
    }
}

void ConvertRunner::Shutdown()
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

void ConvertRunner::Update()
{
    if (!future_.valid())
    {
        if (isRunning_)
        {
            isRunning_ = false;
        }
        return;
    }

    if (isRunning_ && sharedState_ != nullptr)
    {
        std::lock_guard<std::mutex> lock(sharedState_->mutex);
        progress_ = sharedState_->progress;
    }
    if (isRunning_ && progress_ < 0.02f && sourceDurationSeconds_ <= 0.0)
    {
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
        progress_ = std::min(0.85f, 0.05f + static_cast<float>(std::fmod(elapsed * 0.08, 0.8)));
    }

    if (future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            const ConvertRunResult result = future_.get();
            status_ = result.status;
            lastErrorLog_ = result.errorLog;
        }
        catch (const std::exception& exception)
        {
            status_ = std::string("Convert failed: ") + exception.what();
            lastErrorLog_ = exception.what();
        }
        catch (...)
        {
            status_ = "Convert failed: unexpected error.";
            lastErrorLog_.clear();
        }
        progress_ = status_ == "Convert finished." ? 1.0f : progress_;
        elapsedSeconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
        if (status_ == "Convert finished.")
        {
            completedInputPath_ = currentInputPath_;
            completedOutputPath_ = PathUtf8(GetOutputPath(activeRequest_));
            completedElapsedSeconds_ = elapsedSeconds_;
            completedLinkCardUrl_ = activeRequest_.linkCardUrl;
            completedDeleteInputOnSuccess_ = activeRequest_.deleteInputOnSuccess;
            hasCompletedConvert_ = true;
        }
        isRunning_ = false;
        cancelRequested_.reset();
        sharedState_.reset();
        // Keep currentInputPath_ until DockArea finishes handling this status
        // (cancel/fail need the path; Start() overwrites it on the next job).
        sourceDurationSeconds_ = 0.0;
        return;
    }

    elapsedSeconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
}

bool ConvertRunner::IsRunning() const
{
    return isRunning_;
}

const std::string& ConvertRunner::Status() const
{
    return status_;
}

void ConvertRunner::SetStatus(std::string status)
{
    status_ = std::move(status);
}

const std::string& ConvertRunner::LastErrorLog() const
{
    return lastErrorLog_;
}

const std::string& ConvertRunner::CurrentInputPath() const
{
    return currentInputPath_;
}

float ConvertRunner::Progress() const
{
    return progress_;
}

double ConvertRunner::ElapsedSeconds() const
{
    return elapsedSeconds_;
}

double ConvertRunner::SourceDurationSeconds() const
{
    return sourceDurationSeconds_;
}

bool ConvertRunner::ConsumeCompletedConvert(std::string& inputPath,
                                            std::string& outputPath,
                                            double& elapsedSeconds,
                                            std::string& linkCardUrl,
                                            bool& deleteInputOnSuccess)
{
    if (!hasCompletedConvert_)
    {
        return false;
    }

    inputPath = completedInputPath_;
    outputPath = completedOutputPath_;
    elapsedSeconds = completedElapsedSeconds_;
    linkCardUrl = completedLinkCardUrl_;
    deleteInputOnSuccess = completedDeleteInputOnSuccess_;
    hasCompletedConvert_ = false;
    completedLinkCardUrl_.clear();
    completedDeleteInputOnSuccess_ = false;
    return true;
}

std::filesystem::path ConvertRunner::GetOutputPath(const ConvertRequest& request)
{
    const std::filesystem::path input = std::filesystem::u8path(request.inputPath);
    if (input.empty())
    {
        return {};
    }

    std::string ext = input.extension().string();
    if (!ext.empty() && ext.front() == '.')
    {
        ext = ext.substr(1);
    }
    for (char& c : ext)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (request.convertContainer)
    {
        ext = ContainerExtension(request.container);
    }
    else if (request.convertVideo)
    {
        // H.264/H.265/AV1 cannot reliably live in WEBM — remux to MP4.
        if ((request.videoCodec == "H.264" || request.videoCodec == "H.265" || request.videoCodec == "AV1") &&
            ext == "webm")
        {
            ext = "mp4";
        }
        if (request.videoCodec == "VP9" && (ext == "mp4" || ext == "mov" || ext == "mkv"))
        {
            ext = "webm";
        }
    }

    return std::filesystem::u8path(request.outputDirectory) /
           std::filesystem::u8path(
               (request.outputBaseName.empty() ? (input.stem().u8string() + "_converted") : request.outputBaseName) +
               "." + ext);
}

ConvertRunResult ConvertRunner::Run(ConvertRequest request,
                                    std::shared_ptr<std::atomic_bool> cancelRequested,
                                    std::shared_ptr<ConvertSharedState> sharedState)
{
    ConvertRunResult result;
    const std::filesystem::path ffmpegPath = FindFfmpegPath();
    if (ffmpegPath.empty())
    {
        result.status = "Convert failed: ffmpeg not found.";
        return result;
    }

    std::error_code error;
    std::filesystem::create_directories(std::filesystem::u8path(request.outputDirectory), error);
    if (error)
    {
        result.status = "Convert failed: output path.";
        return result;
    }

    const std::filesystem::path input = std::filesystem::u8path(request.inputPath);
    if (!std::filesystem::exists(input))
    {
        result.status = "Convert failed: input file not found.";
        return result;
    }

    const std::filesystem::path output = GetOutputPath(request);
    if (output.empty())
    {
        result.status = "Convert failed: invalid output path.";
        return result;
    }

    std::string lastLine;
    std::string fullOutput;
    int exitCode = -1;
    const auto onOutput = [sourceDuration = request.sourceDurationSeconds, sharedState](const std::string& chunk)
    {
        UpdateConvertProgressFromOutput(chunk, sourceDuration, sharedState);
    };

    const std::string outContainer =
        request.convertContainer
            ? ContainerExtension(request.container)
            : ToLower(output.extension().string().empty() ? std::string{} : output.extension().string().substr(1));
    const InputStreamCodecs sourceCodecs = ProbeInputCodecs(input);

    // YouTube often delivers AV1+AAC. Stream-copy into MOV/WEBM fails (0-byte / tiny broken files).
    // Force encode whenever copy would be illegal for the target container.
    bool encodeVideo = request.convertVideo;
    bool encodeAudio = request.convertAudio;
    std::string uiVideo = NormalizeUiVideoCodec(request.videoCodec.empty() ? DefaultUiVideoForContainer(outContainer)
                                                                           : request.videoCodec);
    std::string uiAudio = NormalizeUiAudioCodec(request.audioCodec.empty() ? DefaultUiAudioForContainer(outContainer)
                                                                           : request.audioCodec);
    if (!encodeVideo && !VideoCodecFitsContainer(sourceCodecs.video, outContainer))
    {
        encodeVideo = true;
        uiVideo = DefaultUiVideoForContainer(outContainer);
    }
    if (!encodeAudio && !AudioCodecFitsContainer(sourceCodecs.audio, outContainer))
    {
        encodeAudio = true;
        uiAudio = DefaultUiAudioForContainer(outContainer);
    }
    // Premiere / many NLEs do not handle AV1 in WEBM well. Unless the user explicitly
    // enabled Video and chose AV1, remux to WEBM re-encodes AV1 → VP9.
    if (!request.convertVideo && !encodeVideo && outContainer == "webm" && ToLower(sourceCodecs.video) == "av1")
    {
        encodeVideo = true;
        uiVideo = "VP9";
    }
    // Container-only jobs still need legal codecs inside the new wrapper.
    if (request.convertContainer && !encodeVideo && !encodeAudio &&
        (!VideoCodecFitsContainer(sourceCodecs.video, outContainer) ||
         !AudioCodecFitsContainer(sourceCodecs.audio, outContainer)))
    {
        encodeVideo = !VideoCodecFitsContainer(sourceCodecs.video, outContainer);
        encodeAudio = !AudioCodecFitsContainer(sourceCodecs.audio, outContainer);
        if (encodeVideo)
        {
            uiVideo = DefaultUiVideoForContainer(outContainer);
        }
        if (encodeAudio)
        {
            uiAudio = DefaultUiAudioForContainer(outContainer);
        }
    }

#ifdef _WIN32
    std::wstring commandLine = QuoteArgument(ffmpegPath.wstring()) + L" -y -i " + QuoteArgument(input.wstring());
    if (encodeVideo)
    {
        commandLine += L" -c:v " + ToWide(VideoEncoder(uiVideo));
        if (uiVideo == "H.264" || uiVideo == "H.265")
        {
            commandLine += L" -pix_fmt yuv420p -preset veryfast";
        }
        if (uiVideo == "VP9")
        {
            commandLine += L" -b:v 0 -crf 32";
        }
    }
    else
    {
        commandLine += L" -c:v copy";
    }
    if (encodeAudio)
    {
        commandLine += L" -c:a " + ToWide(AudioEncoder(uiAudio));
    }
    else
    {
        commandLine += L" -c:a copy";
    }
    if (outContainer == "mov")
    {
        commandLine += L" -f mov";
    }
    else if (outContainer == "webm")
    {
        commandLine += L" -f webm";
    }
    commandLine += L" " + QuoteArgument(output.wstring());
    exitCode = RunProcessCaptureOutput(commandLine, lastLine, fullOutput, cancelRequested, onOutput);
#else
    std::string command = Quote(PathUtf8(ffmpegPath)) + " -y -i " + Quote(PathUtf8(input));
    if (encodeVideo)
    {
        command += " -c:v " + VideoEncoder(uiVideo);
        if (uiVideo == "H.264" || uiVideo == "H.265")
        {
            command += " -pix_fmt yuv420p -preset veryfast";
        }
        if (uiVideo == "VP9")
        {
            command += " -b:v 0 -crf 32";
        }
    }
    else
    {
        command += " -c:v copy";
    }
    if (encodeAudio)
    {
        command += " -c:a " + AudioEncoder(uiAudio);
    }
    else
    {
        command += " -c:a copy";
    }
    if (outContainer == "mov")
    {
        command += " -f mov";
    }
    else if (outContainer == "webm")
    {
        command += " -f webm";
    }
    command += " " + Quote(PathUtf8(output)) + " 2>&1";
    exitCode = RunProcessCaptureOutput(command, lastLine, fullOutput, cancelRequested, onOutput);
#endif
    if (exitCode == -2)
    {
        result.status = "Convert cancelled.";
        return result;
    }
    if (exitCode == 0)
    {
        std::error_code sizeError;
        const auto outputSize = std::filesystem::file_size(output, sizeError);
        // Reject empty/tiny muxer stubs (failed remux often leaves 0–1KB junk).
        constexpr uintmax_t kMinValidOutputBytes = 32 * 1024;
        if (sizeError || outputSize < kMinValidOutputBytes)
        {
            TryRemovePath(output);
            result.status = "Convert failed: empty output file.";
            result.errorLog = fullOutput.empty() ? lastLine : fullOutput;
            return result;
        }

        if (request.deleteInputOnSuccess)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (std::filesystem::absolute(input, sizeError) != std::filesystem::absolute(output, sizeError))
            {
                TryRemovePath(input);
            }
        }
        result.status = "Convert finished.";
        return result;
    }
    if (exitCode < 0)
    {
        result.status = "Convert failed: could not start ffmpeg.";
        return result;
    }
    TryRemovePath(output);
    result.status = lastLine.empty() ? "Convert failed." : "Convert failed: " + lastLine;
    result.errorLog = fullOutput;
    return result;
}

std::string ConvertRunner::Quote(const std::string& value)
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
