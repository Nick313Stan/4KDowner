#pragma once

#include <string>

// Returns quoted yt-dlp launcher, e.g. "...\python.exe" -m yt_dlp or "...\yt-dlp.exe"
std::string BuildYtDlpCommandPrefix();
