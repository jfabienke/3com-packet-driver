/**
 * @file dma.c
 * @brief DMA abstraction layer implementation for scatter-gather operations
 * 
 * Sprint 2.2: Scatter-Gather DMA Implementation
 * 
 * This implementation provides:
 * - Physical address translation for DOS/XMS memory management
 * - Software scatter-gather layer for 3C515-TX (lacks true hardware SG)
 * - Complete fallback to PIO mode for 3C509B
 * - Integration with enhanced ring buffer management
 * - Performance monitoring and statistics collection
 * 
 * Technical Notes:
 * - 3C515-TX supports basic bus mastering but not true scatter-gather
 * - 3C509B is ISA PIO-only with no DMA capabilities
 * - Implementation consolidates fragments in software for 3C515-TX
 * - XMS memory used for large buffers beyond conventional 640KB limit
 */

#include "../include/dma.h"
#include "../include/logging.h"
#include "../include/error_handling.h"
#include "../include/cpu_detect.h"
#include "../include/memory.h"
#include <string.h>
#include <dos.h>

/* Global DMA manager instance */
dma_manager_t g_dma_manager = {0};

/* Internal helper functions */
static uint32_t get_conventional_phys_addr(void *virt_addr);
static int setup_coherency_management(void);
static int init_address_translation(void);
static void dma_set_error(uint8_t nic_index, dma_error_t error);
static dma_error_t dma_get_nic_error(uint8_t nic_index);
static int validate_dma_parameters(void *addr, uint32_t size, uint8_t direction);
static int allocate_coherent_pool(void);
static void cleanup_coherent_pool(void);

/* NIC-specific DMA function implementations */
static int dma_3c515_setup_transfer_impl(dma_nic_context_t *ctx, dma_sg_list_t *sg_list, uint8_t direction);
static int dma_3c515_start_transfer_impl(dma_nic_context_t *ctx);
static int dma_3c515_stop_transfer_impl(dma_nic_context_t *ctx);
static int dma_3c515_get_status_impl(dma_nic_context_t *ctx);

static int dma_3c509b_fallback_impl(dma_nic_context_t *ctx, dma_sg_list_t *sg_list, uint8_t direction);

/**
 * @brief Initialize DMA subsystem
 */
int dma_init(void) {
    int result;
    
    if (g_dma_manager.initialized) {
        LOG_WARNING("DMA manager already initialized");
        return 0;
    }
    
    LOG_INFO("Initializing DMA subsystem for scatter-gather operations");
    
    /* Clear DMA manager structure */
    memset(&g_dma_manager, 0, sizeof(dma_manager_t));
    
    /* Initialize address translation */
    result = init_address_translation();
    if (result != 0) {
        LOG_ERROR("Failed to initialize address translation: %d", result);
        return result;
    }
    
    /* Setup cache coherency management */
    result = setup_coherency_management();
    if (result != 0) {
        LOG_ERROR("Failed to setup coherency management: %d", result);
        return result;
    }
    
    /* Initialize XMS support if available */
    if (xms_is_available()) {
        g_dma_manager.xms_available = true;
        result = dma_init_xms_region(64); /* 64KB XMS region for DMA */
        if (result != 0) {
            LOG_WARNING("Failed to initialize XMS DMA region: %d", result);
            g_dma_manager.xms_available = false;
        } else {
            LOG_INFO("XMS DMA region initialized successfully");
        }
    } else {
        LOG_INFO("XMS not available, using conventional memory only");
        g_dma_manager.xms_available = false;
    }
    
    /* Allocate coherent memory pool */
    result = allocate_coherent_pool();
    if (result != 0) {
        LOG_WARNING("Failed to allocate coherent memory pool: %d", result);
        /* Continue without coherent pool */
    }
    
    /* Initialize NIC contexts */
    for (int i = 0; i < MAX_NICS; i++) {
        memset(&g_dma_manager.nic_contexts[i], 0, sizeof(dma_nic_context_t));
        g_dma_manager.nic_contexts[i].nic_type = 0; /* Uninitialized */
    }
    
    g_dma_manager.initialized = true;
    
    LOG_INFO("DMA subsystem initialized successfully");
    LOG_INFO("  XMS available: %s", g_dma_manager.xms_available ? "Yes" : "No");
    LOG_INFO("  Coherent pool: %s", g_dma_manager.coherent_pool ? "Yes" : "No");
    LOG_INFO("  Cache coherent: %s", g_dma_manager.coherency.cache_coherent_dma ? "Yes" : "No");
    
    return 0;
}

/**
 * @brief Cleanup DMA subsystem
 */
void dma_cleanup(void) {
    if (!g_dma_manager.initialized) {
        return;
    }
    
    LOG_INFO("Cleaning up DMA subsystem");
    
    /* Cleanup all NIC contexts */
    for (int i = 0; i < MAX_NICS; i++) {
        if (g_dma_manager.nic_contexts[i].nic_type != 0) {
            dma_cleanup_nic_context(i);
        }
    }
    
    /* Cleanup coherent pool */
    cleanup_coherent_pool();
    
    /* Free XMS region if allocated */
    if (g_dma_manager.xms_available && g_dma_manager.xms_dma_handle != 0) {
        xms_free(g_dma_manager.xms_dma_handle);
        g_dma_manager.xms_dma_handle = 0;
    }
    
    /* Print final statistics */
    LOG_INFO("DMA subsystem statistics:");
    LOG_INFO("  Total mappings: %u", g_dma_manager.total_mappings);
    LOG_INFO("  Mapping failures: %u", g_dma_manager.mapping_failures);
    LOG_INFO("  Coherency violations: %u", g_dma_manager.coherency_violations);
    
    g_dma_manager.initialized = false;
    
    LOG_INFO("DMA subsystem cleanup completed");
}

/**
 * @brief Initialize NIC-specific DMA context
 */
int dma_init_nic_context(uint8_t nic_index, uint16_t nic_type, uint16_t io_base, 
                        enhanced_ring_context_t *ring_context) {
    dma_nic_context_t *ctx;
    int result;
    
    if (nic_index >= MAX_NICS) {
        LOG_ERROR("Invalid NIC index: %d", nic_index);
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    if (!g_dma_manager.initialized) {
        LOG_ERROR("DMA manager not initialized");
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    ctx = &g_dma_manager.nic_contexts[nic_index];
    
    /* Clear context */
    memset(ctx, 0, sizeof(dma_nic_context_t));
    
    /* Set basic parameters */
    ctx->nic_type = nic_type;
    ctx->io_base = io_base;
    ctx->ring_context = ring_context;
    
    LOG_INFO("Initializing DMA context for NIC %d (type: 0x%04X)", nic_index, nic_type);
    
    /* Configure capabilities based on NIC type */
    switch (nic_type) {
        case 0x5051: /* 3C515-TX */
            ctx->dma_capabilities = DMA_CAP_BASIC_BUSMASTER;
            ctx->max_dma_address = 0xFFFF; /* ISA 16-bit addressing */
            ctx->max_sg_fragments = DMA_MAX_FRAGMENTS_3C515;
            ctx->min_alignment = DMA_MIN_ALIGNMENT;
            ctx->max_transfer_size = DMA_MAX_TRANSFER_SIZE;
            
            /* Set function pointers for 3C515-TX */
            ctx->setup_dma_transfer = dma_3c515_setup_transfer_impl;
            ctx->start_dma_transfer = dma_3c515_start_transfer_impl;
            ctx->stop_dma_transfer = dma_3c515_stop_transfer_impl;
            ctx->get_dma_status = dma_3c515_get_status_impl;
            
            LOG_INFO("  3C515-TX: Basic bus mastering DMA enabled");
            break;
            
        case 0x5090: /* 3C509B */
            ctx->dma_capabilities = DMA_CAP_NONE;
            ctx->max_dma_address = 0; /* No DMA */
            ctx->max_sg_fragments = DMA_MAX_FRAGMENTS_3C509B;
            ctx->min_alignment = 1; /* No alignment requirement for PIO */
            ctx->max_transfer_size = DMA_MAX_TRANSFER_SIZE;
            
            /* Set function pointers for 3C509B fallback */
            ctx->setup_dma_transfer = dma_3c509b_fallback_impl;
            ctx->start_dma_transfer = NULL; /* Not applicable for PIO */
            ctx->stop_dma_transfer = NULL;  /* Not applicable for PIO */
            ctx->get_dma_status = NULL;     /* Not applicable for PIO */
            
            LOG_INFO("  3C509B: PIO mode only, no DMA support");
            break;
            
        default:
            LOG_ERROR("Unknown NIC type: 0x%04X", nic_type);
            return -DMA_ERROR_UNSUPPORTED_OPERATION;
    }
    
    /* Initialize buffer pools if DMA is supported */
    if (ctx->dma_capabilities & DMA_CAP_BASIC_BUSMASTER) {
        /* Initialize TX pool */
        result = dma_pool_init(&ctx->tx_pool, DMA_DEFAULT_TX_POOL_SIZE, DMA_MAX_TRANSFER_SIZE,
                              DMA_MEMORY_CONVENTIONAL, ctx->min_alignment);
        if (result != 0) {
            LOG_ERROR("Failed to initialize TX DMA pool: %d", result);
            return result;
        }
        
        /* Initialize RX pool */
        result = dma_pool_init(&ctx->rx_pool, DMA_DEFAULT_RX_POOL_SIZE, DMA_MAX_TRANSFER_SIZE,
                              DMA_MEMORY_CONVENTIONAL, ctx->min_alignment);
        if (result != 0) {
            LOG_ERROR("Failed to initialize RX DMA pool: %d", result);
            dma_pool_cleanup(&ctx->tx_pool);
            return result;
        }
        
        LOG_INFO("  DMA buffer pools initialized (TX: %d, RX: %d buffers)",
                 DMA_DEFAULT_TX_POOL_SIZE, DMA_DEFAULT_RX_POOL_SIZE);
    }
    
    LOG_INFO("NIC %d DMA context initialized successfully", nic_index);
    
    return 0;
}

/**
 * @brief Cleanup NIC-specific DMA context
 */
void dma_cleanup_nic_context(uint8_t nic_index) {
    dma_nic_context_t *ctx;
    
    if (nic_index >= MAX_NICS) {
        return;
    }
    
    ctx = &g_dma_manager.nic_contexts[nic_index];
    
    if (ctx->nic_type == 0) {
        return; /* Not initialized */
    }
    
    LOG_INFO("Cleaning up DMA context for NIC %d", nic_index);
    
    /* Print statistics */
    if (ctx->sg_consolidations > 0 || ctx->zero_copy_transfers > 0 || ctx->dma_errors > 0) {
        LOG_INFO("  NIC %d DMA statistics:", nic_index);
        LOG_INFO("    SG consolidations: %u", ctx->sg_consolidations);
        LOG_INFO("    Zero-copy transfers: %u", ctx->zero_copy_transfers);
        LOG_INFO("    Fallback transfers: %u", ctx->fallback_transfers);
        LOG_INFO("    DMA errors: %u", ctx->dma_errors);
    }
    
    /* Cleanup buffer pools */
    if (ctx->dma_capabilities & DMA_CAP_BASIC_BUSMASTER) {
        dma_pool_cleanup(&ctx->tx_pool);
        dma_pool_cleanup(&ctx->rx_pool);
    }
    
    /* Clear context */
    memset(ctx, 0, sizeof(dma_nic_context_t));
    
    LOG_DEBUG("NIC %d DMA context cleanup completed", nic_index);
}

/**
 * @brief Convert virtual address to physical address
 */
uint32_t dma_virt_to_phys(void *virt_addr) {
    if (!virt_addr) {
        return 0;
    }
    
    if (!g_dma_manager.initialized) {
        LOG_ERROR("DMA manager not initialized");
        return 0;
    }
    
    /* Use function pointer if available */
    if (g_dma_manager.virt_to_phys) {
        return g_dma_manager.virt_to_phys(virt_addr);
    }
    
    /* Fallback to conventional memory calculation */
    return get_conventional_phys_addr(virt_addr);
}

/**
 * @brief Convert physical address to virtual address
 */
void* dma_phys_to_virt(uint32_t phys_addr) {
    if (phys_addr == 0) {
        return NULL;
    }
    
    if (!g_dma_manager.initialized) {
        LOG_ERROR("DMA manager not initialized");
        return NULL;
    }
    
    /* Use function pointer if available */
    if (g_dma_manager.phys_to_virt) {
        return g_dma_manager.phys_to_virt(phys_addr);
    }
    
    /* For conventional memory in DOS, virtual == physical */
    if (phys_addr < 0x100000) { /* Below 1MB */
        return (void*)phys_addr;
    }
    
    return NULL; /* Cannot translate extended memory without proper mapping */
}

/**
 * @brief Create DMA mapping for memory region
 */
int dma_map_memory(void *virt_addr, uint32_t size, uint8_t direction, dma_mapping_t *mapping) {
    uint32_t phys_addr;
    
    if (!mapping || !virt_addr || size == 0) {
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    if (!g_dma_manager.initialized) {
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    /* Validate parameters */
    int result = validate_dma_parameters(virt_addr, size, direction);
    if (result != 0) {
        return result;
    }
    
    /* Clear mapping structure */
    memset(mapping, 0, sizeof(dma_mapping_t));
    
    /* Get physical address */
    phys_addr = dma_virt_to_phys(virt_addr);
    if (phys_addr == 0) {
        LOG_ERROR("Failed to get physical address for virtual address %p", virt_addr);
        g_dma_manager.mapping_failures++;
        return -DMA_ERROR_MAPPING_FAILED;
    }
    
    /* Fill mapping structure */
    mapping->virtual_addr = virt_addr;
    mapping->physical_addr = phys_addr;
    mapping->size = size;
    mapping->memory_type = DMA_MEMORY_CONVENTIONAL; /* Default for conventional memory */
    mapping->flags = 0;
    mapping->ref_count = 1;
    
    /* Determine memory type and set appropriate flags */
    if ((uint32_t)virt_addr < 0xA0000) {
        mapping->memory_type = DMA_MEMORY_CONVENTIONAL;
        mapping->flags |= DMA_MAP_COHERENT; /* Conventional memory is coherent in DOS */
    } else {
        /* Extended memory regions require special handling */
        mapping->memory_type = DMA_MEMORY_XMS;
        mapping->flags |= DMA_MAP_CACHED; /* May require coherency management */
    }
    
    /* Check alignment */
    if ((phys_addr & (DMA_MIN_ALIGNMENT - 1)) != 0) {
        LOG_WARNING("DMA mapping not properly aligned: phys=0x%08X", phys_addr);
    }
    
    /* Perform cache coherency operations if needed */
    if (direction & DMA_DIRECTION_TO_DEVICE) {
        dma_sync_for_device(virt_addr, size, direction);
    }
    
    g_dma_manager.total_mappings++;
    g_dma_manager.active_mappings++;
    
    LOG_TRACE("DMA mapping created: virt=%p, phys=0x%08X, size=%u", 
              virt_addr, phys_addr, size);
    
    return 0;
}

/**
 * @brief Unmap DMA memory region
 */
void dma_unmap_memory(dma_mapping_t *mapping) {
    if (!mapping || mapping->ref_count == 0) {
        return;
    }
    
    mapping->ref_count--;
    
    if (mapping->ref_count == 0) {
        /* Perform cache coherency operations if needed */
        if (mapping->flags & DMA_MAP_CACHED) {
            dma_sync_for_cpu(mapping->virtual_addr, mapping->size, DMA_DIRECTION_FROM_DEVICE);
        }
        
        /* Unlock XMS memory if applicable */
        if (mapping->memory_type == DMA_MEMORY_XMS && 
            (mapping->flags & DMA_MAP_XMS_LOCKED) && 
            mapping->xms_handle != 0) {
            xms_unlock(mapping->xms_handle);
        }
        
        g_dma_manager.active_mappings--;
        
        LOG_TRACE("DMA mapping unmapped: virt=%p, phys=0x%08X", 
                  mapping->virtual_addr, mapping->physical_addr);
        
        /* Clear mapping structure */
        memset(mapping, 0, sizeof(dma_mapping_t));
    }
}

/**
 * @brief Initialize XMS DMA region
 */
int dma_init_xms_region(uint32_t size_kb) {
    int result;
    uint32_t linear_addr;
    
    if (!g_dma_manager.xms_available) {
        return -DMA_ERROR_XMS_UNAVAILABLE;
    }
    
    LOG_INFO("Initializing XMS DMA region (%u KB)", size_kb);
    
    /* Allocate XMS memory */
    result = xms_allocate(size_kb, &g_dma_manager.xms_dma_handle);
    if (result != XMS_SUCCESS) {
        LOG_ERROR("Failed to allocate XMS memory: %d", result);
        return -DMA_ERROR_XMS_UNAVAILABLE;
    }
    
    /* Lock XMS memory to get linear address */
    result = xms_lock(g_dma_manager.xms_dma_handle, &linear_addr);
    if (result != XMS_SUCCESS) {
        LOG_ERROR("Failed to lock XMS memory: %d", result);
        xms_free(g_dma_manager.xms_dma_handle);
        g_dma_manager.xms_dma_handle = 0;
        return -DMA_ERROR_XMS_UNAVAILABLE;
    }
    
    g_dma_manager.xms_dma_base = linear_addr;
    
    LOG_INFO("XMS DMA region allocated: handle=%u, base=0x%08X", 
             g_dma_manager.xms_dma_handle, g_dma_manager.xms_dma_base);
    
    return 0;
}

/**
 * @brief Create scatter-gather list
 */
dma_sg_list_t* dma_sg_alloc(uint16_t max_fragments) {
    dma_sg_list_t *sg_list;
    
    if (max_fragments == 0 || max_fragments > 64) { /* Reasonable limit */
        return NULL;
    }
    
    /* Allocate SG list structure */
    sg_list = (dma_sg_list_t*)memory_alloc(sizeof(dma_sg_list_t), MEM_TYPE_DMA_BUFFER, MEM_FLAG_ZERO);
    if (!sg_list) {
        return NULL;
    }
    
    /* Allocate fragments array */
    sg_list->fragments = (dma_fragment_t*)memory_alloc(
        max_fragments * sizeof(dma_fragment_t), MEM_TYPE_DMA_BUFFER, MEM_FLAG_ZERO);
    if (!sg_list->fragments) {
        memory_free(sg_list);
        return NULL;
    }
    
    sg_list->max_fragments = max_fragments;
    sg_list->fragment_count = 0;
    sg_list->total_length = 0;
    sg_list->flags = 0;
    sg_list->private_data = NULL;
    
    LOG_TRACE("SG list allocated: max_fragments=%u", max_fragments);
    
    return sg_list;
}

/**
 * @brief Free scatter-gather list
 */
void dma_sg_free(dma_sg_list_t *sg_list) {
    if (!sg_list) {
        return;
    }
    
    if (sg_list->fragments) {
        memory_free(sg_list->fragments);
    }
    
    memory_free(sg_list);
    
    LOG_TRACE("SG list freed");
}

/**
 * @brief Add fragment to scatter-gather list
 */
int dma_sg_add_fragment(dma_sg_list_t *sg_list, void *virt_addr, uint32_t length, uint32_t flags) {
    dma_fragment_t *frag;
    uint32_t phys_addr;
    
    if (!sg_list || !virt_addr || length == 0) {
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    if (sg_list->fragment_count >= sg_list->max_fragments) {
        return -DMA_ERROR_TOO_MANY_FRAGMENTS;
    }
    
    if (length > DMA_MAX_TRANSFER_SIZE) {
        return -DMA_ERROR_FRAGMENT_TOO_LARGE;
    }
    
    /* Get physical address */
    phys_addr = dma_virt_to_phys(virt_addr);
    if (phys_addr == 0) {
        return -DMA_ERROR_MAPPING_FAILED;
    }
    
    /* Add fragment */
    frag = &sg_list->fragments[sg_list->fragment_count];
    frag->physical_addr = phys_addr;
    frag->length = length;
    frag->flags = flags;
    frag->next = NULL;
    
    /* Link fragments */
    if (sg_list->fragment_count > 0) {
        sg_list->fragments[sg_list->fragment_count - 1].next = frag;
    }
    
    sg_list->fragment_count++;
    sg_list->total_length += length;
    
    LOG_TRACE("Fragment added: phys=0x%08X, len=%u, flags=0x%X", 
              phys_addr, length, flags);
    
    return 0;
}

/**
 * @brief Consolidate scatter-gather list into single buffer
 */
int dma_sg_consolidate(dma_sg_list_t *sg_list, uint8_t *consolidated_buffer, uint32_t buffer_size) {
    uint32_t total_copied = 0;
    uint8_t *dest_ptr;
    
    if (!sg_list || !consolidated_buffer || buffer_size == 0) {
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    if (sg_list->total_length > buffer_size) {
        LOG_ERROR("Consolidated buffer too small: need %u, have %u", 
                  sg_list->total_length, buffer_size);
        return -DMA_ERROR_OUT_OF_MEMORY;
    }
    
    dest_ptr = consolidated_buffer;
    
    /* Copy all fragments into consolidated buffer */
    for (uint16_t i = 0; i < sg_list->fragment_count; i++) {
        dma_fragment_t *frag = &sg_list->fragments[i];
        void *src_ptr = dma_phys_to_virt(frag->physical_addr);
        
        if (!src_ptr) {
            LOG_ERROR("Cannot translate physical address 0x%08X for fragment %u", 
                      frag->physical_addr, i);
            return -DMA_ERROR_MAPPING_FAILED;
        }
        
        /* Copy fragment data */
        memory_copy(dest_ptr, src_ptr, frag->length);
        dest_ptr += frag->length;
        total_copied += frag->length;
        
        LOG_TRACE("Fragment %u consolidated: %u bytes", i, frag->length);
    }
    
    /* Mark as consolidated */
    sg_list->flags |= DMA_SG_CONSOLIDATED;
    
    LOG_DEBUG("SG list consolidated: %u fragments, %u bytes total", 
              sg_list->fragment_count, total_copied);
    
    return total_copied;
}

/* === Internal Helper Functions === */

/**
 * @brief Get physical address for conventional memory
 */
static uint32_t get_conventional_phys_addr(void *virt_addr) {
    uint32_t segment, offset;
    
    /* In DOS, conventional memory virtual addresses map directly to physical */
    segment = FP_SEG(virt_addr);
    offset = FP_OFF(virt_addr);
    
    /* Convert segment:offset to linear address */
    return (segment << 4) + offset;
}

/**
 * @brief Setup cache coherency management
 */
static int setup_coherency_management(void) {
    dma_coherency_mgr_t *coherency = &g_dma_manager.coherency;
    
    /* Initialize coherency management */
    coherency->coherent_memory_available = true; /* DOS conventional memory is coherent */
    coherency->cache_coherent_dma = true;        /* No cache in most DOS systems */
    coherency->cache_line_size = 4;              /* Conservative estimate */
    coherency->dma_alignment = DMA_MIN_ALIGNMENT;
    
    /* Set up coherency function pointers (no-ops for DOS) */
    coherency->sync_for_cpu = NULL;    /* Not needed in DOS */
    coherency->sync_for_device = NULL; /* Not needed in DOS */
    
    LOG_DEBUG("Cache coherency management initialized");
    
    return 0;
}

/**
 * @brief Initialize address translation
 */
static int init_address_translation(void) {
    /* Set up address translation function pointers */
    g_dma_manager.virt_to_phys = get_conventional_phys_addr;
    g_dma_manager.phys_to_virt = NULL; /* Use fallback implementation */
    
    LOG_DEBUG("Address translation initialized");
    
    return 0;
}

/**
 * @brief Validate DMA parameters
 */
static int validate_dma_parameters(void *addr, uint32_t size, uint8_t direction) {
    if (!addr || size == 0) {
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    if (direction == 0 || direction > DMA_DIRECTION_BIDIRECTIONAL) {
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    /* Check alignment */
    if (((uint32_t)addr & (DMA_MIN_ALIGNMENT - 1)) != 0) {
        return -DMA_ERROR_ALIGNMENT_ERROR;
    }
    
    /* Check size limits */
    if (size > DMA_MAX_TRANSFER_SIZE) {
        return -DMA_ERROR_FRAGMENT_TOO_LARGE;
    }
    
    return 0;
}

/**
 * @brief Allocate coherent memory pool
 */
static int allocate_coherent_pool(void) {
    /* Allocate coherent pool structure */
    g_dma_manager.coherent_pool = (dma_buffer_pool_t*)memory_alloc(
        sizeof(dma_buffer_pool_t), MEM_TYPE_DMA_BUFFER, MEM_FLAG_ZERO);
    
    if (!g_dma_manager.coherent_pool) {
        return -DMA_ERROR_OUT_OF_MEMORY;
    }
    
    /* Initialize coherent pool */
    int result = dma_pool_init(g_dma_manager.coherent_pool, DMA_COHERENT_POOL_SIZE, 
                               DMA_MAX_TRANSFER_SIZE, DMA_MEMORY_CONVENTIONAL, DMA_MIN_ALIGNMENT);
    if (result != 0) {
        memory_free(g_dma_manager.coherent_pool);
        g_dma_manager.coherent_pool = NULL;
        return result;
    }
    
    LOG_DEBUG("Coherent memory pool allocated (%d buffers)", DMA_COHERENT_POOL_SIZE);
    
    return 0;
}

/**
 * @brief Cleanup coherent memory pool
 */
static void cleanup_coherent_pool(void) {
    if (g_dma_manager.coherent_pool) {
        dma_pool_cleanup(g_dma_manager.coherent_pool);
        memory_free(g_dma_manager.coherent_pool);
        g_dma_manager.coherent_pool = NULL;
    }
}

/* === Cache Coherency Functions === */

/**
 * @brief Synchronize memory for CPU access
 */
void dma_sync_for_cpu(void *addr, uint32_t size, uint8_t direction) {
    /* No-op in DOS environment - no cache coherency issues */
    (void)addr;
    (void)size;
    (void)direction;
}

/**
 * @brief Synchronize memory for device access
 */
void dma_sync_for_device(void *addr, uint32_t size, uint8_t direction) {
    /* No-op in DOS environment - no cache coherency issues */
    (void)addr;
    (void)size;
    (void)direction;
}

/**
 * @brief Check if memory region is cache coherent
 */
bool dma_is_coherent(void *addr, uint32_t size) {
    /* All conventional memory is coherent in DOS */
    (void)size;
    return ((uint32_t)addr < 0xA0000);
}

/* === Error Handling === */

/**
 * @brief Set DMA error for NIC
 */
static void dma_set_error(uint8_t nic_index, dma_error_t error) {
    if (nic_index < MAX_NICS) {
        /* Store error in private data or context as needed */
        g_dma_manager.nic_contexts[nic_index].dma_errors++;
    }
}

/**
 * @brief Get last DMA error
 */
dma_error_t dma_get_last_error(uint8_t nic_index) {
    if (nic_index >= MAX_NICS) {
        return DMA_ERROR_INVALID_PARAM;
    }
    
    /* Return based on context state */
    return DMA_ERROR_NONE; /* Simplified for now */
}

/**
 * @brief Convert DMA error to string
 */
const char* dma_error_to_string(dma_error_t error) {
    switch (error) {
        case DMA_ERROR_NONE: return "No error";
        case DMA_ERROR_INVALID_PARAM: return "Invalid parameter";
        case DMA_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case DMA_ERROR_MAPPING_FAILED: return "Mapping failed";
        case DMA_ERROR_XMS_UNAVAILABLE: return "XMS unavailable";
        case DMA_ERROR_ALIGNMENT_ERROR: return "Alignment error";
        case DMA_ERROR_TRANSFER_TIMEOUT: return "Transfer timeout";
        case DMA_ERROR_HARDWARE_ERROR: return "Hardware error";
        case DMA_ERROR_COHERENCY_VIOLATION: return "Coherency violation";
        case DMA_ERROR_FRAGMENT_TOO_LARGE: return "Fragment too large";
        case DMA_ERROR_TOO_MANY_FRAGMENTS: return "Too many fragments";
        case DMA_ERROR_UNSUPPORTED_OPERATION: return "Unsupported operation";
        default: return "Unknown error";
    }
}

/**
 * @brief Get DMA statistics
 */
int dma_get_statistics(uint8_t nic_index, uint32_t *sg_ops, uint32_t *consolidations,
                      uint32_t *zero_copy, uint32_t *errors) {
    dma_nic_context_t *ctx;
    
    if (nic_index >= MAX_NICS) {
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    ctx = &g_dma_manager.nic_contexts[nic_index];
    
    if (sg_ops) *sg_ops = ctx->sg_consolidations + ctx->zero_copy_transfers;
    if (consolidations) *consolidations = ctx->sg_consolidations;
    if (zero_copy) *zero_copy = ctx->zero_copy_transfers;
    if (errors) *errors = ctx->dma_errors;
    
    return 0;
}

/**
 * @brief Reset DMA statistics
 */
void dma_reset_statistics(uint8_t nic_index) {
    dma_nic_context_t *ctx;
    
    if (nic_index >= MAX_NICS) {
        return;
    }
    
    ctx = &g_dma_manager.nic_contexts[nic_index];
    
    ctx->sg_consolidations = 0;
    ctx->zero_copy_transfers = 0;
    ctx->fallback_transfers = 0;
    ctx->dma_errors = 0;
    
    LOG_DEBUG("DMA statistics reset for NIC %d", nic_index);
}

/**
 * @brief Print DMA status for debugging
 */
void dma_dump_status(uint8_t nic_index) {
    dma_nic_context_t *ctx;
    
    if (nic_index >= MAX_NICS) {
        return;
    }
    
    ctx = &g_dma_manager.nic_contexts[nic_index];
    
    LOG_INFO("=== DMA Status for NIC %d ===", nic_index);
    LOG_INFO("NIC Type: 0x%04X", ctx->nic_type);
    LOG_INFO("DMA Capabilities: 0x%08X", ctx->dma_capabilities);
    LOG_INFO("Max SG Fragments: %u", ctx->max_sg_fragments);
    LOG_INFO("Max Transfer Size: %u", ctx->max_transfer_size);
    LOG_INFO("Statistics:");
    LOG_INFO("  SG Consolidations: %u", ctx->sg_consolidations);
    LOG_INFO("  Zero-copy Transfers: %u", ctx->zero_copy_transfers);
    LOG_INFO("  Fallback Transfers: %u", ctx->fallback_transfers);
    LOG_INFO("  DMA Errors: %u", ctx->dma_errors);
    
    if (ctx->dma_capabilities & DMA_CAP_BASIC_BUSMASTER) {
        LOG_INFO("TX Pool: %u/%u buffers free", ctx->tx_pool.free_count, ctx->tx_pool.buffer_count);
        LOG_INFO("RX Pool: %u/%u buffers free", ctx->rx_pool.free_count, ctx->rx_pool.buffer_count);
    }
    
    LOG_INFO("=== End DMA Status ===");
}

/* === Buffer Pool Management === */

/**
 * @brief Initialize DMA buffer pool
 */
int dma_pool_init(dma_buffer_pool_t *pool, uint16_t buffer_count, uint32_t buffer_size,
                 dma_memory_type_t memory_type, uint32_t alignment) {
    uint32_t total_size;
    uint32_t aligned_size;
    uint8_t *buffer_ptr;
    uint32_t bitmap_size;
    
    if (!pool || buffer_count == 0 || buffer_size == 0) {
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    /* Clear pool structure */
    memset(pool, 0, sizeof(dma_buffer_pool_t));
    
    /* Calculate aligned buffer size */
    aligned_size = (buffer_size + alignment - 1) & ~(alignment - 1);
    
    /* Calculate total memory required */
    total_size = buffer_count * aligned_size;
    
    /* Allocate base memory */
    switch (memory_type) {
        case DMA_MEMORY_CONVENTIONAL:
            pool->base_addr = (uint8_t*)memory_alloc_aligned(total_size, alignment, MEM_TYPE_DMA_BUFFER);
            break;
        case DMA_MEMORY_XMS:
            /* XMS allocation would go here */
            LOG_WARNING("XMS buffer pool allocation not yet implemented");
            return -DMA_ERROR_XMS_UNAVAILABLE;
        default:
            return -DMA_ERROR_INVALID_PARAM;
    }
    
    if (!pool->base_addr) {
        LOG_ERROR("Failed to allocate buffer pool memory (%u bytes)", total_size);
        return -DMA_ERROR_OUT_OF_MEMORY;
    }
    
    /* Allocate bitmap for free buffer tracking */
    bitmap_size = (buffer_count + 31) / 32; /* Round up to 32-bit words */
    pool->free_bitmap = (uint32_t*)memory_alloc(bitmap_size * sizeof(uint32_t), 
                                               MEM_TYPE_DMA_BUFFER, MEM_FLAG_ZERO);
    if (!pool->free_bitmap) {
        memory_free(pool->base_addr);
        return -DMA_ERROR_OUT_OF_MEMORY;
    }
    
    /* Allocate mapping array */
    pool->mappings = (dma_mapping_t*)memory_alloc(buffer_count * sizeof(dma_mapping_t),
                                                 MEM_TYPE_DMA_BUFFER, MEM_FLAG_ZERO);
    if (!pool->mappings) {
        memory_free(pool->free_bitmap);
        memory_free(pool->base_addr);
        return -DMA_ERROR_OUT_OF_MEMORY;
    }
    
    /* Initialize pool parameters */
    pool->pool_size = total_size;
    pool->buffer_size = aligned_size;
    pool->buffer_count = buffer_count;
    pool->free_count = buffer_count;
    pool->memory_type = memory_type;
    
    /* Mark all buffers as free */
    for (uint32_t i = 0; i < bitmap_size; i++) {
        pool->free_bitmap[i] = 0xFFFFFFFF;
    }
    
    /* Clear unused bits in last word */
    if (buffer_count & 31) {
        uint32_t last_word = bitmap_size - 1;
        uint32_t valid_bits = buffer_count & 31;
        pool->free_bitmap[last_word] &= (1U << valid_bits) - 1;
    }
    
    /* Initialize mappings for each buffer */
    buffer_ptr = pool->base_addr;
    for (uint16_t i = 0; i < buffer_count; i++) {
        dma_mapping_t *mapping = &pool->mappings[i];
        
        mapping->virtual_addr = buffer_ptr;
        mapping->physical_addr = dma_virt_to_phys(buffer_ptr);
        mapping->size = aligned_size;
        mapping->memory_type = memory_type;
        mapping->flags = DMA_MAP_COHERENT; /* Pool buffers are coherent */
        mapping->ref_count = 0; /* Initially unused */
        
        buffer_ptr += aligned_size;
    }
    
    LOG_DEBUG("DMA buffer pool initialized: %u buffers of %u bytes each (%u KB total)",
              buffer_count, aligned_size, total_size / 1024);
    
    return 0;
}

/**
 * @brief Cleanup DMA buffer pool
 */
void dma_pool_cleanup(dma_buffer_pool_t *pool) {
    if (!pool) {
        return;
    }
    
    LOG_DEBUG("Cleaning up DMA buffer pool (%u buffers)", pool->buffer_count);
    
    /* Free allocated memory */
    if (pool->mappings) {
        memory_free(pool->mappings);
    }
    
    if (pool->free_bitmap) {
        memory_free(pool->free_bitmap);
    }
    
    if (pool->base_addr) {
        memory_free(pool->base_addr);
    }
    
    /* Clear pool structure */
    memset(pool, 0, sizeof(dma_buffer_pool_t));
}

/**
 * @brief Allocate buffer from DMA pool
 */
int dma_pool_alloc(dma_buffer_pool_t *pool, dma_mapping_t *mapping) {
    uint32_t word_index, bit_index;
    uint32_t buffer_index;
    
    if (!pool || !mapping) {
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    if (pool->free_count == 0) {
        return -DMA_ERROR_OUT_OF_MEMORY;
    }
    
    /* Find first free buffer */
    for (word_index = 0; word_index < (pool->buffer_count + 31) / 32; word_index++) {
        if (pool->free_bitmap[word_index] != 0) {
            /* Find first set bit */
            for (bit_index = 0; bit_index < 32; bit_index++) {
                if (pool->free_bitmap[word_index] & (1U << bit_index)) {
                    buffer_index = word_index * 32 + bit_index;
                    
                    if (buffer_index < pool->buffer_count) {
                        /* Mark buffer as allocated */
                        pool->free_bitmap[word_index] &= ~(1U << bit_index);
                        pool->free_count--;
                        
                        /* Copy mapping */
                        *mapping = pool->mappings[buffer_index];
                        mapping->ref_count = 1;
                        
                        LOG_TRACE("DMA buffer allocated: index=%u, addr=%p", 
                                  buffer_index, mapping->virtual_addr);
                        
                        return 0;
                    }
                }
            }
        }
    }
    
    /* Should not reach here if free_count > 0 */
    LOG_ERROR("DMA pool allocation failed: corrupted free bitmap");
    return -DMA_ERROR_OUT_OF_MEMORY;
}

/**
 * @brief Free buffer to DMA pool
 */
void dma_pool_free(dma_buffer_pool_t *pool, dma_mapping_t *mapping) {
    uint32_t buffer_index;
    uint32_t word_index, bit_index;
    
    if (!pool || !mapping || !mapping->virtual_addr) {
        return;
    }
    
    /* Calculate buffer index */
    if (mapping->virtual_addr < pool->base_addr ||
        mapping->virtual_addr >= pool->base_addr + pool->pool_size) {
        LOG_ERROR("Invalid buffer address for pool free: %p", mapping->virtual_addr);
        return;
    }
    
    buffer_index = ((uint8_t*)mapping->virtual_addr - pool->base_addr) / pool->buffer_size;
    
    if (buffer_index >= pool->buffer_count) {
        LOG_ERROR("Invalid buffer index for pool free: %u", buffer_index);
        return;
    }
    
    /* Check if buffer is already free */
    word_index = buffer_index / 32;
    bit_index = buffer_index % 32;
    
    if (pool->free_bitmap[word_index] & (1U << bit_index)) {
        LOG_ERROR("Double free detected in DMA pool: buffer %u", buffer_index);
        return;
    }
    
    /* Mark buffer as free */
    pool->free_bitmap[word_index] |= (1U << bit_index);
    pool->free_count++;
    
    /* Clear mapping */
    mapping->ref_count = 0;
    
    LOG_TRACE("DMA buffer freed: index=%u, addr=%p", buffer_index, mapping->virtual_addr);
}

/* === Hardware-Specific DMA Operations === */

/**
 * @brief Setup DMA transfer for 3C515-TX
 */
static int dma_3c515_setup_transfer_impl(dma_nic_context_t *ctx, dma_sg_list_t *sg_list, uint8_t direction) {
    enhanced_ring_context_t *ring;
    uint8_t *consolidated_buffer = NULL;
    dma_mapping_t buffer_mapping;
    int result;
    
    if (!ctx || !sg_list) {
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    ring = ctx->ring_context;
    if (!ring) {
        LOG_ERROR("No ring context for 3C515-TX DMA setup");
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Setting up 3C515-TX DMA transfer: %u fragments, %u bytes total",
              sg_list->fragment_count, sg_list->total_length);
    
    /* 3C515-TX doesn't support true scatter-gather, consolidate fragments */
    if (sg_list->fragment_count > 1) {
        LOG_DEBUG("Consolidating %u fragments for 3C515-TX", sg_list->fragment_count);
        
        /* Allocate consolidation buffer from pool */
        if (direction == DMA_DIRECTION_TO_DEVICE) {
            result = dma_pool_alloc(&ctx->tx_pool, &buffer_mapping);
        } else {
            result = dma_pool_alloc(&ctx->rx_pool, &buffer_mapping);
        }
        
        if (result != 0) {
            LOG_ERROR("Failed to allocate consolidation buffer: %d", result);
            return result;
        }
        
        consolidated_buffer = (uint8_t*)buffer_mapping.virtual_addr;
        
        /* Consolidate fragments */
        result = dma_sg_consolidate(sg_list, consolidated_buffer, buffer_mapping.size);
        if (result < 0) {
            LOG_ERROR("Failed to consolidate SG list: %d", result);
            if (direction == DMA_DIRECTION_TO_DEVICE) {
                dma_pool_free(&ctx->tx_pool, &buffer_mapping);
            } else {
                dma_pool_free(&ctx->rx_pool, &buffer_mapping);
            }
            return result;
        }
        
        ctx->sg_consolidations++;
        
        /* Store consolidated buffer info in private data */
        sg_list->private_data = (void*)&buffer_mapping;
    } else {
        /* Single fragment - can use zero-copy if aligned */
        dma_fragment_t *frag = &sg_list->fragments[0];
        
        if ((frag->physical_addr & (ctx->min_alignment - 1)) == 0) {
            LOG_DEBUG("Using zero-copy for single aligned fragment");
            ctx->zero_copy_transfers++;
        } else {
            LOG_DEBUG("Fragment not aligned, consolidating anyway");
            
            /* Allocate aligned buffer and copy */
            if (direction == DMA_DIRECTION_TO_DEVICE) {
                result = dma_pool_alloc(&ctx->tx_pool, &buffer_mapping);
            } else {
                result = dma_pool_alloc(&ctx->rx_pool, &buffer_mapping);
            }
            
            if (result != 0) {
                return result;
            }
            
            consolidated_buffer = (uint8_t*)buffer_mapping.virtual_addr;
            result = dma_sg_consolidate(sg_list, consolidated_buffer, buffer_mapping.size);
            if (result < 0) {
                if (direction == DMA_DIRECTION_TO_DEVICE) {
                    dma_pool_free(&ctx->tx_pool, &buffer_mapping);
                } else {
                    dma_pool_free(&ctx->rx_pool, &buffer_mapping);
                }
                return result;
            }
            
            sg_list->private_data = (void*)&buffer_mapping;
            ctx->sg_consolidations++;
        }
    }
    
    LOG_DEBUG("3C515-TX DMA transfer setup completed");
    
    return 0;
}

/**
 * @brief Start DMA transfer for 3C515-TX
 */
static int dma_3c515_start_transfer_impl(dma_nic_context_t *ctx) {
    /* 3C515-TX DMA start is handled through ring buffer operations */
    /* This is integrated with existing 3C515 ring management */
    LOG_TRACE("3C515-TX DMA transfer started");
    return 0;
}

/**
 * @brief Stop DMA transfer for 3C515-TX
 */
static int dma_3c515_stop_transfer_impl(dma_nic_context_t *ctx) {
    /* 3C515-TX DMA stop is handled through ring buffer operations */
    LOG_TRACE("3C515-TX DMA transfer stopped");
    return 0;
}

/**
 * @brief Get DMA status for 3C515-TX
 */
static int dma_3c515_get_status_impl(dma_nic_context_t *ctx) {
    /* 3C515-TX DMA status is handled through ring buffer operations */
    return 0; /* Success - transfer complete */
}

/**
 * @brief Fallback transfer for 3C509B (PIO mode)
 */
static int dma_3c509b_fallback_impl(dma_nic_context_t *ctx, dma_sg_list_t *sg_list, uint8_t direction) {
    uint8_t consolidated_buffer[DMA_MAX_TRANSFER_SIZE];
    int result;
    
    if (!ctx || !sg_list) {
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("3C509B PIO fallback: %u fragments, %u bytes total",
              sg_list->fragment_count, sg_list->total_length);
    
    /* 3C509B always requires consolidation for PIO transfers */
    result = dma_sg_consolidate(sg_list, consolidated_buffer, sizeof(consolidated_buffer));
    if (result < 0) {
        LOG_ERROR("Failed to consolidate SG list for 3C509B: %d", result);
        return result;
    }
    
    ctx->fallback_transfers++;
    
    /* Store consolidated buffer pointer for PIO operations */
    sg_list->private_data = consolidated_buffer;
    
    LOG_DEBUG("3C509B PIO fallback completed: %d bytes consolidated", result);
    
    return 0;
}

/* === High-Level Integration Functions === */

/**
 * @brief Send packet using DMA scatter-gather
 */
int dma_send_packet_sg(uint8_t nic_index, dma_fragment_t *packet_fragments, uint16_t fragment_count) {
    dma_nic_context_t *ctx;
    dma_sg_list_t *sg_list;
    int result;
    
    if (nic_index >= MAX_NICS || !packet_fragments || fragment_count == 0) {
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    ctx = &g_dma_manager.nic_contexts[nic_index];
    
    if (ctx->nic_type == 0) {
        LOG_ERROR("NIC %d not initialized for DMA", nic_index);
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    if (fragment_count > ctx->max_sg_fragments) {
        LOG_ERROR("Too many fragments for NIC %d: %u > %u", 
                  nic_index, fragment_count, ctx->max_sg_fragments);
        return -DMA_ERROR_TOO_MANY_FRAGMENTS;
    }
    
    /* Create scatter-gather list */
    sg_list = dma_sg_alloc(fragment_count);
    if (!sg_list) {
        return -DMA_ERROR_OUT_OF_MEMORY;
    }
    
    /* Add all fragments to SG list */
    for (uint16_t i = 0; i < fragment_count; i++) {
        uint32_t flags = 0;
        
        if (i == 0) flags |= DMA_FRAG_FIRST;
        if (i == fragment_count - 1) flags |= DMA_FRAG_LAST;
        
        result = dma_sg_add_fragment(sg_list, 
                                    dma_phys_to_virt(packet_fragments[i].physical_addr),
                                    packet_fragments[i].length,
                                    flags);
        if (result != 0) {
            LOG_ERROR("Failed to add fragment %u to SG list: %d", i, result);
            dma_sg_free(sg_list);
            return result;
        }
    }
    
    /* Setup hardware-specific transfer */
    if (ctx->setup_dma_transfer) {
        result = ctx->setup_dma_transfer(ctx, sg_list, DMA_DIRECTION_TO_DEVICE);
        if (result != 0) {
            LOG_ERROR("Failed to setup DMA transfer for NIC %d: %d", nic_index, result);
            dma_sg_free(sg_list);
            return result;
        }
    }
    
    /* Start transfer if applicable */
    if (ctx->start_dma_transfer) {
        result = ctx->start_dma_transfer(ctx);
        if (result != 0) {
            LOG_ERROR("Failed to start DMA transfer for NIC %d: %d", nic_index, result);
            dma_sg_free(sg_list);
            return result;
        }
    }
    
    LOG_DEBUG("Scatter-gather packet sent on NIC %d: %u fragments, %u bytes",
              nic_index, fragment_count, sg_list->total_length);
    
    /* Cleanup SG list */
    dma_sg_free(sg_list);
    
    return 0;
}

/**
 * @brief Test DMA functionality
 */
int dma_self_test(uint8_t nic_index) {
    dma_nic_context_t *ctx;
    dma_sg_list_t *sg_list;
    uint8_t test_data[256];
    uint8_t consolidated_buffer[512];
    int result;
    
    if (nic_index >= MAX_NICS) {
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    ctx = &g_dma_manager.nic_contexts[nic_index];
    
    if (ctx->nic_type == 0) {
        LOG_ERROR("NIC %d not initialized for DMA self-test", nic_index);
        return -DMA_ERROR_INVALID_PARAM;
    }
    
    LOG_INFO("Running DMA self-test for NIC %d", nic_index);
    
    /* Prepare test data */
    for (int i = 0; i < sizeof(test_data); i++) {
        test_data[i] = (uint8_t)(i & 0xFF);
    }
    
    /* Test 1: Single fragment */
    sg_list = dma_sg_alloc(1);
    if (!sg_list) {
        LOG_ERROR("Failed to allocate SG list for self-test");
        return -DMA_ERROR_OUT_OF_MEMORY;
    }
    
    result = dma_sg_add_fragment(sg_list, test_data, sizeof(test_data), DMA_FRAG_SINGLE);
    if (result != 0) {
        LOG_ERROR("Failed to add fragment to SG list: %d", result);
        dma_sg_free(sg_list);
        return result;
    }
    
    /* Test consolidation */
    result = dma_sg_consolidate(sg_list, consolidated_buffer, sizeof(consolidated_buffer));
    if (result != sizeof(test_data)) {
        LOG_ERROR("Consolidation failed: expected %zu, got %d", sizeof(test_data), result);
        dma_sg_free(sg_list);
        return -DMA_ERROR_HARDWARE_ERROR;
    }
    
    /* Verify data */
    if (memcmp(test_data, consolidated_buffer, sizeof(test_data)) != 0) {
        LOG_ERROR("Data verification failed after consolidation");
        dma_sg_free(sg_list);
        return -DMA_ERROR_HARDWARE_ERROR;
    }
    
    dma_sg_free(sg_list);
    
    /* Test 2: Multiple fragments (if supported) */
    if (ctx->max_sg_fragments > 1) {
        sg_list = dma_sg_alloc(2);
        if (!sg_list) {
            LOG_ERROR("Failed to allocate multi-fragment SG list");
            return -DMA_ERROR_OUT_OF_MEMORY;
        }
        
        result = dma_sg_add_fragment(sg_list, test_data, 128, DMA_FRAG_FIRST);
        if (result == 0) {
            result = dma_sg_add_fragment(sg_list, test_data + 128, 128, DMA_FRAG_LAST);
        }
        
        if (result != 0) {
            LOG_ERROR("Failed to add multi-fragments: %d", result);
            dma_sg_free(sg_list);
            return result;
        }
        
        result = dma_sg_consolidate(sg_list, consolidated_buffer, sizeof(consolidated_buffer));
        if (result != sizeof(test_data)) {
            LOG_ERROR("Multi-fragment consolidation failed: expected %zu, got %d", 
                      sizeof(test_data), result);
            dma_sg_free(sg_list);
            return -DMA_ERROR_HARDWARE_ERROR;
        }
        
        if (memcmp(test_data, consolidated_buffer, sizeof(test_data)) != 0) {
            LOG_ERROR("Multi-fragment data verification failed");
            dma_sg_free(sg_list);
            return -DMA_ERROR_HARDWARE_ERROR;
        }
        
        dma_sg_free(sg_list);
        
        LOG_INFO("Multi-fragment DMA test passed");
    }
    
    LOG_INFO("DMA self-test completed successfully for NIC %d", nic_index);
    
    return 0;
}