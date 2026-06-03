cmake_minimum_required(VERSION 3.31.6)

project(MiniKDEConnect VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 20)

add_compile_definitions(MINIKDECONNECT_VERSION=\"${PROJECT_VERSION}\")

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
add_subdirectory(tools/nxlink)

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    add_subdirectory(overlay)

    set(STAGE_DIR "${CMAKE_BINARY_DIR}/stage")
    add_custom_target(publish
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
endif()
