cmake_minimum_required(VERSION 3.10)
project(zlib_custom C)

include(FetchContent)
cmake_policy(SET CMP0169 OLD)

FetchContent_Declare(
    zlib_src
    URL      https://www.zlib.net/fossils/zlib-1.3.1.tar.gz
    URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_GetProperties(zlib_src)
if(NOT zlib_src_POPULATED)
    FetchContent_Populate(zlib_src)
endif()

set(ZLIB_DIR ${zlib_src_SOURCE_DIR})

# Only the inflate side is needed; deflate/gz/compress are unused (libpng's
# write path is excluded from this build and --gc-sections removes the rest).
add_library(zlib_static STATIC
    ${ZLIB_DIR}/adler32.c
    ${ZLIB_DIR}/crc32.c
    ${ZLIB_DIR}/inffast.c
    ${ZLIB_DIR}/inflate.c
    ${ZLIB_DIR}/inftrees.c
    ${ZLIB_DIR}/zutil.c
)

target_include_directories(zlib_static PUBLIC ${ZLIB_DIR})

target_compile_options(zlib_static PRIVATE
    $<$<NOT:$<BOOL:${STACK_USAGE}>>:-flto>
    # +crc enables the ARMv8 CRC32 hardware instruction (__ARM_FEATURE_CRC32),
    # which zlib uses instead of the large CRC32 lookup tables
    $<$<STREQUAL:${CMAKE_SYSTEM_NAME},NintendoSwitch>:-march=armv8-a+crc>
    -Os -ffunction-sections -fdata-sections)
