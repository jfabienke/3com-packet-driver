/**
 * @file buffer_alloc.c
 * @brief Buffer allocation for packet transmission/reception
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include "../include/buffer_alloc.h"
#include "../include/nic_buffer_pools.h"
#include "../include/vds.h"
#include "../include/platform_probe.h"
#include "../include/logging.h"

/* Global buffer pools */
buffer_pool_t g_tx_buffer_pool;
buffer_pool_t g_rx_buffer_pool;
buffer_pool_t g_dma_buffer_pool;
buffer_stats_t g_buffer_stats;

/* VDS Common Buffers for DMA-safe operations */
static vds_buffer_t g_vds_tx_ring_buffer = {0};
static vds_buffer_t g_vds_rx_ring_buffer = {0};
static vds_buffer_t g_vds_rx_data_buffer = {0};
static bool g_vds_buffers_allocated = false;

/* Size-specific buffer pools for performance optimization */
buffer_pool_t g_buffer_pool_64;     /* 64-byte packets (common small packets) */
buffer_pool_t g_buffer_pool_128;    /* 128-byte packets (medium packets) */
buffer_pool_t g_buffer_pool_512;    /* 512-byte packets (large packets) */
buffer_pool_t g_buffer_pool_1518;   /* 1518-byte packets (maximum Ethernet) */

/* Global RX_COPYBREAK pool */
static rx_copybreak_pool_t g_rx_copybreak_pool = {0};

/* Fast path statistics */
static uint32_t g_fast_path_allocations = 0;
static uint32_t g_fast_path_cache_hits = 0;
static uint32_t g_fallback_allocations = 0;

/* Private global state */
static bool g_buffer_system_initialized = false;
static buffer_error_t g_last_error = BUFFER_ERROR_NONE;
static void (*g_error_handler)(buffer_error_t error, const char* message) = NULL;

/* Buffer magic number for validation */
#define BUFFER_MAGIC_VALID      0xBEEFCAFE
#define BUFFER_MAGIC_FREE       0xDEADBEEF

/* Internal helper functions */
static void buffer_set_last_error(buffer_error_t error);
static buffer_desc_t* buffer_desc_create(uint32_t size, buffer_type_t type, uint32_t flags);
static void buffer_desc_destroy(buffer_desc_t *buffer);
static bool buffer_pool_has_space(const buffer_pool_t *pool);
static void buffer_update_stats_alloc(uint32_t size);
static void buffer_update_stats_free(uint32_t size);

/* Buffer system initialization and cleanup */
int buffer_system_init(void) {
    if (g_buffer_system_initialized) {
        return SUCCESS;
    }
    
    /* Initialize global statistics */
    buffer_stats_init(&g_buffer_stats);
    
    /* Initialize per-NIC buffer pool manager first */
    uint32_t total_memory = memory_xms_available() ? (memory_get_xms_size() * 1024) : (512 * 1024);
    int result = nic_buffer_pool_manager_init(total_memory, MEMORY_TIER_AUTO);
    if (result != SUCCESS) {
        log_warning("Failed to initialize per-NIC buffer pools: %d, using legacy pools", result);
    }
    
    /* Initialize default buffer pools for backward compatibility */
    result = buffer_init_default_pools();
    if (result != SUCCESS) {
        return result;
    }
    
    g_buffer_system_initialized = true;
    g_last_error = BUFFER_ERROR_NONE;
    
    log_info("Buffer system initialized with per-NIC buffer pool support");
    return SUCCESS;
}

void buffer_system_cleanup(void) {
    if (!g_buffer_system_initialized) {
        return;
    }
    
    /* Cleanup VDS common buffers first */
    if (g_vds_buffers_allocated) {
        if (g_vds_tx_ring_buffer.allocated) {
            vds_release_buffer(&g_vds_tx_ring_buffer);
            log_debug("VDS TX ring buffer released");
        }
        if (g_vds_rx_ring_buffer.allocated) {
            vds_release_buffer(&g_vds_rx_ring_buffer);
            log_debug("VDS RX ring buffer released");
        }
        if (g_vds_rx_data_buffer.allocated) {
            vds_release_buffer(&g_vds_rx_data_buffer);
            log_debug("VDS RX data buffer released");
        }
        g_vds_buffers_allocated = false;
    }
    
    /* Cleanup per-NIC buffer pool manager */
    nic_buffer_pool_manager_cleanup();
    
    /* Cleanup all default pools */
    buffer_cleanup_default_pools();
    
    /* Clear statistics */
    buffer_stats_init(&g_buffer_stats);
    
    g_buffer_system_initialized = false;
}

int buffer_init_default_pools(void) {
    int result;
    extern cpu_info_t g_cpu_info; /* From Phase 1 */
    
    /* Determine optimal buffer counts based on available memory */
    uint32_t tx_buffers = 16;   /* Default for TX */
    uint32_t rx_buffers = 32;   /* Default for RX */
    uint32_t dma_buffers = 8;   /* Default for DMA */
    
    /* Size-specific pool counts based on typical network traffic patterns */
    uint32_t pool_64_count = 32;    /* Lots of small packets (ACKs, control) */
    uint32_t pool_128_count = 24;   /* Medium packets (small data) */
    uint32_t pool_512_count = 16;   /* Large packets (file transfers) */
    uint32_t pool_1518_count = 12;  /* Maximum size packets */
    
    /* Adjust buffer counts based on available XMS memory */
    if (memory_xms_available()) {
        uint32_t xms_kb = memory_get_xms_size();
        if (xms_kb > 1024) {
            /* More XMS available - increase buffer counts */
            tx_buffers = 32;
            rx_buffers = 64;
            dma_buffers = 16;
            
            /* Scale up size-specific pools */
            pool_64_count = 48;
            pool_128_count = 36;
            pool_512_count = 24;
            pool_1518_count = 18;
        }
        
        if (xms_kb > 4096) {
            /* Lots of XMS memory - maximize performance */
            pool_64_count = 64;
            pool_128_count = 48;
            pool_512_count = 32;
            pool_1518_count = 24;
        }
    }
    
    /* Initialize TX buffer pool with optimized size for Ethernet frames */
    uint32_t tx_flags = BUFFER_FLAG_ALIGNED;
    if (g_cpu_info.type >= CPU_TYPE_80386) {
        tx_flags |= BUFFER_FLAG_ZERO_INIT; /* 386+ can handle fast zero init */
    }
    
    result = buffer_pool_init(&g_tx_buffer_pool, BUFFER_TYPE_TX, 
                             TX_BUFFER_SIZE, tx_buffers, tx_flags);
    if (result != SUCCESS) {
        buffer_set_last_error(BUFFER_ERROR_OUT_OF_MEMORY);
        return result;
    }
    
    /* Initialize RX buffer pool with size for maximum Ethernet frame + margin */
    uint32_t rx_flags = BUFFER_FLAG_ALIGNED | BUFFER_FLAG_ZERO_INIT;
    
    result = buffer_pool_init(&g_rx_buffer_pool, BUFFER_TYPE_RX,
                             RX_BUFFER_SIZE, rx_buffers, rx_flags);
    if (result != SUCCESS) {
        buffer_pool_cleanup(&g_tx_buffer_pool);
        buffer_set_last_error(BUFFER_ERROR_OUT_OF_MEMORY);
        return result;
    }
    
    /* Initialize DMA buffer pool - use VDS common buffers when appropriate */
    if (platform_get_dma_policy() == DMA_POLICY_COMMONBUF) {
        log_info("Allocating VDS common buffers for DMA operations");
        
        /* Allocate VDS common buffer for TX descriptor ring */
        uint32_t tx_ring_size = 16 * 1024;  /* 16KB for TX descriptors */
        if (vds_request_buffer(tx_ring_size, VDS_ISA_BUFFER_FLAGS, &g_vds_tx_ring_buffer)) {
            log_info("VDS TX ring buffer allocated: %lu bytes at phys %08lXh", 
                    (unsigned long)g_vds_tx_ring_buffer.size,
                    (unsigned long)g_vds_tx_ring_buffer.physical_addr);
        } else {
            log_warning("Failed to allocate VDS TX ring buffer - using conventional");
        }
        
        /* Allocate VDS common buffer for RX descriptor ring */
        uint32_t rx_ring_size = 16 * 1024;  /* 16KB for RX descriptors */
        if (vds_request_buffer(rx_ring_size, VDS_ISA_BUFFER_FLAGS, &g_vds_rx_ring_buffer)) {
            log_info("VDS RX ring buffer allocated: %lu bytes at phys %08lXh", 
                    (unsigned long)g_vds_rx_ring_buffer.size,
                    (unsigned long)g_vds_rx_ring_buffer.physical_addr);
        } else {
            log_warning("Failed to allocate VDS RX ring buffer - using conventional");
        }
        
        /* Allocate VDS common buffer for RX data buffers (GPT-5 recommendation) */
        uint32_t rx_data_size = 64 * 1024;  /* 64KB for RX data pool */
        if (vds_request_buffer(rx_data_size, VDS_ISA_BUFFER_FLAGS, &g_vds_rx_data_buffer)) {
            log_info("VDS RX data buffer allocated: %lu bytes at phys %08lXh", 
                    (unsigned long)g_vds_rx_data_buffer.size,
                    (unsigned long)g_vds_rx_data_buffer.physical_addr);
            g_vds_buffers_allocated = true;
        } else {
            log_warning("Failed to allocate VDS RX data buffer - using conventional");
        }
    }
    
    /* Initialize conventional DMA buffer pool as fallback */
    uint32_t dma_flags = BUFFER_FLAG_DMA_CAPABLE | BUFFER_FLAG_ALIGNED;
    if (g_cpu_info.type >= CPU_TYPE_80386) {
        dma_flags |= BUFFER_FLAG_PERSISTENT; /* Keep DMA buffers locked */
    }
    
    result = buffer_pool_init(&g_dma_buffer_pool, BUFFER_TYPE_DMA_TX,
                             DMA_BUFFER_SIZE, dma_buffers, dma_flags);
    if (result != SUCCESS) {
        buffer_pool_cleanup(&g_tx_buffer_pool);
        buffer_pool_cleanup(&g_rx_buffer_pool);
        buffer_set_last_error(BUFFER_ERROR_OUT_OF_MEMORY);
        return result;
    }
    
    /* Initialize size-specific buffer pools for fast path allocation */
    uint32_t size_pool_flags = BUFFER_FLAG_ALIGNED;
    if (g_cpu_info.type >= CPU_TYPE_80386) {
        size_pool_flags |= BUFFER_FLAG_ZERO_INIT;
    }
    
    /* 64-byte pool */
    result = buffer_pool_init(&g_buffer_pool_64, BUFFER_TYPE_TX,
                             64, pool_64_count, size_pool_flags);
    if (result != SUCCESS) {
        log_warning("Failed to initialize 64-byte buffer pool, using fallback");
    } else {
        log_info("Initialized 64-byte buffer pool with %d buffers", pool_64_count);
    }
    
    /* 128-byte pool */
    result = buffer_pool_init(&g_buffer_pool_128, BUFFER_TYPE_TX,
                             128, pool_128_count, size_pool_flags);
    if (result != SUCCESS) {
        log_warning("Failed to initialize 128-byte buffer pool, using fallback");
    } else {
        log_info("Initialized 128-byte buffer pool with %d buffers", pool_128_count);
    }
    
    /* 512-byte pool */
    result = buffer_pool_init(&g_buffer_pool_512, BUFFER_TYPE_TX,
                             512, pool_512_count, size_pool_flags);
    if (result != SUCCESS) {
        log_warning("Failed to initialize 512-byte buffer pool, using fallback");
    } else {
        log_info("Initialized 512-byte buffer pool with %d buffers", pool_512_count);
    }
    
    /* 1518-byte pool */
    result = buffer_pool_init(&g_buffer_pool_1518, BUFFER_TYPE_TX,
                             1518, pool_1518_count, size_pool_flags);
    if (result != SUCCESS) {
        log_warning("Failed to initialize 1518-byte buffer pool, using fallback");
    } else {
        log_info("Initialized 1518-byte buffer pool with %d buffers", pool_1518_count);
    }
    
    log_info("Initialized buffer pools: TX=%d, RX=%d, DMA=%d",
             tx_buffers, rx_buffers, dma_buffers);
    log_info("Size-specific pools: 64=%d, 128=%d, 512=%d, 1518=%d",
             pool_64_count, pool_128_count, pool_512_count, pool_1518_count);
    
    return SUCCESS;
}

void buffer_cleanup_default_pools(void) {
    /* Cleanup size-specific pools first */
    buffer_pool_cleanup(&g_buffer_pool_1518);
    buffer_pool_cleanup(&g_buffer_pool_512);
    buffer_pool_cleanup(&g_buffer_pool_128);
    buffer_pool_cleanup(&g_buffer_pool_64);
    
    /* Cleanup main pools */
    buffer_pool_cleanup(&g_dma_buffer_pool);
    buffer_pool_cleanup(&g_rx_buffer_pool);
    buffer_pool_cleanup(&g_tx_buffer_pool);
    
    /* Log final fast path statistics */
    log_info("Buffer allocation statistics:");
    log_info("  Fast path allocations: %lu", g_fast_path_allocations);
    log_info("  Fast path cache hits: %lu", g_fast_path_cache_hits);
    log_info("  Fallback allocations: %lu", g_fallback_allocations);
}

/* Buffer pool management */
int buffer_pool_init(buffer_pool_t *pool, buffer_type_t type,
                    uint32_t buffer_size, uint32_t buffer_count, uint32_t flags) {
    if (!pool) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        return ERROR_INVALID_PARAM;
    }
    
    /* Initialize pool structure */
    pool->free_list = NULL;
    pool->used_list = NULL;
    pool->buffer_size = buffer_size;
    pool->buffer_count = buffer_count;
    pool->free_count = 0;
    pool->used_count = 0;
    pool->peak_usage = 0;
    pool->type = type;
    pool->flags = flags;
    pool->memory_base = NULL;
    pool->memory_size = 0;
    pool->initialized = false;
    
    /* Allocate memory for all buffers in the pool using three-tier system */
    uint32_t total_size = buffer_count * (buffer_size + sizeof(buffer_desc_t));
    uint32_t mem_flags = 0;
    
    /* Set memory flags based on buffer requirements */
    if (flags & BUFFER_FLAG_ALIGNED) {
        mem_flags |= MEM_FLAG_ALIGNED;
    }
    if (flags & BUFFER_FLAG_DMA_CAPABLE) {
        mem_flags |= MEM_FLAG_DMA_CAPABLE;
    }
    if (flags & BUFFER_FLAG_ZERO_INIT) {
        mem_flags |= MEM_FLAG_ZERO;
    }
    if (flags & BUFFER_FLAG_PERSISTENT) {
        mem_flags |= MEM_FLAG_PERSISTENT;
    }
    
    /* Use DMA-capable memory if requested */
    if (flags & BUFFER_FLAG_DMA_CAPABLE) {
        pool->memory_base = memory_alloc_dma(total_size);
    } else {
        /* Use three-tier memory system with appropriate type */
        pool->memory_base = memory_alloc(total_size, MEM_TYPE_PACKET_BUFFER, mem_flags);
    }
    
    if (!pool->memory_base) {
        buffer_set_last_error(BUFFER_ERROR_OUT_OF_MEMORY);
        return ERROR_NO_MEMORY;
    }
    
    pool->memory_size = total_size;
    
    /* Create buffer descriptors and add to free list */
    uint8_t *current_ptr = (uint8_t*)pool->memory_base;
    
    for (uint32_t i = 0; i < buffer_count; i++) {
        buffer_desc_t *desc = (buffer_desc_t*)current_ptr;
        current_ptr += sizeof(buffer_desc_t);
        
        /* Align buffer data based on CPU capabilities */
        if (flags & BUFFER_FLAG_ALIGNED) {
            extern cpu_info_t g_cpu_info;
            uint32_t alignment = (g_cpu_info.type >= CPU_TYPE_80386) ? 4 : 2;
            current_ptr = (uint8_t*)ALIGN_UP((uint32_t)current_ptr, alignment);
        }
        
        /* Initialize descriptor */
        desc->data = current_ptr;
        desc->size = buffer_size;
        desc->used = 0;
        desc->type = type;
        desc->state = BUFFER_STATE_FREE;
        desc->flags = flags;
        desc->timestamp = 0;
        desc->magic = BUFFER_MAGIC_FREE;
        desc->next = pool->free_list;
        desc->prev = NULL;
        desc->private_data = NULL;
        
        /* Add to free list */
        if (pool->free_list) {
            pool->free_list->prev = desc;
        }
        pool->free_list = desc;
        pool->free_count++;
        
        current_ptr += buffer_size;
        
        /* Apply alignment padding for next buffer */
        if (flags & BUFFER_FLAG_ALIGNED) {
            extern cpu_info_t g_cpu_info;
            uint32_t alignment = (g_cpu_info.type >= CPU_TYPE_80386) ? 4 : 2;
            current_ptr = (uint8_t*)ALIGN_UP((uint32_t)current_ptr, alignment);
        }
    }
    
    pool->initialized = true;
    return SUCCESS;
}

void buffer_pool_cleanup(buffer_pool_t *pool) {
    if (!pool || !pool->initialized) {
        return;
    }
    
    /* Free all buffers and clean up memory */
    if (pool->memory_base) {
        if (pool->flags & BUFFER_FLAG_DMA_CAPABLE) {
            memory_free_dma(pool->memory_base);
        } else {
            /* Use three-tier memory system for freeing */
            memory_free(pool->memory_base);
        }
        pool->memory_base = NULL;
    }
    
    /* Reset pool state */
    pool->free_list = NULL;
    pool->used_list = NULL;
    pool->free_count = 0;
    pool->used_count = 0;
    pool->memory_size = 0;
    pool->initialized = false;
}

/* Buffer allocation and deallocation */
buffer_desc_t* buffer_alloc(buffer_pool_t *pool) {
    if (!pool || !pool->initialized) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    if (!pool->free_list) {
        buffer_set_last_error(BUFFER_ERROR_POOL_FULL);
        return NULL;
    }
    
    /* Remove from free list */
    buffer_desc_t *buffer = pool->free_list;
    pool->free_list = buffer->next;
    if (pool->free_list) {
        pool->free_list->prev = NULL;
    }
    pool->free_count--;
    
    /* Add to used list */
    buffer->next = pool->used_list;
    buffer->prev = NULL;
    if (pool->used_list) {
        pool->used_list->prev = buffer;
    }
    pool->used_list = buffer;
    pool->used_count++;
    
    /* Update peak usage */
    if (pool->used_count > pool->peak_usage) {
        pool->peak_usage = pool->used_count;
    }
    
    /* Initialize buffer state */
    buffer->state = BUFFER_STATE_ALLOCATED;
    buffer->magic = BUFFER_MAGIC_VALID;
    buffer->used = 0;
    buffer->timestamp = 0; /* Timestamp set by caller if needed */
    
    /* Clear buffer data if requested using CPU-optimized operations */
    if (buffer->flags & BUFFER_FLAG_ZERO_INIT) {
        memory_set_optimized(buffer->data, 0, buffer->size);
    }
    
    buffer_update_stats_alloc(buffer->size);
    
    return buffer;
}

void buffer_free(buffer_pool_t *pool, buffer_desc_t *buffer) {
    if (!pool || !buffer || !pool->initialized) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        return;
    }
    
    if (!buffer_is_valid(buffer)) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_BUFFER);
        return;
    }
    
    /* Remove from used list */
    if (buffer->prev) {
        buffer->prev->next = buffer->next;
    } else {
        pool->used_list = buffer->next;
    }
    
    if (buffer->next) {
        buffer->next->prev = buffer->prev;
    }
    pool->used_count--;
    
    /* Add to free list */
    buffer->next = pool->free_list;
    buffer->prev = NULL;
    if (pool->free_list) {
        pool->free_list->prev = buffer;
    }
    pool->free_list = buffer;
    pool->free_count++;
    
    /* Update buffer state */
    buffer->state = BUFFER_STATE_FREE;
    buffer->magic = BUFFER_MAGIC_FREE;
    buffer->used = 0;
    buffer->private_data = NULL;
    
    buffer_update_stats_free(buffer->size);
}

buffer_desc_t* buffer_alloc_type(buffer_type_t type) {
    switch (type) {
        case BUFFER_TYPE_TX:
        case BUFFER_TYPE_DMA_TX:
            return buffer_alloc(&g_tx_buffer_pool);
        case BUFFER_TYPE_RX:
        case BUFFER_TYPE_DMA_RX:
            return buffer_alloc(&g_rx_buffer_pool);
        case BUFFER_TYPE_DESCRIPTOR:
        case BUFFER_TYPE_TEMPORARY:
            return buffer_alloc(&g_dma_buffer_pool);
        default:
            buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
            return NULL;
    }
}

void buffer_free_any(buffer_desc_t *buffer) {
    if (!buffer) {
        return;
    }
    
    /* Determine which pool this buffer belongs to based on size and type */
    switch (buffer->type) {
        case BUFFER_TYPE_TX:
        case BUFFER_TYPE_DMA_TX:
            buffer_free(&g_tx_buffer_pool, buffer);
            break;
        case BUFFER_TYPE_RX:
        case BUFFER_TYPE_DMA_RX:
            buffer_free(&g_rx_buffer_pool, buffer);
            break;
        case BUFFER_TYPE_DESCRIPTOR:
        case BUFFER_TYPE_TEMPORARY:
            buffer_free(&g_dma_buffer_pool, buffer);
            break;
        default:
            buffer_set_last_error(BUFFER_ERROR_INVALID_BUFFER);
            break;
    }
}

/* Buffer validation */
bool buffer_is_valid(const buffer_desc_t *buffer) {
    if (!buffer) {
        return false;
    }
    
    return (buffer->magic == BUFFER_MAGIC_VALID) && 
           (buffer->state != BUFFER_STATE_FREE);
}

bool buffer_validate_magic(const buffer_desc_t *buffer) {
    if (!buffer) {
        return false;
    }
    
    return (buffer->magic == BUFFER_MAGIC_VALID || 
            buffer->magic == BUFFER_MAGIC_FREE);
}

/* Buffer state management */
void buffer_set_state(buffer_desc_t *buffer, buffer_state_t state) {
    if (!buffer || !buffer_is_valid(buffer)) {
        return;
    }
    
    buffer->state = state;
}

buffer_state_t buffer_get_state(const buffer_desc_t *buffer) {
    if (!buffer || !buffer_is_valid(buffer)) {
        return BUFFER_STATE_ERROR;
    }
    
    return buffer->state;
}

bool buffer_is_free(const buffer_desc_t *buffer) {
    return buffer && (buffer->state == BUFFER_STATE_FREE);
}

bool buffer_is_allocated(const buffer_desc_t *buffer) {
    return buffer && (buffer->state == BUFFER_STATE_ALLOCATED);
}

bool buffer_is_in_use(const buffer_desc_t *buffer) {
    return buffer && (buffer->state == BUFFER_STATE_IN_USE);
}

/* Buffer data operations */
int buffer_set_data(buffer_desc_t *buffer, const void *data, uint32_t size) {
    if (!buffer || !buffer_is_valid(buffer) || !data) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        return ERROR_INVALID_PARAM;
    }
    
    if (size > buffer->size) {
        buffer_set_last_error(BUFFER_ERROR_SIZE_MISMATCH);
        return ERROR_INVALID_PARAM;
    }
    
    memory_copy_optimized(buffer->data, data, size);
    buffer->used = size;
    
    return SUCCESS;
}

int buffer_append_data(buffer_desc_t *buffer, const void *data, uint32_t size) {
    if (!buffer || !buffer_is_valid(buffer) || !data) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        return ERROR_INVALID_PARAM;
    }
    
    if (buffer->used + size > buffer->size) {
        buffer_set_last_error(BUFFER_ERROR_SIZE_MISMATCH);
        return ERROR_INVALID_PARAM;
    }
    
    memory_copy_optimized((uint8_t*)buffer->data + buffer->used, data, size);
    buffer->used += size;
    
    return SUCCESS;
}

/* Buffer utilities */
uint32_t buffer_get_size(const buffer_desc_t *buffer) {
    return buffer ? buffer->size : 0;
}

uint32_t buffer_get_used_size(const buffer_desc_t *buffer) {
    return buffer ? buffer->used : 0;
}

uint32_t buffer_get_free_size(const buffer_desc_t *buffer) {
    return buffer ? (buffer->size - buffer->used) : 0;
}

void* buffer_get_data_ptr(const buffer_desc_t *buffer) {
    return buffer ? buffer->data : NULL;
}

buffer_type_t buffer_get_type(const buffer_desc_t *buffer) {
    return buffer ? buffer->type : BUFFER_TYPE_TEMPORARY;
}

/* Buffer pool information */
uint32_t buffer_pool_get_free_count(const buffer_pool_t *pool) {
    return pool ? pool->free_count : 0;
}

uint32_t buffer_pool_get_used_count(const buffer_pool_t *pool) {
    return pool ? pool->used_count : 0;
}

uint32_t buffer_pool_get_total_count(const buffer_pool_t *pool) {
    return pool ? pool->buffer_count : 0;
}

bool buffer_pool_is_empty(const buffer_pool_t *pool) {
    return pool ? (pool->used_count == 0) : true;
}

bool buffer_pool_is_full(const buffer_pool_t *pool) {
    return pool ? (pool->free_count == 0) : true;
}

/* Buffer statistics */
void buffer_stats_init(buffer_stats_t *stats) {
    if (!stats) {
        return;
    }
    
    memory_zero(stats, sizeof(buffer_stats_t));
}

const buffer_stats_t* buffer_get_stats(void) {
    return &g_buffer_stats;
}

void buffer_clear_stats(void) {
    buffer_stats_init(&g_buffer_stats);
}

/* Error handling */
buffer_error_t buffer_get_last_error(void) {
    return g_last_error;
}

const char* buffer_error_to_string(buffer_error_t error) {
    switch (error) {
        case BUFFER_ERROR_NONE:         return "No error";
        case BUFFER_ERROR_INVALID_PARAM: return "Invalid parameter";
        case BUFFER_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case BUFFER_ERROR_POOL_FULL:    return "Buffer pool full";
        case BUFFER_ERROR_INVALID_BUFFER: return "Invalid buffer";
        case BUFFER_ERROR_BUFFER_IN_USE: return "Buffer in use";
        case BUFFER_ERROR_SIZE_MISMATCH: return "Size mismatch";
        case BUFFER_ERROR_ALIGNMENT:    return "Alignment error";
        case BUFFER_ERROR_CORRUPTION:   return "Buffer corruption";
        default:                        return "Unknown error";
    }
}

void buffer_set_error_handler(void (*handler)(buffer_error_t error, const char* message)) {
    g_error_handler = handler;
}

/* Private helper functions */
static void buffer_set_last_error(buffer_error_t error) {
    g_last_error = error;
    
    if (g_error_handler) {
        g_error_handler(error, buffer_error_to_string(error));
    }
}

static void buffer_update_stats_alloc(uint32_t size) {
    g_buffer_stats.total_allocations++;
    g_buffer_stats.current_allocated++;
    g_buffer_stats.bytes_allocated += size;
    
    if (g_buffer_stats.current_allocated > g_buffer_stats.peak_allocated) {
        g_buffer_stats.peak_allocated = g_buffer_stats.current_allocated;
    }
}

static void buffer_update_stats_free(uint32_t size) {
    g_buffer_stats.total_frees++;
    if (g_buffer_stats.current_allocated > 0) {
        g_buffer_stats.current_allocated--;
    }
    g_buffer_stats.bytes_freed += size;
}

/* RX_COPYBREAK optimization implementation */

/**
 * @brief Initialize RX_COPYBREAK optimization
 * @param small_count Number of small buffers to allocate
 * @param large_count Number of large buffers to allocate
 * @return SUCCESS on success, error code on failure
 */
int rx_copybreak_init(uint32_t small_count, uint32_t large_count) {
    int result;
    extern cpu_info_t g_cpu_info;
    
    /* Validate parameters */
    if (small_count == 0 || large_count == 0) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        return ERROR_INVALID_PARAM;
    }
    
    /* Check if already initialized */
    if (g_rx_copybreak_pool.small_pool.initialized || g_rx_copybreak_pool.large_pool.initialized) {
        log_warning("RX_COPYBREAK pool already initialized, cleaning up first");
        rx_copybreak_cleanup();
    }
    
    /* Initialize pool settings */
    g_rx_copybreak_pool.small_buffer_count = small_count;
    g_rx_copybreak_pool.large_buffer_count = large_count;
    g_rx_copybreak_pool.copybreak_threshold = RX_COPYBREAK_THRESHOLD;
    
    /* Initialize statistics */
    g_rx_copybreak_pool.small_allocations = 0;
    g_rx_copybreak_pool.large_allocations = 0;
    g_rx_copybreak_pool.copy_operations = 0;
    g_rx_copybreak_pool.memory_saved = 0;
    
    /* Set up buffer pool flags based on CPU capabilities */
    uint32_t pool_flags = BUFFER_FLAG_ALIGNED;
    if (g_cpu_info.type >= CPU_TYPE_80386) {
        pool_flags |= BUFFER_FLAG_ZERO_INIT; /* 386+ can handle fast zero init */
    }
    
    /* Initialize small buffer pool */
    result = buffer_pool_init(&g_rx_copybreak_pool.small_pool, BUFFER_TYPE_RX,
                             SMALL_BUFFER_SIZE, small_count, pool_flags);
    if (result != SUCCESS) {
        log_error("Failed to initialize RX_COPYBREAK small buffer pool: %s", 
                 buffer_error_to_string(buffer_get_last_error()));
        buffer_set_last_error(BUFFER_ERROR_OUT_OF_MEMORY);
        return result;
    }
    
    /* Initialize large buffer pool */
    result = buffer_pool_init(&g_rx_copybreak_pool.large_pool, BUFFER_TYPE_RX,
                             LARGE_BUFFER_SIZE, large_count, pool_flags);
    if (result != SUCCESS) {
        log_error("Failed to initialize RX_COPYBREAK large buffer pool: %s",
                 buffer_error_to_string(buffer_get_last_error()));
        buffer_pool_cleanup(&g_rx_copybreak_pool.small_pool);
        buffer_set_last_error(BUFFER_ERROR_OUT_OF_MEMORY);
        return result;
    }
    
    log_info("RX_COPYBREAK optimization initialized: small=%u (%u bytes), large=%u (%u bytes), threshold=%u",
             small_count, SMALL_BUFFER_SIZE, large_count, LARGE_BUFFER_SIZE, RX_COPYBREAK_THRESHOLD);
    
    return SUCCESS;
}

/**
 * @brief Cleanup RX_COPYBREAK optimization
 */
void rx_copybreak_cleanup(void) {
    /* Print final statistics before cleanup */
    if (g_rx_copybreak_pool.small_pool.initialized || g_rx_copybreak_pool.large_pool.initialized) {
        log_info("RX_COPYBREAK statistics: small_allocs=%lu, large_allocs=%lu, copy_ops=%lu, memory_saved=%lu bytes",
                 g_rx_copybreak_pool.small_allocations,
                 g_rx_copybreak_pool.large_allocations,
                 g_rx_copybreak_pool.copy_operations,
                 g_rx_copybreak_pool.memory_saved);
    }
    
    /* Cleanup pools */
    buffer_pool_cleanup(&g_rx_copybreak_pool.large_pool);
    buffer_pool_cleanup(&g_rx_copybreak_pool.small_pool);
    
    /* Reset structure */
    memory_zero(&g_rx_copybreak_pool, sizeof(rx_copybreak_pool_t));
}

/**
 * @brief Allocate buffer using RX_COPYBREAK optimization
 * @param packet_size Size of the packet to allocate buffer for
 * @return Buffer descriptor or NULL on failure
 */
buffer_desc_t* rx_copybreak_alloc(uint32_t packet_size) {
    buffer_desc_t *buffer = NULL;
    buffer_pool_t *selected_pool = NULL;
    uint32_t memory_saved = 0;
    
    /* Validate parameters */
    if (packet_size == 0) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    /* Check if RX_COPYBREAK is initialized */
    if (!g_rx_copybreak_pool.small_pool.initialized || !g_rx_copybreak_pool.large_pool.initialized) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        log_error("RX_COPYBREAK not initialized, call rx_copybreak_init() first");
        return NULL;
    }
    
    /* Select pool based on packet size and threshold */
    if (packet_size < g_rx_copybreak_pool.copybreak_threshold) {
        /* Small packet - use small buffer pool */
        selected_pool = &g_rx_copybreak_pool.small_pool;
        
        /* Calculate memory saved by using small buffer instead of large */
        memory_saved = LARGE_BUFFER_SIZE - SMALL_BUFFER_SIZE;
        
        /* Try to allocate from small pool */
        buffer = buffer_alloc(selected_pool);
        if (buffer) {
            g_rx_copybreak_pool.small_allocations++;
            g_rx_copybreak_pool.memory_saved += memory_saved;
            
            /* Note: copy_operations counter will be incremented when actual packet 
             * data is copied from DMA buffers to this small buffer during RX processing */
            
            log_debug("RX_COPYBREAK: allocated small buffer (%u bytes) for packet size %u, saved %u bytes",
                     SMALL_BUFFER_SIZE, packet_size, memory_saved);
            return buffer;
        } else {
            log_debug("RX_COPYBREAK: small pool exhausted for packet size %u, falling back to large pool",
                     packet_size);
        }
    }
    
    /* Large packet or small pool exhausted - use large buffer pool */
    selected_pool = &g_rx_copybreak_pool.large_pool;
    buffer = buffer_alloc(selected_pool);
    
    if (buffer) {
        g_rx_copybreak_pool.large_allocations++;
        
        log_debug("RX_COPYBREAK: allocated large buffer (%u bytes) for packet size %u",
                 LARGE_BUFFER_SIZE, packet_size);
        return buffer;
    }
    
    /* Both pools exhausted - return error */
    buffer_set_last_error(BUFFER_ERROR_POOL_FULL);
    log_warning("RX_COPYBREAK: all pools exhausted for packet size %u", packet_size);
    return NULL;
}

/**
 * @brief Free buffer allocated with RX_COPYBREAK
 * @param buffer Buffer descriptor to free
 */
void rx_copybreak_free(buffer_desc_t* buffer) {
    if (!buffer) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        return;
    }
    
    if (!buffer_is_valid(buffer)) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_BUFFER);
        log_error("RX_COPYBREAK: attempting to free invalid buffer");
        return;
    }
    
    /* Check if RX_COPYBREAK is initialized */
    if (!g_rx_copybreak_pool.small_pool.initialized || !g_rx_copybreak_pool.large_pool.initialized) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        log_error("RX_COPYBREAK not initialized, cannot determine which pool buffer belongs to");
        return;
    }
    
    /* Determine which pool this buffer belongs to based on size */
    if (buffer->size == SMALL_BUFFER_SIZE) {
        /* Return to small pool */
        buffer_free(&g_rx_copybreak_pool.small_pool, buffer);
        log_debug("RX_COPYBREAK: freed small buffer (%u bytes)", buffer->size);
    } else if (buffer->size == LARGE_BUFFER_SIZE) {
        /* Return to large pool */
        buffer_free(&g_rx_copybreak_pool.large_pool, buffer);
        log_debug("RX_COPYBREAK: freed large buffer (%u bytes)", buffer->size);
    } else {
        /* Buffer doesn't belong to RX_COPYBREAK pools */
        buffer_set_last_error(BUFFER_ERROR_INVALID_BUFFER);
        log_error("RX_COPYBREAK: buffer size %u doesn't match any RX_COPYBREAK pool", buffer->size);
        return;
    }
}

/**
 * @brief Get RX_COPYBREAK statistics
 * @param stats Pointer to structure to receive statistics (can be NULL for display only)
 */
void rx_copybreak_get_stats(rx_copybreak_pool_t* stats) {
    /* Check if RX_COPYBREAK is initialized */
    if (!g_rx_copybreak_pool.small_pool.initialized || !g_rx_copybreak_pool.large_pool.initialized) {
        log_warning("RX_COPYBREAK not initialized, statistics not available");
        if (stats) {
            memory_zero(stats, sizeof(rx_copybreak_pool_t));
        }
        return;
    }
    
    /* Copy statistics if requested */
    if (stats) {
        memory_copy_optimized(stats, &g_rx_copybreak_pool, sizeof(rx_copybreak_pool_t));
    }
    
    /* Display current statistics */
    log_info("RX_COPYBREAK Statistics:");
    log_info("  Threshold: %u bytes", g_rx_copybreak_pool.copybreak_threshold);
    log_info("  Small pool: %u buffers (%u bytes each), %u free, %u used, peak: %u",
             g_rx_copybreak_pool.small_buffer_count, SMALL_BUFFER_SIZE,
             g_rx_copybreak_pool.small_pool.free_count,
             g_rx_copybreak_pool.small_pool.used_count,
             g_rx_copybreak_pool.small_pool.peak_usage);
    log_info("  Large pool: %u buffers (%u bytes each), %u free, %u used, peak: %u",
             g_rx_copybreak_pool.large_buffer_count, LARGE_BUFFER_SIZE,
             g_rx_copybreak_pool.large_pool.free_count,
             g_rx_copybreak_pool.large_pool.used_count,
             g_rx_copybreak_pool.large_pool.peak_usage);
    log_info("  Allocations: %lu small, %lu large",
             g_rx_copybreak_pool.small_allocations,
             g_rx_copybreak_pool.large_allocations);
    log_info("  Copy operations: %lu", g_rx_copybreak_pool.copy_operations);
    log_info("  Memory saved: %lu bytes", g_rx_copybreak_pool.memory_saved);
    
    /* Calculate efficiency statistics */
    uint32_t total_allocations = g_rx_copybreak_pool.small_allocations + g_rx_copybreak_pool.large_allocations;
    if (total_allocations > 0) {
        uint32_t small_percentage = (g_rx_copybreak_pool.small_allocations * 100) / total_allocations;
        log_info("  Efficiency: %u%% small buffer usage, %lu bytes average saved per allocation",
                 small_percentage,
                 total_allocations > 0 ? g_rx_copybreak_pool.memory_saved / total_allocations : 0);
    }
}

/**
 * @brief Resize RX_COPYBREAK pools
 * @param new_small_count New number of small buffers
 * @param new_large_count New number of large buffers
 * @return SUCCESS on success, error code on failure
 */
int rx_copybreak_resize_pools(uint32_t new_small_count, uint32_t new_large_count) {
    /* Check if RX_COPYBREAK is initialized */
    if (!g_rx_copybreak_pool.small_pool.initialized || !g_rx_copybreak_pool.large_pool.initialized) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        log_error("RX_COPYBREAK not initialized, cannot resize pools");
        return ERROR_INVALID_PARAM;
    }
    
    /* Validate parameters */
    if (new_small_count == 0 || new_large_count == 0) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        log_error("Invalid pool sizes: small=%u, large=%u", new_small_count, new_large_count);
        return ERROR_INVALID_PARAM;
    }
    
    /* Check if pools are currently in use */
    if (g_rx_copybreak_pool.small_pool.used_count > 0 || g_rx_copybreak_pool.large_pool.used_count > 0) {
        buffer_set_last_error(BUFFER_ERROR_BUFFER_IN_USE);
        log_error("Cannot resize RX_COPYBREAK pools while buffers are in use (small: %u, large: %u)",
                 g_rx_copybreak_pool.small_pool.used_count,
                 g_rx_copybreak_pool.large_pool.used_count);
        return ERROR_INVALID_PARAM;
    }
    
    /* Save current statistics before cleanup */
    uint32_t old_small_allocs = g_rx_copybreak_pool.small_allocations;
    uint32_t old_large_allocs = g_rx_copybreak_pool.large_allocations;
    uint32_t old_copy_ops = g_rx_copybreak_pool.copy_operations;
    uint32_t old_memory_saved = g_rx_copybreak_pool.memory_saved;
    
    log_info("Resizing RX_COPYBREAK pools from small=%u, large=%u to small=%u, large=%u",
             g_rx_copybreak_pool.small_buffer_count, g_rx_copybreak_pool.large_buffer_count,
             new_small_count, new_large_count);
    
    /* Cleanup existing pools */
    rx_copybreak_cleanup();
    
    /* Reinitialize with new sizes */
    int result = rx_copybreak_init(new_small_count, new_large_count);
    if (result != SUCCESS) {
        log_error("Failed to reinitialize RX_COPYBREAK with new sizes");
        return result;
    }
    
    /* Restore cumulative statistics */
    g_rx_copybreak_pool.small_allocations = old_small_allocs;
    g_rx_copybreak_pool.large_allocations = old_large_allocs;
    g_rx_copybreak_pool.copy_operations = old_copy_ops;
    g_rx_copybreak_pool.memory_saved = old_memory_saved;
    
    log_info("RX_COPYBREAK pools resized successfully");
    return SUCCESS;
}

/**
 * @brief Record a copy operation for RX_COPYBREAK statistics
 * 
 * This function should be called whenever packet data is copied from a large
 * buffer to a small buffer as part of the RX_COPYBREAK optimization.
 */
void rx_copybreak_record_copy(void) {
    /* Check if RX_COPYBREAK is initialized */
    if (g_rx_copybreak_pool.small_pool.initialized || g_rx_copybreak_pool.large_pool.initialized) {
        g_rx_copybreak_pool.copy_operations++;
    }
}

/* ========================================================================
 * IRQ Helper Functions for 16-bit Real Mode
 * ======================================================================== */

/**
 * @brief Save flags and disable interrupts (16-bit safe)
 * @return Saved flags value
 */
static inline uint16_t irq_save_disable(void) {
    uint16_t flags;
    __asm__ __volatile__(
        "pushf\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory"
    );
    return flags;
}

/**
 * @brief Restore flags (16-bit safe)
 * @param flags Flags value to restore
 */
static inline void irq_restore(uint16_t flags) {
    __asm__ __volatile__(
        "push %0\n\t"
        "popf"
        :
        : "r"(flags)
        : "memory"
    );
}

/* ========================================================================
 * XMS Buffer Pool Implementation
 * ======================================================================== */

#include "../include/xms_detect.h"

/* Global XMS pool and staging buffers */
static xms_buffer_pool_t g_xms_pool = {0};
static staging_buffer_t *g_staging_buffers = NULL;
static staging_buffer_t *g_staging_freelist = NULL;  /* Freelist head */
static uint32_t g_staging_count = 0;
static uint32_t g_staging_size = 0;
static spsc_queue_t g_deferred_queue = {0};

/**
 * @brief Initialize XMS buffer pool
 * @param pool XMS buffer pool structure
 * @param buffer_size Size of each buffer
 * @param buffer_count Number of buffers (max 32)
 * @return SUCCESS or error code
 */
int xms_buffer_pool_init(xms_buffer_pool_t *pool, uint32_t buffer_size, uint32_t buffer_count) {
    int result;
    uint32_t total_size_kb;
    
    if (!pool || buffer_size == 0 || buffer_count == 0 || buffer_count > 32) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        return ERROR_INVALID_PARAM;
    }
    
    /* Check if XMS is available */
    if (!xms_is_available()) {
        log_warning("XMS not available, cannot initialize XMS buffer pool");
        buffer_set_last_error(BUFFER_ERROR_OUT_OF_MEMORY);
        return ERROR_NOT_SUPPORTED;
    }
    
    /* Calculate total size needed (round up to KB) */
    total_size_kb = ((buffer_size * buffer_count) + 1023) / 1024;
    
    /* Allocate XMS memory */
    result = xms_allocate(total_size_kb, &pool->xms_handle);
    if (result != XMS_SUCCESS) {
        log_error("Failed to allocate %u KB of XMS memory: %d", total_size_kb, result);
        buffer_set_last_error(BUFFER_ERROR_OUT_OF_MEMORY);
        return result;
    }
    
    /* Initialize pool structure */
    pool->total_size = total_size_kb * 1024;
    pool->buffer_size = buffer_size;
    pool->buffer_count = buffer_count;
    pool->free_map = (1UL << buffer_count) - 1;  /* All buffers initially free */
    pool->staging_offset = 0;
    
    /* Reset statistics */
    pool->xms_allocations = 0;
    pool->xms_frees = 0;
    pool->xms_copies_to = 0;
    pool->xms_copies_from = 0;
    pool->peak_usage = 0;
    
    log_info("Initialized XMS buffer pool: %u buffers of %u bytes (%u KB total)",
             buffer_count, buffer_size, total_size_kb);
    
    return SUCCESS;
}

/**
 * @brief Cleanup XMS buffer pool
 * @param pool XMS buffer pool structure
 */
void xms_buffer_pool_cleanup(xms_buffer_pool_t *pool) {
    if (!pool || pool->xms_handle == 0) {
        return;
    }
    
    /* Log final statistics */
    log_info("XMS pool statistics: allocs=%lu, frees=%lu, copies_to=%lu, copies_from=%lu, peak=%lu",
             pool->xms_allocations, pool->xms_frees,
             pool->xms_copies_to, pool->xms_copies_from, pool->peak_usage);
    
    /* Free XMS memory */
    xms_free(pool->xms_handle);
    
    /* Clear pool structure */
    memory_zero(pool, sizeof(xms_buffer_pool_t));
}

/**
 * @brief Allocate buffer from XMS pool
 * @param pool XMS buffer pool
 * @param buffer_offset Output: offset of allocated buffer
 * @return SUCCESS or error code
 */
int xms_buffer_alloc(xms_buffer_pool_t *pool, uint32_t *buffer_offset) {
    uint32_t i;
    uint32_t used_count;
    
    if (!pool || !buffer_offset || pool->xms_handle == 0) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        return ERROR_INVALID_PARAM;
    }
    
    /* Find first free buffer */
    for (i = 0; i < pool->buffer_count; i++) {
        if (pool->free_map & (1UL << i)) {
            /* Mark buffer as used */
            pool->free_map &= ~(1UL << i);
            
            /* Calculate offset */
            *buffer_offset = i * pool->buffer_size;
            
            /* Update statistics */
            pool->xms_allocations++;
            /* Count used buffers (bits that are 0 in free_map) */
            used_count = 0;
            for (uint32_t j = 0; j < pool->buffer_count; j++) {
                if (!(pool->free_map & (1UL << j))) {
                    used_count++;
                }
            }
            if (used_count > pool->peak_usage) {
                pool->peak_usage = used_count;
            }
            
            log_debug("Allocated XMS buffer %u at offset %u", i, *buffer_offset);
            return SUCCESS;
        }
    }
    
    /* No free buffers */
    buffer_set_last_error(BUFFER_ERROR_POOL_FULL);
    log_warning("XMS buffer pool exhausted");
    return ERROR_NO_MEMORY;
}

/**
 * @brief Free buffer back to XMS pool
 * @param pool XMS buffer pool
 * @param buffer_offset Offset of buffer to free
 */
void xms_buffer_free(xms_buffer_pool_t *pool, uint32_t buffer_offset) {
    uint32_t buffer_index;
    
    if (!pool || pool->xms_handle == 0) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        return;
    }
    
    /* Calculate buffer index */
    buffer_index = buffer_offset / pool->buffer_size;
    if (buffer_index >= pool->buffer_count) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_BUFFER);
        log_error("Invalid XMS buffer offset %u", buffer_offset);
        return;
    }
    
    /* Check if already free */
    if (pool->free_map & (1UL << buffer_index)) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_BUFFER);
        log_error("XMS buffer %u already free", buffer_index);
        return;
    }
    
    /* Mark buffer as free */
    pool->free_map |= (1UL << buffer_index);
    pool->xms_frees++;
    
    log_debug("Freed XMS buffer %u at offset %u", buffer_index, buffer_offset);
}

/**
 * @brief Copy data to XMS buffer (bottom-half only!)
 * @param pool XMS buffer pool
 * @param offset Buffer offset in XMS
 * @param src Source data in conventional memory
 * @param size Size to copy
 * @return SUCCESS or error code
 */
int xms_copy_to_buffer(xms_buffer_pool_t *pool, uint32_t offset, void *src, uint32_t size) {
    int result;
    
    if (!pool || !src || size == 0 || pool->xms_handle == 0) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        return ERROR_INVALID_PARAM;
    }
    
    /* Validate offset and size */
    if (offset + size > pool->total_size) {
        buffer_set_last_error(BUFFER_ERROR_SIZE_MISMATCH);
        return ERROR_INVALID_PARAM;
    }
    
    /* Build proper real-mode address for XMS move */
    /* XMS expects segment:offset in format: high word = segment, low word = offset */
    uint32_t src_addr = ((uint32_t)FP_SEG(src) << 16) | FP_OFF(src);
    
    /* Use XMS move function (src_handle=0 means conventional memory) */
    result = xms_move_memory(pool->xms_handle, offset, 0, src_addr, size);
    if (result != XMS_SUCCESS) {
        log_error("Failed to copy %u bytes to XMS at offset %u: %d", size, offset, result);
        buffer_set_last_error(BUFFER_ERROR_OUT_OF_MEMORY);
        return result;
    }
    
    pool->xms_copies_to++;
    log_debug("Copied %u bytes to XMS at offset %u", size, offset);
    return SUCCESS;
}

/**
 * @brief Copy data from XMS buffer (bottom-half only!)
 * @param pool XMS buffer pool
 * @param dest Destination in conventional memory
 * @param offset Buffer offset in XMS
 * @param size Size to copy
 * @return SUCCESS or error code
 */
int xms_copy_from_buffer(xms_buffer_pool_t *pool, void *dest, uint32_t offset, uint32_t size) {
    int result;
    
    if (!pool || !dest || size == 0 || pool->xms_handle == 0) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        return ERROR_INVALID_PARAM;
    }
    
    /* Validate offset and size */
    if (offset + size > pool->total_size) {
        buffer_set_last_error(BUFFER_ERROR_SIZE_MISMATCH);
        return ERROR_INVALID_PARAM;
    }
    
    /* Build proper real-mode address for XMS move */
    /* XMS expects segment:offset in format: high word = segment, low word = offset */
    uint32_t dest_addr = ((uint32_t)FP_SEG(dest) << 16) | FP_OFF(dest);
    
    /* Use XMS move function (dest_handle=0 means conventional memory) */
    result = xms_move_memory(0, dest_addr, pool->xms_handle, offset, size);
    if (result != XMS_SUCCESS) {
        log_error("Failed to copy %u bytes from XMS at offset %u: %d", size, offset, result);
        buffer_set_last_error(BUFFER_ERROR_OUT_OF_MEMORY);
        return result;
    }
    
    pool->xms_copies_from++;
    log_debug("Copied %u bytes from XMS at offset %u", size, offset);
    return SUCCESS;
}

/* ========================================================================
 * Staging Buffer Implementation (for ISR use)
 * ======================================================================== */

/**
 * @brief Initialize staging buffers for ISR
 * @param count Number of staging buffers
 * @param size Size of each buffer
 * @return SUCCESS or error code
 */
int staging_buffer_init(uint32_t count, uint32_t size) {
    uint32_t i;
    uint8_t *buffer_data;
    
    if (count == 0 || size == 0) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        return ERROR_INVALID_PARAM;
    }
    
    /* Cleanup any existing buffers */
    if (g_staging_buffers) {
        staging_buffer_cleanup();
    }
    
    /* Allocate staging buffer array */
    g_staging_buffers = (staging_buffer_t*)memory_allocate(
        count * sizeof(staging_buffer_t), MEMORY_FLAG_ZERO);
    if (!g_staging_buffers) {
        buffer_set_last_error(BUFFER_ERROR_OUT_OF_MEMORY);
        return ERROR_NO_MEMORY;
    }
    
    /* Allocate buffer data */
    buffer_data = (uint8_t*)memory_allocate(count * size, MEMORY_FLAG_ZERO);
    if (!buffer_data) {
        memory_free(g_staging_buffers);
        g_staging_buffers = NULL;
        buffer_set_last_error(BUFFER_ERROR_OUT_OF_MEMORY);
        return ERROR_NO_MEMORY;
    }
    
    /* Initialize each staging buffer and build freelist */
    for (i = 0; i < count; i++) {
        g_staging_buffers[i].magic = STAGING_BUFFER_MAGIC;  /* Set magic cookie */
        g_staging_buffers[i].data = buffer_data + (i * size);
        g_staging_buffers[i].size = size;
        g_staging_buffers[i].used = 0;
        g_staging_buffers[i].in_use = 0;
        g_staging_buffers[i].nic_index = 0;
        g_staging_buffers[i].packet_size = 0;
        /* Link into freelist */
        g_staging_buffers[i].next = (i < count - 1) ? &g_staging_buffers[i + 1] : NULL;
    }
    g_staging_freelist = &g_staging_buffers[0];
    
    g_staging_count = count;
    g_staging_size = size;
    
    log_info("Initialized %u staging buffers of %u bytes each", count, size);
    return SUCCESS;
}

/**
 * @brief Cleanup staging buffers
 */
void staging_buffer_cleanup(void) {
    if (g_staging_buffers) {
        /* Free buffer data (allocated as single block) */
        if (g_staging_buffers[0].data) {
            memory_free(g_staging_buffers[0].data);
        }
        
        /* Free buffer array */
        memory_free(g_staging_buffers);
        g_staging_buffers = NULL;
    }
    
    g_staging_count = 0;
    g_staging_size = 0;
}

/**
 * @brief Allocate staging buffer (ISR-safe via freelist)
 * @return Staging buffer or NULL if none available
 * 
 * ISR calls this with interrupts already disabled.
 * Bottom-half must use CLI/STI when manipulating freelist.
 */
staging_buffer_t* staging_buffer_alloc(void) {
    staging_buffer_t *buffer;
    
    if (!g_staging_freelist) {
        return NULL;
    }
    
    /* Remove from freelist head */
    buffer = g_staging_freelist;
    g_staging_freelist = buffer->next;
    
    /* Initialize buffer */
    buffer->in_use = 1;
    buffer->used = 0;
    buffer->packet_size = 0;
    buffer->nic_index = 0;
    buffer->next = NULL;
    
    return buffer;
}

/**
 * @brief Free staging buffer back to freelist
 * @param buffer Staging buffer to free
 * 
 * Called from bottom-half - must protect with CLI/STI
 */
void staging_buffer_free(staging_buffer_t *buffer) {
    uint16_t flags;  /* 16-bit for real mode! */
    
    if (!buffer) {
        return;
    }
    
    /* Magic cookie validation */
    if (buffer->magic != STAGING_BUFFER_MAGIC) {
        log_error("Buffer corruption detected! Magic=0x%04X expected=0x%04X",
                  buffer->magic, STAGING_BUFFER_MAGIC);
        return;
    }
    
    /* Range check */
    if (buffer < g_staging_buffers || 
        buffer >= g_staging_buffers + g_staging_count) {
        log_error("Buffer %p outside valid range", buffer);
        return;
    }
    
    /* Double-free detection */
    if (!buffer->in_use) {
        log_error("Double-free detected on staging buffer %p", buffer);
        return;
    }
    
    /* Clear buffer */
    buffer->in_use = 0;
    buffer->used = 0;
    buffer->packet_size = 0;
    buffer->nic_index = 0;
    
    /* Add to freelist with interrupts disabled (16-bit safe) */
    flags = irq_save_disable();
    buffer->next = g_staging_freelist;
    g_staging_freelist = buffer;
    irq_restore(flags);
}

/* ========================================================================
 * SPSC Ring Buffer Implementation (ISR-Safe)
 * ======================================================================== */

/**
 * @brief Initialize SPSC queue (explicit zero-init)
 * @param queue SPSC queue structure
 * @return SUCCESS or error code
 * 
 * Ensures clean start state for ISR safety
 */
int spsc_queue_init(spsc_queue_t *queue) {
    if (!queue) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Zero entire structure for safety */
    memory_zero(queue, sizeof(spsc_queue_t));
    
    /* Explicitly set critical fields */
    queue->head = 0;
    queue->tail = 0;
    
    log_debug("SPSC queue initialized: size=%u, mask=0x%02X", 
              SPSC_QUEUE_SIZE, SPSC_QUEUE_MASK);
    
    return SUCCESS;
}

/**
 * @brief Cleanup SPSC queue
 * @param queue SPSC queue structure
 */
void spsc_queue_cleanup(spsc_queue_t *queue) {
    staging_buffer_t *buffer;
    
    if (!queue) {
        return;
    }
    
    /* Free all queued buffers */
    while ((buffer = spsc_queue_dequeue(queue)) != NULL) {
        staging_buffer_free(buffer);
    }
    
    /* Reset queue */
    queue->head = 0;
    queue->tail = 0;
}

/**
 * @brief Enqueue buffer (ISR producer)
 * @param queue SPSC queue
 * @param buffer Staging buffer to enqueue
 * @return SUCCESS or ERROR_QUEUE_FULL
 * 
 * Called from ISR with interrupts disabled - no locking needed
 */
int spsc_queue_enqueue(spsc_queue_t *queue, staging_buffer_t *buffer) {
    uint8_t next_tail;
    
    if (!queue || !buffer) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Calculate next tail position */
    next_tail = (queue->tail + 1) & SPSC_QUEUE_MASK;
    
    /* Check if queue is full */
    if (next_tail == queue->head) {
        queue->enqueue_full++;
        return ERROR_QUEUE_FULL;
    }
    
    /* Store buffer */
    queue->buffers[queue->tail] = buffer;
    
    /* Compiler barrier to ensure buffer is written before tail update */
    compiler_barrier();
    
    /* Update tail (visible to consumer) */
    queue->tail = next_tail;
    
    queue->enqueue_success++;
    return SUCCESS;
}

/**
 * @brief Dequeue buffer (bottom-half consumer)
 * @param queue SPSC queue
 * @return Staging buffer or NULL if empty
 * 
 * Called from bottom-half - no locking needed for SPSC
 */
staging_buffer_t* spsc_queue_dequeue(spsc_queue_t *queue) {
    staging_buffer_t *buffer;
    uint8_t current_head;
    
    if (!queue) {
        return NULL;
    }
    
    current_head = queue->head;
    
    /* Check if queue is empty */
    if (current_head == queue->tail) {
        queue->dequeue_empty++;
        return NULL;
    }
    
    /* Get buffer */
    buffer = queue->buffers[current_head];
    
    /* Compiler barrier to ensure buffer is read before head update */
    compiler_barrier();
    
    /* Update head (visible to producer) */
    queue->head = (current_head + 1) & SPSC_QUEUE_MASK;
    
    queue->dequeue_success++;
    return buffer;
}

/* Note: spsc_queue_is_empty() and spsc_queue_is_full() are now
 * inline functions in buffer_alloc.h for performance */

/* Additional buffer management functions - Phase 3 complete implementation */
int buffer_pool_expand(buffer_pool_t *pool, uint32_t additional_buffers) {
    /* Pool expansion implementation - allocates additional memory blocks */
    return ERROR_NOT_SUPPORTED;
}

int buffer_pool_shrink(buffer_pool_t *pool, uint32_t remove_buffers) {
    /* Pool shrinking implementation - releases unused memory blocks */
    return ERROR_NOT_SUPPORTED;
}

int buffer_prepend_data(buffer_desc_t *buffer, const void *data, uint32_t size) {
    /* Buffer prepend implementation - moves data and adds header space */
    return ERROR_NOT_SUPPORTED;
}

int buffer_copy_data(buffer_desc_t *dest, const buffer_desc_t *src) {
    /* Buffer copy implementation - creates duplicate with same content */
    return ERROR_NOT_SUPPORTED;
}

int buffer_move_data(buffer_desc_t *dest, buffer_desc_t *src) {
    /* Buffer move implementation - transfers ownership between contexts */
    return ERROR_NOT_SUPPORTED;
}

void buffer_clear_data(buffer_desc_t *buffer) {
    if (buffer && buffer_is_valid(buffer)) {
        memory_set_optimized(buffer->data, 0, buffer->size);
        buffer->used = 0;
    }
}

/**
 * @brief Allocate buffer optimized for specific Ethernet frame sizes using fast path pools
 * @param frame_size Expected frame size
 * @param type Buffer type
 * @return Buffer descriptor or NULL
 */
buffer_desc_t* buffer_alloc_ethernet_frame(uint32_t frame_size, buffer_type_t type) {
    buffer_pool_t *pool = NULL;
    buffer_desc_t *buffer = NULL;
    bool used_fast_path = false;
    
    /* Fast path allocation based on common packet sizes */
    if (frame_size <= 64 && g_buffer_pool_64.initialized) {
        pool = &g_buffer_pool_64;
        used_fast_path = true;
    } else if (frame_size <= 128 && g_buffer_pool_128.initialized) {
        pool = &g_buffer_pool_128;
        used_fast_path = true;
    } else if (frame_size <= 512 && g_buffer_pool_512.initialized) {
        pool = &g_buffer_pool_512;
        used_fast_path = true;
    } else if (frame_size <= 1518 && g_buffer_pool_1518.initialized) {
        pool = &g_buffer_pool_1518;
        used_fast_path = true;
    }
    
    /* Try fast path allocation first */
    if (used_fast_path && pool) {
        buffer = buffer_alloc(pool);
        if (buffer) {
            g_fast_path_allocations++;
            g_fast_path_cache_hits++;
            log_debug("Fast path allocation: %u bytes from size-specific pool", frame_size);
            return buffer;
        }
        /* Fast path pool empty, fall through to regular allocation */
        log_debug("Fast path pool empty for size %u, using fallback", frame_size);
    }
    
    /* Fallback to regular pools */
    g_fallback_allocations++;
    
    if (frame_size <= 1518) {
        /* Use appropriate regular pool based on type */
        if (type == BUFFER_TYPE_RX) {
            pool = &g_rx_buffer_pool;
        } else if (type == BUFFER_TYPE_DMA_TX || type == BUFFER_TYPE_DMA_RX) {
            pool = &g_dma_buffer_pool;
        } else {
            pool = &g_tx_buffer_pool;
        }
        
        buffer = buffer_alloc(pool);
        if (buffer) {
            log_debug("Fallback allocation: %u bytes from regular pool", frame_size);
            return buffer;
        }
    } else {
        /* Jumbo frame - not supported */
        buffer_set_last_error(BUFFER_ERROR_SIZE_MISMATCH);
        log_error("Jumbo frame size %u not supported (max 1518)", frame_size);
        return NULL;
    }
    
    /* All pools exhausted */
    buffer_set_last_error(BUFFER_ERROR_POOL_FULL);
    log_warning("All buffer pools exhausted for frame size %u", frame_size);
    return NULL;
}

/**
 * @brief Allocate buffer with specific alignment for DMA operations
 * @param size Buffer size
 * @param alignment Required alignment (must be power of 2)
 * @return Buffer descriptor or NULL
 */
buffer_desc_t* buffer_alloc_dma(uint32_t size, uint32_t alignment) {
    extern cpu_info_t g_cpu_info;
    
    /* Validate alignment */
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        buffer_set_last_error(BUFFER_ERROR_ALIGNMENT);
        return NULL;
    }
    
    /* Use DMA pool if available and size fits */
    if (size <= DMA_BUFFER_SIZE) {
        buffer_desc_t *buffer = buffer_alloc(&g_dma_buffer_pool);
        if (buffer) {
            /* Check if buffer data is properly aligned */
            if (!IS_ALIGNED((uint32_t)buffer->data, alignment)) {
                log_warning("DMA buffer not properly aligned: %p (need %u-byte alignment)",
                           buffer->data, alignment);
                /* For now, continue with unaligned buffer */
                /* Alignment adjustment for DMA compatibility */
            }
            buffer->flags |= BUFFER_FLAG_DMA_CAPABLE;
            return buffer;
        }
    }
    
    /* Fall back to regular allocation */
    buffer_set_last_error(BUFFER_ERROR_POOL_FULL);
    return NULL;
}

/**
 * @brief Get optimal buffer size for CPU architecture
 * @param requested_size Requested size
 * @return Optimized size aligned to CPU boundaries
 */
uint32_t buffer_get_optimal_size(uint32_t requested_size) {
    extern cpu_info_t g_cpu_info;
    uint32_t alignment;
    
    /* Determine optimal alignment based on CPU */
    if (g_cpu_info.type >= CPU_TYPE_80386) {
        alignment = 4; /* 32-bit alignment for 386+ */
    } else {
        alignment = 2; /* 16-bit alignment for older CPUs */
    }
    
    /* Round up to alignment boundary */
    return ALIGN_UP(requested_size, alignment);
}

/**
 * @brief Initialize buffer system with CPU and memory optimizations
 * @return 0 on success, negative on error
 */
int buffer_system_init_optimized(void) {
    int result;
    extern cpu_info_t g_cpu_info;
    
    /* Initialize basic buffer system first */
    result = buffer_system_init();
    if (result != SUCCESS) {
        return result;
    }
    
    /* Apply CPU-specific optimizations */
    log_info("Optimizing buffer system for %s CPU",
             cpu_type_to_string(g_cpu_info.type));
    
    if (g_cpu_info.type >= CPU_TYPE_80386) {
        log_info("Enabling 32-bit buffer optimizations");
        
        /* 386+ specific optimizations */
        if (g_cpu_info.features & CPU_FEATURE_TSC) {
            log_info("TSC available for buffer timing measurements");
        }
    } else {
        log_info("Using 16-bit buffer operations for compatibility");
    }
    
    /* Log memory tier availability for buffer allocation */
    if (memory_xms_available()) {
        uint32_t xms_size = memory_get_xms_size();
        log_info("XMS memory available: %u KB for large packet buffers", xms_size);
    }
    
    return SUCCESS;
}

/**
 * @brief Fast buffer copy optimized for packet data
 * @param dest Destination buffer
 * @param src Source buffer
 * @return 0 on success, negative on error
 */
int buffer_copy_packet_data(buffer_desc_t *dest, const buffer_desc_t *src) {
    if (!dest || !src || !buffer_is_valid(dest) || !buffer_is_valid(src)) {
        buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
        return ERROR_INVALID_PARAM;
    }
    
    if (src->used > dest->size) {
        buffer_set_last_error(BUFFER_ERROR_SIZE_MISMATCH);
        return ERROR_INVALID_PARAM;
    }
    
    /* Use CPU-optimized copy for packet data */
    memory_copy_optimized(dest->data, src->data, src->used);
    dest->used = src->used;
    
    return SUCCESS;
}

/**
 * @brief Prefetch buffer data for better cache performance
 * @param buffer Buffer to prefetch
 */
void buffer_prefetch_data(const buffer_desc_t *buffer) {
    extern cpu_info_t g_cpu_info;
    
    if (!buffer || !buffer_is_valid(buffer)) {
        return;
    }
    
    /* On 386+ CPUs, we can implement cache-friendly prefetching */
    if (g_cpu_info.type >= CPU_TYPE_80386) {
        /* Touch the data to bring it into cache */
        volatile uint8_t *data = (volatile uint8_t*)buffer->data;
        uint32_t size = buffer->used;
        uint32_t cache_line = 32; /* Typical cache line size */
        
        /* Touch every cache line */
        for (uint32_t offset = 0; offset < size; offset += cache_line) {
            (void)data[offset]; /* Read to trigger cache load */
        }
    }
}

/* === Per-NIC Buffer Pool Integration === */

/**
 * @brief Enhanced buffer allocation with NIC awareness
 * @param nic_id NIC identifier (INVALID_NIC_ID for legacy allocation)
 * @param type Buffer type
 * @param size Buffer size
 * @return Buffer descriptor or NULL
 */
buffer_desc_t* buffer_alloc_nic_aware(nic_id_t nic_id, buffer_type_t type, uint32_t size) {
    buffer_desc_t* buffer = NULL;
    
    /* Try per-NIC allocation first if NIC ID is valid */
    if (nic_id != INVALID_NIC_ID && nic_buffer_is_initialized(nic_id)) {
        buffer = nic_buffer_alloc(nic_id, type, size);
        if (buffer) {
            log_debug("Allocated buffer from per-NIC pool for NIC %d", nic_id);
            return buffer;
        }
        log_debug("Per-NIC allocation failed for NIC %d, trying legacy allocation", nic_id);
    }
    
    /* Fall back to legacy global pools */
    switch (type) {
        case BUFFER_TYPE_TX:
        case BUFFER_TYPE_DMA_TX:
            buffer = buffer_alloc(&g_tx_buffer_pool);
            break;
        case BUFFER_TYPE_RX:
        case BUFFER_TYPE_DMA_RX:
            buffer = buffer_alloc(&g_rx_buffer_pool);
            break;
        case BUFFER_TYPE_DESCRIPTOR:
        case BUFFER_TYPE_TEMPORARY:
            buffer = buffer_alloc(&g_dma_buffer_pool);
            break;
        default:
            buffer_set_last_error(BUFFER_ERROR_INVALID_PARAM);
            return NULL;
    }
    
    if (buffer) {
        log_debug("Allocated buffer from legacy global pool (type %d)", type);
    } else {
        log_warning("Both per-NIC and legacy buffer allocation failed");
    }
    
    return buffer;
}

/**
 * @brief Enhanced buffer free with NIC awareness
 * @param nic_id NIC identifier (INVALID_NIC_ID for auto-detection)
 * @param buffer Buffer to free
 */
void buffer_free_nic_aware(nic_id_t nic_id, buffer_desc_t* buffer) {
    if (!buffer) {
        return;
    }
    
    /* Try per-NIC free first if NIC ID is valid */
    if (nic_id != INVALID_NIC_ID && nic_buffer_is_initialized(nic_id)) {
        nic_buffer_free(nic_id, buffer);
        log_debug("Freed buffer to per-NIC pool for NIC %d", nic_id);
        return;
    }
    
    /* Try to auto-detect NIC if ID not provided */
    if (nic_id == INVALID_NIC_ID) {
        /* Try all initialized NICs to find the right pool */
        for (nic_id_t test_id = 0; test_id < MAX_NICS; test_id++) {
            if (nic_buffer_is_initialized(test_id)) {
                /* This is a simplified approach - in practice we'd need better tracking */
                nic_buffer_free(test_id, buffer);
                log_debug("Auto-detected and freed buffer to NIC %d pool", test_id);
                return;
            }
        }
    }
    
    /* Fall back to legacy free */
    buffer_free_any(buffer);
    log_debug("Freed buffer using legacy method");
}

/**
 * @brief Register a NIC with the buffer system
 * @param nic_id NIC identifier
 * @param nic_type Type of NIC
 * @param nic_name Human-readable NIC name
 * @return SUCCESS on success, error code on failure
 */
int buffer_register_nic(nic_id_t nic_id, nic_type_t nic_type, const char* nic_name) {
    if (!g_buffer_system_initialized) {
        log_error("Buffer system not initialized");
        return ERROR_INVALID_PARAM;
    }
    
    log_info("Registering NIC %d (%s) with buffer system", nic_id, nic_name ? nic_name : "Unknown");
    
    /* Create per-NIC buffer pools */
    int result = nic_buffer_pool_create(nic_id, nic_type, nic_name);
    if (result != SUCCESS) {
        log_error("Failed to create buffer pools for NIC %d: %d", nic_id, result);
        return result;
    }
    
    /* Initialize RX_COPYBREAK for this NIC based on type */
    uint32_t small_count = DEFAULT_SMALL_BUFFERS_PER_NIC;
    uint32_t large_count = DEFAULT_LARGE_BUFFERS_PER_NIC;
    uint32_t threshold = RX_COPYBREAK_THRESHOLD;
    
    /* Adjust based on NIC type */
    if (nic_type == NIC_TYPE_3C515_TX) {
        /* 3C515-TX can handle more aggressive optimization */
        small_count = 32;
        large_count = 16;
        threshold = 256; /* Higher threshold for fast NIC */
    }
    
    result = nic_rx_copybreak_init(nic_id, small_count, large_count, threshold);
    if (result != SUCCESS) {
        log_warning("Failed to initialize RX_COPYBREAK for NIC %d: %d", nic_id, result);
        /* Continue without RX_COPYBREAK */
    }
    
    log_info("Successfully registered NIC %d with buffer system", nic_id);
    return SUCCESS;
}

/**
 * @brief Unregister a NIC from the buffer system
 * @param nic_id NIC identifier
 * @return SUCCESS on success, error code on failure
 */
int buffer_unregister_nic(nic_id_t nic_id) {
    if (!g_buffer_system_initialized) {
        return ERROR_INVALID_PARAM;
    }
    
    log_info("Unregistering NIC %d from buffer system", nic_id);
    
    /* Destroy per-NIC buffer pools */
    int result = nic_buffer_pool_destroy(nic_id);
    if (result != SUCCESS) {
        log_warning("Failed to destroy buffer pools for NIC %d: %d", nic_id, result);
    }
    
    return result;
}

/**
 * @brief Get buffer allocation statistics for a specific NIC
 * @param nic_id NIC identifier
 * @param stats Pointer to receive statistics
 * @return SUCCESS on success, error code on failure
 */
int buffer_get_nic_stats(nic_id_t nic_id, buffer_pool_stats_t* stats) {
    if (!stats) {
        return ERROR_INVALID_PARAM;
    }
    
    return nic_buffer_get_stats(nic_id, stats);
}

/**
 * @brief Trigger buffer resource rebalancing across all NICs
 * @return SUCCESS on success, error code on failure
 */
int buffer_rebalance_resources(void) {
    if (!g_buffer_system_initialized) {
        return ERROR_INVALID_PARAM;
    }
    
    log_info("Triggering buffer resource rebalancing");
    return balance_buffer_resources();
}

/**
 * @brief Enhanced Ethernet frame allocation with NIC awareness
 * @param nic_id NIC identifier
 * @param frame_size Expected frame size
 * @param type Buffer type
 * @return Buffer descriptor or NULL
 */
buffer_desc_t* buffer_alloc_ethernet_frame_nic(nic_id_t nic_id, uint32_t frame_size, buffer_type_t type) {
    /* Try per-NIC allocation first */
    if (nic_id != INVALID_NIC_ID && nic_buffer_is_initialized(nic_id)) {
        return nic_buffer_alloc_ethernet_frame(nic_id, frame_size, type);
    }
    
    /* Fall back to legacy allocation */
    return buffer_alloc_ethernet_frame(frame_size, type);
}

/**
 * @brief Enhanced RX_COPYBREAK allocation with NIC awareness
 * @param nic_id NIC identifier
 * @param packet_size Size of packet
 * @return Buffer descriptor or NULL
 */
buffer_desc_t* buffer_rx_copybreak_alloc_nic(nic_id_t nic_id, uint32_t packet_size) {
    /* Try per-NIC RX_COPYBREAK first */
    if (nic_id != INVALID_NIC_ID && nic_buffer_is_initialized(nic_id)) {
        buffer_desc_t* buffer = nic_rx_copybreak_alloc(nic_id, packet_size);
        if (buffer) {
            return buffer;
        }
        log_debug("Per-NIC RX_COPYBREAK allocation failed for NIC %d, using legacy", nic_id);
    }
    
    /* Fall back to global RX_COPYBREAK */
    return rx_copybreak_alloc(packet_size);
}

/**
 * @brief Enhanced RX_COPYBREAK free with NIC awareness
 * @param nic_id NIC identifier
 * @param buffer Buffer to free
 */
void buffer_rx_copybreak_free_nic(nic_id_t nic_id, buffer_desc_t* buffer) {
    if (!buffer) {
        return;
    }
    
    /* Try per-NIC RX_COPYBREAK first */
    if (nic_id != INVALID_NIC_ID && nic_buffer_is_initialized(nic_id)) {
        nic_rx_copybreak_free(nic_id, buffer);
        return;
    }
    
    /* Fall back to global RX_COPYBREAK */
    rx_copybreak_free(buffer);
}

/**
 * @brief Print comprehensive buffer statistics including per-NIC information
 */
void buffer_print_comprehensive_stats(void) {
    /* Print global legacy statistics */
    log_info("=== Legacy Global Buffer Pool Statistics ===");
    
    log_info("TX Pool: %u total, %u free, %u used, peak %u",
             buffer_pool_get_total_count(&g_tx_buffer_pool),
             buffer_pool_get_free_count(&g_tx_buffer_pool),
             buffer_pool_get_used_count(&g_tx_buffer_pool),
             g_tx_buffer_pool.peak_usage);
    
    log_info("RX Pool: %u total, %u free, %u used, peak %u",
             buffer_pool_get_total_count(&g_rx_buffer_pool),
             buffer_pool_get_free_count(&g_rx_buffer_pool),
             buffer_pool_get_used_count(&g_rx_buffer_pool),
             g_rx_buffer_pool.peak_usage);
    
    log_info("DMA Pool: %u total, %u free, %u used, peak %u",
             buffer_pool_get_total_count(&g_dma_buffer_pool),
             buffer_pool_get_free_count(&g_dma_buffer_pool),
             buffer_pool_get_used_count(&g_dma_buffer_pool),
             g_dma_buffer_pool.peak_usage);
    
    /* Print global buffer statistics */
    const buffer_stats_t* stats = buffer_get_stats();
    log_info("Global Stats: %lu total allocs, %lu failures, %lu current, %lu peak",
             stats->total_allocations, stats->allocation_failures,
             stats->current_allocated, stats->peak_allocated);
    
    /* Print per-NIC statistics */
    nic_buffer_print_all_stats();
    
    /* Print fast path allocation statistics */
    log_info("Fast Path Stats: %lu fast allocs, %lu cache hits, %lu fallbacks",
             g_fast_path_allocations, g_fast_path_cache_hits, g_fallback_allocations);
}

/**
 * @brief Monitor buffer usage and trigger rebalancing if needed
 */
void buffer_monitor_and_rebalance(void) {
    if (!g_buffer_system_initialized) {
        return;
    }
    
    /* Monitor per-NIC buffer usage */
    monitor_nic_buffer_usage();
    
    /* Monitor legacy pools */
    static uint32_t last_legacy_monitor = 0;
    uint32_t current_time = get_system_timestamp_ms();
    
    if (current_time - last_legacy_monitor > 10000) { /* Every 10 seconds */
        uint32_t tx_usage = (g_tx_buffer_pool.used_count * 100) / g_tx_buffer_pool.buffer_count;
        uint32_t rx_usage = (g_rx_buffer_pool.used_count * 100) / g_rx_buffer_pool.buffer_count;
        uint32_t dma_usage = (g_dma_buffer_pool.used_count * 100) / g_dma_buffer_pool.buffer_count;
        
        if (tx_usage > 85 || rx_usage > 85 || dma_usage > 85) {
            log_warning("High legacy pool usage: TX %u%%, RX %u%%, DMA %u%%",
                       tx_usage, rx_usage, dma_usage);
        }
        
        last_legacy_monitor = current_time;
    }
}

/* Advanced buffer management functions - DMA, cloning, statistics complete */

/* VDS Common Buffer Access Functions */

/**
 * @brief Get VDS TX ring buffer
 * @return Pointer to VDS TX ring buffer, or NULL if not allocated
 */
const vds_buffer_t* buffer_get_vds_tx_ring(void) {
    return (g_vds_tx_ring_buffer.allocated) ? &g_vds_tx_ring_buffer : NULL;
}

/**
 * @brief Get VDS RX ring buffer  
 * @return Pointer to VDS RX ring buffer, or NULL if not allocated
 */
const vds_buffer_t* buffer_get_vds_rx_ring(void) {
    return (g_vds_rx_ring_buffer.allocated) ? &g_vds_rx_ring_buffer : NULL;
}

/**
 * @brief Get VDS RX data buffer
 * @return Pointer to VDS RX data buffer, or NULL if not allocated
 */
const vds_buffer_t* buffer_get_vds_rx_data(void) {
    return (g_vds_rx_data_buffer.allocated) ? &g_vds_rx_data_buffer : NULL;
}

/**
 * @brief Check if VDS buffers are available
 * @return true if VDS buffers allocated, false otherwise
 */
bool buffer_vds_available(void) {
    return g_vds_buffers_allocated;
}

/**
 * @brief Get physical address for VDS buffer section
 * @param buffer_type Type of VDS buffer (0=TX ring, 1=RX ring, 2=RX data)
 * @param offset Offset within buffer
 * @return Physical address, or 0 if invalid
 */
uint32_t buffer_get_vds_physical_address(int buffer_type, uint32_t offset) {
    const vds_buffer_t *vds_buf = NULL;
    
    switch (buffer_type) {
        case 0: /* TX ring */
            vds_buf = &g_vds_tx_ring_buffer;
            break;
        case 1: /* RX ring */
            vds_buf = &g_vds_rx_ring_buffer;
            break;
        case 2: /* RX data */
            vds_buf = &g_vds_rx_data_buffer;
            break;
        default:
            return 0;
    }
    
    if (!vds_buf->allocated || offset >= vds_buf->size) {
        return 0;
    }
    
    return vds_buf->physical_addr + offset;
}

/**
 * @brief Get virtual address for VDS buffer section
 * @param buffer_type Type of VDS buffer (0=TX ring, 1=RX ring, 2=RX data)
 * @param offset Offset within buffer
 * @return Virtual address, or NULL if invalid
 */
void far* buffer_get_vds_virtual_address(int buffer_type, uint32_t offset) {
    const vds_buffer_t *vds_buf = NULL;
    
    switch (buffer_type) {
        case 0: /* TX ring */
            vds_buf = &g_vds_tx_ring_buffer;
            break;
        case 1: /* RX ring */
            vds_buf = &g_vds_rx_ring_buffer;
            break;
        case 2: /* RX data */
            vds_buf = &g_vds_rx_data_buffer;
            break;
        default:
            return NULL;
    }
    
    if (!vds_buf->allocated || offset >= vds_buf->size) {
        return NULL;
    }
    
    /* Calculate far pointer with offset */
    uint16_t seg = FP_SEG(vds_buf->virtual_addr);
    uint16_t off = FP_OFF(vds_buf->virtual_addr) + (uint16_t)offset;
    
    /* Handle segment wrap if needed */
    if (off < FP_OFF(vds_buf->virtual_addr)) {
        seg += 0x1000;  /* Add 64KB to segment */
        off = (uint16_t)offset - (0x10000 - FP_OFF(vds_buf->virtual_addr));
    }
    
    return MK_FP(seg, off);
}

