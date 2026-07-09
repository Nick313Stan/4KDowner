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
} // namespace

std::string QuoteShellArgument(const std::string& value)
{
#ifdef _WIN32
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
#else
    // POSIX single-quote escaping: 'foo'\''bar'
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('\'');
    for (const char c : value)
    {
        if (c == '\'')
        {
            escaped += "'\\''";
        }
        else
        {
            escaped.push_back(c);
        }
    }
    escaped.push_back('\'');
    return escaped;
#endif
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

    // yt-dlp enables only deno by default; pass any available runtime explicitly.
    std::string runtimes;
    const std::filesystem::path denoPath = FindExecutableInPath("deno");
    if (!denoPath.empty())
    {
        if (!runtimes.empty())
        {
            runtimes += ',';
        }
        runtimes += "deno:" + denoPath.string();
    }
    const std::filesystem::path nodePath = FindNodeExecutable();
    if (!nodePath.empty())
    {
        if (!runtimes.empty())
        {
            runtimes += ',';
        }
        runtimes += "node:" + nodePath.string();
    }
    if (!runtimes.empty())
    {
        args += " --js-runtimes " + QuoteShellArgument(runtimes);
    }

    // Default web clients are often SABR-capped at 1080p. These still expose full DASH (2K/4K/8K).
    args += " --extractor-args " + QuoteShellArgument("youtube:player_client=android_vr,tv_downgraded,web_embedded");
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
} // namespace

const std::vector<std::string>& GetYoutubeCookieBrowsersToTry()
{
    static const std::vector<std::string> browsers = {"firefox", "edge", "chrome", "vivaldi", "opera"};
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
    // Auth / cookie / JS challenge failures — try the next browser (and finally no cookies).
    // Do NOT include "requested format is not available": that means cookies worked
    // and the format selector must be relaxed on the same browser instead.
    // "page needs to be reloaded" is a common cookie/client mismatch on YouTube; bare
    // extraction without cookies often still succeeds.
    return ContainsInsensitive(output, "not a bot") || ContainsInsensitive(output, "sign in to confirm") ||
           ContainsInsensitive(output, "signature solving failed") ||
           ContainsInsensitive(output, "challenge solving failed") ||
           ContainsInsensitive(output, "page needs to be reloaded") ||
           ContainsInsensitive(output, "only images are available") ||
           ContainsInsensitive(output, "could not copy chrome cookie") ||
           ContainsInsensitive(output, "failed to decrypt with dpapi") ||
           ContainsInsensitive(output, "could not find") && ContainsInsensitive(output, "cookies database");
}

bool IsYoutubeFormatUnavailableError(const std::string& output)
{
    return ContainsInsensitive(output, "requested format is not available");
}

std::string SimplifyYtDlpError(const std::string& output)
{
    if (ContainsInsensitive(output, "not a bot") || ContainsInsensitive(output, "sign in to confirm"))
    {
        return "YouTube blocked this request. Sign in to YouTube in Firefox, Edge, Chrome, Vivaldi, or Opera, then "
               "try again.";
    }

    if (ContainsInsensitive(output, "page needs to be reloaded"))
    {
        return "YouTube rejected this browser session. Try again (4KDowner will retry without cookies), or refresh "
               "YouTube in your browser and sign in again.";
    }

    if (ContainsInsensitive(output, "signature solving failed") ||
        ContainsInsensitive(output, "challenge solving failed") ||
        ContainsInsensitive(output, "only images are available"))
    {
        return "YouTube requires browser cookies and Node.js for this video. Sign in to YouTube in your browser, then "
               "try again.";
    }

    if (ContainsInsensitive(output, "could not copy chrome cookie") ||
        ContainsInsensitive(output, "failed to decrypt with dpapi"))
    {
        return "Could not read browser cookies. Close Chrome/Edge/Vivaldi/Opera or sign in through Firefox, then try "
               "again.";
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
