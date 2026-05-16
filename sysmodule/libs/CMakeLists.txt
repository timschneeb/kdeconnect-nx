set(ATMOSPHERE_LIBS_DIR ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs)
set(LIBSTRATOSPHERE_DIR ${ATMOSPHERE_LIBS_DIR}/libstratosphere)

include(ExternalProject)

if(CMAKE_SYSTEM_NAME MATCHES "NintendoSwitch")
    set(LIBSTRATOSPHERE_BUILD_TARGET nx_release)
    set(LIBSTRATOSPHERE_OUTPUT ${LIBSTRATOSPHERE_DIR}/lib/nintendo_nx_arm64_armv8a/release/libstratosphere.a)
else()
    set(LIBSTRATOSPHERE_BUILD_TARGET linux_x64_release)
    set(LIBSTRATOSPHERE_OUTPUT ${LIBSTRATOSPHERE_DIR}/lib/generic_linux_x64/release/libstratosphere.a)
endif()

ExternalProject_Add(atmosphere_libstratosphere
    SOURCE_DIR ${LIBSTRATOSPHERE_DIR}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Building libstratosphere (${LIBSTRATOSPHERE_BUILD_TARGET})..." && ${CMAKE_COMMAND} -E env DEVKITPRO=$ENV{DEVKITPRO} make -C ${LIBSTRATOSPHERE_DIR} ${LIBSTRATOSPHERE_BUILD_TARGET}
    BUILD_IN_SOURCE 1
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${LIBSTRATOSPHERE_OUTPUT}
)

add_library(libstratosphere STATIC IMPORTED GLOBAL)
set_target_properties(libstratosphere PROPERTIES
    IMPORTED_LOCATION ${LIBSTRATOSPHERE_OUTPUT}
)
add_dependencies(libstratosphere atmosphere_libstratosphere)

target_include_directories(libstratosphere INTERFACE
    ${LIBSTRATOSPHERE_DIR}/include
    ${ATMOSPHERE_LIBS_DIR}/libvapours/include
)

target_compile_definitions(libstratosphere INTERFACE
        ATMOSPHERE
        ATMOSPHERE_IS_STRATOSPHERE
        ATMOSPHERE_OS_HORIZON
        ATMOSPHERE_BOARD_NINTENDO_NX
        ATMOSPHERE_ARCH_ARM64
        ATMOSPHERE_ARCH_ARM_V8A
        ATMOSPHERE_CPU_ARM_CORTEX_A57
        _GNU_SOURCE)