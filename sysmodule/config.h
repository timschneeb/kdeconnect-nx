#pragma once

#include <switch.h>

constexpr SocketInitConfig socketInitConfig = {
    .tcp_tx_buf_size = 32 * 1024,
    .tcp_rx_buf_size = 32 * 1024,
    .tcp_tx_buf_max_size = 64 * 1024,
    .tcp_rx_buf_max_size = 64 * 1024,
    .udp_tx_buf_size = 8 * 1024,
    .udp_rx_buf_size = 16 * 1024,
#ifdef NXLINK_ENABLED
    .sb_efficiency = 3,
#else
    .sb_efficiency = 2,
#endif

    .bsd_service_type = BsdServiceType_Auto
};

constexpr size_t bsdGetTransferMemSizeForConfig(const SocketInitConfig *config) {
    u32 tcp_tx_buf_max_size = config->tcp_tx_buf_max_size != 0 ? config->tcp_tx_buf_max_size : config->tcp_tx_buf_size;
    u32 tcp_rx_buf_max_size = config->tcp_rx_buf_max_size != 0 ? config->tcp_rx_buf_max_size : config->tcp_rx_buf_size;
    u32 sum = tcp_tx_buf_max_size + tcp_rx_buf_max_size + config->udp_tx_buf_size + config->udp_rx_buf_size;

    sum = (sum + 0xFFF) & ~0xFFF; // page round-up
    return config->sb_efficiency * sum;
}
