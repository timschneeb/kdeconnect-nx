#pragma once

/**
 * DEBUG SETTINGS
 */
#ifdef DEBUG
// ------ THREADS ------
/// Measure thread stack memory usage
//#define STACK_THREAD_MEASURE
/// Force minimum thread stack size globally
//#define DEBUG_MIN_THREAD_STACK_SIZE 32*1024

// ------ HEAP ANALYSIS ------
/// Trace all allocations with backtraces to a binary file (slow!)
//#define DEBUG_ALLOC_TRACE
/// Log basic heap usage statistics in intervals
#define DEBUG_HEAP

// ------ OTHER ------
/// Exit sysmodule automatically after N seconds
//#define DEBUG_EXIT_TIMEOUT 60
/// Log open socket count + pending thread count periodically (same cadence as DEBUG_HEAP)
#define DEBUG_SOCKETS
#endif

/**
 * SOCKET BUFFER SETTINGS
 */
#ifdef __SWITCH__
#include <switch.h>
constexpr SocketInitConfig socketInitConfig = {
    .tcp_tx_buf_size = 32 * 1024,
    .tcp_rx_buf_size = 32 * 1024,
    .tcp_tx_buf_max_size = 64 * 1024,
    .tcp_rx_buf_max_size = 64 * 1024,
    .udp_tx_buf_size = 8 * 1024,
    .udp_rx_buf_size = 16 * 1024,
    .sb_efficiency = 4,
    .num_bsd_sessions = 6,
    .bsd_service_type = BsdServiceType_System
};

constexpr size_t bsdGetTransferMemSizeForConfig(const SocketInitConfig *config) {
    u32 tcp_tx_buf_max_size = config->tcp_tx_buf_max_size != 0 ? config->tcp_tx_buf_max_size : config->tcp_tx_buf_size;
    u32 tcp_rx_buf_max_size = config->tcp_rx_buf_max_size != 0 ? config->tcp_rx_buf_max_size : config->tcp_rx_buf_size;
    u32 sum = tcp_tx_buf_max_size + tcp_rx_buf_max_size + config->udp_tx_buf_size + config->udp_rx_buf_size;

    sum = (sum + 0xFFF) & ~0xFFF; // page round-up
    return config->sb_efficiency * sum;
}
#endif