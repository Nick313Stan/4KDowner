#include "ToolPaths.h"

#include <array>
#include <cstdlib>
#include <sstream>
#include <string>

namespace
{
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
    if (pathValue == nullptr)
    {
        return {};
    }

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
    return {};
}

// packages/ may hold Windows and Linux builds side by side. Only return the
// native tool so callers (and yt-dlp --ffmpeg-location) never get the wrong OS binary.
std::filesystem::path FindToolNamed(const char* unixName)
{
    const std::string exeName = std::string(unixName) + ".exe";
#ifdef _WIN32
    const std::array<std::filesystem::path, 2> portablePaths = {
        std::filesystem::path("packages") / "ffmpeg" / "bin" / exeName,
        std::filesystem::path("4kdowner.shared") / "packages" / "ffmpeg" / "bin" / exeName};
#else
    const std::array<std::filesystem::path, 2> portablePaths = {
        std::filesystem::path("packages") / "ffmpeg" / "bin" / unixName,
        std::filesystem::path("4kdowner.shared") / "packages" / "ffmpeg" / "bin" / unixName};
#endif
    for (const std::filesystem::path& relativePath : portablePaths)
    {
        const std::filesystem::path found = FindFromCurrentPath(relativePath);
        if (!found.empty())
        {
            return found;
        }
    }

#ifdef _WIN32
    // Prefer ffmpeg.exe on PATH; fall back to extensionless Windows shims (e.g. WinGet).
    std::filesystem::path fromPath = FindExecutableInPath(exeName);
    if (!fromPath.empty())
    {
        return fromPath;
    }
    return FindExecutableInPath(unixName);
#else
    return FindExecutableInPath(unixName);
#endif
}
} // namespace

std::filesystem::path FindFfmpegExecutable()
{
    return FindToolNamed("ffmpeg");
}

std::filesystem::path FindFfprobeExecutable()
{
    return FindToolNamed("ffprobe");
}

std::filesystem::path FindFfmpegDirectory()
{
    const std::filesystem::path ffmpeg = FindFfmpegExecutable();
    if (ffmpeg.empty())
    {
        return {};
    }
    return ffmpeg.parent_path();
}
