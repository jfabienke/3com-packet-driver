/**
 * @file handle_compact.h
 * @brief Compact Handle Structure for Memory Optimization
 * 
 * Phase 4 Enhancement: Reduces handle size from 64 bytes to 16 bytes
 * Saves approximately 3KB with 64 handles (48 bytes * 64 = 3072 bytes)
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef HANDLE_COMPACT_H
#define HANDLE_COMPACT_H

#include <stdint.h>
/* DOS compatibility: avoid stdbool.h, use explicit int */
#include "common.h"

/* Handle flags packed into lower byte */
#define HANDLE_FLAG_ACTIVE      0x01
#define HANDLE_FLAG_PROMISCUOUS 0x02
#define HANDLE_FLAG_PRIORITY    0x04
#define HANDLE_FLAG_XMS_BUFFER  0x08
#define HANDLE_FLAG_MULTICAST   0x10
#define HANDLE_FLAG_ERROR       0x20
#define HANDLE_FLAG_SUSPENDED   0x40
#define HANDLE_FLAG_RESERVED    0x80

/* Handle types packed into upper nibble of interface byte */
#define HANDLE_TYPE_MASK        0xF0
#define HANDLE_TYPE_ETHERNET    0x00
#define HANDLE_TYPE_IEEE8023    0x10
#define HANDLE_TYPE_IEEE8025    0x20
#define HANDLE_TYPE_ARCNET      0x30

/* NIC index in lower nibble of interface byte */
#define HANDLE_NIC_MASK         0x0F
#define HANDLE_MAX_NICS         16

/**
 * @brief Compact handle structure - 16 bytes instead of 64
 * 
 * Memory layout optimized for alignment and access patterns:
 * - Most accessed fields (flags, callback) at start
 * - Statistics index allows unlimited stats in separate table
 * - Packet counts combined into single 32-bit field
 */
typedef struct {
    /* Byte 0-1: Flags and status */
    uint8_t flags;              /* Active, promiscuous, priority, etc. */
    uint8_t interface;          /* Upper nibble: type, Lower nibble: NIC index */
    
    /* Byte 2-3: Statistics reference */
    uint16_t stats_index;       /* Index into statistics table */
    
    /* Byte 4-7: Callback function pointer */
    void (FAR CDECL *callback)(uint8_t FAR *packet, uint16_t length);
    
    /* Byte 8-11: Combined packet counters */
    union {
        uint32_t combined_count;
        struct {
            uint16_t rx_count;  /* Lower 16 bits: RX packet count */
            uint16_t tx_count;  /* Upper 16 bits: TX packet count */
        } counts;  /* Named struct for C89 compliance */
    } packets;
    
    /* Byte 12-15: Context or buffer pointer */
    void FAR *context;        /* User context or XMS buffer pointer */
    
} handle_compact_t;

/* Ensure structure is exactly 16 bytes - DOS-compatible compile-time assert */
typedef char handle_compact_size_check[(sizeof(handle_compact_t) == 16) ? 1 : -1];

/**
 * @brief Extended statistics structure (stored separately)
 * 
 * Full statistics are stored in a separate table, indexed by stats_index.
 * This allows detailed stats without bloating the handle structure.
 */
typedef struct {
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_errors;
    uint32_t tx_errors;
    uint32_t rx_dropped;
    uint32_t tx_dropped;
    uint32_t multicast;
    uint32_t collisions;
    uint32_t rx_length_errors;
    uint32_t rx_over_errors;
    uint32_t rx_crc_errors;
    uint32_t rx_frame_errors;
    uint32_t rx_fifo_errors;
    uint32_t rx_missed_errors;
    uint32_t tx_aborted_errors;
    uint32_t tx_carrier_errors;
    uint32_t tx_fifo_errors;
    uint32_t tx_heartbeat_errors;
    uint32_t tx_window_errors;
} handle_stats_t;

/**
 * @brief Handle manager structure
 */
typedef struct {
    handle_compact_t handles[MAX_HANDLES];
    handle_stats_t *stats_table;       /* Dynamically allocated stats */
    uint16_t stats_table_size;
    uint16_t next_stats_index;
    uint32_t active_handles;
    uint32_t total_handles_created;
    uint32_t memory_saved;              /* Bytes saved vs old structure */
} handle_manager_t;

/* Function prototypes for handle management */
int handle_compact_init(void);
int handle_compact_cleanup(void);
handle_compact_t* handle_compact_allocate(uint8_t nic_index, uint8_t type);
int handle_compact_free(handle_compact_t *handle);
handle_stats_t* handle_compact_get_stats(handle_compact_t *handle);
int handle_compact_set_callback(handle_compact_t *handle, void (FAR CDECL *callback)(uint8_t FAR*, uint16_t));
int handle_compact_set_flags(handle_compact_t *handle, uint8_t flags);
uint8_t handle_compact_get_nic_index(handle_compact_t *handle);
uint8_t handle_compact_get_type(handle_compact_t *handle);
int handle_compact_is_active(handle_compact_t *handle);
void handle_compact_update_counters(handle_compact_t *handle, int is_rx, uint16_t count);
int handle_compact_migrate_from_legacy(void *legacy_handle);
void handle_compact_dump_stats(void);

/* Inline helper functions for performance */
static inline int handle_is_active(handle_compact_t *h) {
    return (h->flags & HANDLE_FLAG_ACTIVE) != 0;
}

static inline int handle_is_promiscuous(handle_compact_t *h) {
    return (h->flags & HANDLE_FLAG_PROMISCUOUS) != 0;
}

static inline uint8_t handle_get_nic(handle_compact_t *h) {
    return h->interface & HANDLE_NIC_MASK;
}

static inline uint8_t handle_get_type(handle_compact_t *h) {
    return h->interface & HANDLE_TYPE_MASK;
}

static inline void handle_increment_rx(handle_compact_t *h) {
    if (h->packets.counts.rx_count < 0xFFFF) h->packets.counts.rx_count++;
}

static inline void handle_increment_tx(handle_compact_t *h) {
    if (h->packets.counts.tx_count < 0xFFFF) h->packets.counts.tx_count++;
}

#ifdef __cplusplus
}
#endif

#endif /* HANDLE_COMPACT_H */