#include "ConvertRunner.h"

#include "WinProcess.h"

#include <array>
#include <atomic>
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

    int RunProcessCaptureOutput(
        const std::wstring& commandLine,
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
    int RunProcessCaptureOutput(
        const std::string& commandLine,
        std::string& lastLine,
        std::string& fullOutput,
        const std::shared_ptr<std::atomic_bool>& cancelRequested,
        const std::function<void(const std::string&)>& onOutput)
    {
        (void)cancelRequested;
        (void)onOutput;
        FILE* pipe = POPEN(commandLine.c_str(), "r");
        if (pipe == nullptr)
        {
            return -1;
        }

        std::array<char, 512> buffer{};
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        {
            const std::string line = buffer.data();
            fullOutput += line;
            if (!line.empty())
            {
                lastLine = line;
            }
        }
        return PCLOSE(pipe);
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

    void UpdateConvertProgressFromOutput(
        const std::string& chunk,
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

    std::filesystem::path FindFfmpegPath()
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
                    return std::filesystem::absolute(candidate);
                }
                const std::filesystem::path parent = directory.parent_path();
                if (parent == directory)
                {
                    break;
                }
                directory = parent;
            }
        }
        return FindExecutableInPath("ffmpeg.exe");
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

    std::string ContainerExtension(const std::string &container)
    {
        std::string value = ToLower(container);
        const size_t space = value.find(' ');
        if (space != std::string::npos)
        {
            value = value.substr(0, space);
        }
        return value == "unknown" || value.empty() ? "mp4" : value;
    }

    std::string VideoEncoder(const std::string &codec)
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

    std::string AudioEncoder(const std::string &codec)
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
    sourceDurationSeconds_ = request.sourceDurationSeconds;
    startedAt_ = std::chrono::steady_clock::now();
    isRunning_ = true;
    cancelRequested_ = std::make_shared<std::atomic_bool>(false);
    sharedState_ = std::make_shared<ConvertSharedState>();
    future_ = std::async(std::launch::async, [request = std::move(request), cancelRequested = cancelRequested_, sharedState = sharedState_]
                         { return Run(request, cancelRequested, sharedState); });
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
            completedElapsedSeconds_ = elapsedSeconds_;
            hasCompletedConvert_ = true;
        }
        isRunning_ = false;
        cancelRequested_.reset();
        sharedState_.reset();
        currentInputPath_.clear();
        sourceDurationSeconds_ = 0.0;
        return;
    }

    elapsedSeconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
}

bool ConvertRunner::IsRunning() const
{
    return isRunning_;
}

const std::string &ConvertRunner::Status() const
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

bool ConvertRunner::ConsumeCompletedConvert(std::string& inputPath, double& elapsedSeconds)
{
    if (!hasCompletedConvert_)
    {
        return false;
    }

    inputPath = completedInputPath_;
    elapsedSeconds = completedElapsedSeconds_;
    hasCompletedConvert_ = false;
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
    if (request.convertContainer)
    {
        ext = ContainerExtension(request.container);
    }

    return std::filesystem::u8path(request.outputDirectory) /
        std::filesystem::u8path(input.stem().u8string() + "_converted." + ext);
}

ConvertRunResult ConvertRunner::Run(
    ConvertRequest request,
    std::shared_ptr<std::atomic_bool> cancelRequested,
    std::shared_ptr<ConvertSharedState> sharedState)
{
    ConvertRunResult result;
    const std::filesystem::path ffmpegPath = FindFfmpegPath();
    if (ffmpegPath.empty())
    {
        result.status = "Convert failed: ffmpeg.exe not found.";
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
#ifdef _WIN32
    std::wstring commandLine = QuoteArgument(ffmpegPath.wstring()) +
                               L" -y -i " + QuoteArgument(input.wstring());
    commandLine += request.convertVideo ? L" -c:v " + ToWide(VideoEncoder(request.videoCodec)) : L" -c:v copy";
    commandLine += request.convertAudio ? L" -c:a " + ToWide(AudioEncoder(request.audioCodec)) : L" -c:a copy";
    commandLine += L" " + QuoteArgument(output.wstring());
    exitCode = RunProcessCaptureOutput(commandLine, lastLine, fullOutput, cancelRequested, onOutput);
#else
    std::string command = Quote(PathUtf8(ffmpegPath)) + " -y -i " + Quote(PathUtf8(input));
    command += request.convertVideo ? " -c:v " + VideoEncoder(request.videoCodec) : " -c:v copy";
    command += request.convertAudio ? " -c:a " + AudioEncoder(request.audioCodec) : " -c:a copy";
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
        result.status = "Convert finished.";
        return result;
    }
    if (exitCode < 0)
    {
        result.status = "Convert failed: could not start ffmpeg.";
        return result;
    }
    result.status = lastLine.empty() ? "Convert failed." : "Convert failed: " + lastLine;
    result.errorLog = fullOutput;
    return result;
}

std::string ConvertRunner::Quote(const std::string &value)
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
