#include "ConverterInfoLoader.h"

#include "ToolPaths.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
#endif

namespace
{
std::string PathUtf8(const std::filesystem::path& path)
{
    return path.u8string();
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

std::filesystem::path FindFfprobePath()
{
    return ::FindFfprobeExecutable();
}

std::filesystem::path FindFfmpegPath()
{
    return ::FindFfmpegExecutable();
}

std::filesystem::path GetConverterPreviewCacheDirectory()
{
#ifdef _WIN32
    char* localAppData = nullptr;
    size_t localAppDataSize = 0;
    if (_dupenv_s(&localAppData, &localAppDataSize, "LOCALAPPDATA") == 0 && localAppData != nullptr &&
        localAppData[0] != '\0')
    {
        const std::filesystem::path directory = std::filesystem::path(localAppData) / "4KDowner" / "converter-previews";
        std::free(localAppData);
        return directory;
    }
    std::free(localAppData);
#endif
    return std::filesystem::temp_directory_path() / "4KDowner" / "converter-previews";
}

std::string MakePreviewFileName(const std::filesystem::path& inputPath)
{
    const auto hash = std::hash<std::string>{}(PathUtf8(inputPath));
    return "preview_" + std::to_string(hash) + ".jpg";
}

std::string ValueAfterPrefix(const std::string& line, const std::string& prefix)
{
    return line.rfind(prefix, 0) == 0 ? line.substr(prefix.size()) : std::string{};
}

std::string ToLower(std::string value)
{
    for (char& c : value)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

std::string NormalizeContainer(std::string value, const std::filesystem::path& filePath)
{
    const std::string extension = ToLower(filePath.extension().u8string());
    if (!extension.empty() && extension.front() == '.')
    {
        const std::string ext = extension.substr(1);
        if (ext == "webm")
        {
            return "WEBM";
        }
        if (ext == "mkv")
        {
            return "MKV";
        }
        if (ext == "mov")
        {
            return "MOV";
        }
        if (ext == "mp4" || ext == "m4v")
        {
            return "MP4";
        }
    }

    const size_t comma = value.find(',');
    if (comma != std::string::npos)
    {
        const std::string secondary = value.substr(comma + 1);
        if (secondary == "webm")
        {
            return "WEBM";
        }
        value = value.substr(0, comma);
    }

    if (value == "mov" || value == "qt")
    {
        return "MOV";
    }
    if (value == "mp4" || value == "m4a" || value == "m4v" || value == "3gp")
    {
        return "MP4";
    }
    if (value == "matroska" || value == "webm")
    {
        return value == "webm" ? "WEBM" : "MKV";
    }
    for (char& c : value)
    {
        if (c >= 'a' && c <= 'z')
        {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return value.empty() ? "Unknown" : value;
}

std::string NormalizeVideoCodec(std::string value)
{
    value = ToLower(value);
    if (value == "h264" || value.find("avc") != std::string::npos)
    {
        return "H.264";
    }
    if (value == "hevc" || value == "h265")
    {
        return "H.265";
    }
    if (value == "av1" || value.find("av01") != std::string::npos)
    {
        return "AV1";
    }
    if (value == "vp9" || value.find("vp9") != std::string::npos)
    {
        return "VP9";
    }
    if (value == "vp8" || value.find("vp8") != std::string::npos)
    {
        return "VP8";
    }
    return value.empty() ? "None" : value;
}

std::string NormalizeAudioCodec(std::string value)
{
    if (value == "aac")
    {
        return "AAC";
    }
    if (value == "mp3")
    {
        return "MP3";
    }
    if (value == "opus")
    {
        return "Opus";
    }
    if (value == "flac")
    {
        return "FLAC";
    }
    if (value == "vorbis")
    {
        return "Vorbis";
    }
    return value.empty() ? "None" : value;
}

std::string FormatDuration(double seconds)
{
    if (seconds <= 0.0)
    {
        return "--:--";
    }

    const int totalSeconds = static_cast<int>(seconds + 0.5);
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    const int secs = totalSeconds % 60;
    char buffer[32]{};
    if (hours > 0)
    {
        std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes, secs);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, secs);
    }
    return buffer;
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

std::string RunCommand(const std::wstring& commandLine)
{
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
        return {};
    }

    std::string output;
    const auto consumeOutput = [&]()
    {
        DWORD available = 0;
        while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0)
        {
            std::array<char, 512> buffer{};
            DWORD read = 0;
            if (!ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || read == 0)
            {
                break;
            }
            output.append(buffer.data(), read);
        }
    };

    while (WaitForSingleObject(processInfo.hProcess, 100) == WAIT_TIMEOUT)
    {
        consumeOutput();
    }
    consumeOutput();

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    CloseHandle(readPipe);
    return output;
}
#else
std::string RunCommand(const std::string& command)
{
    FILE* pipe = popen(command.c_str(), "r");
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
    pclose(pipe);
    return output;
}
#endif

std::string ExtractMidVideoPreview(const std::filesystem::path& inputPath, double durationSeconds)
{
    if (durationSeconds <= 0.05)
    {
        return {};
    }

    const std::filesystem::path ffmpegPath = FindFfmpegPath();
    if (ffmpegPath.empty())
    {
        return {};
    }

    std::error_code error;
    const std::filesystem::path cacheDirectory = GetConverterPreviewCacheDirectory();
    std::filesystem::create_directories(cacheDirectory, error);
    if (error)
    {
        return {};
    }

    const std::filesystem::path previewPath = cacheDirectory / MakePreviewFileName(inputPath);
    if (std::filesystem::exists(previewPath, error))
    {
        std::filesystem::remove(previewPath, error);
    }

    const double seekSeconds = std::max(0.0, durationSeconds * 0.5);
    char seekBuffer[32]{};
    std::snprintf(seekBuffer, sizeof(seekBuffer), "%.3f", seekSeconds);

#ifdef _WIN32
    const std::wstring seekWide(seekBuffer, seekBuffer + std::strlen(seekBuffer));
    const std::wstring command = QuoteArgument(ffmpegPath.wstring()) + L" -hide_banner -loglevel error -ss " +
                                 seekWide + L" -i " + QuoteArgument(inputPath.wstring()) + L" -frames:v 1 -q:v 3 -y " +
                                 QuoteArgument(previewPath.wstring());
    RunCommand(command);
#else
    auto quotePath = [](const std::string& value)
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
    const std::string command = quotePath(PathUtf8(ffmpegPath)) + " -hide_banner -loglevel error -ss " + seekBuffer +
                                " -i " + quotePath(PathUtf8(inputPath)) + " -frames:v 1 -q:v 3 -y " +
                                quotePath(PathUtf8(previewPath)) + " 2>&1";
    RunCommand(command);
#endif

    if (!std::filesystem::exists(previewPath, error) || std::filesystem::file_size(previewPath, error) == 0)
    {
        return {};
    }

    return PathUtf8(previewPath);
}

std::string FirstNonEmptyLine(const std::string& text)
{
    std::stringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (!line.empty() && line.find("No such file") == std::string::npos &&
            line.find("The filename") == std::string::npos && line.find("Invalid argument") == std::string::npos)
        {
            return line;
        }
    }
    return {};
}

std::mutex g_abandonedConverterFuturesMutex;
std::vector<std::future<ConverterFileInfo>> g_abandonedConverterFutures;

void AbandonConverterFuture(std::future<ConverterFileInfo>& future)
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

    std::lock_guard<std::mutex> lock(g_abandonedConverterFuturesMutex);
    g_abandonedConverterFutures.push_back(std::move(future));
}
} // namespace

ConverterInfoLoader::~ConverterInfoLoader()
{
    AbandonRunningWork();
}

ConverterInfoLoader::ConverterInfoLoader(ConverterInfoLoader&& other) noexcept
    : future_(std::move(other.future_)),
      result_(std::move(other.result_)),
      isLoading_(other.isLoading_),
      hasResult_(other.hasResult_)
{
    other.isLoading_ = false;
    other.hasResult_ = false;
}

ConverterInfoLoader& ConverterInfoLoader::operator=(ConverterInfoLoader&& other) noexcept
{
    if (this != &other)
    {
        AbandonRunningWork();
        future_ = std::move(other.future_);
        result_ = std::move(other.result_);
        isLoading_ = other.isLoading_;
        hasResult_ = other.hasResult_;
        other.isLoading_ = false;
        other.hasResult_ = false;
    }
    return *this;
}

void ConverterInfoLoader::AbandonRunningWork()
{
    AbandonConverterFuture(future_);
    isLoading_ = false;
}

void ConverterInfoLoader::ReapAbandoned()
{
    std::lock_guard<std::mutex> lock(g_abandonedConverterFuturesMutex);
    auto it = g_abandonedConverterFutures.begin();
    while (it != g_abandonedConverterFutures.end())
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
            it = g_abandonedConverterFutures.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void ConverterInfoLoader::Cancel()
{
    AbandonRunningWork();
    result_ = {};
    hasResult_ = false;
}

void ConverterInfoLoader::ClearResult()
{
    hasResult_ = false;
}

void ConverterInfoLoader::Start(std::string filePath)
{
    AbandonRunningWork();

    result_ = {};
    result_.filePath = filePath;
    hasResult_ = false;
    isLoading_ = true;

    future_ = std::async(std::launch::async,
                         [filePath = std::move(filePath)]
                         {
                             return Load(filePath);
                         });
}

void ConverterInfoLoader::Update()
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
    }
}

bool ConverterInfoLoader::IsLoading() const
{
    return isLoading_;
}

bool ConverterInfoLoader::HasResult() const
{
    return hasResult_;
}

const ConverterFileInfo& ConverterInfoLoader::GetResult() const
{
    return result_;
}

ConverterFileInfo ConverterInfoLoader::Load(std::string filePath)
{
    ConverterFileInfo info;
    const std::filesystem::path inputPath = std::filesystem::u8path(filePath);
    info.filePath = filePath;
    info.fileName = inputPath.filename().u8string();

    const std::filesystem::path ffprobePath = FindFfprobePath();
    if (ffprobePath.empty())
    {
        info.error = "ffprobe.exe not found.";
        return info;
    }

#ifdef _WIN32
    const std::wstring ffprobeBase = QuoteArgument(ffprobePath.wstring());
    const std::wstring fileArg = QuoteArgument(inputPath.wstring());
    const std::string output = RunCommand(
        ffprobeBase + L" -v error -show_entries format=format_name,duration -of default=noprint_wrappers=1 " + fileArg);
    const std::string videoStream = RunCommand(
        ffprobeBase +
        L" -v error -select_streams v:0 -show_entries stream=codec_name,width,height -of default=noprint_wrappers=1 " +
        fileArg);
    const std::string audioCodec = FirstNonEmptyLine(RunCommand(
        ffprobeBase +
        L" -v error -select_streams a:0 -show_entries stream=codec_name -of default=noprint_wrappers=1:nokey=1 " +
        fileArg));
#else
    const std::string ffprobe = Quote(PathUtf8(ffprobePath));
    const std::string file = Quote(PathUtf8(inputPath));
    const std::string output =
        RunCommand(ffprobe + " -v error -show_entries format=format_name,duration -of default=noprint_wrappers=1 " +
                   file + " 2>&1");
    const std::string videoStream = RunCommand(
        ffprobe +
        " -v error -select_streams v:0 -show_entries stream=codec_name,width,height -of default=noprint_wrappers=1 " +
        file + " 2>&1");
    const std::string audioCodec = FirstNonEmptyLine(RunCommand(
        ffprobe +
        " -v error -select_streams a:0 -show_entries stream=codec_name -of default=noprint_wrappers=1:nokey=1 " + file +
        " 2>&1"));
#endif

    std::string videoCodec;
    {
        std::stringstream videoParse(videoStream);
        std::string videoLine;
        while (std::getline(videoParse, videoLine))
        {
            if (!videoLine.empty() && videoLine.back() == '\r')
            {
                videoLine.pop_back();
            }
            const std::string codecName = ValueAfterPrefix(videoLine, "codec_name=");
            if (!codecName.empty())
            {
                videoCodec = codecName;
                continue;
            }
            const std::string widthValue = ValueAfterPrefix(videoLine, "width=");
            if (!widthValue.empty())
            {
                try
                {
                    info.width = std::stoi(widthValue);
                }
                catch (...)
                {
                    info.width = 0;
                }
                continue;
            }
            const std::string heightValue = ValueAfterPrefix(videoLine, "height=");
            if (!heightValue.empty())
            {
                try
                {
                    info.height = std::stoi(heightValue);
                }
                catch (...)
                {
                    info.height = 0;
                }
                continue;
            }
        }
        if (info.height > 0)
        {
            info.resolution = std::to_string(info.height) + "p";
        }
    }

    std::stringstream stream(output);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        const std::string formatName = ValueAfterPrefix(line, "format_name=");
        if (!formatName.empty())
        {
            info.container = NormalizeContainer(formatName, inputPath);
            continue;
        }

        const std::string duration = ValueAfterPrefix(line, "duration=");
        if (!duration.empty())
        {
            try
            {
                info.durationSeconds = std::stod(duration);
                info.duration = FormatDuration(info.durationSeconds);
            }
            catch (...)
            {
                info.durationSeconds = 0.0;
                info.duration = "--:--";
            }
            continue;
        }
    }

    info.videoCodec = NormalizeVideoCodec(videoCodec);
    info.audioCodec = NormalizeAudioCodec(audioCodec);

    if (info.container.empty())
    {
        info.container = "Unknown";
    }
    if (info.videoCodec.empty())
    {
        info.videoCodec = "None";
    }
    if (info.audioCodec.empty())
    {
        info.audioCodec = "None";
    }
    if (info.duration.empty())
    {
        info.duration = "--:--";
    }

    info.success = output.find("format_name=") != std::string::npos;
    if (!info.success)
    {
        const std::string errorLine = FirstNonEmptyLine(output);
        info.error = errorLine.empty() ? "ffprobe could not read this file." : errorLine;
    }
    else if (info.videoCodec != "None" && info.durationSeconds > 0.05)
    {
        info.previewPath = ExtractMidVideoPreview(inputPath, info.durationSeconds);
    }
    return info;
}

std::string ConverterInfoLoader::Quote(const std::string& value)
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
