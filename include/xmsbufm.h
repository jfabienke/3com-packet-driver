/**
 * @file xms_buffer_migration.h
 * @brief XMS Buffer Migration System Header
 * 
 * Phase 4 Enhancement: Automatic migration of packet buffers to XMS
 * Maintains 4KB conventional cache with LRU eviction
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef XMS_BUFFER_MIGRATION_H
#define XMS_BUFFER_MIGRATION_H

#include <stdint.h>
/* DOS compatibility: avoid stdbool.h, use explicit int */
#include "common.h"
#include "xms.h"

/* Migration System Constants */
#define XMS_MIGRATION_CACHE_SIZE    4096    /* 4KB conventional cache */
#define XMS_MIGRATION_PACKET_SIZE   1518    /* Max Ethernet packet */
#define XMS_MIGRATION_CACHE_SLOTS   2       /* Cache can hold 2 packets */
#define XMS_MIGRATION_THRESHOLD     8192    /* Migrate after 8KB allocated */
#define XMS_MIGRATION_BATCH_SIZE    16      /* Migrate 16 packets at once */

/* Migration Flags */
#define XMS_MIG_FLAG_ENABLED        0x01
#define XMS_MIG_FLAG_ACTIVE         0x02
#define XMS_MIG_FLAG_CACHE_FULL     0x04
#define XMS_MIG_FLAG_XMS_AVAILABLE  0x08
#define XMS_MIG_FLAG_ERROR          0x10
#define XMS_MIG_FLAG_SUSPENDED      0x20

/* Buffer States */
#define XMS_BUFFER_STATE_FREE       0x00
#define XMS_BUFFER_STATE_CONV       0x01    /* In conventional memory */
#define XMS_BUFFER_STATE_XMS        0x02    /* In XMS memory */
#define XMS_BUFFER_STATE_MIGRATING  0x03    /* Being migrated */
#define XMS_BUFFER_STATE_CACHED     0x04    /* In cache */

/**
 * @brief XMS buffer descriptor
 */
typedef struct {
    uint32_t xms_address;           /* XMS linear address */
    uint16_t conv_segment;          /* Conventional memory segment */
    uint16_t conv_offset;           /* Conventional memory offset */
    uint16_t buffer_size;           /* Buffer size in bytes */
    uint8_t state;                  /* Buffer state */
    uint8_t flags;                  /* Buffer flags */
    uint32_t access_count;          /* Access counter for LRU */
    uint32_t last_access;           /* Last access timestamp */
    uint16_t packet_length;         /* Current packet length */
    uint16_t reserved;              /* Alignment padding */
} xms_buffer_t;

/**
 * @brief Cache slot structure
 */
typedef struct {
    uint8_t *data;                  /* Pointer to cache data */
    uint16_t buffer_index;          /* Index of buffer using this slot */
    uint32_t last_access;           /* LRU timestamp */
    bool in_use;                    /* Slot is in use */
} cache_slot_t;

/**
 * @brief XMS migration statistics
 */
typedef struct {
    uint32_t total_migrations;      /* Total buffers migrated */
    uint32_t cache_hits;            /* Cache hit count */
    uint32_t cache_misses;          /* Cache miss count */
    uint32_t xms_transfers;         /* XMS transfer count */
    uint32_t failed_migrations;     /* Failed migration count */
    uint32_t bytes_migrated;        /* Total bytes migrated */
    uint32_t bytes_cached;          /* Total bytes cached */
    uint32_t evictions;             /* Cache eviction count */
    uint32_t peak_xms_usage;        /* Peak XMS memory usage */
    uint32_t current_xms_usage;     /* Current XMS memory usage */
} xms_migration_stats_t;

/**
 * @brief XMS migration manager
 */
typedef struct {
    /* XMS Management */
    uint16_t xms_handle;            /* XMS handle for migrations */
    uint32_t xms_size;              /* Total XMS allocated */
    uint32_t xms_free_offset;       /* Next free XMS offset */
    
    /* Buffer Management */
    xms_buffer_t *buffers;          /* Buffer descriptor array */
    uint16_t buffer_count;          /* Total buffer count */
    uint16_t buffers_in_xms;        /* Buffers in XMS */
    uint16_t buffers_in_conv;       /* Buffers in conventional */
    
    /* Cache Management */
    cache_slot_t cache[XMS_MIGRATION_CACHE_SLOTS];
    uint8_t cache_data[XMS_MIGRATION_CACHE_SIZE];
    uint32_t cache_access_counter;  /* Global access counter */
    
    /* Configuration */
    uint32_t migration_threshold;   /* Bytes before migration */
    uint16_t batch_size;            /* Packets per migration */
    uint8_t flags;                  /* System flags */
    uint8_t reserved;               /* Alignment */
    
    /* Statistics */
    xms_migration_stats_t stats;
    
    /* Function Pointers */
    int (*xms_copy)(uint32_t src, uint32_t dst, uint32_t size);
    void (*notify_callback)(uint16_t buffer_index, uint8_t new_state);
    
} xms_migration_manager_t;

/* Function Prototypes - Initialization */
int xms_migration_init(uint32_t xms_size, uint32_t threshold);
int xms_migration_cleanup(void);
int xms_migration_enable(void);
int xms_migration_disable(void);

/* Function Prototypes - Buffer Management */
int xms_migration_allocate_buffer(uint16_t size, uint16_t *buffer_index);
int xms_migration_free_buffer(uint16_t buffer_index);
int xms_migration_migrate_buffer(uint16_t buffer_index);
int xms_migration_migrate_batch(void);

/* Function Prototypes - Cache Management */
void* xms_migration_get_buffer(uint16_t buffer_index);
int xms_migration_cache_buffer(uint16_t buffer_index);
int xms_migration_evict_lru(void);
void xms_migration_update_lru(uint16_t buffer_index);

/* Function Prototypes - Data Access */
int xms_migration_read_packet(uint16_t buffer_index, void *dest, uint16_t length);
int xms_migration_write_packet(uint16_t buffer_index, const void *src, uint16_t length);
int xms_migration_copy_packet(uint16_t src_index, uint16_t dst_index);

/* Function Prototypes - Statistics */
void xms_migration_get_stats(xms_migration_stats_t *stats);
void xms_migration_reset_stats(void);
void xms_migration_dump_stats(void);

/* Function Prototypes - Configuration */
int xms_migration_set_threshold(uint32_t threshold);
int xms_migration_set_batch_size(uint16_t batch_size);
int xms_migration_set_callback(void (*callback)(uint16_t, uint8_t));

/* Function Prototypes - Utilities */
int xms_migration_is_enabled(void);
int xms_migration_is_buffer_in_xms(uint16_t buffer_index);
uint32_t xms_migration_get_free_xms(void);
uint16_t xms_migration_get_buffer_count(void);

/* Inline Helper Functions */
static inline int xms_migration_should_migrate(uint32_t conv_usage) {
    extern xms_migration_manager_t g_xms_manager;
    return (conv_usage >= g_xms_manager.migration_threshold) &&
           (g_xms_manager.flags & XMS_MIG_FLAG_ENABLED) &&
           (g_xms_manager.flags & XMS_MIG_FLAG_XMS_AVAILABLE);
}

static inline int xms_migration_is_cached(uint16_t buffer_index) {
    extern xms_migration_manager_t g_xms_manager;
    for (int i = 0; i < XMS_MIGRATION_CACHE_SLOTS; i++) {
        if (g_xms_manager.cache[i].in_use && 
            g_xms_manager.cache[i].buffer_index == buffer_index) {
            return 1;
        }
    }
    return 0;
}

#endif /* XMS_BUFFER_MIGRATION_H */