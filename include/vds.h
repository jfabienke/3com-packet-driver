/**
 * @file vds.h
 * @brief Virtual DMA Services (VDS) for 3C515-TX bus master support
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * VDS provides memory locking and physical address translation for
 * the 3C515-TX's bus master DMA engine under V86 mode (EMM386, Windows 3.x).
 * 
 * NOTE: The 3C509B uses PIO only and does not require VDS.
 *       The 3C515-TX has its own bus master DMA engine and does
 *       NOT use the system 8237A DMA controller.
 */

#ifndef _VDS_H_
#define _VDS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

/* VDS INT 4Bh function codes */
#define VDS_GET_VERSION         0x8102  /* Get VDS version */
#define VDS_LOCK_REGION         0x8103  /* Lock DMA region */
#define VDS_UNLOCK_REGION       0x8104  /* Unlock DMA region */
#define VDS_SCATTER_LOCK        0x8105  /* Scatter/gather lock */
#define VDS_SCATTER_UNLOCK      0x8106  /* Scatter/gather unlock */
#define VDS_REQUEST_BUFFER      0x8107  /* Request DMA buffer */
#define VDS_RELEASE_BUFFER      0x8108  /* Release DMA buffer */
#define VDS_COPY_TO_BUFFER      0x8109  /* Copy to DMA buffer */
#define VDS_COPY_FROM_BUFFER    0x810A  /* Copy from DMA buffer */
#define VDS_DISABLE_TRANSLATION 0x810B  /* Disable address translation */
#define VDS_ENABLE_TRANSLATION  0x810C  /* Enable address translation */

/* VDS error codes */
#define VDS_SUCCESS             0x00
#define VDS_REGION_NOT_LOCKED   0x01
#define VDS_LOCK_FAILED         0x02
#define VDS_INVALID_PARAMS      0x03
#define VDS_BOUNDARY_CROSSED    0x04
#define VDS_BUFFER_IN_USE       0x05
#define VDS_REGION_TOO_LARGE    0x06
#define VDS_BUFFER_BOUNDARY     0x07
#define VDS_INVALID_ID          0x08
#define VDS_BUFFER_NOT_LOCKED   0x09
#define VDS_INVALID_SIZE        0x0A
#define VDS_BOUNDARY_VIOLATION  0x0B
#define VDS_INVALID_ALIGNMENT   0x0C
#define VDS_NOT_SUPPORTED       0x0F
#define VDS_FLAGS_NOT_SUPPORTED 0x10

/* VDS flags per VDS 1.0 specification */
/* GPT-5 Fix: Corrected flag definitions per spec */
#define VDS_NO_AUTO_REMAP       0x02    /* Don't auto-remap buffer */
#define VDS_NO_AUTO_ALLOC       0x04    /* Don't auto-allocate buffer */
#define VDS_ALIGN_64K           0x10    /* Align on 64K boundary */
#define VDS_ALIGN_128K          0x20    /* Align on 128K boundary */
/* Note: VDS_COPY_DATA removed - not a standard VDS flag */
/* Direction is specified differently in VDS specification */
#define VDS_NO_CROSS_64K        0x80    /* Don't cross 64K boundary */
#define VDS_CONTIG_REQUIRED     0x01    /* Contiguous buffer required */
#define VDS_ALLOW_NONCONTIG     0x00    /* Allow non-contiguous buffer */

/* VDS version structure */
typedef struct {
    uint8_t major;              /* Major version */
    uint8_t minor;              /* Minor version */
    uint16_t flags;             /* VDS capability flags */
    uint16_t max_dma_size;      /* Maximum DMA buffer size */
    uint8_t dma_id;             /* DMA buffer ID */
    uint8_t reserved;
} vds_version_t;

/* VDS DMA descriptor structure */
typedef struct {
    uint32_t size;              /* Region size in bytes */
    uint32_t offset;            /* Linear offset */
    uint16_t segment;           /* Segment (unused in linear) */
    uint16_t buffer_id;         /* Buffer ID from VDS */
    uint32_t physical_addr;     /* Physical address */
} vds_dma_descriptor_t;

/* VDS Extended DDS (DMA Descriptor Structure) */
typedef struct {
    uint32_t region_size;       /* Size of region */
    uint32_t offset;            /* Offset in region */
    uint16_t segment;           /* Segment selector */
    uint16_t buffer_id;         /* VDS buffer ID */
    uint32_t physical_address;  /* Physical address */
    uint32_t region_avail;      /* Available contiguous bytes */
    uint32_t pages_used;        /* Number of pages used */
} vds_edds_t;

/* VDS global state */
typedef struct {
    bool available;             /* VDS services available */
    bool initialized;           /* VDS initialized */
    vds_version_t version;      /* VDS version info */
    bool v86_mode;              /* Running in V86 mode */
    bool translation_enabled;   /* Address translation active */
    uint16_t locked_regions;    /* Number of locked regions */
} vds_state_t;

/* Function prototypes */

/**
 * @brief Initialize VDS support
 * 
 * Detects VDS availability and initializes services if present.
 * Must be called after V86 mode detection.
 * 
 * @return 0 on success, negative on error
 */
int vds_init(void);

/**
 * @brief Check if VDS is available
 * 
 * @return true if VDS services are available
 */
bool vds_available(void);

/**
 * @brief Get VDS version information
 * 
 * @param version Output version structure
 * @return 0 on success, negative on error
 */
int vds_get_version(vds_version_t *version);

/**
 * @brief Lock a DMA region for bus master access
 * 
 * Locks a memory region and returns its physical address.
 * The region remains locked until explicitly unlocked.
 * 
 * @param linear_addr Linear address of region
 * @param size Size of region in bytes
 * @param descriptor Output DMA descriptor
 * @return 0 on success, VDS error code on failure
 */
int vds_lock_region(void *linear_addr, uint32_t size, 
                    vds_dma_descriptor_t *descriptor);

/**
 * @brief Unlock a previously locked DMA region
 * 
 * @param descriptor DMA descriptor from lock operation
 * @return 0 on success, VDS error code on failure
 */
int vds_unlock_region(vds_dma_descriptor_t *descriptor);

/**
 * @brief Request a DMA buffer from VDS
 * 
 * Allocates a DMA-safe buffer that doesn't cross boundaries.
 * 
 * @param size Required buffer size
 * @param flags VDS flags (alignment, boundary constraints)
 * @param descriptor Output DMA descriptor
 * @return 0 on success, VDS error code on failure
 */
int vds_request_buffer(uint32_t size, uint16_t flags,
                      vds_dma_descriptor_t *descriptor);

/**
 * @brief Release a VDS-allocated buffer
 * 
 * @param descriptor DMA descriptor from request operation
 * @return 0 on success, VDS error code on failure
 */
int vds_release_buffer(vds_dma_descriptor_t *descriptor);

/**
 * @brief Copy data to a VDS buffer
 * 
 * @param descriptor DMA descriptor
 * @param source Source data pointer
 * @param size Size to copy
 * @return 0 on success, negative on error
 */
int vds_copy_to_buffer(vds_dma_descriptor_t *descriptor,
                      void *source, uint32_t size);

/**
 * @brief Copy data from a VDS buffer
 * 
 * @param descriptor DMA descriptor
 * @param dest Destination pointer
 * @param size Size to copy
 * @return 0 on success, negative on error
 */
int vds_copy_from_buffer(vds_dma_descriptor_t *descriptor,
                        void *dest, uint32_t size);

/**
 * @brief Copy data to VDS DMA buffer (GPT-5: Added)
 * 
 * @param dma_buffer VDS DMA buffer descriptor
 * @param src_buffer Source data buffer
 * @param length Number of bytes to copy
 * @return true on success, false on failure
 */
bool vds_copy_to_dma_buffer(vds_dma_descriptor_t *dma_buffer, void __far *src_buffer, uint32_t length);

/**
 * @brief Copy data from VDS DMA buffer (GPT-5: Added)
 * 
 * @param dma_buffer VDS DMA buffer descriptor
 * @param dst_buffer Destination data buffer
 * @param length Number of bytes to copy
 * @return true on success, false on failure
 */
bool vds_copy_from_dma_buffer(vds_dma_descriptor_t *dma_buffer, void __far *dst_buffer, uint32_t length);

/* NOTE: vds_program_dma_channel() removed - neither 3C509B nor 3C515-TX use system DMA */

/**
 * @brief Disable VDS address translation
 * 
 * Temporarily disables translation for direct physical access.
 * Must be re-enabled after DMA operation.
 * 
 * @return 0 on success, negative on error
 */
int vds_disable_translation(void);

/**
 * @brief Enable VDS address translation
 * 
 * Re-enables translation after direct physical access.
 * 
 * @return 0 on success, negative on error
 */
int vds_enable_translation(void);

/**
 * @brief Check if running in V86 mode
 * 
 * @return true if in V86 mode
 */
bool vds_in_v86_mode(void);

/**
 * @brief Get physical address for DMA
 * 
 * Returns the physical address for DMA operations.
 * Handles both V86 (via VDS) and real mode cases.
 * 
 * @param linear_addr Linear/virtual address
 * @param size Size of region
 * @param phys_addr Output physical address
 * @return 0 on success, negative on error
 */
int vds_get_physical_address(void *linear_addr, uint32_t size,
                            uint32_t *phys_addr);

/**
 * @brief Cleanup VDS resources
 * 
 * Unlocks all regions and releases all buffers.
 */
void vds_cleanup(void);

/**
 * @brief Get VDS error string
 * 
 * @param error VDS error code
 * @return Error description string
 */
const char* vds_error_string(int error);

/* GPT-5 A+ Implementation: Scatter/gather VDS functions */

/* VDS scatter/gather entry for new DMA framework */
typedef struct vds_sg_entry_s {
    uint32_t phys;
    uint16_t len;
} vds_sg_entry_t;

/**
 * @brief Lock region with scatter/gather support
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
                       uint16_t *out_sg_count, uint16_t *out_lock_handle);

/**
 * @brief Unlock region previously locked with vds_lock_region_sg
 * 
 * @param lock_handle Lock handle from vds_lock_region_sg
 * @return 0 on success, VDS error code on failure
 */
int vds_unlock_region_sg(uint16_t lock_handle);

#ifdef __cplusplus
}
#endif

#endif /* _VDS_H_ */