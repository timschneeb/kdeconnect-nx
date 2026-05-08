#include "tls.h"

#include <mbedtls/bignum.h>
#include <mbedtls/base64.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pem.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509.h>

#include <fstream>
#include <cstring>
#include <sstream>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <random>

namespace {
std::string generate_device_id() {
    std::array<unsigned char, 16> bytes{};
    std::random_device rd;
    for (auto& b : bytes) {
        b = static_cast<unsigned char>(rd());
    }
    std::ostringstream oss;
    oss << std::hex;
    for (const auto b : bytes) {
        oss.width(2);
        oss.fill('0');
        oss << static_cast<int>(b);
    }
    return oss.str();
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_file(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << data;
    return true;
}

int socket_send(void* ctx, const unsigned char* buf, size_t len) {
    int fd = *static_cast<int*>(ctx);
    ssize_t sent = send(fd, buf, len, 0);
    if (sent < 0) {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    return static_cast<int>(sent);
}

int socket_recv(void* ctx, unsigned char* buf, size_t len) {
    int fd = *static_cast<int*>(ctx);
    ssize_t recvd = recv(fd, buf, len, 0);
    if (recvd < 0) {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    if (recvd == 0) {
        return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    }
    return static_cast<int>(recvd);
}

std::vector<unsigned char> pem_write_buffer(const unsigned char* data, size_t data_len, const char* header, const char* footer) {
    std::vector<unsigned char> out(4096);
    size_t olen = 0;
    int ret = mbedtls_pem_write_buffer(header, footer, data, data_len, out.data(), out.size(), &olen);
    if (ret != 0) {
        return {};
    }
    out.resize(olen);
    return out;
}
} // namespace

TlsSession::TlsSession() {
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&config);
}

TlsSession::~TlsSession() {
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config);
}

TlsContext::TlsContext() {
    mbedtls_entropy_init(&entropy_);
    mbedtls_ctr_drbg_init(&ctr_drbg_);
    mbedtls_x509_crt_init(&cert_);
    mbedtls_pk_init(&key_);
}

TlsContext::~TlsContext() {
    mbedtls_x509_crt_free(&cert_);
    mbedtls_pk_free(&key_);
    mbedtls_ctr_drbg_free(&ctr_drbg_);
    mbedtls_entropy_free(&entropy_);
}

bool TlsContext::load_or_create(const std::string& cert_path, const std::string& key_path) {
    const char* pers = "minikdeconnect";
    if (mbedtls_ctr_drbg_seed(&ctr_drbg_, mbedtls_entropy_func, &entropy_,
                              reinterpret_cast<const unsigned char*>(pers), strlen(pers)) != 0) {
        return false;
    }

    if (!load_from_files(cert_path, key_path)) {
        if (!generate_self_signed(cert_path, key_path)) {
            return false;
        }
    }
    return true;
}

std::unique_ptr<TlsSession> TlsContext::create_session(int fd, bool is_client) {
    auto session = std::make_unique<TlsSession>();
    session->fd = fd;

    int ret = mbedtls_ssl_config_defaults(&session->config,
                                          is_client ? MBEDTLS_SSL_IS_CLIENT : MBEDTLS_SSL_IS_SERVER,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        return nullptr;
    }
    mbedtls_ssl_conf_min_version(&session->config, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_rng(&session->config, mbedtls_ctr_drbg_random, &ctr_drbg_);
    mbedtls_ssl_conf_authmode(&session->config, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_own_cert(&session->config, &cert_, &key_);

    ret = mbedtls_ssl_setup(&session->ssl, &session->config);
    if (ret != 0) {
        return nullptr;
    }
    mbedtls_ssl_set_bio(&session->ssl, &session->fd, socket_send, socket_recv, nullptr);
    return session;
}

std::vector<unsigned char> TlsContext::local_pubkey_bytes() const {
    std::vector<unsigned char> out(2048);
    const int len = mbedtls_pk_write_pubkey_der(&key_, out.data(), out.size());
    if (len <= 0) {
        return {};
    }
    return { out.end() - len, out.end() };
}

std::string TlsContext::peer_cert_pem(const TlsSession& session) {
    const mbedtls_x509_crt* cert = mbedtls_ssl_get_peer_cert(&session.ssl);
    if (!cert) {
        return {};
    }
    const auto pem = pem_write_buffer(cert->raw.p, cert->raw.len, "-----BEGIN CERTIFICATE-----\n", "-----END CERTIFICATE-----\n");
    return std::string(reinterpret_cast<const char*>(pem.data()), pem.size());
}

std::vector<unsigned char> TlsContext::peer_pubkey_bytes(const TlsSession& session) {
    const mbedtls_x509_crt* cert = mbedtls_ssl_get_peer_cert(&session.ssl);
    if (!cert) {
        return {};
    }
    std::vector<unsigned char> out(2048);
    const int len = mbedtls_pk_write_pubkey_der(&cert->pk, out.data(), out.size());
    if (len <= 0) {
        return {};
    }
    return std::vector(out.end() - len, out.end());
}

bool TlsContext::load_from_files(const std::string& cert_path, const std::string& key_path) {
    std::string cert_pem = read_file(cert_path);
    std::string key_pem = read_file(key_path);
    if (cert_pem.empty() || key_pem.empty()) {
        return false;
    }
    int ret = mbedtls_x509_crt_parse(&cert_, reinterpret_cast<const unsigned char*>(cert_pem.c_str()), cert_pem.size() + 1);
    if (ret != 0) {
        return false;
    }
    ret = mbedtls_pk_parse_key(&key_, reinterpret_cast<const unsigned char*>(key_pem.c_str()), key_pem.size() + 1, nullptr, 0, mbedtls_ctr_drbg_random, &ctr_drbg_);
    if (ret != 0) {
        return false;
    }

    char buf[256] = {};
    mbedtls_x509_dn_gets(buf, sizeof(buf), &cert_.subject);
    std::string subject(buf);
    auto pos = subject.find("CN=");
    if (pos == std::string::npos) {
        mbedtls_x509_crt_free(&cert_);
        mbedtls_x509_crt_init(&cert_);
        mbedtls_pk_free(&key_);
        mbedtls_pk_init(&key_);
        return false;
    }
    device_id_ = subject.substr(pos + 3);
    auto comma = device_id_.find(',');
    if (comma != std::string::npos) {
        device_id_ = device_id_.substr(0, comma);
    }

    cert_pem_ = cert_pem;
    return true;
}

bool TlsContext::generate_self_signed(const std::string& cert_path, const std::string& key_path) {
    device_id_ = generate_device_id();

    if (mbedtls_pk_setup(&key_, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) != 0) {
        return false;
    }
    if (mbedtls_rsa_gen_key(mbedtls_pk_rsa(key_), mbedtls_ctr_drbg_random, &ctr_drbg_, 2048, 65537) != 0) {
        return false;
    }

    mbedtls_x509write_cert write_cert;
    mbedtls_x509write_crt_init(&write_cert);
    mbedtls_x509write_crt_set_md_alg(&write_cert, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&write_cert, &key_);
    mbedtls_x509write_crt_set_issuer_key(&write_cert, &key_);

    std::string subject = "CN=" + device_id_;
    if (mbedtls_x509write_crt_set_subject_name(&write_cert, subject.c_str()) != 0) {
        mbedtls_x509write_crt_free(&write_cert);
        return false;
    }
    if (mbedtls_x509write_crt_set_issuer_name(&write_cert, subject.c_str()) != 0) {
        mbedtls_x509write_crt_free(&write_cert);
        return false;
    }
    mbedtls_x509write_crt_set_version(&write_cert, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_mpi serial;
    mbedtls_mpi_init(&serial);
    mbedtls_mpi_lset(&serial, 1);
    mbedtls_x509write_crt_set_serial(&write_cert, &serial);
    mbedtls_x509write_crt_set_validity(&write_cert, "20260101000000", "21990101000000");

    unsigned char cert_buf[4096];
    if (mbedtls_x509write_crt_pem(&write_cert, cert_buf, sizeof(cert_buf), mbedtls_ctr_drbg_random, &ctr_drbg_) != 0) {
        mbedtls_mpi_free(&serial);
        mbedtls_x509write_crt_free(&write_cert);
        return false;
    }
    cert_pem_.assign(reinterpret_cast<char*>(cert_buf));

    if (!write_file(cert_path, cert_pem_)) {
        mbedtls_x509write_crt_free(&write_cert);
        return false;
    }

    unsigned char key_buf[4096];
    if (mbedtls_pk_write_key_pem(&key_, key_buf, sizeof(key_buf)) != 0) {
        mbedtls_mpi_free(&serial);
        mbedtls_x509write_crt_free(&write_cert);
        return false;
    }
    if (!write_file(key_path, reinterpret_cast<char*>(key_buf))) {
        mbedtls_mpi_free(&serial);
        mbedtls_x509write_crt_free(&write_cert);
        return false;
    }

    mbedtls_mpi_free(&serial);
    mbedtls_x509write_crt_free(&write_cert);
    return mbedtls_x509_crt_parse(&cert_, reinterpret_cast<const unsigned char*>(cert_pem_.c_str()), cert_pem_.size() + 1) == 0;
}

std::string sha256_hex_upper(const std::vector<unsigned char>& data) {
    unsigned char hash[32];
    mbedtls_sha256(data.data(), data.size(), hash, 0);
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(64);
    for (unsigned char b : hash) {
        out.push_back(hex[(b >> 4) & 0xF]);
        out.push_back(hex[b & 0xF]);
    }
    return out;
}

