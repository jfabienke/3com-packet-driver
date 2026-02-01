/**
 * @file vds.c
 * @brief Virtual DMA Services implementation
 *
 * Provides VDS interface for physical address mapping under
 * memory managers like EMM386, QEMM, and Windows DOS boxes.
 */

#include <dos.h>
#include "../../include/common.h"
#include "../../include/vds.h"

/* VDS INT 4B multiplex interface */
#define VDS_INT         0x4B
#define VDS_SIGNATURE   0x0102

/* VDS initialization state */
static bool g_vds_initialized = false;

/**
 * Check if VDS is available
 */
bool vds_available(void) {
    union REGS r;

    r.x.ax = 0x8102;  /* VDS Get Version */
    r.h.ah = 0x81;
    r.h.al = 0x02;
    r.x.dx = 0;

    int86(VDS_INT, &r, &r);

    /* Check if VDS responded */
    if (r.x.cflag) {
        return false;
    }

    /* VDS version should be non-zero */
    return (r.h.al > 0 || r.h.ah > 0);
}

/**
 * Initialize VDS subsystem
 * @return 0 on success, non-zero on failure
 */
int vds_init(void) {
    uint8_t major, minor;
    uint16_t flags;

    if (g_vds_initialized) {
        return 0;  /* Already initialized */
    }

    /* Check if VDS is available */
    if (!vds_available()) {
        /* VDS not present - not an error, just not available */
        return 1;  /* Non-zero indicates VDS not available */
    }

    /* Get version info for diagnostics */
    if (vds_get_version(&major, &minor, &flags)) {
        /* VDS is present and responding */
        g_vds_initialized = true;
        return 0;  /* Success */
    }

    return 2;  /* VDS present but version query failed */
}

/**
 * Get VDS version
 */
bool vds_get_version(uint8_t *major, uint8_t *minor, uint16_t *flags) {
    union REGS r;
    
    r.x.ax = VDS_GET_VERSION;
    r.x.dx = 0;
    
    int86(VDS_INT, &r, &r);
    
    if (r.x.cflag) {
        return false;
    }
    
    if (major) *major = r.h.ah;
    if (minor) *minor = r.h.al;
    if (flags) *flags = r.x.bx;
    
    return true;
}

/**
 * Lock a memory region for DMA
 */
uint8_t vds_lock_region(void __far *ptr, uint32_t size, VDS_DDS *dds) {
    union REGS r;
    struct SREGS sr;
    uint8_t result;
    uint32_t linear;  /* C89: declare at block start */

    /* Prepare DDS structure */
    dds->size = size;
    dds->segment = FP_SEG(ptr);
    dds->offset = FP_OFF(ptr);
    dds->buffer_id = 0;
    dds->physical = 0;  /* Clear for safety */

    /* Call VDS Lock Region */
    r.x.ax = VDS_LOCK_REGION;
    r.x.dx = VDS_FLAGS_NO_ALLOC;  /* Don't allocate buffer */
    sr.es = FP_SEG(dds);
    r.x.di = FP_OFF(dds);

    int86x(VDS_INT, &r, &r, &sr);

    result = r.h.al;

    /* Check for partial mapping */
    if (result == VDS_SUCCESS) {
        /* Verify we got a real physical address */
        if (dds->physical == 0) {
            /* VDS returned success but no physical - abort */
            vds_unlock_region(dds);
            return VDS_INVALID_REGION;
        }

        /* Under EMM/QEMM, never trust seg<<4+off calculation */
        linear = ((uint32_t)dds->segment << 4) + dds->offset;
        if (dds->physical == linear && dds->segment >= 0xA000) {
            /* Suspicious - might be EMS/QEMM without proper VDS */
            /* Force bounce buffer usage */
            return VDS_REGION_NOT_CONTIGUOUS;
        }
    }

    return result;
}

/**
 * Unlock a previously locked region
 */
uint8_t vds_unlock_region(VDS_DDS *dds) {
    union REGS r;
    struct SREGS sr;
    
    r.x.ax = VDS_UNLOCK_REGION;
    r.x.dx = VDS_FLAGS_NO_ALLOC;
    sr.es = FP_SEG(dds);
    r.x.di = FP_OFF(dds);
    
    int86x(VDS_INT, &r, &r, &sr);
    
    return r.h.al;
}

/**
 * Request a DMA buffer
 */
uint8_t vds_request_buffer(uint32_t size, VDS_DDS *dds) {
    union REGS r;
    struct SREGS sr;
    
    /* Prepare DDS */
    dds->size = size;
    dds->segment = 0;
    dds->offset = 0;
    dds->buffer_id = 0;
    
    r.x.ax = VDS_REQUEST_BUFFER;
    r.x.dx = VDS_FLAGS_COPY | VDS_FLAGS_64K_ALIGN;
    sr.es = FP_SEG(dds);
    r.x.di = FP_OFF(dds);
    
    int86x(VDS_INT, &r, &r, &sr);
    
    return r.h.al;
}

/**
 * Release a DMA buffer
 */
uint8_t vds_release_buffer(VDS_DDS *dds) {
    union REGS r;
    struct SREGS sr;

    r.x.ax = VDS_RELEASE_BUFFER;
    r.x.dx = VDS_FLAGS_COPY;
    sr.es = FP_SEG(dds);
    r.x.di = FP_OFF(dds);

    int86x(VDS_INT, &r, &r, &sr);

    return r.h.al;
}

/**
 * Allocate a VDS common buffer (wrapper for buffer_alloc.c compatibility)
 * This provides a higher-level interface to vds_request_buffer
 */
bool vds_alloc_buffer(uint32_t size, uint16_t flags, vds_buffer_t *buffer) {
    uint8_t result;

    (void)flags;  /* Reserved for future use */

    if (!buffer || size == 0) {
        return false;
    }

    /* Initialize buffer structure */
    buffer->allocated = false;
    buffer->locked = false;
    buffer->size = 0;
    buffer->physical_addr = 0;
    buffer->virtual_addr = NULL;

    /* Check VDS availability */
    if (!vds_available()) {
        return false;
    }

    /* Prepare DDS for allocation */
    buffer->dds.size = size;
    buffer->dds.segment = 0;
    buffer->dds.offset = 0;
    buffer->dds.buffer_id = 0;
    buffer->dds.physical = 0;
    buffer->dds.flags = 0;

    /* Request buffer from VDS */
    result = vds_request_buffer(size, &buffer->dds);
    if (result != VDS_SUCCESS) {
        return false;
    }

    /* Fill in convenience fields from DDS */
    buffer->size = buffer->dds.size;
    buffer->physical_addr = buffer->dds.physical;
    buffer->virtual_addr = MK_FP(buffer->dds.segment, (uint16_t)buffer->dds.offset);
    buffer->allocated = true;
    buffer->locked = true;  /* VDS buffers are inherently locked */

    return true;
}

/**
 * Release a VDS common buffer (wrapper for buffer_alloc.c compatibility)
 */
bool vds_free_buffer(vds_buffer_t *buffer) {
    uint8_t result;

    if (!buffer || !buffer->allocated) {
        return false;
    }

    /* Release the buffer via VDS */
    result = vds_release_buffer(&buffer->dds);

    /* Clear buffer structure regardless of result */
    buffer->allocated = false;
    buffer->locked = false;
    buffer->size = 0;
    buffer->physical_addr = 0;
    buffer->virtual_addr = NULL;

    return (result == VDS_SUCCESS);
}
