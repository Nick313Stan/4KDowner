#pragma once

#include <string>

// YouTube live titles often end with " YYYY-MM-DD HH:MM" (stream start); strip for display/filenames.
std::string StripYoutubeLiveStreamTitleSuffix(const std::string& title);
std::string NormalizeVideoTitle(const std::string& title);
