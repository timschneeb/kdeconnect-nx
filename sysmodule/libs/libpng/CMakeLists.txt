cmake_minimum_required(VERSION 3.10)
project(libpng_custom C)

include(FetchContent)
cmake_policy(SET CMP0169 OLD)

FetchContent_Declare(
    libpng_src
    URL      https://download.sourceforge.net/libpng/libpng-1.6.48.tar.xz
    URL_HASH SHA256=46fd06ff37db1db64c0dc288d78a3f5efd23ad9ac41561193f983e20937ece03
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_GetProperties(libpng_src)
if(NOT libpng_src_POPULATED)
    FetchContent_Populate(libpng_src)
endif()

set(LIBPNG_DIR ${libpng_src_SOURCE_DIR})

# Read-only sources; write files (pngwrite/wio/wtran/wutil) and the push
# (progressive) reader (pngpread) are excluded — they are entirely wrapped
# in their respective feature guards and are unused in this build.
add_library(png_static STATIC
    ${LIBPNG_DIR}/png.c
    ${LIBPNG_DIR}/pngerror.c
    ${LIBPNG_DIR}/pngget.c
    ${LIBPNG_DIR}/pngmem.c
    ${LIBPNG_DIR}/pngread.c
    ${LIBPNG_DIR}/pngrio.c
    ${LIBPNG_DIR}/pngrtran.c
    ${LIBPNG_DIR}/pngrutil.c
    ${LIBPNG_DIR}/pngset.c
    ${LIBPNG_DIR}/pngtrans.c
    # AArch64 always has NEON; pngpriv.h sets PNG_ARM_NEON_OPT automatically
    # from __ARM_NEON, so these definitions are required.
    ${LIBPNG_DIR}/arm/arm_init.c
    ${LIBPNG_DIR}/arm/filter_neon_intrinsics.c
    ${LIBPNG_DIR}/arm/palette_neon_intrinsics.c
)

# CMAKE_CURRENT_LIST_DIR must come first so our pnglibconf.h is found
# before any generated copy that might exist in the source tree.
target_include_directories(png_static PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${LIBPNG_DIR}
)

target_link_libraries(png_static PUBLIC zlib_static)

target_compile_options(png_static PRIVATE
    $<$<NOT:$<BOOL:${STACK_USAGE}>>:-flto> -ffast-math
    -Os -ffunction-sections -fdata-sections)
