#include "WinAppPaths.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>

bool SetWorkingDirectoryToExecutable()
{
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return false;
    }

    std::error_code error;
    std::filesystem::current_path(std::filesystem::path(buffer).parent_path(), error);
    return !error;
}
#else
bool SetWorkingDirectoryToExecutable()
{
    return false;
}
#endif
