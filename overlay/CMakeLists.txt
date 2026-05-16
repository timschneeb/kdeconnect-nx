option(BUILD_OVERLAY "Build the Tesla overlay" OFF)

if(BUILD_OVERLAY)
    add_executable(MiniKDEConnect_overlay
        ${CMAKE_CURRENT_LIST_DIR}/main_overlay.cpp
    )
    # TODO: Link against Tesla/overlay helper libraries once integrated.
endif()

