/**
 * @file vds.h
 * @brief Virtual DMA Services (VDS) interface for physical address mapping
 *
 * VDS provides physical address information for DMA operations under
 * memory managers like EMM386, QEMM, and Windows 3.x DOS boxes.
 */

#ifndef _VDS_H_
#define _VDS_H_

#include <stdint.h>
#include <stdbool.h>

/* VDS service IDs */
#define VDS_GET_VERSION         0x8102
#define VDS_LOCK_REGION        0x8103
#define VDS_UNLOCK_REGION      0x8104
#define VDS_SCATTER_LOCK       0x8105
#define VDS_SCATTER_UNLOCK     0x8106
#define VDS_REQUEST_BUFFER     0x8107
#define VDS_RELEASE_BUFFER     0x8108
#define VDS_COPY_TO_BUFFER     0x8109
#define VDS_COPY_FROM_BUFFER   0x810A
#define VDS_DISABLE_TRANSLATION 0x810B
#define VDS_ENABLE_TRANSLATION  0x810C

/* VDS flags */
#define VDS_FLAGS_COPY          0x02    /* Copy to/from buffer if needed */
#define VDS_FLAGS_NO_ALLOC      0x04    /* Don't allocate buffer */
#define VDS_FLAGS_NO_REMAP      0x08    /* Don't remap to contiguous region */
#define VDS_FLAGS_64K_ALIGN     0x10    /* Align on 64K boundary */
#define VDS_FLAGS_128K_ALIGN    0x20    /* Align on 128K boundary */
#define VDS_FLAGS_NO_CACHE_FLUSH 0x40   /* VDS handles cache, no driver flush needed */
#define VDS_FLAGS_NO_CACHE_INV  0x80    /* VDS handles cache, no driver invalidate needed */

/* VDS error codes */
#define VDS_SUCCESS             0x00
#define VDS_REGION_NOT_CONTIGUOUS 0x01
#define VDS_REGION_CROSSED_BOUNDARY 0x02
#define VDS_UNABLE_TO_LOCK      0x03
#define VDS_NO_BUFFER_AVAILABLE 0x04
#define VDS_REGION_TOO_LARGE    0x05
#define VDS_BUFFER_IN_USE       0x06
#define VDS_INVALID_REGION      0x07

/* DMA Descriptor Structure (DDS) */
#pragma pack(push, 1)
typedef struct {
    uint32_t    size;           /* Region size in bytes */
    uint32_t    offset;         /* Linear offset */
    uint16_t    segment;        /* Segment (or selector) */
    uint16_t    buffer_id;      /* Buffer ID (0 if not allocated) */
    uint32_t    physical;       /* Physical address */
    uint16_t    flags;          /* Returned flags indicating cache handling */
} VDS_DDS;

/* Extended DDS for scatter/gather */
typedef struct {
    uint32_t    size;           /* Region size */
    uint32_t    offset;         /* Linear offset */
    uint16_t    segment;        /* Segment */
    uint16_t    reserved;
    uint16_t    num_avail;      /* Number of entries available */
    uint16_t    num_used;       /* Number of entries used */
} VDS_EDDS;

/* VDS scatter/gather entry */
typedef struct {
    uint32_t    physical;       /* Physical address */
    uint32_t    size;          /* Size in bytes */
} VDS_SG_ENTRY;
#pragma pack(pop)

/* Function prototypes */

/**
 * Check if VDS is available
 * @return true if VDS services are available
 */
bool vds_available(void);

/**
 * Get VDS version
 * @param major Major version number
 * @param minor Minor version number
 * @param flags VDS capability flags
 * @return true on success
 */
bool vds_get_version(uint8_t *major, uint8_t *minor, uint16_t *flags);

/**
 * Lock a memory region for DMA
 * @param ptr Far pointer to region
 * @param size Size in bytes
 * @param dds Filled with DMA descriptor
 * @return VDS error code (0 = success)
 */
uint8_t vds_lock_region(void far *ptr, uint32_t size, VDS_DDS *dds);

/**
 * Unlock a previously locked region
 * @param dds DMA descriptor from lock operation
 * @return VDS error code (0 = success)
 */
uint8_t vds_unlock_region(VDS_DDS *dds);

/**
 * Request a DMA buffer
 * @param size Size needed
 * @param dds Filled with buffer descriptor
 * @return VDS error code (0 = success)
 */
uint8_t vds_request_buffer(uint32_t size, VDS_DDS *dds);

/**
 * Release a DMA buffer
 * @param dds Buffer descriptor
 * @return VDS error code (0 = success)
 */
uint8_t vds_release_buffer(VDS_DDS *dds);

/**
 * Get physical address without VDS (real mode only)
 * @param ptr Far pointer
 * @return Physical address
 */
uint32_t far_ptr_to_physical(void far *ptr);

/**
 * Check if address crosses 64KB boundary
 * @param physical Physical address
 * @param size Size in bytes
 * @return true if crosses boundary
 */
bool crosses_64k_boundary(uint32_t physical, uint32_t size);

/**
 * Check if address is above 16MB (ISA DMA limit)
 * @param physical Physical address
 * @return true if above 16MB
 */
bool above_isa_limit(uint32_t physical);

/* Inline implementations for critical functions */

static inline uint32_t far_ptr_to_physical(void far *ptr) {
    return ((uint32_t)FP_SEG(ptr) << 4) + FP_OFF(ptr);
}

static inline bool crosses_64k_boundary(uint32_t physical, uint32_t size) {
    uint32_t start_64k = physical & 0xFFFF0000;
    uint32_t end_64k = (physical + size - 1) & 0xFFFF0000;
    return start_64k != end_64k;
}

static inline bool above_isa_limit(uint32_t physical) {
    return physical >= 0x1000000;  /* 16MB */
}

#endif /* _VDS_H_ */