/**
 * @file hardware.c
 * @brief Hardware abstraction layer with polymorphic NIC operations
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include "hardware.h"
#include "hwhal.h"
#include "nicctx.h"
#include "halerr.h"
#include "regacc.h"
#include "nic_init.h"
#include "logging.h"
#include "memory.h"
#include "diag.h"
#include "3c509b.h"
#include "3c515.h"
#include "errhndl.h"
#include "bufaloc.h"
#include "nicbufp.h"
#include "main.h"
#include <string.h>
#include <stdlib.h>

/* Hardware statistics structure - C89 compatible */
typedef struct {
    uint32_t packets_sent;          /* Total packets sent */
    uint32_t packets_received;      /* Total packets received */
    uint32_t send_errors;           /* Send error count */
    uint32_t receive_errors;        /* Receive error count */
    uint32_t successful_sends;      /* Successful sends */
    uint32_t successful_receives;   /* Successful receives */
    uint32_t interrupts_handled;    /* Interrupts handled */
} hardware_stats_t;

/* Per-NIC recovery statistics */
typedef struct {
    uint32_t consecutive_errors;    /* Consecutive error count */
    uint32_t recovery_attempts;     /* Recovery attempts */
    uint32_t last_error_time;       /* Last error timestamp */
    uint32_t last_recovery_time;    /* Last recovery timestamp */
    uint32_t error_counts[12];      /* Error counts by type */
} nic_recovery_stats_t;

/* Hardware recovery statistics structure */
typedef struct {
    uint32_t total_failures;        /* Total failure count */
    uint32_t successful_recoveries; /* Successful recovery count */
    bool failover_active;           /* Failover currently active */
    int primary_nic;                /* Primary NIC index */
    int backup_nic;                 /* Backup NIC index */
    nic_recovery_stats_t nic_stats[MAX_NICS]; /* Per-NIC statistics */
} hardware_recovery_stats_t;

/* Global hardware state */
nic_info_t g_nic_infos[MAX_NICS];
int g_num_nics = 0;
bool g_hardware_initialized = false;

/* NIC operations vtables - forward declarations */
static nic_ops_t g_3c509b_ops;
static nic_ops_t g_3c515_ops;

/* NOTE: HAL vtable instances removed 2026-01-25 - dead code.
 * The C nic_ops_t vtable is the production path.
 */

/* Private hardware state */
static hardware_stats_t g_hardware_stats;

/* PnP detection results storage */
static nic_detect_info_t g_pnp_detection_results[MAX_NICS];
static int g_pnp_detection_count = 0;

/* Production error recovery state */
static struct {
    uint32_t error_counts[MAX_NICS][12];    /* Error counts by type per NIC */
    uint32_t last_error_time[MAX_NICS];     /* Last error timestamp per NIC */
    uint32_t consecutive_errors[MAX_NICS];  /* Consecutive error count per NIC */
    uint32_t recovery_attempts[MAX_NICS];   /* Recovery attempt count per NIC */
    uint32_t last_recovery_time[MAX_NICS];  /* Last recovery timestamp per NIC */
    bool failover_in_progress;              /* Global failover state */
    int primary_nic;                        /* Primary NIC for failover */
    int backup_nic;                         /* Backup NIC for failover */
    uint32_t total_failures;                /* Total failure count */
    uint32_t successful_recoveries;         /* Successful recovery count */
    uint32_t failovers;                     /* Failover count */
} g_error_recovery_state = {0};

/* Private helper functions */
static void hardware_reset_stats(void);
static bool hardware_validate_nic_index(int index);
static void hardware_update_packet_stats(bool sent, bool success);
static int hardware_register_nic_with_buffer_system(nic_info_t* nic, int nic_index);
static void hardware_unregister_nic_from_buffer_system(int nic_index);

/* Production error recovery functions */
static int hardware_detect_failure(nic_info_t *nic);
static int hardware_recover_nic(nic_info_t *nic, int failure_type);
static int hardware_attempt_failover(int failed_nic_index);
static void hardware_graceful_degradation(nic_info_t *nic);
static int hardware_validate_recovery(nic_info_t *nic);
static void hardware_log_failure(nic_info_t *nic, int failure_type, const char* details);
static int hardware_emergency_reset(nic_info_t *nic);
static bool hardware_is_critical_failure(int failure_type);
static void hardware_notify_application_error(int nic_index, int error_type);
static uint32_t hardware_calculate_recovery_timeout(int failure_type);

/* Production error types */
#define HW_FAILURE_NONE         0
#define HW_FAILURE_LINK_LOST    1
#define HW_FAILURE_TX_TIMEOUT   2  
#define HW_FAILURE_RX_TIMEOUT   3
#define HW_FAILURE_FIFO_OVERRUN 4
#define HW_FAILURE_DMA_ERROR    5
#define HW_FAILURE_REGISTER_CORRUPTION 6
#define HW_FAILURE_INTERRUPT_STORM 7
#define HW_FAILURE_MEMORY_ERROR 8
#define HW_FAILURE_THERMAL      9
#define HW_FAILURE_POWER        10
#define HW_FAILURE_CRITICAL     11

/* Recovery strategy types */
#define RECOVERY_SOFT_RESET     1
#define RECOVERY_HARD_RESET     2
#define RECOVERY_REINITIALIZE   3
#define RECOVERY_FAILOVER       4
#define RECOVERY_DISABLE        5

/* Failure detection thresholds */
#define MAX_CONSECUTIVE_ERRORS  5
#define MAX_ERROR_RATE_PERCENT  15
#define LINK_CHECK_INTERVAL_MS  1000
#define TX_TIMEOUT_MS          5000
#define RX_TIMEOUT_MS          2000

/* Hardware initialization and cleanup */
int hardware_init(void) {
    int result;

    if (g_hardware_initialized) {
        return SUCCESS;
    }

    LOG_INFO("Initializing hardware abstraction layer");

    /* Initialize NIC array */
    memory_zero(g_nic_infos, sizeof(g_nic_infos));
    g_num_nics = 0;

    /* Initialize statistics */
    hardware_reset_stats();

    /* Initialize NIC detection and initialization system */
    result = nic_init_system();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize NIC system: %d", result);
        return result;
    }
    
    /* Initialize error handling system */
    result = hardware_init_error_handling();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize error handling system: %d", result);
        return result;
    }
    
    /* Detect and initialize all NICs */
    g_num_nics = nic_init_all_detected();
    if (g_num_nics < 0) {
        LOG_WARNING("No NICs detected or initialized");
        g_num_nics = 0;
    }
    
    /* Create error handling contexts and register NICs with buffer system */
    {
        int i;
        for (i = 0; i < g_num_nics; i++) {
        result = hardware_create_error_context(&g_nic_infos[i]);
        if (result != SUCCESS) {
            LOG_WARNING("Failed to create error context for NIC %d: %d", i, result);
        }
        
        /* Register NIC with per-NIC buffer pool system */
        result = hardware_register_nic_with_buffer_system(&g_nic_infos[i], i);
        if (result != SUCCESS) {
            LOG_WARNING("Failed to register NIC %d with buffer system: %d", i, result);
        }
        }
    }

    g_hardware_initialized = true;
    
    LOG_INFO("Hardware layer initialized with %d NICs and error handling", g_num_nics);
    
    return SUCCESS;
}

/* Forward declaration for nic_irq_uninstall */
extern void nic_irq_uninstall(void);

void hardware_cleanup(void) {
    int i;

    if (!g_hardware_initialized) {
        return;
    }

    LOG_INFO("Shutting down hardware layer");

    /* Restore original IRQ vector before tearing down NICs */
    nic_irq_uninstall();

    /* Cleanup all NICs */
    for (i = 0; i < g_num_nics; i++) {
        /* Unregister from buffer system first */
        hardware_unregister_nic_from_buffer_system(i);

        if (g_nic_infos[i].ops && g_nic_infos[i].ops->cleanup) {
            g_nic_infos[i].ops->cleanup(&g_nic_infos[i]);
        }
    }

    /* Cleanup NIC initialization system */
    nic_init_cleanup();

    /* Cleanup error handling system */
    hardware_cleanup_error_handling();

    g_num_nics = 0;
    g_hardware_initialized = false;
}

/**
 * @brief Get the primary (first active) NIC for testing
 *
 * Phase 5: Moved from hwstubs.c - now a real implementation.
 * This is used for DMA capability testing after hardware init.
 * Returns the first NIC that is present and initialized.
 *
 * @return Pointer to primary NIC or NULL if none available
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

    LOG_WARNING("No primary NIC available for testing");
    return NULL;
}

/**
 * @brief Clear pending NIC interrupts
 *
 * Phase 5: Moved from hwstubs.c - now a real implementation.
 * Reads/acknowledges pending interrupt status on the specified NIC.
 *
 * @param nic Pointer to NIC info structure
 * @return 0 on success, negative on error
 */
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

/* NIC operations vtable management */
nic_ops_t* get_nic_ops(nic_type_t type) {
    static bool vtables_initialized = false;
    
    /* Initialize vtables on first call */
    if (!vtables_initialized) {
        init_3c509b_ops();
        init_3c515_ops();
        vtables_initialized = true;
    }
    
    switch (type) {
        case NIC_TYPE_3C509B:
            return &g_3c509b_ops;
        case NIC_TYPE_3C515_TX:
            return &g_3c515_ops;
        default:
            return NULL;
    }
}

nic_ops_t* get_3c509b_ops(void) {
    return get_nic_ops(NIC_TYPE_3C509B);
}

nic_ops_t* get_3c515_ops(void) {
    return get_nic_ops(NIC_TYPE_3C515_TX);
}

int hardware_register_nic_ops(nic_type_t type, nic_ops_t *ops) {
    if (!ops) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Dynamic NIC operations registration completed via static tables */
    /* Operations are registered during hardware initialization */
    
    return SUCCESS;
}

/* NIC management */
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

/* Packet operations */
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

    if (!(nic->status & NIC_STATUS_ACTIVE)) {
        hardware_update_packet_stats(false, false);
        return ERROR_BUSY;
    }

    result = nic->ops->receive_packet(nic, buffer, length);
    hardware_update_packet_stats(false, result == SUCCESS);

    return result;
}

int hardware_send_packet_to_nic(int nic_index, const uint8_t *packet, uint16_t length) {
    nic_info_t *nic = hardware_get_nic(nic_index);
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    return hardware_send_packet(nic, packet, length);
}

int hardware_receive_packet_from_nic(int nic_index, uint8_t *buffer, uint16_t *length) {
    int result;
    size_t len;
    nic_info_t *nic = hardware_get_nic(nic_index);

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    len = (size_t)*length;
    result = hardware_receive_packet(nic, buffer, &len);
    *length = (uint16_t)len;
    return result;
}

/* Interrupt handling */
void hardware_interrupt_handler(void) {
    int i;
    nic_info_t *nic;

    for (i = 0; i < g_num_nics; i++) {
        nic = &g_nic_infos[i];

        if (!(nic->status & NIC_STATUS_ACTIVE) || !nic->ops) {
            continue;
        }
        
        /* Check if this NIC has an interrupt pending */
        if (nic->ops->check_interrupt && nic->ops->check_interrupt(nic)) {
            /* Handle the interrupt */
            if (nic->ops->handle_interrupt) {
                nic->ops->handle_interrupt(nic);
            }
        }
    }
}

int hardware_enable_interrupts(nic_info_t *nic) {
    if (!nic || !nic->ops) {
        return ERROR_INVALID_PARAM;
    }
    
    if (nic->ops->enable_interrupts) {
        return nic->ops->enable_interrupts(nic);
    }
    
    return ERROR_NOT_SUPPORTED;
}

int hardware_disable_interrupts(nic_info_t *nic) {
    if (!nic || !nic->ops) {
        return ERROR_INVALID_PARAM;
    }
    
    if (nic->ops->disable_interrupts) {
        return nic->ops->disable_interrupts(nic);
    }
    
    return ERROR_NOT_SUPPORTED;
}

/* NIC configuration */
int hardware_configure_nic(nic_info_t *nic, const nic_config_t *config) {
    if (!nic || !config) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!nic->ops || !nic->ops->configure) {
        return ERROR_NOT_SUPPORTED;
    }
    
    return nic->ops->configure(nic, config);
}

int hardware_reset_nic(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!nic->ops || !nic->ops->reset) {
        return ERROR_NOT_SUPPORTED;
    }
    
    return nic->ops->reset(nic);
}

int hardware_self_test_nic(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!nic->ops || !nic->ops->self_test) {
        return ERROR_NOT_SUPPORTED;
    }
    
    return nic->ops->self_test(nic);
}

/* Link status */
bool hardware_is_link_up(nic_info_t *nic) {
    if (!nic) {
        return false;
    }
    
    if (nic->ops && nic->ops->get_link_status) {
        return nic->ops->get_link_status(nic);
    }
    
    return nic->link_up;
}

int hardware_get_link_speed(nic_info_t *nic) {
    if (!nic) {
        return 0;
    }
    
    if (nic->ops && nic->ops->get_link_speed) {
        return nic->ops->get_link_speed(nic);
    }
    
    return nic->speed;
}

/* Statistics */
const hardware_stats_t* hardware_get_stats(void) {
    return &g_hardware_stats;
}

void hardware_clear_stats(void) {
    hardware_reset_stats();
}

void hardware_print_nic_info(const nic_info_t *nic) {
    if (!nic) {
        LOG_ERROR("Invalid NIC pointer");
        return;
    }

    LOG_INFO("NIC: Type=%d, I/O=0x%X, IRQ=%d, MAC=%02X:%02X:%02X:%02X:%02X:%02X",
             nic->type, nic->io_base, nic->irq,
             nic->mac[0], nic->mac[1], nic->mac[2],
             nic->mac[3], nic->mac[4], nic->mac[5]);
}

const char* hardware_nic_type_to_string(nic_type_t type) {
    switch (type) {
        case NIC_TYPE_3C509B:       return "3C509B";
        case NIC_TYPE_3C515_TX:     return "3C515-TX";
        default:                    return "Unknown";
    }
}

const char* hardware_nic_status_to_string(uint32_t status) {
    static char status_str[128];
    status_str[0] = '\0';
    
    if (status & NIC_STATUS_PRESENT)     strcat(status_str, "PRESENT ");
    if (status & NIC_STATUS_INITIALIZED) strcat(status_str, "INIT ");
    if (status & NIC_STATUS_ACTIVE)      strcat(status_str, "ACTIVE ");
    if (status & NIC_STATUS_LINK_UP)     strcat(status_str, "LINK_UP ");
    if (status & NIC_STATUS_ERROR)       strcat(status_str, "ERROR ");
    
    if (status_str[0] == '\0') {
        strcpy(status_str, "NONE");
    }
    
    return status_str;
}

/* Advanced features */
int hardware_set_promiscuous_mode(nic_info_t *nic, bool enable) {
    if (!nic || !nic->ops) {
        return ERROR_INVALID_PARAM;
    }
    
    if (nic->ops->set_promiscuous) {
        return nic->ops->set_promiscuous(nic, enable);
    }
    
    return ERROR_NOT_SUPPORTED;
}

int hardware_set_multicast_filter(nic_info_t *nic, const uint8_t *mc_list, int count) {
    if (!nic || !nic->ops) {
        return ERROR_INVALID_PARAM;
    }
    
    if (nic->ops->set_multicast) {
        return nic->ops->set_multicast(nic, mc_list, count);
    }
    
    return ERROR_NOT_SUPPORTED;
}

/* Private helper function implementations */
static void hardware_reset_stats(void) {
    memory_zero(&g_hardware_stats, sizeof(hardware_stats_t));
}

static bool hardware_validate_nic_index(int index) {
    return (index >= 0 && index < g_num_nics && index < MAX_NICS);
}

static void hardware_update_packet_stats(bool sent, bool success) {
    if (sent) {
        g_hardware_stats.packets_sent++;
        if (!success) {
            g_hardware_stats.send_errors++;
        }
    } else {
        g_hardware_stats.packets_received++;
        if (!success) {
            g_hardware_stats.receive_errors++;
        }
    }
}

/* NIC-specific operation implementations */

/* 3C509B Operations */
static int nic_3c509b_init(struct nic_info *nic) {
    int result;

    if (!nic) return ERROR_INVALID_PARAM;

    LOG_DEBUG("Initializing 3C509B at I/O 0x%X", nic->io_base);

    /* Reset the NIC */
    _3C509B_SELECT_WINDOW(nic->io_base, _3C509B_WINDOW_0);
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_GLOBAL_RESET);
    nic_delay_milliseconds(100);

    /* Read and set MAC address */
    result = nic_read_mac_address_3c509b(nic->io_base, nic->mac);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to read MAC address from 3C509B");
        return result;
    }
    
    /* Copy MAC to permanent MAC */
    memcpy(nic->perm_mac, nic->mac, ETH_ALEN);
    
    /* Configure basic settings */
    nic->mtu = _3C509B_MAX_MTU;
    nic->speed = 10; /* 10 Mbps */
    nic->full_duplex = false;
    nic->capabilities = HW_CAP_MULTICAST | HW_CAP_PROMISCUOUS;
    nic->status |= NIC_STATUS_INITIALIZED;
    
    LOG_INFO("3C509B initialized at I/O 0x%X, MAC %02X:%02X:%02X:%02X:%02X:%02X",
             nic->io_base, nic->mac[0], nic->mac[1], nic->mac[2],
             nic->mac[3], nic->mac[4], nic->mac[5]);
    
    return SUCCESS;
}

static int nic_3c509b_reset(struct nic_info *nic) {
    if (!nic) return ERROR_INVALID_PARAM;
    
    LOG_DEBUG("Resetting 3C509B at I/O 0x%X", nic->io_base);
    
    /* Send total reset command */
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_GLOBAL_RESET);
    nic_delay_milliseconds(100);
    
    /* Wait for reset completion */
    {
    int timeout = 1000;
    uint16_t status;
    while (timeout-- > 0) {
        status = inw(nic->io_base + _3C509B_STATUS_REG);
        if (!(status & _3C509B_STATUS_CMD_BUSY)) {
            break;
        }
        nic_delay_milliseconds(1);
    }

    if (timeout <= 0) {
        LOG_ERROR("3C509B reset timeout");
        return ERROR_TIMEOUT;
    }
    }

    return SUCCESS;
}

static int nic_3c509b_enable_interrupts(struct nic_info *nic) {
    if (!nic) return ERROR_INVALID_PARAM;
    
    _3C509B_SELECT_WINDOW(nic->io_base, _3C509B_WINDOW_1);
    outw(nic->io_base + _3C509B_COMMAND_REG, 
         _3C509B_CMD_SET_INTR_ENABLE | _3C509B_IMASK_ALL);
    
    return SUCCESS;
}

static int nic_3c509b_disable_interrupts(struct nic_info *nic) {
    if (!nic) return ERROR_INVALID_PARAM;
    
    _3C509B_SELECT_WINDOW(nic->io_base, _3C509B_WINDOW_1);
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_SET_INTR_ENABLE | 0);
    
    return SUCCESS;
}

/* 3C515-TX Operations */
static int nic_3c515_init(struct nic_info *nic) {
    int result;

    if (!nic) return ERROR_INVALID_PARAM;

    LOG_DEBUG("Initializing 3C515-TX at I/O 0x%X", nic->io_base);

    /* Reset the NIC */
    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_0);
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TOTAL_RESET);
    nic_delay_milliseconds(100);

    /* Read and set MAC address */
    result = nic_read_mac_address_3c515(nic->io_base, nic->mac);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to read MAC address from 3C515-TX");
        return result;
    }
    
    /* Copy MAC to permanent MAC */
    memcpy(nic->perm_mac, nic->mac, ETH_ALEN);
    
    /* Configure basic settings */
    nic->mtu = _3C515_TX_MAX_MTU;
    nic->speed = 100; /* Default to 100 Mbps */
    nic->full_duplex = false; /* Start with half duplex */
    nic->capabilities = HW_CAP_DMA | HW_CAP_BUS_MASTER | HW_CAP_MULTICAST | 
                       HW_CAP_PROMISCUOUS | HW_CAP_FULL_DUPLEX | HW_CAP_AUTO_SPEED;
    nic->status |= NIC_STATUS_INITIALIZED;
    
    LOG_INFO("3C515-TX initialized at I/O 0x%X, MAC %02X:%02X:%02X:%02X:%02X:%02X",
             nic->io_base, nic->mac[0], nic->mac[1], nic->mac[2],
             nic->mac[3], nic->mac[4], nic->mac[5]);
    
    return SUCCESS;
}

static int nic_3c515_reset(struct nic_info *nic) {
    if (!nic) return ERROR_INVALID_PARAM;
    
    LOG_DEBUG("Resetting 3C515-TX at I/O 0x%X", nic->io_base);
    
    /* Send total reset command */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TOTAL_RESET);
    nic_delay_milliseconds(100);
    
    /* Wait for reset completion */
    {
    int timeout = 1000;
    uint16_t status;
    while (timeout-- > 0) {
        status = inw(nic->io_base + _3C515_TX_STATUS_REG);
        if (!(status & _3C515_TX_STATUS_CMD_IN_PROGRESS)) {
            break;
        }
        nic_delay_milliseconds(1);
    }

    if (timeout <= 0) {
        LOG_ERROR("3C515-TX reset timeout");
        return ERROR_TIMEOUT;
    }
    }

    return SUCCESS;
}

static int nic_3c515_enable_interrupts(struct nic_info *nic) {
    if (!nic) return ERROR_INVALID_PARAM;
    
    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_1);
    outw(nic->io_base + _3C515_TX_COMMAND_REG, 
         _3C515_TX_CMD_SET_INTR_ENB | (_3C515_TX_IMASK_ADAPTER_FAILURE |
                                      _3C515_TX_IMASK_TX_COMPLETE |
                                      _3C515_TX_IMASK_RX_COMPLETE |
                                      _3C515_TX_IMASK_UP_COMPLETE |
                                      _3C515_TX_IMASK_DOWN_COMPLETE));
    
    return SUCCESS;
}

static int nic_3c515_disable_interrupts(struct nic_info *nic) {
    if (!nic) return ERROR_INVALID_PARAM;
    
    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_1);
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_SET_INTR_ENB | 0);
    
    return SUCCESS;
}

/* External function declarations for 3C509B */
extern int _3c509b_init(nic_info_t *nic);
extern int _3c509b_cleanup(nic_info_t *nic);
extern int _3c509b_reset(nic_info_t *nic);
extern int _3c509b_send_packet(nic_info_t *nic, const uint8_t *packet, size_t length);
extern int _3c509b_receive_packet(nic_info_t *nic, uint8_t *buffer, size_t *length);
extern int _3c509b_check_interrupt(nic_info_t *nic);
extern void _3c509b_handle_interrupt(nic_info_t *nic);
extern int _3c509b_self_test(nic_info_t *nic);
extern int _3c509b_enable_interrupts(nic_info_t *nic);
extern int _3c509b_disable_interrupts(nic_info_t *nic);
extern int _3c509b_check_tx_complete(nic_info_t *nic);
extern int _3c509b_check_rx_available(nic_info_t *nic);

/* External function declarations for 3C515 */
extern int _3c515_init(nic_info_t *nic);
extern int _3c515_cleanup(nic_info_t *nic);
extern int _3c515_reset(nic_info_t *nic);
extern int _3c515_send_packet(nic_info_t *nic, const uint8_t *packet, size_t length);
extern int _3c515_receive_packet(nic_info_t *nic, uint8_t *buffer, size_t *length);
extern int _3c515_check_interrupt(nic_info_t *nic);
extern void _3c515_handle_interrupt(nic_info_t *nic);
extern int _3c515_self_test(nic_info_t *nic);
extern int _3c515_enable_interrupts(nic_info_t *nic);
extern int _3c515_disable_interrupts(nic_info_t *nic);
extern int _3c515_check_tx_complete(nic_info_t *nic);
extern int _3c515_check_rx_available(nic_info_t *nic);

/* Initialize vtables */
static void init_3c509b_ops(void) {
    g_3c509b_ops.init = _3c509b_init;
    g_3c509b_ops.cleanup = _3c509b_cleanup;
    g_3c509b_ops.reset = _3c509b_reset;
    g_3c509b_ops.self_test = _3c509b_self_test;
    g_3c509b_ops.send_packet = _3c509b_send_packet;
    g_3c509b_ops.receive_packet = _3c509b_receive_packet;
    g_3c509b_ops.check_tx_complete = _3c509b_check_tx_complete;
    g_3c509b_ops.check_rx_available = _3c509b_check_rx_available;
    g_3c509b_ops.handle_interrupt = _3c509b_handle_interrupt;
    g_3c509b_ops.check_interrupt = _3c509b_check_interrupt;
    g_3c509b_ops.enable_interrupts = _3c509b_enable_interrupts;
    g_3c509b_ops.disable_interrupts = _3c509b_disable_interrupts;
    /* Set other operations to NULL for now */
}

static void init_3c515_ops(void) {
    g_3c515_ops.init = _3c515_init;
    g_3c515_ops.cleanup = _3c515_cleanup;
    g_3c515_ops.reset = _3c515_reset;
    g_3c515_ops.self_test = _3c515_self_test;
    g_3c515_ops.send_packet = _3c515_send_packet;
    g_3c515_ops.receive_packet = _3c515_receive_packet;
    g_3c515_ops.check_tx_complete = _3c515_check_tx_complete;
    g_3c515_ops.check_rx_available = _3c515_check_rx_available;
    g_3c515_ops.handle_interrupt = _3c515_handle_interrupt;
    g_3c515_ops.check_interrupt = _3c515_check_interrupt;
    g_3c515_ops.enable_interrupts = _3c515_enable_interrupts;
    g_3c515_ops.disable_interrupts = _3c515_disable_interrupts;
    /* Set other operations to NULL for now */
}

/* Hardware abstraction functions */
int hardware_add_nic(nic_info_t *nic) {
    if (!nic || g_num_nics >= MAX_NICS) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Copy NIC info to global array */
    memcpy(&g_nic_infos[g_num_nics], nic, sizeof(nic_info_t));
    g_nic_infos[g_num_nics].index = g_num_nics;
    
    g_num_nics++;
    
    LOG_DEBUG("Added NIC %d to hardware layer", g_num_nics - 1);
    
    return SUCCESS;
}

int hardware_remove_nic(int index) {
    int i;
    nic_info_t *nic;
    if (index < 0 || index >= g_num_nics) {
        return ERROR_INVALID_PARAM;
    }

    /* Cleanup NIC */
    nic = &g_nic_infos[index];
    if (nic->ops && nic->ops->cleanup) {
        nic->ops->cleanup(nic);
    }

    /* Shift remaining NICs down */
    for (i = index; i < g_num_nics - 1; i++) {
        memcpy(&g_nic_infos[i], &g_nic_infos[i + 1], sizeof(nic_info_t));
        g_nic_infos[i].index = i;
    }
    
    g_num_nics--;
    
    LOG_DEBUG("Removed NIC %d from hardware layer", index);
    
    return SUCCESS;
}

/**
 * @brief Detect hardware failures on a NIC
 * @param nic NIC to check
 * @return Failure type or HW_FAILURE_NONE
 */
static int hardware_detect_failure(nic_info_t *nic) {
    uint32_t current_time;
    uint32_t error_rate;
    bool link_up;
    uint32_t time_diff;

    if (!nic) {
        return HW_FAILURE_CRITICAL;
    }

    current_time = get_system_timestamp_ms();
    
    /* Check if NIC is still present and responding */
    if (!(nic->status & NIC_STATUS_PRESENT)) {
        return HW_FAILURE_CRITICAL;
    }
    
    /* Check link status */
    if (nic->ops && nic->ops->get_link_status) {
        link_up = nic->ops->get_link_status(nic);
        if (!link_up && nic->link_up) {
            /* Link just went down */
            nic->link_up = false;
            return HW_FAILURE_LINK_LOST;
        }
        nic->link_up = link_up;
    }
    
    /* Check error rate */
    if (nic->tx_packets > 100) {  /* Only check if we have meaningful sample size */
        error_rate = (nic->tx_errors * 100) / nic->tx_packets;
        if (error_rate > MAX_ERROR_RATE_PERCENT) {
            return HW_FAILURE_TX_TIMEOUT;
        }
    }
    
    if (nic->rx_packets > 100) {
        error_rate = (nic->rx_errors * 100) / nic->rx_packets;
        if (error_rate > MAX_ERROR_RATE_PERCENT) {
            return HW_FAILURE_RX_TIMEOUT;
        }
    }
    
    /* Check for interrupt storms */
    if (nic->interrupts > 0) {
        time_diff = current_time - g_error_recovery_state.last_error_time[nic->index];
        if (time_diff < 100 && g_error_recovery_state.consecutive_errors[nic->index] > 10) {
            return HW_FAILURE_INTERRUPT_STORM;
        }
    }
    
    /* Check for register corruption by attempting a basic read */
    if (nic->ops && nic->ops->self_test) {
        int test_result;
        test_result = nic->ops->self_test(nic);
        if (test_result != SUCCESS) {
            return HW_FAILURE_REGISTER_CORRUPTION;
        }
    }

    return HW_FAILURE_NONE;
}

/**
 * @brief Recover from a detected NIC failure
 * @param nic NIC to recover
 * @param failure_type Type of failure detected
 * @return 0 on success, negative on error
 */
static int hardware_recover_nic(nic_info_t *nic, int failure_type) {
    int result;
    uint32_t timeout;
    int recovery_strategy;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    LOG_INFO("Attempting recovery of NIC %d from failure type %d", nic->index, failure_type);
    
    /* Update recovery statistics */
    g_error_recovery_state.error_counts[nic->index][failure_type]++;
    g_error_recovery_state.recovery_attempts[nic->index]++;
    g_error_recovery_state.last_recovery_time[nic->index] = get_system_timestamp_ms();
    
    /* Determine recovery strategy based on failure type */
    switch (failure_type) {
        case HW_FAILURE_LINK_LOST:
            recovery_strategy = RECOVERY_SOFT_RESET;
            timeout = 2000;  /* 2 seconds for link recovery */
            break;
            
        case HW_FAILURE_TX_TIMEOUT:
        case HW_FAILURE_RX_TIMEOUT:
            recovery_strategy = RECOVERY_SOFT_RESET;
            timeout = 1000;  /* 1 second for timeout recovery */
            break;
            
        case HW_FAILURE_FIFO_OVERRUN:
            recovery_strategy = RECOVERY_REINITIALIZE;
            timeout = 3000;  /* 3 seconds for FIFO recovery */
            break;
            
        case HW_FAILURE_DMA_ERROR:
            recovery_strategy = RECOVERY_HARD_RESET;
            timeout = 5000;  /* 5 seconds for DMA recovery */
            break;
            
        case HW_FAILURE_REGISTER_CORRUPTION:
            recovery_strategy = RECOVERY_HARD_RESET;
            timeout = 5000;
            break;
            
        case HW_FAILURE_INTERRUPT_STORM:
            recovery_strategy = RECOVERY_SOFT_RESET;
            timeout = 1000;
            break;
            
        case HW_FAILURE_CRITICAL:
        case HW_FAILURE_THERMAL:
        case HW_FAILURE_POWER:
            /* Critical failures require complete reset */
            recovery_strategy = RECOVERY_DISABLE;
            timeout = 0;
            break;
            
        default:
            recovery_strategy = RECOVERY_SOFT_RESET;
            timeout = 2000;
            break;
    }
    
    /* Execute recovery strategy */
    switch (recovery_strategy) {
        case RECOVERY_SOFT_RESET:
            /* Disable NIC temporarily */
            if (nic->ops && nic->ops->disable_interrupts) {
                nic->ops->disable_interrupts(nic);
            }
            
            /* Wait brief moment */
            mdelay(100);
            
            /* Re-enable */
            if (nic->ops && nic->ops->enable_interrupts) {
                result = nic->ops->enable_interrupts(nic);
                if (result != SUCCESS) {
                    LOG_ERROR("Failed to re-enable interrupts on NIC %d", nic->index);
                    return result;
                }
            }
            break;
            
        case RECOVERY_HARD_RESET:
            /* Perform full NIC reset */
            if (nic->ops && nic->ops->reset) {
                result = nic->ops->reset(nic);
                if (result != SUCCESS) {
                    LOG_ERROR("Hard reset failed on NIC %d", nic->index);
                    return result;
                }
                
                /* Wait for reset to complete */
                mdelay(100);
            }
            break;
            
        case RECOVERY_REINITIALIZE:
            /* Complete re-initialization */
            if (nic->ops && nic->ops->cleanup) {
                nic->ops->cleanup(nic);
            }
            
            if (nic->ops && nic->ops->init) {
                result = nic->ops->init(nic);
                if (result != SUCCESS) {
                    LOG_ERROR("Re-initialization failed on NIC %d", nic->index);
                    return result;
                }
            }
            break;
            
        case RECOVERY_DISABLE:
            /* Disable the NIC permanently */
            LOG_ERROR("Disabling NIC %d due to critical failure", nic->index);
            hardware_graceful_degradation(nic);
            return ERROR_HARDWARE;
            
        default:
            LOG_ERROR("Unknown recovery strategy %d", recovery_strategy);
            return ERROR_NOT_SUPPORTED;
    }
    
    /* Validate recovery was successful */
    result = hardware_validate_recovery(nic);
    if (result != SUCCESS) {
        LOG_ERROR("Recovery validation failed for NIC %d", nic->index);
        return result;
    }
    
    /* Update statistics */
    g_error_recovery_state.successful_recoveries++;
    
    LOG_INFO("Successfully recovered NIC %d from failure type %d", nic->index, failure_type);
    
    return SUCCESS;
}

/**
 * @brief Attempt failover to backup NIC
 * @param failed_nic_index Index of failed NIC
 * @return 0 on success, negative on error
 */
static int hardware_attempt_failover(int failed_nic_index) {
    int backup_nic_index = -1;
    int i;
    nic_info_t *failed_nic, *backup_nic;
    nic_info_t *candidate;

    if (failed_nic_index < 0 || failed_nic_index >= g_num_nics) {
        return ERROR_INVALID_PARAM;
    }

    if (g_error_recovery_state.failover_in_progress) {
        LOG_WARNING("Failover already in progress, rejecting new failover request");
        return ERROR_BUSY;
    }

    g_error_recovery_state.failover_in_progress = true;
    failed_nic = &g_nic_infos[failed_nic_index];

    LOG_WARNING("Initiating failover from failed NIC %d", failed_nic_index);

    /* Find a suitable backup NIC */
    for (i = 0; i < g_num_nics; i++) {
        if (i == failed_nic_index) continue;

        candidate = &g_nic_infos[i];
        if ((candidate->status & NIC_STATUS_ACTIVE) && 
            (candidate->status & NIC_STATUS_LINK_UP) &&
            g_error_recovery_state.consecutive_errors[i] == 0) {
            
            backup_nic_index = i;
            break;
        }
    }
    
    if (backup_nic_index == -1) {
        LOG_ERROR("No suitable backup NIC found for failover");
        g_error_recovery_state.failover_in_progress = false;
        
        /* Notify application of complete network failure */
        hardware_notify_application_error(failed_nic_index, HW_FAILURE_CRITICAL);
        return ERROR_NOT_FOUND;
    }
    
    backup_nic = &g_nic_infos[backup_nic_index];
    
    LOG_INFO("Failing over from NIC %d to NIC %d", failed_nic_index, backup_nic_index);
    
    /* Update failover tracking */
    g_error_recovery_state.primary_nic = backup_nic_index;
    g_error_recovery_state.backup_nic = failed_nic_index;
    
    /* Mark failed NIC as inactive but keep it for potential recovery */
    failed_nic->status &= ~NIC_STATUS_ACTIVE;
    failed_nic->status |= NIC_STATUS_ERROR;
    
    /* Ensure backup NIC is fully operational */
    if (backup_nic->ops && backup_nic->ops->self_test) {
        int test_result;
        test_result = backup_nic->ops->self_test(backup_nic);
        if (test_result != SUCCESS) {
            LOG_ERROR("Backup NIC %d failed self-test during failover", backup_nic_index);
            g_error_recovery_state.failover_in_progress = false;
            return ERROR_HARDWARE;
        }
    }
    
    /* Notify application of successful failover */
    hardware_notify_application_error(failed_nic_index, HW_FAILURE_NONE);
    
    g_error_recovery_state.failover_in_progress = false;
    
    LOG_INFO("Failover completed successfully to NIC %d", backup_nic_index);
    
    return SUCCESS;
}

/**
 * @brief Gracefully degrade NIC service
 * @param nic NIC to degrade
 */
static void hardware_graceful_degradation(nic_info_t *nic) {
    if (!nic) return;
    
    LOG_WARNING("Initiating graceful degradation for NIC %d", nic->index);
    
    /* Disable interrupts first */
    if (nic->ops && nic->ops->disable_interrupts) {
        nic->ops->disable_interrupts(nic);
    }
    
    /* Mark as inactive but not completely failed */
    nic->status &= ~NIC_STATUS_ACTIVE;
    nic->status |= NIC_STATUS_ERROR;
    
    /* Clear any pending operations */
    if (nic->ops && nic->ops->reset) {
        nic->ops->reset(nic);
    }
    
    LOG_INFO("Graceful degradation completed for NIC %d", nic->index);
}

/**
 * @brief Validate that NIC recovery was successful
 * @param nic NIC to validate
 * @return 0 on success, negative on error
 */
static int hardware_validate_recovery(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Check basic NIC presence */
    if (!(nic->status & NIC_STATUS_PRESENT)) {
        return ERROR_HARDWARE;
    }
    
    /* Run self-test if available */
    if (nic->ops && nic->ops->self_test) {
        int test_result;
        test_result = nic->ops->self_test(nic);
        if (test_result != SUCCESS) {
            return test_result;
        }
    }

    /* Check link status */
    if (nic->ops && nic->ops->get_link_status) {
        bool link_up;
        link_up = nic->ops->get_link_status(nic);
        nic->link_up = link_up;
    }
    
    /* Mark as active if validation passes */
    nic->status |= NIC_STATUS_ACTIVE;
    nic->status &= ~NIC_STATUS_ERROR;
    
    return SUCCESS;
}

/**
 * @brief Log hardware failure details
 * @param nic Failed NIC
 * @param failure_type Type of failure
 * @param details Additional details
 */
static void hardware_log_failure(nic_info_t *nic, int failure_type, const char* details) {
    static const char* failure_names[] = {
        "None", "Link Lost", "TX Timeout", "RX Timeout", "FIFO Overrun",
        "DMA Error", "Register Corruption", "Interrupt Storm",
        "Memory Error", "Thermal", "Power", "Critical"
    };
    const char* failure_name;

    failure_name = (failure_type >= 0 && failure_type <= HW_FAILURE_CRITICAL) ?
                              failure_names[failure_type] : "Unknown";
    
    LOG_ERROR("Hardware Failure - NIC %d: %s (%d) - %s", 
              nic ? nic->index : -1, failure_name, failure_type, details ? details : "No details");
    
    /* Update global failure statistics */
    g_error_recovery_state.total_failures++;
    
    if (nic) {
        nic->error_count++;
        nic->last_error = failure_type;
    }
}

/**
 * @brief Check if failure is critical and requires immediate action
 * @param failure_type Type of failure
 * @return true if critical
 */
static bool hardware_is_critical_failure(int failure_type) {
    switch (failure_type) {
        case HW_FAILURE_CRITICAL:
        case HW_FAILURE_THERMAL:
        case HW_FAILURE_POWER:
        case HW_FAILURE_MEMORY_ERROR:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Notify application of error conditions
 * @param nic_index NIC index
 * @param error_type Error type
 */
static void hardware_notify_application_error(int nic_index, int error_type) {
    /* In a full implementation, this would notify the application layer */
    /* For now, just log the notification */
    LOG_INFO("Notifying application: NIC %d error type %d", nic_index, error_type);
    
    /* Could trigger callbacks, set global flags, or send messages to application */
}

/**
 * @brief Calculate appropriate recovery timeout based on failure type
 * @param failure_type Type of failure
 * @return Timeout in milliseconds
 */
static uint32_t hardware_calculate_recovery_timeout(int failure_type) {
    switch (failure_type) {
        case HW_FAILURE_LINK_LOST:
            return 5000;  /* 5 seconds for link recovery */
        case HW_FAILURE_TX_TIMEOUT:
        case HW_FAILURE_RX_TIMEOUT:
            return 2000;  /* 2 seconds for timeout recovery */
        case HW_FAILURE_FIFO_OVERRUN:
            return 1000;  /* 1 second for FIFO recovery */
        case HW_FAILURE_DMA_ERROR:
            return 3000;  /* 3 seconds for DMA recovery */
        case HW_FAILURE_REGISTER_CORRUPTION:
            return 5000;  /* 5 seconds for register recovery */
        default:
            return 2000;  /* Default 2 seconds */
    }
}

/**
 * @brief Emergency reset procedure for completely unresponsive NIC
 * @param nic NIC to reset
 * @return 0 on success, negative on error
 */
static int hardware_emergency_reset(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_WARNING("Performing emergency reset on NIC %d", nic->index);
    
    /* Disable all interrupts immediately */
    if (nic->ops && nic->ops->disable_interrupts) {
        nic->ops->disable_interrupts(nic);
    }
    
    /* Force hardware reset */
    if (nic->ops && nic->ops->reset) {
        int result;
        result = nic->ops->reset(nic);
        if (result != SUCCESS) {
            LOG_ERROR("Emergency reset failed on NIC %d", nic->index);
            return result;
        }
    }

    /* Wait longer for emergency reset */
    mdelay(500);

    /* Attempt to re-initialize */
    if (nic->ops && nic->ops->init) {
        int result;
        result = nic->ops->init(nic);
        if (result != SUCCESS) {
            LOG_ERROR("Post-emergency initialization failed on NIC %d", nic->index);
            return result;
        }
    }
    
    LOG_INFO("Emergency reset completed on NIC %d", nic->index);
    return SUCCESS;
}

/**
 * @brief Enhanced packet send with production error recovery
 * @param nic NIC to send on
 * @param packet Packet data
 * @param length Packet length
 * @return 0 on success, negative on error
 */
int hardware_send_packet_with_recovery(nic_info_t *nic, const uint8_t *packet, uint16_t length) {
    int result;
    int retry_count = 0;
    int max_retries = 3;
    uint32_t start_time;
    int failure_type;
    int detected_failure;

    if (!nic || !packet || length == 0) {
        hardware_update_packet_stats(true, false);
        return ERROR_INVALID_PARAM;
    }
    
    if (!nic->ops || !nic->ops->send_packet) {
        hardware_update_packet_stats(true, false);
        return ERROR_NOT_SUPPORTED;
    }
    
    start_time = get_system_timestamp_ms();
    
    while (retry_count <= max_retries) {
        /* Check for hardware failure before attempting send */
        failure_type = hardware_detect_failure(nic);
        if (failure_type != HW_FAILURE_NONE) {
            LOG_WARNING("NIC %d failure detected (type %d) before send attempt", nic->index, failure_type);
            
            /* Attempt recovery */
            result = hardware_recover_nic(nic, failure_type);
            if (result != SUCCESS) {
                LOG_ERROR("NIC %d recovery failed, attempting failover", nic->index);
                return hardware_attempt_failover(nic->index);
            }
        }
        
        /* Check if NIC is still active after potential recovery */
        if (!(nic->status & NIC_STATUS_ACTIVE)) {
            LOG_ERROR("NIC %d not active for packet transmission", nic->index);
            hardware_update_packet_stats(true, false);
            return ERROR_BUSY;
        }
        
        /* Attempt packet transmission */
        result = nic->ops->send_packet(nic, packet, length);
        
        if (result == SUCCESS) {
            /* Successful transmission */
            hardware_update_packet_stats(true, true);
            
            /* Reset consecutive error count on success */
            g_error_recovery_state.consecutive_errors[nic->index] = 0;
            
            return SUCCESS;
        }
        
        /* Handle transmission failure */
        LOG_WARNING("Packet transmission failed on NIC %d (attempt %d/%d): %d", 
                   nic->index, retry_count + 1, max_retries + 1, result);
        
        /* Update error tracking */
        g_error_recovery_state.consecutive_errors[nic->index]++;
        g_error_recovery_state.last_error_time[nic->index] = get_system_timestamp_ms();
        
        /* Determine if we should attempt recovery */
        if (result == ERROR_TIMEOUT || result == ERROR_IO || result == ERROR_HARDWARE) {
            /* Detect specific failure type based on error */
            detected_failure = (result == ERROR_TIMEOUT) ? HW_FAILURE_TX_TIMEOUT :
                          (result == ERROR_IO) ? HW_FAILURE_REGISTER_CORRUPTION :
                          HW_FAILURE_CRITICAL;
            
            /* Attempt recovery if not too many consecutive errors */
            if (g_error_recovery_state.consecutive_errors[nic->index] < MAX_CONSECUTIVE_ERRORS) {
                result = hardware_recover_nic(nic, detected_failure);
                if (result == SUCCESS) {
                    /* Recovery succeeded, try transmission again */
                    continue;
                }
            }
        }
        
        /* Check for critical failure requiring failover */
        if (hardware_is_critical_failure(result) || 
            g_error_recovery_state.consecutive_errors[nic->index] >= MAX_CONSECUTIVE_ERRORS) {
            
            LOG_ERROR("Critical failure on NIC %d, initiating failover", nic->index);
            hardware_graceful_degradation(nic);
            return hardware_attempt_failover(nic->index);
        }
        
        retry_count++;
        
        /* Brief delay before retry */
        mdelay(10 * retry_count);  /* Exponential backoff */
        
        /* Check for overall timeout */
        if (get_system_timestamp_ms() - start_time > TX_TIMEOUT_MS) {
            LOG_ERROR("Hardware send timeout exceeded for NIC %d", nic->index);
            break;
        }
    }
    
    /* All retries failed */
    hardware_update_packet_stats(true, false);
    hardware_log_failure(nic, HW_FAILURE_TX_TIMEOUT, "Packet send failed after all retries");
    
    return result;
}

/**
 * @brief Enhanced packet receive with production error recovery
 * @param nic NIC to receive from
 * @param buffer Buffer for received packet
 * @param length Pointer to buffer length (input) and actual length (output)
 * @return 0 on success, negative on error
 */
int hardware_receive_packet_with_recovery(nic_info_t *nic, uint8_t *buffer, uint16_t *length) {
    int result;
    int retry_count = 0;
    int max_retries = 2;
    uint32_t start_time;
    int failure_type;
    size_t recv_length;
    int detected_failure;

    if (!nic || !buffer || !length) {
        hardware_update_packet_stats(false, false);
        return ERROR_INVALID_PARAM;
    }
    
    if (!nic->ops || !nic->ops->receive_packet) {
        hardware_update_packet_stats(false, false);
        return ERROR_NOT_SUPPORTED;
    }
    
    start_time = get_system_timestamp_ms();
    
    while (retry_count <= max_retries) {
        /* Check for hardware failure before attempting receive */
        failure_type = hardware_detect_failure(nic);
        if (failure_type != HW_FAILURE_NONE) {
            LOG_WARNING("NIC %d failure detected (type %d) during receive", nic->index, failure_type);
            
            /* Attempt recovery for non-critical failures */
            if (!hardware_is_critical_failure(failure_type)) {
                result = hardware_recover_nic(nic, failure_type);
                if (result != SUCCESS) {
                    LOG_ERROR("NIC %d recovery failed during receive", nic->index);
                    hardware_graceful_degradation(nic);
                    return ERROR_HARDWARE;
                }
            } else {
                hardware_graceful_degradation(nic);
                return ERROR_HARDWARE;
            }
        }
        
        /* Check if NIC is still active after potential recovery */
        if (!(nic->status & NIC_STATUS_ACTIVE)) {
            LOG_ERROR("NIC %d not active for packet reception", nic->index);
            hardware_update_packet_stats(false, false);
            return ERROR_BUSY;
        }
        
        /* Attempt packet reception */
        recv_length = *length;
        result = nic->ops->receive_packet(nic, buffer, &recv_length);
        *length = recv_length;
        
        if (result == SUCCESS || result == ERROR_NO_DATA) {
            /* Successful operation (packet received or no packet available) */
            if (result == SUCCESS) {
                hardware_update_packet_stats(false, true);
                /* Reset consecutive error count on success */
                g_error_recovery_state.consecutive_errors[nic->index] = 0;
            }
            return result;
        }
        
        /* Handle reception failure */
        LOG_WARNING("Packet reception failed on NIC %d (attempt %d/%d): %d", 
                   nic->index, retry_count + 1, max_retries + 1, result);
        
        /* Update error tracking */
        g_error_recovery_state.consecutive_errors[nic->index]++;
        g_error_recovery_state.last_error_time[nic->index] = get_system_timestamp_ms();
        
        /* Determine recovery strategy based on error type */
        if (result == ERROR_TIMEOUT || result == ERROR_IO) {
            detected_failure = (result == ERROR_TIMEOUT) ? HW_FAILURE_RX_TIMEOUT : HW_FAILURE_FIFO_OVERRUN;
            
            /* Attempt recovery if not too many consecutive errors */
            if (g_error_recovery_state.consecutive_errors[nic->index] < MAX_CONSECUTIVE_ERRORS) {
                result = hardware_recover_nic(nic, detected_failure);
                if (result == SUCCESS) {
                    /* Recovery succeeded, try reception again */
                    continue;
                }
            }
        }
        
        /* Check for critical failure */
        if (hardware_is_critical_failure(result) || 
            g_error_recovery_state.consecutive_errors[nic->index] >= MAX_CONSECUTIVE_ERRORS) {
            
            LOG_ERROR("Critical receive failure on NIC %d", nic->index);
            hardware_graceful_degradation(nic);
            return ERROR_HARDWARE;
        }
        
        retry_count++;
        
        /* Brief delay before retry */
        mdelay(5 * retry_count);
        
        /* Check for overall timeout */
        if (get_system_timestamp_ms() - start_time > RX_TIMEOUT_MS) {
            LOG_ERROR("Hardware receive timeout exceeded for NIC %d", nic->index);
            break;
        }
    }
    
    /* All retries failed */
    hardware_update_packet_stats(false, false);
    hardware_log_failure(nic, HW_FAILURE_RX_TIMEOUT, "Packet receive failed after all retries");
    
    return result;
}

/**
 * @brief Get comprehensive hardware error recovery statistics
 * @param stats Pointer to store statistics
 * @return 0 on success, negative on error
 */
int hardware_get_recovery_stats(hardware_recovery_stats_t *stats) {
    int i;
    int j;
    if (!stats) {
        return ERROR_INVALID_PARAM;
    }

    memset(stats, 0, sizeof(hardware_recovery_stats_t));

    stats->total_failures = g_error_recovery_state.total_failures;
    stats->successful_recoveries = g_error_recovery_state.successful_recoveries;
    stats->failover_active = g_error_recovery_state.failover_in_progress;
    stats->primary_nic = g_error_recovery_state.primary_nic;
    stats->backup_nic = g_error_recovery_state.backup_nic;

    /* Copy per-NIC statistics */
    for (i = 0; i < g_num_nics && i < MAX_NICS; i++) {
        stats->nic_stats[i].consecutive_errors = g_error_recovery_state.consecutive_errors[i];
        stats->nic_stats[i].recovery_attempts = g_error_recovery_state.recovery_attempts[i];
        stats->nic_stats[i].last_error_time = g_error_recovery_state.last_error_time[i];
        stats->nic_stats[i].last_recovery_time = g_error_recovery_state.last_recovery_time[i];

        /* Copy error counts by type */
        for (j = 0; j < 12; j++) {
            stats->nic_stats[i].error_counts[j] = g_error_recovery_state.error_counts[i][j];
        }
    }

    return SUCCESS;
}

/**
 * @brief Monitor all NICs for health and trigger recovery as needed
 * @return 0 on success, positive for warnings, negative for critical errors
 */
int hardware_monitor_health(void) {
    int health_score = 0;
    int active_nics = 0;
    int i;
    nic_info_t *nic;
    int failure_type;

    if (!g_hardware_initialized) {
        return -100;  /* Critical: hardware not initialized */
    }

    for (i = 0; i < g_num_nics; i++) {
        nic = &g_nic_infos[i];

        if (!(nic->status & NIC_STATUS_PRESENT)) {
            continue;
        }

        /* Check for hardware failures */
        failure_type = hardware_detect_failure(nic);
        if (failure_type != HW_FAILURE_NONE) {
            if (hardware_is_critical_failure(failure_type)) {
                LOG_ERROR("Critical failure detected on NIC %d: type %d", i, failure_type);
                health_score -= 50;
                
                /* Attempt immediate recovery for critical failures */
                hardware_graceful_degradation(nic);
                hardware_attempt_failover(i);
            } else {
                LOG_WARNING("Non-critical failure detected on NIC %d: type %d", i, failure_type);
                health_score -= 10;
                
                /* Attempt recovery */
                if (hardware_recover_nic(nic, failure_type) == SUCCESS) {
                    health_score += 5;  /* Partial recovery */
                }
            }
        }
        
        if (nic->status & NIC_STATUS_ACTIVE) {
            active_nics++;
            
            /* Check error rates */
            if (nic->tx_packets > 0) {
                uint32_t tx_error_rate;
                tx_error_rate = (nic->tx_errors * 100) / nic->tx_packets;
                if (tx_error_rate > 10) {
                    health_score -= 15;
                } else if (tx_error_rate > 5) {
                    health_score -= 5;
                }
            }

            if (nic->rx_packets > 0) {
                uint32_t rx_error_rate;
                rx_error_rate = (nic->rx_errors * 100) / nic->rx_packets;
                if (rx_error_rate > 10) {
                    health_score -= 15;
                } else if (rx_error_rate > 5) {
                    health_score -= 5;
                }
            }
            
            /* Check link status */
            if (!nic->link_up) {
                health_score -= 20;
            }
        }
    }
    
    if (active_nics == 0) {
        LOG_ERROR("No active NICs available - critical system failure");
        return -200;
    }
    
    /* Log health status */
    if (health_score >= 0) {
        LOG_DEBUG("Hardware health: EXCELLENT (score: %d)", health_score);
    } else if (health_score >= -20) {
        LOG_INFO("Hardware health: GOOD (score: %d)", health_score);
    } else if (health_score >= -50) {
        LOG_WARNING("Hardware health: FAIR (score: %d)", health_score);
    } else {
        LOG_ERROR("Hardware health: POOR (score: %d)", health_score);
    }
    
    return health_score;
}

/**
 * @brief Print comprehensive hardware recovery statistics
 */
void hardware_print_recovery_stats(void) {
    int i;
    LOG_INFO("=== Hardware Recovery Statistics ===");
    LOG_INFO("Total Failures: %lu", g_error_recovery_state.total_failures);
    LOG_INFO("Successful Recoveries: %lu", g_error_recovery_state.successful_recoveries);
    LOG_INFO("Failover Active: %s", g_error_recovery_state.failover_in_progress ? "YES" : "NO");

    if (g_error_recovery_state.primary_nic >= 0) {
        LOG_INFO("Primary NIC: %d", g_error_recovery_state.primary_nic);
    }
    if (g_error_recovery_state.backup_nic >= 0) {
        LOG_INFO("Backup NIC: %d", g_error_recovery_state.backup_nic);
    }

    for (i = 0; i < g_num_nics; i++) {
        if (g_error_recovery_state.consecutive_errors[i] > 0 || 
            g_error_recovery_state.recovery_attempts[i] > 0) {
            
            LOG_INFO("NIC %d: Consecutive Errors=%lu, Recovery Attempts=%lu", 
                    i, g_error_recovery_state.consecutive_errors[i], 
                    g_error_recovery_state.recovery_attempts[i]);
        }
    }
    
    LOG_INFO("=== End Recovery Statistics ===");
}

/**
 * @brief Reset hardware recovery statistics
 */
void hardware_reset_recovery_stats(void) {
    LOG_INFO("Resetting hardware recovery statistics");
    memset(&g_error_recovery_state, 0, sizeof(g_error_recovery_state));
    g_error_recovery_state.primary_nic = -1;
    g_error_recovery_state.backup_nic = -1;
}

/**
 * @brief Check if system can handle a NIC failure (has backup)
 * @return true if system is resilient to single NIC failure
 */
bool hardware_is_failure_resilient(void) {
    int active_nics = 0;
    int i;

    for (i = 0; i < g_num_nics; i++) {
        if ((g_nic_infos[i].status & NIC_STATUS_ACTIVE) &&
            (g_nic_infos[i].status & NIC_STATUS_LINK_UP)) {
            active_nics++;
        }
    }

    return active_nics >= 2;
}

/**
 * @brief Comprehensive multi-NIC testing suite
 * This implements concurrent operations, load balancing, failover, and performance testing
 */

/**
 * @brief Test concurrent multi-NIC operations
 * @param test_duration_ms Duration of test in milliseconds
 * @return 0 on success, negative on error
 */
int hardware_test_concurrent_operations(uint32_t test_duration_ms) {
    uint8_t test_packet[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /* Broadcast dest */
        0x00, 0x20, 0xAF, 0x12, 0x34, 0x56,  /* Source MAC */
        0x08, 0x00,                          /* EtherType IP */
        'C', 'O', 'N', 'C', 'U', 'R', 'R', 'E', 'N', 'T',  /* Payload */
        'T', 'E', 'S', 'T', 'P', 'K', 'T'
    };

    uint32_t start_time = get_system_timestamp_ms();
    uint32_t tx_counts[MAX_NICS] = {0};
    uint32_t rx_counts[MAX_NICS] = {0};
    uint32_t errors[MAX_NICS] = {0};
    int active_nics = 0;
    int i;
    int nic_idx;
    nic_info_t *nic;
    int tx_result;
    uint8_t rx_buffer[256];
    size_t rx_length;
    int rx_result;
    volatile int vi;
    int failure;

    LOG_INFO("Starting concurrent multi-NIC operations test (duration: %lu ms)", test_duration_ms);

    /* Count active NICs */
    for (i = 0; i < g_num_nics; i++) {
        if (hardware_is_nic_active(i)) {
            active_nics++;
        }
    }

    if (active_nics < 2) {
        LOG_ERROR("Concurrent test requires at least 2 active NICs (found %d)", active_nics);
        return ERROR_INVALID_PARAM;
    }

    /* Main test loop - simulate concurrent operations */
    while ((get_system_timestamp_ms() - start_time) < test_duration_ms) {
        for (nic_idx = 0; nic_idx < g_num_nics; nic_idx++) {
            if (!hardware_is_nic_active(nic_idx)) {
                continue;
            }

            nic = hardware_get_nic(nic_idx);
            if (!nic) {
                continue;
            }

            /* Test concurrent TX operations */
            if ((tx_counts[nic_idx] % 10) == (uint32_t)(nic_idx % 10)) {  /* Staggered timing */
                tx_result = hardware_send_packet(nic, test_packet, sizeof(test_packet));
                if (tx_result == SUCCESS) {
                    tx_counts[nic_idx]++;
                } else {
                    errors[nic_idx]++;
                }
            }

            /* Test concurrent RX operations */
            rx_length = sizeof(rx_buffer);
            rx_result = hardware_receive_packet(nic, rx_buffer, &rx_length);
            if (rx_result == SUCCESS) {
                rx_counts[nic_idx]++;
            } else if (rx_result != ERROR_NO_DATA) {
                errors[nic_idx]++;
            }

            /* Brief yield to simulate real concurrent access */
            for (vi = 0; vi < 100; vi++);
        }

        /* Check for any hardware failures during test */
        for (i = 0; i < g_num_nics; i++) {
            if (hardware_is_nic_present(i)) {
                failure = hardware_detect_failure(&g_nic_infos[i]);
                if (failure != HW_FAILURE_NONE) {
                    LOG_WARNING("Hardware failure detected on NIC %d during concurrent test: type %d",
                               i, failure);
                    errors[i]++;
                }
            }
        }
    }
    
    /* Report results */
    LOG_INFO("=== Concurrent Operations Test Results ===");
    {
        uint32_t total_ops;
        uint32_t error_rate;
        for (i = 0; i < g_num_nics; i++) {
            if (hardware_is_nic_present(i)) {
                LOG_INFO("NIC %d: TX=%lu, RX=%lu, Errors=%lu",
                        i, tx_counts[i], rx_counts[i], errors[i]);

                /* Calculate error rates */
                total_ops = tx_counts[i] + rx_counts[i];
                if (total_ops > 0) {
                    error_rate = (errors[i] * 100) / total_ops;
                    if (error_rate > 5) {  /* > 5% error rate is concerning */
                        LOG_WARNING("High error rate on NIC %d: %lu%%", i, error_rate);
                    }
                }
            }
        }
    }

    LOG_INFO("Concurrent operations test completed successfully");
    return SUCCESS;
}

/**
 * @brief Test load balancing across multiple NICs
 * @param num_packets Number of test packets to send
 * @return 0 on success, negative on error
 */
int hardware_test_load_balancing(uint32_t num_packets) {
    uint8_t test_packet[] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,  /* Dest MAC */
        0x00, 0x20, 0xAF, 0x12, 0x34, 0x56,  /* Source MAC */
        0x08, 0x00,                          /* EtherType IP */
        'L', 'O', 'A', 'D', 'B', 'A', 'L', 'A', 'N', 'C', 'E'  /* Payload */
    };

    uint32_t nic_packet_counts[MAX_NICS] = {0};
    uint32_t nic_error_counts[MAX_NICS] = {0};
    int active_nics = 0;
    int next_nic = 0;
    int i;
    uint32_t pkt;
    int attempts;
    nic_info_t *nic;
    int result;
    volatile int vi;
    uint32_t total_sent = 0;
    uint32_t total_errors = 0;
    uint32_t min_packets = UINT32_MAX;
    uint32_t max_packets = 0;

    LOG_INFO("Starting load balancing test with %lu packets", num_packets);

    /* Count active NICs */
    for (i = 0; i < g_num_nics; i++) {
        if (hardware_is_nic_active(i)) {
            active_nics++;
        }
    }

    if (active_nics < 2) {
        LOG_ERROR("Load balancing test requires at least 2 active NICs (found %d)", active_nics);
        return ERROR_INVALID_PARAM;
    }

    /* Distribute packets across NICs using round-robin */
    for (pkt = 0; pkt < num_packets; pkt++) {
        /* Find next active NIC */
        attempts = 0;
        while (attempts < g_num_nics) {
            if (hardware_is_nic_active(next_nic)) {
                break;
            }
            next_nic = (next_nic + 1) % g_num_nics;
            attempts++;
        }

        if (attempts >= g_num_nics) {
            LOG_ERROR("No active NICs found during load balancing test");
            return ERROR_HARDWARE;
        }

        /* Modify packet to make it unique */
        test_packet[sizeof(test_packet) - 1] = (uint8_t)(pkt & 0xFF);

        /* Send packet */
        nic = hardware_get_nic(next_nic);
        result = hardware_send_packet(nic, test_packet, sizeof(test_packet));

        if (result == SUCCESS) {
            nic_packet_counts[next_nic]++;
        } else {
            nic_error_counts[next_nic]++;
            LOG_DEBUG("Packet %lu failed on NIC %d: %d", pkt, next_nic, result);
        }

        /* Move to next NIC */
        next_nic = (next_nic + 1) % g_num_nics;

        /* Brief delay to avoid overwhelming the NICs */
        if ((pkt % 100) == 0) {
            for (vi = 0; vi < 1000; vi++);
        }
    }

    /* Analyze load distribution */
    LOG_INFO("=== Load Balancing Test Results ===");

    for (i = 0; i < g_num_nics; i++) {
        if (hardware_is_nic_present(i)) {
            LOG_INFO("NIC %d: Sent=%lu, Errors=%lu", i, nic_packet_counts[i], nic_error_counts[i]);
            
            total_sent += nic_packet_counts[i];
            total_errors += nic_error_counts[i];
            
            if (hardware_is_nic_active(i)) {
                if (nic_packet_counts[i] < min_packets) min_packets = nic_packet_counts[i];
                if (nic_packet_counts[i] > max_packets) max_packets = nic_packet_counts[i];
            }
        }
    }
    
    /* Calculate load balance quality */
    if (min_packets != UINT32_MAX && max_packets > 0) {
        uint32_t balance_ratio;
        balance_ratio = (min_packets * 100) / max_packets;
        LOG_INFO("Load balance quality: %lu%% (min=%lu, max=%lu)",
                balance_ratio, min_packets, max_packets);

        if (balance_ratio < 80) {  /* Less than 80% balance */
            LOG_WARNING("Poor load balancing detected");
        }
    }

    {
    uint32_t error_rate;
    error_rate = (total_errors * 100) / num_packets;
    LOG_INFO("Overall: Sent=%lu/%lu, Error rate=%lu%%", total_sent, num_packets, error_rate);
    
    if (error_rate > 5) {
        LOG_ERROR("High error rate during load balancing test: %lu%%", error_rate);
        return ERROR_HARDWARE;
    }
    }

    LOG_INFO("Load balancing test completed successfully");
    return SUCCESS;
}

/**
 * @brief Test NIC failover mechanisms
 * @param primary_nic Index of primary NIC to fail
 * @return 0 on success, negative on error
 */
int hardware_test_failover(int primary_nic) {
    uint8_t test_packet[] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,  /* Dest MAC */
        0x00, 0x20, 0xAF, 0x12, 0x34, 0x56,  /* Source MAC */
        0x08, 0x00,                          /* EtherType IP */
        'F', 'A', 'I', 'L', 'O', 'V', 'E', 'R', 'T', 'S', 'T'  /* Payload */
    };

    nic_info_t *primary;
    nic_info_t *backup;
    int backup_nic = -1;
    uint32_t original_status;
    uint32_t packets_before_failover = 0;
    uint32_t packets_after_failover = 0;
    uint32_t failover_time_ms;
    uint32_t start_time;
    int i;
    int result;
    int failover_result;
    int recovery_result;

    LOG_INFO("Starting failover test with primary NIC %d", primary_nic);

    /* Validate primary NIC */
    primary = hardware_get_nic(primary_nic);
    if (!primary || !hardware_is_nic_active(primary_nic)) {
        LOG_ERROR("Primary NIC %d is not active for failover test", primary_nic);
        return ERROR_INVALID_PARAM;
    }

    /* Find backup NIC */
    for (i = 0; i < g_num_nics; i++) {
        if (i != primary_nic && hardware_is_nic_active(i)) {
            backup_nic = i;
            break;
        }
    }

    if (backup_nic == -1) {
        LOG_ERROR("No backup NIC available for failover test");
        return ERROR_INVALID_PARAM;
    }

    LOG_INFO("Using NIC %d as backup for failover test", backup_nic);

    /* Test normal operation before failover */
    LOG_INFO("Testing normal operation before failover...");
    for (i = 0; i < 50; i++) {
        result = hardware_send_packet(primary, test_packet, sizeof(test_packet));
        if (result == SUCCESS) {
            packets_before_failover++;
        }
    }

    LOG_INFO("Sent %lu packets before failover", packets_before_failover);

    /* Simulate primary NIC failure */
    LOG_INFO("Simulating primary NIC failure...");
    original_status = primary->status;
    start_time = get_system_timestamp_ms();

    /* Mark primary NIC as failed */
    primary->status &= ~NIC_STATUS_ACTIVE;
    primary->status |= NIC_STATUS_ERROR;

    /* Trigger failover */
    failover_result = hardware_attempt_failover(primary_nic);
    failover_time_ms = get_system_timestamp_ms() - start_time;

    if (failover_result != SUCCESS) {
        LOG_ERROR("Failover attempt failed: %d", failover_result);
        /* Restore original status */
        primary->status = original_status;
        return failover_result;
    }

    LOG_INFO("Failover completed in %lu ms", failover_time_ms);

    /* Test operation after failover using backup NIC */
    LOG_INFO("Testing operation after failover...");
    backup = hardware_get_nic(backup_nic);

    for (i = 0; i < 50; i++) {
        result = hardware_send_packet(backup, test_packet, sizeof(test_packet));
        if (result == SUCCESS) {
            packets_after_failover++;
        }
    }

    LOG_INFO("Sent %lu packets after failover", packets_after_failover);

    /* Test primary NIC recovery */
    LOG_INFO("Testing primary NIC recovery...");
    primary->status = original_status;  /* Restore original status */

    /* Validate recovery */
    recovery_result = hardware_validate_recovery(primary);
    if (recovery_result == SUCCESS) {
        LOG_INFO("Primary NIC recovery successful");
    } else {
        LOG_WARNING("Primary NIC recovery failed: %d", recovery_result);
    }

    /* Test failback to primary */
    if (recovery_result == SUCCESS) {
        LOG_INFO("Testing failback to primary NIC...");
        for (i = 0; i < 10; i++) {
            result = hardware_send_packet(primary, test_packet, sizeof(test_packet));
            if (result != SUCCESS) {
                LOG_WARNING("Failback test packet %d failed: %d", i, result);
            }
        }
    }
    
    /* Report failover test results */
    LOG_INFO("=== Failover Test Results ===");
    LOG_INFO("Packets before failover: %lu/50", packets_before_failover);
    LOG_INFO("Packets after failover: %lu/50", packets_after_failover);
    LOG_INFO("Failover time: %lu ms", failover_time_ms);
    LOG_INFO("Primary recovery: %s", (recovery_result == SUCCESS) ? "SUCCESS" : "FAILED");
    
    /* Evaluate failover quality */
    if (packets_before_failover < 40) {
        LOG_WARNING("Poor primary NIC performance before failover");
    }
    
    if (packets_after_failover < 40) {
        LOG_WARNING("Poor backup NIC performance after failover");
    }
    
    if (failover_time_ms > 1000) {  /* > 1 second */
        LOG_WARNING("Slow failover time: %lu ms", failover_time_ms);
    }
    
    LOG_INFO("Failover test completed successfully");
    return SUCCESS;
}

/**
 * @brief Test resource contention between NICs
 * @param num_iterations Number of test iterations
 * @return 0 on success, negative on error
 */
int hardware_test_resource_contention(uint32_t num_iterations) {
    uint8_t test_packets[MAX_NICS][64];
    uint32_t success_counts[MAX_NICS] = {0};
    uint32_t contention_errors[MAX_NICS] = {0};
    uint32_t timeout_errors[MAX_NICS] = {0};
    int i;
    uint32_t iter;
    uint32_t start_time;
    int nic_idx;
    nic_info_t *nic;
    int result;
    uint8_t rx_buffer[256];
    size_t rx_length;
    uint32_t iteration_time;
    volatile int vi;

    LOG_INFO("Starting resource contention test (%lu iterations)", num_iterations);

    /* Prepare unique test packets for each NIC */
    for (i = 0; i < g_num_nics; i++) {
        if (hardware_is_nic_present(i)) {
            /* Build unique packet for this NIC */
            memset(test_packets[i], 0, sizeof(test_packets[i]));
            /* Destination MAC */
            test_packets[i][0] = 0x00; test_packets[i][1] = 0x11; test_packets[i][2] = 0x22;
            test_packets[i][3] = 0x33; test_packets[i][4] = 0x44; test_packets[i][5] = 0x55;
            /* Source MAC (unique per NIC) */
            test_packets[i][6] = 0x00; test_packets[i][7] = 0x20; test_packets[i][8] = 0xAF;
            test_packets[i][9] = 0x12; test_packets[i][10] = 0x34; test_packets[i][11] = 0x50 + i;
            /* EtherType */
            test_packets[i][12] = 0x08; test_packets[i][13] = 0x00;
            /* Payload with NIC identifier */
            sprintf((char*)&test_packets[i][14], "CONTENTION_NIC_%d", i);
        }
    }

    /* Run contention test */
    for (iter = 0; iter < num_iterations; iter++) {
        start_time = get_system_timestamp_ms();

        /* Attempt simultaneous operations on all NICs */
        for (nic_idx = 0; nic_idx < g_num_nics; nic_idx++) {
            if (!hardware_is_nic_active(nic_idx)) {
                continue;
            }

            nic = hardware_get_nic(nic_idx);
            if (!nic) {
                continue;
            }

            /* Try to send packet */
            result = hardware_send_packet(nic, test_packets[nic_idx], sizeof(test_packets[nic_idx]));

            if (result == SUCCESS) {
                success_counts[nic_idx]++;
            } else if (result == ERROR_TIMEOUT) {
                timeout_errors[nic_idx]++;
            } else if (result == ERROR_BUSY) {
                contention_errors[nic_idx]++;
            }

            /* Also test receive operations for contention */
            rx_length = sizeof(rx_buffer);
            hardware_receive_packet(nic, rx_buffer, &rx_length);
        }

        iteration_time = get_system_timestamp_ms() - start_time;
        if (iteration_time > 100) {  /* > 100ms for one iteration is slow */
            LOG_DEBUG("Slow iteration %lu: %lu ms", iter, iteration_time);
        }

        /* Brief delay between iterations */
        if ((iter % 100) == 0) {
            for (vi = 0; vi < 5000; vi++);
        }
    }
    
    /* Analyze contention results */
    LOG_INFO("=== Resource Contention Test Results ===");
    {
        uint32_t total_attempts = 0;
        uint32_t total_successes = 0;
        uint32_t total_contentions = 0;
        uint32_t total_timeouts = 0;
        uint32_t attempts;
        uint32_t success_rate;
        uint32_t contention_rate;

        for (i = 0; i < g_num_nics; i++) {
            if (hardware_is_nic_present(i)) {
                attempts = success_counts[i] + contention_errors[i] + timeout_errors[i];

                LOG_INFO("NIC %d: Success=%lu, Contention=%lu, Timeout=%lu (of %lu attempts)",
                        i, success_counts[i], contention_errors[i], timeout_errors[i], attempts);

                if (attempts > 0) {
                    success_rate = (success_counts[i] * 100) / attempts;
                    contention_rate = (contention_errors[i] * 100) / attempts;

                    LOG_INFO("  Success rate: %lu%%, Contention rate: %lu%%",
                            success_rate, contention_rate);

                    if (contention_rate > 10) {  /* > 10% contention */
                        LOG_WARNING("High contention rate on NIC %d: %lu%%", i, contention_rate);
                    }
                }

                total_attempts += attempts;
                total_successes += success_counts[i];
                total_contentions += contention_errors[i];
                total_timeouts += timeout_errors[i];
            }
        }
    
    if (total_attempts > 0) {
        uint32_t overall_success_rate;
        uint32_t overall_contention_rate;
        overall_success_rate = (total_successes * 100) / total_attempts;
        overall_contention_rate = (total_contentions * 100) / total_attempts;

        LOG_INFO("Overall: Success rate=%lu%%, Contention rate=%lu%%",
                overall_success_rate, overall_contention_rate);

        if (overall_contention_rate > 15) {
            LOG_ERROR("Excessive resource contention detected: %lu%%", overall_contention_rate);
            return ERROR_HARDWARE;
        }
    }
    }

    LOG_INFO("Resource contention test completed successfully");
    return SUCCESS;
}

/**
 * @brief Performance validation under multi-NIC load
 * @param test_duration_ms Duration of performance test
 * @return 0 on success, negative on error
 */
int hardware_test_multi_nic_performance(uint32_t test_duration_ms) {
    uint8_t test_packet[1518];  /* Maximum Ethernet frame size */
    uint32_t start_time;
    uint32_t tx_counts[MAX_NICS] = {0};
    uint32_t rx_counts[MAX_NICS] = {0};
    uint32_t error_counts[MAX_NICS] = {0};
    uint32_t total_bytes_tx = 0;
    uint32_t total_bytes_rx = 0;
    int nic_idx;
    nic_info_t *nic;
    int burst;
    int tx_result;
    uint8_t rx_buffer[1518];
    size_t rx_length;
    int rx_result;
    uint32_t actual_duration;
    uint32_t total_tx_packets = 0;
    uint32_t total_rx_packets = 0;
    uint32_t total_errors = 0;
    int i;
    uint32_t nic_tx_rate;
    uint32_t nic_rx_rate;

    LOG_INFO("Starting multi-NIC performance test (duration: %lu ms)", test_duration_ms);

    /* Prepare maximum-size test packet */
    memset(test_packet, 0xAA, sizeof(test_packet));
    /* Set Ethernet header */
    memset(test_packet, 0xFF, 6);  /* Broadcast destination */
    test_packet[6] = 0x00; test_packet[7] = 0x20; test_packet[8] = 0xAF;
    test_packet[9] = 0x12; test_packet[10] = 0x34; test_packet[11] = 0x56;  /* Source */
    test_packet[12] = 0x08; test_packet[13] = 0x00;  /* EtherType IP */

    start_time = get_system_timestamp_ms();

    /* Performance test loop */
    while ((get_system_timestamp_ms() - start_time) < test_duration_ms) {
        for (nic_idx = 0; nic_idx < g_num_nics; nic_idx++) {
            if (!hardware_is_nic_active(nic_idx)) {
                continue;
            }

            nic = hardware_get_nic(nic_idx);
            if (!nic) {
                continue;
            }

            /* High-rate transmission test */
            for (burst = 0; burst < 5; burst++) {
                tx_result = hardware_send_packet(nic, test_packet, sizeof(test_packet));
                if (tx_result == SUCCESS) {
                    tx_counts[nic_idx]++;
                    total_bytes_tx += sizeof(test_packet);
                } else {
                    error_counts[nic_idx]++;
                }
            }

            /* Reception test */
            rx_length = sizeof(rx_buffer);
            rx_result = hardware_receive_packet(nic, rx_buffer, &rx_length);
            if (rx_result == SUCCESS) {
                rx_counts[nic_idx]++;
                total_bytes_rx += rx_length;
            } else if (rx_result != ERROR_NO_DATA) {
                error_counts[nic_idx]++;
            }
        }
    }

    actual_duration = get_system_timestamp_ms() - start_time;

    /* Calculate and report performance metrics */
    LOG_INFO("=== Multi-NIC Performance Test Results ===");
    LOG_INFO("Test duration: %lu ms", actual_duration);

    for (i = 0; i < g_num_nics; i++) {
        if (hardware_is_nic_present(i)) {
            nic_tx_rate = (tx_counts[i] * 1000) / actual_duration;  /* packets/sec */
            nic_rx_rate = (rx_counts[i] * 1000) / actual_duration;  /* packets/sec */

            LOG_INFO("NIC %d: TX=%lu pps, RX=%lu pps, Errors=%lu",
                    i, nic_tx_rate, nic_rx_rate, error_counts[i]);

            total_tx_packets += tx_counts[i];
            total_rx_packets += rx_counts[i];
            total_errors += error_counts[i];
        }
    }
    
    /* Overall performance metrics */
    {
    uint32_t total_tx_rate;
    uint32_t total_rx_rate;
    uint32_t tx_throughput_kbps;
    uint32_t rx_throughput_kbps;
    uint32_t expected_min_tx_rate;
    uint32_t total_operations;

    total_tx_rate = (total_tx_packets * 1000) / actual_duration;
    total_rx_rate = (total_rx_packets * 1000) / actual_duration;
    tx_throughput_kbps = (total_bytes_tx * 8) / actual_duration;  /* Kbps */
    rx_throughput_kbps = (total_bytes_rx * 8) / actual_duration;  /* Kbps */

    LOG_INFO("=== Overall Performance ===");
    LOG_INFO("TX Rate: %lu packets/sec (%lu Kbps)", total_tx_rate, tx_throughput_kbps);
    LOG_INFO("RX Rate: %lu packets/sec (%lu Kbps)", total_rx_rate, rx_throughput_kbps);
    LOG_INFO("Total errors: %lu", total_errors);

    /* Performance validation */
    expected_min_tx_rate = 1000;  /* Minimum 1000 pps expected */
    if (total_tx_rate < expected_min_tx_rate) {
        LOG_WARNING("Low TX performance: %lu pps (expected > %lu pps)",
                   total_tx_rate, expected_min_tx_rate);
    }

    total_operations = total_tx_packets + total_rx_packets;
    if (total_operations > 0) {
        uint32_t error_rate;
        error_rate = (total_errors * 100) / total_operations;
        if (error_rate > 3) {  /* > 3% error rate */
            LOG_ERROR("High error rate during performance test: %lu%%", error_rate);
            return ERROR_HARDWARE;
        }
    }
    }

    LOG_INFO("Multi-NIC performance test completed successfully");
    return SUCCESS;
}

/**
 * @brief Run comprehensive multi-NIC test suite
 * @return 0 on success, negative on error
 */
int hardware_run_multi_nic_tests(void) {
    int tests_passed = 0;
    int tests_failed = 0;
    int result;
    
    LOG_INFO("=== Starting Comprehensive Multi-NIC Test Suite ===");
    
    /* Check if we have multiple NICs */
    if (g_num_nics < 2) {
        LOG_WARNING("Multi-NIC tests require at least 2 NICs (found %d)", g_num_nics);
        return ERROR_INVALID_PARAM;
    }
    
    /* Test 1: Concurrent Operations */
    LOG_INFO("Running concurrent operations test...");
    result = hardware_test_concurrent_operations(5000);  /* 5 second test */
    if (result == SUCCESS) {
        tests_passed++;
        LOG_INFO("Concurrent operations test PASSED");
    } else {
        tests_failed++;
        LOG_ERROR("Concurrent operations test FAILED: %d", result);
    }
    
    /* Test 2: Load Balancing */
    LOG_INFO("Running load balancing test...");
    result = hardware_test_load_balancing(1000);  /* 1000 packets */
    if (result == SUCCESS) {
        tests_passed++;
        LOG_INFO("Load balancing test PASSED");
    } else {
        tests_failed++;
        LOG_ERROR("Load balancing test FAILED: %d", result);
    }
    
    /* Test 3: Failover (if we have resilient configuration) */
    if (hardware_is_failure_resilient()) {
        LOG_INFO("Running failover test...");
        result = hardware_test_failover(0);  /* Test failover of NIC 0 */
        if (result == SUCCESS) {
            tests_passed++;
            LOG_INFO("Failover test PASSED");
        } else {
            tests_failed++;
            LOG_ERROR("Failover test FAILED: %d", result);
        }
    } else {
        LOG_INFO("Skipping failover test - system not failure resilient");
    }
    
    /* Test 4: Resource Contention */
    LOG_INFO("Running resource contention test...");
    result = hardware_test_resource_contention(500);  /* 500 iterations */
    if (result == SUCCESS) {
        tests_passed++;
        LOG_INFO("Resource contention test PASSED");
    } else {
        tests_failed++;
        LOG_ERROR("Resource contention test FAILED: %d", result);
    }
    
    /* Test 5: Performance Under Load */
    LOG_INFO("Running multi-NIC performance test...");
    result = hardware_test_multi_nic_performance(10000);  /* 10 second test */
    if (result == SUCCESS) {
        tests_passed++;
        LOG_INFO("Multi-NIC performance test PASSED");
    } else {
        tests_failed++;
        LOG_ERROR("Multi-NIC performance test FAILED: %d", result);
    }
    
    /* Report overall results */
    LOG_INFO("=== Multi-NIC Test Suite Summary ===");
    LOG_INFO("Tests passed: %d", tests_passed);
    LOG_INFO("Tests failed: %d", tests_failed);
    
    if (tests_failed == 0) {
        LOG_INFO("=== ALL MULTI-NIC TESTS PASSED ===");
        return SUCCESS;
    } else {
        LOG_ERROR("=== SOME MULTI-NIC TESTS FAILED ===");
        return ERROR_HARDWARE;
    }
}

/* ========================================================================== */
/* ERROR HANDLING INTEGRATION FUNCTIONS                                      */
/* ========================================================================== */

/**
 * @brief Initialize error handling system for hardware layer
 * @return 0 on success, negative on error
 */
int hardware_init_error_handling(void) {
    int result;

    LOG_INFO("Initializing hardware error handling integration");

    /* Initialize the global error handling system */
    result = error_handling_init();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize error handling system: %d", result);
        return result;
    }

    LOG_INFO("Hardware error handling integration initialized successfully");
    return SUCCESS;
}

/**
 * @brief Cleanup error handling system for hardware layer
 */
void hardware_cleanup_error_handling(void) {
    int i;
    LOG_INFO("Cleaning up hardware error handling integration");

    /* Cleanup error contexts for all NICs */
    for (i = 0; i < g_num_nics; i++) {
        if (g_nic_infos[i].error_context) {
            hardware_destroy_error_context(&g_nic_infos[i]);
        }
    }

    /* Cleanup global error handling system */
    error_handling_cleanup();

    LOG_INFO("Hardware error handling integration cleanup completed");
}

/**
 * @brief Create error handling context for a NIC
 * @param nic NIC information structure
 * @return 0 on success, negative on error
 */
int hardware_create_error_context(nic_info_t *nic) {
    nic_error_context_t *ctx;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    LOG_INFO("Creating error context for NIC %d (type: %d)", nic->index, nic->type);

    /* Allocate error context */
    ctx = malloc(sizeof(nic_error_context_t));
    if (!ctx) {
        LOG_ERROR("Failed to allocate error context for NIC %d", nic->index);
        return ERROR_NO_MEMORY;
    }

    /* Initialize error context */
    memset(ctx, 0, sizeof(nic_error_context_t));

    /* Link to NIC information (pointer, not copy) */
    ctx->nic_info = nic;

    /* Initialize error statistics */
    error_handling_reset_stats(ctx);

    /* Set initial state */
    ctx->link_up = nic->link_up;
    ctx->recovery_state = 0;
    ctx->recovery_strategy = RECOVERY_STRATEGY_NONE;
    ctx->adapter_disabled = false;

    /* Link the context to the NIC */
    nic->error_context = ctx;

    LOG_INFO("Error context created successfully for NIC %d", nic->index);
    return SUCCESS;
}

/**
 * @brief Destroy error handling context for a NIC
 * @param nic NIC information structure
 */
void hardware_destroy_error_context(nic_info_t *nic) {
    if (!nic || !nic->error_context) {
        return;
    }
    
    LOG_INFO("Destroying error context for NIC %d", nic->index);
    
    /* Print final error statistics */
    hardware_print_error_statistics(nic);
    
    /* Free the error context */
    free(nic->error_context);
    nic->error_context = NULL;
    
    LOG_INFO("Error context destroyed for NIC %d", nic->index);
}

/**
 * @brief Handle RX error using error handling system
 * @param nic NIC information structure
 * @param rx_status RX status register value
 * @return 0 on success, negative on error
 */
int hardware_handle_rx_error(nic_info_t *nic, uint32_t rx_status) {
    int result;

    if (!nic || !nic->error_context) {
        LOG_ERROR("Invalid NIC or missing error context for RX error handling");
        return ERROR_INVALID_PARAM;
    }

    /* Update legacy error tracking */
    nic->error_count++;
    nic->rx_errors++;
    nic->last_error = rx_status;

    /* Use comprehensive error handling */
    result = handle_rx_error(nic->error_context, rx_status);
    
    /* Update NIC status based on error handling result */
    if (result == RECOVERY_FATAL || nic->error_context->adapter_disabled) {
        nic->status |= NIC_STATUS_ERROR;
        nic->status &= ~NIC_STATUS_ACTIVE;
        LOG_CRITICAL("NIC %d disabled due to fatal RX errors", nic->index);
    }
    
    return result;
}

/**
 * @brief Handle TX error using error handling system
 * @param nic NIC information structure
 * @param tx_status TX status register value
 * @return 0 on success, negative on error
 */
int hardware_handle_tx_error(nic_info_t *nic, uint32_t tx_status) {
    int result;

    if (!nic || !nic->error_context) {
        LOG_ERROR("Invalid NIC or missing error context for TX error handling");
        return ERROR_INVALID_PARAM;
    }

    /* Update legacy error tracking */
    nic->error_count++;
    nic->tx_errors++;
    nic->last_error = tx_status;

    /* Use comprehensive error handling */
    result = handle_tx_error(nic->error_context, tx_status);
    
    /* Update NIC status based on error handling result */
    if (result == RECOVERY_FATAL || nic->error_context->adapter_disabled) {
        nic->status |= NIC_STATUS_ERROR;
        nic->status &= ~NIC_STATUS_ACTIVE;
        LOG_CRITICAL("NIC %d disabled due to fatal TX errors", nic->index);
    }
    
    return result;
}

/**
 * @brief Handle adapter-level error using error handling system
 * @param nic NIC information structure
 * @param failure_type Adapter failure type
 * @return 0 on success, negative on error
 */
int hardware_handle_adapter_error(nic_info_t *nic, uint8_t failure_type) {
    int result;

    if (!nic || !nic->error_context) {
        LOG_ERROR("Invalid NIC or missing error context for adapter error handling");
        return ERROR_INVALID_PARAM;
    }

    /* Update legacy error tracking */
    nic->error_count++;
    nic->last_error = failure_type;

    /* Use comprehensive error handling */
    result = handle_adapter_error(nic->error_context, failure_type);
    
    /* Update NIC status based on error handling result */
    if (result == RECOVERY_FATAL || nic->error_context->adapter_disabled) {
        nic->status |= NIC_STATUS_ERROR;
        nic->status &= ~NIC_STATUS_ACTIVE;
        LOG_CRITICAL("NIC %d disabled due to fatal adapter error: %s", 
                    nic->index, adapter_failure_to_string(failure_type));
    }
    
    return result;
}

/**
 * @brief Attempt recovery for a NIC using error handling system
 * @param nic NIC information structure
 * @return 0 on success, negative on error
 */
int hardware_attempt_recovery(nic_info_t *nic) {
    int result;

    if (!nic || !nic->error_context) {
        LOG_ERROR("Invalid NIC or missing error context for recovery");
        return ERROR_INVALID_PARAM;
    }

    LOG_WARNING("Attempting recovery for NIC %d", nic->index);

    /* Use comprehensive recovery system */
    result = attempt_adapter_recovery(nic->error_context);
    
    /* Update NIC status based on recovery result */
    if (result == RECOVERY_SUCCESS) {
        nic->status &= ~NIC_STATUS_ERROR;
        nic->status |= NIC_STATUS_ACTIVE;
        LOG_INFO("Recovery successful for NIC %d", nic->index);
    } else if (result == RECOVERY_FATAL) {
        nic->status |= NIC_STATUS_ERROR;
        nic->status &= ~NIC_STATUS_ACTIVE;
        LOG_CRITICAL("Recovery failed fatally for NIC %d", nic->index);
    } else {
        LOG_WARNING("Recovery partially successful for NIC %d (result: %d)", 
                   nic->index, result);
    }
    
    return result;
}

/**
 * @brief Print error statistics for a NIC
 * @param nic NIC information structure
 */
void hardware_print_error_statistics(nic_info_t *nic) {
    if (!nic || !nic->error_context) {
        printf("No error statistics available for NIC\n");
        return;
    }
    
    printf("\n=== Hardware Error Statistics for NIC %d ===\n", nic->index);
    printf("Legacy Error Count: %lu\n", nic->error_count);
    printf("Legacy TX Errors: %lu\n", nic->tx_errors);
    printf("Legacy RX Errors: %lu\n", nic->rx_errors);
    printf("Last Error Code: 0x%08lX\n", nic->last_error);
    
    /* Print comprehensive error statistics */
    print_error_statistics(nic->error_context);
}

/**
 * @brief Print global error summary for all NICs
 */
void hardware_print_global_error_summary(void) {
    uint32_t total_errors = 0;
    uint32_t total_recoveries = 0;
    uint32_t disabled_nics = 0;
    int i;
    nic_info_t *nic;

    printf("\n=== Global Hardware Error Summary ===\n");
    printf("Total NICs: %d\n", g_num_nics);

    for (i = 0; i < g_num_nics; i++) {
        nic = &g_nic_infos[i];
        if (nic->error_context) {
            total_errors += nic->error_context->error_stats.rx_errors +
                           nic->error_context->error_stats.tx_errors;
            total_recoveries += nic->error_context->error_stats.recoveries_attempted;
            if (nic->error_context->adapter_disabled) {
                disabled_nics++;
            }
        }
    }

    printf("Total Errors: %lu\n", total_errors);
    printf("Total Recovery Attempts: %lu\n", total_recoveries);
    printf("Disabled NICs: %lu\n", disabled_nics);

    /* Print global error handling statistics */
    print_global_error_summary();

    printf("System Health: %d%%\n", hardware_get_system_health_status());
}

/**
 * @brief Get system health status
 * @return Health percentage (0-100)
 */
int hardware_get_system_health_status(void) {
    return get_system_health_status();
}

/**
 * @brief Export error log to buffer
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return Number of bytes written
 */
int hardware_export_error_log(char *buffer, size_t buffer_size) {
    error_log_entry_t entries[100];
    int num_entries;
    size_t written = 0;
    int i;
    int len;

    if (!buffer || buffer_size == 0) {
        return 0;
    }

    /* Read error log entries */
    num_entries = read_error_log_entries(entries, 100);

    for (i = 0; i < num_entries && written < buffer_size - 1; i++) {
        len = snprintf(buffer + written, buffer_size - written,
                          "[%lu] %s NIC%d: %s\n",
                          entries[i].timestamp,
                          error_severity_to_string(entries[i].severity),
                          entries[i].nic_id,
                          entries[i].message);
        if (len > 0) {
            written += len;
        }
    }

    return (int)written;
}

/**
 * @brief Configure error thresholds for a NIC
 * @param nic NIC information structure
 * @param max_error_rate Maximum error rate percentage
 * @param max_consecutive Maximum consecutive errors
 * @param recovery_timeout Recovery timeout in milliseconds
 * @return 0 on success, negative on error
 */
int hardware_configure_error_thresholds(nic_info_t *nic, uint32_t max_error_rate, 
                                       uint32_t max_consecutive, uint32_t recovery_timeout) {
    if (!nic || !nic->error_context) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_INFO("Configuring error thresholds for NIC %d: rate=%lu%%, consecutive=%lu, timeout=%lums",
             nic->index, max_error_rate, max_consecutive, recovery_timeout);
    
    /* Use the generic error threshold configuration function */
    return configure_error_thresholds(nic->error_context, max_error_rate, 
                                    max_consecutive, recovery_timeout);
}

/* === Per-NIC Buffer System Integration === */

/**
 * @brief Register a NIC with the per-NIC buffer pool system
 * @param nic NIC information structure
 * @param nic_index NIC index (used as NIC ID)
 * @return SUCCESS on success, error code on failure
 */
static int hardware_register_nic_with_buffer_system(nic_info_t* nic, int nic_index) {
    char nic_name[32];
    const char* type_name;
    int result;

    if (!nic || nic_index < 0 || nic_index >= MAX_NICS) {
        LOG_ERROR("Invalid parameters for NIC buffer registration");
        return ERROR_INVALID_PARAM;
    }

    /* Create a descriptive NIC name */
    type_name = "Unknown";

    switch (nic->type) {
        case NIC_TYPE_3C509B:
            type_name = "3C509B";
            break;
        case NIC_TYPE_3C515_TX:
            type_name = "3C515-TX";
            break;
        default:
            type_name = "Unknown";
            break;
    }

    snprintf(nic_name, sizeof(nic_name), "%s-%d", type_name, nic_index);

    LOG_INFO("Registering NIC %d (%s) with per-NIC buffer pools", nic_index, nic_name);

    /* Register with the buffer system */
    result = buffer_register_nic((nic_id_t)nic_index, nic->type, nic_name);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to register NIC %d with buffer system: %d", nic_index, result);
        return result;
    }
    
    /* Store NIC ID in the NIC info for future reference */
    nic->index = nic_index; /* Ensure index is set correctly */
    
    LOG_INFO("Successfully registered NIC %d with buffer system", nic_index);
    return SUCCESS;
}

/**
 * @brief Unregister a NIC from the per-NIC buffer pool system
 * @param nic_index NIC index (used as NIC ID)
 */
static void hardware_unregister_nic_from_buffer_system(int nic_index) {
    int result;

    if (nic_index < 0 || nic_index >= MAX_NICS) {
        LOG_ERROR("Invalid NIC index for buffer unregistration: %d", nic_index);
        return;
    }

    LOG_INFO("Unregistering NIC %d from buffer system", nic_index);

    /* Unregister from the buffer system */
    result = buffer_unregister_nic((nic_id_t)nic_index);
    if (result != SUCCESS) {
        LOG_WARNING("Failed to unregister NIC %d from buffer system: %d", nic_index, result);
    } else {
        LOG_INFO("Successfully unregistered NIC %d from buffer system", nic_index);
    }
}

/* === Enhanced Buffer-Aware Packet Operations === */

/**
 * @brief Send packet using per-NIC buffer pools for optimal performance
 * @param nic NIC information structure
 * @param packet Packet data
 * @param length Packet length
 * @return SUCCESS on success, error code on failure
 */
int hardware_send_packet_buffered(nic_info_t *nic, const uint8_t *packet, uint16_t length) {
    nic_id_t nic_id;
    buffer_desc_t* tx_buffer;
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

    /* Allocate buffer from per-NIC pool for this transmission */
    nic_id = (nic_id_t)nic->index;
    tx_buffer = buffer_alloc_ethernet_frame_nic(nic_id, length, BUFFER_TYPE_TX);

    if (!tx_buffer) {
        LOG_WARNING("Failed to allocate TX buffer for NIC %d, using direct transmission", nic->index);
        /* Fall back to direct transmission without buffering */
        result = nic->ops->send_packet(nic, packet, length);
        hardware_update_packet_stats(true, result == SUCCESS);
        return result;
    }

    /* Copy packet data to allocated buffer */
    if (buffer_set_data(tx_buffer, packet, length) != SUCCESS) {
        LOG_ERROR("Failed to copy packet data to TX buffer for NIC %d", nic->index);
        buffer_free_nic_aware(nic_id, tx_buffer);
        hardware_update_packet_stats(true, false);
        return ERROR_GENERIC;
    }

    /* Send packet using the buffered data */
    result = nic->ops->send_packet(nic, (const uint8_t*)buffer_get_data_ptr(tx_buffer), length);
    
    /* Free the buffer back to the per-NIC pool */
    buffer_free_nic_aware(nic_id, tx_buffer);
    
    hardware_update_packet_stats(true, result == SUCCESS);
    
    if (result == SUCCESS) {
        LOG_DEBUG("Successfully sent %u-byte packet using per-NIC buffer for NIC %d", length, nic->index);
    } else {
        LOG_WARNING("Failed to send packet using per-NIC buffer for NIC %d: %d", nic->index, result);
    }
    
    return result;
}

/**
 * @brief Receive packet using per-NIC buffer pools with RX_COPYBREAK optimization
 * @param nic NIC information structure
 * @param buffer Output buffer
 * @param length Pointer to buffer length (input) and received length (output)
 * @return SUCCESS on success, error code on failure
 */
int hardware_receive_packet_buffered(nic_info_t *nic, uint8_t *buffer, uint16_t *length) {
    nic_id_t nic_id;
    uint16_t buffer_size;
    buffer_desc_t* rx_buffer;
    size_t rx_buffer_len;
    int result;
    uint16_t copy_size;

    if (!nic || !buffer || !length) {
        hardware_update_packet_stats(false, false);
        return ERROR_INVALID_PARAM;
    }

    if (!nic->ops || !nic->ops->receive_packet) {
        hardware_update_packet_stats(false, false);
        return ERROR_NOT_SUPPORTED;
    }

    if (!(nic->status & NIC_STATUS_ACTIVE)) {
        hardware_update_packet_stats(false, false);
        return ERROR_BUSY;
    }

    nic_id = (nic_id_t)nic->index;
    buffer_size = *length;

    /* Try to allocate an optimized receive buffer using RX_COPYBREAK */
    rx_buffer = buffer_rx_copybreak_alloc_nic(nic_id, buffer_size);

    if (!rx_buffer) {
        LOG_DEBUG("RX_COPYBREAK allocation failed for NIC %d, trying regular allocation", nic->index);
        /* Fall back to regular per-NIC allocation */
        rx_buffer = buffer_alloc_ethernet_frame_nic(nic_id, buffer_size, BUFFER_TYPE_RX);
    }

    if (!rx_buffer) {
        LOG_WARNING("Failed to allocate RX buffer for NIC %d, using direct reception", nic->index);
        /* Fall back to direct reception without buffering */
        rx_buffer_len = (size_t)*length;
        result = nic->ops->receive_packet(nic, buffer, &rx_buffer_len);
        *length = (uint16_t)rx_buffer_len;
        hardware_update_packet_stats(false, result == SUCCESS);
        return result;
    }

    /* Receive packet into the allocated buffer */
    rx_buffer_len = (size_t)buffer_get_size(rx_buffer);
    result = nic->ops->receive_packet(nic, (uint8_t*)buffer_get_data_ptr(rx_buffer), &rx_buffer_len);

    if (result == SUCCESS && rx_buffer_len > 0) {
        /* Copy received data to output buffer */
        copy_size = (rx_buffer_len < buffer_size) ? (uint16_t)rx_buffer_len : buffer_size;
        memory_copy_optimized(buffer, buffer_get_data_ptr(rx_buffer), copy_size);
        *length = copy_size;
        
        /* Record copy operation for RX_COPYBREAK statistics */
        if (rx_buffer->size <= RX_COPYBREAK_THRESHOLD) {
            rx_copybreak_record_copy();
        }
        
        LOG_DEBUG("Successfully received %u-byte packet using per-NIC buffer for NIC %d", copy_size, nic->index);
    } else {
        *length = 0;
        if (result != SUCCESS) {
            LOG_DEBUG("Failed to receive packet for NIC %d: %d", nic->index, result);
        }
    }
    
    /* Free the buffer back to the appropriate pool */
    if (rx_buffer->size <= LARGE_BUFFER_SIZE) {
        /* This was likely an RX_COPYBREAK buffer */
        buffer_rx_copybreak_free_nic(nic_id, rx_buffer);
    } else {
        /* Regular per-NIC buffer */
        buffer_free_nic_aware(nic_id, rx_buffer);
    }
    
    hardware_update_packet_stats(false, result == SUCCESS);
    return result;
}

/**
 * @brief Get buffer allocation statistics for a specific NIC
 * @param nic_index NIC index
 * @param stats Pointer to receive statistics
 * @return SUCCESS on success, error code on failure
 */
int hardware_get_nic_buffer_stats(int nic_index, buffer_pool_stats_t* stats) {
    if (!hardware_validate_nic_index(nic_index) || !stats) {
        return ERROR_INVALID_PARAM;
    }
    
    return buffer_get_nic_stats((nic_id_t)nic_index, stats);
}

/**
 * @brief Trigger buffer resource rebalancing across all NICs
 * @return SUCCESS on success, error code on failure
 */
int hardware_rebalance_buffer_resources(void) {
    if (!g_hardware_initialized) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_INFO("Triggering hardware layer buffer resource rebalancing");
    return buffer_rebalance_resources();
}

/**
 * @brief Print comprehensive hardware and buffer statistics
 */
void hardware_print_comprehensive_stats(void) {
    int i;
    nic_info_t* nic;
    buffer_pool_stats_t nic_stats;

    if (!g_hardware_initialized) {
        LOG_INFO("Hardware layer not initialized");
        return;
    }

    LOG_INFO("=== Hardware Layer Comprehensive Statistics ===");

    /* Print basic hardware statistics */
    LOG_INFO("Hardware Stats:");
    LOG_INFO("  Active NICs: %d", g_num_nics);
    LOG_INFO("  Packets sent: %lu (success: %lu, failed: %lu)",
             g_hardware_stats.packets_sent,
             g_hardware_stats.successful_sends,
             g_hardware_stats.packets_sent - g_hardware_stats.successful_sends);
    LOG_INFO("  Packets received: %lu (success: %lu, failed: %lu)",
             g_hardware_stats.packets_received,
             g_hardware_stats.successful_receives,
             g_hardware_stats.packets_received - g_hardware_stats.successful_receives);

    /* Print per-NIC information */
    for (i = 0; i < g_num_nics; i++) {
        nic = &g_nic_infos[i];
        LOG_INFO("NIC %d (%s): Status 0x%X, Type %d, I/O 0x%X, IRQ %d",
                 i, nic->type == NIC_TYPE_3C509B ? "3C509B" :
                    (nic->type == NIC_TYPE_3C515_TX ? "3C515-TX" : "Unknown"),
                 nic->status, nic->type, nic->io_base, nic->irq);

        /* Print buffer statistics for this NIC */
        if (hardware_get_nic_buffer_stats(i, &nic_stats) == SUCCESS) {
            LOG_INFO("  Buffer Stats: %lu allocs, %lu failures, %lu current, %lu peak",
                     nic_stats.total_allocations, nic_stats.allocation_failures,
                     nic_stats.current_allocated, nic_stats.peak_allocated);
        }
    }

    /* Print comprehensive buffer statistics */
    buffer_print_comprehensive_stats();
}

/**
 * @brief Monitor hardware and buffer usage periodically
 */
void hardware_monitor_and_maintain(void) {
    static uint32_t last_monitor_time = 0;
    uint32_t current_time;
    int i;
    nic_info_t* nic;
    buffer_pool_stats_t stats;

    if (!g_hardware_initialized) {
        return;
    }

    current_time = get_system_timestamp_ms();

    /* Monitor every 30 seconds */
    if (current_time - last_monitor_time < 30000) {
        return;
    }

    LOG_DEBUG("Hardware maintenance and monitoring cycle");

    /* Monitor buffer usage and rebalance if needed */
    buffer_monitor_and_rebalance();

    /* Check for any NICs that need recovery */
    for (i = 0; i < g_num_nics; i++) {
        nic = &g_nic_infos[i];

        /* Basic health check */
        if (nic->status & NIC_STATUS_ACTIVE) {
            /* Check for buffer allocation failures */
            if (hardware_get_nic_buffer_stats(i, &stats) == SUCCESS) {
                if (stats.allocation_failures > 0) {
                    LOG_WARNING("NIC %d has %lu buffer allocation failures",
                               i, stats.allocation_failures);

                    /* Clear failure count after reporting */
                    /* Note: In a real implementation, we might want to trigger recovery */
                }
            }
        }
    }

    last_monitor_time = current_time;
}

/* NOTE: ASM HAL vtable infrastructure removed 2026-01-25.
 * The C nic_ops_t vtable (get_3c509b_ops(), get_3c515_ops()) is the
 * production path. See hwhal.h for HAL error codes and utilities.
 */

/**
 * @brief Convert HAL error code to string
 * @param error_code HAL error code
 * @return String description
 */
const char* hal_error_to_string(int error_code) {
    switch (error_code) {
        case HAL_SUCCESS: return "Success";
        case HAL_ERROR_INVALID_PARAM: return "Invalid parameter";
        case HAL_ERROR_HARDWARE_FAILURE: return "Hardware failure";
        case HAL_ERROR_TIMEOUT: return "Operation timeout";
        case HAL_ERROR_NOT_SUPPORTED: return "Not supported";
        case HAL_ERROR_RESOURCE_BUSY: return "Resource busy";
        case HAL_ERROR_INITIALIZATION: return "Initialization error";
        case HAL_ERROR_MEMORY: return "Memory error";
        case HAL_ERROR_DMA: return "DMA error";
        case HAL_ERROR_INTERRUPT: return "Interrupt error";
        case HAL_ERROR_LINK_DOWN: return "Link down";
        case HAL_ERROR_MEDIA_FAILURE: return "Media failure";
        case HAL_ERROR_CHECKSUM: return "Checksum error";
        default: return "Unknown error";
    }
}

/**
 * @brief Set PnP detection results for hardware integration
 * @param results Array of detected device information
 * @param count Number of devices detected
 */
void hardware_set_pnp_detection_results(const nic_detect_info_t *results, int count) {
    int i;
    const char* type_name;

    if (!results || count <= 0 || count > MAX_NICS) {
        LOG_WARNING("Invalid PnP detection results: results=%p, count=%d", results, count);
        g_pnp_detection_count = 0;
        return;
    }

    /* Store PnP detection results for later use during hardware initialization */
    memory_copy(g_pnp_detection_results, results, count * sizeof(nic_detect_info_t));
    g_pnp_detection_count = count;

    LOG_DEBUG("Stored %d PnP detection results for hardware integration", count);

    /* Log each detected device for debugging */
    for (i = 0; i < count; i++) {
        type_name = (results[i].type == NIC_TYPE_3C509B) ? "3C509B" :
                               (results[i].type == NIC_TYPE_3C515_TX) ? "3C515-TX" : "Unknown";
        LOG_DEBUG("PnP Device %d: %s at I/O 0x%X, IRQ %d", i, type_name,
                 results[i].io_base, results[i].irq);
    }
}

/**
 * @brief Get stored PnP detection results
 * @param results Buffer to receive results
 * @param max_count Maximum number of results to return
 * @return Number of results copied
 */
int hardware_get_pnp_detection_results(nic_detect_info_t *results, int max_count) {
    int copy_count;

    if (!results || max_count <= 0) {
        return 0;
    }

    copy_count = (g_pnp_detection_count < max_count) ? g_pnp_detection_count : max_count;
    if (copy_count > 0) {
        memory_copy(results, g_pnp_detection_results, copy_count * sizeof(nic_detect_info_t));
    }
    
    return copy_count;
}

/**
 * @brief Get number of stored PnP detection results
 * @return Number of stored results
 */
int hardware_get_pnp_detection_count(void) {
    return g_pnp_detection_count;
}

/**
 * @brief Get system timestamp for error tracking
 * @return Current timestamp
 */
static uint32_t hardware_get_timestamp(void) {
    /* Simple tick counter for DOS environment */
    static uint32_t tick_counter = 0;
    return ++tick_counter;
}

/**
 * @brief Convert media type to string
 * @param media_type Media type identifier
 * @return String description
 */
const char* hal_media_type_to_string(int media_type) {
    switch (media_type) {
        case HAL_MEDIA_AUTO: return "Auto-negotiate";
        case HAL_MEDIA_10_HALF: return "10 Mbps Half-duplex";
        case HAL_MEDIA_10_FULL: return "10 Mbps Full-duplex";
        case HAL_MEDIA_100_HALF: return "100 Mbps Half-duplex";
        case HAL_MEDIA_100_FULL: return "100 Mbps Full-duplex";
        default: return "Unknown media";
    }
}

/**
 * @brief Check if TX operation is complete
 * @param nic NIC information structure
 * @return 1 if TX complete, 0 if not, negative on error
 */
int hardware_check_tx_complete(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!g_hardware_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!nic->ops || !nic->ops->check_tx_complete) {
        /* If no specific implementation, assume TX is always complete */
        return 1;
    }
    
    return nic->ops->check_tx_complete(nic);
}

/**
 * @brief Check if RX data is available
 * @param nic NIC information structure  
 * @return 1 if RX available, 0 if not, negative on error
 */
int hardware_check_rx_available(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!g_hardware_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!nic->ops || !nic->ops->check_rx_available) {
        /* If no specific implementation, assume no RX data */
        return 0;
    }
    
    return nic->ops->check_rx_available(nic);
}

/* Forward declarations for static implementations */
static int _3c509b_check_tx_complete(struct nic_info *nic);
static int _3c509b_check_rx_available(struct nic_info *nic);
static int _3c515_check_tx_complete(struct nic_info *nic);
static int _3c515_check_rx_available(struct nic_info *nic);

/* 3C509B specific implementations */
static int _3c509b_check_tx_complete(struct nic_info *nic) {
    uint16_t status;

    if (!nic) return ERROR_INVALID_PARAM;

    /* Check EL3_STATUS for TxComplete event */
    status = inw(nic->io_base + EL3_STATUS);
    if (status & TxComplete) {
        /* Acknowledge the interrupt */
        outw(AckIntr | TxComplete, nic->io_base + EL3_CMD);
        return 1;
    }
    return 0;
}

static int _3c509b_check_rx_available(struct nic_info *nic) {
    uint16_t rx_status;

    if (!nic) return ERROR_INVALID_PARAM;

    /* Switch to Window 1 for RX_STATUS access */
    EL3WINDOW(nic, 1);
    rx_status = inw(nic->io_base + 0x08); /* RX_STATUS register */
    return (rx_status > 0) ? 1 : 0; /* Non-zero means data available */
}

/* 3C515 specific implementations */
static int _3c515_check_tx_complete(struct nic_info *nic) {
    uint8_t tx_status;

    if (!nic) return ERROR_INVALID_PARAM;

    /* Switch to Window 1 for TxStatus access */
    EL3WINDOW(nic, 1);
    tx_status = inb(nic->io_base + 0x1B); /* TxStatus register */

    if (tx_status) {
        /* Clear the status by writing it back */
        outb(tx_status, nic->io_base + 0x1B);
        return 1;
    }
    return 0;
}

static int _3c515_check_rx_available(struct nic_info *nic) {
    uint16_t rx_status;

    if (!nic) return ERROR_INVALID_PARAM;

    /* Switch to Window 1 for RxStatus access */
    EL3WINDOW(nic, 1);
    rx_status = inw(nic->io_base + 0x18); /* RxStatus register */
    return (rx_status > 0) ? 1 : 0; /* Non-zero means data available */
}

/**
 * @brief Get last error timestamp for a NIC
 * @param nic_index NIC index
 * @return Timestamp of last error, 0 if none
 */
uint32_t hardware_get_last_error_time(uint8_t nic_index) {
    if (nic_index >= MAX_NICS) {
        return 0;
    }
    return g_error_recovery_state.last_error_time[nic_index];
}
/* Attach a 3C589-like PCMCIA NIC using 3C509B PIO ops */
int hardware_attach_pcmcia_nic(uint16_t io_base, uint8_t irq, uint8_t socket)
{
    nic_info_t *nic;
    extern nic_ops_t* get_3c509b_ops(void);
    int rc;

    if (g_num_nics >= MAX_NICS) {
        LOG_ERROR("Cannot attach PCMCIA NIC: max NICs reached");
        return -1;
    }
    nic = &g_nic_infos[g_num_nics];
    memset(nic, 0, sizeof(*nic));
    nic->type = NIC_TYPE_3C509B; /* Reuse 3C509B ops for 3C589 PCMCIA */
    nic->io_base = io_base;
    nic->irq = irq;
    nic->status = NIC_STATUS_PRESENT | NIC_STATUS_INITIALIZED;

    /* Get 3C509B ops and initialize */
    nic->ops = get_3c509b_ops();
    if (nic->ops && nic->ops->init) {
        rc = nic->ops->init(nic);
        if (rc != SUCCESS) {
            LOG_ERROR("PCMCIA NIC init failed: %d", rc);
            memset(nic, 0, sizeof(*nic));
            return rc;
        }
    }
    /* Register with buffer system */
    buffer_register_nic(g_num_nics, nic->type, "3C589 PCMCIA");
    LOG_INFO("Attached PCMCIA NIC #%d at IO=0x%04X IRQ=%u (socket %u)", g_num_nics, io_base, irq, socket);
    return g_num_nics++;
}

/* Find NIC by I/O and IRQ */
int hardware_find_nic_by_io_irq(uint16_t io_base, uint8_t irq)
{
    int i;
    for (i = 0; i < g_num_nics; i++) {
        if (g_nic_infos[i].io_base == io_base && g_nic_infos[i].irq == irq) return i;
    }
    return -1;
}

/* Detach and remove NIC by index */
int hardware_detach_nic_by_index(int index)
{
    int i;
    nic_info_t *nic;
    if (index < 0 || index >= g_num_nics) return -1;
    nic = &g_nic_infos[index];
    LOG_INFO("Detaching NIC #%d (IO=0x%04X IRQ=%u)", index, nic->io_base, nic->irq);
    if (nic->ops && nic->ops->cleanup) nic->ops->cleanup(nic);
    buffer_unregister_nic(index);
    /* Compact array */
    for (i = index; i < g_num_nics - 1; i++) {
        g_nic_infos[i] = g_nic_infos[i + 1];
    }
    memset(&g_nic_infos[g_num_nics - 1], 0, sizeof(nic_info_t));
    g_num_nics--;
    return 0;
}
