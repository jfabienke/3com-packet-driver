/**
 * @file vds.c
 * @brief Virtual DMA Services (VDS) for 3C515-TX bus master support
 * 
 * Provides memory locking and physical address translation for the
 * 3C515-TX's bus master DMA engine when running under V86 mode with
 * memory managers like EMM386, QEMM, 386MAX.
 * 
 * NOTE: The 3C509B uses PIO only and does not require VDS.
 *       The 3C515-TX has its own bus master DMA engine and does
 *       NOT use the system 8237A DMA controller.
 */

#include <dos.h>
#include <string.h>
#include <stdlib.h>
#include "../include/vds.h"
#include "../include/logging.h"

/* Helper macro */
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

/* VDS service availability flag */
static bool g_vds_available = false;
static vds_version_t g_vds_version = {0};
static vds_capabilities_t g_vds_caps = {0};

/* VDS statistics for telemetry */
static struct {
    uint32_t lock_attempts;
    uint32_t lock_successes;
    uint32_t lock_failures;
    uint32_t unlock_attempts;
    uint32_t unlock_successes;
    uint32_t unlock_failures;
    uint32_t scatter_gather_locks;
    uint32_t contiguous_violations;
    uint32_t alignment_violations;
} g_vds_stats = {0};

/* Forward declaration */
static int vds_lock_region_fallback(void __far *addr, uint32_t len, uint16_t flags,
                                    vds_sg_entry_t __far *sg_list, uint16_t sg_list_max,
                                    uint16_t *out_sg_count, uint16_t *out_lock_handle);

/**
 * @brief Check if VDS services are available
 * 
 * Checks INT 4Bh vector to determine if VDS is installed
 * This is required for EMM386, QEMM, 386MAX compatibility
 */
bool vds_is_available(void) {
    union REGS regs;
    struct SREGS sregs;
    uint32_t vector_addr;
    uint8_t __far *vector_ptr;
    
    /* Check if already initialized */
    if (g_vds_available) {
        return true;
    }
    
    /* Get INT 4Bh vector */
    _disable();
    vector_ptr = (uint8_t __far *)_dos_getvect(0x4B);
    _enable();
    
    /* NULL vector means VDS not installed */
    if (!vector_ptr) {
        LOG_DEBUG("VDS: INT 4Bh vector is NULL - VDS not available");
        return false;
    }
    
    /* Convert to linear address */
    vector_addr = ((uint32_t)FP_SEG(vector_ptr) << 4) + FP_OFF(vector_ptr);
    
    /* Check if vector points to valid code (not 0000:0000 or FFFF:FFFF) */
    if (vector_addr == 0x00000000UL || vector_addr == 0xFFFFFFFFUL) {
        LOG_DEBUG("VDS: INT 4Bh vector invalid (%08lX) - VDS not available", vector_addr);
        return false;
    }
    
    /* Try to get VDS version to confirm it's really there */
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x8102;  /* Get VDS Version */
    segread(&sregs);
    
    int86x(0x4B, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        LOG_DEBUG("VDS: Version query failed (CF=%d) - VDS not available", regs.x.cflag);
        return false;
    }
    
    /* Store version information */
    g_vds_version.major = (regs.x.ax >> 8) & 0xFF;
    g_vds_version.minor = regs.x.ax & 0xFF;
    g_vds_version.oem_number = regs.x.bx;
    g_vds_version.oem_revision = regs.x.cx;
    g_vds_version.flags = regs.x.dx;
    
    /* Check for required VDS version (1.0 or higher) */
    if (g_vds_version.major < 1) {
        LOG_WARNING("VDS: Version %d.%d too old, need 1.0+", 
                    g_vds_version.major, g_vds_version.minor);
        return false;
    }
    
    LOG_INFO("VDS: Available - Version %d.%d OEM=%04X Rev=%04X Flags=%04X",
             g_vds_version.major, g_vds_version.minor,
             g_vds_version.oem_number, g_vds_version.oem_revision,
             g_vds_version.flags);
    
    /* Get VDS capabilities */
    vds_get_capabilities(&g_vds_caps);
    
    g_vds_available = true;
    return true;
}

/**
 * @brief Initialize VDS subsystem
 * 
 * Must be called during driver initialization
 */
int vds_init(void) {
    LOG_INFO("Initializing VDS subsystem");
    
    /* Reset statistics */
    memset(&g_vds_stats, 0, sizeof(g_vds_stats));
    
    /* Check for VDS availability */
    if (vds_is_available()) {
        LOG_INFO("VDS initialized successfully");
        
        /* Report telemetry integration */
        extern void telemetry_record_vds_init(bool available, uint8_t major, uint8_t minor);
        telemetry_record_vds_init(true, g_vds_version.major, g_vds_version.minor);
        
        return 0;
    } else {
        LOG_INFO("VDS not available - running in real mode or no memory manager");
        
        /* Report to telemetry */
        extern void telemetry_record_vds_init(bool available, uint8_t major, uint8_t minor);
        telemetry_record_vds_init(false, 0, 0);
        
        return 0;  /* Not an error - VDS is optional */
    }
}

/**
 * @brief Get VDS version information
 */
bool vds_get_version(vds_version_t *version) {
    if (!version || !g_vds_available) {
        return false;
    }
    
    *version = g_vds_version;
    return true;
}

/**
 * @brief Get VDS capabilities
 */
bool vds_get_capabilities(vds_capabilities_t *caps) {
    union REGS regs;
    struct SREGS sregs;
    
    if (!caps || !g_vds_available) {
        return false;
    }
    
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x8103;  /* Get VDS Capabilities */
    segread(&sregs);
    
    int86x(0x4B, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        LOG_ERROR("VDS: Get capabilities failed");
        return false;
    }
    
    caps->max_dma_buffer_size = ((uint32_t)regs.x.dx << 16) | regs.x.cx;
    caps->flags = regs.x.bx;
    caps->supports_scatter_gather = (caps->flags & VDS_CAP_SCATTER_GATHER) != 0;
    caps->supports_64k_aligned = (caps->flags & VDS_CAP_64K_ALIGNED) != 0;
    caps->supports_contiguous = (caps->flags & VDS_CAP_CONTIGUOUS) != 0;
    
    LOG_DEBUG("VDS Capabilities: MaxDMA=%lu Flags=%04X SG=%d 64K=%d Contig=%d",
              caps->max_dma_buffer_size, caps->flags,
              caps->supports_scatter_gather,
              caps->supports_64k_aligned,
              caps->supports_contiguous);
    
    if (caps == &g_vds_caps) {
        /* Storing globally, no need to copy */
    } else {
        *caps = g_vds_caps;
    }
    
    return true;
}

/**
 * @brief Lock DMA region for device access
 * 
 * This function locks a memory region and returns its physical address.
 * The region remains locked until vds_unlock_region() is called.
 * 
 * @param buffer Virtual address of buffer
 * @param length Buffer length
 * @param flags VDS flags (VDS_NO_AUTO_REMAP, etc.)
 * @param mapping Output mapping information
 * @return true on success, false on failure
 */
bool vds_lock_region(void *buffer, uint32_t length, uint16_t flags, vds_mapping_t *mapping) {
    union REGS regs;
    struct SREGS sregs;
    vds_dds_t dds;
    
    if (!buffer || !length || !mapping || !g_vds_available) {
        LOG_ERROR("VDS: Invalid parameters for lock_region");
        return false;
    }
    
    g_vds_stats.lock_attempts++;
    
    /* Initialize DDS structure */
    memset(&dds, 0, sizeof(dds));
    dds.region_size = length;
    dds.linear_offset = FP_OFF(buffer);
    dds.region_segment = FP_SEG(buffer);
    
    /* Setup for INT 4Bh call */
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x8103;  /* Lock DMA Region */
    regs.x.dx = flags;
    
    /* ES:DI points to DDS */
    segread(&sregs);
    sregs.es = FP_SEG(&dds);
    regs.x.di = FP_OFF(&dds);
    
    int86x(0x4B, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        LOG_ERROR("VDS: Lock region failed - AX=%04X", regs.x.ax);
        g_vds_stats.lock_failures++;
        
        /* Decode error code */
        switch (regs.x.ax) {
            case 0x0001:
                LOG_ERROR("VDS: Region not contiguous");
                g_vds_stats.contiguous_violations++;
                break;
            case 0x0002:
                LOG_ERROR("VDS: Region crossed 64K boundary");
                break;
            case 0x0003:
                LOG_ERROR("VDS: Unable to lock pages");
                break;
            case 0x0004:
                LOG_ERROR("VDS: No buffer available");
                break;
            case 0x0005:
                LOG_ERROR("VDS: Region too large");
                break;
            case 0x0006:
                LOG_ERROR("VDS: Buffer in use");
                break;
            case 0x0007:
                LOG_ERROR("VDS: Invalid region");
                break;
            case 0x0008:
                LOG_ERROR("VDS: Region not aligned");
                g_vds_stats.alignment_violations++;
                break;
            default:
                LOG_ERROR("VDS: Unknown error %04X", regs.x.ax);
                break;
        }
        
        /* Report to telemetry */
        extern void telemetry_record_vds_lock_failure(uint16_t error_code);
        telemetry_record_vds_lock_failure(regs.x.ax);
        
        return false;
    }
    
    /* Extract physical address and buffer info from DDS */
    mapping->physical_addr = dds.physical_address;
    mapping->buffer_addr = buffer;
    mapping->buffer_size = length;
    mapping->lock_handle = dds.buffer_id;
    mapping->needs_unlock = true;
    mapping->is_contiguous = !(dds.flags & VDS_DDS_NOT_CONTIGUOUS);
    mapping->uses_buffer = (dds.flags & VDS_DDS_BUFFER_USED) != 0;
    
    /* Check if VDS had to use a buffer (remapping occurred) */
    if (mapping->uses_buffer) {
        LOG_DEBUG("VDS: Used internal buffer for remapping");
        /* The physical address now points to VDS's internal buffer */
        /* We need to copy data there for TX operations */
    }
    
    LOG_DEBUG("VDS: Locked region - Virt=%p Phys=%08lX Size=%lu Handle=%04X Contig=%d",
              buffer, mapping->physical_addr, length, 
              mapping->lock_handle, mapping->is_contiguous);
    
    g_vds_stats.lock_successes++;
    
    /* Report to telemetry */
    extern void telemetry_record_vds_lock_success(uint32_t size, bool uses_buffer);
    telemetry_record_vds_lock_success(length, mapping->uses_buffer);
    
    return true;
}

/**
 * @brief Unlock previously locked DMA region
 * 
 * @param mapping Mapping returned by vds_lock_region()
 * @return true on success, false on failure
 */
bool vds_unlock_region(vds_mapping_t *mapping) {
    union REGS regs;
    struct SREGS sregs;
    vds_dds_t dds;
    
    if (!mapping || !mapping->needs_unlock || !g_vds_available) {
        return false;
    }
    
    g_vds_stats.unlock_attempts++;
    
    /* Initialize DDS with our mapping info */
    memset(&dds, 0, sizeof(dds));
    dds.region_size = mapping->buffer_size;
    dds.linear_offset = FP_OFF(mapping->buffer_addr);
    dds.region_segment = FP_SEG(mapping->buffer_addr);
    dds.buffer_id = mapping->lock_handle;
    dds.physical_address = mapping->physical_addr;
    
    /* Setup for INT 4Bh call */
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x8104;  /* Unlock DMA Region */
    regs.x.dx = 0;       /* Flags must be 0 for unlock */
    
    /* ES:DI points to DDS */
    segread(&sregs);
    sregs.es = FP_SEG(&dds);
    regs.x.di = FP_OFF(&dds);
    
    int86x(0x4B, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        LOG_ERROR("VDS: Unlock region failed - AX=%04X", regs.x.ax);
        g_vds_stats.unlock_failures++;
        return false;
    }
    
    LOG_DEBUG("VDS: Unlocked region - Handle=%04X", mapping->lock_handle);
    
    mapping->needs_unlock = false;
    g_vds_stats.unlock_successes++;
    return true;
}

/**
 * @brief Lock scatter-gather list for DMA
 * 
 * Locks multiple memory regions in a single call
 * 
 * @param sg_list Array of scatter-gather descriptors
 * @param count Number of descriptors
 * @param flags VDS flags
 * @return true if all regions locked successfully
 */
bool vds_lock_scatter_gather(vds_sg_descriptor_t *sg_list, uint16_t count, uint16_t flags) {
    union REGS regs;
    struct SREGS sregs;
    vds_edds_t *edds_list;
    uint16_t i;
    bool success = true;
    
    if (!sg_list || count == 0 || !g_vds_available) {
        return false;
    }
    
    /* Check if scatter-gather is supported */
    if (!g_vds_caps.supports_scatter_gather) {
        LOG_WARNING("VDS: Scatter-gather not supported, using individual locks");
        
        /* Fall back to individual locks */
        for (i = 0; i < count; i++) {
            if (!vds_lock_region(sg_list[i].buffer, sg_list[i].length, 
                               flags, &sg_list[i].mapping)) {
                /* Unlock previously locked regions on failure */
                while (i > 0) {
                    i--;
                    vds_unlock_region(&sg_list[i].mapping);
                }
                return false;
            }
        }
        return true;
    }
    
    g_vds_stats.scatter_gather_locks++;
    
    /* Allocate EDDS array */
    edds_list = malloc(sizeof(vds_edds_t) * count);
    if (!edds_list) {
        LOG_ERROR("VDS: Failed to allocate EDDS list");
        return false;
    }
    
    /* Initialize EDDS structures */
    for (i = 0; i < count; i++) {
        edds_list[i].region_size = sg_list[i].length;
        edds_list[i].linear_offset = FP_OFF(sg_list[i].buffer);
        edds_list[i].region_segment = FP_SEG(sg_list[i].buffer);
        edds_list[i].number_avail = 0;
        edds_list[i].number_used = 0;
        edds_list[i].region_0_physical = 0;
        edds_list[i].region_0_size = 0;
    }
    
    /* Setup for INT 4Bh call */
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x8105;  /* Scatter-Gather Lock Region */
    regs.x.dx = flags;
    regs.x.cx = count;
    
    /* ES:DI points to EDDS array */
    segread(&sregs);
    sregs.es = FP_SEG(edds_list);
    regs.x.di = FP_OFF(edds_list);
    
    int86x(0x4B, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        LOG_ERROR("VDS: Scatter-gather lock failed - AX=%04X", regs.x.ax);
        success = false;
    } else {
        /* Extract physical addresses from EDDS */
        for (i = 0; i < count; i++) {
            sg_list[i].mapping.physical_addr = edds_list[i].region_0_physical;
            sg_list[i].mapping.buffer_addr = sg_list[i].buffer;
            sg_list[i].mapping.buffer_size = sg_list[i].length;
            sg_list[i].mapping.needs_unlock = true;
            sg_list[i].mapping.is_contiguous = (edds_list[i].number_used == 1);
            
            LOG_DEBUG("VDS: SG[%u] locked - Phys=%08lX Size=%lu Contig=%d",
                     i, sg_list[i].mapping.physical_addr, 
                     sg_list[i].length, sg_list[i].mapping.is_contiguous);
        }
    }
    
    free(edds_list);
    return success;
}

/**
 * @brief Unlock scatter-gather list
 * 
 * @param sg_list Array of scatter-gather descriptors
 * @param count Number of descriptors
 * @return true if all regions unlocked successfully
 */
bool vds_unlock_scatter_gather(vds_sg_descriptor_t *sg_list, uint16_t count) {
    uint16_t i;
    bool all_success = true;
    
    if (!sg_list || count == 0 || !g_vds_available) {
        return false;
    }
    
    /* Unlock each region individually */
    for (i = 0; i < count; i++) {
        if (sg_list[i].mapping.needs_unlock) {
            if (!vds_unlock_region(&sg_list[i].mapping)) {
                all_success = false;
            }
        }
    }
    
    return all_success;
}

/**
 * @brief Copy data to/from VDS buffer if remapping occurred
 * 
 * @param mapping VDS mapping
 * @param buffer Source/destination buffer
 * @param length Data length
 * @param to_device true for TX (copy to VDS buffer), false for RX
 * @return true on success
 */
bool vds_copy_buffer(vds_mapping_t *mapping, void *buffer, uint32_t length, bool to_device) {
    union REGS regs;
    struct SREGS sregs;
    
    if (!mapping || !buffer || !length || !g_vds_available) {
        return false;
    }
    
    /* Only needed if VDS used internal buffer */
    if (!mapping->uses_buffer) {
        return true;  /* No copy needed */
    }
    
    LOG_DEBUG("VDS: Copying %lu bytes %s VDS buffer",
              length, to_device ? "to" : "from");
    
    /* Use VDS copy function if available (VDS 2.0+) */
    if (g_vds_version.major >= 2) {
        memset(&regs, 0, sizeof(regs));
        regs.x.ax = to_device ? 0x8106 : 0x8107;  /* Copy to/from buffer */
        regs.x.bx = mapping->lock_handle;
        regs.x.cx = length & 0xFFFF;
        regs.x.dx = (length >> 16) & 0xFFFF;
        
        segread(&sregs);
        sregs.es = FP_SEG(buffer);
        regs.x.di = FP_OFF(buffer);
        
        int86x(0x4B, &regs, &regs, &sregs);
        
        if (!regs.x.cflag) {
            return true;
        }
        LOG_WARNING("VDS: Copy buffer failed, using manual copy");
    }
    
    /* Manual copy as fallback */
    /* Note: This requires knowing the VDS buffer virtual address,
     * which we don't have. In practice, VDS should handle this
     * transparently. */
    LOG_WARNING("VDS: Manual buffer copy not implemented");
    return false;
}

/**
 * @brief Check if address is ISA DMA compatible
 * 
 * @param physical_addr Physical address
 * @param length Buffer length
 * @return true if compatible with ISA DMA constraints
 */
bool vds_is_isa_compatible(uint32_t physical_addr, uint32_t length) {
    /* Check 16MB ISA limit */
    if (physical_addr >= 0x1000000UL) {
        return false;
    }
    
    if ((physical_addr + length) > 0x1000000UL) {
        return false;
    }
    
    /* Check 64KB boundary crossing */
    uint32_t start_64k_page = physical_addr & 0xFFFF0000UL;
    uint32_t end_64k_page = (physical_addr + length - 1) & 0xFFFF0000UL;
    
    if (start_64k_page != end_64k_page) {
        return false;
    }
    
    return true;
}

/**
 * @brief Request DMA buffer from VDS
 * 
 * Allocates a DMA-safe buffer from VDS pool
 * 
 * @param size Buffer size needed
 * @param physical_addr Output physical address
 * @return Virtual address of buffer, or NULL on failure
 */
void* vds_request_buffer(uint32_t size, uint32_t *physical_addr) {
    union REGS regs;
    struct SREGS sregs;
    
    if (!size || !physical_addr || !g_vds_available) {
        return NULL;
    }
    
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x8107;  /* Request DMA Buffer */
    regs.x.cx = size & 0xFFFF;
    regs.x.dx = (size >> 16) & 0xFFFF;
    
    segread(&sregs);
    int86x(0x4B, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        LOG_ERROR("VDS: Request buffer failed - AX=%04X", regs.x.ax);
        return NULL;
    }
    
    /* Physical address in DX:AX */
    *physical_addr = ((uint32_t)regs.x.dx << 16) | regs.x.ax;
    
    /* Virtual address in ES:DI */
    void __far *buffer = MK_FP(sregs.es, regs.x.di);
    
    LOG_DEBUG("VDS: Allocated buffer - Virt=%p Phys=%08lX Size=%lu",
              buffer, *physical_addr, size);
    
    return buffer;
}

/**
 * @brief Release DMA buffer back to VDS
 * 
 * @param buffer Virtual address of buffer
 * @param physical_addr Physical address of buffer
 * @return true on success
 */
bool vds_release_buffer(void *buffer, uint32_t physical_addr) {
    union REGS regs;
    struct SREGS sregs;
    
    if (!buffer || !g_vds_available) {
        return false;
    }
    
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x8108;  /* Release DMA Buffer */
    regs.x.dx = (physical_addr >> 16) & 0xFFFF;
    regs.x.ax = physical_addr & 0xFFFF;
    
    segread(&sregs);
    sregs.es = FP_SEG(buffer);
    regs.x.di = FP_OFF(buffer);
    
    int86x(0x4B, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        LOG_ERROR("VDS: Release buffer failed - AX=%04X", regs.x.ax);
        return false;
    }
    
    LOG_DEBUG("VDS: Released buffer - Virt=%p Phys=%08lX", buffer, physical_addr);
    return true;
}

/**
 * @brief Disable VDS page remapping
 * 
 * Tells VDS not to remap pages for this process.
 * Used when driver manages its own DMA buffers.
 * 
 * @return true on success
 */
bool vds_disable_translation(void) {
    union REGS regs;
    struct SREGS sregs;
    
    if (!g_vds_available) {
        return false;
    }
    
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x810B;  /* Disable DMA Translation */
    regs.x.bx = 0;       /* Disable for current DMA controller */
    
    segread(&sregs);
    int86x(0x4B, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        LOG_ERROR("VDS: Disable translation failed - AX=%04X", regs.x.ax);
        return false;
    }
    
    LOG_INFO("VDS: DMA translation disabled");
    return true;
}

/**
 * @brief Enable VDS page remapping
 * 
 * Re-enables VDS page remapping after it was disabled
 * 
 * @return true on success
 */
bool vds_enable_translation(void) {
    union REGS regs;
    struct SREGS sregs;
    
    if (!g_vds_available) {
        return false;
    }
    
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x810C;  /* Enable DMA Translation */
    regs.x.bx = 0;       /* Enable for current DMA controller */
    
    segread(&sregs);
    int86x(0x4B, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        LOG_ERROR("VDS: Enable translation failed - AX=%04X", regs.x.ax);
        return false;
    }
    
    LOG_INFO("VDS: DMA translation enabled");
    return true;
}

/**
 * @brief Get VDS statistics for telemetry
 * 
 * @param stats Output statistics structure
 */
void vds_get_statistics(vds_statistics_t *stats) {
    if (!stats) {
        return;
    }
    
    stats->available = g_vds_available;
    stats->version_major = g_vds_version.major;
    stats->version_minor = g_vds_version.minor;
    stats->lock_attempts = g_vds_stats.lock_attempts;
    stats->lock_successes = g_vds_stats.lock_successes;
    stats->lock_failures = g_vds_stats.lock_failures;
    stats->unlock_attempts = g_vds_stats.unlock_attempts;
    stats->unlock_successes = g_vds_stats.unlock_successes;
    stats->unlock_failures = g_vds_stats.unlock_failures;
    stats->scatter_gather_locks = g_vds_stats.scatter_gather_locks;
    stats->contiguous_violations = g_vds_stats.contiguous_violations;
    stats->alignment_violations = g_vds_stats.alignment_violations;
}

/**
 * @brief Print VDS statistics
 */
void vds_print_statistics(void) {
    LOG_INFO("=== VDS Statistics ===");
    LOG_INFO("Available: %s", g_vds_available ? "Yes" : "No");
    
    if (g_vds_available) {
        LOG_INFO("Version: %d.%d", g_vds_version.major, g_vds_version.minor);
        LOG_INFO("Lock attempts: %lu", g_vds_stats.lock_attempts);
        LOG_INFO("Lock successes: %lu", g_vds_stats.lock_successes);
        LOG_INFO("Lock failures: %lu", g_vds_stats.lock_failures);
        LOG_INFO("Unlock attempts: %lu", g_vds_stats.unlock_attempts);
        LOG_INFO("Unlock successes: %lu", g_vds_stats.unlock_successes);
        LOG_INFO("Unlock failures: %lu", g_vds_stats.unlock_failures);
        LOG_INFO("Scatter-gather locks: %lu", g_vds_stats.scatter_gather_locks);
        LOG_INFO("Contiguous violations: %lu", g_vds_stats.contiguous_violations);
        LOG_INFO("Alignment violations: %lu", g_vds_stats.alignment_violations);
    }
}

/**
 * @brief Copy data to bus master buffer
 * 
 * Copies data from user buffer to a buffer suitable for 3C515-TX
 * bus master DMA operations. Handles physical address constraints.
 * 
 * @param dma_buffer Bus master buffer descriptor
 * @param src_buffer Source data buffer
 * @param length Number of bytes to copy
 * @return true on success, false on failure
 */
bool vds_copy_to_dma_buffer(vds_dma_descriptor_t *dma_buffer, void __far *src_buffer, uint32_t length) {
    union REGS regs;
    struct SREGS sregs;
    vds_dds_t dds;
    
    if (!dma_buffer || !src_buffer || length == 0) {
        return false;
    }
    
    /* GPT-5 Fix: Check VDS availability and provide fallback */
    if (!g_vds_available) {
        LOG_WARN("VDS: Copy to buffer using direct memory fallback (VDS not available)");
        /* Fallback: direct memory copy if buffers are accessible */
        
        /* GPT-5 Recommendation: Verify address is within real-mode range */
        if (dma_buffer->physical_addr + length > 0x100000UL) {
            LOG_ERROR("VDS: Fallback copy exceeds 1MB real-mode limit");
            return false;
        }
        
        /* GPT-5 A+ Enhancement: Check for 64KB segment boundary crossing */
        if (((dma_buffer->physical_addr & 0x0F) + length) > 0x10000UL) {
            LOG_ERROR("VDS: Fallback copy would cross 64KB segment boundary");
            return false;
        }
        
        /* GPT-5 Critical Fix: Cannot cast physical address directly to far pointer */
        /* Physical address must be converted to segment:offset format */
        void __far *dst_ptr = MK_FP((uint16_t)(dma_buffer->physical_addr >> 4), 
                                    (uint16_t)(dma_buffer->physical_addr & 0x0F));
        _fmemcpy(dst_ptr, src_buffer, length);
        return true;
    }
    
    /* GPT-5 Fix: Properly setup DDS structure for VDS call */
    memset(&dds, 0, sizeof(dds));
    dds.region_size = length;
    dds.linear_offset = FP_OFF(src_buffer);
    dds.region_segment = FP_SEG(src_buffer);
    dds.buffer_id = dma_buffer->buffer_id;
    dds.physical_address = dma_buffer->physical_addr;
    
    /* Setup for INT 4Bh call */
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.x.ax = VDS_COPY_TO_BUFFER;  /* 0x8109 - Copy Into DMA Buffer */
    regs.x.bx = 0;                    /* Flags (0 = no special options) */
    regs.x.cx = length & 0xFFFF;      /* Low word of size */
    regs.x.dx = (length >> 16) & 0xFFFF; /* High word of size */
    
    /* ES:DI points to DDS structure */
    segread(&sregs);
    sregs.es = FP_SEG(&dds);
    regs.x.di = FP_OFF(&dds);
    
    int86x(0x4B, &regs, &regs, &sregs);
    
    /* GPT-5 Fix: Check carry flag properly */
    if (regs.x.cflag) {
        LOG_ERROR("VDS: Copy to DMA buffer failed - CF=%d AX=%04X", 
                  regs.x.cflag, regs.x.ax);
        return false;
    }
    
    LOG_DEBUG("VDS: Copied %lu bytes to DMA buffer ID=%04X", length, dma_buffer->buffer_id);
    return true;
}

/**
 * @brief Copy data from bus master buffer
 * 
 * Copies received data from a 3C515-TX bus master buffer back to
 * user buffer after DMA completion.
 * 
 * @param dma_buffer Bus master buffer descriptor
 * @param dst_buffer Destination data buffer
 * @param length Number of bytes to copy
 * @return true on success, false on failure
 */
bool vds_copy_from_dma_buffer(vds_dma_descriptor_t *dma_buffer, void __far *dst_buffer, uint32_t length) {
    union REGS regs;
    struct SREGS sregs;
    vds_dds_t dds;
    
    if (!dma_buffer || !dst_buffer || length == 0) {
        return false;
    }
    
    /* GPT-5 Fix: Check VDS availability and provide fallback */
    if (!g_vds_available) {
        LOG_WARN("VDS: Copy from buffer using direct memory fallback (VDS not available)");
        /* Fallback: direct memory copy if buffers are accessible */
        
        /* GPT-5 Recommendation: Verify address is within real-mode range */
        if (dma_buffer->physical_addr + length > 0x100000UL) {
            LOG_ERROR("VDS: Fallback copy exceeds 1MB real-mode limit");
            return false;
        }
        
        /* GPT-5 A+ Enhancement: Check for 64KB segment boundary crossing */
        if (((dma_buffer->physical_addr & 0x0F) + length) > 0x10000UL) {
            LOG_ERROR("VDS: Fallback copy would cross 64KB segment boundary");
            return false;
        }
        
        /* GPT-5 Critical Fix: Cannot cast physical address directly to far pointer */
        /* Physical address must be converted to segment:offset format */
        void __far *src_ptr = MK_FP((uint16_t)(dma_buffer->physical_addr >> 4), 
                                    (uint16_t)(dma_buffer->physical_addr & 0x0F));
        _fmemcpy(dst_buffer, src_ptr, length);
        return true;
    }
    
    /* GPT-5 Fix: Properly setup DDS structure for VDS call */
    memset(&dds, 0, sizeof(dds));
    dds.region_size = length;
    dds.linear_offset = FP_OFF(dst_buffer);
    dds.region_segment = FP_SEG(dst_buffer);
    dds.buffer_id = dma_buffer->buffer_id;
    dds.physical_address = dma_buffer->physical_addr;
    
    /* Setup for INT 4Bh call */
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.x.ax = VDS_COPY_FROM_BUFFER;  /* 0x810A - Copy From DMA Buffer */
    regs.x.bx = 0;                      /* Flags (0 = no special options) */
    regs.x.cx = length & 0xFFFF;        /* Low word of size */
    regs.x.dx = (length >> 16) & 0xFFFF; /* High word of size */
    
    /* ES:DI points to DDS structure */
    segread(&sregs);
    sregs.es = FP_SEG(&dds);
    regs.x.di = FP_OFF(&dds);
    
    int86x(0x4B, &regs, &regs, &sregs);
    
    /* GPT-5 Fix: Check carry flag properly */
    if (regs.x.cflag) {
        LOG_ERROR("VDS: Copy from DMA buffer failed - CF=%d AX=%04X", 
                  regs.x.cflag, regs.x.ax);
        return false;
    }
    
    LOG_DEBUG("VDS: Copied %lu bytes from DMA buffer ID=%04X", length, dma_buffer->buffer_id);
    return true;
}

/* NOTE: 8237A DMA controller programming removed per GPT-5 recommendation.
 * Neither the 3C509B (PIO only) nor 3C515-TX (bus master) use system DMA channels.
 * VDS is retained only for bus master memory locking and address translation.
 */

/**
 * @brief Lock region with scatter/gather support (GPT-5 Fixed Implementation)
 * 
 * Properly locks a memory region and enumerates all physical fragments
 * according to VDS 1.0 specification.
 * 
 * @param addr Virtual address of buffer
 * @param len Buffer length
 * @param flags VDS flags (usually 0)
 * @param sg_list Output scatter/gather list
 * @param sg_list_max Maximum entries in sg_list
 * @param out_sg_count Output actual number of fragments
 * @param out_lock_handle Output lock handle for unlock
 * @return 0 on success, VDS error code on failure
 */
int vds_lock_region_sg(void __far *addr, uint32_t len, uint16_t flags,
                       vds_sg_entry_t __far *sg_list, uint16_t sg_list_max,
                       uint16_t *out_sg_count, uint16_t *out_lock_handle)
{
    union REGS regs;
    struct SREGS sregs;
    
    /* VDS Scatter/Gather DDS structure per VDS 1.0 spec */
    /* GPT-5 Fix: Explicit pack to ensure no padding */
    #pragma pack(push, 1)
    struct {
        uint32_t region_size;        /* 00h: Size of region */
        uint32_t linear_offset;      /* 04h: Linear offset (0 for real mode) */
        uint16_t buffer_seg;         /* 08h: Segment */
        uint16_t reserved1;          /* 0Ah: Reserved */
        uint16_t buffer_off;         /* 0Ch: Offset */
        uint16_t num_avail;          /* 0Eh: Number of physical regions available */
        uint16_t num_used;           /* 10h: Number of physical regions used (output) */
        uint16_t region_0_size;      /* 12h: Size of first physical region (output) */
        uint32_t region_0_phys;      /* 14h: Physical address of first region (output) */
    } sg_dds;
    
    /* Extended scatter-gather list for additional regions */
    /* GPT-5 Fix: Ensure packed layout matches VDS expectation */
    #pragma pack(push, 1)
    struct {
        uint16_t size;
        uint32_t phys;
    } sg_regions[16];  /* Max 16 regions */
    #pragma pack(pop)
    #pragma pack(pop)  /* Pop the first pack(push, 1) */
    
    if (!addr || len == 0 || !sg_list || !out_sg_count || !out_lock_handle) {
        return VDS_INVALID_PARAMS;
    }
    
    if (!g_vds_available) {
        return VDS_NOT_SUPPORTED;
    }
    
    /* Initialize DDS for scatter-gather lock */
    memset(&sg_dds, 0, sizeof(sg_dds));
    sg_dds.region_size = len;
    sg_dds.linear_offset = 0;  /* Real mode */
    sg_dds.buffer_seg = FP_SEG(addr);
    sg_dds.buffer_off = FP_OFF(addr);
    /* GPT-5 Fix: num_avail should always be 17 (1 in DDS + 16 in array) */
    /* Don't cap by sg_list_max - we can accept all fragments VDS returns */
    sg_dds.num_avail = 17;  /* 1 in DDS + 16 in extended list */
    
    memset(sg_regions, 0, sizeof(sg_regions));
    
    /* GPT-5 Fix: Use VDS Scatter Lock (0x8105) instead of regular lock */
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = VDS_SCATTER_LOCK;  /* 0x8105 - Scatter/Gather Lock */
    /* GPT-5 Critical Fix: Flags go in BX, not DX */
    regs.x.bx = flags;  /* Flags in BX per VDS spec */
    
    segread(&sregs);
    sregs.es = FP_SEG(&sg_dds);
    regs.x.di = FP_OFF(&sg_dds);
    
    /* GPT-5 Fix: Use DS:SI for extended region list (VDS 1.0 spec) */
    sregs.ds = FP_SEG(sg_regions);
    regs.x.si = FP_OFF(sg_regions);
    
    int86x(0x4B, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        /* GPT-5 Fix: Error code is in AH, not AL */
        if (regs.h.ah == 0x01) {  /* Function not supported */
            LOG_WARNING("VDS: Scatter lock not supported, falling back to regular lock");
            return vds_lock_region_fallback(addr, len, flags, sg_list, sg_list_max, out_sg_count, out_lock_handle);
        }
        LOG_ERROR("VDS: Scatter lock failed with error 0x%02X", regs.h.ah);
        return regs.h.ah;
    }
    
    /* GPT-5 Critical Fix: Lock handle is returned in DX, fallback to AX if DX is zero */
    uint16_t handle = regs.x.dx ? regs.x.dx : regs.x.ax;
    *out_lock_handle = handle;
    
    /* Parse the returned scatter-gather list */
    uint16_t num_regions = sg_dds.num_used;
    if (num_regions == 0) {
        LOG_ERROR("VDS: Scatter lock returned no regions");
        vds_unlock_region_sg(*out_lock_handle);
        return VDS_INVALID_SIZE;
    }
    
    /* First region is in the DDS structure itself */
    if (num_regions > 0 && sg_list_max > 0) {
        sg_list[0].phys = sg_dds.region_0_phys;
        sg_list[0].len = sg_dds.region_0_size;
    }
    
    /* Additional regions are in the extended list */
    uint16_t i;
    for (i = 1; i < num_regions && i < sg_list_max; i++) {
        sg_list[i].phys = sg_regions[i-1].phys;
        sg_list[i].len = sg_regions[i-1].size;
    }
    
    *out_sg_count = MIN(num_regions, sg_list_max);
    
    if (num_regions > sg_list_max) {
        LOG_WARNING("VDS: Returned %u regions but only %u fit in buffer", num_regions, sg_list_max);
    }
    
    return VDS_SUCCESS;
}

/**
 * @brief Fallback for systems without scatter lock support
 */
static int vds_lock_region_fallback(void __far *addr, uint32_t len, uint16_t flags,
                                    vds_sg_entry_t __far *sg_list, uint16_t sg_list_max,
                                    uint16_t *out_sg_count, uint16_t *out_lock_handle)
{
    union REGS regs;
    struct SREGS sregs;
    vds_edds_t edds;
    
    /* Use regular lock and return single region */
    memset(&edds, 0, sizeof(edds));
    edds.region_size = len;
    edds.offset = FP_OFF(addr);
    edds.segment = FP_SEG(addr);
    
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = VDS_LOCK_REGION;  /* 0x8103 - Regular Lock */
    regs.x.dx = flags;
    
    segread(&sregs);
    sregs.es = FP_SEG(&edds);
    regs.x.di = FP_OFF(&edds);
    
    int86x(0x4B, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        /* GPT-5 Fix: Error code is in AH, not AL */
        LOG_ERROR("VDS: Regular lock failed with error 0x%02X", regs.h.ah);
        return regs.h.ah;
    }
    
    /* Return single contiguous region */
    *out_lock_handle = edds.buffer_id;
    sg_list[0].phys = edds.physical_address;
    sg_list[0].len = (uint16_t)MIN(edds.region_avail, len);
    *out_sg_count = 1;
    
    /* Warn if not fully mapped */
    if (edds.region_avail < len) {
        LOG_WARNING("VDS: Only mapped %lu of %lu bytes", edds.region_avail, len);
    }
    
    return VDS_SUCCESS;
}

/**
 * @brief Unlock region previously locked with vds_lock_region_sg
 * 
 * GPT-5 Fix: Use VDS Scatter Unlock (0x8106) for scatter-locked regions
 * 
 * @param lock_handle Lock handle from vds_lock_region_sg
 * @return 0 on success, VDS error code on failure
 */
int vds_unlock_region_sg(uint16_t lock_handle)
{
    union REGS regs;
    struct SREGS sregs;
    
    if (!g_vds_available) {
        return VDS_NOT_SUPPORTED;
    }
    
    /* Try scatter unlock first (0x8106) */
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = VDS_SCATTER_UNLOCK;  /* 0x8106 - Scatter/Gather Unlock */
    /* GPT-5 Fix: Most VDS implementations expect handle in DX for unlock */
    regs.x.dx = lock_handle;  /* Lock handle in DX */
    regs.x.bx = 0;  /* No flags in BX */
    
    segread(&sregs);
    
    int86x(0x4B, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        /* GPT-5 Critical Fix: Try alternate register BX if not function unsupported */
        if (regs.h.ah != 0x01) {  /* Not "function not supported" - try BX */
            LOG_DEBUG("VDS: Scatter unlock failed with DX, trying BX");
            
            memset(&regs, 0, sizeof(regs));
            regs.x.ax = VDS_SCATTER_UNLOCK;  /* 0x8106 */
            regs.x.bx = lock_handle;  /* Try handle in BX instead */
            regs.x.dx = 0;
            
            segread(&sregs);
            int86x(0x4B, &regs, &regs, &sregs);
            
            if (!regs.x.cflag) {
                return VDS_SUCCESS;  /* Worked with BX */
            }
        }
        
        /* If scatter unlock not supported or both attempts failed, try regular unlock */
        if (regs.h.ah == 0x01 || regs.x.cflag) {  /* Function not supported or still failing */
            vds_edds_t edds;
            
            LOG_DEBUG("VDS: Scatter unlock not supported, using regular unlock");
            
            memset(&edds, 0, sizeof(edds));
            edds.buffer_id = lock_handle;
            
            memset(&regs, 0, sizeof(regs));
            regs.x.ax = VDS_UNLOCK_REGION;  /* 0x8104 - Regular Unlock */
            regs.x.dx = 0;
            
            sregs.es = FP_SEG(&edds);
            regs.x.di = FP_OFF(&edds);
            
            int86x(0x4B, &regs, &regs, &sregs);
            
            if (regs.x.cflag) {
                /* GPT-5 Fix: Error code is in AH, not AL */
                LOG_ERROR("VDS: Regular unlock also failed with error 0x%02X", regs.h.ah);
                return regs.h.ah;
            }
        } else {
            LOG_ERROR("VDS: Scatter unlock failed with error 0x%02X", regs.h.ah);
            return regs.h.ah;
        }
    }
    
    return VDS_SUCCESS;
}

/**
 * @brief Check if VDS is available (wrapper for compatibility)
 */
bool vds_available(void)
{
    return g_vds_available;
}