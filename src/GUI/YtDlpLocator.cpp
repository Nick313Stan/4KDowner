#include "YtDlpLocator.h"

#include "YtDlpYouTube.h"

#include <array>
#include <cstdlib>
#include <filesystem>
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
} // namespace

std::string BuildYtDlpCommandPrefix()
{
#ifdef _WIN32
    const std::array<std::filesystem::path, 2> portablePythonPaths = {
        std::filesystem::path("packages") / "ytdown" / "python" / "python.exe",
        std::filesystem::path("4kdowner.shared") / "packages" / "ytdown" / "python" / "python.exe"};
#else
    const std::array<std::filesystem::path, 4> portablePythonPaths = {
        std::filesystem::path("packages") / "ytdown" / "venv" / "bin" / "python3",
        std::filesystem::path("packages") / "ytdown" / "venv" / "bin" / "python",
        std::filesystem::path("packages") / "ytdown" / "bin" / "python3",
        std::filesystem::path("4kdowner.shared") / "packages" / "ytdown" / "venv" / "bin" / "python3"};
#endif
    for (const std::filesystem::path& relativePath : portablePythonPaths)
    {
        const std::filesystem::path pythonPath = FindFromCurrentPath(relativePath);
        if (!pythonPath.empty())
        {
            return QuoteShellArgument(pythonPath.string()) + " -u -m yt_dlp";
        }
    }

#ifdef _WIN32
    const std::array<std::filesystem::path, 2> legacyLauncherPaths = {
        std::filesystem::path("packages") / "ytdown" / ".venv" / "Scripts" / "yt-dlp.exe",
        std::filesystem::path("4kdowner.shared") / "packages" / "ytdown" / ".venv" / "Scripts" / "yt-dlp.exe"};
#else
    const std::array<std::filesystem::path, 2> legacyLauncherPaths = {
        std::filesystem::path("packages") / "ytdown" / "venv" / "bin" / "yt-dlp",
        std::filesystem::path("packages") / "ytdown" / "bin" / "yt-dlp"};
#endif
    for (const std::filesystem::path& relativePath : legacyLauncherPaths)
    {
        const std::filesystem::path launcherPath = FindFromCurrentPath(relativePath);
        if (!launcherPath.empty())
        {
            return QuoteShellArgument(launcherPath.string());
        }
    }

    const std::filesystem::path fromPath = FindExecutableInPath("yt-dlp");
    if (!fromPath.empty())
    {
        return QuoteShellArgument(fromPath.string());
    }

    // Do not fall back to `python3 -m yt_dlp` on Linux: system Python often lacks
    // the module and yields "No module named yt_dlp". Use
    // scripts/Linux/setup-ytdown-portable.sh → packages/ytdown/bin/yt-dlp, or PATH.

    return {};
}
