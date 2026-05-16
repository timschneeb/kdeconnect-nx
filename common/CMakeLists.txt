add_library(minikdeconnect_common STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/utils/logger.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/utils/nxlink_sink.cpp
)

target_include_directories(minikdeconnect_common
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/src/utils
)