#pragma once

#include <string>
#include <vector>

std::string QuoteShellArgument(const std::string& value);
std::string NormalizeYoutubeUrl(const std::string& url);
// JS challenge runtime + YouTube player clients that still list 2K/4K/8K formats.
std::string BuildYoutubeJsRuntimeArgs();
std::string BuildYoutubeCookiesArgs(const std::string& browser);
std::string BuildYoutubeDownloadExtraArgs();
const std::vector<std::string>& GetYoutubeCookieBrowsersToTry();
std::vector<std::string> BuildYoutubeCookieBrowsersToTryList();
void SetPreferredYoutubeCookieBrowser(const std::string& browser);
std::string GetPreferredYoutubeCookieBrowser();
bool ShouldRetryYoutubeWithDifferentCookies(const std::string& output);
bool IsYoutubeFormatUnavailableError(const std::string& output);
std::string SimplifyYtDlpError(const std::string& output);
