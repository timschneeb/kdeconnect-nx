option(BUILD_APPLET "Build the Switch testing applet" ON)
option(BUILD_SYSMODULE "Build the Switch sysmodule" ON)
option(BUILD_CLI "Build the desktop test CLI app" ON)

set(MINIKDECONNECT_SYSMODULE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/mdns/mdns_discovery.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/net/kdeconnect_client.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/net/network_packet.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/net/tls.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/utils/storage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/utils/settings_store.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/utils/network_util.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/plugins/ping_plugin.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/plugins/battery_plugin.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/plugins/notification_plugin.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/plugins/find_my_phone_plugin.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/plugins/mpris_plugin.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/plugins/system_volume_plugin.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/plugins/share_plugin.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/plugins/run_command_plugin.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/plugins/mousepad_plugin.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/plugins/plugin_registry.cpp
)

set(MINIKDECONNECT_NX_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/nx_application.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ipc/ipc_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ipc/ipc_server.c
)

set(MINIKDECONNECT_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/libs
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/utils
    ${CMAKE_CURRENT_LIST_DIR}/src/ipc
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/common/src
    ${CMAKE_SOURCE_DIR}/common/src/utils
)

set(MINIKDECONNECT_LINK_LIBS
    minikdeconnect_common
    nlohmann_json::nlohmann_json
    ${MBEDTLS_LIBRARIES}
    Threads::Threads
)

set(MINIKDECONNECT_TOOLBOX_JSON ${CMAKE_CURRENT_LIST_DIR}/toolbox.json)
set(MINIKDECONNECT_SYSMODULE_JSON ${CMAKE_CURRENT_LIST_DIR}/sysmodule.json)
file(READ ${MINIKDECONNECT_TOOLBOX_JSON} MINIKDECONNECT_TOOLBOX_CONTENTS)
string(JSON MINIKDECONNECT_TID GET ${MINIKDECONNECT_TOOLBOX_CONTENTS} tid)

if(CMAKE_SYSTEM_NAME MATCHES "NintendoSwitch")
    if(BUILD_APPLET)
        add_executable(MiniKDEConnect_applet
            ${CMAKE_CURRENT_LIST_DIR}/src/main_applet.cpp
            ${MINIKDECONNECT_SYSMODULE_SOURCES}
            ${MINIKDECONNECT_NX_SOURCES}
        )

        target_compile_options(MiniKDEConnect_applet PRIVATE -flto -fno-exceptions -Oz -ffunction-sections -fdata-sections)
        target_link_options(MiniKDEConnect_applet PRIVATE -Wl,--gc-sections)

        target_include_directories(MiniKDEConnect_applet PRIVATE ${MINIKDECONNECT_INCLUDE_DIRS})
        target_link_libraries(MiniKDEConnect_applet PRIVATE ${MINIKDECONNECT_LINK_LIBS})

        nx_generate_nacp(MiniKDEConnect_applet.nacp
            NAME "kdeconnect-nx"
            AUTHOR "timschneeb"
            VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
        )

        nx_create_nro(MiniKDEConnect_applet NACP MiniKDEConnect_applet.nacp)
    endif()

    if(BUILD_SYSMODULE)
        add_executable(MiniKDEConnect_sysmodule
            ${CMAKE_CURRENT_LIST_DIR}/src/main_sysmodule.cpp
            ${MINIKDECONNECT_SYSMODULE_SOURCES}
            ${MINIKDECONNECT_NX_SOURCES}
        )
        target_include_directories(MiniKDEConnect_sysmodule PRIVATE ${MINIKDECONNECT_INCLUDE_DIRS})
        target_compile_definitions(MiniKDEConnect_sysmodule PRIVATE MINIKDECONNECT_SYSMODULE)
        target_compile_options(MiniKDEConnect_sysmodule PRIVATE -flto -fno-exceptions -Os -ffunction-sections -fdata-sections)
        target_link_libraries(MiniKDEConnect_sysmodule PRIVATE ${MINIKDECONNECT_LINK_LIBS} nx)
        target_link_options(MiniKDEConnect_sysmodule PRIVATE -Wl,--gc-sections -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/MiniKDEConnect_sysmodule.map)

        file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${MINIKDECONNECT_TID})
        file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${MINIKDECONNECT_TID}/flags)
        configure_file(${MINIKDECONNECT_SYSMODULE_JSON} ${CMAKE_CURRENT_BINARY_DIR}/sysmodule.json)

        add_custom_command(
            TARGET MiniKDEConnect_sysmodule POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                ${MINIKDECONNECT_TOOLBOX_JSON}
                ${CMAKE_CURRENT_BINARY_DIR}/${MINIKDECONNECT_TID}/toolbox.json
            COMMAND ${CMAKE_COMMAND} -E touch
                ${CMAKE_CURRENT_BINARY_DIR}/${MINIKDECONNECT_TID}/flags/boot2.flag
        )

        nx_create_exefs(MiniKDEConnect_sysmodule
            OUTPUT ${MINIKDECONNECT_TID}/exefs.nsp
            CONFIG ${CMAKE_CURRENT_BINARY_DIR}/sysmodule.json
        )

        # Upload to Switch via FTP
        find_program(CURL_EXE curl REQUIRED NO_CMAKE_FIND_ROOT_PATH)
        add_custom_target(MiniKDEConnect_sysmodule_upload
                COMMAND ${CURL_EXE} --ftp-create-dirs -T 4DE000000C011EC7/exefs.nsp
                "ftp://anonymous:@192.168.178.177:5001/atmosphere/contents/4DE000000C011EC7/exefs.nsp"
                COMMAND ${CURL_EXE} --ftp-create-dirs -T 4DE000000C011EC7/toolbox.json
                "ftp://anonymous:@192.168.178.177:5001/atmosphere/contents/4DE000000C011EC7/toolbox.json"
                DEPENDS MiniKDEConnect_sysmodule_nsp
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                COMMENT "Uploading sysmodule to Switch"
        )
    endif()
else()
    if(BUILD_CLI)
        add_executable(MiniKDEConnect_cli
                ${CMAKE_CURRENT_LIST_DIR}/src/main_linux.cpp
            ${MINIKDECONNECT_SYSMODULE_SOURCES}
        )
        target_include_directories(MiniKDEConnect_cli PRIVATE ${MINIKDECONNECT_INCLUDE_DIRS})
        target_link_libraries(MiniKDEConnect_cli PRIVATE ${MINIKDECONNECT_LINK_LIBS})
    endif()
endif()
