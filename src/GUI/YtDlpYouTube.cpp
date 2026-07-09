#include "YtDlpYouTube.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>

namespace
{
    std::string g_preferredCookieBrowser;
    bool g_preferredCookieBrowserExplicit = false;

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

    std::filesystem::path FindNodeExecutable()
    {
        const std::filesystem::path fromPath = FindExecutableInPath("node");
        if (!fromPath.empty())
        {
            return fromPath;
        }

#ifdef _WIN32
        const std::array<std::filesystem::path, 2> commonPaths = {
            std::filesystem::path("C:/Program Files/nodejs/node.exe"),
            std::filesystem::path("C:/Program Files (x86)/nodejs/node.exe")};
        for (const std::filesystem::path& candidate : commonPaths)
        {
            if (std::filesystem::exists(candidate))
            {
                return candidate;
            }
        }
#endif

        return {};
    }

    bool ContainsInsensitive(const std::string& haystack, const std::string& needle)
    {
        if (needle.empty() || haystack.size() < needle.size())
        {
            return false;
        }

        for (size_t index = 0; index + needle.size() <= haystack.size(); ++index)
        {
            bool matches = true;
            for (size_t offset = 0; offset < needle.size(); ++offset)
            {
                const char left = haystack[index + offset];
                const char right = needle[offset];
                if (left >= 'A' && left <= 'Z')
                {
                    if (left != right && left + ('a' - 'A') != right)
                    {
                        matches = false;
                        break;
                    }
                }
                else if (right >= 'A' && right <= 'Z')
                {
                    if (left != right && left != right + ('a' - 'A'))
                    {
                        matches = false;
                        break;
                    }
                }
                else if (left != right)
                {
                    matches = false;
                    break;
                }
            }
            if (matches)
            {
                return true;
            }
        }

        return false;
    }
}

std::string QuoteShellArgument(const std::string& value)
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
}

std::string NormalizeYoutubeUrl(const std::string& url)
{
    const std::string marker = "youtu.be/";
    const size_t markerIndex = url.find(marker);
    if (markerIndex == std::string::npos)
    {
        return url;
    }

    const size_t queryIndex = url.find('?', markerIndex + marker.size());
    if (queryIndex == std::string::npos)
    {
        return url;
    }

    return url.substr(0, queryIndex);
}

std::string BuildYoutubeJsRuntimeArgs()
{
    std::string args = " --remote-components ejs:github";
    const std::filesystem::path nodePath = FindNodeExecutable();
    if (!nodePath.empty())
    {
        args += " --js-runtimes " + QuoteShellArgument("node:" + nodePath.string());
    }
    return args;
}

std::string BuildYoutubeCookiesArgs(const std::string& browser)
{
    if (browser.empty())
    {
        return {};
    }

    if (browser.find(' ') != std::string::npos)
    {
        return " --cookies-from-browser " + QuoteShellArgument(browser);
    }

    return " --cookies-from-browser " + browser;
}

namespace
{
    void AppendDetectedOperaBrowsers(std::vector<std::string>& browsersToTry)
    {
#ifdef _WIN32
        const char* appData = std::getenv("APPDATA");
        if (appData == nullptr)
        {
            return;
        }

        const std::filesystem::path roaming = std::filesystem::path(appData) / "Opera Software";
        const std::array<std::filesystem::path, 2> operaPaths = {
            roaming / "Opera GX Stable",
            roaming / "Opera Stable",
        };

        for (const std::filesystem::path& operaPath : operaPaths)
        {
            std::error_code error;
            if (!std::filesystem::exists(operaPath, error) || error)
            {
                continue;
            }

            const std::string spec = "opera:" + operaPath.string();
            if (std::find(browsersToTry.begin(), browsersToTry.end(), spec) == browsersToTry.end())
            {
                browsersToTry.push_back(spec);
            }
        }
#endif
    }
}

const std::vector<std::string>& GetYoutubeCookieBrowsersToTry()
{
    static const std::vector<std::string> browsers = {"firefox", "edge", "chrome", "opera"};
    return browsers;
}

std::vector<std::string> BuildYoutubeCookieBrowsersToTryList()
{
    std::vector<std::string> browsersToTry;
    if (g_preferredCookieBrowserExplicit)
    {
        browsersToTry.push_back(g_preferredCookieBrowser);
    }
    else if (!g_preferredCookieBrowser.empty())
    {
        browsersToTry.push_back(g_preferredCookieBrowser);
    }

    for (const std::string& browser : GetYoutubeCookieBrowsersToTry())
    {
        if (std::find(browsersToTry.begin(), browsersToTry.end(), browser) == browsersToTry.end())
        {
            browsersToTry.push_back(browser);
        }
    }

    AppendDetectedOperaBrowsers(browsersToTry);

    if (std::find(browsersToTry.begin(), browsersToTry.end(), std::string{}) == browsersToTry.end())
    {
        browsersToTry.push_back("");
    }

    return browsersToTry;
}

void SetPreferredYoutubeCookieBrowser(const std::string& browser)
{
    g_preferredCookieBrowser = browser;
    g_preferredCookieBrowserExplicit = true;
}

std::string GetPreferredYoutubeCookieBrowser()
{
    return g_preferredCookieBrowser;
}

std::string BuildYoutubeDownloadExtraArgs()
{
    std::string args = BuildYoutubeJsRuntimeArgs();
    const std::vector<std::string> browsersToTry = BuildYoutubeCookieBrowsersToTryList();
    if (!browsersToTry.empty())
    {
        args += BuildYoutubeCookiesArgs(browsersToTry.front());
    }
    return args;
}

bool ShouldRetryYoutubeWithDifferentCookies(const std::string& output)
{
    return ContainsInsensitive(output, "not a bot") ||
        ContainsInsensitive(output, "sign in to confirm") ||
        ContainsInsensitive(output, "requested format is not available") ||
        ContainsInsensitive(output, "signature solving failed") ||
        ContainsInsensitive(output, "challenge solving failed") ||
        ContainsInsensitive(output, "only images are available") ||
        ContainsInsensitive(output, "could not copy chrome cookie") ||
        ContainsInsensitive(output, "failed to decrypt with dpapi") ||
        ContainsInsensitive(output, "could not find") && ContainsInsensitive(output, "cookies database");
}

std::string SimplifyYtDlpError(const std::string& output)
{
    if (ContainsInsensitive(output, "not a bot") || ContainsInsensitive(output, "sign in to confirm"))
    {
        return "YouTube blocked this request. Sign in to YouTube in Firefox, Edge, Chrome, or Opera, then try again.";
    }

    if (ContainsInsensitive(output, "signature solving failed") ||
        ContainsInsensitive(output, "challenge solving failed") ||
        ContainsInsensitive(output, "only images are available"))
    {
        return "YouTube requires browser cookies and Node.js for this video. Sign in to YouTube in your browser, then try again.";
    }

    if (ContainsInsensitive(output, "could not copy chrome cookie") ||
        ContainsInsensitive(output, "failed to decrypt with dpapi"))
    {
        return "Could not read browser cookies. Close Chrome/Edge/Opera or sign in through Firefox, then try again.";
    }

    std::stringstream stream(output);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.rfind("ERROR:", 0) == 0)
        {
            return line.substr(6);
        }
    }

    return output.empty() ? "yt-dlp could not parse this link." : output;
}
