cmake_minimum_required(VERSION 4.2)

project(MiniKDEConnect VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 20)

add_compile_definitions(MINIKDECONNECT_VERSION=\"${PROJECT_VERSION}\")

set(NXLINK_HOST "" CACHE STRING "IP address of the PC running nxlink (leave empty to let libnx get the host from the launch argument; only works for the applet if launched with nxlink)")
if(NXLINK_HOST)
    add_compile_definitions(NXLINK_HOST=\"${NXLINK_HOST}\")
else ()
    add_compile_definitions(NXLINK_HOST=\"\")
endif()

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

# Custom in-tree MbedTLS build for size optimization and Switch compatibility
add_subdirectory(sysmodule/libs/mbedtls)
set(MBEDTLS_LIBRARIES MBEDTLS::mbedtls MBEDTLS::mbedx509 MBEDTLS::mbedcrypto)
set(MBEDTLS_INCLUDE_DIRS "") # Handled by target_link_libraries

find_package(Threads REQUIRED)

add_subdirectory(common)
add_subdirectory(sysmodule)
add_subdirectory(tools)

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    add_subdirectory(overlay)
endif()
