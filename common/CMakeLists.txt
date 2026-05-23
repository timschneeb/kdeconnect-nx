add_library(minikdeconnect_common STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/utils/logger.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/utils/nxlink_sink.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/kdec/ipc_client.cpp
        src/constants.h
)

target_include_directories(minikdeconnect_common
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/src/utils
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(minikdeconnect_common PUBLIC NXLINK_ENABLED)
endif()