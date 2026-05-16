cmake_minimum_required(VERSION 4.2)

project(MiniKDEConnect VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 20)

add_compile_definitions(MINIKDECONNECT_VERSION=\"${PROJECT_VERSION}\")

include(FetchContent)

find_package(nlohmann_json QUIET)
if (NOT ${nlohmann_json_FOUND})
    FetchContent_Declare(
            nlohmann_json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG v3.11.3
            OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# Use pkg-config to find MbedTLS (for devkitpro compatibility)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MBEDTLS REQUIRED IMPORTED_TARGET mbedtls mbedx509 mbedcrypto)

find_package(Threads REQUIRED)

add_subdirectory(common)
add_subdirectory(sysmodule)
add_subdirectory(overlay)
add_subdirectory(tools)
