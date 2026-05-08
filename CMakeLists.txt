cmake_minimum_required(VERSION 4.2)

project(MiniKDEConnect VERSION 0.0.1)

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

if(CMAKE_SYSTEM_NAME MATCHES "NintendoSwitch")
    set(PLATFORM_MAIN main_switch.cpp)
else()
    set(PLATFORM_MAIN main.cpp)
endif()

add_executable(MiniKDEConnect
  ${PLATFORM_MAIN}
  src/kdeconnect_client.cpp
        src/mdns/mdns_discovery.cpp
  src/network_packet.cpp
  src/storage.cpp
  src/tls.cpp
  src/utils/network_util.cpp
  src/utils/logger.cpp
  src/plugins/ping_plugin.cpp
  src/plugins/battery_plugin.cpp
  src/plugins/notification_plugin.cpp
  src/plugins/find_my_phone_plugin.cpp
  src/plugins/mpris_plugin.cpp
  src/plugins/system_volume_plugin.cpp
  src/plugins/share_plugin.cpp
  src/plugins/plugin_registry.cpp
)

target_include_directories(MiniKDEConnect PRIVATE src)

target_link_libraries(MiniKDEConnect PRIVATE nlohmann_json::nlohmann_json PkgConfig::MBEDTLS Threads::Threads)

if(CMAKE_SYSTEM_NAME MATCHES "NintendoSwitch")

nx_generate_nacp(MiniKDEConnect.nacp
   NAME "kdeconnect-nx"
   AUTHOR "timschneeb"
   VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
)

nx_create_nro(MiniKDEConnect
    NACP MiniKDEConnect.nacp
    # ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg
)

INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/kdeconnect-nx.nro DESTINATION . COMPONENT kdeconnect-nx)

endif()
