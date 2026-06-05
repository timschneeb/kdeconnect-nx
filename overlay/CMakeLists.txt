add_subdirectory(libs)

# Define target
add_executable(MiniKDEConnect_overlay
    src/main.cpp
    src/gui/commands_gui.cpp
    src/gui/device_gui.cpp
    src/gui/main_gui.cpp
    src/gui/media_gui.cpp
    src/gui/settings_gui.cpp
    src/gui/notification_style_gui.cpp
    src/gui/volume_gui.cpp
    src/utils/symbols.h
    src/gui/error_widget.cpp
    src/gui/error_widget.h
    src/gui/elements/media_button_row.cpp
    src/gui/elements/media_button_row.h
    src/gui/elements/media_button_icon.cpp
    src/gui/elements/media_button_icon.h
    src/gui/elements/media_seek_bar.cpp
    src/gui/elements/media_seek_bar.h
    src/gui/elements/media_title_bar.cpp
    src/gui/elements/media_title_bar.h
)

# Album art decoding: stb_image (JPEG/PNG) + libwebp (WebP)
target_include_directories(MiniKDEConnect_overlay PRIVATE libs/stb)
find_path(DKP_PORTLIBS_INCLUDE webp/decode.h REQUIRED)
target_include_directories(MiniKDEConnect_overlay PRIVATE ${DKP_PORTLIBS_INCLUDE})
find_library(WEBP_LIB webp REQUIRED)
target_link_libraries(MiniKDEConnect_overlay PRIVATE ${WEBP_LIB})

# Optimization and build flags
target_compile_options(MiniKDEConnect_overlay PRIVATE
    -Wall -Os -flto
    $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
    -std=c++26 -fno-exceptions -fno-rtti -Wno-dangling-else -ffast-math
    #-fno-unwind-tables -fno-asynchronous-unwind-tables
)

target_link_options(MiniKDEConnect_overlay PRIVATE
    -Wl,--gc-sections -Wl,--as-needed -flto
    #-Wl,-wrap,__cxa_throw -Wl,-wrap,_Unwind_Resume -Wl,-wrap,__gxx_personality_v0
    $<$<CONFIG:Debug>:-Wl,--wrap,malloc>
    $<$<CONFIG:Debug>:-Wl,--wrap,free>
    $<$<CONFIG:Debug>:-Wl,--wrap,realloc>
    $<$<CONFIG:Debug>:-Wl,--wrap,calloc>
    $<$<CONFIG:Debug>:-Wl,--wrap,memalign>
    $<$<CONFIG:Debug>:-Wl,--wrap,posix_memalign>
    $<$<CONFIG:Debug>:-Wl,--wrap,aligned_alloc>
    $<$<CONFIG:Debug>:-Wl,--wrap,valloc>
)

target_include_directories(MiniKDEConnect_overlay PRIVATE ${CMAKE_SOURCE_DIR}/common)
target_link_libraries(MiniKDEConnect_overlay PRIVATE minikdeconnect_common libultrahand)

nx_generate_nacp(MiniKDEConnect_overlay.nacp
        NAME "KDE Connect NX"
        AUTHOR "timschneeb"
        VERSION "${CMAKE_PROJECT_VERSION}"
)

# Hook NACP generation into the build graph so it exists before POST_BUILD runs
add_custom_target(MiniKDEConnect_overlay_nacp
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/MiniKDEConnect_overlay.nacp"
)
add_dependencies(MiniKDEConnect_overlay MiniKDEConnect_overlay_nacp)

# Create NRO, then rename to .ovl and append signature
add_custom_command(
    TARGET MiniKDEConnect_overlay POST_BUILD
    COMMAND ${NX_ELF2NRO_EXE} MiniKDEConnect_overlay.elf MiniKDEConnect_overlay.ovl --nacp=MiniKDEConnect_overlay.nacp
    COMMAND ${CMAKE_COMMAND} -E echo "ULTR" >> MiniKDEConnect_overlay.ovl
    COMMENT "Generating .ovl and appending Ultrahand signature"
)

# Upload to Switch via FTP
find_program(CURL_EXE curl REQUIRED NO_CMAKE_FIND_ROOT_PATH)
add_custom_target(MiniKDEConnect_overlay_upload
    COMMAND ${CURL_EXE} -T MiniKDEConnect_overlay.ovl
            "${SWITCH_FTP_BASE_URL}/switch/.overlays/kdeconnect-nx.ovl"
    DEPENDS MiniKDEConnect_overlay
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Uploading overlay to Switch"
)
