/**
 * @file bounce_buffers.c
 * @brief Bounce Buffer System for 64KB Boundary Safety
 * 
 * Agent Team B (07-08): Week 1 Implementation
 * 
 * This module implements a bounce buffer system to handle DMA buffers that
 * cross 64KB boundaries, which are problematic for ISA bus-master controllers
 * like the 3C515. When a packet buffer crosses a 64KB boundary, it is
 * automatically copied to a boundary-safe bounce buffer for DMA operations.
 * 
 * Key Features:
 * - Automatic detection of 64KB boundary crossings
 * - Pool of pre-allocated boundary-safe bounce buffers
 * - Copy-on-demand with minimal performance impact
 * - Integration with DMA ring management
 * - Support for both TX and RX operations
 * 
 * ISA DMA Constraints:
 * - Buffers cannot cross 64KB boundaries (64KB = 0x10000)
 * - Physical addresses must be below 16MB for ISA DMA
 * - Buffers must be physically contiguous
 * - Cache coherency required for consistent data
 * 
 * This file is part of the CORKSCRW.MOD module.
 * Copyright (c) 2025 3Com/Phase3A Team B
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Bounce Buffer Configuration */
#define BOUNCE_POOL_SIZE        16      /* Number of bounce buffers */
#define BOUNCE_BUFFER_SIZE      1536    /* Size of each bounce buffer */
#define DMA_BOUNDARY_SIZE       0x10000 /* 64KB boundary */
#define ISA_DMA_LIMIT           0x1000000  /* 16MB ISA DMA limit */
#define CACHE_LINE_SIZE         32      /* Typical cache line size */

/* Buffer States */
#define BOUNCE_FREE             0       /* Buffer is available */
#define BOUNCE_ALLOCATED        1       /* Buffer is allocated */
#define BOUNCE_DMA_ACTIVE       2       /* Buffer is in DMA operation */
#define BOUNCE_COPY_PENDING     3       /* Copy operation pending */

/**
 * Bounce Buffer Descriptor
 */
typedef struct {
    void *virt_addr;            /* Virtual address of bounce buffer */
    uint32_t phys_addr;         /* Physical address (DMA-safe) */
    uint16_t size;              /* Buffer size */
    uint8_t state;              /* Buffer state */
    uint8_t direction;          /* DMA direction (TX/RX) */
    
    /* Original buffer information */
    void *orig_virt_addr;       /* Original buffer virtual address */
    uint32_t orig_phys_addr;    /* Original buffer physical address */
    uint16_t orig_size;         /* Original buffer size */
    
    /* Usage tracking */
    uint32_t allocation_time;   /* Time allocated (for debugging) */
    uint16_t copy_count;        /* Number of copies performed */
} bounce_buffer_t;

/**
 * Bounce Buffer Pool
 */
typedef struct {
    bounce_buffer_t buffers[BOUNCE_POOL_SIZE];
    uint8_t initialized;        /* Pool initialization flag */
    uint16_t free_count;        /* Number of free buffers */
    uint16_t allocation_failures; /* Allocation failure count */
    uint32_t boundary_crossings;  /* Number of boundary crossings detected */
    uint32_t copies_performed;    /* Total copies performed */
    uint32_t cache_flushes;       /* Cache flush operations */
} bounce_pool_t;

/* Global bounce buffer pool */
static bounce_pool_t g_bounce_pool;

/* Forward Declarations */
static int allocate_bounce_buffers(void);
static void free_bounce_buffers(void);
static bool is_boundary_crossing(uint32_t phys_addr, uint16_t size);
static bounce_buffer_t* find_free_bounce_buffer(void);
static int copy_to_bounce_buffer(bounce_buffer_t *bounce, const void *src_data, uint16_t size);
static int copy_from_bounce_buffer(const bounce_buffer_t *bounce, void *dst_data, uint16_t size);
static void cache_flush_range(void *addr, uint16_t size);
static void cache_invalidate_range(void *addr, uint16_t size);
static uint32_t virt_to_phys(void *virt_addr);
static void *alloc_dma_safe(uint16_t size, uint32_t *phys_addr);
static void free_dma_safe(void *virt_addr, uint16_t size);

/**
 * ============================================================================
 * BOUNCE BUFFER POOL MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Initialize bounce buffer pool
 * 
 * @return 0 on success, negative error code on failure
 */
int bounce_buffers_init(void)
{
    /* Check if already initialized */
    if (g_bounce_pool.initialized) {
        return -1;  /* Already initialized */
    }
    
    /* Clear pool structure */
    memset(&g_bounce_pool, 0, sizeof(bounce_pool_t));
    
    /* Allocate bounce buffers */
    int result = allocate_bounce_buffers();
    if (result < 0) {
        return result;
    }
    
    g_bounce_pool.free_count = BOUNCE_POOL_SIZE;
    g_bounce_pool.initialized = 1;
    
    return 0;
}

/**
 * @brief Clean up bounce buffer pool
 */
void bounce_buffers_cleanup(void)
{
    if (!g_bounce_pool.initialized) {
        return;
    }
    
    /* Free all bounce buffers */
    free_bounce_buffers();
    
    /* Clear pool structure */
    memset(&g_bounce_pool, 0, sizeof(bounce_pool_t));
}

/**
 * @brief Get bounce buffer pool statistics
 * 
 * @param free_count Pointer to store free buffer count
 * @param boundary_crossings Pointer to store boundary crossing count
 * @param copies_performed Pointer to store copy count
 * @return 0 on success, negative on error
 */
int bounce_buffers_get_stats(uint16_t *free_count, uint32_t *boundary_crossings, 
                            uint32_t *copies_performed)
{
    if (!g_bounce_pool.initialized) {
        return -1;
    }
    
    if (free_count) {
        *free_count = g_bounce_pool.free_count;
    }
    
    if (boundary_crossings) {
        *boundary_crossings = g_bounce_pool.boundary_crossings;
    }
    
    if (copies_performed) {
        *copies_performed = g_bounce_pool.copies_performed;
    }
    
    return 0;
}

/**
 * ============================================================================
 * BOUNCE BUFFER OPERATIONS
 * ============================================================================
 */

/**
 * @brief Check if buffer needs bounce buffer (crosses 64KB boundary)
 * 
 * @param virt_addr Virtual address of buffer
 * @param size Size of buffer in bytes
 * @return true if bounce buffer needed, false otherwise
 */
bool bounce_buffer_needed(void *virt_addr, uint16_t size)
{
    if (!virt_addr || size == 0) {
        return false;
    }
    
    uint32_t phys_addr = virt_to_phys(virt_addr);
    return is_boundary_crossing(phys_addr, size);
}

/**
 * @brief Allocate bounce buffer for TX operation
 * 
 * @param orig_data Original packet data
 * @param size Size of packet
 * @param bounce_phys Pointer to store bounce buffer physical address
 * @return Pointer to bounce buffer virtual address, NULL on failure
 */
void* bounce_buffer_alloc_tx(const void *orig_data, uint16_t size, uint32_t *bounce_phys)
{
    if (!g_bounce_pool.initialized || !orig_data || size == 0 || 
        size > BOUNCE_BUFFER_SIZE || !bounce_phys) {
        return NULL;
    }
    
    /* Check if bounce buffer is actually needed */
    if (!bounce_buffer_needed((void*)orig_data, size)) {
        /* No bounce needed, return original addresses */
        *bounce_phys = virt_to_phys((void*)orig_data);
        return (void*)orig_data;
    }
    
    g_bounce_pool.boundary_crossings++;
    
    /* Find free bounce buffer */
    bounce_buffer_t *bounce = find_free_bounce_buffer();
    if (!bounce) {
        g_bounce_pool.allocation_failures++;
        return NULL;
    }
    
    /* Copy data to bounce buffer */
    int result = copy_to_bounce_buffer(bounce, orig_data, size);
    if (result < 0) {
        bounce->state = BOUNCE_FREE;
        g_bounce_pool.free_count++;
        return NULL;
    }
    
    /* Set up bounce buffer for TX */
    bounce->orig_virt_addr = (void*)orig_data;
    bounce->orig_phys_addr = virt_to_phys((void*)orig_data);
    bounce->orig_size = size;
    bounce->direction = 0;  /* TX direction */
    bounce->state = BOUNCE_DMA_ACTIVE;
    
    *bounce_phys = bounce->phys_addr;
    return bounce->virt_addr;
}

/**
 * @brief Allocate bounce buffer for RX operation
 * 
 * @param size Size of buffer needed
 * @param bounce_phys Pointer to store bounce buffer physical address
 * @return Pointer to bounce buffer virtual address, NULL on failure
 */
void* bounce_buffer_alloc_rx(uint16_t size, uint32_t *bounce_phys)
{
    if (!g_bounce_pool.initialized || size == 0 || 
        size > BOUNCE_BUFFER_SIZE || !bounce_phys) {
        return NULL;
    }
    
    /* Find free bounce buffer */
    bounce_buffer_t *bounce = find_free_bounce_buffer();
    if (!bounce) {
        g_bounce_pool.allocation_failures++;
        return NULL;
    }
    
    /* Set up bounce buffer for RX */
    bounce->orig_virt_addr = NULL;  /* Will be set when copying back */
    bounce->orig_phys_addr = 0;
    bounce->orig_size = size;
    bounce->direction = 1;  /* RX direction */
    bounce->state = BOUNCE_DMA_ACTIVE;
    
    *bounce_phys = bounce->phys_addr;
    return bounce->virt_addr;
}

/**
 * @brief Copy data from bounce buffer back to original buffer (RX completion)
 * 
 * @param bounce_virt Bounce buffer virtual address
 * @param orig_data Original buffer to copy to
 * @param size Size of data to copy
 * @return 0 on success, negative on error
 */
int bounce_buffer_copy_rx(void *bounce_virt, void *orig_data, uint16_t size)
{
    if (!g_bounce_pool.initialized || !bounce_virt || !orig_data || size == 0) {
        return -1;
    }
    
    /* Find bounce buffer by virtual address */
    bounce_buffer_t *bounce = NULL;
    for (int i = 0; i < BOUNCE_POOL_SIZE; i++) {
        if (g_bounce_pool.buffers[i].virt_addr == bounce_virt &&
            g_bounce_pool.buffers[i].state == BOUNCE_DMA_ACTIVE &&
            g_bounce_pool.buffers[i].direction == 1) {  /* RX */
            bounce = &g_bounce_pool.buffers[i];
            break;
        }
    }
    
    if (!bounce) {
        return -2;  /* Bounce buffer not found */
    }
    
    /* Copy data from bounce buffer to original buffer */
    int result = copy_from_bounce_buffer(bounce, orig_data, size);
    if (result < 0) {
        return result;
    }
    
    /* Mark bounce buffer as free */
    bounce->state = BOUNCE_FREE;
    bounce->orig_virt_addr = NULL;
    bounce->orig_phys_addr = 0;
    g_bounce_pool.free_count++;
    
    return 0;
}

/**
 * @brief Free bounce buffer (TX completion)
 * 
 * @param bounce_virt Bounce buffer virtual address
 * @return 0 on success, negative on error
 */
int bounce_buffer_free(void *bounce_virt)
{
    if (!g_bounce_pool.initialized || !bounce_virt) {
        return -1;
    }
    
    /* Find bounce buffer by virtual address */
    bounce_buffer_t *bounce = NULL;
    for (int i = 0; i < BOUNCE_POOL_SIZE; i++) {
        if (g_bounce_pool.buffers[i].virt_addr == bounce_virt &&
            g_bounce_pool.buffers[i].state == BOUNCE_DMA_ACTIVE) {
            bounce = &g_bounce_pool.buffers[i];
            break;
        }
    }
    
    if (!bounce) {
        return -2;  /* Bounce buffer not found */
    }
    
    /* Mark bounce buffer as free */
    bounce->state = BOUNCE_FREE;
    bounce->orig_virt_addr = NULL;
    bounce->orig_phys_addr = 0;
    bounce->direction = 0;
    g_bounce_pool.free_count++;
    
    return 0;
}

/**
 * ============================================================================
 * INTERNAL IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Allocate all bounce buffers in the pool
 */
static int allocate_bounce_buffers(void)
{
    for (int i = 0; i < BOUNCE_POOL_SIZE; i++) {
        bounce_buffer_t *bounce = &g_bounce_pool.buffers[i];
        
        /* Allocate DMA-safe buffer */
        bounce->virt_addr = alloc_dma_safe(BOUNCE_BUFFER_SIZE, &bounce->phys_addr);
        if (!bounce->virt_addr) {
            /* Free previously allocated buffers */
            for (int j = 0; j < i; j++) {
                free_dma_safe(g_bounce_pool.buffers[j].virt_addr, BOUNCE_BUFFER_SIZE);
            }
            return -1;
        }
        
        /* Verify buffer is boundary-safe */
        if (is_boundary_crossing(bounce->phys_addr, BOUNCE_BUFFER_SIZE)) {
            /* This shouldn't happen with proper allocation */
            free_dma_safe(bounce->virt_addr, BOUNCE_BUFFER_SIZE);
            
            /* Free previously allocated buffers */
            for (int j = 0; j < i; j++) {
                free_dma_safe(g_bounce_pool.buffers[j].virt_addr, BOUNCE_BUFFER_SIZE);
            }
            return -2;
        }
        
        /* Initialize bounce buffer */
        bounce->size = BOUNCE_BUFFER_SIZE;
        bounce->state = BOUNCE_FREE;
        bounce->direction = 0;
        bounce->orig_virt_addr = NULL;
        bounce->orig_phys_addr = 0;
        bounce->orig_size = 0;
        bounce->allocation_time = 0;
        bounce->copy_count = 0;
    }
    
    return 0;
}

/**
 * @brief Free all bounce buffers in the pool
 */
static void free_bounce_buffers(void)
{
    for (int i = 0; i < BOUNCE_POOL_SIZE; i++) {
        bounce_buffer_t *bounce = &g_bounce_pool.buffers[i];
        
        if (bounce->virt_addr) {
            free_dma_safe(bounce->virt_addr, BOUNCE_BUFFER_SIZE);
            bounce->virt_addr = NULL;
        }
    }
}

/**
 * @brief Check if buffer crosses 64KB boundary
 */
static bool is_boundary_crossing(uint32_t phys_addr, uint16_t size)
{
    if (size == 0) {
        return false;
    }
    
    uint32_t start_boundary = phys_addr >> 16;      /* 64KB boundary number */
    uint32_t end_boundary = (phys_addr + size - 1) >> 16;
    
    return (start_boundary != end_boundary);
}

/**
 * @brief Find a free bounce buffer
 */
static bounce_buffer_t* find_free_bounce_buffer(void)
{
    for (int i = 0; i < BOUNCE_POOL_SIZE; i++) {
        bounce_buffer_t *bounce = &g_bounce_pool.buffers[i];
        
        if (bounce->state == BOUNCE_FREE) {
            bounce->state = BOUNCE_ALLOCATED;
            g_bounce_pool.free_count--;
            return bounce;
        }
    }
    
    return NULL;  /* No free buffers */
}

/**
 * @brief Copy data to bounce buffer
 */
static int copy_to_bounce_buffer(bounce_buffer_t *bounce, const void *src_data, uint16_t size)
{
    if (!bounce || !src_data || size == 0 || size > bounce->size) {
        return -1;
    }
    
    /* Invalidate cache before reading source data */
    cache_invalidate_range((void*)src_data, size);
    
    /* Copy data */
    memcpy(bounce->virt_addr, src_data, size);
    
    /* Flush cache after writing to bounce buffer */
    cache_flush_range(bounce->virt_addr, size);
    
    bounce->copy_count++;
    g_bounce_pool.copies_performed++;
    
    return 0;
}

/**
 * @brief Copy data from bounce buffer
 */
static int copy_from_bounce_buffer(const bounce_buffer_t *bounce, void *dst_data, uint16_t size)
{
    if (!bounce || !dst_data || size == 0 || size > bounce->size) {
        return -1;
    }
    
    /* Invalidate cache before reading bounce buffer */
    cache_invalidate_range(bounce->virt_addr, size);
    
    /* Copy data */
    memcpy(dst_data, bounce->virt_addr, size);
    
    /* Flush cache after writing to destination */
    cache_flush_range(dst_data, size);
    
    g_bounce_pool.copies_performed++;
    
    return 0;
}

/**
 * ============================================================================
 * CACHE COHERENCY AND UTILITY FUNCTIONS (Stubs for Week 1)
 * ============================================================================
 */

static void cache_flush_range(void *addr, uint16_t size)
{
    /* Stub for Week 1 - would use CPU-specific cache flush */
    /* In DOS, cache coherency is typically not an issue */
    g_bounce_pool.cache_flushes++;
}

static void cache_invalidate_range(void *addr, uint16_t size)
{
    /* Stub for Week 1 - would use CPU-specific cache invalidation */
    /* In DOS, cache coherency is typically not an issue */
}

static uint32_t virt_to_phys(void *virt_addr)
{
    /* Stub for Week 1 - in DOS, virtual == physical for conventional memory */
    return (uint32_t)virt_addr;
}

static void *alloc_dma_safe(uint16_t size, uint32_t *phys_addr)
{
    /* Stub for Week 1 - would use memory management API */
    /* Return fake boundary-safe address */
    *phys_addr = 0x100000 + (size * 0x1000);  /* Fake safe address */
    return NULL;  /* Would return allocated buffer */
}

static void free_dma_safe(void *virt_addr, uint16_t size)
{
    /* Stub for Week 1 - would free via memory management API */
}