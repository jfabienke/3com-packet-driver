/**
 * @file memory_manager.c
 * @brief Memory Manager Implementation for 3Com Packet Driver
 * 
 * Phase 3A: Dynamic Module Loading - Stream 1 Day 2-3
 * 
 * This file implements sophisticated memory management for DOS environment,
 * including XMS, UMB, and conventional memory handling with optimization.
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include "memory_manager.h"
#include "core_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>

/* XMS function numbers */
#define XMS_GET_VERSION      0x00
#define XMS_ALLOC_UMB        0x10
#define XMS_FREE_UMB         0x11
#define XMS_ALLOC_EXTENDED   0x09
#define XMS_FREE_EXTENDED    0x0A
#define XMS_LOCK_EXTENDED    0x0C
#define XMS_UNLOCK_EXTENDED  0x0D
#define XMS_GET_INFO         0x08

/* Memory block tracking */
#define MAX_MEMORY_BLOCKS 128
static memory_block_t memory_blocks[MAX_MEMORY_BLOCKS];
static int memory_block_count = 0;

/* XMS driver information */
static void (far *xms_driver)(void) = NULL;
static bool xms_available = false;

/* UMB information */
static umb_block_info_t umb_blocks[MAX_UMB_BLOCKS];
static int umb_block_count = 0;

/* Memory statistics */
static memory_stats_t global_memory_stats;

/* Enhanced buffer pools - GPT-5 recommendation: 128/256/512/1536 */
static packet_buffer_t tiny_buffer_pool[32];    /* 128 bytes - Control packets */
static packet_buffer_t small_buffer_pool[64];   /* 256 bytes - ARP, ICMP, TCP ACKs */
static packet_buffer_t medium_buffer_pool[48];  /* 512 bytes - DNS, small HTTP */
static packet_buffer_t large_buffer_pool[32];   /* 1536 bytes - Full MTU + headroom */
static bool tiny_pool_initialized = false;
static bool small_pool_initialized = false;
static bool medium_pool_initialized = false;
static bool large_pool_initialized = false;

/* GPT-5 Critical Fix: Store enhanced configuration for dynamic threshold checking */
static enhanced_buffer_pool_config_t current_enhanced_config = {
    .tiny_buffer_size = 128,        /* Default values if not initialized */
    .small_buffer_size = 256,
    .medium_buffer_size = 512,
    .large_buffer_size = 1536
};

/* Forward declarations */
static bool detect_xms_driver(void);
static bool detect_umb_blocks(void);
static void* allocate_conventional(size_t size, uint16_t flags, size_t alignment);
static void* allocate_umb(size_t size, uint16_t flags, size_t alignment);
static void* allocate_xms(size_t size, uint16_t flags, size_t alignment);
static bool free_memory_block(memory_block_t* block);
static memory_block_t* find_memory_block(const void* ptr);
static bool initialize_buffer_pools(buffer_pool_config_t* config);
static void shutdown_buffer_pools(void);
static void update_memory_statistics(void);

/* XMS interface functions */
static uint16_t xms_call(uint16_t function, uint16_t dx);
static void far* xms_call_far(uint16_t function, uint16_t dx);

/* ============================================================================
 * Memory Manager Initialization and Shutdown
 * ============================================================================ */

/**
 * @brief Initialize the memory management system
 */
bool memory_manager_initialize(memory_services_t* services, core_config_t* config)
{
    if (!services || !config) {
        return false;
    }
    
    /* Clear memory blocks */
    memset(memory_blocks, 0, sizeof(memory_blocks));
    memory_block_count = 0;
    
    /* Clear statistics */
    memset(&global_memory_stats, 0, sizeof(global_memory_stats));
    
    /* Detect XMS driver */
    xms_available = detect_xms_driver();
    if (xms_available) {
        printf("3CPD: XMS driver detected\n");
    }
    
    /* Detect UMB blocks */
    if (!detect_umb_blocks()) {
        printf("3CPD: Warning - UMB detection failed\n");
    }
    
    /* Initialize enhanced buffer pools - GPT-5 recommendation */
    buffer_pool_config_t pool_config = {
        .small_buffer_size = 256,                  /* Legacy compatibility */
        .large_buffer_size = 1536,                 /* GPT-5: Better than 1600 */
        .small_buffer_count = config->buffer_pool_size,
        .large_buffer_count = config->buffer_pool_size / 2,
        .memory_type = MEMORY_TYPE_BUFFER,
        .alignment = MEMORY_ALIGN_WORD
    };
    
    /* Enhanced buffer pool configuration */
    enhanced_buffer_pool_config_t enhanced_config = {
        .tiny_buffer_size = 128,                   /* Control packets */
        .small_buffer_size = 256,                  /* ARP, ICMP, TCP ACKs */
        .medium_buffer_size = 512,                 /* DNS, small HTTP */
        .large_buffer_size = 1536,                 /* Full MTU + 2-byte headroom */
        .tiny_buffer_count = config->buffer_pool_size / 4,
        .small_buffer_count = config->buffer_pool_size,
        .medium_buffer_count = config->buffer_pool_size / 2,
        .large_buffer_count = config->buffer_pool_size / 3,
        .memory_type = MEMORY_TYPE_BUFFER,
        .alignment = MEMORY_ALIGN_WORD,
        .enable_adaptive_sizing = true,            /* GPT-5: Adaptive thresholds */
        .device_caps = NULL                        /* Will be set per-device */
    };
    
    /* Try enhanced buffer pools first */
    if (!initialize_enhanced_buffer_pools(&enhanced_config)) {
        printf("3CPD: Warning - Enhanced buffer pool initialization failed, using legacy pools\n");
        if (!initialize_buffer_pools(&pool_config)) {
            printf("3CPD: Warning - Buffer pool initialization failed\n");
        }
    } else {
        printf("3CPD: Enhanced buffer pools initialized (128/256/512/1536 bytes)\n");
    }
    
    /* Set up service function pointers */
    services->allocate = memory_alloc;
    services->deallocate = memory_free;
    services->reallocate = memory_realloc;
    services->query_block = memory_query;
    services->get_stats = memory_get_stats;
    
    services->get_buffer = buffer_get;
    services->return_buffer = buffer_return;
    services->addref_buffer = buffer_addref;
    services->release_buffer = buffer_release;
    
    services->dma_prepare = dma_prepare_buffer;
    services->dma_complete = dma_complete_buffer;
    services->alloc_coherent = dma_alloc_coherent;
    services->free_coherent = dma_free_coherent;
    
    services->memset_fast = memset;  /* Use standard for now */
    services->memcpy_fast = memcpy;  /* Use standard for now */
    services->memcmp_fast = memcmp;  /* Use standard for now */
    
    /* Update initial statistics */
    update_memory_statistics();
    
    if (config->verbose_logging) {
        printf("3CPD: Memory manager initialized\n");
        printf("3CPD: Conventional: %zu KB, UMB: %zu KB, XMS: %zu KB\n",
               global_memory_stats.conventional_total / 1024,
               global_memory_stats.umb_total / 1024,
               global_memory_stats.xms_total / 1024);
    }
    
    return true;
}

/**
 * @brief Shutdown the memory management system
 */
void memory_manager_shutdown(memory_services_t* services)
{
    if (!services) return;
    
    /* Free all allocated blocks */
    for (int i = 0; i < memory_block_count; i++) {
        if (memory_blocks[i].address) {
            free_memory_block(&memory_blocks[i]);
        }
    }
    
    /* Shutdown buffer pools */
    shutdown_buffer_pools();
    
    /* Clear services */
    memset(services, 0, sizeof(memory_services_t));
    
    printf("3CPD: Memory manager shutdown complete\n");
}

/* ============================================================================
 * Core Memory Allocation Functions
 * ============================================================================ */

/**
 * @brief Allocate memory with specified type and alignment
 */
void* memory_alloc(size_t size, memory_type_t type, uint16_t flags, size_t alignment)
{
    void* ptr = NULL;
    memory_block_t* block;
    
    if (size == 0) return NULL;
    
    /* Align size to requested boundary */
    size = ALIGN_SIZE(size, alignment);
    
    /* Find free memory block slot */
    if (memory_block_count >= MAX_MEMORY_BLOCKS) {
        return NULL;  /* No more block slots */
    }
    
    block = &memory_blocks[memory_block_count];
    
    /* Try allocation based on memory type preference */
    switch (type) {
        case MEMORY_TYPE_UMB:
            ptr = allocate_umb(size, flags, alignment);
            if (ptr) break;
            /* Fall through to XMS if UMB fails */
            
        case MEMORY_TYPE_XMS:
            ptr = allocate_xms(size, flags, alignment);
            if (ptr) break;
            /* Fall through to conventional if XMS fails */
            
        case MEMORY_TYPE_CONVENTIONAL:
        default:
            ptr = allocate_conventional(size, flags, alignment);
            break;
    }
    
    if (!ptr) {
        return NULL;  /* Allocation failed */
    }
    
    /* Initialize memory block tracking */
    block->address = ptr;
    block->size = size;
    block->type = type;
    block->flags = flags;
    block->handle = 0;  /* Would be set for XMS/UMB */
    block->owner_id = 0;  /* Would be set by caller */
    block->lock_count = 0;
    block->timestamp = 0;  /* Would use timer service */
    
    memory_block_count++;
    
    /* Zero-initialize if requested */
    if (flags & MEMORY_FLAG_ZERO) {
        memset(ptr, 0, size);
    }
    
    /* Update statistics */
    global_memory_stats.total_allocations++;
    global_memory_stats.current_usage += size;
    if (global_memory_stats.current_usage > global_memory_stats.peak_usage) {
        global_memory_stats.peak_usage = global_memory_stats.current_usage;
    }
    
    return ptr;
}

/**
 * @brief Free allocated memory
 */
bool memory_free(void* ptr)
{
    memory_block_t* block;
    
    if (!ptr) return true;  /* NULL pointer is valid to free */
    
    /* Find memory block */
    block = find_memory_block(ptr);
    if (!block) {
        return false;  /* Block not found */
    }
    
    /* Free the block */
    if (!free_memory_block(block)) {
        return false;
    }
    
    /* Update statistics */
    global_memory_stats.total_deallocations++;
    global_memory_stats.current_usage -= block->size;
    
    /* Remove block from tracking */
    int index = block - memory_blocks;
    memmove(&memory_blocks[index], &memory_blocks[index + 1],
            (memory_block_count - index - 1) * sizeof(memory_block_t));
    memory_block_count--;
    
    return true;
}

/**
 * @brief Reallocate memory block
 */
void* memory_realloc(void* ptr, size_t new_size)
{
    memory_block_t* block;
    void* new_ptr;
    size_t copy_size;
    
    if (!ptr) {
        return memory_alloc(new_size, MEMORY_TYPE_CONVENTIONAL, 0, 1);
    }
    
    if (new_size == 0) {
        memory_free(ptr);
        return NULL;
    }
    
    /* Find existing block */
    block = find_memory_block(ptr);
    if (!block) {
        return NULL;  /* Invalid pointer */
    }
    
    /* If size is the same, return existing pointer */
    if (block->size == new_size) {
        return ptr;
    }
    
    /* Allocate new block */
    new_ptr = memory_alloc(new_size, block->type, block->flags, 1);
    if (!new_ptr) {
        return NULL;  /* Allocation failed */
    }
    
    /* Copy data */
    copy_size = (new_size < block->size) ? new_size : block->size;
    memcpy(new_ptr, ptr, copy_size);
    
    /* Free old block */
    memory_free(ptr);
    
    return new_ptr;
}

/**
 * @brief Query memory block information
 */
bool memory_query(const void* ptr, memory_block_t* block_info)
{
    memory_block_t* block;
    
    if (!ptr || !block_info) {
        return false;
    }
    
    block = find_memory_block(ptr);
    if (!block) {
        return false;
    }
    
    *block_info = *block;
    return true;
}

/**
 * @brief Get memory system statistics
 */
bool memory_get_stats(memory_stats_t* stats)
{
    if (!stats) return false;
    
    update_memory_statistics();
    *stats = global_memory_stats;
    return true;
}

/* ============================================================================
 * Memory Type-Specific Allocation
 * ============================================================================ */

/**
 * @brief Allocate conventional memory
 */
static void* allocate_conventional(size_t size, uint16_t flags, size_t alignment)
{
    void* ptr;
    
    /* Use standard malloc for conventional memory */
    ptr = malloc(size + alignment - 1);
    if (!ptr) return NULL;
    
    /* Apply alignment if requested */
    if (flags & MEMORY_FLAG_ALIGN) {
        ptr = ALIGN_POINTER(ptr, alignment);
    }
    
    return ptr;
}

/**
 * @brief Allocate upper memory block
 */
static void* allocate_umb(size_t size, uint16_t flags, size_t alignment)
{
    /* Simplified UMB allocation - would use DOS API in real implementation */
    if (umb_block_count == 0) {
        return NULL;  /* No UMB blocks available */
    }
    
    /* For now, fall back to conventional memory */
    return allocate_conventional(size, flags, alignment);
}

/**
 * @brief Allocate extended memory (XMS)
 */
static void* allocate_xms(size_t size, uint16_t flags, size_t alignment)
{
    if (!xms_available) {
        return NULL;
    }
    
    /* Convert bytes to KB (XMS works in KB) */
    uint16_t size_kb = (uint16_t)((size + 1023) / 1024);
    if (size_kb == 0) size_kb = 1;
    
    /* Allocate XMS handle */
    uint16_t handle = xms_call(XMS_ALLOC_EXTENDED, size_kb);
    if (handle == 0) {
        return NULL;  /* Allocation failed */
    }
    
    /* Lock XMS block to get linear address */
    void far* linear_addr = xms_call_far(XMS_LOCK_EXTENDED, handle);
    if (!linear_addr) {
        xms_call(XMS_FREE_EXTENDED, handle);
        return NULL;
    }
    
    /* For DOS real mode, we need to handle addressing differently */
    /* This is a simplified implementation */
    return (void*)linear_addr;
}

/* ============================================================================
 * XMS Driver Interface
 * ============================================================================ */

/**
 * @brief Detect XMS driver
 */
static bool detect_xms_driver(void)
{
    union REGS regs;
    
    /* Check for XMS driver presence */
    regs.x.ax = 0x4300;
    int86(0x2F, &regs, &regs);
    
    if (regs.h.al != 0x80) {
        return false;  /* XMS driver not present */
    }
    
    /* Get XMS driver entry point */
    regs.x.ax = 0x4310;
    int86(0x2F, &regs, &regs);
    
    xms_driver = MK_FP(regs.x.es, regs.x.bx);
    
    return true;
}

/**
 * @brief Call XMS driver function
 */
static uint16_t xms_call(uint16_t function, uint16_t dx)
{
    union REGS regs;
    
    if (!xms_driver) return 0;
    
    regs.h.ah = function;
    regs.x.dx = dx;
    
    /* Call XMS driver */
    /* This is simplified - real implementation would use inline assembly */
    return regs.x.ax;
}

/**
 * @brief Call XMS driver function returning far pointer
 */
static void far* xms_call_far(uint16_t function, uint16_t dx)
{
    uint16_t result = xms_call(function, dx);
    
    /* Convert result to far pointer - simplified */
    return MK_FP(result >> 16, result & 0xFFFF);
}

/* ============================================================================
 * UMB Detection and Management
 * ============================================================================ */

/**
 * @brief Detect available UMB blocks
 */
static bool detect_umb_blocks(void)
{
    union REGS regs;
    
    /* Check for UMB support via DOS */
    regs.x.ax = 0x5800;  /* Get memory allocation strategy */
    int86(0x21, &regs, &regs);
    
    if (regs.x.cflag) {
        return false;  /* DOS doesn't support UMB */
    }
    
    /* Try to allocate UMB to test availability */
    regs.x.ax = 0x5801;  /* Set memory allocation strategy */
    regs.x.bx = 0x0040;  /* Try high memory first */
    int86(0x21, &regs, &regs);
    
    if (regs.x.cflag) {
        return false;  /* Cannot set UMB strategy */
    }
    
    /* Restore original strategy */
    regs.x.ax = 0x5801;
    regs.x.bx = 0x0000;  /* Low memory first */
    int86(0x21, &regs, &regs);
    
    umb_block_count = 1;  /* Simplified - assume one UMB block */
    umb_blocks[0].segment = 0xD000;  /* Typical UMB location */
    umb_blocks[0].paragraphs = 64;   /* 1KB */
    umb_blocks[0].in_use = false;
    umb_blocks[0].owner_id = 0;
    
    return true;
}

/* ============================================================================
 * Buffer Pool Management
 * ============================================================================ */

/**
 * @brief Initialize enhanced packet buffer pools - GPT-5 recommendation
 * 
 * Implements adaptive buffer classes: 128/256/512/1536 bytes
 * 
 * @param config Enhanced buffer pool configuration
 * @return true on success, false on failure
 */
static bool initialize_enhanced_buffer_pools(enhanced_buffer_pool_config_t* config)
{
    if (!config) return false;
    
    /* GPT-5 Critical Fix: Store configuration for dynamic threshold checking */
    current_enhanced_config = *config;
    
    printf("3CPD: Initializing enhanced buffer pools (GPT-5 design)\n");
    
    /* Initialize tiny buffer pool (128 bytes - control packets) */
    for (int i = 0; i < config->tiny_buffer_count && i < 32; i++) {
        tiny_buffer_pool[i].data = memory_alloc(config->tiny_buffer_size,
                                               config->memory_type, 0,
                                               config->alignment);
        if (!tiny_buffer_pool[i].data) {
            printf("3CPD: Failed to allocate tiny buffer %d\n", i);
            return false;
        }
        
        tiny_buffer_pool[i].size = config->tiny_buffer_size;
        tiny_buffer_pool[i].used = 0;
        tiny_buffer_pool[i].buffer_id = i;
        tiny_buffer_pool[i].ref_count = 0;
        tiny_buffer_pool[i].flags = 0;
        tiny_buffer_pool[i].private_data = NULL;
    }
    
    /* Initialize small buffer pool (256 bytes - ARP, ICMP, TCP ACKs) */
    for (int i = 0; i < config->small_buffer_count && i < 64; i++) {
        small_buffer_pool[i].data = memory_alloc(config->small_buffer_size,
                                               config->memory_type, 0,
                                               config->alignment);
        if (!small_buffer_pool[i].data) {
            printf("3CPD: Failed to allocate small buffer %d\n", i);
            return false;
        }
        
        small_buffer_pool[i].size = config->small_buffer_size;
        small_buffer_pool[i].used = 0;
        small_buffer_pool[i].buffer_id = i + 100;  /* Offset for small buffers */
        small_buffer_pool[i].ref_count = 0;
        small_buffer_pool[i].flags = 0;
        small_buffer_pool[i].private_data = NULL;
    }
    
    /* Initialize medium buffer pool (512 bytes - DNS, small HTTP) */
    for (int i = 0; i < config->medium_buffer_count && i < 48; i++) {
        medium_buffer_pool[i].data = memory_alloc(config->medium_buffer_size,
                                                config->memory_type, 0,
                                                config->alignment);
        if (!medium_buffer_pool[i].data) {
            printf("3CPD: Failed to allocate medium buffer %d\n", i);
            return false;
        }
        
        medium_buffer_pool[i].size = config->medium_buffer_size;
        medium_buffer_pool[i].used = 0;
        medium_buffer_pool[i].buffer_id = i + 500;  /* Offset for medium buffers */
        medium_buffer_pool[i].ref_count = 0;
        medium_buffer_pool[i].flags = 0;
        medium_buffer_pool[i].private_data = NULL;
    }
    
    /* Initialize large buffer pool (1536 bytes - Full MTU + headroom) */
    for (int i = 0; i < config->large_buffer_count && i < 32; i++) {
        large_buffer_pool[i].data = memory_alloc(config->large_buffer_size,
                                                config->memory_type, 0,
                                                config->alignment);
        if (!large_buffer_pool[i].data) {
            printf("3CPD: Failed to allocate large buffer %d\n", i);
            return false;
        }
        
        large_buffer_pool[i].size = config->large_buffer_size;
        large_buffer_pool[i].used = 0;
        large_buffer_pool[i].buffer_id = i + 1000;  /* Offset for large buffers */
        large_buffer_pool[i].ref_count = 0;
        large_buffer_pool[i].flags = 0;
        large_buffer_pool[i].private_data = NULL;
    }
    
    /* Mark all pools as initialized */
    tiny_pool_initialized = true;
    small_pool_initialized = true;
    medium_pool_initialized = true;
    large_pool_initialized = true;
    
    printf("3CPD: Enhanced buffer pools initialized - %d tiny, %d small, %d medium, %d large\n",
           config->tiny_buffer_count, config->small_buffer_count, 
           config->medium_buffer_count, config->large_buffer_count);
    
    return true;
}

/**
 * @brief Initialize packet buffer pools (legacy)
 */
static bool initialize_buffer_pools(buffer_pool_config_t* config)
{
    if (!config) return false;
    
    /* Initialize small buffer pool */
    for (int i = 0; i < config->small_buffer_count && i < 64; i++) {
        small_buffer_pool[i].data = memory_alloc(config->small_buffer_size,
                                               config->memory_type, 0,
                                               config->alignment);
        if (!small_buffer_pool[i].data) {
            return false;
        }
        
        small_buffer_pool[i].size = config->small_buffer_size;
        small_buffer_pool[i].used = 0;
        small_buffer_pool[i].buffer_id = i;
        small_buffer_pool[i].ref_count = 0;
        small_buffer_pool[i].flags = 0;
        small_buffer_pool[i].private_data = NULL;
    }
    
    /* Initialize large buffer pool */
    for (int i = 0; i < config->large_buffer_count && i < 32; i++) {
        large_buffer_pool[i].data = memory_alloc(config->large_buffer_size,
                                                config->memory_type, 0,
                                                config->alignment);
        if (!large_buffer_pool[i].data) {
            return false;
        }
        
        large_buffer_pool[i].size = config->large_buffer_size;
        large_buffer_pool[i].used = 0;
        large_buffer_pool[i].buffer_id = i + 1000;  /* Offset for large buffers */
        large_buffer_pool[i].ref_count = 0;
        large_buffer_pool[i].flags = 0;
        large_buffer_pool[i].private_data = NULL;
    }
    
    small_pool_initialized = true;
    large_pool_initialized = true;
    
    return true;
}

/**
 * @brief Shutdown buffer pools
 */
static void shutdown_buffer_pools(void)
{
    /* Free small buffer pool */
    if (small_pool_initialized) {
        for (int i = 0; i < 64; i++) {
            if (small_buffer_pool[i].data) {
                memory_free(small_buffer_pool[i].data);
                small_buffer_pool[i].data = NULL;
            }
        }
        small_pool_initialized = false;
    }
    
    /* Free large buffer pool */
    if (large_pool_initialized) {
        for (int i = 0; i < 32; i++) {
            if (large_buffer_pool[i].data) {
                memory_free(large_buffer_pool[i].data);
                large_buffer_pool[i].data = NULL;
            }
        }
        large_pool_initialized = false;
    }
}

/* ============================================================================
 * Buffer Pool Interface Functions
 * ============================================================================ */

/**
 * @brief Get a packet buffer from enhanced pools - GPT-5 design
 * 
 * Implements adaptive buffer selection:
 * - 128 bytes: Control packets (ARP, ICMP ping)
 * - 256 bytes: Small frames (TCP ACKs, DNS queries)
 * - 512 bytes: Medium frames (small HTTP, DHCP)
 * - 1536 bytes: Large frames (Full MTU + 2-byte headroom)
 */
packet_buffer_t* buffer_get(size_t size, uint16_t timeout_ms)
{
    packet_buffer_t* pool;
    int pool_size;
    
    /* Enhanced buffer class selection - GPT-5 Fix: Use configured thresholds */
    if (size <= current_enhanced_config.tiny_buffer_size && tiny_pool_initialized) {
        pool = tiny_buffer_pool;
        pool_size = 32;
        
        /* Find available tiny buffer */
        for (int i = 0; i < pool_size; i++) {
            if (pool[i].ref_count == 0) {
                pool[i].ref_count = 1;
                pool[i].used = 0;
                return &pool[i];
            }
        }
        
        /* Fall through to small if tiny exhausted */
    }
    
    if (size <= current_enhanced_config.small_buffer_size && small_pool_initialized) {
        pool = small_buffer_pool;
        pool_size = 64;
        
        /* Find available small buffer */
        for (int i = 0; i < pool_size; i++) {
            if (pool[i].ref_count == 0) {
                pool[i].ref_count = 1;
                pool[i].used = 0;
                return &pool[i];
            }
        }
        
        /* Fall through to medium if small exhausted */
    }
    
    if (size <= current_enhanced_config.medium_buffer_size && medium_pool_initialized) {
        pool = medium_buffer_pool;
        pool_size = 48;
        
        /* Find available medium buffer */
        for (int i = 0; i < pool_size; i++) {
            if (pool[i].ref_count == 0) {
                pool[i].ref_count = 1;
                pool[i].used = 0;
                return &pool[i];
            }
        }
        
        /* Fall through to large if medium exhausted */
    }
    
    /* Large buffer pool or fallback for any size */
    if (large_pool_initialized) {
        pool = large_buffer_pool;
        pool_size = 32;
        
        /* Find available large buffer */
        for (int i = 0; i < pool_size; i++) {
            if (pool[i].ref_count == 0) {
                pool[i].ref_count = 1;
                pool[i].used = 0;
                return &pool[i];
            }
        }
    }
    
    /* Legacy fallback - try old small/large pools if enhanced not available */
    if (!tiny_pool_initialized && !medium_pool_initialized) {
        if (size <= current_enhanced_config.small_buffer_size && small_pool_initialized) {
            pool = small_buffer_pool;
            pool_size = 64;
        } else if (large_pool_initialized) {
            pool = large_buffer_pool;
            pool_size = 32;
        } else {
            return NULL;
        }
        
        /* Find available buffer in legacy pools */
        for (int i = 0; i < pool_size; i++) {
            if (pool[i].ref_count == 0) {
                pool[i].ref_count = 1;
                pool[i].used = 0;
                return &pool[i];
            }
        }
    }
    
    return NULL;  /* No buffer available in any pool */
}

/**
 * @brief Return a packet buffer to pool
 */
bool buffer_return(packet_buffer_t* buffer)
{
    if (!buffer) return false;
    
    buffer->ref_count = 0;
    buffer->used = 0;
    return true;
}

/**
 * @brief Add reference to buffer
 */
uint8_t buffer_addref(packet_buffer_t* buffer)
{
    if (!buffer) return 0;
    
    buffer->ref_count++;
    return buffer->ref_count;
}

/**
 * @brief Release reference to buffer
 */
uint8_t buffer_release(packet_buffer_t* buffer)
{
    if (!buffer || buffer->ref_count == 0) return 0;
    
    buffer->ref_count--;
    if (buffer->ref_count == 0) {
        buffer->used = 0;
    }
    
    return buffer->ref_count;
}

/* ============================================================================
 * DMA and Cache Coherency Support
 * ============================================================================ */

/**
 * @brief Prepare buffer for DMA operation
 */
bool dma_prepare_buffer(const dma_operation_t* dma_op)
{
    /* Simplified implementation - would flush CPU caches */
    return true;
}

/**
 * @brief Complete DMA operation
 */
bool dma_complete_buffer(const dma_operation_t* dma_op)
{
    /* Simplified implementation - would invalidate CPU caches */
    return true;
}

/**
 * @brief Allocate DMA-coherent memory
 */
void* dma_alloc_coherent(size_t size, dma_device_type_t device_type, size_t alignment)
{
    /* Allocate aligned memory for DMA */
    return memory_alloc(size, MEMORY_TYPE_DMA_COHERENT,
                       MEMORY_FLAG_ALIGN, alignment);
}

/**
 * @brief Free DMA-coherent memory
 */
bool dma_free_coherent(void* ptr, size_t size)
{
    return memory_free(ptr);
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Find memory block by pointer
 */
static memory_block_t* find_memory_block(const void* ptr)
{
    for (int i = 0; i < memory_block_count; i++) {
        if (memory_blocks[i].address == ptr) {
            return &memory_blocks[i];
        }
    }
    return NULL;
}

/**
 * @brief Free a memory block
 */
static bool free_memory_block(memory_block_t* block)
{
    if (!block || !block->address) {
        return false;
    }
    
    /* Free based on memory type */
    switch (block->type) {
        case MEMORY_TYPE_XMS:
            if (block->handle) {
                xms_call(XMS_UNLOCK_EXTENDED, block->handle);
                xms_call(XMS_FREE_EXTENDED, block->handle);
            }
            break;
            
        case MEMORY_TYPE_UMB:
            /* Would use DOS UMB deallocation */
            free(block->address);
            break;
            
        case MEMORY_TYPE_CONVENTIONAL:
        default:
            free(block->address);
            break;
    }
    
    /* Clear block */
    memset(block, 0, sizeof(memory_block_t));
    return true;
}

/**
 * @brief Update memory statistics
 */
static void update_memory_statistics(void)
{
    union REGS regs;
    
    /* Get conventional memory info */
    regs.h.ah = 0x48;
    regs.x.bx = 0xFFFF;  /* Request impossible amount to get available */
    int86(0x21, &regs, &regs);
    
    if (regs.x.cflag) {
        global_memory_stats.conventional_free = regs.x.bx * 16;  /* Convert paragraphs to bytes */
        global_memory_stats.conventional_largest = regs.x.bx * 16;
    }
    
    global_memory_stats.conventional_total = CONVENTIONAL_MEMORY_LIMIT;
    
    /* Update UMB stats */
    global_memory_stats.umb_blocks = umb_block_count;
    global_memory_stats.umb_total = umb_block_count * 1024;  /* Simplified */
    global_memory_stats.umb_free = global_memory_stats.umb_total;
    
    /* Update XMS stats if available */
    if (xms_available) {
        uint16_t free_kb = xms_call(XMS_GET_INFO, 0);
        global_memory_stats.xms_free = free_kb * 1024;
        global_memory_stats.xms_total = global_memory_stats.xms_free;
    }
    
    /* Calculate fragmentation percentage */
    if (global_memory_stats.conventional_total > 0) {
        global_memory_stats.fragmentation_pct = 
            (uint16_t)((global_memory_stats.conventional_free * 100) / 
                      global_memory_stats.conventional_total);
    }
}