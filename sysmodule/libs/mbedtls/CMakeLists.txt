cmake_minimum_required(VERSION 3.10)

project(mbedtls_custom C)

include(FetchContent)

set(MBEDTLS_VERSION 2.28.10)

FetchContent_Declare(
    mbedtls_src
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-${MBEDTLS_VERSION}/mbedtls-${MBEDTLS_VERSION}.tar.bz2
    URL_HASH SHA256=19e5b81fdac0fe22009b9e2bdcd52d7dcafbf62bc67fc59cf0a76b5b5540d149
    PATCH_COMMAND patch -p1 < ${CMAKE_CURRENT_LIST_DIR}/mbedtls-2.28.10.patch
)

FetchContent_GetProperties(mbedtls_src)
if(NOT mbedtls_src_POPULATED)
    FetchContent_Populate(mbedtls_src)
endif()

set(MBEDTLS_DIR ${mbedtls_src_SOURCE_DIR})

# Configuration
set(MBEDTLS_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/config-switch.h")
add_definitions("-DMBEDTLS_CONFIG_FILE=\"${MBEDTLS_CONFIG_FILE}\"")

set(MBEDCRYPTO_SOURCES
    ${MBEDTLS_DIR}/library/aes.c
    ${MBEDTLS_DIR}/library/asn1parse.c
    ${MBEDTLS_DIR}/library/asn1write.c
    ${MBEDTLS_DIR}/library/base64.c
    ${MBEDTLS_DIR}/library/bignum.c
    ${MBEDTLS_DIR}/library/cipher.c
    ${MBEDTLS_DIR}/library/cipher_wrap.c
    ${MBEDTLS_DIR}/library/constant_time.c
    ${MBEDTLS_DIR}/library/ctr_drbg.c
    ${MBEDTLS_DIR}/library/ecdh.c
    ${MBEDTLS_DIR}/library/ecdsa.c
    ${MBEDTLS_DIR}/library/ecp.c
    ${MBEDTLS_DIR}/library/ecp_curves.c
    ${MBEDTLS_DIR}/library/entropy.c
    ${MBEDTLS_DIR}/library/entropy_poll.c
    ${MBEDTLS_DIR}/library/error.c
    ${MBEDTLS_DIR}/library/gcm.c
    ${MBEDTLS_DIR}/library/hmac_drbg.c
    ${MBEDTLS_DIR}/library/md.c
    ${MBEDTLS_DIR}/library/oid.c
    ${MBEDTLS_DIR}/library/pem.c
    ${MBEDTLS_DIR}/library/pk.c
    ${MBEDTLS_DIR}/library/pk_wrap.c
    ${MBEDTLS_DIR}/library/pkparse.c
    ${MBEDTLS_DIR}/library/pkwrite.c
    ${MBEDTLS_DIR}/library/platform.c
    ${MBEDTLS_DIR}/library/platform_util.c
    ${MBEDTLS_DIR}/library/rsa.c
    ${MBEDTLS_DIR}/library/rsa_internal.c
    ${MBEDTLS_DIR}/library/sha256.c
    ${MBEDTLS_DIR}/library/sha512.c
    ${MBEDTLS_DIR}/library/timing.c
)

set(MBEDX509_SOURCES
    ${MBEDTLS_DIR}/library/x509.c
    ${MBEDTLS_DIR}/library/x509_create.c
    ${MBEDTLS_DIR}/library/x509_crt.c
    ${MBEDTLS_DIR}/library/x509_crl.c
    ${MBEDTLS_DIR}/library/x509_csr.c
    ${MBEDTLS_DIR}/library/x509write_crt.c
    ${MBEDTLS_DIR}/library/x509write_csr.c
)

set(MBEDTLS_SOURCES
    ${MBEDTLS_DIR}/library/net_sockets.c
    ${MBEDTLS_DIR}/library/ssl_cache.c
    ${MBEDTLS_DIR}/library/ssl_ciphersuites.c
    ${MBEDTLS_DIR}/library/ssl_cli.c
    ${MBEDTLS_DIR}/library/ssl_cookie.c
    ${MBEDTLS_DIR}/library/ssl_msg.c
    ${MBEDTLS_DIR}/library/ssl_srv.c
    ${MBEDTLS_DIR}/library/ssl_ticket.c
    ${MBEDTLS_DIR}/library/ssl_tls.c
)

add_library(mbedcrypto STATIC ${MBEDCRYPTO_SOURCES})
add_library(mbedx509 STATIC ${MBEDX509_SOURCES})
add_library(mbedtls STATIC ${MBEDTLS_SOURCES})

target_include_directories(mbedcrypto PUBLIC ${MBEDTLS_DIR}/include ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(mbedx509 PUBLIC ${MBEDTLS_DIR}/include)
target_include_directories(mbedtls PUBLIC ${MBEDTLS_DIR}/include)

target_link_libraries(mbedx509 mbedcrypto)
target_link_libraries(mbedtls mbedx509)

target_compile_options(mbedcrypto PRIVATE -flto -Os -ffunction-sections -fdata-sections)
target_link_options(mbedcrypto PRIVATE -Wl,--gc-sections)
target_compile_options(mbedx509 PRIVATE -flto -Os -ffunction-sections -fdata-sections)
target_link_options(mbedx509 PRIVATE -Wl,--gc-sections)
target_compile_options(mbedtls PRIVATE -flto -Os -ffunction-sections -fdata-sections)
target_link_options(mbedtls PRIVATE -Wl,--gc-sections)

# Export targets
add_library(MBEDTLS::mbedtls ALIAS mbedtls)
add_library(MBEDTLS::mbedx509 ALIAS mbedx509)
add_library(MBEDTLS::mbedcrypto ALIAS mbedcrypto)
