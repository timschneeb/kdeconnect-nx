# Portlibs dependencies

set(PORTLIBS_PATH ${DEVKITPRO}/portlibs/switch)
include_directories(${PORTLIBS_PATH}/include)

# Find libraries
find_library(CURL_LIB curl REQUIRED)
find_library(ZLIB_LIB z REQUIRED)
find_library(MINIZIP_LIB minizip REQUIRED)

add_library(libultrahand STATIC
    libultrahand/libtesla/source/tesla.cpp
    libultrahand/libultra/source/audio.cpp
    libultrahand/libultra/source/debug_funcs.cpp
    libultrahand/libultra/source/download_funcs.cpp
    libultrahand/libultra/source/exception_wrap.cpp
    libultrahand/libultra/source/get_funcs.cpp
    libultrahand/libultra/source/global_vars.cpp
    libultrahand/libultra/source/haptics.cpp
    libultrahand/libultra/source/hex_funcs.cpp
    libultrahand/libultra/source/ini_funcs.cpp
    libultrahand/libultra/source/json_funcs.cpp
    libultrahand/libultra/source/list_funcs.cpp
    libultrahand/libultra/source/mod_funcs.cpp
    libultrahand/libultra/source/path_funcs.cpp
    libultrahand/libultra/source/string_funcs.cpp
    libultrahand/libultra/source/tsl_utils.cpp
    libultrahand/common/cJSON.c
)

target_compile_definitions(libultrahand PUBLIC
    USING_WIDGET_DIRECTIVE=1
    USE_EXCEPTION_WRAP=1
    UI_OVERRIDE_PATH="/config/kdeconnect/"
)

target_include_directories(libultrahand PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/libultrahand/libtesla/include
    ${CMAKE_CURRENT_LIST_DIR}/libultrahand/libultra/include
    ${CMAKE_CURRENT_LIST_DIR}/libultrahand/common
    ${PORTLIBS_PATH}/include
)

# Handle dependencies
target_link_libraries(libultrahand PUBLIC
    ${CURL_LIB}
    ${ZLIB_LIB}
    ${MINIZIP_LIB}
    ${MBEDTLS_LIBRARIES}
)

