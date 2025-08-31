/**
 * @file vds_support.c
 * @brief Virtual DMA Services implementation for V86 mode support
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * Implements VDS support for DMA operations under V86 mode (EMM386, Windows).
 * GPT-5: Critical for safe DMA operations when memory managers are active.
 */

#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "../../include/vds.h"
#include "../../include/logging.h"
#include "../../include/cpu_detect.h"

/* VDS interrupt vector */
#define VDS_INT_VECTOR  0x4B

/* Global VDS state */
static vds_state_t g_vds_state = {0};

/* Maximum locked regions to track */
#define MAX_LOCKED_REGIONS  16

/* Track locked regions for cleanup */
static struct {
    vds_dma_descriptor_t descriptor;
    bool in_use;
} g_locked_regions[MAX_LOCKED_REGIONS];

/**
 * @brief Check if running in V86 mode
 * 
 * @return true if in V86 mode
 */
bool vds_in_v86_mode(void) {
    uint32_t eflags;
    
    /* Only 386+ can be in V86 mode */
    if (!cpu_supports_32bit()) {
        return false;
    }
    
    /* Read EFLAGS and check VM bit (bit 17) */
    _asm {
        pushfd
        pop eax
        mov eflags, eax
    }
    
    return (eflags & 0x20000) != 0;  /* VM flag is bit 17 */
}

/**
 * @brief Initialize VDS support
 * 
 * @return 0 on success, negative on error
 */
int vds_init(void) {
    union REGS regs;
    struct SREGS sregs;
    
    if (g_vds_state.initialized) {
        return 0;
    }
    
    log_info("Initializing VDS support");
    
    /* Clear state */
    memset(&g_vds_state, 0, sizeof(g_vds_state));
    memset(g_locked_regions, 0, sizeof(g_locked_regions));
    
    /* Check if in V86 mode */
    g_vds_state.v86_mode = vds_in_v86_mode();
    
    if (!g_vds_state.v86_mode) {
        log_info("  Not in V86 mode - VDS not needed");
        g_vds_state.initialized = true;
        return 0;
    }
    
    log_info("  V86 mode detected - checking for VDS");
    
    /* Check for VDS presence using INT 4Bh, AX=8102h */
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.x.ax = VDS_GET_VERSION;
    regs.x.dx = 0;  /* Must be 0 for get version */
    
    int86x(VDS_INT_VECTOR, &regs, &regs, &sregs);
    
    /* Check carry flag - if set, VDS not available */
    if (regs.x.cflag) {
        log_warning("  VDS not available (INT 4Bh failed)");
        g_vds_state.available = false;
        g_vds_state.initialized = true;
        return -1;
    }
    
    /* Parse version information */
    g_vds_state.version.major = (regs.h.ah >> 4) & 0x0F;
    g_vds_state.version.minor = regs.h.ah & 0x0F;
    g_vds_state.version.flags = regs.x.bx;
    g_vds_state.version.max_dma_size = regs.x.cx;
    g_vds_state.version.dma_id = regs.h.dl;
    
    log_info("  VDS version %d.%d detected", 
             g_vds_state.version.major, 
             g_vds_state.version.minor);
    log_info("  Max DMA size: %u KB", g_vds_state.version.max_dma_size);
    
    g_vds_state.available = true;
    g_vds_state.translation_enabled = true;
    g_vds_state.initialized = true;
    
    return 0;
}

/**
 * @brief Check if VDS is available
 * 
 * @return true if VDS services are available
 */
bool vds_available(void) {
    return g_vds_state.available;
}

/**
 * @brief Get VDS version information
 * 
 * @param version Output version structure
 * @return 0 on success, negative on error
 */
int vds_get_version(vds_version_t *version) {
    if (!version) {
        return -1;
    }
    
    if (!g_vds_state.initialized) {
        return -1;
    }
    
    *version = g_vds_state.version;
    return 0;
}

/**
 * @brief Lock a DMA region for bus master access
 * 
 * @param linear_addr Linear address of region
 * @param size Size of region in bytes
 * @param descriptor Output DMA descriptor
 * @return 0 on success, VDS error code on failure
 */
int vds_lock_region(void *linear_addr, uint32_t size, 
                    vds_dma_descriptor_t *descriptor) {
    union REGS regs;
    struct SREGS sregs;
    vds_edds_t edds;
    int i;
    
    if (!descriptor || !linear_addr || size == 0) {
        return VDS_INVALID_PARAMS;
    }
    
    /* If not in V86 mode or VDS not available, use direct mapping */
    if (!g_vds_state.v86_mode || !g_vds_state.available) {
        uint16_t seg = FP_SEG(linear_addr);
        uint16_t off = FP_OFF(linear_addr);
        
        descriptor->size = size;
        descriptor->segment = seg;
        descriptor->offset = off;
        descriptor->buffer_id = 0;
        descriptor->physical_addr = ((uint32_t)seg << 4) + off;
        
        return VDS_SUCCESS;
    }
    
    /* Prepare Extended DDS */
    memset(&edds, 0, sizeof(edds));
    edds.region_size = size;
    edds.offset = FP_OFF(linear_addr);
    edds.segment = FP_SEG(linear_addr);
    
    /* Call VDS lock region (INT 4Bh, AX=8103h) */
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.x.ax = VDS_LOCK_REGION;
    regs.x.dx = VDS_NO_CROSS_64K;  /* Don't cross 64K boundary */
    sregs.es = FP_SEG(&edds);
    regs.x.di = FP_OFF(&edds);
    
    int86x(VDS_INT_VECTOR, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        log_error("VDS lock region failed: error 0x%02X", regs.h.al);
        return regs.h.al;  /* Return VDS error code */
    }
    
    /* Fill output descriptor */
    descriptor->size = edds.region_size;
    descriptor->offset = edds.offset;
    descriptor->segment = edds.segment;
    descriptor->buffer_id = edds.buffer_id;
    descriptor->physical_addr = edds.physical_address;
    
    /* Track locked region for cleanup */
    for (i = 0; i < MAX_LOCKED_REGIONS; i++) {
        if (!g_locked_regions[i].in_use) {
            g_locked_regions[i].descriptor = *descriptor;
            g_locked_regions[i].in_use = true;
            g_vds_state.locked_regions++;
            break;
        }
    }
    
    log_info("VDS locked region: virt=%04X:%04X phys=%08lX size=%lu",
             descriptor->segment, descriptor->offset,
             descriptor->physical_addr, descriptor->size);
    
    return VDS_SUCCESS;
}

/**
 * @brief Unlock a previously locked DMA region
 * 
 * @param descriptor DMA descriptor from lock operation
 * @return 0 on success, VDS error code on failure
 */
int vds_unlock_region(vds_dma_descriptor_t *descriptor) {
    union REGS regs;
    struct SREGS sregs;
    vds_edds_t edds;
    int i;
    
    if (!descriptor) {
        return VDS_INVALID_PARAMS;
    }
    
    /* If not in V86 mode or VDS not available, nothing to unlock */
    if (!g_vds_state.v86_mode || !g_vds_state.available) {
        return VDS_SUCCESS;
    }
    
    /* Prepare Extended DDS */
    memset(&edds, 0, sizeof(edds));
    edds.region_size = descriptor->size;
    edds.offset = descriptor->offset;
    edds.segment = descriptor->segment;
    edds.buffer_id = descriptor->buffer_id;
    edds.physical_address = descriptor->physical_addr;
    
    /* Call VDS unlock region (INT 4Bh, AX=8104h) */
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.x.ax = VDS_UNLOCK_REGION;
    regs.x.dx = 0;
    sregs.es = FP_SEG(&edds);
    regs.x.di = FP_OFF(&edds);
    
    int86x(VDS_INT_VECTOR, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        log_error("VDS unlock region failed: error 0x%02X", regs.h.al);
        return regs.h.al;
    }
    
    /* Remove from tracked regions */
    for (i = 0; i < MAX_LOCKED_REGIONS; i++) {
        if (g_locked_regions[i].in_use &&
            g_locked_regions[i].descriptor.buffer_id == descriptor->buffer_id) {
            g_locked_regions[i].in_use = false;
            g_vds_state.locked_regions--;
            break;
        }
    }
    
    return VDS_SUCCESS;
}

/**
 * @brief Request a DMA buffer from VDS
 * 
 * @param size Required buffer size
 * @param flags VDS flags (alignment, boundary constraints)
 * @param descriptor Output DMA descriptor
 * @return 0 on success, VDS error code on failure
 */
int vds_request_buffer(uint32_t size, uint16_t flags,
                      vds_dma_descriptor_t *descriptor) {
    union REGS regs;
    struct SREGS sregs;
    vds_edds_t edds;
    
    if (!descriptor || size == 0) {
        return VDS_INVALID_PARAMS;
    }
    
    if (!g_vds_state.available) {
        return VDS_NOT_SUPPORTED;
    }
    
    /* Prepare Extended DDS */
    memset(&edds, 0, sizeof(edds));
    edds.region_size = size;
    
    /* Call VDS request buffer (INT 4Bh, AX=8107h) */
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.x.ax = VDS_REQUEST_BUFFER;
    regs.x.dx = flags | VDS_NO_CROSS_64K;  /* Add 64K boundary constraint */
    sregs.es = FP_SEG(&edds);
    regs.x.di = FP_OFF(&edds);
    
    int86x(VDS_INT_VECTOR, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        log_error("VDS request buffer failed: error 0x%02X", regs.h.al);
        return regs.h.al;
    }
    
    /* Fill output descriptor */
    descriptor->size = edds.region_size;
    descriptor->offset = edds.offset;
    descriptor->segment = edds.segment;
    descriptor->buffer_id = edds.buffer_id;
    descriptor->physical_addr = edds.physical_address;
    
    log_info("VDS allocated buffer: phys=%08lX size=%lu id=%u",
             descriptor->physical_addr, descriptor->size, 
             descriptor->buffer_id);
    
    return VDS_SUCCESS;
}

/**
 * @brief Get physical address for DMA
 * 
 * @param linear_addr Linear/virtual address
 * @param size Size of region
 * @param phys_addr Output physical address
 * @return 0 on success, negative on error
 */
int vds_get_physical_address(void *linear_addr, uint32_t size,
                            uint32_t *phys_addr) {
    vds_dma_descriptor_t desc;
    int result;
    
    if (!linear_addr || !phys_addr || size == 0) {
        return -1;
    }
    
    /* Lock region to get physical address */
    result = vds_lock_region(linear_addr, size, &desc);
    if (result != VDS_SUCCESS) {
        return -1;
    }
    
    *phys_addr = desc.physical_addr;
    
    /* If VDS was used, unlock immediately (caller will re-lock if needed) */
    if (g_vds_state.available && g_vds_state.v86_mode) {
        vds_unlock_region(&desc);
    }
    
    return 0;
}

/**
 * @brief Cleanup VDS resources
 */
void vds_cleanup(void) {
    int i;
    
    if (!g_vds_state.initialized) {
        return;
    }
    
    log_info("Cleaning up VDS resources");
    
    /* Unlock all tracked regions */
    for (i = 0; i < MAX_LOCKED_REGIONS; i++) {
        if (g_locked_regions[i].in_use) {
            log_warning("  Unlocking orphaned region id=%u",
                       g_locked_regions[i].descriptor.buffer_id);
            vds_unlock_region(&g_locked_regions[i].descriptor);
        }
    }
    
    g_vds_state.initialized = false;
    g_vds_state.available = false;
    g_vds_state.locked_regions = 0;
}

/**
 * @brief Get VDS error string
 * 
 * @param error VDS error code
 * @return Error description string
 */
const char* vds_error_string(int error) {
    switch (error) {
        case VDS_SUCCESS:
            return "Success";
        case VDS_REGION_NOT_LOCKED:
            return "Region not locked";
        case VDS_LOCK_FAILED:
            return "Lock failed";
        case VDS_INVALID_PARAMS:
            return "Invalid parameters";
        case VDS_BOUNDARY_CROSSED:
            return "64K boundary crossed";
        case VDS_BUFFER_IN_USE:
            return "Buffer in use";
        case VDS_REGION_TOO_LARGE:
            return "Region too large";
        case VDS_BUFFER_BOUNDARY:
            return "Buffer boundary violation";
        case VDS_INVALID_ID:
            return "Invalid buffer ID";
        case VDS_BUFFER_NOT_LOCKED:
            return "Buffer not locked";
        case VDS_INVALID_SIZE:
            return "Invalid size";
        case VDS_BOUNDARY_VIOLATION:
            return "Boundary violation";
        case VDS_INVALID_ALIGNMENT:
            return "Invalid alignment";
        case VDS_NOT_SUPPORTED:
            return "VDS not supported";
        case VDS_FLAGS_NOT_SUPPORTED:
            return "Flags not supported";
        default:
            return "Unknown VDS error";
    }
}