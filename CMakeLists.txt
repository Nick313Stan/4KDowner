cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(FourKDowner
    VERSION 1.2.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(PACKAGES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../packages")
set(RAYLIB_DIR "${PACKAGES_DIR}/raylib")
set(TINYFILEDIALOGS_DIR "${PACKAGES_DIR}/tinyfiledialogs")

if(NOT EXISTS "${RAYLIB_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "raylib was not found at ${RAYLIB_DIR}")
endif()

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_JPG ON CACHE BOOL "" FORCE)

add_subdirectory("${RAYLIB_DIR}" "${CMAKE_BINARY_DIR}/raylib")

add_executable(4KDowner
    src/main.cpp
    src/GUI/Application.cpp
    src/GUI/Application.h
    src/GUI/WinAppPaths.cpp
    src/GUI/WinAppPaths.h
    src/GUI/Button.cpp
    src/GUI/Button.h
    src/GUI/Checkbox.cpp
    src/GUI/Checkbox.h
    src/GUI/ConvertRunner.cpp
    src/GUI/ConvertRunner.h
    src/GUI/ConverterFileCardNode.cpp
    src/GUI/ConverterFileCardNode.h
    src/GUI/ConverterInfoLoader.cpp
    src/GUI/ConverterInfoLoader.h
    src/GUI/DockArea.cpp
    src/GUI/DockArea.h
    src/GUI/YtDlpLocator.cpp
    src/GUI/YtDlpLocator.h
    src/GUI/YtDlpYouTube.cpp
    src/GUI/YtDlpYouTube.h
    src/GUI/VideoTitle.cpp
    src/GUI/VideoTitle.h
    src/GUI/DownloadFormatPredictor.cpp
    src/GUI/DownloadFormatPredictor.h
    src/GUI/DownloadOptions.h
    src/GUI/DownloadRunner.cpp
    src/GUI/DownloadRunner.h
    src/GUI/FooterDownloadSpeedMeter.cpp
    src/GUI/FooterDownloadSpeedMeter.h
    src/GUI/DownloadSpeedHistory.cpp
    src/GUI/DownloadSpeedHistory.h
    src/GUI/Dropdown.cpp
    src/GUI/Dropdown.h
    src/GUI/FoldoutPanel.cpp
    src/GUI/FoldoutPanel.h
    src/GUI/BrowserDiagnostics.cpp
    src/GUI/BrowserDiagnostics.h
    src/GUI/CardChrome.cpp
    src/GUI/CardChrome.h
    src/GUI/EmojiText.cpp
    src/GUI/EmojiText.h
    src/GUI/EmojiBackendFactory.cpp
    src/GUI/IEmojiBackend.h
    src/GUI/SpriteEmojiBackend.cpp
    src/GUI/SpriteEmojiBackend.h
    src/GUI/LinkInfoLoader.cpp
    src/GUI/LinkInfoLoader.h
    src/GUI/LinkCardNode.cpp
    src/GUI/LinkCardNode.h
    src/GUI/LinkCardGroupNode.cpp
    src/GUI/LinkCardGroupNode.h
    src/GUI/LinkGroupInfoLoader.cpp
    src/GUI/LinkGroupInfoLoader.h
    src/GUI/DownloaderListItem.cpp
    src/GUI/DownloaderListItem.h
    src/GUI/LoadingSpinner.cpp
    src/GUI/LoadingSpinner.h
    src/GUI/PathField.cpp
    src/GUI/PathField.h
    src/GUI/Scrollbar.cpp
    src/GUI/Scrollbar.h
    src/GUI/ShortcutRouter.cpp
    src/GUI/ShortcutRouter.h
    src/GUI/Tooltip.cpp
    src/GUI/Tooltip.h
    src/GUI/TaskbarProgress.cpp
    src/GUI/TaskbarProgress.h
    src/GUI/UiClip.cpp
    src/GUI/UiClip.h
    src/GUI/UndoStack.cpp
    src/GUI/UndoStack.h
    src/GUI/ToolPaths.cpp
    src/GUI/ToolPaths.h
    "${TINYFILEDIALOGS_DIR}/tinyfiledialogs.c"
)

target_link_libraries(4KDowner PRIVATE raylib)

set(_4kd_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in"
    "${_4kd_generated_dir}/Version.h" @ONLY)
target_include_directories(4KDowner BEFORE PRIVATE "${_4kd_generated_dir}")

if(WIN32)
    set(_4kd_icon_path "${CMAKE_CURRENT_SOURCE_DIR}/assets/logo/logo.ico")
    file(TO_NATIVE_PATH "${_4kd_icon_path}" _4kd_icon_native)
    string(REPLACE "\\" "\\\\" _4kd_icon_native "${_4kd_icon_native}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc.in"
        "${_4kd_generated_dir}/app.rc" @ONLY)
    target_link_libraries(4KDowner PRIVATE Shell32 Ole32 winhttp)
    target_sources(4KDowner PRIVATE "${_4kd_generated_dir}/app.rc")
    if(MSVC)
        target_link_options(4KDowner PRIVATE "/SUBSYSTEM:WINDOWS" "/ENTRY:mainCRTStartup")
    endif()
endif()

if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(DBUS QUIET dbus-1)
        if(DBUS_FOUND)
            target_compile_definitions(4KDowner PRIVATE FOURKDOWNER_HAS_DBUS=1)
            target_include_directories(4KDowner PRIVATE ${DBUS_INCLUDE_DIRS})
            target_link_libraries(4KDowner PRIVATE ${DBUS_LIBRARIES})
        endif()
    endif()
endif()

target_include_directories(4KDowner BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/shim")
target_include_directories(4KDowner PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${TINYFILEDIALOGS_DIR}"
)

if(MSVC)
    target_compile_options(4KDowner PRIVATE /W4 /FS)
else()
    target_compile_options(4KDowner PRIVATE -Wall -Wextra -Wpedantic)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackagePortable.cmake")
