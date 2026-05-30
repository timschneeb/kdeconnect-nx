# Custom MbedTLS build for size optimization
add_subdirectory(mbedtls)

# Lightweight JSON library
set(YYJSON_DISABLE_UTILS ON CACHE BOOL "" FORCE)
set(YYJSON_DISABLE_NON_STANDARD ON CACHE BOOL "" FORCE)
set(YYJSON_DISABLE_INCR_READER ON CACHE BOOL "" FORCE)
set(YYJSON_DISABLE_FAST_FP_CONV ON CACHE BOOL "" FORCE)
set(YYJSON_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(yyjson)

target_compile_options(yyjson PRIVATE
    $<$<NOT:$<BOOL:${STACK_USAGE}>>:-flto>
    -Os -ffunction-sections -fdata-sections)

# Tiny vsnprintf/sprintf replacement
include(FetchContent)
FetchContent_Declare(
    printf_library
    GIT_REPOSITORY https://github.com/eyalroz/printf.git
    GIT_TAG        v6.1.0
)
set(PRINTF_ALIAS_STANDARD_FUNCTION_NAMES_SOFT OFF CACHE BOOL "" FORCE)
set(PRINTF_ALIAS_STANDARD_FUNCTION_NAMES_HARD OFF CACHE BOOL "" FORCE)
set(PRINTF_SUPPORT_EXPONENTIAL_SPECIFIERS OFF CACHE BOOL "" FORCE)
set(PRINTF_SUPPORT_MSVC_STYLE_INTEGER_SPECIFIERS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(printf_library)

target_compile_options(printf PRIVATE
    $<$<NOT:$<BOOL:${STACK_USAGE}>>:-flto>
    -Os -ffunction-sections -fdata-sections)
