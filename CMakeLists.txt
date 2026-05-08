cmake_minimum_required(VERSION 4.2)
project(MiniKDEConnect)

set(CMAKE_CXX_STANDARD 20)

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

add_executable(MiniKDEConnect
  main.cpp
  src/kdeconnect_client.cpp
  src/network_packet.cpp
  src/storage.cpp
  src/tls.cpp
  src/utils/network_util.cpp
  src/utils/logger.cpp
  src/plugins/ping_plugin.cpp
  src/plugins/plugin_registry.cpp
)

target_include_directories(MiniKDEConnect PRIVATE src)

target_link_libraries(MiniKDEConnect PRIVATE nlohmann_json::nlohmann_json PkgConfig::MBEDTLS Threads::Threads)
