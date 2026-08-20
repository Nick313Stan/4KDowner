#pragma once

#include <filesystem>
#include <string>

// Locate bundled or PATH tools for the current OS (Windows: *.exe; Linux: no extension).
// Prefer the native portable binary when packages/ holds both OS builds side by side.
std::filesystem::path FindFfmpegExecutable();
std::filesystem::path FindFfprobeExecutable();
std::filesystem::path FindFfmpegDirectory();
