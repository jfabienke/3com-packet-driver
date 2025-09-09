/**
 * @file handle_manager.c
 * @brief Application Interface Layer with Handle Management - Agent 12 Implementation
 *
 * 3Com Packet Driver - Application Interface Layer
 * Implements comprehensive handle management and multiplexing for applications
 * with support for multiple concurrent applications and packet type filtering.
 * 
 * Features:
 * - Handle allocation and deallocation
 * - Packet type filtering and multiplexing  
 * - Application callback management
 * - Handle-based statistics tracking
 * - Priority-based packet delivery
 * - Multi-application coordination
 * 
 * Agent 12: Driver API
 * Week 1 Deliverable - Application interface layer
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "unified_api.h" 
#include "metrics_core.h"
#include "../include/logging.h"
#include "../include/packet_ops.h"
#include "../loader/app_callback.h"
#include "../../docs/agents/shared/error-codes.h"

/* Handle Manager Constants */
#define MAX_APPLICATION_HANDLES     64      /* Maximum application handles */
#define MAX_PACKET_TYPES_PER_HANDLE 8       /* Maximum packet types per handle */
#define HANDLE_SIGNATURE           "HNDL"   /* Handle signature */
#define INVALID_HANDLE_ID          0x0000   /* Invalid handle identifier */
#define HANDLE_BITMAP_SIZE         ((MAX_APPLICATION_HANDLES + 7) / 8)

/* Handle Priority Levels */
#define HANDLE_PRIORITY_BACKGROUND  0       /* Background/batch processing */
#define HANDLE_PRIORITY_NORMAL      64      /* Normal priority */
#define HANDLE_PRIORITY_HIGH        128     /* High priority */ 
#define HANDLE_PRIORITY_REALTIME    192     /* Real-time priority */
#define HANDLE_PRIORITY_SYSTEM      255     /* System priority */

/* Handle State */
typedef enum {
    HANDLE_STATE_FREE = 0,
    HANDLE_STATE_ALLOCATED,
    HANDLE_STATE_ACTIVE,
    HANDLE_STATE_SUSPENDED,
    HANDLE_STATE_ERROR
} handle_state_t;

/* Packet Type Filter */
typedef struct {
    uint16_t packet_type;               /* Ethernet type (0 = accept all) */
    uint8_t active;                     /* Filter is active */
    uint8_t reserved;                   /* Alignment padding */
    uint32_t packets_matched;           /* Packets matched by this filter */
} packet_filter_t;

/* Application Handle Structure */
typedef struct {
    char signature[4];                  /* Handle signature "HNDL" */
    uint16_t handle_id;                 /* Unique handle identifier */
    handle_state_t state;               /* Handle state */
    uint8_t priority;                   /* Handle priority */
    uint8_t flags;                      /* Handle flags */
    
    /* Application Information */
    uint16_t application_id;            /* Application identifier */
    char application_name[16];          /* Application name */
    void far *receiver_callback;        /* Packet receiver callback */
    void far *error_callback;           /* Error callback */
    
    /* Packet Filtering */
    uint8_t filter_count;               /* Number of active filters */
    packet_filter_t filters[MAX_PACKET_TYPES_PER_HANDLE];
    
    /* Interface Binding */
    uint8_t interface_number;           /* Bound interface number */
    uint8_t receive_mode;               /* Receive mode flags */
    
    /* Statistics */
    uint32_t packets_received;          /* Packets delivered to application */
    uint32_t packets_sent;              /* Packets sent by application */
    uint32_t bytes_received;            /* Bytes delivered to application */
    uint32_t bytes_sent;                /* Bytes sent by application */
    uint32_t packets_dropped;           /* Packets dropped due to errors */
    uint32_t callback_errors;           /* Callback invocation errors */
    
    /* Performance Metrics */
    uint32_t allocation_time;           /* Time when handle was allocated */
    uint32_t last_activity_time;        /* Last packet activity */
    uint32_t total_callback_time;       /* Total time spent in callbacks */
    uint32_t max_callback_time;         /* Maximum callback execution time */
    uint32_t callback_count;            /* Number of callback invocations */
    
    /* Multi-Module Coordination */
    uint8_t preferred_module;           /* Preferred dispatch module */
    uint32_t module_routing_mask;       /* Module routing preferences */
    
} application_handle_t;

/* Handle Manager State */
typedef struct {
    char signature[4];                  /* Manager signature */
    uint16_t version;                   /* Manager version */
    uint16_t max_handles;               /* Maximum handles supported */
    uint16_t allocated_handles;         /* Currently allocated handles */
    uint16_t active_handles;            /* Currently active handles */
    uint16_t peak_handles;              /* Peak concurrent handles */
    
    /* Handle Allocation Bitmap */
    uint8_t handle_bitmap[HANDLE_BITMAP_SIZE];
    uint16_t next_handle_id;            /* Next available handle ID */
    
    /* Handle Array */
    application_handle_t handles[MAX_APPLICATION_HANDLES];
    
    /* Global Statistics */
    uint32_t total_allocations;         /* Total handle allocations */
    uint32_t total_deallocations;       /* Total handle deallocations */
    uint32_t total_packets_delivered;   /* Total packets delivered */
    uint32_t total_delivery_errors;     /* Total delivery errors */
    
} handle_manager_t;

/* Global Handle Manager */
static handle_manager_t g_handle_manager;
static bool handle_manager_initialized = false;

/* Forward Declarations */
static uint16_t allocate_handle_id(void);
static void free_handle_id(uint16_t handle_id);
static int validate_handle_id(uint16_t handle_id);
static application_handle_t *get_handle_by_id(uint16_t handle_id);
static int should_deliver_to_handle(const application_handle_t *handle, 
                                   uint16_t packet_type, const uint8_t *packet);
static int invoke_application_callback(application_handle_t *handle, 
                                      const uint8_t *packet, uint16_t length);
static void update_handle_statistics(application_handle_t *handle, 
                                    uint32_t bytes, bool success);

/**
 * @brief Initialize Handle Manager
 * @return SUCCESS on success, error code on failure
 */
int handle_manager_init(void) {
    if (handle_manager_initialized) {
        return SUCCESS;
    }
    
    log_info("Initializing Application Handle Manager");
    
    /* Initialize handle manager structure */
    memset(&g_handle_manager, 0, sizeof(handle_manager_t));
    strncpy(g_handle_manager.signature, "HMGR", 4);
    g_handle_manager.version = 0x0100;
    g_handle_manager.max_handles = MAX_APPLICATION_HANDLES;
    g_handle_manager.next_handle_id = 1; /* Start from 1, 0 is invalid */
    
    /* Initialize all handles */
    for (int i = 0; i < MAX_APPLICATION_HANDLES; i++) {
        application_handle_t *handle = &g_handle_manager.handles[i];
        strncpy(handle->signature, HANDLE_SIGNATURE, 4);
        handle->handle_id = INVALID_HANDLE_ID;
        handle->state = HANDLE_STATE_FREE;
        handle->priority = HANDLE_PRIORITY_NORMAL;
    }
    
    handle_manager_initialized = true;
    log_info("Handle Manager initialized (max handles: %d)", MAX_APPLICATION_HANDLES);
    
    return SUCCESS;
}

/**
 * @brief Cleanup Handle Manager
 * @return SUCCESS on success, error code on failure
 */
int handle_manager_cleanup(void) {
    if (!handle_manager_initialized) {
        return SUCCESS;
    }
    
    log_info("Cleaning up Application Handle Manager");
    
    /* Free all allocated handles */
    for (int i = 0; i < MAX_APPLICATION_HANDLES; i++) {
        application_handle_t *handle = &g_handle_manager.handles[i];
        if (handle->state != HANDLE_STATE_FREE) {
            handle_manager_free_handle(handle->handle_id);
        }
    }
    
    /* Log final statistics */
    log_info("Handle Manager Statistics:");
    log_info("  Total allocations: %lu", g_handle_manager.total_allocations);
    log_info("  Total deallocations: %lu", g_handle_manager.total_deallocations);
    log_info("  Total packets delivered: %lu", g_handle_manager.total_packets_delivered);
    log_info("  Peak concurrent handles: %d", g_handle_manager.peak_handles);
    
    handle_manager_initialized = false;
    log_info("Handle Manager cleanup completed");
    
    return SUCCESS;
}

/**
 * @brief Allocate a new application handle
 * @param packet_type Ethernet packet type (0 = accept all)
 * @param interface_num Interface number to bind to
 * @param receiver_callback Application receiver callback
 * @param application_name Application name (optional)
 * @return Handle ID on success, 0 on failure
 */
uint16_t handle_manager_allocate_handle(uint16_t packet_type, uint8_t interface_num,
                                       void far *receiver_callback, const char *application_name) {
    
    if (!handle_manager_initialized) {
        log_error("Handle Manager not initialized");
        return INVALID_HANDLE_ID;
    }
    
    if (!receiver_callback) {
        log_error("Invalid receiver callback");
        return INVALID_HANDLE_ID;
    }
    
    /* Find free handle slot */
    int handle_slot = -1;
    for (int i = 0; i < MAX_APPLICATION_HANDLES; i++) {
        if (g_handle_manager.handles[i].state == HANDLE_STATE_FREE) {
            handle_slot = i;
            break;
        }
    }
    
    if (handle_slot < 0) {
        log_error("No free handle slots available");
        return INVALID_HANDLE_ID;
    }
    
    /* Allocate unique handle ID */
    uint16_t handle_id = allocate_handle_id();
    if (handle_id == INVALID_HANDLE_ID) {
        log_error("Failed to allocate handle ID");
        return INVALID_HANDLE_ID;
    }
    
    application_handle_t *handle = &g_handle_manager.handles[handle_slot];
    
    /* Initialize handle */
    handle->handle_id = handle_id;
    handle->state = HANDLE_STATE_ALLOCATED;
    handle->priority = HANDLE_PRIORITY_NORMAL;
    handle->flags = 0;
    
    /* Set application information */
    handle->application_id = (uint16_t)(handle_id & 0x7FFF); /* Derive from handle ID */
    if (application_name) {
        strncpy(handle->application_name, application_name, sizeof(handle->application_name) - 1);
        handle->application_name[sizeof(handle->application_name) - 1] = '\0';
    } else {
        snprintf(handle->application_name, sizeof(handle->application_name), "APP_%04X", handle_id);
    }
    
    handle->receiver_callback = receiver_callback;
    handle->error_callback = NULL;
    
    /* Set packet filtering */
    handle->filter_count = 1;
    handle->filters[0].packet_type = packet_type;
    handle->filters[0].active = 1;
    handle->filters[0].packets_matched = 0;
    
    /* Initialize remaining filters as inactive */
    for (int i = 1; i < MAX_PACKET_TYPES_PER_HANDLE; i++) {
        handle->filters[i].active = 0;
        handle->filters[i].packet_type = 0;
        handle->filters[i].packets_matched = 0;
    }
    
    /* Set interface binding */
    handle->interface_number = interface_num;
    handle->receive_mode = 0; /* Normal receive mode */
    
    /* Clear statistics */
    memset(&handle->packets_received, 0, 
           sizeof(application_handle_t) - offsetof(application_handle_t, packets_received));
    
    /* Set timing information */
    handle->allocation_time = get_system_time();
    handle->last_activity_time = handle->allocation_time;
    
    /* Set module preferences */
    handle->preferred_module = 0xFF; /* No preference */
    handle->module_routing_mask = 0xFFFFFFFF; /* Allow all modules */
    
    /* Activate handle */
    handle->state = HANDLE_STATE_ACTIVE;
    
    /* Update manager statistics */
    g_handle_manager.allocated_handles++;
    g_handle_manager.active_handles++;
    g_handle_manager.total_allocations++;
    
    if (g_handle_manager.active_handles > g_handle_manager.peak_handles) {
        g_handle_manager.peak_handles = g_handle_manager.active_handles;
    }
    
    /* Update metrics core - assume module 0 for unified API handles */
    metrics_handle_opened(0);
    
    log_info("Allocated handle %04X for %s (type=%04X, interface=%d)", 
             handle_id, handle->application_name, packet_type, interface_num);
    
    return handle_id;
}

/**
 * @brief Free an application handle
 * @param handle_id Handle ID to free
 * @return SUCCESS on success, error code on failure
 */
int handle_manager_free_handle(uint16_t handle_id) {
    if (!handle_manager_initialized) {
        return ERROR_INVALID_STATE;
    }
    
    application_handle_t *handle = get_handle_by_id(handle_id);
    if (!handle || handle->state == HANDLE_STATE_FREE) {
        log_error("Invalid or already freed handle %04X", handle_id);
        return ERROR_PKTDRV_HANDLE;
    }
    
    log_info("Freeing handle %04X for %s (rx=%lu, tx=%lu, drops=%lu)",
             handle_id, handle->application_name,
             handle->packets_received, handle->packets_sent, handle->packets_dropped);
    
    /* Clear handle data */
    handle->state = HANDLE_STATE_FREE;
    free_handle_id(handle_id);
    
    /* Update statistics */
    g_handle_manager.allocated_handles--;
    if (handle->state == HANDLE_STATE_ACTIVE) {
        g_handle_manager.active_handles--;
    }
    g_handle_manager.total_deallocations++;
    
    /* Update metrics core - assume module 0 for unified API handles */
    metrics_handle_closed(0);
    
    /* Clear handle structure */
    memset(handle, 0, sizeof(application_handle_t));
    strncpy(handle->signature, HANDLE_SIGNATURE, 4);
    handle->handle_id = INVALID_HANDLE_ID;
    handle->state = HANDLE_STATE_FREE;
    
    return SUCCESS;
}

/**
 * @brief Add packet type filter to handle
 * @param handle_id Handle ID
 * @param packet_type Ethernet packet type to filter
 * @return SUCCESS on success, error code on failure
 */
int handle_manager_add_packet_filter(uint16_t handle_id, uint16_t packet_type) {
    application_handle_t *handle = get_handle_by_id(handle_id);
    if (!handle || handle->state != HANDLE_STATE_ACTIVE) {
        return ERROR_PKTDRV_HANDLE;
    }
    
    /* Check if filter already exists */
    for (int i = 0; i < handle->filter_count; i++) {
        if (handle->filters[i].packet_type == packet_type) {
            return ERROR_ALREADY_EXISTS;
        }
    }
    
    /* Find free filter slot */
    if (handle->filter_count >= MAX_PACKET_TYPES_PER_HANDLE) {
        return ERROR_BUFFER_TOO_SMALL;
    }
    
    /* Add new filter */
    handle->filters[handle->filter_count].packet_type = packet_type;
    handle->filters[handle->filter_count].active = 1;
    handle->filters[handle->filter_count].packets_matched = 0;
    handle->filter_count++;
    
    log_debug("Added packet filter %04X to handle %04X", packet_type, handle_id);
    
    return SUCCESS;
}

/**
 * @brief Process received packet and deliver to matching handles
 * @param packet Packet data
 * @param length Packet length
 * @param interface_num Source interface number
 * @return Number of handles packet was delivered to
 */
int handle_manager_deliver_packet(const uint8_t *packet, uint16_t length, uint8_t interface_num) {
    uint16_t packet_type;
    int deliveries = 0;
    
    if (!handle_manager_initialized || !packet || length < 14) {
        return 0;
    }
    
    /* Extract Ethernet type from packet header */
    packet_type = (packet[12] << 8) | packet[13];
    
    log_debug("Delivering packet: len=%d, type=%04X, interface=%d", 
              length, packet_type, interface_num);
    
    /* Deliver to all matching handles */
    for (int i = 0; i < MAX_APPLICATION_HANDLES; i++) {
        application_handle_t *handle = &g_handle_manager.handles[i];
        
        if (handle->state == HANDLE_STATE_ACTIVE) {
            /* Check interface binding */
            if (handle->interface_number != interface_num && handle->interface_number != 0xFF) {
                continue; /* Handle not bound to this interface */
            }
            
            /* Check packet type filtering */
            if (should_deliver_to_handle(handle, packet_type, packet)) {
                /* Invoke application callback */
                if (invoke_application_callback(handle, packet, length) == SUCCESS) {
                    deliveries++;
                    handle->packets_received++;
                    handle->bytes_received += length;
                    
                    /* Update filter statistics */
                    for (int f = 0; f < handle->filter_count; f++) {
                        if (handle->filters[f].active &&
                            (handle->filters[f].packet_type == 0 || 
                             handle->filters[f].packet_type == packet_type)) {
                            handle->filters[f].packets_matched++;
                            break;
                        }
                    }
                } else {
                    handle->packets_dropped++;
                    handle->callback_errors++;
                }
                
                handle->last_activity_time = get_system_time();
            }
        }
    }
    
    g_handle_manager.total_packets_delivered += deliveries;
    if (deliveries == 0) {
        log_debug("No handles matched packet type %04X", packet_type);
    }
    
    return deliveries;
}

/**
 * @brief Get handle statistics
 * @param handle_id Handle ID
 * @param stats Statistics structure to fill
 * @return SUCCESS on success, error code on failure
 */
int handle_manager_get_handle_statistics(uint16_t handle_id, void *stats) {
    application_handle_t *handle = get_handle_by_id(handle_id);
    if (!handle || handle->state != HANDLE_STATE_ACTIVE || !stats) {
        return ERROR_PKTDRV_HANDLE;
    }
    
    /* Fill standard packet driver statistics structure */
    pd_statistics_t *pd_stats = (pd_statistics_t *)stats;
    
    pd_stats->packets_in = handle->packets_received;
    pd_stats->packets_out = handle->packets_sent;
    pd_stats->bytes_in = handle->bytes_received;
    pd_stats->bytes_out = handle->bytes_sent;
    pd_stats->errors_in = handle->callback_errors;
    pd_stats->errors_out = 0; /* No TX errors tracked per handle */
    pd_stats->packets_lost = handle->packets_dropped;
    
    return SUCCESS;
}

/**
 * @brief Set handle priority
 * @param handle_id Handle ID
 * @param priority Priority level
 * @return SUCCESS on success, error code on failure
 */
int handle_manager_set_handle_priority(uint16_t handle_id, uint8_t priority) {
    application_handle_t *handle = get_handle_by_id(handle_id);
    if (!handle || handle->state != HANDLE_STATE_ACTIVE) {
        return ERROR_PKTDRV_HANDLE;
    }
    
    handle->priority = priority;
    log_debug("Set priority %d for handle %04X", priority, handle_id);
    
    return SUCCESS;
}

/* Internal Helper Functions */

static uint16_t allocate_handle_id(void) {
    /* Simple linear search for next available ID */
    uint16_t start_id = g_handle_manager.next_handle_id;
    
    for (int i = 0; i < 65535; i++) {
        uint16_t candidate_id = start_id + i;
        if (candidate_id == INVALID_HANDLE_ID) {
            continue; /* Skip invalid handle ID */
        }
        
        /* Check if ID is already in use */
        bool in_use = false;
        for (int j = 0; j < MAX_APPLICATION_HANDLES; j++) {
            if (g_handle_manager.handles[j].handle_id == candidate_id) {
                in_use = true;
                break;
            }
        }
        
        if (!in_use) {
            g_handle_manager.next_handle_id = candidate_id + 1;
            return candidate_id;
        }
    }
    
    return INVALID_HANDLE_ID; /* No available IDs */
}

static void free_handle_id(uint16_t handle_id) {
    /* Handle ID is implicitly freed when handle is marked as FREE */
    /* No additional bookkeeping needed for simple linear allocation */
}

static int validate_handle_id(uint16_t handle_id) {
    return (handle_id != INVALID_HANDLE_ID);
}

static application_handle_t *get_handle_by_id(uint16_t handle_id) {
    if (!validate_handle_id(handle_id)) {
        return NULL;
    }
    
    for (int i = 0; i < MAX_APPLICATION_HANDLES; i++) {
        if (g_handle_manager.handles[i].handle_id == handle_id) {
            return &g_handle_manager.handles[i];
        }
    }
    
    return NULL;
}

static int should_deliver_to_handle(const application_handle_t *handle, 
                                   uint16_t packet_type, const uint8_t *packet) {
    
    /* Check packet type filters */
    for (int i = 0; i < handle->filter_count; i++) {
        if (handle->filters[i].active) {
            if (handle->filters[i].packet_type == 0 ||  /* Accept all */
                handle->filters[i].packet_type == packet_type) {
                return 1; /* Match found */
            }
        }
    }
    
    return 0; /* No match */
}

static int invoke_application_callback(application_handle_t *handle, 
                                      const uint8_t *packet, uint16_t length) {
    uint32_t start_time, callback_time;
    
    if (!handle->receiver_callback) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Record callback start time */
    start_time = get_system_time();
    
    /* Invoke application callback with proper calling convention */
    if (handle_entry->receiver.entry) {
        result = callback_deliver_packet(&handle_entry->receiver,
                                       (void far *)packet_data,
                                       packet_length,
                                       link_type,
                                       handle);
        if (result != CB_SUCCESS) {
            log_error("Callback failed for handle %04X: %d", handle, result);
            return ERROR_CALLBACK_FAILED;
        }
    } else {
        log_warning("No receiver callback registered for handle %04X", handle);
        return ERROR_NO_CALLBACK;
    }
    
    /* Calculate callback execution time */
    callback_time = get_system_time() - start_time;
    
    /* Update callback metrics */
    handle->total_callback_time += callback_time;
    if (callback_time > handle->max_callback_time) {
        handle->max_callback_time = callback_time;
    }
    handle->callback_count++;
    
    return SUCCESS;
}

static void update_handle_statistics(application_handle_t *handle, 
                                    uint32_t bytes, bool success) {
    if (success) {
        handle->packets_received++;
        handle->bytes_received += bytes;
    } else {
        handle->packets_dropped++;
    }
    
    handle->last_activity_time = get_system_time();
}

/* External system time function */
extern uint32_t get_system_time(void);