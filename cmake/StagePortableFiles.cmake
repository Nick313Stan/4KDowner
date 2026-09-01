# Invoked by package-portable custom target (-P).
# Expected -D: STAGE_OUT, ASSETS_SRC, PKG_ROOT, WIN32_BUILD

if(NOT STAGE_OUT)
    message(FATAL_ERROR "STAGE_OUT not set")
endif()

if(EXISTS "${ASSETS_SRC}")
    file(COPY "${ASSETS_SRC}" DESTINATION "${STAGE_OUT}")
endif()

# ffmpeg
set(_ffmpeg_bin "${PKG_ROOT}/ffmpeg/bin")
if(EXISTS "${_ffmpeg_bin}")
    file(MAKE_DIRECTORY "${STAGE_OUT}/packages/ffmpeg/bin")
    if(WIN32_BUILD)
        foreach(_tool ffmpeg.exe ffprobe.exe)
            if(EXISTS "${_ffmpeg_bin}/${_tool}")
                file(COPY "${_ffmpeg_bin}/${_tool}" DESTINATION "${STAGE_OUT}/packages/ffmpeg/bin")
            endif()
        endforeach()
    else()
        foreach(_tool ffmpeg ffprobe)
            if(EXISTS "${_ffmpeg_bin}/${_tool}")
                file(COPY "${_ffmpeg_bin}/${_tool}" DESTINATION "${STAGE_OUT}/packages/ffmpeg/bin")
            elseif(EXISTS "${_ffmpeg_bin}/${_tool}.exe")
                file(COPY "${_ffmpeg_bin}/${_tool}.exe" DESTINATION "${STAGE_OUT}/packages/ffmpeg/bin")
            endif()
        endforeach()
    endif()
endif()

# ytdown / portable python
set(_ytdown_src "${PKG_ROOT}/ytdown")
if(EXISTS "${_ytdown_src}")
    file(COPY "${_ytdown_src}" DESTINATION "${STAGE_OUT}/packages")
endif()

# portable Node.js (yt-dlp JS runtime) — native binary only
set(_nodejs_bin "${PKG_ROOT}/nodejs/bin")
if(EXISTS "${_nodejs_bin}")
    file(MAKE_DIRECTORY "${STAGE_OUT}/packages/nodejs/bin")
    if(WIN32_BUILD)
        if(EXISTS "${_nodejs_bin}/node.exe")
            file(COPY "${_nodejs_bin}/node.exe" DESTINATION "${STAGE_OUT}/packages/nodejs/bin")
        endif()
    else()
        if(EXISTS "${_nodejs_bin}/node")
            file(COPY "${_nodejs_bin}/node" DESTINATION "${STAGE_OUT}/packages/nodejs/bin")
        endif()
    endif()
    if(EXISTS "${PKG_ROOT}/nodejs/VERSION")
        file(COPY "${PKG_ROOT}/nodejs/VERSION" DESTINATION "${STAGE_OUT}/packages/nodejs")
    endif()
endif()

message(STATUS "Staged portable tree at ${STAGE_OUT}")
