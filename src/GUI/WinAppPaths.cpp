#include "WinAppPaths.h"

#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>

#include <array>
#include <climits>
#endif

bool SetWorkingDirectoryToExecutable()
{
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return false;
    }

    std::error_code error;
    std::filesystem::current_path(std::filesystem::path(buffer).parent_path(), error);
    return !error;
#elif defined(__linux__)
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
#else
    return false;
#endif
}

std::filesystem::path GetDocuments4KDownerTempPath()
{
    std::filesystem::path documentsDir;
#ifdef _WIN32
    char* userProfile = nullptr;
    size_t userProfileSize = 0;
    if (_dupenv_s(&userProfile, &userProfileSize, "USERPROFILE") == 0 && userProfile != nullptr &&
        userProfile[0] != '\0')
    {
        documentsDir = std::filesystem::path(userProfile) / "Documents";
        std::free(userProfile);
    }
    else
    {
        std::free(userProfile);
    }
#else
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0')
    {
        documentsDir = std::filesystem::path(home) / "Documents";
    }
#endif

    if (documentsDir.empty())
    {
        documentsDir = std::filesystem::path("Documents");
    }

    return documentsDir / "4KDownerTemp";
}

std::filesystem::path FindAssetPath(const std::filesystem::path& relativePath)
{
    std::error_code error;
    std::filesystem::path directory = std::filesystem::current_path(error);
    if (error)
    {
        return relativePath;
    }

    while (!directory.empty())
    {
        const std::filesystem::path candidate = directory / relativePath;
        if (std::filesystem::exists(candidate, error))
        {
            return std::filesystem::absolute(candidate, error);
        }

        const std::filesystem::path parent = directory.parent_path();
        if (parent == directory)
        {
            break;
        }
        directory = parent;
    }

    return relativePath;
}
