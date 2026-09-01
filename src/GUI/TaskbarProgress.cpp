#include "TaskbarProgress.h"

#include "raylib.h"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#include <windows.h>
#include <shobjidl.h>
#undef CloseWindow
#undef ShowCursor
#undef DrawTextEx
#undef LoadImage
#undef DrawText
#endif

#if defined(__linux__) && defined(FOURKDOWNER_HAS_DBUS)
#include <cstdio>
#include <cstring>
#include <dbus/dbus.h>
#endif

namespace TaskbarProgress
{
namespace
{
#ifdef _WIN32
ITaskbarList3* g_taskbar = nullptr;
bool g_triedInit = false;
float g_lastProgress = -2.0f;

void EnsureTaskbar()
{
    if (g_triedInit)
    {
        return;
    }
    g_triedInit = true;

    void* handle = GetWindowHandle();
    if (handle == nullptr)
    {
        g_triedInit = false;
        return;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    ITaskbarList3* taskbar = nullptr;
    const HRESULT created = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&taskbar));
    if (FAILED(created) || taskbar == nullptr)
    {
        return;
    }

    const HRESULT initialized = taskbar->HrInit();
    if (FAILED(initialized))
    {
        taskbar->Release();
        return;
    }

    g_taskbar = taskbar;
}

void SetProgressWin(float progress01)
{
    EnsureTaskbar();
    if (g_taskbar == nullptr)
    {
        return;
    }

    HWND hwnd = static_cast<HWND>(GetWindowHandle());
    if (hwnd == nullptr)
    {
        return;
    }

    if (progress01 < 0.0f)
    {
        if (g_lastProgress < 0.0f && g_lastProgress > -1.5f)
        {
            return;
        }
        g_taskbar->SetProgressState(hwnd, TBPF_NOPROGRESS);
        g_lastProgress = -1.0f;
        return;
    }

    const float clamped = std::clamp(progress01, 0.0f, 1.0f);
    if (std::fabs(clamped - g_lastProgress) < 0.002f && g_lastProgress >= 0.0f)
    {
        return;
    }

    g_taskbar->SetProgressState(hwnd, TBPF_NORMAL);
    g_taskbar->SetProgressValue(hwnd, static_cast<ULONGLONG>(clamped * 1000.0f + 0.5f), 1000);
    g_lastProgress = clamped;
}
#endif

#if defined(__linux__) && defined(FOURKDOWNER_HAS_DBUS)
float g_lastProgressLinux = -2.0f;

void SetProgressLinux(float progress01)
{
    if (progress01 < 0.0f)
    {
        if (g_lastProgressLinux < 0.0f && g_lastProgressLinux > -1.5f)
        {
            return;
        }
    }
    else
    {
        const float clamped = std::clamp(progress01, 0.0f, 1.0f);
        if (std::fabs(clamped - g_lastProgressLinux) < 0.002f && g_lastProgressLinux >= 0.0f)
        {
            return;
        }
    }

    DBusError error;
    dbus_error_init(&error);
    DBusConnection* connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (dbus_error_is_set(&error) || connection == nullptr)
    {
        dbus_error_free(&error);
        return;
    }

    DBusMessage* message = dbus_message_new_signal(
        "/com/canonical/unity/launcherentry/4kdowner", "com.canonical.Unity.LauncherEntry", "Update");
    if (message == nullptr)
    {
        return;
    }

    const char* appUri = "application://4KDowner.desktop";
    DBusMessageIter args;
    dbus_message_iter_init_append(message, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &appUri);

    DBusMessageIter dict;
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);

    const bool visible = progress01 >= 0.0f;
    {
        DBusMessageIter entry;
        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
        const char* key = "progress-visible";
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
        DBusMessageIter variant;
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
        dbus_bool_t value = visible ? TRUE : FALSE;
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
        dbus_message_iter_close_container(&entry, &variant);
        dbus_message_iter_close_container(&dict, &entry);
    }

    {
        DBusMessageIter entry;
        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
        const char* key = "progress";
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
        DBusMessageIter variant;
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "d", &variant);
        const double value = visible ? static_cast<double>(std::clamp(progress01, 0.0f, 1.0f)) : 0.0;
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_DOUBLE, &value);
        dbus_message_iter_close_container(&entry, &variant);
        dbus_message_iter_close_container(&dict, &entry);
    }

    dbus_message_iter_close_container(&args, &dict);
    dbus_connection_send(connection, message, nullptr);
    dbus_connection_flush(connection);
    dbus_message_unref(message);

    g_lastProgressLinux = visible ? std::clamp(progress01, 0.0f, 1.0f) : -1.0f;
}
#endif
} // namespace

void SetProgress(float progress01)
{
#ifdef _WIN32
    SetProgressWin(progress01);
#elif defined(__linux__) && defined(FOURKDOWNER_HAS_DBUS)
    SetProgressLinux(progress01);
#else
    (void)progress01;
#endif
}
} // namespace TaskbarProgress
