#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>
#include <vector>

inline void KillProcessTree(unsigned long processId)
{
    STARTUPINFOA startupInfo{};
    PROCESS_INFORMATION processInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    const std::string command = "taskkill /F /T /PID " + std::to_string(processId);
    std::vector<char> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back('\0');

    if (CreateProcessA(nullptr,
                       mutableCommand.data(),
                       nullptr,
                       nullptr,
                       FALSE,
                       CREATE_NO_WINDOW,
                       nullptr,
                       nullptr,
                       &startupInfo,
                       &processInfo))
    {
        WaitForSingleObject(processInfo.hProcess, 5000);
        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);
    }
}
#else
#include <csignal>
#include <unistd.h>

inline void KillProcessTree(unsigned long processId)
{
    if (processId == 0)
    {
        return;
    }
    // Negative PID = process group (runners should setpgid).
    ::kill(-static_cast<pid_t>(processId), SIGTERM);
    ::kill(static_cast<pid_t>(processId), SIGTERM);
}
#endif
