#include "WinAppPaths.h"

#include <filesystem>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
#elif defined(__linux__)
#include <unistd.h>

#include <array>
#include <climits>

bool SetWorkingDirectoryToExecutable()
{
    std::array<char, PATH_MAX> buffer{};
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0)
    {
        return false;
    }
    buffer[static_cast<size_t>(length)] = '\0';
    std::error_code error;
    std::filesystem::current_path(std::filesystem::path(buffer.data()).parent_path(), error);
    return !error;
}
#else
bool SetWorkingDirectoryToExecutable()
{
    return false;
}
#endif
