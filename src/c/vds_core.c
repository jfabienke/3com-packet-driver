/**
 * @file vds_core.c
 * @brief Unified VDS Core Layer Implementation
 * 
 * Provides raw VDS operations via INT 4Bh interface.
 * Leverages existing cpu_detect.h for V86 mode detection.
 * Consolidates best features from vds.c, vds_support.c, and vds_mapping.c.
 */

#include <dos.h>
#include <string.h>
#include <stdlib.h>
#include "../../include/vds_core.h"
#include "../../include/cpudet.h"
#include "../../include/logging.h"

/* VDS interrupt vector */
#define VDS_INT_VECTOR  0x4B

/* Timeout protection (from vds_mapping.c) */
#define VDS_RETRY_COUNT 3
#define VDS_RETRY_DELAY 10  /* milliseconds */

/* Global state */
static bool vds_core_initialized = false;
static vds_caps_t vds_capabilities = {0};
static vds_core_stats_t vds_stats = {0};

/* Forward declarations */
static bool detect_vds_presence(void);
static void delay_ms(uint16_t ms);
static int vds_populate_sg_list(vds_raw_lock_result_t* result);

/* 64K boundary and chunking support */
#define MAX_COPY_CHUNK  0xF000   /* 61440 bytes - safer than full 64K */
#define ISA_24BIT_LIMIT 0x1000000  /* 16MB ISA DMA limit */

/**
 * Check if address range crosses 64K boundary
 */
static bool crosses_64k_boundary(uint32_t addr, uint32_t size)
{
    uint32_t start_64k = addr & ~0xFFFF;
    uint32_t end_64k = (addr + size - 1) & ~0xFFFF;
    return (start_64k != end_64k);
}

/**
 * Validate ISA DMA constraints
 */
static bool validate_isa_constraints(uint32_t addr, uint32_t size)
{
    /* Check 24-bit address limit */
    if (addr >= ISA_24BIT_LIMIT || (addr + size) > ISA_24BIT_LIMIT) {
        LOG_WARNING("VDS: Address exceeds 24-bit ISA limit (0x%08lX)", addr);
        return false;
    }
    
    /* Check 64K boundary crossing */
    if (crosses_64k_boundary(addr, size)) {
        LOG_WARNING("VDS: Buffer crosses 64K boundary (0x%08lX, size %lu)", addr, size);
        return false;
    }
    
    return true;
}

/**
 * Initialize VDS core services
 */
int vds_core_init(void)
{
    if (vds_core_initialized) {
        return 0;
    }
    
    /* Clear capabilities */
    memset(&vds_capabilities, 0, sizeof(vds_capabilities));
    memset(&vds_stats, 0, sizeof(vds_stats));
    
    /* Use existing CPU detection to check V86 mode */
    const cpu_info_t* cpu = cpu_get_info();
    
    if (!cpu->in_v86_mode) {
        LOG_INFO("VDS: Not in V86 mode - VDS not needed (CPU: %s)", cpu->cpu_name);
        vds_capabilities.present = false;
        vds_core_initialized = true;
        return 0;
    }
    
    LOG_INFO("VDS: V86 mode detected - checking for VDS services");
    
    /* Check for VDS presence */
    if (!detect_vds_presence()) {
        LOG_WARNING("VDS: V86 mode active but VDS not available");
        vds_capabilities.present = false;
        vds_core_initialized = true;
        return -1;
    }
    
    /* Get VDS version and capabilities */
    union REGS regs;
    struct SREGS sregs;
    
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.x.ax = VDS_FUNC_GET_VERSION;
    regs.x.dx = 0;  /* Must be 0 for get version */
    
    int86x(VDS_INT_VECTOR, &regs, &regs, &sregs);
    
    if (!regs.x.cflag) {
        vds_capabilities.present = true;
        vds_capabilities.major_version = (regs.x.ax >> 8) & 0xFF;
        vds_capabilities.minor_version = regs.x.ax & 0xFF;
        vds_capabilities.oem_number = regs.x.bx;
        vds_capabilities.revision = regs.x.cx;
        vds_capabilities.max_dma_size = ((uint32_t)regs.x.si << 16) | regs.x.di;
        vds_capabilities.flags = regs.x.dx;
        
        /* Determine capabilities from version */
        if (vds_capabilities.major_version >= 2) {
            vds_capabilities.supports_scatter = true;
            vds_capabilities.max_sg_entries = 17;  /* VDS 2.0 standard */
        } else {
            vds_capabilities.supports_scatter = false;
            vds_capabilities.max_sg_entries = 1;
        }
        
        LOG_INFO("VDS: Version %d.%d detected (OEM: 0x%04X, Max DMA: %lu bytes)",
                vds_capabilities.major_version, vds_capabilities.minor_version,
                vds_capabilities.oem_number, vds_capabilities.max_dma_size);
    } else {
        LOG_ERROR("VDS: Version query failed (AX=0x%04X)", regs.x.ax);
        vds_capabilities.present = false;
        vds_core_initialized = true;
        return -1;
    }
    
    vds_core_initialized = true;
    LOG_INFO("VDS: Core services initialized successfully");
    
    return 0;
}

/**
 * Check if VDS is present
 */
bool vds_is_present(void)
{
    if (!vds_core_initialized) {
        vds_core_init();
    }
    return vds_capabilities.present;
}

/**
 * Check if running in V86 mode
 */
bool vds_is_v86_mode(void)
{
    /* Use existing CPU detection - single source of truth! */
    return asm_is_v86_mode();
}

/**
 * Get VDS capabilities
 */
const vds_caps_t* vds_get_capabilities(void)
{
    if (!vds_core_initialized) {
        vds_core_init();
    }
    return &vds_capabilities;
}

/**
 * Detect VDS presence by checking INT 4Bh vector
 */
static bool detect_vds_presence(void)
{
    void far* vector;
    uint32_t vector_addr;
    
    /* Get INT 4Bh vector */
    _disable();
    vector = _dos_getvect(VDS_INT_VECTOR);
    _enable();
    
    /* NULL vector means VDS not installed */
    if (!vector) {
        LOG_DEBUG("VDS: INT 4Bh vector is NULL");
        return false;
    }
    
    /* Convert to linear address */
    vector_addr = ((uint32_t)FP_SEG(vector) << 4) + FP_OFF(vector);
    
    /* Check if vector points to valid code */
    if (vector_addr == 0x00000000UL || vector_addr == 0xFFFFFFFFUL) {
        LOG_DEBUG("VDS: INT 4Bh vector invalid (0x%08lX)", vector_addr);
        return false;
    }
    
    /* Try a simple VDS call to verify it's really there */
    union REGS regs;
    struct SREGS sregs;
    
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.x.ax = VDS_FUNC_GET_VERSION;
    regs.x.dx = 0;
    
    int86x(VDS_INT_VECTOR, &regs, &regs, &sregs);
    
    return !regs.x.cflag;  /* Success if carry flag not set */
}

/**
 * Raw VDS lock region with timeout protection
 */
uint8_t vds_core_lock_region(void far* linear_addr, uint32_t size, 
                             uint16_t flags, vds_transfer_direction_t direction,
                             vds_raw_lock_result_t* result)
{
    union REGS regs;
    struct SREGS sregs;
    vds_raw_descriptor_t desc;
    uint8_t retry_count = 0;
    uint16_t error_code = VDS_RAW_SUCCESS;  /* 16-bit error code */
    
    if (!result) {
        return VDS_RAW_INVALID_PARAMS;
    }
    
    /* Validate size to prevent overflow issues */
    if (size == 0 || size > 0x00FFFFFFUL) {  /* Max 16MB for practical reasons */
        LOG_ERROR("VDS: Invalid size for lock (size: 0x%08lX)", size);
        return VDS_RAW_INVALID_SIZE;
    }
    
    memset(result, 0, sizeof(*result));
    result->lock_handle = 0xFFFF;
    result->sg_list = NULL;
    vds_stats.lock_attempts++;
    
    /* Check if VDS is available */
    if (!vds_is_present()) {
        /* Not in V86 or VDS not available - use direct mapping */
        result->success = true;
        result->physical_addr = vds_linear_to_physical(linear_addr);
        result->actual_length = size;  /* Full size in real mode */
        result->translation_type = VDS_TRANS_DIRECT;  /* Direct mapping in real mode */
        result->is_scattered = false;
        result->needs_pre_copy = false;  /* No copy needed in real mode */
        result->needs_post_copy = false;
        vds_stats.lock_successes++;
        return VDS_RAW_SUCCESS;
    }
    
    /* Prepare descriptor */
    memset(&desc, 0, sizeof(desc));
    desc.region_size = size;
    desc.segment = FP_SEG(linear_addr);
    desc.linear_offset = FP_OFF(linear_addr);
    
    /* Build device capability flags if not provided */
    if (flags == 0) {
        /* Auto-detect based on common requirements */
        flags = VDS_FLAG_NO_64K_CROSS;  /* Most devices need this */
    }
    
    /* Log AX flags for diagnostics */
    LOG_DEBUG("VDS: Locking with flags 0x%04X, direction %d", flags, direction);
    
    /* Retry loop with timeout protection (from vds_mapping.c) */
    for (retry_count = 0; retry_count < VDS_RETRY_COUNT; retry_count++) {
        memset(&regs, 0, sizeof(regs));
        memset(&sregs, 0, sizeof(sregs));
        
        regs.x.ax = VDS_FUNC_LOCK_REGION;
        regs.x.dx = flags;
        sregs.es = FP_SEG(&desc);
        regs.x.di = FP_OFF(&desc);
        
        int86x(VDS_INT_VECTOR, &regs, &regs, &sregs);
        
        /* CRITICAL: Check carry flag FIRST before parsing AX */
        if (!regs.x.cflag) {
            /* Success */
            result->success = true;
            result->error_code = VDS_RAW_SUCCESS;
            result->lock_handle = desc.buffer_id;
            result->physical_addr = desc.physical_address;
            result->actual_length = desc.region_size;  /* VDS returns actual locked size */
            
            /* Parse AX register flags per VDS specification */
            uint16_t ax_flags = regs.x.ax;
            LOG_DEBUG("VDS: Lock succeeded with AX flags: 0x%04X", ax_flags);
            result->is_scattered = (ax_flags & 0x02) != 0;  /* Bit 1: scatter/gather */
            
            /* If scattered, retrieve the S/G list */
            if (result->is_scattered) {
                /* Populate S/G list */
                if (vds_populate_sg_list(result) != 0) {
                    LOG_WARNING("VDS: Failed to retrieve S/G list for scattered lock");
                    /* Continue anyway - we have the lock handle */
                }
            }
            
            /* Bits 2-3: Translation type (00=DIRECT, 01=REMAPPED, 10=ALTERNATE, 11=UNKNOWN) */
            result->translation_type = (vds_translation_type_t)((ax_flags >> 2) & 0x03);
            
            /* Determine copy requirements based on translation type and direction */
            switch (result->translation_type) {
                case VDS_TRANS_DIRECT:
                    /* Direct mapping - no copies needed */
                    result->needs_pre_copy = false;
                    result->needs_post_copy = false;
                    vds_stats.vds_direct_locks++;
                    LOG_DEBUG("VDS: DIRECT mapping (phys: 0x%08lX)", result->physical_addr);
                    break;
                    
                case VDS_TRANS_REMAPPED:
                    /* Remapped but no copy needed */
                    result->needs_pre_copy = false;
                    result->needs_post_copy = false;
                    vds_stats.vds_direct_locks++;  /* Count remapped as direct for stats */
                    LOG_DEBUG("VDS: REMAPPED translation (phys: 0x%08lX)", result->physical_addr);
                    break;
                    
                case VDS_TRANS_ALTERNATE:
                    /* ALTERNATE buffer requires copies */
                    result->needs_pre_copy = (direction == VDS_DIR_HOST_TO_DEVICE || 
                                              direction == VDS_DIR_BIDIRECTIONAL);
                    result->needs_post_copy = (direction == VDS_DIR_DEVICE_TO_HOST || 
                                               direction == VDS_DIR_BIDIRECTIONAL);
                    vds_stats.vds_bounce_detections++;
                    LOG_INFO("VDS: ALTERNATE buffer mode - copy required (phys: 0x%08lX)",
                            result->physical_addr);
                    break;
                    
                case VDS_TRANS_UNKNOWN:
                default:
                    /* Unknown/reserved - be conservative and require copies */
                    LOG_WARNING("VDS: Unknown translation type %d - using conservative copy",
                               result->translation_type);
                    result->needs_pre_copy = (direction != VDS_DIR_DEVICE_TO_HOST);
                    result->needs_post_copy = (direction != VDS_DIR_HOST_TO_DEVICE);
                    vds_stats.vds_bounce_detections++;
                    break;
            }
            
            vds_stats.lock_successes++;
            if (result->is_scattered) {
                vds_stats.scatter_gather_locks++;
            }
            
            LOG_DEBUG("VDS: Locked -> 0x%08lX (handle: 0x%04X, trans: %d, pre: %d, post: %d)",
                     result->physical_addr, result->lock_handle, 
                     result->translation_type, result->needs_pre_copy, result->needs_post_copy);
            
            return VDS_RAW_SUCCESS;
        }
        
        /* Error - get 16-bit error code from AX (CF is set) */
        error_code = regs.x.ax;  /* Full 16-bit error code */
        
        if (error_code == VDS_RAW_BOUNDARY_CROSSED || 
            error_code == VDS_RAW_BOUNDARY_VIOLATION) {
            vds_stats.boundary_violations++;
        }
        
        /* Delay before retry */
        if (retry_count < VDS_RETRY_COUNT - 1) {
            delay_ms(VDS_RETRY_DELAY);
            LOG_DEBUG("VDS: Lock retry %d (error: 0x%02X)", retry_count + 1, error_code);
        }
    }
    
    /* All retries failed */
    result->success = false;
    result->error_code = error_code;
    result->success = false;
    vds_stats.lock_failures++;
    
    LOG_ERROR("VDS: Lock failed after %d retries (error: 0x%02X - %s)",
             VDS_RETRY_COUNT, error_code, vds_core_error_string(error_code));
    
    return error_code;
}

/**
 * Raw VDS unlock region
 */
uint8_t vds_core_unlock_region(uint16_t lock_handle)
{
    union REGS regs;
    struct SREGS sregs;
    vds_raw_descriptor_t desc;
    
    vds_stats.unlock_attempts++;
    
    /* Check if VDS is available */
    if (!vds_is_present()) {
        /* Not in V86 - nothing to unlock */
        vds_stats.unlock_successes++;
        return VDS_RAW_SUCCESS;
    }
    
    /* Prepare descriptor with handle */
    memset(&desc, 0, sizeof(desc));
    desc.buffer_id = lock_handle;
    
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.x.ax = VDS_FUNC_UNLOCK_REGION;
    regs.x.dx = 0;  /* Flags must be 0 for unlock */
    sregs.es = FP_SEG(&desc);
    regs.x.di = FP_OFF(&desc);
    
    int86x(VDS_INT_VECTOR, &regs, &regs, &sregs);
    
    if (!regs.x.cflag) {
        vds_stats.unlock_successes++;
        LOG_DEBUG("VDS: Unlocked handle 0x%04X", lock_handle);
        return VDS_RAW_SUCCESS;
    }
    
    vds_stats.unlock_failures++;
    LOG_ERROR("VDS: Unlock failed for handle 0x%04X (error: 0x%02X)",
             lock_handle, regs.h.al);
    
    return regs.h.al;
}

/**
 * Copy data to VDS ALTERNATE buffer before DMA write
 */
uint16_t vds_core_copy_to_alternate(uint16_t lock_handle, void far* source, 
                                    uint32_t size, uint32_t offset)
{
    union REGS regs;
    struct SREGS sregs;
    vds_copy_descriptor_t desc;
    uint32_t remaining = size;
    uint32_t current_offset = offset;
    
    /* Defensive check - avoid no-op INT call */
    if (size == 0) {
        LOG_DEBUG("VDS: Zero-size copy requested, returning success");
        return VDS_RAW_SUCCESS;
    }
    
    /* Overflow validation - ensure offset + size doesn't overflow */
    if (offset > 0xFFFFFFFFUL - size) {
        LOG_ERROR("VDS: Offset + size would overflow (offset: 0x%08lX, size: 0x%08lX)",
                 offset, size);
        return VDS_RAW_INVALID_SIZE;
    }
    
    /* Calculate base linear address once - avoid far pointer arithmetic! */
    uint32_t base_linear = ((uint32_t)FP_SEG(source) << 4) + FP_OFF(source);
    uint32_t processed = 0;  /* Track bytes processed in linear space */
    
    if (!vds_capabilities.present) {
        /* No VDS - no copy needed */
        return VDS_RAW_SUCCESS;
    }
    
    /* Implement chunking for large copies */
    while (remaining > 0) {
        uint32_t chunk_size = (remaining > MAX_COPY_CHUNK) ? MAX_COPY_CHUNK : remaining;
        uint32_t chunk_linear = base_linear + processed;
        
        /* Check for 20-bit wrap (real mode 1MB boundary) */
        if ((chunk_linear & 0xFFFFF) + chunk_size > 0x100000) {
            /* Would wrap past 1MB - limit chunk to avoid wrap */
            chunk_size = 0x100000 - (chunk_linear & 0xFFFFF);
            if (chunk_size == 0) {
                LOG_ERROR("VDS: Copy would wrap at 1MB boundary");
                return VDS_RAW_BOUNDARY_VIOLATION;
            }
        }
    
        /* Setup proper copy descriptor per VDS specification */
        memset(&desc, 0, sizeof(desc));
        desc.region_size = chunk_size;          /* Size for this chunk */
        desc.offset = current_offset;            /* Offset within locked region */
        desc.buffer_id = lock_handle;            /* Lock handle */
        
        /* Calculate linear address from base plus processed bytes */
        desc.client_linear = base_linear + processed;  /* Correct linear calculation */
        desc.reserved = 0;                /* Must be zero */
        
        /* Call VDS function 0x8109 - Copy to DMA buffer */
        regs.x.ax = VDS_FUNC_COPY_TO_BUFFER;
        regs.x.dx = lock_handle;  /* Some VDS implementations also check DX */
        segread(&sregs);
        sregs.es = FP_SEG(&desc);
        regs.x.di = FP_OFF(&desc);  /* ES:DI points to descriptor */
        
        int86x(VDS_INT_VECTOR, &regs, &regs, &sregs);
        
        /* Check carry flag first */
        if (regs.x.cflag) {
            uint16_t error = regs.x.ax;  /* 16-bit error code */
            LOG_ERROR("VDS: Copy to ALTERNATE buffer failed (error: 0x%04X)", error);
            return error;  /* Return full 16-bit error code */
        }
        
        /* Update for next chunk */
        remaining -= chunk_size;
        current_offset += chunk_size;
        processed += chunk_size;  /* Track in linear space, not far pointer */
        
        if (remaining > 0) {
            LOG_DEBUG("VDS: Copied chunk %lu bytes, %lu remaining", chunk_size, remaining);
        }
    }
    
    LOG_DEBUG("VDS: Copied %lu bytes to ALTERNATE buffer (handle: 0x%04X, offset: %lu)", 
             size, lock_handle, offset);
    return VDS_RAW_SUCCESS;
}

/**
 * Copy data from VDS ALTERNATE buffer after DMA read
 */
uint16_t vds_core_copy_from_alternate(uint16_t lock_handle, void far* dest, 
                                      uint32_t size, uint32_t offset)
{
    union REGS regs;
    struct SREGS sregs;
    vds_copy_descriptor_t desc;
    uint32_t remaining = size;
    uint32_t current_offset = offset;
    
    /* Defensive check - avoid no-op INT call */
    if (size == 0) {
        LOG_DEBUG("VDS: Zero-size copy requested, returning success");
        return VDS_RAW_SUCCESS;
    }
    
    /* Overflow validation - ensure offset + size doesn't overflow */
    if (offset > 0xFFFFFFFFUL - size) {
        LOG_ERROR("VDS: Offset + size would overflow (offset: 0x%08lX, size: 0x%08lX)",
                 offset, size);
        return VDS_RAW_INVALID_SIZE;
    }
    
    /* Calculate base linear address once - avoid far pointer arithmetic! */
    uint32_t base_linear = ((uint32_t)FP_SEG(dest) << 4) + FP_OFF(dest);
    uint32_t processed = 0;  /* Track bytes processed in linear space */
    
    if (!vds_capabilities.present) {
        /* No VDS - no copy needed */
        return VDS_RAW_SUCCESS;
    }
    
    /* Implement chunking for large copies */
    while (remaining > 0) {
        uint32_t chunk_size = (remaining > MAX_COPY_CHUNK) ? MAX_COPY_CHUNK : remaining;
        uint32_t chunk_linear = base_linear + processed;
        
        /* Check for 20-bit wrap (real mode 1MB boundary) */
        if ((chunk_linear & 0xFFFFF) + chunk_size > 0x100000) {
            /* Would wrap past 1MB - limit chunk to avoid wrap */
            chunk_size = 0x100000 - (chunk_linear & 0xFFFFF);
            if (chunk_size == 0) {
                LOG_ERROR("VDS: Copy would wrap at 1MB boundary");
                return VDS_RAW_BOUNDARY_VIOLATION;
            }
        }
        
        /* Setup proper copy descriptor per VDS specification */
        memset(&desc, 0, sizeof(desc));
        desc.region_size = chunk_size;          /* Size for this chunk */
        desc.offset = current_offset;            /* Offset within locked region */
        desc.buffer_id = lock_handle;            /* Lock handle */
        
        /* Calculate linear address from base plus processed bytes */
        desc.client_linear = base_linear + processed;  /* Correct linear calculation */
        desc.reserved = 0;                /* Must be zero */
        
        /* Call VDS function 0x810A - Copy from DMA buffer */
        regs.x.ax = VDS_FUNC_COPY_FROM_BUFFER;
        regs.x.dx = lock_handle;  /* Some VDS implementations also check DX */
        segread(&sregs);
        sregs.es = FP_SEG(&desc);
        regs.x.di = FP_OFF(&desc);  /* ES:DI points to descriptor */
        
        int86x(VDS_INT_VECTOR, &regs, &regs, &sregs);
        
        /* Check carry flag first */
        if (regs.x.cflag) {
            uint16_t error = regs.x.ax;  /* 16-bit error code */
            LOG_ERROR("VDS: Copy from ALTERNATE buffer failed (error: 0x%04X)", error);
            return error;  /* Return full 16-bit error code */
        }
        
        /* Update for next chunk */
        remaining -= chunk_size;
        current_offset += chunk_size;
        processed += chunk_size;  /* Track in linear space, not far pointer */
        
        if (remaining > 0) {
            LOG_DEBUG("VDS: Copied chunk %lu bytes, %lu remaining", chunk_size, remaining);
        }
    }
    
    LOG_DEBUG("VDS: Copied %lu bytes from ALTERNATE buffer (handle: 0x%04X, offset: %lu)", 
             size, lock_handle, offset);
    return VDS_RAW_SUCCESS;
}

/**
 * Get scatter/gather list for locked region
 */
uint16_t vds_core_get_sg_list(uint16_t lock_handle, vds_sg_entry_t far* sg_list,
                              uint16_t max_entries, uint16_t* actual_entries)
{
    union REGS regs;
    struct SREGS sregs;
    vds_sg_descriptor_t desc;
    uint16_t entries_returned;
    
    if (!vds_capabilities.supports_scatter) {
        return VDS_RAW_NOT_SUPPORTED;
    }
    
    if (!sg_list || !actual_entries || max_entries == 0) {
        return VDS_RAW_INVALID_PARAMS;
    }
    
    /* Setup S/G descriptor */
    memset(&desc, 0, sizeof(desc));
    desc.num_avail = max_entries;      /* How many entries we can accept */
    /* Calculate proper linear address for far pointer */
    desc.sg_list_addr = ((uint32_t)FP_SEG(sg_list) << 4) + FP_OFF(sg_list);
    
    /* Call VDS function 0x8108 - Get Scatter/Gather List */
    regs.x.ax = VDS_FUNC_GET_SG_LIST;
    regs.x.dx = lock_handle;
    segread(&sregs);
    sregs.es = FP_SEG(&desc);
    regs.x.di = FP_OFF(&desc);
    
    int86x(VDS_INT_VECTOR, &regs, &regs, &sregs);
    
    /* Check carry flag first */
    if (regs.x.cflag) {
        uint16_t error = regs.x.ax;
        LOG_ERROR("VDS: Get S/G list failed (error: 0x%04X)", error);
        *actual_entries = 0;
        return error;  /* Return full 16-bit error code */
    }
    
    /* Get number of entries returned */
    entries_returned = desc.num_used;
    *actual_entries = entries_returned;
    
    /* Check if all entries fit */
    if (entries_returned > max_entries) {
        LOG_WARNING("VDS: S/G list truncated (%u entries, only %u fit)",
                   entries_returned, max_entries);
        /* We still got partial results, which is useful */
    }
    
    LOG_DEBUG("VDS: Retrieved %u S/G entries for handle 0x%04X",
             entries_returned, lock_handle);
    
    return VDS_RAW_SUCCESS;
}

/**
 * Internal function to retrieve and populate S/G list
 */
static int vds_populate_sg_list(vds_raw_lock_result_t* result)
{
    /* Use larger capacity as GPT-5 suggested */
    const uint16_t sg_capacity = 32;  /* Increased from 17 */
    vds_sg_entry_t far* temp_list;
    uint16_t actual_count = 0;
    uint16_t error;  /* 16-bit error code */
    
    /* Allocate far buffer for S/G list - not on stack! */
    temp_list = (vds_sg_entry_t far*)_fmalloc(sg_capacity * sizeof(vds_sg_entry_t));
    if (!temp_list) {
        LOG_ERROR("VDS: Failed to allocate S/G buffer");
        return -1;
    }
    
    /* Query S/G list */
    error = vds_core_get_sg_list(result->lock_handle, temp_list, sg_capacity, &actual_count);
    if (error != VDS_RAW_SUCCESS) {
        LOG_ERROR("VDS: Failed to get S/G list (error: 0x%04X)", error);
        _ffree(temp_list);
        return -1;
    }
    
    if (actual_count > 0) {
        /* Allocate near buffer for result */
        result->sg_list = (vds_sg_entry_t*)malloc(actual_count * sizeof(vds_sg_entry_t));
        if (!result->sg_list) {
            LOG_ERROR("VDS: Failed to allocate S/G list (%u entries)", actual_count);
            _ffree(temp_list);
            return -1;
        }
        
        /* Copy from far to near buffer - portable approach */
        /* Use movedata for maximum compiler compatibility */
        movedata(FP_SEG(temp_list), FP_OFF(temp_list),
                _DS, (unsigned)result->sg_list,
                actual_count * sizeof(vds_sg_entry_t));
        result->sg_count = actual_count;
        
        /* Log S/G details */
        LOG_DEBUG("VDS: S/G list with %u entries:", actual_count);
        for (int i = 0; i < actual_count && i < 3; i++) {
            LOG_DEBUG("  [%d] Phys: 0x%08lX, Size: %lu", 
                     i, temp_list[i].physical_addr, temp_list[i].size);
        }
        
        /* Use first segment as primary physical address */
        if (actual_count > 0) {
            result->physical_addr = temp_list[0].physical_addr;
        }
    }
    
    /* Free far buffer */
    _ffree(temp_list);
    return 0;
}

/**
 * Request DMA buffer from VDS
 */
uint8_t vds_core_request_buffer(uint32_t size, uint16_t flags,
                                vds_raw_lock_result_t* result)
{
    union REGS regs;
    struct SREGS sregs;
    vds_raw_descriptor_t desc;
    
    if (!result) {
        return VDS_RAW_INVALID_PARAMS;
    }
    
    /* Validate size to prevent overflow/unreasonable requests */
    if (size == 0 || size > 0x00100000UL) {  /* Max 1MB for buffer request */
        LOG_ERROR("VDS: Invalid buffer size request (size: 0x%08lX)", size);
        return VDS_RAW_INVALID_SIZE;
    }
    
    memset(result, 0, sizeof(*result));
    
    if (!vds_is_present()) {
        return VDS_RAW_NOT_SUPPORTED;
    }
    
    /* Prepare descriptor */
    memset(&desc, 0, sizeof(desc));
    desc.region_size = size;
    
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.x.ax = VDS_FUNC_REQUEST_BUFFER;
    regs.x.dx = flags;
    sregs.es = FP_SEG(&desc);
    regs.x.di = FP_OFF(&desc);
    
    int86x(VDS_INT_VECTOR, &regs, &regs, &sregs);
    
    if (!regs.x.cflag) {
        result->success = true;
        result->error_code = VDS_RAW_SUCCESS;
        result->lock_handle = desc.buffer_id;
        result->physical_addr = desc.physical_address;
        
        LOG_DEBUG("VDS: Allocated buffer 0x%08lX (size: %lu, id: 0x%04X)",
                 result->physical_addr, size, result->lock_handle);
        
        return VDS_RAW_SUCCESS;
    }
    
    result->error_code = regs.h.al;
    LOG_ERROR("VDS: Buffer allocation failed (size: %lu, error: 0x%02X)",
             size, regs.h.al);
    
    return regs.h.al;
}

/**
 * Release VDS-allocated buffer
 */
uint8_t vds_core_release_buffer(uint16_t buffer_id)
{
    union REGS regs;
    struct SREGS sregs;
    vds_raw_descriptor_t desc;
    
    if (!vds_is_present()) {
        return VDS_RAW_NOT_SUPPORTED;
    }
    
    memset(&desc, 0, sizeof(desc));
    desc.buffer_id = buffer_id;
    
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.x.ax = VDS_FUNC_RELEASE_BUFFER;
    regs.x.dx = 0;
    sregs.es = FP_SEG(&desc);
    regs.x.di = FP_OFF(&desc);
    
    int86x(VDS_INT_VECTOR, &regs, &regs, &sregs);
    
    if (!regs.x.cflag) {
        LOG_DEBUG("VDS: Released buffer id 0x%04X", buffer_id);
        return VDS_RAW_SUCCESS;
    }
    
    LOG_ERROR("VDS: Buffer release failed (id: 0x%04X, error: 0x%02X)",
             buffer_id, regs.h.al);
    
    return regs.h.al;
}

/**
 * Copy data to VDS buffer
 */
uint16_t vds_core_copy_to_buffer(uint16_t buffer_id, void far* src, uint32_t size)
{
    union REGS regs;
    struct SREGS sregs;
    vds_copy_descriptor_t desc;
    
    /* Validate size to prevent overflow */
    if (size == 0 || size > 0x00100000UL) {  /* Max 1MB for copy */
        LOG_ERROR("VDS: Invalid copy size (size: 0x%08lX)", size);
        return VDS_RAW_INVALID_SIZE;
    }
    
    if (!vds_is_present()) {
        return VDS_RAW_NOT_SUPPORTED;
    }
    
    /* Setup copy descriptor */
    memset(&desc, 0, sizeof(desc));
    desc.region_size = size;
    desc.offset = 0;  /* Copy to beginning of buffer */
    desc.buffer_id = buffer_id;
    desc.client_linear = ((uint32_t)FP_SEG(src) << 4) + FP_OFF(src);
    desc.reserved = 0;
    
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.x.ax = VDS_FUNC_COPY_TO_BUFFER;
    regs.x.dx = buffer_id;  /* Some implementations check DX too */
    sregs.es = FP_SEG(&desc);
    regs.x.di = FP_OFF(&desc);
    
    int86x(VDS_INT_VECTOR, &regs, &regs, &sregs);
    
    if (!regs.x.cflag) {
        LOG_DEBUG("VDS: Copied %lu bytes to buffer 0x%04X", size, buffer_id);
        return VDS_RAW_SUCCESS;
    }
    
    uint16_t error = regs.x.ax;  /* 16-bit error code */
    LOG_ERROR("VDS: Copy to buffer failed (id: 0x%04X, error: 0x%04X)",
             buffer_id, error);
    
    return error;
}

/**
 * Copy data from VDS buffer
 */
uint16_t vds_core_copy_from_buffer(uint16_t buffer_id, void far* dst, uint32_t size)
{
    union REGS regs;
    struct SREGS sregs;
    vds_copy_descriptor_t desc;
    
    /* Validate size to prevent overflow */
    if (size == 0 || size > 0x00100000UL) {  /* Max 1MB for copy */
        LOG_ERROR("VDS: Invalid copy size (size: 0x%08lX)", size);
        return VDS_RAW_INVALID_SIZE;
    }
    
    if (!vds_is_present()) {
        return VDS_RAW_NOT_SUPPORTED;
    }
    
    /* Setup copy descriptor */
    memset(&desc, 0, sizeof(desc));
    desc.region_size = size;
    desc.offset = 0;  /* Copy from beginning of buffer */
    desc.buffer_id = buffer_id;
    desc.client_linear = ((uint32_t)FP_SEG(dst) << 4) + FP_OFF(dst);
    desc.reserved = 0;
    
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.x.ax = VDS_FUNC_COPY_FROM_BUFFER;
    regs.x.dx = buffer_id;  /* Some implementations check DX too */
    sregs.es = FP_SEG(&desc);
    regs.x.di = FP_OFF(&desc);
    
    int86x(VDS_INT_VECTOR, &regs, &regs, &sregs);
    
    if (!regs.x.cflag) {
        LOG_DEBUG("VDS: Copied %lu bytes from buffer 0x%04X", size, buffer_id);
        return VDS_RAW_SUCCESS;
    }
    
    uint16_t error = regs.x.ax;  /* 16-bit error code */
    LOG_ERROR("VDS: Copy from buffer failed (id: 0x%04X, error: 0x%04X)",
             buffer_id, error);
    
    return error;
}

/**
 * Convert linear address to physical (real mode only)
 */
uint32_t vds_linear_to_physical(void far* linear_addr)
{
    /* In real mode, physical = (segment << 4) + offset */
    return ((uint32_t)FP_SEG(linear_addr) << 4) + FP_OFF(linear_addr);
}

/**
 * Get VDS error string
 */
const char* vds_core_error_string(uint8_t error_code)
{
    switch (error_code) {
        case VDS_RAW_SUCCESS:             return "Success";
        case VDS_RAW_REGION_NOT_LOCKED:   return "Region not locked";
        case VDS_RAW_LOCK_FAILED:         return "Lock failed";
        case VDS_RAW_INVALID_PARAMS:      return "Invalid parameters";
        case VDS_RAW_BOUNDARY_CROSSED:    return "64K boundary crossed";
        case VDS_RAW_BUFFER_IN_USE:       return "Buffer in use";
        case VDS_RAW_REGION_TOO_LARGE:    return "Region too large";
        case VDS_RAW_BUFFER_BOUNDARY:     return "Buffer boundary error";
        case VDS_RAW_INVALID_ID:          return "Invalid buffer ID";
        case VDS_RAW_BUFFER_NOT_LOCKED:   return "Buffer not locked";
        case VDS_RAW_INVALID_SIZE:        return "Invalid size";
        case VDS_RAW_BOUNDARY_VIOLATION:  return "Boundary violation";
        case VDS_RAW_INVALID_ALIGNMENT:   return "Invalid alignment";
        case VDS_RAW_NOT_SUPPORTED:       return "Function not supported";
        case VDS_RAW_FLAGS_NOT_SUPPORTED: return "Flags not supported";
        default:                          return "Unknown error";
    }
}

/**
 * Get VDS core statistics
 */
void vds_core_get_stats(vds_core_stats_t* stats)
{
    if (stats) {
        *stats = vds_stats;
    }
}

/**
 * Reset VDS core statistics
 */
void vds_core_reset_stats(void)
{
    memset(&vds_stats, 0, sizeof(vds_stats));
}

/**
 * Simple delay in milliseconds
 */
static void delay_ms(uint16_t ms)
{
    /* Use BIOS timer tick count (18.2 Hz) */
    uint16_t ticks = (ms * 182) / 10000;  /* Convert ms to ticks */
    uint32_t start_ticks;
    uint32_t current_ticks;
    
    _disable();
    start_ticks = *(uint32_t far*)MK_FP(0x0040, 0x006C);  /* BIOS timer */
    _enable();
    
    do {
        _disable();
        current_ticks = *(uint32_t far*)MK_FP(0x0040, 0x006C);
        _enable();
    } while ((current_ticks - start_ticks) < ticks);
}