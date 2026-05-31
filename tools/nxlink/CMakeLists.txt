cmake_minimum_required(VERSION 3.10)

# Find required packages for nxlink
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)

# nxlink executable
add_executable(nxlink nxlink.c)

target_link_libraries(nxlink PRIVATE ZLIB::ZLIB)

# Link against system socket libraries on Windows
if(WIN32)
    target_link_libraries(nxlink PRIVATE ws2_32)
endif()

# Install target
install(TARGETS nxlink DESTINATION bin)

