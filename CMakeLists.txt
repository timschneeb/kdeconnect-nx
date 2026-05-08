cmake_minimum_required(VERSION 4.2)
project(MiniKDEConnect)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

find_package(MbedTLS REQUIRED)
find_package(Threads REQUIRED)

add_executable(MiniKDEConnect
  main.cpp
  src/kdeconnect_client.cpp
  src/network_packet.cpp
  src/storage.cpp
  src/tls.cpp
  src/logger.cpp
  src/plugins/ping_plugin.cpp
  src/plugins/plugin_registry.cpp
)

target_include_directories(MiniKDEConnect PRIVATE src)

target_link_libraries(MiniKDEConnect PRIVATE nlohmann_json::nlohmann_json MbedTLS::mbedtls MbedTLS::mbedx509 MbedTLS::mbedcrypto Threads::Threads)
