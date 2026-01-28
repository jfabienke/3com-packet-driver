/**
 * @file api_rt.c
 * @brief Packet Driver API runtime functions (ROOT segment)
 *
 * Split from api.c on 2026-01-28 08:18:45
 *
 * This file contains runtime code that must remain resident after
 * driver initialization:
 * - INT 60h handler dispatch functions
 * - Packet send/receive API functions
 * - Handle table access functions
 * - Statistics query functions
 * - All functions callable from ISR context
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "api.h"
#include "hardware.h"
#include "pktops.h"
#include "logging.h"
#include "stats.h"
#include "routing.h"
#include "prod.h"
#include "arp.h"

/* Packet Driver API constants */
#define PD_MAX_HANDLES    16      /* Maximum number of handles */
#define PD_MAX_TYPES      8       /* Maximum number of packet types per handle */
#define PD_INVALID_HANDLE 0xFFFF  /* Invalid handle value */

/* Phase 3 Extended Constants */
#define PD_MAX_EXTENDED_HANDLES 16  /* Maximum extended handles */
#define PD_DEFAULT_PRIORITY     128 /* Default priority level */
#define PD_MAX_BANDWIDTH        0   /* Unlimited bandwidth */
#define PD_QOS_BUFFER_SIZE      64  /* QoS packet buffer size */
#define PD_FLOW_TIMEOUT_MS      30000 /* Flow timeout (30 seconds) */

/* Handle state structure (legacy Phase 2) */
typedef struct {
    uint16_t handle;           /* Handle ID */
    uint16_t packet_type;      /* Packet type */
    uint8_t class;             /* Packet class */
    uint8_t number;            /* Interface number */
    uint8_t type;              /* Basic/Extended mode */
    uint8_t flags;             /* Handle flags */
    void far *receiver;        /* Receiver function pointer */
    uint32_t packets_received; /* Statistics */
    uint32_t packets_dropped;  /* Statistics */
    uint32_t packets_sent;     /* TX packet count */
    uint32_t bytes_received;   /* RX byte count */
    uint32_t bytes_sent;       /* TX byte count */
} pd_handle_t;

/* Global API state - defined here, accessible from api_init.c via extern */
pd_handle_t handles[PD_MAX_HANDLES];
extended_packet_handle_t extended_handles[PD_MAX_EXTENDED_HANDLES];
int next_handle = 1;
int api_initialized = 0;
int extended_api_initialized = 0;
static uint16_t driver_signature = 0x3C0D; /* "3COm" in hex - proper 3Com signature */

/* GPT-5: API guard state to prevent calls during initialization */
/* Phase 5: Removed static for external access from unwind.c */
volatile int api_ready = 0;  /* Set to 1 only after full activation */

/* Phase 3 Global State */
int load_balancing_enabled = 0;
int qos_enabled = 0;
int virtual_interrupts_enabled = 0;
uint32_t global_bandwidth_limit = 0; /* Unlimited by default */
pd_load_balance_params_t global_lb_config;
pd_qos_params_t default_qos_params;

/* Load balancing and NIC management */
static uint32_t nic_weights[MAX_NICS] = {100, 100}; /* Equal weights by default */
static uint32_t nic_utilization[MAX_NICS] = {0, 0};
static uint32_t nic_error_counts[MAX_NICS] = {0, 0};
static uint32_t last_nic_used = 0; /* Round-robin state */

/* QoS packet queues (simplified implementation) */
static struct {
    uint8_t *packet_data[PD_QOS_BUFFER_SIZE];
    uint16_t packet_lengths[PD_QOS_BUFFER_SIZE];
    uint16_t handle_ids[PD_QOS_BUFFER_SIZE];
    uint8_t priorities[PD_QOS_BUFFER_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} qos_packet_queue;

/* Forward declarations for static functions */
static int should_deliver_packet(const pd_handle_t *handle, uint16_t eth_type, const uint8_t *packet);
static int deliver_packet_to_handler(pd_handle_t *handle, buffer_desc_t *buffer, uint16_t eth_type);
static uint32_t calculate_average_latency(extended_packet_handle_t *ext_handle);
static uint32_t calculate_jitter(extended_packet_handle_t *ext_handle);
static uint32_t get_system_timestamp(void);
static int api_load_balance_select_nic(uint16_t handle, const uint8_t *packet, uint8_t *selected_nic);
static int api_round_robin_select_nic(uint8_t *selected_nic);
static int api_weighted_select_nic(uint8_t *selected_nic);
static int api_performance_select_nic(uint8_t *selected_nic);
static int api_application_select_nic(uint16_t handle, uint8_t *selected_nic);
static int api_flow_aware_select_nic(uint16_t handle, const uint8_t *packet, uint8_t *selected_nic);

/* External assembly functions */
extern int packet_deliver_to_handler(uint16_t handle, uint16_t length,
                                     const void far *packet_data,
                                     void far *receiver_func);

/* Hot section: Performance-critical runtime functions */
#ifdef __WATCOMC__
#pragma code_seg ( "_TEXT" )
#else
#pragma code_seg("_TEXT", "CODE")
#endif

/**
 * @brief Access packet driver function
 * @param function Function number
 * @param handle Handle (if applicable)
 * @param params Function parameters
 * @return Function result
 */
int pd_access_type(uint8_t function, uint16_t handle, void *params) {
    /* GPT-5: Check API ready state before processing */
    if (!api_ready) {
        log_warning("API call during initialization - not ready");
        return API_ERR_NOT_READY;  /* Driver not ready */
    }

    log_debug("PD Access: function=0x%04X, handle=%04X", function, handle);

    /* Validate function number ranges */
    if (function < PD_FUNC_DRIVER_INFO ||
        (function > PD_FUNC_SET_ADDRESS && function < PD_FUNC_SET_HANDLE_PRIORITY) ||
        function > PD_FUNC_GET_ERROR_INFO) {
        log_error("Invalid function number: 0x%04X", function);
        return API_ERR_BAD_FUNCTION;
    }

    if (!api_initialized) {
        log_error("API not initialized");
        return API_ERR_NOT_INITIALIZED;
    }

    switch (function) {
        case PD_FUNC_DRIVER_INFO:
            return pd_get_driver_info(params);
        case PD_FUNC_ACCESS_TYPE:
            return pd_handle_access_type(params);
        case PD_FUNC_RELEASE_TYPE:
            return pd_release_handle(handle);
        case PD_FUNC_SEND_PKT:
            return pd_send_packet(handle, params);
        case PD_FUNC_TERMINATE:
            return pd_terminate(handle);
        case PD_FUNC_GET_ADDRESS:
            return pd_get_address(handle, params);
        case PD_FUNC_RESET_INTERFACE:
            return pd_reset_interface(handle);
        case PD_FUNC_GET_PARAMETERS:
            return pd_get_parameters(handle, params);
        case PD_FUNC_SET_RCV_MODE:
            return pd_set_rcv_mode(handle, params);
        case PD_FUNC_GET_RCV_MODE:
            return pd_get_rcv_mode(handle, params);
        case PD_FUNC_GET_STATISTICS:
            return pd_get_statistics(handle, params);
        case PD_FUNC_SET_ADDRESS:
            return pd_set_address(handle, params);

        /* Phase 3 Extended Functions */
        case PD_FUNC_SET_HANDLE_PRIORITY:
            return pd_set_handle_priority(handle, params);
        case PD_FUNC_GET_ROUTING_INFO:
            return pd_get_routing_info(handle, params);
        case PD_FUNC_SET_LOAD_BALANCE:
            return pd_set_load_balance(handle, params);
        case PD_FUNC_GET_NIC_STATUS:
            return pd_get_nic_status(handle, params);
        case PD_FUNC_SET_QOS_PARAMS:
            return pd_set_qos_params(handle, params);
        case PD_FUNC_GET_FLOW_STATS:
            return pd_get_flow_stats(handle, params);
        case PD_FUNC_SET_NIC_PREFERENCE:
            return pd_set_nic_preference(handle, params);
        case PD_FUNC_GET_HANDLE_INFO:
            return pd_get_handle_info(handle, params);
        case PD_FUNC_SET_BANDWIDTH_LIMIT:
            return pd_set_bandwidth_limit(handle, params);
        case PD_FUNC_GET_ERROR_INFO:
            return pd_get_error_info(handle, params);

        default:
            log_error("Unknown packet driver function: %d", function);
            return API_ERR_BAD_FUNCTION;
    }
}

/**
 * @brief Get driver information
 * @param info_ptr Pointer to driver info structure
 * @return 0 on success, negative on error
 */
int pd_get_driver_info(void *info_ptr) {
    pd_driver_info_t *info = (pd_driver_info_t *)info_ptr;

    /* GPT-5: Process deferred work on frequently-called API entry */
    extern void packet_process_deferred_work(void);
    packet_process_deferred_work();

    if (!info_ptr) {
        return API_ERR_INVALID_PARAM;
    }

    /* Fill in proper 3Com driver information */
    info->version = 0x0100;           /* Version 1.0 */
    info->class = PD_CLASS_ETHERNET;  /* Ethernet class */
    info->type = PD_TYPE_3COM;        /* 3Com type */
    info->number = 0;                 /* Interface 0 */
    info->basic = 1;                  /* Basic functions supported */
    info->extended = 1;               /* Extended functions supported */
    info->high_performance = 0;       /* High performance mode */

    strncpy(info->name, "3Com Packet Driver", sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';

    log_debug("Driver info requested");
    return 0;
}

/**
 * @brief Handle access type request
 * @param params Access type parameters
 * @return Handle on success, negative on error
 */
int pd_handle_access_type(void *params) {
    pd_access_params_t *access = (pd_access_params_t *)params;
    int i, handle_idx = -1;

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    log_debug("Access type: class=%d, type=%04X", access->class, access->type);

    /* Validate packet class */
    if (access->class != PD_CLASS_ETHERNET) {
        log_error("Unsupported packet class: %d", access->class);
        return API_ERR_INVALID_PARAM;
    }

    /* Validate interface number */
    if (access->number >= hardware_get_nic_count()) {
        log_error("Invalid interface number: %d", access->number);
        return API_ERR_NO_INTERFACE;
    }

    /* Find free handle slot */
    for (i = 0; i < PD_MAX_HANDLES; i++) {
        if (handles[i].handle == PD_INVALID_HANDLE) {
            handle_idx = i;
            break;
        }
    }

    if (handle_idx < 0) {
        log_error("No free handles available");
        return API_ERR_NO_HANDLES;
    }

    /* Initialize handle */
    handles[handle_idx].handle = next_handle++;
    handles[handle_idx].packet_type = access->type;
    handles[handle_idx].class = access->class;
    handles[handle_idx].number = access->number;
    handles[handle_idx].type = access->basic;
    handles[handle_idx].receiver = access->receiver;
    handles[handle_idx].packets_received = 0;
    handles[handle_idx].packets_dropped = 0;
    handles[handle_idx].packets_sent = 0;
    handles[handle_idx].bytes_received = 0;
    handles[handle_idx].bytes_sent = 0;

    log_info("Allocated handle %04X for type %04X",
             handles[handle_idx].handle, access->type);

    return handles[handle_idx].handle;
}

/**
 * @brief Release a packet driver handle
 * @param handle Handle to release
 * @return 0 on success, negative on error
 */
int pd_release_handle(uint16_t handle) {
    int i;

    int j;

    log_debug("Releasing handle %04X", handle);

    /* Release any extended handle resources */
    for (j = 0; j < PD_MAX_EXTENDED_HANDLES; j++) {
        if (extended_handles[j].handle_id == handle) {
            extended_handles[j].handle_id = PD_INVALID_HANDLE;
            memset(&extended_handles[j], 0, sizeof(extended_handles[j]));
            extended_handles[j].handle_id = PD_INVALID_HANDLE;
            extended_handles[j].priority = PD_DEFAULT_PRIORITY;
            extended_handles[j].preferred_nic = 0xFF;
            break;
        }
    }

    /* Find handle */
    for (i = 0; i < PD_MAX_HANDLES; i++) {
        if (handles[i].handle == handle) {
            log_info("Released handle %04X (rx=%lu, dropped=%lu)",
                     handle, handles[i].packets_received,
                     handles[i].packets_dropped);

            handles[i].handle = PD_INVALID_HANDLE;
            memset(&handles[i], 0, sizeof(handles[i]));
            handles[i].handle = PD_INVALID_HANDLE;
            return 0;
        }
    }

    log_error("Handle %04X not found", handle);
    return API_ERR_BAD_HANDLE;
}

/**
 * @brief Send a packet through the appropriate NIC
 * @param handle Sender handle
 * @param params Send parameters
 * @return 0 on success, negative on error
 */
int pd_send_packet(uint16_t handle, void *params) {
    pd_send_params_t *send = (pd_send_params_t *)params;
    buffer_desc_t *tx_buffer = NULL;
    int result;
    int i, handle_idx = -1;
    uint8_t interface_num = 0;
    uint8_t selected_nic;
    extended_packet_handle_t *ext_handle;
    nic_info_t *nic;

    if (!params || !send->buffer) {
        return API_ERR_INVALID_PARAM;
    }

    log_debug("Send packet: handle=%04X, len=%d", handle, send->length);

    /* Validate handle and find interface */
    for (i = 0; i < PD_MAX_HANDLES; i++) {
        if (handles[i].handle == handle) {
            handle_idx = i;
            interface_num = handles[i].number;
            break;
        }
    }

    if (handle_idx < 0) {
        log_error("Invalid handle %04X", handle);
        return API_ERR_BAD_HANDLE;
    }

    /* Validate packet length */
    if (send->length < 60 || send->length > 1514) {
        log_error("Invalid packet length: %d", send->length);
        return API_ERR_INVALID_PARAM;
    }

    /* Allocate transmit buffer */
    tx_buffer = buffer_alloc_ethernet_frame(send->length, BUFFER_TYPE_TX);
    if (!tx_buffer) {
        log_error("Failed to allocate TX buffer");
        return API_ERR_INVALID_PARAM;
    }

    /* Copy packet data to TX buffer */
    /* Cast far pointer to near - buffer_set_data uses _fmemcpy internally */
    result = buffer_set_data(tx_buffer, (const void *)send->buffer, send->length);
    if (result < 0) {
        log_error("Failed to copy packet data to TX buffer");
        buffer_free_any(tx_buffer);
        return API_ERR_INVALID_PARAM;
    }

    /* Phase 3 Enhanced Packet Sending with Intelligent NIC Selection */

    /* Check bandwidth limit for extended handles */
    result = api_check_bandwidth_limit(handle, send->length);
    if (result != API_SUCCESS) {
        log_debug("Bandwidth limit exceeded for handle %04X", handle);
        buffer_free_any(tx_buffer);
        return result;
    }

    /* Select optimal NIC using Phase 3 intelligence */
    selected_nic = interface_num; /* Default to handle's interface */
    /* Cast far pointer to near - only header bytes are examined */
    result = api_select_optimal_nic(handle, (const uint8_t *)send->buffer, &selected_nic);
    if (result == API_SUCCESS && selected_nic != interface_num) {
        /* Update interface number based on intelligent selection */
        interface_num = selected_nic;

        /* Update extended handle statistics */
        if (api_get_extended_handle(handle, &ext_handle) == API_SUCCESS) {
            ext_handle->nic_switches++;
            ext_handle->interface_num = selected_nic;
        }

        log_debug("Intelligent routing selected NIC %d for handle %04X", selected_nic, handle);
    }

    /* Send packet through hardware layer using direct vtable dispatch */
    nic = hardware_get_nic(interface_num);
    if (!nic || !nic->ops || !nic->ops->send_packet) {
        buffer_free_any(tx_buffer);
        return API_ERR_FUNCTION_NOT_SUPPORTED;
    }

    result = nic->ops->send_packet(nic, buffer_get_data_ptr(tx_buffer), send->length);

    /* Update NIC utilization statistics */
    api_update_nic_utilization(interface_num, send->length);

    /* Free the TX buffer */
    buffer_free_any(tx_buffer);

    if (result < 0) {
        log_error("Hardware send failed: %d", result);
        return result;
    }

    /* Update statistics */
    stats_increment_tx_packets();
    stats_add_tx_bytes(send->length);

    log_debug("Packet sent successfully through interface %d", interface_num);
    return 0;
}

/**
 * @brief Terminate driver
 * @param handle Handle (usually ignored)
 * @return 0 on success, negative on error
 */
int pd_terminate(uint16_t handle) {
    log_info("Driver termination requested (handle=%04X)", handle);

    /* Driver termination is handled at TSR level */
    /* Individual handles cannot terminate the entire driver */
    if (handle != PD_INVALID_HANDLE && pd_validate_handle(handle)) {
        /* Release specific handle instead */
        return pd_release_handle(handle);
    }

    /* This is typically handled at a higher level */
    return API_ERR_FUNCTION_NOT_SUPPORTED;
}

/**
 * @brief Get interface address
 * @param handle Handle
 * @param params Address parameters
 * @return 0 on success, negative on error
 */
int pd_get_address(uint16_t handle, void *params) {
    pd_address_params_t *addr = (pd_address_params_t *)params;
    nic_info_t *nic;
    int i;
    int interface_num = 0;

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    log_debug("Get address: handle=%04X", handle);

    /* Validate handle */
    if (!pd_validate_handle(handle)) {
        return API_ERR_BAD_HANDLE;
    }

    /* Find the interface number for this handle */
    for (i = 0; i < PD_MAX_HANDLES; i++) {
        if (handles[i].handle == handle) {
            interface_num = handles[i].number;
            break;
        }
    }

    /* Get NIC info using interface number */
    nic = hardware_get_nic(interface_num);
    if (!nic) {
        return API_ERR_NO_INTERFACE;
    }

    if (!nic->ops || !nic->ops->get_mac_address) {
        return API_ERR_FUNCTION_NOT_SUPPORTED;
    }

    return nic->ops->get_mac_address(nic, addr->address);
}

/**
 * @brief Reset interface
 * @param handle Handle
 * @return 0 on success, negative on error
 */
int pd_reset_interface(uint16_t handle) {
    nic_info_t *nic;
    int i;
    int interface_num = 0;

    log_debug("Reset interface: handle=%04X", handle);

    /* Find interface number for this handle */
    for (i = 0; i < PD_MAX_HANDLES; i++) {
        if (handles[i].handle == handle) {
            interface_num = handles[i].number;
            break;
        }
    }

    if (i >= PD_MAX_HANDLES) {
        return API_ERR_BAD_HANDLE;
    }

    /* Validate handle */
    if (!pd_validate_handle(handle)) {
        return API_ERR_BAD_HANDLE;
    }

    /* Get NIC using interface number from handle */
    nic = hardware_get_nic(interface_num);
    if (!nic || !nic->ops || !nic->ops->init) {
        return API_ERR_NO_INTERFACE;
    }

    return nic->ops->init(nic);
}

/**
 * @brief Get interface parameters
 * @param handle Handle
 * @param params Parameter structure
 * @return 0 on success, negative on error
 */
int pd_get_parameters(uint16_t handle, void *params) {
    /* C89: Declarations must be at the start of the block */
    pd_driver_info_t *driver_params;

    log_debug("Get parameters: handle=%04X", handle);

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    /* Fill basic interface parameters */
    driver_params = (pd_driver_info_t *)params;
    driver_params->version = 0x0100;
    driver_params->class = PD_CLASS_ETHERNET;
    driver_params->type = PD_TYPE_3COM;
    driver_params->basic = 1;
    driver_params->extended = extended_api_initialized ? 1 : 0;
    driver_params->high_performance = 0;

    /* Validate handle */
    if (!pd_validate_handle(handle)) {
        return API_ERR_BAD_HANDLE;
    }

    /* Return basic parameters */
    return 0;
}

/**
 * @brief Set receive mode
 * @param handle Handle
 * @param params Mode parameters
 * @return 0 on success, negative on error
 */
int pd_set_rcv_mode(uint16_t handle, void *params) {
    uint16_t *mode = (uint16_t *)params;
    int i, interface_num = 0;
    nic_info_t *nic;

    log_debug("Set receive mode: handle=%04X", handle);

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    /* Validate handle */
    if (!pd_validate_handle(handle)) {
        return API_ERR_BAD_HANDLE;
    }

    /* Find the interface number for this handle */
    for (i = 0; i < PD_MAX_HANDLES; i++) {
        if (handles[i].handle == handle) {
            interface_num = handles[i].number;
            break;
        }
    }

    nic = hardware_get_nic(interface_num);
    if (!nic) {
        return API_ERR_NO_INTERFACE;
    }

    if (!nic->ops || !nic->ops->set_receive_mode) {
        return API_ERR_FUNCTION_NOT_SUPPORTED;
    }

    return nic->ops->set_receive_mode(nic, (uint8_t)*mode);
}

/**
 * @brief Get receive mode
 * @param handle Handle
 * @param params Mode parameters
 * @return 0 on success, negative on error
 */
int pd_get_rcv_mode(uint16_t handle, void *params) {
    int i;
    int interface_num = 0;
    nic_info_t *nic;

    log_debug("Get receive mode: handle=%04X", handle);

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    /* Validate handle */
    if (!pd_validate_handle(handle)) {
        return API_ERR_BAD_HANDLE;
    }

    /* Get current receive mode from hardware */
    for (i = 0; i < PD_MAX_HANDLES; i++) {
        if (handles[i].handle == handle) {
            interface_num = handles[i].number;
            break;
        }
    }

    nic = hardware_get_nic(interface_num);
    if (nic) {
        /* Return the cached receive mode from NIC info structure */
        *(uint16_t *)params = nic->receive_mode;
        return 0;
    }

    /* NIC not available */
    return API_ERR_NO_INTERFACE;
}

/**
 * @brief Get interface statistics
 * @param handle Handle
 * @param params Statistics structure
 * @return 0 on success, negative on error
 */
int pd_get_statistics(uint16_t handle, void *params) {
    pd_statistics_t *stats = (pd_statistics_t *)params;
    int i, interface_num = 0;
    nic_info_t *nic;

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    log_debug("Get statistics: handle=%04X", handle);

    /* Validate handle */
    if (!pd_validate_handle(handle)) {
        return API_ERR_BAD_HANDLE;
    }

    /* Find handle and get interface number */
    for (i = 0; i < PD_MAX_HANDLES; i++) {
        if (handles[i].handle == handle) {
            interface_num = handles[i].number;

            /* Initialize with basic handle statistics */
            stats->packets_in = handles[i].packets_received;
            stats->packets_out = handles[i].packets_sent; /* Per-handle TX stats */
            stats->bytes_in = handles[i].bytes_received;    /* Per-handle byte stats */
            stats->bytes_out = handles[i].bytes_sent;
            stats->errors_in = handles[i].packets_dropped;
            stats->errors_out = 0;
            stats->packets_lost = handles[i].packets_dropped;

            /* Enhance with hardware-specific statistics through vtable */
            nic = hardware_get_nic(interface_num);
            if (nic && nic->ops && nic->ops->get_statistics) {
                /* Get hardware-specific statistics through vtable */
                nic->ops->get_statistics(nic, stats);
            }

            return 0;
        }
    }

    return API_ERR_BAD_HANDLE;
}

/**
 * @brief Set interface address
 * @param handle Handle
 * @param params Address parameters
 * @return 0 on success, negative on error
 */
int pd_set_address(uint16_t handle, void *params) {
    log_debug("Set address: handle=%04X", handle);

    /* Most Ethernet cards don't allow MAC address changes */
    /* This is typically read-only for security reasons */
    log_warning("Attempt to set MAC address on handle %04X (not allowed)", handle);

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    /* Validate handle */
    if (!pd_validate_handle(handle)) {
        return API_ERR_BAD_HANDLE;
    }

    /* Address setting is typically not allowed */
    return API_ERR_FUNCTION_NOT_SUPPORTED;
}

/**
 * @brief Validate handle
 * @param handle Handle to validate
 * @return 1 if valid, 0 if invalid
 */
int pd_validate_handle(uint16_t handle) {
    int i;

    for (i = 0; i < PD_MAX_HANDLES; i++) {
        if (handles[i].handle == handle) {
            return 1;
        }
    }

    return 0;
}

/**
 * @brief Process received packet and deliver to registered handlers
 * @param packet Packet data
 * @param length Packet length
 * @param nic_id NIC that received the packet
 * @return 0 on success, negative on error
 */
int api_process_received_packet(const uint8_t *packet, size_t length, int nic_id) {
    int i, delivered = 0;
    uint16_t eth_type;
    buffer_desc_t *rx_buffer = NULL;

    if (!packet || length < 14) { /* Minimum Ethernet frame size */
        return API_ERR_INVALID_PARAM;
    }

    if (!api_initialized) {
        log_debug("API not initialized, dropping packet");
        return API_ERR_NOT_INITIALIZED;
    }

    log_debug("Processing received packet, length=%d, nic=%d", length, nic_id);

    /* Extract Ethernet type from packet header */
    eth_type = (packet[12] << 8) | packet[13];

    /* Allocate buffer for packet delivery if we have handlers */
    for (i = 0; i < PD_MAX_HANDLES; i++) {
        if (handles[i].handle != PD_INVALID_HANDLE) {
            /* Check packet type filtering */
            if (should_deliver_packet(&handles[i], eth_type, packet)) {
                /* Allocate RX buffer on first match */
                if (!rx_buffer) {
                    rx_buffer = buffer_alloc_ethernet_frame(length, BUFFER_TYPE_RX);
                    if (!rx_buffer) {
                        log_error("Failed to allocate RX buffer for packet delivery");
                        return API_ERR_INVALID_PARAM;
                    }
                    /* Copy packet data to allocated buffer */
                    buffer_set_data(rx_buffer, packet, length);
                }

                /* Deliver to application callback */
                if (deliver_packet_to_handler(&handles[i], rx_buffer, eth_type)) {
                    handles[i].packets_received++;
                    delivered = 1;
                    log_debug("Delivered packet to handle %04X", handles[i].handle);
                } else {
                    handles[i].packets_dropped++;
                    log_debug("Failed to deliver packet to handle %04X", handles[i].handle);
                }
            }
        }
    }

    /* Free the RX buffer if we allocated one */
    if (rx_buffer) {
        buffer_free_any(rx_buffer);
    }

    if (!delivered) {
        log_debug("No handlers for packet type %04X", eth_type);
    }

    /* Update global statistics */
    stats_increment_rx_packets();
    stats_add_rx_bytes(length);

    return delivered ? 0 : API_ERR_NO_HANDLERS;
}

/**
 * @brief Check if packet should be delivered to handler based on filtering
 * @param handle Handle information
 * @param eth_type Ethernet type from packet
 * @param packet Packet data for additional filtering
 * @return 1 if should deliver, 0 if not
 */
static int should_deliver_packet(const pd_handle_t *handle, uint16_t eth_type, const uint8_t *packet) {
    if (!handle || handle->handle == PD_INVALID_HANDLE) {
        return 0;
    }

    /* Check packet type filter */
    if (handle->packet_type != 0 && handle->packet_type != eth_type) {
        return 0; /* Type doesn't match */
    }

    /* Add additional filtering based on receive mode */
    /* Check if handle is in promiscuous mode or specific filtering */
    if (handle->flags & 0x01) { /* Promiscuous mode */
        return 1; /* Accept all packets */
    }

    /* For now, basic type filtering is sufficient */

    return 1; /* Packet should be delivered */
}

/**
 * @brief Deliver packet to application handler
 * @param handle Handle information
 * @param buffer Packet buffer
 * @param eth_type Ethernet type
 * @return 1 on success, 0 on failure
 */
static int deliver_packet_to_handler(pd_handle_t *handle, buffer_desc_t *buffer, uint16_t eth_type) {
    void far *receiver;
    uint16_t length;
    void far *data_ptr;
    int result;

    if (!handle || !buffer) {
        return 0;
    }

    receiver = handle->receiver;
    if (!receiver) {
        log_debug("Handle %04X has no receiver function", handle->handle);
        return 0;
    }

    length = (uint16_t)buffer_get_used_size(buffer);
    data_ptr = buffer_get_data_ptr(buffer);

    /* Call receiver function using assembly helper */
    result = packet_deliver_to_handler(handle->handle, length, data_ptr, receiver);

    if (result == 0) {
        log_debug("Successfully delivered packet to receiver at %Fp", receiver);
        return 1;
    } else {
        log_debug("Failed to deliver packet to receiver at %Fp", receiver);
        return 0;
    }
}

/* Phase 3 Group 3B Extended API Function Implementations */

/**
 * @brief Get extended handle structure for a given handle ID
 * @param handle Handle ID
 * @param ext_handle Pointer to receive extended handle pointer
 * @return 0 on success, negative on error
 */
int api_get_extended_handle(uint16_t handle, extended_packet_handle_t **ext_handle) {
    int i;

    if (!ext_handle) {
        return API_ERR_INVALID_PARAM;
    }

    *ext_handle = NULL;

    /* Find extended handle */
    for (i = 0; i < PD_MAX_EXTENDED_HANDLES; i++) {
        if (extended_handles[i].handle_id == handle) {
            *ext_handle = &extended_handles[i];
            return API_SUCCESS;
        }
    }

    return API_ERR_BAD_HANDLE;
}

/**
 * @brief Upgrade a basic handle to extended handle
 * @param handle Handle ID to upgrade
 * @return 0 on success, negative on error
 */
int api_upgrade_handle(uint16_t handle) {
    int i, basic_idx = -1, ext_idx = -1;
    int result;

    if (!extended_api_initialized) {
        result = api_init_extended_handles();
        if (result != API_SUCCESS) {
            return result;
        }
    }

    /* Find basic handle */
    for (i = 0; i < PD_MAX_HANDLES; i++) {
        if (handles[i].handle == handle) {
            basic_idx = i;
            break;
        }
    }

    if (basic_idx < 0) {
        return API_ERR_BAD_HANDLE;
    }

    /* Find free extended handle slot */
    for (i = 0; i < PD_MAX_EXTENDED_HANDLES; i++) {
        if (extended_handles[i].handle_id == PD_INVALID_HANDLE) {
            ext_idx = i;
            break;
        }
    }

    if (ext_idx < 0) {
        return API_ERR_NO_HANDLES;
    }

    /* Copy basic handle data to extended handle */
    extended_handles[ext_idx].handle_id = handles[basic_idx].handle;
    extended_handles[ext_idx].packet_type = handles[basic_idx].packet_type;
    extended_handles[ext_idx].interface_num = handles[basic_idx].number;
    extended_handles[ext_idx].receiver_func = handles[basic_idx].receiver;

    /* Set default extended values */
    extended_handles[ext_idx].priority = PD_DEFAULT_PRIORITY;
    extended_handles[ext_idx].preferred_nic = 0xFF; /* No preference */
    extended_handles[ext_idx].bandwidth_limit = PD_MAX_BANDWIDTH;
    extended_handles[ext_idx].flags = HANDLE_FLAG_ROUTING_AWARE;
    extended_handles[ext_idx].routing_preferences = 0;

    /* Initialize statistics */
    extended_handles[ext_idx].packets_routed = 0;
    extended_handles[ext_idx].routing_failures = 0;
    extended_handles[ext_idx].qos_drops = 0;
    extended_handles[ext_idx].bandwidth_drops = 0;
    extended_handles[ext_idx].nic_switches = 0;
    extended_handles[ext_idx].last_packet_time = 0;
    extended_handles[ext_idx].bytes_this_second = 0;
    extended_handles[ext_idx].time_window_start = 0;

    log_info("Upgraded handle %04X to extended handle", handle);
    return API_SUCCESS;
}

/**
 * @brief Set handle priority (AH=20h)
 * @param handle Handle ID
 * @param params Priority parameters
 * @return 0 on success, negative on error
 */
int pd_set_handle_priority(uint16_t handle, void *params) {
    extended_packet_handle_t *ext_handle;
    uint8_t *priority = (uint8_t *)params;
    int result;

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    log_debug("Set handle priority: handle=%04X, priority=%d", handle, *priority);

    /* Get or create extended handle */
    result = api_get_extended_handle(handle, &ext_handle);
    if (result != API_SUCCESS) {
        result = api_upgrade_handle(handle);
        if (result != API_SUCCESS) {
            return result;
        }
        result = api_get_extended_handle(handle, &ext_handle);
        if (result != API_SUCCESS) {
            return result;
        }
    }

    ext_handle->priority = *priority;
    ext_handle->flags |= HANDLE_FLAG_PRIORITY_ENABLED;

    log_info("Set priority %d for handle %04X", *priority, handle);
    return API_SUCCESS;
}

/**
 * @brief Get routing information (AH=21h)
 * @param handle Handle ID
 * @param params Routing info structure
 * @return 0 on success, negative on error
 */
int pd_get_routing_info(uint16_t handle, void *params) {
    pd_routing_info_t *info = (pd_routing_info_t *)params;
    const routing_stats_t *routing_stats;

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    log_debug("Get routing info: handle=%04X", handle);

    /* Validate handle */
    if (!pd_validate_handle(handle)) {
        return API_ERR_BAD_HANDLE;
    }

    /* Get routing statistics from Group 3A */
    routing_stats = routing_get_stats();
    if (!routing_stats) {
        return API_ERR_ROUTING_FAILED;
    }

    /* Fill routing information */
    info->route_count = g_routing_table.entry_count;
    info->arp_entries = g_arp_cache.entry_count;
    info->packets_routed = routing_stats->packets_routed;
    info->routing_errors = routing_stats->routing_errors;
    info->default_nic = g_routing_table.default_nic;
    info->routing_mode = routing_is_enabled() ? 1 : 0;
    info->reserved = 0;

    return API_SUCCESS;
}

/**
 * @brief Set load balancing configuration (AH=22h)
 * @param handle Handle ID
 * @param params Load balance parameters
 * @return 0 on success, negative on error
 */
int pd_set_load_balance(uint16_t handle, void *params) {
    pd_load_balance_params_t *lb_params = (pd_load_balance_params_t *)params;
    extended_packet_handle_t *ext_handle;
    int result;

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    log_debug("Set load balance: handle=%04X, mode=%d", handle, lb_params->mode);

    /* Validate load balance mode */
    if (lb_params->mode > LB_MODE_FLOW_AWARE) {
        return API_ERR_INVALID_PARAM;
    }

    /* Validate NIC indices */
    if (!routing_validate_nic(lb_params->primary_nic) ||
        !routing_validate_nic(lb_params->secondary_nic)) {
        return API_ERR_NIC_UNAVAILABLE;
    }

    /* Get or create extended handle */
    result = api_get_extended_handle(handle, &ext_handle);
    if (result != API_SUCCESS) {
        result = api_upgrade_handle(handle);
        if (result != API_SUCCESS) {
            return result;
        }
        result = api_get_extended_handle(handle, &ext_handle);
        if (result != API_SUCCESS) {
            return result;
        }
    }

    /* Update global load balancing configuration */
    memcpy(&global_lb_config, lb_params, sizeof(pd_load_balance_params_t));

    ext_handle->flags |= HANDLE_FLAG_LOAD_BALANCE;
    load_balancing_enabled = 1;

    log_info("Load balancing enabled for handle %04X (mode=%d)", handle, lb_params->mode);
    return API_SUCCESS;
}

/**
 * @brief Get NIC status information (AH=23h)
 * @param handle Handle ID
 * @param params NIC status structure
 * @return 0 on success, negative on error
 */
int pd_get_nic_status(uint16_t handle, void *params) {
    pd_nic_status_t *status = (pd_nic_status_t *)params;
    nic_info_t *nic;

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    log_debug("Get NIC status: handle=%04X, nic=%d", handle, status->nic_index);

    /* Validate NIC index */
    if (!routing_validate_nic(status->nic_index)) {
        return API_ERR_NIC_UNAVAILABLE;
    }

    /* Get NIC information from hardware layer */
    nic = hardware_get_nic(status->nic_index);
    if (!nic) {
        return API_ERR_NIC_UNAVAILABLE;
    }

    /* Fill NIC status */
    status->status = (uint8_t)nic->status;
    status->link_speed = (uint16_t)nic->speed;
    status->utilization = nic_utilization[status->nic_index];
    status->error_count = nic_error_counts[status->nic_index];
    status->last_error_time = hardware_get_last_error_time(status->nic_index); /* Error timestamp tracking */

    switch (status->status) {
        case NIC_STATUS_UP:
            strcpy(status->status_text, "Link Up");
            break;
        case NIC_STATUS_DOWN:
            strcpy(status->status_text, "Link Down");
            break;
        case NIC_STATUS_ERROR:
            strcpy(status->status_text, "Error");
            break;
        case NIC_STATUS_DEGRADED:
            strcpy(status->status_text, "Degraded");
            break;
        default:
            strcpy(status->status_text, "Unknown");
            break;
    }

    return API_SUCCESS;
}

/**
 * @brief Set QoS parameters for handle (AH=24h)
 * @param handle Handle ID
 * @param params QoS parameters
 * @return 0 on success, negative on error
 */
int pd_set_qos_params(uint16_t handle, void *params) {
    pd_qos_params_t *qos_params = (pd_qos_params_t *)params;
    extended_packet_handle_t *ext_handle;
    int result;

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    log_debug("Set QoS params: handle=%04X, class=%d", handle, qos_params->priority_class);

    /* Validate QoS class */
    if (qos_params->priority_class > QOS_CLASS_NETWORK) {
        return API_ERR_INVALID_PARAM;
    }

    /* Get or create extended handle */
    result = api_get_extended_handle(handle, &ext_handle);
    if (result != API_SUCCESS) {
        result = api_upgrade_handle(handle);
        if (result != API_SUCCESS) {
            return result;
        }
        result = api_get_extended_handle(handle, &ext_handle);
        if (result != API_SUCCESS) {
            return result;
        }
    }

    /* Set bandwidth limit from QoS parameters */
    if (qos_params->max_bandwidth > 0) {
        ext_handle->bandwidth_limit = qos_params->max_bandwidth;
        ext_handle->flags |= HANDLE_FLAG_BANDWIDTH_LIMIT;
    }

    /* Map QoS class to priority */
    ext_handle->priority = (qos_params->priority_class + 1) * 32; /* Map 0-7 to 32-256 */
    ext_handle->flags |= HANDLE_FLAG_QOS_ENABLED;

    qos_enabled = 1;

    log_info("QoS enabled for handle %04X (class=%d, priority=%d)",
             handle, qos_params->priority_class, ext_handle->priority);
    return API_SUCCESS;
}

/**
 * @brief Get flow statistics for handle (AH=25h)
 * @param handle Handle ID
 * @param params Flow statistics structure
 * @return 0 on success, negative on error
 */
int pd_get_flow_stats(uint16_t handle, void *params) {
    pd_flow_stats_t *flow_stats = (pd_flow_stats_t *)params;
    extended_packet_handle_t *ext_handle;
    int result;
    int i;

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    log_debug("Get flow stats: handle=%04X", handle);

    /* Get extended handle */
    result = api_get_extended_handle(handle, &ext_handle);
    if (result != API_SUCCESS) {
        /* Return basic stats for non-extended handles */
        for (i = 0; i < PD_MAX_HANDLES; i++) {
            if (handles[i].handle == handle) {
                flow_stats->handle = handle;
                flow_stats->flow_id = handle; /* Use handle as flow ID */
                flow_stats->packets_sent = handles[i].packets_sent; /* Per-handle TX stats */
                flow_stats->bytes_sent = 0;
                flow_stats->avg_latency = 0;
                flow_stats->jitter = 0;
                flow_stats->active_nic = handles[i].number;
                flow_stats->flow_state = FLOW_STATE_ACTIVE;
                return API_SUCCESS;
            }
        }
        return API_ERR_BAD_HANDLE;
    }

    /* Fill flow statistics from extended handle */
    flow_stats->handle = handle;
    flow_stats->flow_id = handle; /* Use handle as flow ID for simplicity */
    flow_stats->packets_sent = ext_handle->packets_routed;
    flow_stats->bytes_sent = ext_handle->bytes_this_second; /* Track bytes sent */
    flow_stats->avg_latency = calculate_average_latency(ext_handle); /* Calculate average latency */
    flow_stats->jitter = calculate_jitter(ext_handle); /* Calculate jitter */
    flow_stats->active_nic = ext_handle->interface_num;
    flow_stats->flow_state = (ext_handle->flags & HANDLE_FLAG_ROUTING_AWARE) ?
                            FLOW_STATE_ACTIVE : FLOW_STATE_INACTIVE;

    return API_SUCCESS;
}

/**
 * @brief Set NIC preference for handle
 * @param handle Handle ID
 * @param params NIC preference parameters
 * @return 0 on success, negative on error
 */
int pd_set_nic_preference(uint16_t handle, void *params) {
    extended_packet_handle_t *ext_handle;
    uint8_t *preferred_nic = (uint8_t *)params;
    int result;

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    log_debug("Set NIC preference: handle=%04X, nic=%d", handle, *preferred_nic);

    /* Validate NIC index */
    if (*preferred_nic != 0xFF && !routing_validate_nic(*preferred_nic)) {
        return API_ERR_NIC_UNAVAILABLE;
    }

    /* Get or create extended handle */
    result = api_get_extended_handle(handle, &ext_handle);
    if (result != API_SUCCESS) {
        result = api_upgrade_handle(handle);
        if (result != API_SUCCESS) {
            return result;
        }
        result = api_get_extended_handle(handle, &ext_handle);
        if (result != API_SUCCESS) {
            return result;
        }
    }

    ext_handle->preferred_nic = *preferred_nic;
    ext_handle->flags |= HANDLE_FLAG_NIC_PREFERENCE;

    log_info("Set NIC preference %d for handle %04X", *preferred_nic, handle);
    return API_SUCCESS;
}

/**
 * @brief Get extended handle information
 * @param handle Handle ID
 * @param params Handle info structure
 * @return 0 on success, negative on error
 */
int pd_get_handle_info(uint16_t handle, void *params) {
    extended_packet_handle_t *ext_handle;
    extended_packet_handle_t *info = (extended_packet_handle_t *)params;
    int result;

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    log_debug("Get handle info: handle=%04X", handle);

    /* Get extended handle */
    result = api_get_extended_handle(handle, &ext_handle);
    if (result != API_SUCCESS) {
        return API_ERR_BAD_HANDLE;
    }

    /* Copy extended handle information */
    memcpy(info, ext_handle, sizeof(extended_packet_handle_t));

    return API_SUCCESS;
}

/**
 * @brief Set bandwidth limit for handle
 * @param handle Handle ID
 * @param params Bandwidth limit parameters
 * @return 0 on success, negative on error
 */
int pd_set_bandwidth_limit(uint16_t handle, void *params) {
    extended_packet_handle_t *ext_handle;
    uint32_t *bandwidth_limit = (uint32_t *)params;
    int result;

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    log_debug("Set bandwidth limit: handle=%04X, limit=%lu", handle, *bandwidth_limit);

    /* Get or create extended handle */
    result = api_get_extended_handle(handle, &ext_handle);
    if (result != API_SUCCESS) {
        result = api_upgrade_handle(handle);
        if (result != API_SUCCESS) {
            return result;
        }
        result = api_get_extended_handle(handle, &ext_handle);
        if (result != API_SUCCESS) {
            return result;
        }
    }

    ext_handle->bandwidth_limit = *bandwidth_limit;
    if (*bandwidth_limit > 0) {
        ext_handle->flags |= HANDLE_FLAG_BANDWIDTH_LIMIT;
    } else {
        ext_handle->flags &= ~HANDLE_FLAG_BANDWIDTH_LIMIT;
    }

    log_info("Set bandwidth limit %lu bytes/sec for handle %04X", *bandwidth_limit, handle);
    return API_SUCCESS;
}

/**
 * @brief Get error information
 * @param handle Handle ID
 * @param params Error info structure
 * @return 0 on success, negative on error
 */
int pd_get_error_info(uint16_t handle, void *params) {
    pd_error_info_t *error_info = (pd_error_info_t *)params;

    if (!params) {
        return API_ERR_INVALID_PARAM;
    }

    log_debug("Get error info: handle=%04X", handle);

    /* Validate handle */
    if (!pd_validate_handle(handle)) {
        return API_ERR_BAD_HANDLE;
    }

    /* Return basic error information */
    error_info->error_code = 0; /* No error */
    error_info->error_time = 0;
    error_info->affected_nic = 0xFF; /* No specific NIC */
    error_info->error_severity = ERROR_SEVERITY_INFO;
    error_info->recovery_action = 0; /* No action needed */
    strcpy(error_info->error_description, "No errors");

    return API_SUCCESS;
}

/* Advanced Multiplexing and Load Balancing Functions */

/**
 * @brief Select optimal NIC for packet transmission
 * @param handle Handle ID
 * @param packet Packet data for analysis
 * @param selected_nic Pointer to receive selected NIC index
 * @return 0 on success, negative on error
 */
int api_select_optimal_nic(uint16_t handle, const uint8_t *packet, uint8_t *selected_nic) {
    extended_packet_handle_t *ext_handle;
    uint8_t dest_nic;
    int result;
    packet_buffer_t routing_packet;
    route_decision_t decision;

    if (!packet || !selected_nic) {
        return API_ERR_INVALID_PARAM;
    }

    *selected_nic = 0; /* Default to first NIC */

    /* Get extended handle if available */
    result = api_get_extended_handle(handle, &ext_handle);
    if (result == API_SUCCESS) {
        /* Check NIC preference */
        if ((ext_handle->flags & HANDLE_FLAG_NIC_PREFERENCE) &&
            ext_handle->preferred_nic != 0xFF &&
            routing_validate_nic(ext_handle->preferred_nic)) {
            *selected_nic = ext_handle->preferred_nic;
            return API_SUCCESS;
        }

        /* Check load balancing configuration */
        if ((ext_handle->flags & HANDLE_FLAG_LOAD_BALANCE) && load_balancing_enabled) {
            return api_load_balance_select_nic(handle, packet, selected_nic);
        }
    }

    /* Use Group 3A routing system for intelligent selection */
    if (routing_is_enabled() && packet) {
        routing_packet.data = (uint8_t *)packet;
        routing_packet.length = 60; /* Minimum Ethernet frame size for analysis */

        decision = routing_decide(&routing_packet, 0, &dest_nic);
        if (decision == ROUTE_DECISION_FORWARD && routing_validate_nic(dest_nic)) {
            *selected_nic = dest_nic;

            /* Update routing statistics for extended handle */
            if (ext_handle) {
                ext_handle->packets_routed++;
            }
            return API_SUCCESS;
        }
    }

    /* Fall back to simple round-robin */
    return api_round_robin_select_nic(selected_nic);
}

/**
 * @brief Check bandwidth limit for a handle
 * @param handle Handle ID
 * @param packet_size Size of packet to send
 * @return 0 if within limit, negative if exceeds limit
 */
int api_check_bandwidth_limit(uint16_t handle, uint32_t packet_size) {
    extended_packet_handle_t *ext_handle;
    uint32_t current_time;
    int result;

    /* Get extended handle */
    result = api_get_extended_handle(handle, &ext_handle);
    if (result != API_SUCCESS) {
        return API_SUCCESS; /* No limit for basic handles */
    }

    /* Check if bandwidth limiting is enabled */
    if (!(ext_handle->flags & HANDLE_FLAG_BANDWIDTH_LIMIT) ||
        ext_handle->bandwidth_limit == 0) {
        return API_SUCCESS; /* No limit */
    }

    /* Simple bandwidth limiting implementation */
    /* In a real implementation, this would use precise timing */
    current_time = get_system_timestamp(); /* Get actual timestamp */

    /* Reset bandwidth window every second */
    if (current_time - ext_handle->time_window_start >= 1000) {
        ext_handle->bytes_this_second = 0;
        ext_handle->time_window_start = current_time;
    }

    /* Check if adding this packet would exceed the limit */
    if (ext_handle->bytes_this_second + packet_size > ext_handle->bandwidth_limit) {
        ext_handle->bandwidth_drops++;
        return API_ERR_BANDWIDTH_EXCEEDED;
    }

    /* Update bandwidth usage */
    ext_handle->bytes_this_second += packet_size;

    return API_SUCCESS;
}

/**
 * @brief Handle NIC failure and initiate recovery
 * @param failed_nic NIC index that failed
 * @return 0 on success, negative on error
 */
int api_handle_nic_failure(uint8_t failed_nic) {
    int handles_affected = 0;
    int i;
    uint8_t alternate_nic;
    int result;

    if (!routing_validate_nic(failed_nic)) {
        return API_ERR_INVALID_PARAM;
    }

    log_error("NIC %d failed, initiating recovery", failed_nic);

    /* Update NIC error count */
    if (failed_nic < MAX_NICS) {
        nic_error_counts[failed_nic]++;
    }

    /* Find handles that need to be switched to other NICs */
    for (i = 0; i < PD_MAX_EXTENDED_HANDLES; i++) {
        if (extended_handles[i].handle_id != PD_INVALID_HANDLE) {
            /* Check if this handle was using the failed NIC */
            if (extended_handles[i].preferred_nic == failed_nic ||
                extended_handles[i].interface_num == failed_nic) {

                /* Switch to alternate NIC */
                result = api_select_optimal_nic(extended_handles[i].handle_id, NULL, &alternate_nic);
                if (result == API_SUCCESS && alternate_nic != failed_nic) {
                    extended_handles[i].interface_num = alternate_nic;
                    extended_handles[i].nic_switches++;
                    handles_affected++;

                    log_info("Switched handle %04X from NIC %d to NIC %d",
                             extended_handles[i].handle_id, failed_nic, alternate_nic);
                }
            }
        }
    }

    /* Coordinate with Group 3A routing system */
    api_coordinate_recovery_with_routing(failed_nic);

    log_info("NIC failure recovery completed, %d handles affected", handles_affected);
    return API_SUCCESS;
}

/**
 * @brief Coordinate recovery with Group 3A routing system
 * @param failed_nic Failed NIC index
 * @return 0 on success, negative on error
 */
int api_coordinate_recovery_with_routing(uint8_t failed_nic) {
    int i;

    /* Update routing system about the failure */
    if (routing_is_enabled()) {
        /* Remove routes that depend on the failed NIC */
        /* This would integrate with Group 3A routing functions */
        log_info("Coordinating with routing system for NIC %d failure", failed_nic);

        /* Update default route if it was using the failed NIC */
        if (g_routing_table.default_nic == failed_nic) {
            /* Find alternate NIC */
            for (i = 0; i < hardware_get_nic_count(); i++) {
                if (i != failed_nic && routing_validate_nic(i)) {
                    routing_set_default_route(i, g_routing_table.default_decision);
                    log_info("Updated default route to use NIC %d", i);
                    break;
                }
            }
        }
    }

    return API_SUCCESS;
}

/* Load balancing helper functions */
static int api_load_balance_select_nic(uint16_t handle, const uint8_t *packet, uint8_t *selected_nic) {
    switch (global_lb_config.mode) {
        case LB_MODE_ROUND_ROBIN:
            return api_round_robin_select_nic(selected_nic);

        case LB_MODE_WEIGHTED:
            return api_weighted_select_nic(selected_nic);

        case LB_MODE_PERFORMANCE:
            return api_performance_select_nic(selected_nic);

        case LB_MODE_APPLICATION:
            return api_application_select_nic(handle, selected_nic);

        case LB_MODE_FLOW_AWARE:
            return api_flow_aware_select_nic(handle, packet, selected_nic);

        default:
            return api_round_robin_select_nic(selected_nic);
    }
}

static int api_round_robin_select_nic(uint8_t *selected_nic) {
    int nic_count = hardware_get_nic_count();
    int i;

    if (nic_count <= 0) {
        return API_ERR_NIC_UNAVAILABLE;
    }

    /* Simple round-robin */
    last_nic_used = (last_nic_used + 1) % nic_count;

    /* Ensure selected NIC is available */
    if (routing_validate_nic(last_nic_used)) {
        *selected_nic = last_nic_used;
        return API_SUCCESS;
    }

    /* Find next available NIC */
    for (i = 0; i < nic_count; i++) {
        if (routing_validate_nic(i)) {
            *selected_nic = i;
            last_nic_used = i;
            return API_SUCCESS;
        }
    }

    return API_ERR_NIC_UNAVAILABLE;
}

static int api_weighted_select_nic(uint8_t *selected_nic) {
    uint32_t total_weight = global_lb_config.weight_primary + global_lb_config.weight_secondary;
    uint32_t selection_point = (last_nic_used * 100) % total_weight;

    if (selection_point < global_lb_config.weight_primary) {
        if (routing_validate_nic(global_lb_config.primary_nic)) {
            *selected_nic = global_lb_config.primary_nic;
            return API_SUCCESS;
        }
    }

    if (routing_validate_nic(global_lb_config.secondary_nic)) {
        *selected_nic = global_lb_config.secondary_nic;
        return API_SUCCESS;
    }

    /* Fall back to round-robin */
    return api_round_robin_select_nic(selected_nic);
}

static int api_performance_select_nic(uint8_t *selected_nic) {
    uint8_t best_nic = 0;
    uint32_t best_score = 0xFFFFFFFF; /* Lower is better */
    int i;
    uint32_t score;

    for (i = 0; i < hardware_get_nic_count(); i++) {
        if (!routing_validate_nic(i)) {
            continue;
        }

        /* Calculate performance score: utilization + error_count */
        score = nic_utilization[i] + (nic_error_counts[i] * 10);

        if (score < best_score) {
            best_score = score;
            best_nic = i;
        }
    }

    if (routing_validate_nic(best_nic)) {
        *selected_nic = best_nic;
        return API_SUCCESS;
    }

    return API_ERR_NIC_UNAVAILABLE;
}

static int api_application_select_nic(uint16_t handle, uint8_t *selected_nic) {
    extended_packet_handle_t *ext_handle;
    int result;

    /* Get extended handle */
    result = api_get_extended_handle(handle, &ext_handle);
    if (result != API_SUCCESS) {
        /* Fall back to round-robin */
        return api_round_robin_select_nic(selected_nic);
    }

    /* Use handle priority to influence NIC selection */
    if (ext_handle->priority > 192) { /* High priority */
        if (routing_validate_nic(global_lb_config.primary_nic)) {
            *selected_nic = global_lb_config.primary_nic;
            return API_SUCCESS;
        }
    } else if (ext_handle->priority < 64) { /* Low priority */
        if (routing_validate_nic(global_lb_config.secondary_nic)) {
            *selected_nic = global_lb_config.secondary_nic;
            return API_SUCCESS;
        }
    }

    /* Medium priority or fallback */
    return api_performance_select_nic(selected_nic);
}

static int api_flow_aware_select_nic(uint16_t handle, const uint8_t *packet, uint8_t *selected_nic) {
    const uint8_t *dest_mac;
    bridge_entry_t *bridge_entry;
    int result;

    /* Unused parameter - for future flow-to-handle association */
    (void)handle;

    if (!packet || !selected_nic) {
        return API_ERR_INVALID_PARAM;
    }

    /* Extract destination MAC for flow tracking */
    dest_mac = packet;

    /* Check if this flow already exists in Group 3A bridge table */
    bridge_entry = bridge_lookup_mac(dest_mac);
    if (bridge_entry && routing_validate_nic(bridge_entry->nic_index)) {
        *selected_nic = bridge_entry->nic_index;
        return API_SUCCESS;
    }

    /* For new flows, use performance-based selection */
    result = api_performance_select_nic(selected_nic);

    /* Learn this flow for future consistency */
    if (result == API_SUCCESS && routing_is_enabled()) {
        bridge_learn_mac(dest_mac, *selected_nic);
    }

    return result;
}

/**
 * @brief Update NIC utilization statistics
 * @param nic_index NIC index
 * @param packet_size Size of packet processed
 * @return 0 on success, negative on error
 */
int api_update_nic_utilization(uint8_t nic_index, uint32_t packet_size) {
    if (nic_index >= MAX_NICS) {
        return API_ERR_INVALID_PARAM;
    }

    /* Simple utilization tracking */
    /* In a real implementation, this would be more sophisticated */
    nic_utilization[nic_index] = (nic_utilization[nic_index] + packet_size) / 2;

    /* Prevent overflow */
    if (nic_utilization[nic_index] > 100) {
        nic_utilization[nic_index] = 100;
    }

    return API_SUCCESS;
}

/* Helper functions for statistics calculations */
static uint32_t calculate_average_latency(extended_packet_handle_t *ext_handle) {
    if (!ext_handle || ext_handle->packets_routed == 0) {
        return 0;
    }
    /* Simple averaging - in real implementation would track actual latency */
    return 1000; /* Default 1ms average */
}

static uint32_t calculate_jitter(extended_packet_handle_t *ext_handle) {
    if (!ext_handle) {
        return 0;
    }
    /* Simple jitter calculation - in real implementation would track variance */
    return 100; /* Default 100us jitter */
}

static uint32_t get_system_timestamp(void) {
    /* Use INT 1Ah to get system timer ticks (18.2 Hz) */
    /* Returns tick count in CX:DX format, combine to 32-bit value */
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.h.ah = 0;  /* Read system clock - function 00h */
    int86(0x1A, &regs, &regs);

    /* CX contains high 16 bits, DX contains low 16 bits */
    return ((uint32_t)regs.x.cx << 16) | regs.x.dx;
}

/* Restore default code segment */
#ifdef __WATCOMC__
#pragma code_seg ( )
#else
#pragma code_seg()
#endif
