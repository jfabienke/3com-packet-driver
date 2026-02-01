/**
 * @file hwchksm_rt.c
 * @brief Hardware Checksum Offload - Runtime Functions (ROOT Segment)
 *
 * Created: 2026-01-28 09:26:56 CET
 *
 * This file contains runtime checksum functions that are called during
 * active packet TX/RX operations, including interrupt handlers. These
 * functions must remain memory-resident (ROOT segment) for performance
 * and reliability during high-frequency network operations.
 *
 * Functions included:
 * - Checksum calculation functions (IP/TCP/UDP)
 * - TX path checksum calculation
 * - RX path checksum validation
 * - Software checksum implementation
 * - Statistics access functions
 * - Utility/debugging functions
 *
 * Split from hwchksm.c for memory segmentation optimization.
 * Init/cleanup functions are in hwchksm_init.c (OVERLAY segment).
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 */

#include "hwchksm.h"
#include "niccap.h"
#include "nicctx.h"
#include "logging.h"
#include "errhndl.h"
#include "pktops.h"
#include "cpudet.h"
#include "main.h"
#include "diag.h"
#include <string.h>
/* NOTE: stdio.h REMOVED to avoid C library bloat (~40KB)
 * hw_checksum_print_stats() is now a stub - use hwchksm_init.c for diagnostics */

/* ========================================================================== */
/* External declarations for global state (defined in hwchksm_init.c)        */
/* ========================================================================== */

extern bool checksum_system_initialized;
extern checksum_mode_t global_checksum_mode;
extern checksum_stats_t global_checksum_stats;
extern uint16_t checksum_optimization_flags;

/* ========================================================================== */
/* TRANSMIT PATH CHECKSUM CALCULATION                                        */
/* ========================================================================== */

int hw_checksum_tx_calculate(nic_context_t *ctx, uint8_t *packet, uint16_t length, uint32_t protocols) {
    /* C89: All variable declarations must be at the beginning of the block */
    uint8_t *ip_header;
    uint16_t ip_header_len;
    uint8_t ip_protocol;
    uint16_t ip_total_len;
    int result;
    uint32_t start_time;
    uint8_t *transport_header;
    uint16_t transport_len;
    uint32_t end_time;
    uint16_t calc_time;

    if (!ctx || !packet || length < ETH_HEADER_SIZE) {
        return HW_CHECKSUM_INVALID_PARAM;
    }

    if (!checksum_system_initialized) {
        LOG_WARNING("Checksum system not initialized");
        return HW_CHECKSUM_ERROR;
    }

    ip_header = packet + ETH_HEADER_SIZE;
    ip_header_len = (ip_header[IP_OFFSET_VERSION_IHL] & 0x0F) * 4;
    ip_protocol = ip_header[IP_OFFSET_PROTOCOL];
    ip_total_len = (ip_header[IP_OFFSET_TOTAL_LEN] << 8) | ip_header[IP_OFFSET_TOTAL_LEN + 1];

    result = HW_CHECKSUM_SUCCESS;
    start_time = packet_get_timestamp();

    /* Calculate IP header checksum if requested */
    if (protocols & (1 << CHECKSUM_PROTO_IP)) {
        result = hw_checksum_calculate_ip(ip_header, ip_header_len);
        if (result != HW_CHECKSUM_SUCCESS) {
            global_checksum_stats.calculation_errors++;
            return result;
        }
        global_checksum_stats.ip_checksums++;
    }

    /* Calculate transport layer checksum */
    transport_header = ip_header + ip_header_len;
    transport_len = ip_total_len - ip_header_len;

    if (ip_protocol == IP_PROTO_TCP && (protocols & (1 << CHECKSUM_PROTO_TCP))) {
        /* C89: Declare variables at beginning of block */
        checksum_context_t ctx_tcp;
        uint32_t src_ip;
        uint32_t dst_ip;

        memset(&ctx_tcp, 0, sizeof(checksum_context_t));
        ctx_tcp.mode = hw_checksum_get_optimal_mode(ctx, CHECKSUM_PROTO_TCP);
        ctx_tcp.protocol = CHECKSUM_PROTO_TCP;

        /* Calculate pseudo-header sum - IP addresses are already in network order */
        src_ip = *(uint32_t*)(ip_header + IP_OFFSET_SRC_IP);
        dst_ip = *(uint32_t*)(ip_header + IP_OFFSET_DST_IP);

        /* Pass network-order addresses directly to pseudo-header calculation */
        ctx_tcp.pseudo_header_sum = sw_checksum_pseudo_header(src_ip, dst_ip, IP_PROTO_TCP, transport_len);

        result = hw_checksum_calculate_tcp(&ctx_tcp, transport_header, transport_len);
        if (result == HW_CHECKSUM_SUCCESS) {
            global_checksum_stats.tcp_checksums++;
        }
    } else if (ip_protocol == IP_PROTO_UDP && (protocols & (1 << CHECKSUM_PROTO_UDP))) {
        /* C89: Declare variables at beginning of block */
        checksum_context_t ctx_udp;
        uint32_t src_ip;
        uint32_t dst_ip;

        memset(&ctx_udp, 0, sizeof(checksum_context_t));
        ctx_udp.mode = hw_checksum_get_optimal_mode(ctx, CHECKSUM_PROTO_UDP);
        ctx_udp.protocol = CHECKSUM_PROTO_UDP;

        /* Calculate pseudo-header sum - IP addresses are already in network order */
        src_ip = *(uint32_t*)(ip_header + IP_OFFSET_SRC_IP);
        dst_ip = *(uint32_t*)(ip_header + IP_OFFSET_DST_IP);

        /* Pass network-order addresses directly to pseudo-header calculation */
        ctx_udp.pseudo_header_sum = sw_checksum_pseudo_header(src_ip, dst_ip, IP_PROTO_UDP, transport_len);

        result = hw_checksum_calculate_udp(&ctx_udp, transport_header, transport_len);
        if (result == HW_CHECKSUM_SUCCESS) {
            global_checksum_stats.udp_checksums++;
        }
    }

    /* Update statistics */
    end_time = packet_get_timestamp();
    calc_time = (end_time > start_time) ? (end_time - start_time) : 0;

    global_checksum_stats.tx_checksums_calculated++;
    global_checksum_stats.total_bytes_processed += length;
    global_checksum_stats.software_fallbacks++;  /* All calculations are software for 3C515/3C509B */

    if (calc_time > 0) {
        global_checksum_stats.avg_calc_time_us =
            (global_checksum_stats.avg_calc_time_us + calc_time) / 2;
    }

    if (result != HW_CHECKSUM_SUCCESS) {
        global_checksum_stats.calculation_errors++;
    }

    return result;
}

int hw_checksum_calculate_ip(uint8_t *ip_header, uint16_t header_length) {
    uint16_t checksum;  /* C89: declare at start of block */

    if (!ip_header || header_length < IP_HEADER_MIN_SIZE) {
        return HW_CHECKSUM_INVALID_PARAM;
    }

    /* Clear existing checksum */
    ip_header[IP_OFFSET_CHECKSUM] = 0;
    ip_header[IP_OFFSET_CHECKSUM + 1] = 0;

    /* Calculate checksum */
    checksum = sw_checksum_internet(ip_header, header_length, 0);

    /* Store in network byte order */
    ip_header[IP_OFFSET_CHECKSUM] = (checksum >> 8) & 0xFF;
    ip_header[IP_OFFSET_CHECKSUM + 1] = checksum & 0xFF;

    return HW_CHECKSUM_SUCCESS;
}

int hw_checksum_calculate_tcp(checksum_context_t *ctx, uint8_t *tcp_header, uint16_t tcp_length) {
    uint16_t checksum;  /* C89: declare at start of block */

    if (!ctx || !tcp_header || tcp_length < TCP_HEADER_MIN_SIZE) {
        return HW_CHECKSUM_INVALID_PARAM;
    }

    /* Clear existing checksum */
    tcp_header[TCP_OFFSET_CHECKSUM] = 0;
    tcp_header[TCP_OFFSET_CHECKSUM + 1] = 0;

    /* Calculate checksum including pseudo-header */
    checksum = sw_checksum_internet(tcp_header, tcp_length, ctx->pseudo_header_sum);

    /* Store in network byte order */
    tcp_header[TCP_OFFSET_CHECKSUM] = (checksum >> 8) & 0xFF;
    tcp_header[TCP_OFFSET_CHECKSUM + 1] = checksum & 0xFF;

    return HW_CHECKSUM_SUCCESS;
}

int hw_checksum_calculate_udp(checksum_context_t *ctx, uint8_t *udp_header, uint16_t udp_length) {
    uint16_t checksum;  /* C89: declare at start of block */

    if (!ctx || !udp_header || udp_length < UDP_HEADER_SIZE) {
        return HW_CHECKSUM_INVALID_PARAM;
    }

    /* Clear existing checksum */
    udp_header[UDP_OFFSET_CHECKSUM] = 0;
    udp_header[UDP_OFFSET_CHECKSUM + 1] = 0;

    /* Calculate checksum including pseudo-header */
    checksum = sw_checksum_internet(udp_header, udp_length, ctx->pseudo_header_sum);

    /* UDP checksum of 0 means no checksum - convert 0x0000 to 0xFFFF */
    if (checksum == 0) {
        checksum = 0xFFFF;
    }

    /* Store in network byte order */
    udp_header[UDP_OFFSET_CHECKSUM] = (checksum >> 8) & 0xFF;
    udp_header[UDP_OFFSET_CHECKSUM + 1] = checksum & 0xFF;

    return HW_CHECKSUM_SUCCESS;
}

/* ========================================================================== */
/* RECEIVE PATH CHECKSUM VALIDATION                                          */
/* ========================================================================== */

int hw_checksum_rx_validate(nic_context_t *ctx, const uint8_t *packet, uint16_t length, uint32_t *result_mask) {
    /* C89: declare all variables at start of block */
    const uint8_t *ip_header;
    uint16_t ip_header_len;
    uint8_t ip_protocol;
    uint16_t ip_total_len;
    checksum_result_t ip_result;
    const uint8_t *transport_header;
    uint16_t transport_len;

    if (!ctx || !packet || !result_mask || length < ETH_HEADER_SIZE) {
        return HW_CHECKSUM_INVALID_PARAM;
    }

    *result_mask = 0;

    ip_header = packet + ETH_HEADER_SIZE;
    ip_header_len = (ip_header[IP_OFFSET_VERSION_IHL] & 0x0F) * 4;
    ip_protocol = ip_header[IP_OFFSET_PROTOCOL];
    ip_total_len = (ip_header[IP_OFFSET_TOTAL_LEN] << 8) | ip_header[IP_OFFSET_TOTAL_LEN + 1];

    /* Validate IP header checksum */
    ip_result = hw_checksum_validate_ip(ip_header, ip_header_len);
    *result_mask |= (ip_result << (CHECKSUM_PROTO_IP * 2));

    /* Validate transport layer checksum */
    transport_header = ip_header + ip_header_len;
    transport_len = ip_total_len - ip_header_len;

    if (ip_protocol == IP_PROTO_TCP) {
        checksum_result_t tcp_result = hw_checksum_validate_tcp(ip_header, transport_header, transport_len);
        *result_mask |= (tcp_result << (CHECKSUM_PROTO_TCP * 2));
    } else if (ip_protocol == IP_PROTO_UDP) {
        checksum_result_t udp_result = hw_checksum_validate_udp(ip_header, transport_header, transport_len);
        *result_mask |= (udp_result << (CHECKSUM_PROTO_UDP * 2));
    }

    global_checksum_stats.rx_checksums_validated++;
    global_checksum_stats.software_fallbacks++;  /* All validation is software for 3C515/3C509B */

    return HW_CHECKSUM_SUCCESS;
}

checksum_result_t hw_checksum_validate_ip(const uint8_t *ip_header, uint16_t header_length) {
    uint16_t checksum;  /* C89: declare at start of block */

    if (!ip_header || header_length < IP_HEADER_MIN_SIZE) {
        return CHECKSUM_RESULT_UNKNOWN;
    }

    checksum = sw_checksum_internet(ip_header, header_length, 0);
    return (checksum == 0) ? CHECKSUM_RESULT_VALID : CHECKSUM_RESULT_INVALID;
}

checksum_result_t hw_checksum_validate_tcp(const uint8_t *ip_header, const uint8_t *tcp_header, uint16_t tcp_length) {
    /* C89: declare all variables at start of block */
    uint32_t src_ip;
    uint32_t dst_ip;
    uint32_t pseudo_sum;
    uint16_t checksum;

    if (!ip_header || !tcp_header || tcp_length < TCP_HEADER_MIN_SIZE) {
        return CHECKSUM_RESULT_UNKNOWN;
    }

    /* Calculate pseudo-header sum */
    src_ip = *(uint32_t*)(ip_header + IP_OFFSET_SRC_IP);
    dst_ip = *(uint32_t*)(ip_header + IP_OFFSET_DST_IP);
    pseudo_sum = sw_checksum_pseudo_header(src_ip, dst_ip, IP_PROTO_TCP, tcp_length);

    checksum = sw_checksum_internet(tcp_header, tcp_length, pseudo_sum);
    return (checksum == 0) ? CHECKSUM_RESULT_VALID : CHECKSUM_RESULT_INVALID;
}

checksum_result_t hw_checksum_validate_udp(const uint8_t *ip_header, const uint8_t *udp_header, uint16_t udp_length) {
    /* C89: declare all variables at start of block */
    uint16_t stored_checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
    uint32_t pseudo_sum;
    uint16_t checksum;

    if (!ip_header || !udp_header || udp_length < UDP_HEADER_SIZE) {
        return CHECKSUM_RESULT_UNKNOWN;
    }

    /* Check if UDP checksum is disabled (0x0000) */
    stored_checksum = (udp_header[UDP_OFFSET_CHECKSUM] << 8) | udp_header[UDP_OFFSET_CHECKSUM + 1];
    if (stored_checksum == 0) {
        return CHECKSUM_RESULT_NOT_CHECKED;  /* UDP checksum disabled */
    }

    /* Calculate pseudo-header sum */
    src_ip = *(uint32_t*)(ip_header + IP_OFFSET_SRC_IP);
    dst_ip = *(uint32_t*)(ip_header + IP_OFFSET_DST_IP);
    pseudo_sum = sw_checksum_pseudo_header(src_ip, dst_ip, IP_PROTO_UDP, udp_length);

    checksum = sw_checksum_internet(udp_header, udp_length, pseudo_sum);
    return (checksum == 0) ? CHECKSUM_RESULT_VALID : CHECKSUM_RESULT_INVALID;
}

/* ========================================================================== */
/* SOFTWARE CHECKSUM IMPLEMENTATION                                          */
/* ========================================================================== */

uint16_t sw_checksum_internet(const uint8_t *data, uint16_t length, uint32_t initial) {
    uint32_t sum = initial;

    /* Use optimized version if alignment and flags permit */
    if ((checksum_optimization_flags & CHECKSUM_OPT_ALIGN_16BIT) && ((uintptr_t)data % 2 == 0)) {
        return sw_checksum_optimized_16bit(data, length, initial);
    }

    /* Process 16-bit words */
    while (length > 1) {
        sum += (data[0] << 8) | data[1];
        data += 2;
        length -= 2;
    }

    /* Handle odd byte */
    if (length > 0) {
        sum += data[0] << 8;
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}

uint32_t sw_checksum_pseudo_header(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol, uint16_t length) {
    uint32_t sum = 0;

    /* Add source IP */
    sum += (src_ip >> 16) + (src_ip & 0xFFFF);

    /* Add destination IP */
    sum += (dst_ip >> 16) + (dst_ip & 0xFFFF);

    /* Add protocol and length */
    sum += protocol + length;

    return sum;
}

uint16_t sw_checksum_optimized_16bit(const uint8_t *data, uint16_t length, uint32_t initial) {
    uint32_t sum = initial;
    const uint16_t *word_ptr = (const uint16_t*)data;

    /* Process multiple words at once if unroll optimization is enabled */
    if (checksum_optimization_flags & CHECKSUM_OPT_UNROLL_LOOPS) {
        while (length >= 8) {
            sum += word_ptr[0] + word_ptr[1] + word_ptr[2] + word_ptr[3];
            word_ptr += 4;
            length -= 8;
        }
    }

    /* Process remaining 16-bit words */
    while (length > 1) {
        sum += *word_ptr++;
        length -= 2;
    }

    /* Handle odd byte */
    if (length > 0) {
        sum += *(uint8_t*)word_ptr << 8;
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}

/* ========================================================================== */
/* CAPABILITY QUERY FUNCTIONS (called at runtime for mode selection)         */
/* ========================================================================== */

bool hw_checksum_is_supported(nic_context_t *ctx, checksum_protocol_t protocol) {
    uint32_t capabilities = hw_checksum_detect_capabilities(ctx);
    return (capabilities & (1 << protocol)) != 0;
}

checksum_mode_t hw_checksum_get_optimal_mode(nic_context_t *ctx, checksum_protocol_t protocol) {
    if (!ctx) {
        return CHECKSUM_MODE_SOFTWARE;
    }

    /* 3C515-TX and 3C509B always use software */
    if (ctx->nic_type == NIC_TYPE_3C515_TX || ctx->nic_type == NIC_TYPE_3C509B) {
        return CHECKSUM_MODE_SOFTWARE;
    }

    /* Check hardware capability */
    if (hw_checksum_is_supported(ctx, protocol)) {
        return CHECKSUM_MODE_HARDWARE;
    }

    return CHECKSUM_MODE_SOFTWARE;
}

/* ========================================================================== */
/* STATISTICS AND MONITORING                                                 */
/* ========================================================================== */

int hw_checksum_get_stats(checksum_stats_t *stats) {
    if (!stats) {
        return HW_CHECKSUM_INVALID_PARAM;
    }

    memcpy(stats, &global_checksum_stats, sizeof(checksum_stats_t));
    return HW_CHECKSUM_SUCCESS;
}

int hw_checksum_clear_stats(void) {
    memset(&global_checksum_stats, 0, sizeof(checksum_stats_t));
    return HW_CHECKSUM_SUCCESS;
}

void hw_checksum_print_stats(void) {
    /* Stub - diagnostic printing removed from runtime to save ~40KB
     * Full diagnostic output is in hwchksm_init.c (OVERLAY segment)
     * Statistics can still be retrieved via hw_checksum_get_stats() */
}

/* ========================================================================== */
/* DEBUGGING AND UTILITY FUNCTIONS                                           */
/* ========================================================================== */

const char* hw_checksum_result_to_string(checksum_result_t result) {
    switch (result) {
        case CHECKSUM_RESULT_VALID:      return "Valid";
        case CHECKSUM_RESULT_INVALID:    return "Invalid";
        case CHECKSUM_RESULT_UNKNOWN:    return "Unknown";
        case CHECKSUM_RESULT_NOT_CHECKED: return "Not Checked";
        default:                         return "Error";
    }
}

const char* hw_checksum_mode_to_string(checksum_mode_t mode) {
    switch (mode) {
        case CHECKSUM_MODE_NONE:     return "None";
        case CHECKSUM_MODE_SOFTWARE: return "Software";
        case CHECKSUM_MODE_HARDWARE: return "Hardware";
        case CHECKSUM_MODE_AUTO:     return "Auto";
        default:                     return "Unknown";
    }
}
