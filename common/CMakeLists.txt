set(MINIKDECONNECT_COMMON_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/utils/logger.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/utils/nxlink_sink.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/constants.h
)

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    add_library(minikdeconnect_common STATIC
        ${MINIKDECONNECT_COMMON_SOURCES}
        ${CMAKE_CURRENT_LIST_DIR}/src/kdec/ipc_client.cpp
    )
else()
    add_library(minikdeconnect_common STATIC ${MINIKDECONNECT_COMMON_SOURCES})
endif()

target_include_directories(minikdeconnect_common
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/src/utils
)

target_compile_options(minikdeconnect_common PRIVATE -flto -Os -ffunction-sections -fdata-sections)
target_link_options(minikdeconnect_common PRIVATE -Wl,--gc-sections)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(minikdeconnect_common PUBLIC NXLINK_ENABLED)
endif()