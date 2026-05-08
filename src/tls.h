#pragma once

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <memory>
#include <string>
#include <vector>

struct TlsSession {
    TlsSession();
    ~TlsSession();

    mbedtls_ssl_context ssl{};
    mbedtls_ssl_config config{};
    int fd = -1;
};

class TlsContext {
public:
    TlsContext();
    ~TlsContext();

    bool load_or_create(const std::string& cert_path, const std::string& key_path);
    std::string device_id() const { return device_id_; }

    std::unique_ptr<TlsSession> create_session(int fd, bool is_client);

    [[nodiscard]] std::string local_cert_pem() const { return cert_pem_; }
    [[nodiscard]] std::vector<unsigned char> local_pubkey_bytes() const;

    [[nodiscard]] static std::string peer_cert_pem(const TlsSession& session);
    [[nodiscard]] static std::vector<unsigned char> peer_pubkey_bytes(const TlsSession& session) ;

private:
    mbedtls_entropy_context entropy_{};
    mbedtls_ctr_drbg_context ctr_drbg_{};
    mbedtls_x509_crt cert_{};
    mbedtls_pk_context key_{};
    std::string cert_pem_;

    std::string device_id_;

    bool load_from_files(const std::string& cert_path, const std::string& key_path);
    bool generate_self_signed(const std::string& cert_path, const std::string& key_path);
};

std::string sha256_hex_upper(const std::vector<unsigned char>& data);

