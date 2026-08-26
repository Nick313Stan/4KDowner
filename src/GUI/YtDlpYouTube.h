#pragma once

#include <string>
#include <vector>

std::string QuoteShellArgument(const std::string& value);
std::string NormalizeYoutubeUrl(const std::string& url);
// --force-ipv4 + JS challenge runtime + player clients for DASH/HLS including 2K/4K/8K.
std::string BuildYoutubeJsRuntimeArgs();
// Same JS plumbing, but visionos only — exposes HLS m3u8 up to 4K when android_vr DASH CDN 403s.
std::string BuildYoutubeVisionOsJsRuntimeArgs();
// Flat playlist/channel tab dumps: JS runtime only — player_client ladder is for watch pages.
std::string BuildYoutubeFlatPlaylistArgs();
// Lighter JS/player stack for duration-only Shorts/video lookups (no full DASH ladder).
std::string BuildYoutubeDurationLookupArgs();
std::string BuildYoutubeCookiesArgs(const std::string& browser);
std::string BuildYoutubeDownloadExtraArgs();
const std::vector<std::string>& GetYoutubeCookieBrowsersToTry();
std::vector<std::string> BuildYoutubeCookieBrowsersToTryList();
void SetPreferredYoutubeCookieBrowser(const std::string& browser);
std::string GetPreferredYoutubeCookieBrowser();
bool ShouldRetryYoutubeWithDifferentCookies(const std::string& output);
bool IsYoutubeFormatUnavailableError(const std::string& output);
// CDN/auth rejects the media URL mid-download (common on high-res DASH without a working session).
bool IsYoutubeHttpForbiddenError(const std::string& output);
// Rolling "[download] 34.1% of … at …" lines — not errors; omit from failure text and clipboard logs.
bool IsYtDlpDownloadProgressLine(const std::string& line);
std::string FilterYtDlpProgressLinesFromOutput(const std::string& output);
std::string ExtractLastMeaningfulYtDlpOutputLine(const std::string& output);
std::string SimplifyYtDlpError(const std::string& output);
