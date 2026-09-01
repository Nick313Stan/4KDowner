# Portable staging: exe + assets + optional packages/ffmpeg + packages/ytdown + packages/nodejs
# Usage:
#   cmake --build build-windows --config Release --target package-portable
#   cmake --build build-windows --config Release --target package-archive

if(WIN32)
    set(_4kd_release_name "4KDowner-${PROJECT_VERSION}-windows-x64")
    set(_4kd_archive_ext "zip")
    set(_4kd_exe_name "4KDowner.exe")
else()
    set(_4kd_release_name "4KDowner-${PROJECT_VERSION}-linux-x64")
    set(_4kd_archive_ext "tar.gz")
    set(_4kd_exe_name "4KDowner")
endif()

set(4KDOWNER_PACKAGE_DIR
    "${CMAKE_SOURCE_DIR}/../yCompiled/${_4kd_release_name}"
    CACHE PATH "Output directory for package-portable")
set(4KDOWNER_PACKAGES_ROOT "${CMAKE_SOURCE_DIR}/../packages" CACHE PATH
    "Source packages (ffmpeg, ytdown)")
set(4KDOWNER_ARCHIVE_PATH
    "${CMAKE_SOURCE_DIR}/../yCompiled/${_4kd_release_name}.${_4kd_archive_ext}"
    CACHE FILEPATH "Output archive for package-archive")

function(fourkdowner_stage_portable)
    set(out_dir "${4KDOWNER_PACKAGE_DIR}")
    set(pkg_root "${4KDOWNER_PACKAGES_ROOT}")
    set(assets_src "${CMAKE_SOURCE_DIR}/assets")
    get_filename_component(_4kd_package_folder_name "${out_dir}" NAME)

    add_custom_target(package-portable
        DEPENDS 4KDowner
        COMMAND ${CMAKE_COMMAND} -E echo "Staging portable to ${out_dir}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${out_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:4KDowner>" "${out_dir}/${_4kd_exe_name}"
        COMMAND ${CMAKE_COMMAND}
            -DSTAGE_OUT=${out_dir}
            -DASSETS_SRC=${assets_src}
            -DPKG_ROOT=${pkg_root}
            -DWIN32_BUILD=$<BOOL:${WIN32}>
            -P "${CMAKE_SOURCE_DIR}/cmake/StagePortableFiles.cmake"
        COMMENT "package-portable: ${out_dir}"
        VERBATIM
    )

    if(WIN32)
        add_custom_target(package-archive
            DEPENDS package-portable
            COMMAND ${CMAKE_COMMAND} -E rm -f "${4KDOWNER_ARCHIVE_PATH}"
            COMMAND ${CMAKE_COMMAND} -E chdir "${out_dir}/.."
                ${CMAKE_COMMAND} -E tar cf "${4KDOWNER_ARCHIVE_PATH}" --format=zip
                    "${_4kd_package_folder_name}"
            COMMENT "package-archive: ${4KDOWNER_ARCHIVE_PATH}"
            VERBATIM
        )
    else()
        add_custom_target(package-archive
            DEPENDS package-portable
            COMMAND ${CMAKE_COMMAND} -E rm -f "${4KDOWNER_ARCHIVE_PATH}"
            COMMAND ${CMAKE_COMMAND} -E chdir "${out_dir}/.."
                ${CMAKE_COMMAND} -E tar czf "${4KDOWNER_ARCHIVE_PATH}"
                    "${_4kd_package_folder_name}"
            COMMENT "package-archive: ${4KDOWNER_ARCHIVE_PATH}"
            VERBATIM
        )
    endif()
endfunction()

fourkdowner_stage_portable()
