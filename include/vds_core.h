/**
 * @file vds_core.h
 * @brief Unified VDS Core Layer - Raw VDS Operations
 * 
 * Core VDS functionality that provides raw INT 4Bh operations.
 * Leverages existing cpu_detect.h for V86 mode detection.
 * 
 * This is the lowest layer of the unified VDS architecture:
 * - No business logic or validation
 * - Direct INT 4Bh interface
 * - Single source of truth for VDS presence
 */

#ifndef VDS_CORE_H
#define VDS_CORE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VDS INT 4Bh function codes */
#define VDS_FUNC_GET_VERSION        0x8102  /* Get VDS version */
#define VDS_FUNC_LOCK_REGION        0x8103  /* Lock DMA region */
#define VDS_FUNC_UNLOCK_REGION      0x8104  /* Unlock DMA region */
#define VDS_FUNC_SCATTER_LOCK       0x8105  /* Scatter/gather lock */
#define VDS_FUNC_SCATTER_UNLOCK     0x8106  /* Scatter/gather unlock */
#define VDS_FUNC_REQUEST_BUFFER     0x8107  /* Request DMA buffer */
#define VDS_FUNC_GET_SG_LIST        0x8108  /* Get scatter/gather list */
#define VDS_FUNC_COPY_TO_BUFFER     0x8109  /* Copy to DMA buffer */
#define VDS_FUNC_COPY_FROM_BUFFER   0x810A  /* Copy from DMA buffer */
#define VDS_FUNC_DISABLE_TRANSLATION 0x810B /* Disable address translation */
#define VDS_FUNC_ENABLE_TRANSLATION  0x810C /* Enable address translation */

/* VDS device capability flags */
#define VDS_FLAG_ISA_DMA            0x0001  /* ISA DMA constraints */
#define VDS_FLAG_NO_64K_CROSS       0x0002  /* Cannot cross 64K boundary */
#define VDS_FLAG_BUSMASTER          0x0004  /* Bus master device */
#define VDS_FLAG_24BIT_ADDRESS      0x0008  /* 24-bit address limit (ISA) */

/* VDS raw error codes from INT 4Bh */
#define VDS_RAW_SUCCESS             0x00
#define VDS_RAW_REGION_NOT_LOCKED   0x01
#define VDS_RAW_LOCK_FAILED         0x02
#define VDS_RAW_INVALID_PARAMS      0x03
#define VDS_RAW_BOUNDARY_CROSSED    0x04
#define VDS_RAW_BUFFER_IN_USE       0x05
#define VDS_RAW_REGION_TOO_LARGE    0x06
#define VDS_RAW_BUFFER_BOUNDARY     0x07
#define VDS_RAW_INVALID_ID          0x08
#define VDS_RAW_BUFFER_NOT_LOCKED   0x09
#define VDS_RAW_INVALID_SIZE        0x0A
#define VDS_RAW_BOUNDARY_VIOLATION  0x0B
#define VDS_RAW_INVALID_ALIGNMENT   0x0C
#define VDS_RAW_NOT_SUPPORTED       0x0F
#define VDS_RAW_FLAGS_NOT_SUPPORTED 0x10

/* VDS Translation types (bits 2-3 of AX register after lock) */
typedef enum {
    VDS_TRANS_DIRECT = 0,       /* Direct mapping - no translation needed */
    VDS_TRANS_REMAPPED = 1,     /* Remapped - address translated but no copy needed */
    VDS_TRANS_ALTERNATE = 2,    /* Alternate buffer - copy required after DMA */
    VDS_TRANS_UNKNOWN = 3       /* Reserved/unknown */
} vds_translation_type_t;

/* VDS capability flags */
typedef struct {
    bool present;               /* VDS services available */
    uint8_t major_version;      /* Major version number */
    uint8_t minor_version;      /* Minor version number */
    uint16_t oem_number;        /* OEM number */
    uint16_t revision;          /* Revision number */
    uint32_t max_dma_size;      /* Maximum DMA buffer size */
    uint16_t flags;             /* VDS capability flags */
    bool supports_scatter;      /* Scatter/gather supported */
    bool supports_64k_cross;    /* Can handle 64K boundary crossing */
    uint8_t max_sg_entries;     /* Max scatter/gather entries */
} vds_caps_t;

/* Ensure all VDS structures are properly packed to 1-byte alignment */
#pragma pack(push, 1)

/* Raw VDS DMA descriptor (matches INT 4Bh specification) */
typedef struct {
    uint32_t region_size;       /* 00h: Size in bytes */
    uint32_t linear_offset;     /* 04h: Linear offset */
    uint16_t segment;           /* 08h: Segment (real mode) */
    uint16_t selector;          /* 0Ah: Selector (protected mode) */
    uint16_t buffer_id;         /* 0Ch: Buffer ID */
    uint32_t physical_address;  /* 10h: Physical address */
} vds_raw_descriptor_t;

/* VDS scatter/gather entry */
typedef struct {
    uint32_t physical_addr;     /* Physical address */
    uint32_t size;              /* Size in bytes */
} vds_sg_entry_t;

/* Extended descriptor for scatter/gather */
typedef struct {
    vds_raw_descriptor_t base;  /* Base descriptor */
    uint16_t num_pages;         /* Number of pages */
    uint16_t reserved;          /* Reserved */
    vds_sg_entry_t* sg_list;    /* Scatter/gather list */
} vds_raw_extended_desc_t;

/* VDS copy descriptor for ALTERNATE buffer operations */
typedef struct {
    uint32_t region_size;       /* 00h: Size to copy */
    uint32_t offset;            /* 04h: Offset within locked region */
    uint32_t client_linear;     /* 08h: Client buffer linear address */
    uint16_t buffer_id;         /* 0Ch: Lock handle */
    uint16_t reserved;          /* 0Eh: Reserved (must be 0) */
} vds_copy_descriptor_t;

/* S/G list descriptor for INT 4Bh function 0x8108 */
typedef struct {
    uint32_t region_size;       /* 00h: Size of locked region */
    uint32_t linear_offset;     /* 04h: Linear offset */
    uint16_t segment;           /* 08h: Segment */
    uint16_t reserved1;         /* 0Ah: Reserved */
    uint16_t num_avail;         /* 0Ch: Number of S/G entries available */
    uint16_t num_used;          /* 0Eh: Number of S/G entries returned */
    uint32_t sg_list_addr;      /* 10h: Address of S/G list buffer */
} vds_sg_descriptor_t;
#pragma pack(pop)

/* Portable compile-time size checks for pre-C11 compilers */
#ifdef __STDC_VERSION__
  #if __STDC_VERSION__ >= 201112L
    /* C11 or newer - use static_assert */
    static_assert(sizeof(vds_copy_descriptor_t) == 16, "VDS copy descriptor must be 16 bytes");
    static_assert(sizeof(vds_sg_descriptor_t) == 20, "VDS S/G descriptor must be 20 bytes");
  #else
    /* Pre-C11 - use typedef array trick */
    typedef char vds_assert_copy_desc_size[(sizeof(vds_copy_descriptor_t) == 16) ? 1 : -1];
    typedef char vds_assert_sg_desc_size[(sizeof(vds_sg_descriptor_t) == 20) ? 1 : -1];
  #endif
#else
  /* Old compiler without __STDC_VERSION__ - use typedef trick */
  typedef char vds_assert_copy_desc_size[(sizeof(vds_copy_descriptor_t) == 16) ? 1 : -1];
  typedef char vds_assert_sg_desc_size[(sizeof(vds_sg_descriptor_t) == 20) ? 1 : -1];
#endif

/* Transfer direction for DMA operations */
typedef enum {
    VDS_DIR_HOST_TO_DEVICE = 0, /* Write to device (needs pre-copy) */
    VDS_DIR_DEVICE_TO_HOST = 1, /* Read from device (needs post-copy) */
    VDS_DIR_BIDIRECTIONAL = 2   /* Both directions (needs both copies) */
} vds_transfer_direction_t;

/* Raw lock result */
typedef struct {
    bool success;               /* Lock succeeded */
    uint16_t error_code;        /* Raw VDS error code (16-bit) */
    uint16_t lock_handle;       /* Lock handle for unlock */
    uint32_t physical_addr;     /* Physical address (first segment if S/G) */
    uint32_t actual_length;     /* Actual locked length (may be < requested) */
    vds_translation_type_t translation_type; /* How VDS mapped the buffer */
    bool is_scattered;          /* Buffer is scattered */
    uint16_t sg_count;          /* Number of S/G entries */
    bool needs_pre_copy;        /* HOST_TO_DEVICE with ALTERNATE */
    bool needs_post_copy;       /* DEVICE_TO_HOST with ALTERNATE */
    vds_sg_entry_t* sg_list;    /* Scatter/gather list if is_scattered */
} vds_raw_lock_result_t;

/* Core initialization and detection */

/**
 * Initialize VDS core services
 * Uses cpu_detect.h to check V86 mode and VDS presence
 */
int vds_core_init(void);

/**
 * Check if VDS is present and available
 */
bool vds_is_present(void);

/**
 * Check if running in V86 mode (uses cpu_detect.h)
 */
bool vds_is_v86_mode(void);

/**
 * Get VDS capabilities
 */
const vds_caps_t* vds_get_capabilities(void);

/* Raw VDS operations (INT 4Bh wrappers) */

/**
 * Raw VDS lock region
 * @param linear_addr Linear address to lock
 * @param size Size in bytes
 * @param flags VDS flags (contiguous, alignment, etc.)
 * @param direction Transfer direction (for copy determination)
 * @param result Output result structure
 * @return Raw VDS error code
 */
uint8_t vds_core_lock_region(void far* linear_addr, uint32_t size, 
                             uint16_t flags, vds_transfer_direction_t direction,
                             vds_raw_lock_result_t* result);

/**
 * Raw VDS unlock region
 * @param lock_handle Handle from lock operation
 * @return Raw VDS error code
 */
uint8_t vds_core_unlock_region(uint16_t lock_handle);

/**
 * Copy data to VDS ALTERNATE buffer before DMA write
 * Required when needs_pre_copy is true
 * @param lock_handle VDS lock handle
 * @param source Source data buffer
 * @param size Actual size to copy (from lock result)
 * @param offset Offset within locked region
 * @return Raw VDS error code (16-bit)
 */
uint16_t vds_core_copy_to_alternate(uint16_t lock_handle, void far* source, 
                                    uint32_t size, uint32_t offset);

/**
 * Copy data from VDS ALTERNATE buffer after DMA read
 * Required when needs_post_copy is true
 * @param lock_handle VDS lock handle
 * @param dest Destination buffer
 * @param size Actual size to copy (from lock result)
 * @param offset Offset within locked region
 * @return Raw VDS error code (16-bit)
 */
uint16_t vds_core_copy_from_alternate(uint16_t lock_handle, void far* dest, 
                                      uint32_t size, uint32_t offset);

/**
 * Get scatter/gather list for locked region
 * @param lock_handle Handle from lock operation
 * @param sg_list Output scatter/gather list
 * @param max_entries Maximum entries in list
 * @param actual_entries Actual entries returned
 * @return Raw VDS error code
 */
uint16_t vds_core_get_sg_list(uint16_t lock_handle, vds_sg_entry_t far* sg_list,
                              uint16_t max_entries, uint16_t* actual_entries);

/**
 * Request DMA buffer from VDS
 * @param size Required size
 * @param flags Alignment and boundary flags
 * @param result Output result structure
 * @return Raw VDS error code
 */
uint8_t vds_core_request_buffer(uint32_t size, uint16_t flags,
                                vds_raw_lock_result_t* result);

/**
 * Release VDS-allocated buffer
 * @param buffer_id Buffer ID from request
 * @return Raw VDS error code
 */
uint8_t vds_core_release_buffer(uint16_t buffer_id);

/**
 * Copy data to VDS buffer
 * @param buffer_id Buffer ID
 * @param src Source data
 * @param size Size to copy
 * @return Raw VDS error code (16-bit)
 */
uint16_t vds_core_copy_to_buffer(uint16_t buffer_id, void far* src, uint32_t size);

/**
 * Copy data from VDS buffer
 * @param buffer_id Buffer ID
 * @param dst Destination
 * @param size Size to copy
 * @return Raw VDS error code (16-bit)
 */
uint16_t vds_core_copy_from_buffer(uint16_t buffer_id, void far* dst, uint32_t size);

/* Utility functions */

/**
 * Convert linear address to physical (real mode only)
 * @param linear_addr Linear address
 * @return Physical address
 */
uint32_t vds_linear_to_physical(void far* linear_addr);

/**
 * Get VDS error string
 * @param error_code Raw VDS error code
 * @return Error description
 */
const char* vds_core_error_string(uint8_t error_code);

/* Statistics (from vds.c) */
typedef struct {
    uint32_t lock_attempts;
    uint32_t lock_successes;
    uint32_t lock_failures;
    uint32_t unlock_attempts;
    uint32_t unlock_successes;
    uint32_t unlock_failures;
    uint32_t scatter_gather_locks;
    uint32_t boundary_violations;
    uint32_t vds_bounce_detections;    /* VDS used bounce buffer */
    uint32_t vds_direct_locks;         /* VDS locked in place */
} vds_core_stats_t;

/**
 * Get VDS core statistics
 */
void vds_core_get_stats(vds_core_stats_t* stats);

/**
 * Reset VDS core statistics
 */
void vds_core_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* VDS_CORE_H */