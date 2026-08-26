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

std::filesystem::path FindNodeExecutable()
{
#ifdef _WIN32
    const std::array<std::filesystem::path, 2> portablePaths = {
        std::filesystem::path("packages") / "nodejs" / "bin" / "node.exe",
        std::filesystem::path("4kdowner.shared") / "packages" / "nodejs" / "bin" / "node.exe"};
#else
    const std::array<std::filesystem::path, 2> portablePaths = {
        std::filesystem::path("packages") / "nodejs" / "bin" / "node",
        std::filesystem::path("4kdowner.shared") / "packages" / "nodejs" / "bin" / "node"};
#endif
    for (const std::filesystem::path& relativePath : portablePaths)
    {
        const std::filesystem::path found = FindFromCurrentPath(relativePath);
        if (!found.empty())
        {
            return found;
        }
    }

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
    // Prefer IPv4: on Windows, default IPv6 routing can throttle long YouTube downloads
    // to ~0.5 MB/s while IPv4 stays at tens of MiB/s (same network / same yt-dlp).
    std::string args = " --force-ipv4 --remote-components ejs:github";

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

    // android_vr / tv_downgraded / web_embedded: DASH (2K/4K/8K when GVS PO allows).
    // web_safari: Safari UA → HLS muxed streams up to 1080p (age-restricted / long VODs).
    // visionos: HLS m3u8 up to 4K — needed for format listing when android_vr skips PO-token https
    // formats (yt-dlp ≥2026.08) and for downloads that would otherwise 403 mid-file on itag 401/315.
    args += " --extractor-args " +
            QuoteShellArgument("youtube:player_client=android_vr,tv_downgraded,web_embedded,web_safari,visionos");
    return args;
}

std::string BuildYoutubeVisionOsJsRuntimeArgs()
{
    // Same as BuildYoutubeJsRuntimeArgs, but only visionos: HLS (m3u8) often still serves 1440/2160
    // when android_vr https DASH URLs 403 mid-download (GVS PO token / SABR experiments).
    std::string args = " --force-ipv4 --remote-components ejs:github";

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

    args += " --extractor-args " + QuoteShellArgument("youtube:player_client=visionos");
    return args;
}

std::string BuildYoutubeFlatPlaylistArgs()
{
    // Flat dumps only need a JS runtime for challenges — not the watch-page client ladder.
    std::string args = " --force-ipv4 --remote-components ejs:github";
    std::string runtimes;
    const std::filesystem::path denoPath = FindExecutableInPath("deno");
    if (!denoPath.empty())
    {
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
    return args;
}

std::string BuildYoutubeDurationLookupArgs()
{
    // Same JS challenge plumbing as full parse (needed for many Shorts), but fewer clients —
    // duration metadata does not need the full DASH quality ladder.
    std::string args = " --force-ipv4 --remote-components ejs:github";

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

    args += " --extractor-args " + QuoteShellArgument("youtube:player_client=android,web_embedded");
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
        // Profile folder can exist after uninstall leftovers without a Cookies DB.
        const bool hasCookies = std::filesystem::exists(operaPath / "Cookies", error) ||
                                std::filesystem::exists(operaPath / "Network" / "Cookies", error);
        if (!hasCookies)
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
    // Do not list bare "opera" — only AppendDetectedOperaBrowsers when a real Cookies DB exists.
    static const std::vector<std::string> browsers = {"firefox", "edge", "chrome", "vivaldi"};
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
    // Auth / cookie / JS challenge / rights-block failures — try the next browser (and finally
    // no cookies). Bare extraction often restores full android_vr DASH (2K/4K/8K).
    //
    // "requested format is not available": on download, callers try a relaxed -f on the same
    // browser first; if that still fails, falling through here is correct (cookie clients can
    // leave only storyboards while no-cookies still has the ladder).
    // "page needs to be reloaded" is a common cookie/client mismatch on YouTube.
    // Age-gate / rate-limit: another logged-in browser may still work (different Google session).
    return ContainsInsensitive(output, "not a bot") || ContainsInsensitive(output, "sign in to confirm") ||
           ContainsInsensitive(output, "sign in to youtube") ||
           ContainsInsensitive(output, "signature solving failed") ||
           ContainsInsensitive(output, "challenge solving failed") ||
           ContainsInsensitive(output, "page needs to be reloaded") ||
           ContainsInsensitive(output, "only images are available") ||
           ContainsInsensitive(output, "requested format is not available") ||
           ContainsInsensitive(output, "blocked it from display") || ContainsInsensitive(output, "blocked it from") ||
           ContainsInsensitive(output, "age-restricted") || ContainsInsensitive(output, "age restricted") ||
           ContainsInsensitive(output, "confirm your age") ||
           ContainsInsensitive(output, "only available on youtube") || ContainsInsensitive(output, "rate-limited") ||
           ContainsInsensitive(output, "rate limited") || ContainsInsensitive(output, "try again later") ||
           (ContainsInsensitive(output, "video unavailable") &&
            (ContainsInsensitive(output, "blocked") || ContainsInsensitive(output, "watch on youtube"))) ||
           ContainsInsensitive(output, "could not copy chrome cookie") ||
           ContainsInsensitive(output, "failed to decrypt with dpapi") ||
           ContainsInsensitive(output, "could not find") && ContainsInsensitive(output, "cookies database");
}

bool IsYoutubeFormatUnavailableError(const std::string& output)
{
    return ContainsInsensitive(output, "requested format is not available");
}

bool IsYoutubeHttpForbiddenError(const std::string& output)
{
    return ContainsInsensitive(output, "http error 403") ||
           ContainsInsensitive(output, "unable to download video data");
}

bool IsYtDlpDownloadProgressLine(const std::string& line)
{
    if (line.find("[download]") == std::string::npos)
    {
        return false;
    }

    if (line.find("Got error:") != std::string::npos || line.find("ERROR:") != std::string::npos)
    {
        return false;
    }

    if (line.find("Destination:") != std::string::npos)
    {
        return false;
    }

    return line.find("% of") != std::string::npos;
}

std::string FilterYtDlpProgressLinesFromOutput(const std::string& output)
{
    std::ostringstream filtered;
    std::stringstream stream(output);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (IsYtDlpDownloadProgressLine(line))
        {
            continue;
        }
        if (!filtered.str().empty())
        {
            filtered << '\n';
        }
        filtered << line;
    }
    return filtered.str();
}

std::string ExtractLastMeaningfulYtDlpOutputLine(const std::string& output)
{
    size_t end = output.size();
    while (end > 0)
    {
        while (end > 0 && (output[end - 1] == '\r' || output[end - 1] == '\n' || output[end - 1] == ' '))
        {
            --end;
        }
        if (end == 0)
        {
            break;
        }

        size_t start = end;
        while (start > 0 && output[start - 1] != '\r' && output[start - 1] != '\n')
        {
            --start;
        }

        const std::string line = output.substr(start, end - start);
        end = start;
        if (line.empty() || IsYtDlpDownloadProgressLine(line))
        {
            continue;
        }
        return line;
    }

    return {};
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
        if (line.find("[download] Got error:") != std::string::npos)
        {
            const size_t errorStart = line.find("Got error:");
            return line.substr(errorStart);
        }
    }

    const std::string meaningfulLine = ExtractLastMeaningfulYtDlpOutputLine(output);
    if (!meaningfulLine.empty())
    {
        return meaningfulLine;
    }

    return output.empty() ? "yt-dlp could not parse this link." : "yt-dlp could not parse this link.";
}
