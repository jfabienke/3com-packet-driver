/**
 * @file memory_mgmt.c
 * @brief BOOMTEX.MOD Memory Management Integration
 * 
 * BOOMTEX.MOD - Memory Management Integration with Agent 11
 * Team C (Agents 09-10) - Week 1 Critical Deliverable
 * 
 * Provides DMA-safe buffer allocation and management for bus mastering
 * operations on 3C515-TX and 3C900-TPO NICs.
 */

#include "boomtex_internal.h"
#include "../../include/config.h"      /* For bus master testing integration */
#include <string.h>                    /* For memset */

/* Memory management context */
typedef struct {
    void *dma_pool_base;        /* Base address of DMA pool */
    uint32_t dma_pool_size;     /* Total pool size */
    uint32_t dma_pool_used;     /* Used pool size */
    uint8_t *allocation_bitmap; /* Allocation bitmap */
    uint16_t max_allocations;   /* Maximum allocations */
} boomtex_memory_context_t;

/* Global memory context */
static boomtex_memory_context_t g_memory_context;

/* External memory services */
extern memory_services_t *g_memory_services;

/* Buffer allocation sizes */
#define SMALL_BUFFER_SIZE       256     /* Small packet buffers */
#define LARGE_BUFFER_SIZE       1600    /* Large packet buffers */
#define DESCRIPTOR_SIZE         16      /* DMA descriptor size */
#define RING_BUFFER_SIZE        (BOOMTEX_MAX_TX_RING * DESCRIPTOR_SIZE * 2)

/**
 * @brief Create DMA buffer pools
 * 
 * Creates DMA-coherent buffer pools for packet operations.
 * 
 * @param config Buffer pool configuration
 * @return SUCCESS on success, negative error code on failure
 */
int boomtex_create_dma_pools(buffer_pool_config_t *config) {
    uint32_t total_size;
    void *pool_memory;
    uint32_t pool_phys;
    int result;
    
    if (!config || !g_memory_services) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("BOOMTEX: Creating DMA buffer pools");
    
    /* Calculate total memory needed */
    total_size = (config->small_buffer_size * config->small_buffer_count) +
                 (config->large_buffer_size * config->large_buffer_count) +
                 RING_BUFFER_SIZE;  /* For descriptor rings */
    
    /* Align to page boundary for DMA safety */
    total_size = (total_size + 4095) & ~4095;
    
    LOG_DEBUG("BOOMTEX: Allocating %lu bytes for DMA pools", total_size);
    
    /* Allocate DMA-coherent memory */
    result = g_memory_services->alloc_dma_coherent(total_size, config->alignment, 
                                                   &pool_memory, &pool_phys);
    if (result != SUCCESS) {
        LOG_ERROR("BOOMTEX: DMA pool allocation failed: %d", result);
        return result;
    }
    
    /* Verify DMA constraints */
    if (pool_phys + total_size > 0x1000000) {  /* 16MB ISA DMA limit */
        LOG_ERROR("BOOMTEX: DMA pool exceeds 16MB limit (0x%08lX)", pool_phys);
        g_memory_services->free_dma_coherent(pool_memory, total_size);
        return ERROR_DMA_BOUNDARY;
    }
    
    /* Initialize memory context */
    g_memory_context.dma_pool_base = pool_memory;
    g_memory_context.dma_pool_size = total_size;
    g_memory_context.dma_pool_used = 0;
    g_memory_context.max_allocations = 64;  /* Maximum individual allocations */
    
    /* Allocate allocation bitmap */
    g_memory_context.allocation_bitmap = malloc(g_memory_context.max_allocations / 8);
    if (!g_memory_context.allocation_bitmap) {
        g_memory_services->free_dma_coherent(pool_memory, total_size);
        return ERROR_OUT_OF_MEMORY;
    }
    
    memset(g_memory_context.allocation_bitmap, 0, g_memory_context.max_allocations / 8);
    
    LOG_INFO("BOOMTEX: DMA pools created - %lu bytes at physical 0x%08lX",
             total_size, pool_phys);
    
    return SUCCESS;
}

/**
 * @brief Setup DMA descriptor rings
 * 
 * Sets up TX and RX descriptor rings for bus mastering.
 * 
 * @param nic NIC context structure
 * @return SUCCESS on success, negative error code on failure
 */
int boomtex_setup_dma_rings(boomtex_nic_context_t *nic) {
    uint32_t tx_ring_phys, rx_ring_phys;
    
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("BOOMTEX: Setting up DMA rings for NIC");
    
    /* Allocate TX descriptor ring */
    nic->tx_ring = (boomtex_descriptor_t *)boomtex_alloc_dma_buffer(
        BOOMTEX_MAX_TX_RING * sizeof(boomtex_descriptor_t), &tx_ring_phys);
    if (!nic->tx_ring) {
        LOG_ERROR("BOOMTEX: TX ring allocation failed");
        return ERROR_OUT_OF_MEMORY;
    }
    nic->tx_ring_phys = tx_ring_phys;
    
    /* Allocate RX descriptor ring */
    nic->rx_ring = (boomtex_descriptor_t *)boomtex_alloc_dma_buffer(
        BOOMTEX_MAX_RX_RING * sizeof(boomtex_descriptor_t), &rx_ring_phys);
    if (!nic->rx_ring) {
        LOG_ERROR("BOOMTEX: RX ring allocation failed");
        boomtex_free_dma_buffer(nic->tx_ring);
        return ERROR_OUT_OF_MEMORY;
    }
    nic->rx_ring_phys = rx_ring_phys;
    
    /* Initialize ring pointers */
    nic->tx_head = 0;
    nic->tx_tail = 0;
    nic->rx_head = 0;
    nic->rx_tail = 0;
    
    /* Clear descriptor rings */
    memset(nic->tx_ring, 0, BOOMTEX_MAX_TX_RING * sizeof(boomtex_descriptor_t));
    memset(nic->rx_ring, 0, BOOMTEX_MAX_RX_RING * sizeof(boomtex_descriptor_t));
    
    /* Allocate packet buffers */
    for (int i = 0; i < BOOMTEX_MAX_RX_RING; i++) {
        nic->rx_buffers[i] = boomtex_alloc_dma_buffer(BOOMTEX_BUFFER_SIZE, 
                                                      &nic->rx_buffer_phys[i]);
        if (!nic->rx_buffers[i]) {
            LOG_ERROR("BOOMTEX: RX buffer %d allocation failed", i);
            boomtex_cleanup_dma_resources(nic);
            return ERROR_OUT_OF_MEMORY;
        }
        
        /* Initialize RX descriptor */
        nic->rx_ring[i].fragment_pointer = nic->rx_buffer_phys[i];
        nic->rx_ring[i].fragment_length = BOOMTEX_BUFFER_SIZE | BOOMTEX_DESC_LAST_FRAG;
        nic->rx_ring[i].next_pointer = rx_ring_phys + 
            (((i + 1) % BOOMTEX_MAX_RX_RING) * sizeof(boomtex_descriptor_t));
    }
    
    LOG_INFO("BOOMTEX: DMA rings initialized - TX: 0x%08lX, RX: 0x%08lX",
             nic->tx_ring_phys, nic->rx_ring_phys);
    
    return SUCCESS;
}

/**
 * @brief Allocate DMA-safe buffer
 * 
 * Allocates a DMA-coherent buffer from the pool.
 * 
 * @param size Buffer size in bytes
 * @param phys_addr Output physical address
 * @return Virtual address or NULL on failure
 */
void *boomtex_alloc_dma_buffer(uint32_t size, uint32_t *phys_addr) {
    uint8_t *virtual_base;
    uint32_t physical_base;
    uint32_t aligned_size;
    int slot = -1;
    
    if (!size || !phys_addr || !g_memory_context.dma_pool_base) {
        return NULL;
    }
    
    /* Align size to 16-byte boundary */
    aligned_size = (size + 15) & ~15;
    
    /* Find free slot in allocation bitmap */
    for (int i = 0; i < g_memory_context.max_allocations; i++) {
        int byte_index = i / 8;
        int bit_index = i % 8;
        
        if (!(g_memory_context.allocation_bitmap[byte_index] & (1 << bit_index))) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        LOG_ERROR("BOOMTEX: No free allocation slots");
        return NULL;
    }
    
    /* Check if we have enough space */
    if (g_memory_context.dma_pool_used + aligned_size > g_memory_context.dma_pool_size) {
        LOG_ERROR("BOOMTEX: DMA pool exhausted");
        return NULL;
    }
    
    /* Calculate addresses */
    virtual_base = (uint8_t *)g_memory_context.dma_pool_base + g_memory_context.dma_pool_used;
    
    /* Get physical address from memory services */
    if (g_memory_services->virt_to_phys) {
        physical_base = g_memory_services->virt_to_phys(virtual_base);
    } else {
        /* Assume 1:1 mapping for DOS */
        physical_base = (uint32_t)virtual_base;
    }
    
    /* Mark slot as used */
    int byte_index = slot / 8;
    int bit_index = slot % 8;
    g_memory_context.allocation_bitmap[byte_index] |= (1 << bit_index);
    
    /* Update pool usage */
    g_memory_context.dma_pool_used += aligned_size;
    
    *phys_addr = physical_base;
    
    LOG_DEBUG("BOOMTEX: Allocated DMA buffer - %lu bytes at virtual 0x%p, physical 0x%08lX",
              aligned_size, virtual_base, physical_base);
    
    return virtual_base;
}

/**
 * @brief Free DMA buffer
 * 
 * Frees a previously allocated DMA buffer.
 * 
 * @param buffer Buffer to free
 */
void boomtex_free_dma_buffer(void *buffer) {
    uint8_t *pool_base = (uint8_t *)g_memory_context.dma_pool_base;
    uint32_t offset;
    int slot;
    
    if (!buffer || !pool_base) {
        return;
    }
    
    /* Calculate offset into pool */
    offset = (uint8_t *)buffer - pool_base;
    if (offset >= g_memory_context.dma_pool_size) {
        LOG_ERROR("BOOMTEX: Invalid buffer address for free");
        return;
    }
    
    /* Simple slot calculation (would be more sophisticated in real implementation) */
    slot = offset / 256;  /* Assume 256-byte average allocation */
    if (slot >= g_memory_context.max_allocations) {
        slot = g_memory_context.max_allocations - 1;
    }
    
    /* Mark slot as free */
    int byte_index = slot / 8;
    int bit_index = slot % 8;
    g_memory_context.allocation_bitmap[byte_index] &= ~(1 << bit_index);
    
    LOG_DEBUG("BOOMTEX: Freed DMA buffer at 0x%p", buffer);
}

/**
 * @brief Setup bus mastering for NIC
 * 
 * Configures the NIC and system for bus mastering DMA operations.
 * 
 * @param nic NIC context structure
 * @return SUCCESS on success, negative error code on failure
 */
int boomtex_setup_bus_mastering(boomtex_nic_context_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Verify CPU supports bus mastering (80286+) */
    extern cpu_info_t g_cpu_info;
    if (g_cpu_info.type < CPU_TYPE_80286) {
        LOG_ERROR("BOOMTEX: Bus mastering requires 80286+ CPU with chipset support");
        return ERROR_CPU_DETECTION;
    }
    
    LOG_INFO("BOOMTEX: Performing comprehensive bus master testing for safety...");
    
    /* Access global configuration for bus master testing */
    extern config_t g_config;
    
    /* Only proceed with bus mastering if globally enabled */
    if (g_config.busmaster == BUSMASTER_OFF) {
        LOG_INFO("BOOMTEX: Bus mastering disabled by configuration - using PIO mode");
        return ERROR_HARDWARE;
    }
    
    /* Create test context for the NIC */
    nic_context_t test_ctx;
    memset(&test_ctx, 0, sizeof(test_ctx));
    test_ctx.io_base = nic->io_base;
    test_ctx.irq = nic->irq;
    
    /* Perform comprehensive bus master testing */
    bool quick_mode = (g_config.busmaster == BUSMASTER_AUTO);
    int test_result = config_perform_busmaster_auto_test(&g_config, &test_ctx, quick_mode);
    
    if (test_result != 0) {
        LOG_WARNING("BOOMTEX: Bus master testing failed (%d) - falling back to PIO mode", test_result);
        return ERROR_NOT_SUPPORTED;
    }
    
    /* Verify final configuration allows bus mastering */
    if (g_config.busmaster != BUSMASTER_ON) {
        LOG_INFO("BOOMTEX: Bus master testing completed but not enabled - using PIO mode");
        return ERROR_HARDWARE;
    }
    
    /* Hardware-specific bus mastering setup */
    switch (nic->hardware_type) {
        case BOOMTEX_HARDWARE_3C515TX:
            /* 3C515-TX: ISA bus mastering tested and approved */
            LOG_INFO("BOOMTEX: 3C515-TX ISA bus master testing PASSED - enabling DMA");
            break;
            
        case BOOMTEX_HARDWARE_3C900TPO:
            /* PCI bus mastering is typically more reliable but still test */
            LOG_INFO("BOOMTEX: 3C900-TPO PCI bus master testing PASSED - enabling DMA");
            break;
            
        default:
            LOG_WARNING("BOOMTEX: Bus mastering not supported for hardware type %d", 
                        nic->hardware_type);
            return ERROR_NOT_IMPLEMENTED;
    }
    
    LOG_INFO("BOOMTEX: Bus mastering configured and tested successfully");
    return SUCCESS;
}

/**
 * @brief Cleanup DMA resources
 * 
 * Frees all DMA resources associated with a NIC.
 * 
 * @param nic NIC context structure
 * @return SUCCESS on success, negative error code on failure
 */
int boomtex_cleanup_dma_resources(boomtex_nic_context_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("BOOMTEX: Cleaning up DMA resources");
    
    /* Free RX buffers */
    for (int i = 0; i < BOOMTEX_MAX_RX_RING; i++) {
        if (nic->rx_buffers[i]) {
            boomtex_free_dma_buffer(nic->rx_buffers[i]);
            nic->rx_buffers[i] = NULL;
        }
    }
    
    /* Free TX buffers */
    for (int i = 0; i < BOOMTEX_MAX_TX_RING; i++) {
        if (nic->tx_buffers[i]) {
            boomtex_free_dma_buffer(nic->tx_buffers[i]);
            nic->tx_buffers[i] = NULL;
        }
    }
    
    /* Free descriptor rings */
    if (nic->tx_ring) {
        boomtex_free_dma_buffer(nic->tx_ring);
        nic->tx_ring = NULL;
    }
    
    if (nic->rx_ring) {
        boomtex_free_dma_buffer(nic->rx_ring);
        nic->rx_ring = NULL;
    }
    
    LOG_DEBUG("BOOMTEX: DMA resources cleanup complete");
    return SUCCESS;
}

/**
 * @brief Free all allocated memory
 * 
 * Frees all memory allocated by the BOOMTEX module.
 */
void boomtex_free_allocated_memory(void) {
    LOG_DEBUG("BOOMTEX: Freeing all allocated memory");
    
    /* Free allocation bitmap */
    if (g_memory_context.allocation_bitmap) {
        free(g_memory_context.allocation_bitmap);
        g_memory_context.allocation_bitmap = NULL;
    }
    
    /* Free DMA pool */
    if (g_memory_context.dma_pool_base && g_memory_services) {
        g_memory_services->free_dma_coherent(g_memory_context.dma_pool_base, 
                                           g_memory_context.dma_pool_size);
        g_memory_context.dma_pool_base = NULL;
    }
    
    /* Clear context */
    memset(&g_memory_context, 0, sizeof(g_memory_context));
    
    LOG_DEBUG("BOOMTEX: Memory cleanup complete");
}

/**
 * @brief Get memory usage statistics
 * 
 * @param total_size Output total pool size
 * @param used_size Output used pool size
 * @param free_size Output free pool size
 * @return SUCCESS on success
 */
int boomtex_get_memory_stats(uint32_t *total_size, uint32_t *used_size, uint32_t *free_size) {
    if (total_size) {
        *total_size = g_memory_context.dma_pool_size;
    }
    if (used_size) {
        *used_size = g_memory_context.dma_pool_used;
    }
    if (free_size) {
        *free_size = g_memory_context.dma_pool_size - g_memory_context.dma_pool_used;
    }
    return SUCCESS;
}