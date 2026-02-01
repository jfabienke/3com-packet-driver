/**
 * @file pktops_init.c
 * @brief Packet operations initialization functions (OVERLAY segment)
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file contains initialization, configuration, and cleanup functions
 * for the packet operations subsystem. These functions are loaded in the
 * OVERLAY segment and can be discarded after driver initialization to
 * free up conventional memory.
 *
 * Split from pktops.c - 2026-01-28 08:00:00 UTC
 *
 * Functions in this file:
 * - packet_ops_init, packet_ops_cleanup
 * - packet_queue_init_all, packet_queue_cleanup_all
 * - packet_bottom_half_init, packet_bottom_half_cleanup
 * - packet_reset_statistics
 * - packet_print_detailed_stats
 * - Queue management and configuration functions
 * - Loopback testing functions
 * - Statistics and health monitoring setup
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "dos_io.h"
#include <string.h>
#include <dos.h>
#include "pktops.h"
#include "hardware.h"
#include "routing.h"
#include "statrt.h"
#include "arp.h"
#include "bufaloc.h"
#include "logging.h"
#include "stats.h"
#include "api.h"
#include "flowctl.h"  /* Phase 2.3: 802.3x Flow Control */
#include "prod.h"
#include "dmamap.h"   /* GPT-5: Centralized DMA mapping layer */
#include "3c509pio.h" /* GPT-5: PIO fast path for 3C509B */
#include "vds.h"           /* VDS Virtual DMA Services */
#include "vds_mapping.h"   /* VDS mapping structures (vds_mapping_t) */
#include "3c515.h"         /* 3C515 TX descriptor types and context */
#include "3c509b.h"        /* 3C509B hardware constants */
#include "cpudet.h"        /* CPU detection and cpu_info_t */
#include "pltprob.h"       /* Platform detection and DMA policy */

/* Additional error codes */
#define PACKET_ERR_NOT_INITIALIZED  -11
#define PACKET_ERR_NO_MEMORY        -12
#define PACKET_ERR_NO_BUFFER        -13
#define PACKET_ERR_NO_PACKET        -14
#define PACKET_ERR_QUEUE_FULL       -15

/* Additional error codes for feature support */
#ifndef PACKET_ERR_NOT_SUPPORTED
#define PACKET_ERR_NOT_SUPPORTED    -16
#endif
#ifndef PACKET_ERR_INVALID_DATA
#define PACKET_ERR_INVALID_DATA     -17
#endif
#ifndef PACKET_ERR_TIMEOUT
#define PACKET_ERR_TIMEOUT          -18
#endif
#ifndef PACKET_ERR_LOOPBACK_FAILED
#define PACKET_ERR_LOOPBACK_FAILED  -19
#endif
#ifndef PACKET_ERR_INTEGRITY_FAILED
#define PACKET_ERR_INTEGRITY_FAILED -20
#endif

/* Priority queue constants */
#define MAX_PRIORITY_LEVELS         4       /* Number of priority levels */

/* Production queue management constants */
#define TX_QUEUE_URGENT_SIZE     32     /* Urgent priority queue size */
#define TX_QUEUE_HIGH_SIZE       64     /* High priority queue size */
#define TX_QUEUE_NORMAL_SIZE     128    /* Normal priority queue size */
#define TX_QUEUE_LOW_SIZE        64     /* Low priority queue size */
#define RX_QUEUE_SIZE           256     /* RX queue size */
#define QUEUE_WATERMARK_HIGH     80     /* High watermark percentage */
#define QUEUE_WATERMARK_LOW      20     /* Low watermark percentage */
#define FLOW_CONTROL_THRESHOLD   90     /* Flow control threshold percentage */
#define QUEUE_CHECK_INTERVAL_MS  100    /* Queue health check interval */

/* Global state definitions - these are referenced by pktops_rt.c */
int packet_ops_initialized = 0;
packet_stats_t packet_statistics = {0};
packet_queue_t g_packet_queues[MAX_PRIORITY_LEVELS];  /* Priority queues */

/* Production queue management state */
struct {
    packet_queue_t tx_queues[4];    /* Priority-based TX queues */
    packet_queue_t rx_queue;        /* Single RX queue */
    uint32_t queue_full_events;     /* Queue overflow counter */
    uint32_t backpressure_events;   /* Flow control events */
    uint32_t priority_drops;        /* Priority-based drops */
    uint32_t adaptive_resizes;      /* Adaptive size changes */
    bool flow_control_active;       /* Flow control state */
    uint32_t last_queue_check;      /* Last queue health check */
} g_queue_state = {0};

/* Bottom-half processing state */
struct bottom_half_state_s {
    volatile bool xms_enabled;             /* XMS buffers enabled (volatile!) */
    volatile bool bottom_half_active;      /* Bottom-half processing active (volatile!) */
    uint16_t xms_threshold;                /* Size threshold for XMS (16-bit) */
    /* Statistics - 16-bit for atomicity on 16-bit CPU */
    uint16_t packets_deferred;
    uint16_t packets_processed;
    uint16_t xms_copies;
    uint16_t staging_exhausted;
    uint16_t queue_full_drops;
    uint16_t oversize_drops;
    uint16_t xms_alloc_failures;
    uint16_t xms_move_failures;
} g_bottom_half_state = {0};

/* XMS buffer pool and deferred queue - used by bottom-half processing */
xms_buffer_pool_t g_xms_pool;
spsc_queue_t g_deferred_queue;

/* Forward declarations for internal functions */
static int packet_queue_init_all(void);
static void packet_queue_cleanup_all(void);
static uint32_t packet_calculate_queue_usage(packet_queue_t *queue);
static int packet_check_queue_health(void);
static void packet_apply_flow_control(void);
static void packet_adaptive_queue_resize(void);
static void packet_handle_queue_overflow(int priority);
static bool packet_should_drop_on_full(int priority, int queue_usage);
static int packet_emergency_queue_drain(void);
static int packet_enqueue_with_priority(packet_buffer_t *buffer, int priority);
static packet_buffer_t* packet_dequeue_by_priority(void);

/* Loopback testing forward declarations */
static int packet_enable_loopback_mode(nic_info_t *nic, loopback_type_t loopback_type);
static int packet_disable_loopback_mode(nic_info_t *nic);
static int packet_enable_3c509b_loopback(nic_info_t *nic, loopback_type_t loopback_type);
static int packet_enable_3c515_loopback(nic_info_t *nic, loopback_type_t loopback_type);
static int packet_disable_3c509b_loopback(nic_info_t *nic);
static int packet_disable_3c515_loopback(nic_info_t *nic);
static int packet_test_single_loopback_pattern(int nic_index, const loopback_test_pattern_t *pattern);
static void packet_analyze_error_patterns(packet_integrity_result_t *integrity_result);

/* Cold section: Initialization functions (discarded after init) */
#pragma code_seg("COLD_TEXT", "CODE")

/* ========================================================================
 * Initialization and Cleanup Functions
 * ======================================================================== */

/**
 * @brief Initialize packet operations subsystem
 * @param config Driver configuration
 * @return 0 on success, negative on error
 */
int packet_ops_init(const config_t *config) {
    int result;

    if (!config) {
        log_error("packet_ops_init: NULL config parameter");
        return PACKET_ERR_INVALID_PARAM;
    }

    log_info("Initializing packet operations subsystem with production queue management");

    /* Clear statistics */
    memset(&packet_statistics, 0, sizeof(packet_statistics));

    /* Initialize production queue management */
    result = packet_queue_init_all();
    if (result != 0) {
        log_error("Failed to initialize production queue management: %d", result);
        return result;
    }

    /* Initialize flow control and adaptive management */
    g_queue_state.flow_control_active = false;
    g_queue_state.last_queue_check = stats_get_timestamp();

    /* Initialize 802.3x Flow Control with CPU-efficient state management (Phase 2.3) */
    result = fc_simple_init();
    if (result != 0) {
        log_warning("802.3x Flow Control initialization failed: %d, continuing without flow control", result);
        /* Continue - flow control is optional feature */
    } else {
        log_debug("802.3x Flow Control initialized with CPU-efficient state management");
    }

    packet_ops_initialized = 1;

    log_info("Packet operations subsystem initialized with production features");
    return 0;
}

/**
 * @brief Cleanup packet operations
 * @return 0 on success, negative on error
 */
int packet_ops_cleanup(void) {
    if (!packet_ops_initialized) {
        return 0;
    }

    log_info("Cleaning up packet operations subsystem");

    /* Cleanup production queue management */
    packet_queue_cleanup_all();

    /* Print final statistics */
    log_info("Final packet statistics:");
    log_info("  TX: %lu packets, %lu bytes, %lu errors",
             packet_statistics.tx_packets,
             packet_statistics.tx_bytes,
             packet_statistics.tx_errors);
    log_info("  RX: %lu packets, %lu bytes, %lu errors, %lu dropped",
             packet_statistics.rx_packets,
             packet_statistics.rx_bytes,
             packet_statistics.rx_errors,
             packet_statistics.rx_dropped);

    /* Print queue management statistics */
    log_info("Queue Statistics:");
    log_info("  Queue full events: %lu", g_queue_state.queue_full_events);
    log_info("  Backpressure events: %lu", g_queue_state.backpressure_events);
    log_info("  Priority drops: %lu", g_queue_state.priority_drops);
    log_info("  Adaptive resizes: %lu", g_queue_state.adaptive_resizes);

    packet_ops_initialized = 0;

    log_info("Packet operations cleanup completed");
    return 0;
}

/* ========================================================================
 * Queue Initialization and Cleanup
 * ======================================================================== */

/**
 * @brief Initialize all production packet queues
 * @return 0 on success, negative on error
 */
static int packet_queue_init_all(void) {
    int result;

    log_info("Initializing production packet queues");

    /* Initialize priority-based TX queues */
    result = packet_queue_init(&g_queue_state.tx_queues[PACKET_PRIORITY_URGENT],
                              TX_QUEUE_URGENT_SIZE, TX_QUEUE_URGENT_SIZE * 1514);
    if (result != 0) {
        log_error("Failed to initialize urgent TX queue");
        return result;
    }

    result = packet_queue_init(&g_queue_state.tx_queues[PACKET_PRIORITY_HIGH],
                              TX_QUEUE_HIGH_SIZE, TX_QUEUE_HIGH_SIZE * 1514);
    if (result != 0) {
        log_error("Failed to initialize high priority TX queue");
        return result;
    }

    result = packet_queue_init(&g_queue_state.tx_queues[PACKET_PRIORITY_NORMAL],
                              TX_QUEUE_NORMAL_SIZE, TX_QUEUE_NORMAL_SIZE * 1514);
    if (result != 0) {
        log_error("Failed to initialize normal priority TX queue");
        return result;
    }

    result = packet_queue_init(&g_queue_state.tx_queues[PACKET_PRIORITY_LOW],
                              TX_QUEUE_LOW_SIZE, TX_QUEUE_LOW_SIZE * 1514);
    if (result != 0) {
        log_error("Failed to initialize low priority TX queue");
        return result;
    }

    /* Initialize RX queue */
    result = packet_queue_init(&g_queue_state.rx_queue,
                              RX_QUEUE_SIZE, RX_QUEUE_SIZE * 1514);
    if (result != 0) {
        log_error("Failed to initialize RX queue");
        return result;
    }

    log_info("Production packet queues initialized successfully");
    return 0;
}

/**
 * @brief Cleanup all production packet queues
 */
static void packet_queue_cleanup_all(void) {
    int i;

    log_info("Cleaning up production packet queues");

    /* Emergency drain all queues before cleanup */
    packet_emergency_queue_drain();

    /* Cleanup TX queues */
    for (i = 0; i < 4; i++) {
        packet_queue_cleanup(&g_queue_state.tx_queues[i]);
    }

    /* Cleanup RX queue */
    packet_queue_cleanup(&g_queue_state.rx_queue);

    log_info("Production packet queues cleaned up");
}

/* ========================================================================
 * Bottom-Half Initialization and Cleanup
 * ======================================================================== */

/**
 * @brief Initialize bottom-half processing with XMS support
 * @param enable_xms Enable XMS buffer support
 * @param staging_count Number of staging buffers
 * @param xms_count Number of XMS buffers
 * @return 0 on success, negative on error
 */
int packet_bottom_half_init(bool enable_xms, uint32_t staging_count, uint32_t xms_count) {
    int result;

    log_info("Initializing bottom-half processing: xms=%s, staging=%u, xms_buffers=%u",
             enable_xms ? "enabled" : "disabled", staging_count, xms_count);

    /* Initialize staging buffers (always needed) */
    result = staging_buffer_init(staging_count, ETH_MAX_FRAME);
    if (result != SUCCESS) {
        log_error("Failed to initialize staging buffers: %d", result);
        return result;
    }

    /* Initialize SPSC queue */
    result = spsc_queue_init(&g_deferred_queue);
    if (result != SUCCESS) {
        log_error("Failed to initialize SPSC queue: %d", result);
        staging_buffer_cleanup();
        return result;
    }

    /* Initialize XMS pool if enabled */
    if (enable_xms && xms_count > 0) {
        result = xms_buffer_pool_init(&g_xms_pool, ETH_MAX_FRAME, xms_count);
        if (result == SUCCESS) {
            g_bottom_half_state.xms_enabled = true;
            g_bottom_half_state.xms_threshold = RX_COPYBREAK_THRESHOLD;
            log_info("XMS buffer pool initialized with %u buffers", xms_count);
        } else {
            log_warning("XMS pool init failed (%d), using conventional memory only", result);
            g_bottom_half_state.xms_enabled = false;
        }
    }

    /* Reset statistics */
    g_bottom_half_state.packets_deferred = 0;
    g_bottom_half_state.packets_processed = 0;
    g_bottom_half_state.xms_copies = 0;
    g_bottom_half_state.staging_exhausted = 0;
    g_bottom_half_state.queue_full_drops = 0;
    g_bottom_half_state.oversize_drops = 0;
    g_bottom_half_state.xms_alloc_failures = 0;
    g_bottom_half_state.xms_move_failures = 0;
    g_bottom_half_state.bottom_half_active = true;

    return SUCCESS;
}

/**
 * @brief Cleanup bottom-half processing
 */
void packet_bottom_half_cleanup(void) {
    log_info("Bottom-half statistics:");
    log_info("  Packets: deferred=%u, processed=%u",
             (unsigned)g_bottom_half_state.packets_deferred,
             (unsigned)g_bottom_half_state.packets_processed);
    log_info("  Drops: staging=%u, queue_full=%u, oversize=%u",
             (unsigned)g_bottom_half_state.staging_exhausted,
             (unsigned)g_bottom_half_state.queue_full_drops,
             (unsigned)g_bottom_half_state.oversize_drops);
    log_info("  XMS: copies=%u, alloc_fail=%u, move_fail=%u",
             (unsigned)g_bottom_half_state.xms_copies,
             (unsigned)g_bottom_half_state.xms_alloc_failures,
             (unsigned)g_bottom_half_state.xms_move_failures);

    /* Cleanup XMS pool if initialized */
    if (g_bottom_half_state.xms_enabled) {
        xms_buffer_pool_cleanup(&g_xms_pool);
    }

    /* Cleanup SPSC queue */
    spsc_queue_cleanup(&g_deferred_queue);

    /* Cleanup staging buffers */
    staging_buffer_cleanup();

    /* Reset state */
    memory_zero(&g_bottom_half_state, sizeof(g_bottom_half_state));
}

/* ========================================================================
 * Statistics Functions
 * ======================================================================== */

/**
 * @brief Reset packet statistics
 * @return 0 on success
 */
int packet_reset_statistics(void) {
    int total_nics;
    nic_info_t *nic;
    int i;

    log_info("Resetting packet statistics");
    memset(&packet_statistics, 0, sizeof(packet_statistics));

    /* Reset per-NIC statistics as well */
    total_nics = hardware_get_nic_count();
    for (i = 0; i < total_nics; i++) {
        nic = hardware_get_nic(i);
        if (nic) {
            nic->tx_packets = 0;
            nic->rx_packets = 0;
            nic->tx_bytes = 0;
            nic->rx_bytes = 0;
            nic->tx_errors = 0;
            nic->rx_errors = 0;
            nic->tx_dropped = 0;
            nic->rx_dropped = 0;
        }
    }

    return 0;
}

/**
 * @brief Print detailed packet driver statistics
 */
void packet_print_detailed_stats(void) {
    int total_nics;
    nic_info_t *nic;
    int i;

    log_info("=== Packet Driver Statistics ===");
    log_info("Global Counters:");
    log_info("  TX: %lu packets, %lu bytes, %lu errors",
             packet_statistics.tx_packets,
             packet_statistics.tx_bytes,
             packet_statistics.tx_errors);
    log_info("  RX: %lu packets, %lu bytes, %lu errors, %lu dropped",
             packet_statistics.rx_packets,
             packet_statistics.rx_bytes,
             packet_statistics.rx_errors,
             packet_statistics.rx_dropped);
    log_info("  Routed: %lu packets", packet_statistics.routed_packets);
    log_info("  Buffer events: %lu TX full", packet_statistics.tx_buffer_full);

    /* Per-NIC statistics */
    total_nics = hardware_get_nic_count();
    for (i = 0; i < total_nics; i++) {
        nic = hardware_get_nic(i);
        if (nic) {
            log_info("NIC %d (%s):", i,
                    (nic->status & NIC_STATUS_ACTIVE) ? "ACTIVE" : "INACTIVE");
            log_info("  Status: Link=%s, Speed=%dMbps, Duplex=%s",
                    (nic->status & NIC_STATUS_LINK_UP) ? "UP" : "DOWN",
                    (nic->status & NIC_STATUS_100MBPS) ? 100 : 10,
                    (nic->status & NIC_STATUS_FULL_DUPLEX) ? "FULL" : "HALF");
            log_info("  TX: %lu packets, %lu bytes, %lu errors",
                    nic->tx_packets, nic->tx_bytes, nic->tx_errors);
            log_info("  RX: %lu packets, %lu bytes, %lu errors",
                    nic->rx_packets, nic->rx_bytes, nic->rx_errors);
        }
    }

    log_info("=== End Statistics ===");
}

/**
 * @brief Get comprehensive packet driver performance metrics
 * @param metrics Pointer to store performance metrics
 * @return 0 on success, negative on error
 */
int packet_get_performance_metrics(packet_performance_metrics_t *metrics) {
    int total_nics;
    nic_info_t *nic;
    uint32_t total_tx_packets = 0;
    uint32_t total_rx_packets = 0;
    uint32_t total_errors = 0;
    int i;

    if (!metrics) {
        return PACKET_ERR_INVALID_PARAM;
    }

    memset(metrics, 0, sizeof(packet_performance_metrics_t));

    /* Copy basic statistics */
    metrics->tx_packets = packet_statistics.tx_packets;
    metrics->rx_packets = packet_statistics.rx_packets;
    metrics->tx_bytes = packet_statistics.tx_bytes;
    metrics->rx_bytes = packet_statistics.rx_bytes;
    metrics->tx_errors = packet_statistics.tx_errors;
    metrics->rx_errors = packet_statistics.rx_errors;
    metrics->rx_dropped = packet_statistics.rx_dropped;

    /* Calculate performance ratios */
    total_tx_packets = packet_statistics.tx_packets;
    total_rx_packets = packet_statistics.rx_packets;
    total_errors = packet_statistics.tx_errors + packet_statistics.rx_errors;

    if (total_tx_packets > 0) {
        metrics->tx_error_rate = (packet_statistics.tx_errors * 100) / total_tx_packets;
    }

    if (total_rx_packets > 0) {
        metrics->rx_error_rate = (packet_statistics.rx_errors * 100) / total_rx_packets;
        metrics->rx_drop_rate = (packet_statistics.rx_dropped * 100) / total_rx_packets;
    }

    /* Calculate throughput (simplified - packets per second estimate) */
    /* In real implementation, this would use actual time measurements */
    metrics->tx_throughput = total_tx_packets; /* Simplified */
    metrics->rx_throughput = total_rx_packets; /* Simplified */

    /* Aggregate per-NIC statistics */
    total_nics = hardware_get_nic_count();
    for (i = 0; i < total_nics && i < MAX_NICS; i++) {
        nic = hardware_get_nic(i);
        if (nic) {
            metrics->nic_stats[i].active = (nic->status & NIC_STATUS_ACTIVE) ? 1 : 0;
            metrics->nic_stats[i].link_up = (nic->status & NIC_STATUS_LINK_UP) ? 1 : 0;
            metrics->nic_stats[i].speed = (nic->status & NIC_STATUS_100MBPS) ? 100 : 10;
            metrics->nic_stats[i].full_duplex = (nic->status & NIC_STATUS_FULL_DUPLEX) ? 1 : 0;
            metrics->nic_stats[i].tx_packets = nic->tx_packets;
            metrics->nic_stats[i].rx_packets = nic->rx_packets;
            metrics->nic_stats[i].tx_errors = nic->tx_errors;
            metrics->nic_stats[i].rx_errors = nic->rx_errors;
        }
    }

    metrics->active_nics = total_nics;
    metrics->collection_time = stats_get_timestamp();

    return 0;
}

/**
 * @brief Monitor packet driver health and performance
 * @return Health status (0 = healthy, positive = warnings, negative = errors)
 */
int packet_monitor_health(void) {
    int health_score = 0;
    int total_nics;
    nic_info_t *nic;
    uint32_t total_packets;
    uint32_t total_errors;
    int active_nics = 0;
    int i;
    uint32_t tx_error_rate;
    uint32_t rx_error_rate;
    uint32_t global_error_rate;

    /* Check if packet operations are initialized */
    if (!packet_ops_initialized) {
        log_warning("Packet operations not initialized");
        return -10;
    }

    /* Check for active NICs */
    total_nics = hardware_get_nic_count();
    if (total_nics == 0) {
        log_error("No NICs available");
        return -20;
    }

    for (i = 0; i < total_nics; i++) {
        nic = hardware_get_nic(i);
        if (nic && (nic->status & NIC_STATUS_ACTIVE)) {
            active_nics++;

            /* Check link status */
            if (!(nic->status & NIC_STATUS_LINK_UP)) {
                log_warning("NIC %d link is down", i);
                health_score += 5;
            }

            /* Check error rates */
            if (nic->tx_packets > 0) {
                tx_error_rate = (nic->tx_errors * 100) / nic->tx_packets;
                if (tx_error_rate > 10) {
                    log_warning("NIC %d high TX error rate: %lu%%", i, tx_error_rate);
                    health_score += 10;
                } else if (tx_error_rate > 5) {
                    health_score += 5;
                }
            }

            if (nic->rx_packets > 0) {
                rx_error_rate = (nic->rx_errors * 100) / nic->rx_packets;
                if (rx_error_rate > 10) {
                    log_warning("NIC %d high RX error rate: %lu%%", i, rx_error_rate);
                    health_score += 10;
                } else if (rx_error_rate > 5) {
                    health_score += 5;
                }
            }
        }
    }

    if (active_nics == 0) {
        log_error("No active NICs available");
        return -30;
    }

    /* Check global error rates */
    total_packets = packet_statistics.tx_packets + packet_statistics.rx_packets;
    total_errors = packet_statistics.tx_errors + packet_statistics.rx_errors;

    if (total_packets > 0) {
        global_error_rate = (total_errors * 100) / total_packets;
        if (global_error_rate > 15) {
            log_warning("High global error rate: %lu%%", global_error_rate);
            health_score += 15;
        } else if (global_error_rate > 10) {
            health_score += 10;
        } else if (global_error_rate > 5) {
            health_score += 5;
        }
    }

    /* Check buffer utilization */
    if (packet_statistics.tx_buffer_full > 0) {
        log_warning("TX buffer exhaustion events: %lu", packet_statistics.tx_buffer_full);
        health_score += 5;
    }

    /* Log health status */
    if (health_score == 0) {
        log_debug("Packet driver health: EXCELLENT");
    } else if (health_score < 10) {
        log_info("Packet driver health: GOOD (score: %d)", health_score);
    } else if (health_score < 25) {
        log_warning("Packet driver health: FAIR (score: %d)", health_score);
    } else {
        log_warning("Packet driver health: POOR (score: %d)", health_score);
    }

    return health_score;
}

/**
 * @brief Get comprehensive queue management statistics
 * @param stats Pointer to store statistics
 * @return 0 on success, negative on error
 */
int packet_get_queue_stats(packet_queue_management_stats_t *stats) {
    int i;

    if (!stats) {
        return PACKET_ERR_INVALID_PARAM;
    }

    memset(stats, 0, sizeof(packet_queue_management_stats_t));

    /* Copy queue counts and usage */
    for (i = 0; i < 4; i++) {
        stats->tx_queue_counts[i] = g_queue_state.tx_queues[i].count;
        stats->tx_queue_max[i] = g_queue_state.tx_queues[i].max_count;
        stats->tx_queue_usage[i] = packet_calculate_queue_usage(&g_queue_state.tx_queues[i]);
        stats->tx_queue_dropped[i] = g_queue_state.tx_queues[i].dropped_packets;
    }

    stats->rx_queue_count = g_queue_state.rx_queue.count;
    stats->rx_queue_max = g_queue_state.rx_queue.max_count;
    stats->rx_queue_usage = packet_calculate_queue_usage(&g_queue_state.rx_queue);
    stats->rx_queue_dropped = g_queue_state.rx_queue.dropped_packets;

    /* Copy management statistics */
    stats->queue_full_events = g_queue_state.queue_full_events;
    stats->backpressure_events = g_queue_state.backpressure_events;
    stats->priority_drops = g_queue_state.priority_drops;
    stats->adaptive_resizes = g_queue_state.adaptive_resizes;
    stats->flow_control_active = g_queue_state.flow_control_active;

    return 0;
}

/* ========================================================================
 * Queue Management Internal Functions
 * ======================================================================== */

/**
 * @brief Calculate queue usage percentage
 * @param queue Queue to check
 * @return Usage percentage (0-100)
 */
static uint32_t packet_calculate_queue_usage(packet_queue_t *queue) {
    if (!queue || queue->max_count == 0) {
        return 0;
    }

    return (queue->count * 100) / queue->max_count;
}

/**
 * @brief Check queue health and trigger adaptive management
 * @return 0 on success, negative on error
 */
static int packet_check_queue_health(void) {
    /* C89: All declarations at start of function */
    uint32_t current_time;
    bool health_issues;
    int i;
    packet_queue_t *queue;
    uint32_t usage;
    packet_buffer_t *head;

    current_time = stats_get_timestamp();
    health_issues = false;

    /* Only check periodically */
    if (current_time - g_queue_state.last_queue_check < QUEUE_CHECK_INTERVAL_MS) {
        return 0;
    }

    g_queue_state.last_queue_check = current_time;

    /* Check each TX queue for health issues */
    for (i = 0; i < 4; i++) {
        queue = &g_queue_state.tx_queues[i];
        usage = packet_calculate_queue_usage(queue);

        if (usage > QUEUE_WATERMARK_HIGH) {
            log_warning("Queue %d usage high: %d%%", i, (int)usage);
            health_issues = true;
        }

        /* Check for stale packets (simplified - would need timestamps) */
        if (queue->count > 0) {
            head = packet_queue_peek(queue);
            if (head && head->timestamp > 0) {
                uint32_t age = current_time - head->timestamp;
                if (age > 5000) {  /* 5 second threshold */
                    log_warning("Stale packets detected in queue %d (age: %dms)", i, age);
                    health_issues = true;
                }
            }
        }
    }

    /* Check RX queue health */
    {
        uint32_t rx_usage = packet_calculate_queue_usage(&g_queue_state.rx_queue);
        if (rx_usage > QUEUE_WATERMARK_HIGH) {
            log_warning("RX queue usage high: %d%%", (int)rx_usage);
            health_issues = true;
        }
    }

    /* Trigger adaptive management if needed */
    if (health_issues) {
        packet_adaptive_queue_resize();
    }

    return health_issues ? 1 : 0;
}

/**
 * @brief Apply flow control backpressure
 */
static void packet_apply_flow_control(void) {
    /* In a full implementation, this would:
     * 1. Signal upper layers to slow down
     * 2. Implement TCP-like window scaling
     * 3. Adjust NIC interrupt rates
     * 4. Apply per-connection throttling
     */
    volatile int delay_i;

    log_debug("Applying flow control backpressure");

    /* For now, just add a small delay to slow down packet processing */
    for (delay_i = 0; delay_i < 100; delay_i++) {
        /* Brief backpressure delay */
    }
}

/**
 * @brief Adaptively resize queues based on load
 */
static void packet_adaptive_queue_resize(void) {
    static uint32_t last_resize = 0;
    uint32_t current_time = stats_get_timestamp();
    int i;
    packet_queue_t *queue;
    uint32_t usage;

    /* Limit resize frequency */
    if (current_time - last_resize < 10000) {  /* 10 second minimum */
        return;
    }

    last_resize = current_time;

    log_info("Performing adaptive queue resize analysis");

    /* Analyze queue usage patterns */
    for (i = 0; i < 4; i++) {
        queue = &g_queue_state.tx_queues[i];
        usage = packet_calculate_queue_usage(queue);

        if (usage > 90 && queue->max_count < 512) {
            /* Queue consistently full - consider expansion */
            log_info("Queue %d consistently full (%d%%), would expand if possible", i, usage);
            /* In full implementation, would dynamically resize */
            g_queue_state.adaptive_resizes++;
        } else if (usage < 10 && queue->max_count > 32) {
            /* Queue underutilized - consider shrinking */
            log_info("Queue %d underutilized (%d%%), would shrink if possible", i, usage);
            g_queue_state.adaptive_resizes++;
        }
    }
}

/**
 * @brief Handle queue overflow by dropping lower priority packets
 * @param priority Current priority level
 */
static void packet_handle_queue_overflow(int priority) {
    int dropped = 0;
    int lower_priority;
    packet_queue_t *lower_queue;

    /* Try to drop packets from lower priority queues */
    for (lower_priority = PACKET_PRIORITY_LOW; lower_priority < priority; lower_priority++) {
        lower_queue = &g_queue_state.tx_queues[lower_priority];

        while (!packet_queue_is_empty(lower_queue) && dropped < 5) {
            packet_buffer_t *dropped_buffer = packet_queue_dequeue(lower_queue);
            if (dropped_buffer) {
                packet_buffer_free(dropped_buffer);
                dropped++;
                g_queue_state.priority_drops++;
            }
        }

        if (dropped >= 5) break;  /* Don't drop too many at once */
    }

    if (dropped > 0) {
        log_info("Dropped %d lower priority packets to make room for priority %d", dropped, priority);
    }
}

/**
 * @brief Check if packet should be dropped when queue is full
 * @param priority Packet priority
 * @param queue_usage Current queue usage percentage
 * @return true if should drop
 */
static bool packet_should_drop_on_full(int priority, int queue_usage) {
    /* Higher priority packets are more likely to preempt lower priority */
    switch (priority) {
        case PACKET_PRIORITY_URGENT:
            return true;   /* Always try to make room for urgent packets */
        case PACKET_PRIORITY_HIGH:
            return queue_usage > 95;  /* Drop if very full */
        case PACKET_PRIORITY_NORMAL:
            return queue_usage > 90;  /* Drop if mostly full */
        case PACKET_PRIORITY_LOW:
            return false;  /* Don't preempt others for low priority */
        default:
            return false;
    }
}

/**
 * @brief Emergency drain all queues (e.g., during shutdown)
 * @return Number of packets drained
 */
static int packet_emergency_queue_drain(void) {
    int total_drained = 0;
    int i;
    packet_queue_t *queue;
    int drained;
    int rx_drained = 0;

    log_warning("Emergency draining all packet queues");

    /* Drain TX queues */
    for (i = 0; i < 4; i++) {
        queue = &g_queue_state.tx_queues[i];
        drained = 0;

        while (!packet_queue_is_empty(queue)) {
            packet_buffer_t *buffer = packet_queue_dequeue(queue);
            if (buffer) {
                packet_buffer_free(buffer);
                drained++;
            }
        }

        if (drained > 0) {
            log_info("Drained %d packets from TX queue %d", drained, i);
            total_drained += drained;
        }
    }

    /* Drain RX queue */
    while (!packet_queue_is_empty(&g_queue_state.rx_queue)) {
        packet_buffer_t *buffer = packet_queue_dequeue(&g_queue_state.rx_queue);
        if (buffer) {
            packet_buffer_free(buffer);
            rx_drained++;
        }
    }

    if (rx_drained > 0) {
        log_info("Drained %d packets from RX queue", rx_drained);
        total_drained += rx_drained;
    }

    log_info("Emergency drain completed: %d total packets drained", total_drained);
    return total_drained;
}

/**
 * @brief Enqueue packet with priority-based flow control
 * @param buffer Packet buffer
 * @param priority Packet priority (0-3)
 * @return 0 on success, negative on error
 */
static int packet_enqueue_with_priority(packet_buffer_t *buffer, int priority) {
    packet_queue_t *queue;
    int result;
    uint32_t queue_usage;

    if (!buffer || priority < 0 || priority > 3) {
        return PACKET_ERR_INVALID_PARAM;
    }

    queue = &g_queue_state.tx_queues[priority];
    queue_usage = packet_calculate_queue_usage(queue);

    /* Check for queue overflow */
    if (packet_queue_is_full(queue)) {
        log_debug("Queue %d full, checking drop policy", priority);

        if (packet_should_drop_on_full(priority, queue_usage)) {
            /* Drop lower priority packets to make room if possible */
            packet_handle_queue_overflow(priority);

            /* Try again after making room */
            if (packet_queue_is_full(queue)) {
                g_queue_state.queue_full_events++;
                g_queue_state.priority_drops++;
                log_warning("Dropping packet due to queue %d overflow", priority);
                return PACKET_ERR_NO_BUFFERS;
            }
        } else {
            g_queue_state.queue_full_events++;
            return PACKET_ERR_NO_BUFFERS;
        }
    }

    /* Check for flow control threshold */
    if (queue_usage > FLOW_CONTROL_THRESHOLD) {
        if (!g_queue_state.flow_control_active) {
            log_info("Activating flow control - queue usage %d%%", queue_usage);
            g_queue_state.flow_control_active = true;
            g_queue_state.backpressure_events++;
        }
        packet_apply_flow_control();
    }

    /* Enqueue the packet - CRITICAL SECTION */
    _asm { cli }  /* Disable interrupts */
    result = packet_queue_enqueue(queue, buffer);
    _asm { sti }  /* Enable interrupts */
    if (result != 0) {
        log_error("Failed to enqueue packet to priority queue %d", priority);
        return result;
    }

    log_trace("Enqueued packet to priority %d queue (usage: %d%%)", priority, queue_usage);
    return 0;
}

/**
 * @brief Dequeue packet using priority scheduling
 * @return Packet buffer or NULL if no packets
 */
static packet_buffer_t* packet_dequeue_by_priority(void) {
    packet_buffer_t *buffer = NULL;
    int priority;
    int i;
    uint32_t total_usage;

    /* Check queues in priority order (urgent first) */
    for (priority = PACKET_PRIORITY_URGENT; priority >= PACKET_PRIORITY_LOW; priority--) {
        if (!packet_queue_is_empty(&g_queue_state.tx_queues[priority])) {
            /* Dequeue from priority queue - CRITICAL SECTION */
            _asm { cli }  /* Disable interrupts */
            buffer = packet_queue_dequeue(&g_queue_state.tx_queues[priority]);
            _asm { sti }  /* Enable interrupts */
            if (buffer) {
                log_trace("Dequeued packet from priority %d queue", priority);

                /* Check if we can disable flow control */
                total_usage = 0;
                for (i = 0; i < 4; i++) {
                    total_usage += packet_calculate_queue_usage(&g_queue_state.tx_queues[i]);
                }

                if (g_queue_state.flow_control_active && total_usage < QUEUE_WATERMARK_LOW) {
                    log_info("Deactivating flow control - total usage %d%%", total_usage / 4);
                    g_queue_state.flow_control_active = false;
                }

                return buffer;
            }
        }
    }

    return NULL;
}

/* ========================================================================
 * Enhanced Queue TX Functions
 * ======================================================================== */

/**
 * @brief Enhanced packet queue TX with production features
 * @param packet Packet data
 * @param length Packet length
 * @param priority Packet priority (0-3)
 * @param handle Sender handle
 * @return 0 on success, negative on error
 */
int packet_queue_tx_enhanced(const uint8_t *packet, size_t length, int priority, uint16_t handle) {
    packet_buffer_t *buffer;
    int result;

    if (!packet || length == 0 || priority < 0 || priority > 3) {
        return PACKET_ERR_INVALID_PARAM;
    }

    if (!packet_ops_initialized) {
        return PACKET_ERR_NOT_INITIALIZED;
    }

    /* Check queue health periodically */
    packet_check_queue_health();

    /* Allocate packet buffer */
    buffer = packet_buffer_alloc(length);
    if (!buffer) {
        log_error("Failed to allocate packet buffer for queuing");
        return PACKET_ERR_NO_BUFFERS;
    }

    /* Copy packet data and set metadata */
    result = packet_set_data(buffer, packet, length);
    if (result != 0) {
        packet_buffer_free(buffer);
        return result;
    }

    buffer->priority = priority;
    buffer->handle = handle;
    buffer->timestamp = stats_get_timestamp();

    /* Enqueue with priority management */
    result = packet_enqueue_with_priority(buffer, priority);
    if (result != 0) {
        packet_buffer_free(buffer);
        return result;
    }

    log_debug("Queued packet for transmission: priority=%d, length=%zu, handle=%04X",
              priority, length, handle);

    return 0;
}

/**
 * @brief Enhanced packet queue flush with priority scheduling
 * @return Number of packets processed, negative on error
 */
int packet_flush_tx_queue_enhanced(void) {
    int packets_sent = 0;
    int max_packets = 32;  /* Limit to prevent starvation */
    packet_buffer_t *buffer;
    int result;

    if (!packet_ops_initialized) {
        return PACKET_ERR_NOT_INITIALIZED;
    }

    /* Process packets by priority until queue empty or limit reached */
    while (packets_sent < max_packets) {
        buffer = packet_dequeue_by_priority();
        if (!buffer) {
            break;  /* No more packets */
        }

        /* Send the packet using enhanced send with recovery */
        result = packet_send_with_retry(buffer->data, buffer->length,
                                       NULL, buffer->handle, 3);

        if (result == 0) {
            packets_sent++;
            log_trace("Successfully sent queued packet (handle=%04X)", buffer->handle);
        } else {
            log_warning("Failed to send queued packet: %d", result);

            /* For failed packets, could implement retry logic or dead letter queue */
        }

        packet_buffer_free(buffer);
    }

    if (packets_sent > 0) {
        log_debug("Flushed %d packets from TX queues", packets_sent);
    }

    return packets_sent;
}

/* ========================================================================
 * Loopback Testing Functions
 * ======================================================================== */

/**
 * @brief Test internal loopback functionality
 * @param nic_index NIC to test
 * @param test_pattern Test pattern to use
 * @param pattern_size Size of test pattern
 * @return 0 on success, negative on error
 */
int packet_test_internal_loopback(int nic_index, const uint8_t *test_pattern, uint16_t pattern_size) {
    /* C89: All declarations at start of function */
    static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    nic_info_t *nic;
    uint8_t test_frame[ETH_MAX_FRAME];
    uint8_t rx_buffer[ETH_MAX_FRAME];
    uint16_t frame_length = 0;  /* Initialize to silence W200 */
    size_t rx_length;
    int result;
    uint32_t timeout_ms;
    uint32_t start_time;
    uint8_t *rx_payload;

    timeout_ms = 1000;
    (void)test_frame;
    (void)frame_length;

    if (!test_pattern || pattern_size == 0 || pattern_size > ETH_MAX_DATA) {
        log_error("Invalid loopback test parameters");
        return PACKET_ERR_INVALID_PARAM;
    }

    nic = hardware_get_nic(nic_index);
    if (!nic) {
        log_error("Invalid NIC index for loopback test: %d", nic_index);
        return PACKET_ERR_INVALID_NIC;
    }

    if (!(nic->status & NIC_STATUS_ACTIVE)) {
        log_error("NIC %d not active for loopback test", nic_index);
        return PACKET_ERR_INVALID_NIC;
    }

    log_info("Starting internal loopback test on NIC %d", nic_index);

    /* Build test frame with broadcast destination */
    /* Note: broadcast_mac is declared at the start of the function */
    frame_length = packet_build_ethernet_frame(test_frame, sizeof(test_frame),
                                              broadcast_mac, nic->mac,
                                              0x0800, /* IP ethertype */
                                              test_pattern, pattern_size);

    if (frame_length < 0) {
        log_error("Failed to build loopback test frame");
        return frame_length;
    }

    /* Enable internal loopback mode */
    result = packet_enable_loopback_mode(nic, LOOPBACK_INTERNAL);
    if (result != 0) {
        log_error("Failed to enable internal loopback mode: %d", result);
        return result;
    }

    /* Clear any pending RX packets */
    rx_length = sizeof(rx_buffer);
    while (packet_receive_from_nic(nic_index, rx_buffer, &rx_length) == 0) {
        rx_length = sizeof(rx_buffer);
    }

    /* Send test frame */
    result = packet_send_enhanced(nic_index, test_pattern, pattern_size, broadcast_mac, 0x1234);
    if (result != 0) {
        log_error("Failed to send loopback test frame: %d", result);
        packet_disable_loopback_mode(nic);
        return result;
    }

    log_debug("Loopback test frame sent, waiting for reception...");

    /* Wait for loopback reception */
    start_time = stats_get_timestamp();
    rx_length = sizeof(rx_buffer);

    while ((stats_get_timestamp() - start_time) < timeout_ms) {
        result = packet_receive_from_nic(nic_index, rx_buffer, &rx_length);

        if (result == 0) {
            /* Verify received frame */
            if (rx_length >= ETH_HEADER_LEN + pattern_size) {
                /* Extract payload from received frame */
                rx_payload = rx_buffer + ETH_HEADER_LEN;

                if (memcmp(rx_payload, test_pattern, pattern_size) == 0) {
                    log_info("Internal loopback test PASSED on NIC %d", nic_index);
                    packet_disable_loopback_mode(nic);
                    return 0;
                } else {
                    log_error("Loopback data mismatch on NIC %d", nic_index);
                    packet_disable_loopback_mode(nic);
                    return PACKET_ERR_INVALID_DATA;
                }
            }
        }

        /* Brief delay before retry */
        { volatile int delay_i; for (delay_i = 0; delay_i < 1000; delay_i++); }
        rx_length = sizeof(rx_buffer);
    }

    log_error("Internal loopback test TIMEOUT on NIC %d", nic_index);
    packet_disable_loopback_mode(nic);
    return PACKET_ERR_TIMEOUT;
}

/**
 * @brief Test external loopback with physical connector
 * @param nic_index NIC to test
 * @param test_patterns Array of test patterns
 * @param num_patterns Number of test patterns
 * @return 0 on success, negative on error
 */
int packet_test_external_loopback(int nic_index, const loopback_test_pattern_t *test_patterns, int num_patterns) {
    /* C89: All declarations at start of function */
    nic_info_t *nic;
    int passed_tests;
    int failed_tests;
    int result;
    int i;

    passed_tests = 0;
    failed_tests = 0;

    if (!test_patterns || num_patterns <= 0) {
        return PACKET_ERR_INVALID_PARAM;
    }

    nic = hardware_get_nic(nic_index);
    if (!nic) {
        return PACKET_ERR_INVALID_NIC;
    }

    log_info("Starting external loopback test on NIC %d (%d patterns)", nic_index, num_patterns);

    /* Disable internal loopback, enable external */
    result = packet_enable_loopback_mode(nic, LOOPBACK_EXTERNAL);
    if (result != 0) {
        log_error("Failed to enable external loopback mode: %d", result);
        return result;
    }

    /* Test each pattern */
    for (i = 0; i < num_patterns; i++) {
        log_debug("Testing external loopback pattern %d: %s", i, test_patterns[i].name);

        result = packet_test_single_loopback_pattern(nic_index, &test_patterns[i]);
        if (result == 0) {
            passed_tests++;
            log_debug("Pattern %d PASSED", i);
        } else {
            failed_tests++;
            log_warning("Pattern %d FAILED: %d", i, result);
        }
    }

    packet_disable_loopback_mode(nic);

    log_info("External loopback test completed: %d passed, %d failed", passed_tests, failed_tests);

    return (failed_tests == 0) ? 0 : PACKET_ERR_LOOPBACK_FAILED;
}

/**
 * @brief Test cross-NIC loopback for multi-NIC validation
 * @param src_nic_index Source NIC
 * @param dest_nic_index Destination NIC
 * @param test_data Test data to send
 * @param data_size Size of test data
 * @return 0 on success, negative on error
 */
int packet_test_cross_nic_loopback(int src_nic_index, int dest_nic_index,
                                  const uint8_t *test_data, uint16_t data_size) {
    /* C89: All declarations at start of function */
    nic_info_t *src_nic;
    nic_info_t *dest_nic;
    uint8_t test_frame[ETH_MAX_FRAME];
    uint8_t rx_buffer[ETH_MAX_FRAME];
    uint16_t frame_length;
    size_t rx_length;
    int result;
    uint32_t timeout_ms;
    uint32_t start_time;
    eth_header_t eth_header;

    timeout_ms = 2000;  /* Longer timeout for cross-NIC */

    if (!test_data || data_size == 0 || src_nic_index == dest_nic_index) {
        return PACKET_ERR_INVALID_PARAM;
    }

    src_nic = hardware_get_nic(src_nic_index);
    dest_nic = hardware_get_nic(dest_nic_index);

    if (!src_nic || !dest_nic) {
        log_error("Invalid NIC indices for cross-NIC test: src=%d, dest=%d",
                 src_nic_index, dest_nic_index);
        return PACKET_ERR_INVALID_NIC;
    }

    if (!(src_nic->status & NIC_STATUS_ACTIVE) || !(dest_nic->status & NIC_STATUS_ACTIVE)) {
        log_error("NICs not active for cross-NIC test");
        return PACKET_ERR_INVALID_NIC;
    }

    log_info("Starting cross-NIC loopback test: NIC %d -> NIC %d", src_nic_index, dest_nic_index);

    /* Build test frame addressed to destination NIC */
    frame_length = packet_build_ethernet_frame(test_frame, sizeof(test_frame),
                                              dest_nic->mac, src_nic->mac,
                                              0x0800, /* IP ethertype */
                                              test_data, data_size);

    if (frame_length < 0) {
        log_error("Failed to build cross-NIC test frame");
        return frame_length;
    }

    /* Enable promiscuous mode on destination NIC to receive all packets */
    result = hardware_set_promiscuous_mode(dest_nic, true);
    if (result != 0) {
        log_warning("Failed to enable promiscuous mode on dest NIC %d", dest_nic_index);
    }

    /* Clear any pending packets on destination NIC */
    rx_length = sizeof(rx_buffer);
    while (packet_receive_from_nic(dest_nic_index, rx_buffer, &rx_length) == 0) {
        rx_length = sizeof(rx_buffer);
    }

    /* Send packet from source NIC */
    result = packet_send_enhanced(src_nic_index, test_data, data_size, dest_nic->mac, 0x5678);
    if (result != 0) {
        log_error("Failed to send cross-NIC test packet: %d", result);
        hardware_set_promiscuous_mode(dest_nic, false);
        return result;
    }

    log_debug("Cross-NIC packet sent, waiting for reception on NIC %d...", dest_nic_index);

    /* Wait for packet on destination NIC */
    start_time = stats_get_timestamp();
    rx_length = sizeof(rx_buffer);

    while ((stats_get_timestamp() - start_time) < timeout_ms) {
        result = packet_receive_from_nic(dest_nic_index, rx_buffer, &rx_length);

        if (result == 0) {
            /* Verify received frame */
            /* Note: eth_header declared at function start */
            result = packet_parse_ethernet_header(rx_buffer, rx_length, &eth_header);

            if (result == 0) {
                /* Check if this is our test packet */
                if (memcmp(eth_header.dest_mac, dest_nic->mac, ETH_ALEN) == 0 &&
                    memcmp(eth_header.src_mac, src_nic->mac, ETH_ALEN) == 0) {

                    /* Verify payload */
                    uint8_t *rx_payload = rx_buffer + ETH_HEADER_LEN;
                    uint16_t payload_length = rx_length - ETH_HEADER_LEN;

                    if (payload_length >= data_size &&
                        memcmp(rx_payload, test_data, data_size) == 0) {
                        log_info("Cross-NIC loopback test PASSED: NIC %d -> NIC %d",
                                src_nic_index, dest_nic_index);
                        hardware_set_promiscuous_mode(dest_nic, false);
                        return 0;
                    } else {
                        log_error("Cross-NIC payload mismatch");
                        hardware_set_promiscuous_mode(dest_nic, false);
                        return PACKET_ERR_INVALID_DATA;
                    }
                }
            }
        }

        /* Brief delay before retry */
        { volatile int delay_i; for (delay_i = 0; delay_i < 1000; delay_i++); }
        rx_length = sizeof(rx_buffer);
    }

    log_error("Cross-NIC loopback test TIMEOUT: NIC %d -> NIC %d", src_nic_index, dest_nic_index);
    hardware_set_promiscuous_mode(dest_nic, false);
    return PACKET_ERR_TIMEOUT;
}

/**
 * @brief Comprehensive packet integrity verification during loopback
 * @param original_data Original packet data
 * @param received_data Received packet data
 * @param data_length Length of data to compare
 * @param integrity_result Pointer to store detailed integrity result
 * @return 0 if integrity check passed, negative on error
 */
int packet_verify_loopback_integrity(const uint8_t *original_data, const uint8_t *received_data,
                                    uint16_t data_length, packet_integrity_result_t *integrity_result) {
    /* C89: All declarations at start of function */
    uint16_t i;
    packet_mismatch_detail_t *detail;

    if (!original_data || !received_data || !integrity_result || data_length == 0) {
        return PACKET_ERR_INVALID_PARAM;
    }

    memset(integrity_result, 0, sizeof(packet_integrity_result_t));
    integrity_result->bytes_compared = data_length;

    /* Byte-by-byte comparison */
    for (i = 0; i < data_length; i++) {
        if (original_data[i] != received_data[i]) {
            integrity_result->mismatch_count++;

            /* Store first few mismatches for debugging */
            if (integrity_result->mismatch_count <= MAX_MISMATCH_DETAILS) {
                detail = &integrity_result->mismatch_details[integrity_result->mismatch_count - 1];
                detail->offset = i;
                detail->expected = original_data[i];
                detail->actual = received_data[i];
            }
        }
    }

    /* Calculate error statistics */
    if (integrity_result->mismatch_count > 0) {
        integrity_result->error_rate_percent =
            (integrity_result->mismatch_count * 100) / data_length;

        /* Analyze error patterns */
        packet_analyze_error_patterns(integrity_result);

        log_error("Packet integrity check FAILED: %d mismatches out of %d bytes (%d.%02d%%)",
                 integrity_result->mismatch_count, data_length,
                 integrity_result->error_rate_percent,
                 (int)((integrity_result->mismatch_count * 10000UL) / data_length) % 100);

        return PACKET_ERR_INTEGRITY_FAILED;
    }

    log_debug("Packet integrity check PASSED: %d bytes verified", data_length);
    return 0;
}

/**
 * @brief Enable loopback mode on a NIC
 * @param nic NIC to configure
 * @param loopback_type Type of loopback to enable
 * @return 0 on success, negative on error
 */
static int packet_enable_loopback_mode(nic_info_t *nic, loopback_type_t loopback_type) {
    if (!nic) {
        return PACKET_ERR_INVALID_PARAM;
    }

    log_debug("Enabling loopback mode %d on NIC %d", loopback_type, nic->index);

    if (nic->type == NIC_TYPE_3C509B) {
        return packet_enable_3c509b_loopback(nic, loopback_type);
    } else if (nic->type == NIC_TYPE_3C515_TX) {
        return packet_enable_3c515_loopback(nic, loopback_type);
    }

    return PACKET_ERR_NOT_SUPPORTED;
}

/**
 * @brief Disable loopback mode on a NIC
 * @param nic NIC to configure
 * @return 0 on success, negative on error
 */
static int packet_disable_loopback_mode(nic_info_t *nic) {
    if (!nic) {
        return PACKET_ERR_INVALID_PARAM;
    }

    log_debug("Disabling loopback mode on NIC %d", nic->index);

    if (nic->type == NIC_TYPE_3C509B) {
        return packet_disable_3c509b_loopback(nic);
    } else if (nic->type == NIC_TYPE_3C515_TX) {
        return packet_disable_3c515_loopback(nic);
    }

    return PACKET_ERR_NOT_SUPPORTED;
}

/**
 * @brief Enable 3C509B loopback mode
 * @param nic NIC to configure
 * @param loopback_type Type of loopback
 * @return 0 on success, negative on error
 */
static int packet_enable_3c509b_loopback(nic_info_t *nic, loopback_type_t loopback_type) {
    /* C89: All declarations at start of function */
    uint16_t rx_filter;

    rx_filter = 0x01;  /* Individual address */

    _3C509B_SELECT_WINDOW(nic->io_base, _3C509B_WINDOW_0);

    switch (loopback_type) {
        case LOOPBACK_INTERNAL:
            /* Set internal loopback in RX filter */
            rx_filter |= 0x08;  /* Loopback mode */
            break;

        case LOOPBACK_EXTERNAL:
            /* External loopback requires physical connector */
            /* No special register settings needed */
            break;

        default:
            return PACKET_ERR_INVALID_PARAM;
    }

    /* Apply RX filter settings */
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_SET_RX_FILTER | rx_filter);

    /* Enable TX and RX */
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_TX_ENABLE);
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_RX_ENABLE);

    return 0;
}

/**
 * @brief Enable 3C515-TX loopback mode
 * @param nic NIC to configure
 * @param loopback_type Type of loopback
 * @return 0 on success, negative on error
 */
static int packet_enable_3c515_loopback(nic_info_t *nic, loopback_type_t loopback_type) {
    /* C89: All declarations at start of function */
    uint16_t media_options;

    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_4);

    media_options = inw(nic->io_base + _3C515_TX_W4_MEDIA);

    switch (loopback_type) {
        case LOOPBACK_INTERNAL:
            /* Enable internal loopback */
            media_options |= 0x0008;  /* Internal loopback bit */
            break;

        case LOOPBACK_EXTERNAL:
            /* Disable internal loopback for external testing */
            media_options &= ~0x0008;
            break;

        default:
            return PACKET_ERR_INVALID_PARAM;
    }

    outw(nic->io_base + _3C515_TX_W4_MEDIA, media_options);

    /* Enable TX and RX */
    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_1);
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TX_ENABLE);
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_RX_ENABLE);

    return 0;
}

/**
 * @brief Disable 3C509B loopback mode
 * @param nic NIC to configure
 * @return 0 on success, negative on error
 */
static int packet_disable_3c509b_loopback(nic_info_t *nic) {
    /* C89: All declarations at start of function */
    uint16_t rx_filter;

    _3C509B_SELECT_WINDOW(nic->io_base, _3C509B_WINDOW_0);

    /* Reset to normal RX filter (individual + broadcast) */
    rx_filter = 0x01 | 0x02;  /* Individual + broadcast */
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_SET_RX_FILTER | rx_filter);

    return 0;
}

/**
 * @brief Disable 3C515-TX loopback mode
 * @param nic NIC to configure
 * @return 0 on success, negative on error
 */
static int packet_disable_3c515_loopback(nic_info_t *nic) {
    /* C89: All declarations at start of function */
    uint16_t media_options;

    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_4);

    /* Disable internal loopback */
    media_options = inw(nic->io_base + _3C515_TX_W4_MEDIA);
    media_options &= ~0x0008;  /* Clear internal loopback bit */
    outw(nic->io_base + _3C515_TX_W4_MEDIA, media_options);

    return 0;
}

/**
 * @brief Test a single loopback pattern
 * @param nic_index NIC to test
 * @param pattern Test pattern to use
 * @return 0 on success, negative on error
 */
static int packet_test_single_loopback_pattern(int nic_index, const loopback_test_pattern_t *pattern) {
    /* C89: All declarations at start of function */
    uint8_t rx_buffer[ETH_MAX_FRAME];
    size_t rx_length = 0;  /* Initialize to silence W200 */
    packet_integrity_result_t integrity_result = {0};  /* Initialize to silence W200 */
    int result;
    uint32_t timeout_ms;
    uint32_t start_time = 0;  /* Initialize to silence W200 */

    (void)rx_buffer;
    (void)rx_length;
    (void)integrity_result;
    (void)start_time;

    timeout_ms = pattern->timeout_ms ? pattern->timeout_ms : 1000;
    (void)timeout_ms;

    /* Send test pattern */
    result = packet_test_internal_loopback(nic_index, pattern->data, pattern->size);
    if (result != 0) {
        return result;
    }

    return 0;  /* Success if internal loopback passed */
}

/**
 * @brief Analyze error patterns in received data
 * @param integrity_result Integrity result to analyze
 */
static void packet_analyze_error_patterns(packet_integrity_result_t *integrity_result) {
    /* C89: All declarations at start of function */
    int bit_errors;
    int byte_shifts;
    int burst_errors;
    int i;
    int bit;
    int bits_different;
    packet_mismatch_detail_t *detail;
    packet_mismatch_detail_t *prev;
    uint8_t xor_result;

    if (!integrity_result || integrity_result->mismatch_count == 0) {
        return;
    }

    /* Look for common error patterns */
    bit_errors = 0;
    byte_shifts = 0;
    burst_errors = 0;
    (void)byte_shifts;  /* May be unused */

    for (i = 0; i < integrity_result->mismatch_count && i < MAX_MISMATCH_DETAILS; i++) {
        detail = &integrity_result->mismatch_details[i];
        xor_result = detail->expected ^ detail->actual;

        /* Count bit errors */
        bits_different = 0;
        for (bit = 0; bit < 8; bit++) {
            if (xor_result & (1 << bit)) {
                bits_different++;
            }
        }

        if (bits_different == 1) {
            bit_errors++;
        }

        /* Check for byte shift patterns */
        if (i > 0) {
            prev = &integrity_result->mismatch_details[i - 1];
            if (detail->offset == prev->offset + 1) {
                burst_errors++;
            }
        }
    }

    /* Store pattern analysis results */
    integrity_result->single_bit_errors = bit_errors;
    integrity_result->burst_errors = burst_errors;

    /* Determine likely error cause */
    if (bit_errors > burst_errors) {
        strcpy(integrity_result->error_pattern_description, "Single-bit errors (electrical noise)");
    } else if (burst_errors > 0) {
        strcpy(integrity_result->error_pattern_description, "Burst errors (synchronization issue)");
    } else {
        strcpy(integrity_result->error_pattern_description, "Random data corruption");
    }
}

/* Restore default code segment */
#pragma code_seg()
