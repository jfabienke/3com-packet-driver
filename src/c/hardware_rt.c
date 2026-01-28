/**
 * @file hardware_rt.c
 * @brief Hardware abstraction layer - Runtime functions (ROOT segment)
 *
 * This file contains only the runtime functions needed after initialization:
 * - Packet send/receive dispatch
 * - NIC lookup functions
 * - Interrupt control
 * - Link status
 * - Statistics
 *
 * Init-only functions are in hardware_init.c (OVERLAY segment)
 *
 * Updated: 2026-01-28 05:30:00 UTC
 */

#include "hardware.h"
#include "logging.h"
#include "memory.h"
#include "common.h"
#include <string.h>

/* ============================================================================
 * Global Hardware State (Shared with hardware_init.c)
 * ============================================================================ */

/* These are defined here but declared extern in hardware_init.c */
nic_info_t g_nic_infos[MAX_NICS];
int g_num_nics = 0;
bool g_hardware_initialized = false;

/* Private statistics */
typedef struct {
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t send_errors;
    uint32_t receive_errors;
    uint32_t successful_sends;
    uint32_t successful_receives;
    uint32_t interrupts_handled;
} hardware_stats_t;

static hardware_stats_t g_hardware_stats = {0};

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

static bool hardware_validate_nic_index(int index) {
    return (index >= 0 && index < g_num_nics);
}

static void hardware_update_packet_stats(bool sent, bool success) {
    if (sent) {
        g_hardware_stats.packets_sent++;
        if (success) {
            g_hardware_stats.successful_sends++;
        } else {
            g_hardware_stats.send_errors++;
        }
    } else {
        g_hardware_stats.packets_received++;
        if (success) {
            g_hardware_stats.successful_receives++;
        } else {
            g_hardware_stats.receive_errors++;
        }
    }
}

/* ============================================================================
 * NIC Access Functions - Runtime
 * ============================================================================ */

int hardware_get_nic_count(void) {
    return g_num_nics;
}

nic_info_t* hardware_get_nic(int index) {
    if (!hardware_validate_nic_index(index)) {
        return NULL;
    }
    return &g_nic_infos[index];
}

nic_info_t* hardware_find_nic_by_type(nic_type_t type) {
    int i;
    for (i = 0; i < g_num_nics; i++) {
        if (g_nic_infos[i].type == type) {
            return &g_nic_infos[i];
        }
    }
    return NULL;
}

nic_info_t* hardware_find_nic_by_mac(const uint8_t *mac) {
    int i;
    if (!mac) {
        return NULL;
    }

    for (i = 0; i < g_num_nics; i++) {
        if (memory_compare(g_nic_infos[i].mac, mac, ETH_ALEN) == 0) {
            return &g_nic_infos[i];
        }
    }
    return NULL;
}

/**
 * @brief Get the primary (first active) NIC
 */
nic_info_t* hardware_get_primary_nic(void) {
    int i;

    for (i = 0; i < g_num_nics; i++) {
        if ((g_nic_infos[i].status & NIC_STATUS_PRESENT) &&
            (g_nic_infos[i].status & NIC_STATUS_INITIALIZED)) {
            LOG_DEBUG("Primary NIC selected: index %d, type %d",
                     i, g_nic_infos[i].type);
            return &g_nic_infos[i];
        }
    }

    LOG_WARNING("No primary NIC available");
    return NULL;
}

bool hardware_is_nic_present(int index) {
    if (!hardware_validate_nic_index(index)) {
        return false;
    }
    return (g_nic_infos[index].status & NIC_STATUS_PRESENT) != 0;
}

bool hardware_is_nic_active(int index) {
    if (!hardware_validate_nic_index(index)) {
        return false;
    }
    return (g_nic_infos[index].status & NIC_STATUS_ACTIVE) != 0;
}

/* ============================================================================
 * Packet Operations - Runtime
 * ============================================================================ */

int hardware_send_packet(nic_info_t *nic, const uint8_t *packet, size_t length) {
    int result;

    if (!nic || !packet || length == 0) {
        hardware_update_packet_stats(true, false);
        return ERROR_INVALID_PARAM;
    }

    if (!nic->ops || !nic->ops->send_packet) {
        hardware_update_packet_stats(true, false);
        return ERROR_NOT_SUPPORTED;
    }

    if (!(nic->status & NIC_STATUS_ACTIVE)) {
        hardware_update_packet_stats(true, false);
        return ERROR_BUSY;
    }

    result = nic->ops->send_packet(nic, packet, length);
    hardware_update_packet_stats(true, result == SUCCESS);

    return result;
}

int hardware_receive_packet(nic_info_t *nic, uint8_t *buffer, size_t *length) {
    int result;

    if (!nic || !buffer || !length) {
        hardware_update_packet_stats(false, false);
        return ERROR_INVALID_PARAM;
    }

    if (!nic->ops || !nic->ops->receive_packet) {
        hardware_update_packet_stats(false, false);
        return ERROR_NOT_SUPPORTED;
    }

    result = nic->ops->receive_packet(nic, buffer, length);
    hardware_update_packet_stats(false, result == SUCCESS);

    return result;
}

/* ============================================================================
 * Interrupt Control - Runtime
 * ============================================================================ */

int hardware_enable_interrupts(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    if (!nic->ops || !nic->ops->enable_interrupts) {
        return ERROR_NOT_SUPPORTED;
    }

    return nic->ops->enable_interrupts(nic);
}

int hardware_disable_interrupts(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    if (!nic->ops || !nic->ops->disable_interrupts) {
        return ERROR_NOT_SUPPORTED;
    }

    return nic->ops->disable_interrupts(nic);
}

int hardware_clear_interrupts(nic_info_t *nic) {
    if (!nic) {
        return -1;
    }

    /* If NIC has disable_interrupts op, use it to clear pending interrupts */
    if (nic->ops && nic->ops->disable_interrupts) {
        int rc = nic->ops->disable_interrupts(nic);
        if (rc != SUCCESS) {
            LOG_WARNING("Failed to clear interrupts on NIC: %d", rc);
            return rc;
        }
    }

    return SUCCESS;
}

/* ============================================================================
 * Link Status - Runtime
 * ============================================================================ */

int hardware_get_link_status(nic_info_t *nic) {
    if (!nic) {
        return 0;
    }

    if (!nic->ops || !nic->ops->get_link_status) {
        return nic->link_up ? 1 : 0;
    }

    return nic->ops->get_link_status(nic);
}

int hardware_get_link_speed(nic_info_t *nic) {
    if (!nic) {
        return 0;
    }

    if (!nic->ops || !nic->ops->get_link_speed) {
        return nic->speed;
    }

    return nic->ops->get_link_speed(nic);
}

bool hardware_is_link_up(nic_info_t *nic) {
    if (!nic) {
        return false;
    }

    return hardware_get_link_status(nic) != 0;
}

/* ============================================================================
 * Statistics - Runtime
 * ============================================================================ */

int hardware_get_stats(nic_info_t *nic, void *stats) {
    if (!nic || !stats) {
        return ERROR_INVALID_PARAM;
    }

    if (nic->ops && nic->ops->get_statistics) {
        return nic->ops->get_statistics(nic, stats);
    }

    return ERROR_NOT_SUPPORTED;
}

int hardware_clear_stats(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    if (nic->ops && nic->ops->clear_statistics) {
        return nic->ops->clear_statistics(nic);
    }

    /* Reset local stats */
    nic->tx_packets = 0;
    nic->rx_packets = 0;
    nic->tx_bytes = 0;
    nic->rx_bytes = 0;
    nic->tx_errors = 0;
    nic->rx_errors = 0;

    return SUCCESS;
}

/* ============================================================================
 * Promiscuous/Multicast Mode - Runtime
 * ============================================================================ */

int hardware_set_promiscuous_mode(nic_info_t *nic, bool enable) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    if (!nic->ops || !nic->ops->set_promiscuous) {
        return ERROR_NOT_SUPPORTED;
    }

    return nic->ops->set_promiscuous(nic, enable);
}

int hardware_set_multicast_filter(nic_info_t *nic, const uint8_t *mc_list, int count) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    if (!nic->ops || !nic->ops->set_multicast) {
        return ERROR_NOT_SUPPORTED;
    }

    return nic->ops->set_multicast(nic, mc_list, count);
}

/* ============================================================================
 * Self Test - Runtime
 * ============================================================================ */

int hardware_self_test_nic(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    if (!nic->ops || !nic->ops->self_test) {
        return ERROR_NOT_SUPPORTED;
    }

    return nic->ops->self_test(nic);
}

/* ============================================================================
 * Print NIC Info - Runtime (for diagnostics)
 * ============================================================================ */

void hardware_print_nic_info(const nic_info_t *nic) {
    if (!nic) {
        return;
    }

    LOG_INFO("NIC Info:");
    LOG_INFO("  Type: %d, Index: %d", nic->type, nic->index);
    LOG_INFO("  IO Base: 0x%04X, IRQ: %d", nic->io_base, nic->irq);
    LOG_INFO("  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             nic->mac[0], nic->mac[1], nic->mac[2],
             nic->mac[3], nic->mac[4], nic->mac[5]);
    LOG_INFO("  Link: %s, Speed: %d Mbps",
             nic->link_up ? "UP" : "DOWN", nic->speed);
    LOG_INFO("  TX: %lu pkts, RX: %lu pkts",
             (unsigned long)nic->tx_packets, (unsigned long)nic->rx_packets);
}
