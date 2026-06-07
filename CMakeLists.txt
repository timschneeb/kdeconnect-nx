cmake_minimum_required(VERSION 3.31.6)

find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_LAST_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty=-dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknown")
endif()
if(GIT_LAST_TAG)
    string(REGEX REPLACE "^v" "" GIT_VERSION "${GIT_LAST_TAG}")
else()
    set(GIT_LAST_TAG "unknown")
    set(GIT_VERSION "0.0.0")
endif()
if(NOT GIT_DESCRIBE)
    set(GIT_DESCRIBE "unknown")
endif()

project(MiniKDEConnect VERSION ${GIT_VERSION})

set(CMAKE_CXX_STANDARD 20)

add_compile_definitions(MINIKDECONNECT_VERSION=\"${PROJECT_VERSION}\")
add_compile_definitions(
    GIT_COMMIT_HASH=\"${GIT_COMMIT_HASH}\"
    GIT_LAST_TAG=\"${GIT_LAST_TAG}\"
    GIT_DESCRIBE=\"${GIT_DESCRIBE}\"
)

file(READ ${CMAKE_SOURCE_DIR}/sysmodule/toolbox.json _toolbox_contents)
string(JSON MINIKDECONNECT_TID GET ${_toolbox_contents} tid)
add_compile_definitions(SYSMODULE_TITLE_ID=0x${MINIKDECONNECT_TID})

set(NXLINK_HOST "" CACHE STRING "IP address of the PC running nxlink (leave empty to let libnx get the host from the launch argument; only works for the applet if launched with nxlink)")
set(SWITCH_FTP_BASE_URL "" CACHE STRING "Base FTP URL for upload targets, e.g. ftp://anonymous:@192.168.1.1:5001 (no trailing slash)")
if(NXLINK_HOST)
    add_compile_definitions(NXLINK_HOST=\"${NXLINK_HOST}\")
else ()
    add_compile_definitions(NXLINK_HOST=\"\")
endif()

# For puncover: -fstack-usage is incompatible with -flto (LTO defers code-gen to
# link time, so GCC never reaches the RTL pass that emits .su files).
# Enable this option to get .su files; it automatically suppresses -flto everywhere.
option(STACK_USAGE "Generate .su files for puncover stack analysis (disables LTO)" OFF)

if(STACK_USAGE)
    add_compile_options(-fstack-usage)
endif()

find_package(Threads REQUIRED)

add_subdirectory(common)
add_subdirectory(sysmodule)
add_subdirectory(sysmodule-am-mitm)
add_subdirectory(tools/nxlink)

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    add_subdirectory(overlay)

    set(STAGE_DIR "${CMAKE_BINARY_DIR}/stage")
    add_custom_target(stage
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${STAGE_DIR}/atmosphere/contents"
            "${STAGE_DIR}/switch/.overlays"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/sysmodule/${MINIKDECONNECT_TID}"
            "${STAGE_DIR}/atmosphere/contents/${MINIKDECONNECT_TID}"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_BINARY_DIR}/overlay/MiniKDEConnect_overlay.ovl"
            "${STAGE_DIR}/switch/.overlays/kdeconnect-nx.ovl"
        DEPENDS MiniKDEConnect_sysmodule_nsp MiniKDEConnect_overlay
        COMMENT "Staging SD card layout to ${STAGE_DIR}"
    )

    if(DEFINED ENV{GITHUB_ACTIONS})
        set(RELEASE_ZIP "${CMAKE_BINARY_DIR}/kdeconnect-nx-dev.zip")
        set(DEBUG_ZIP   "${CMAKE_BINARY_DIR}/debug-symbols.zip")
    else()
        set(RELEASE_ZIP "${CMAKE_BINARY_DIR}/kdeconnect-nx-release-v${PROJECT_VERSION}.zip")
        set(DEBUG_ZIP   "${CMAKE_BINARY_DIR}/debug-symbols-v${PROJECT_VERSION}.zip")
    endif()

    set(DEBUG_STAGE_DIR "${CMAKE_BINARY_DIR}/debug-stage")

    add_custom_target(release
        # SD card layout ZIP (paths relative to stage/).
        COMMAND ${CMAKE_COMMAND} -E tar cf "${RELEASE_ZIP}" --format=zip -- .
        # Flat debug symbols ZIP: copy ELFs into a temp dir first so there are
        # no subdirectory paths inside the archive.
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEBUG_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_BINARY_DIR}/sysmodule/MiniKDEConnect_sysmodule.elf"
                "${DEBUG_STAGE_DIR}/MiniKDEConnect_sysmodule.elf"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_BINARY_DIR}/overlay/MiniKDEConnect_overlay.elf"
                "${DEBUG_STAGE_DIR}/MiniKDEConnect_overlay.elf"
        COMMAND ${CMAKE_COMMAND} -E chdir "${DEBUG_STAGE_DIR}"
                ${CMAKE_COMMAND} -E tar cf "${DEBUG_ZIP}" --format=zip --
                MiniKDEConnect_sysmodule.elf
                MiniKDEConnect_overlay.elf
        DEPENDS stage
        WORKING_DIRECTORY "${STAGE_DIR}"
        COMMENT "Packaging ${RELEASE_ZIP} and ${DEBUG_ZIP}"
    )
endif()
