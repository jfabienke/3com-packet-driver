/**
 * @file dma_safety.h
 * @brief DMA Safety Framework with Bounce Buffers - Header File
 *
 * CRITICAL: GPT-5 Identified DMA Safety Requirements
 * "Ensure all DMA-visible buffers respect the strictest device constraints.
 *  If you ever use upper memory/XMS for buffers, implement reliable bounce buffering."
 *
 * This framework provides:
 * 1. Automatic 64KB boundary checking
 * 2. ISA 16MB limit enforcement
 * 3. Device-specific constraint validation
 * 4. Transparent bounce buffer management
 * 5. Cache coherency handling
 * 6. Physical contiguity guarantees
 */

#ifndef DMA_SAFETY_H
#define DMA_SAFETY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DMA buffer types */
typedef enum {
    DMA_BUFFER_TYPE_TX,                     /* Transmit buffer */
    DMA_BUFFER_TYPE_RX,                     /* Receive buffer */
    DMA_BUFFER_TYPE_DESCRIPTOR,             /* Descriptor ring */
    DMA_BUFFER_TYPE_GENERAL,                /* General purpose */
    DMA_BUFFER_TYPE_COUNT
} dma_buffer_type_t;

/* DMA direction for cache coherency */
typedef enum {
    DMA_TO_DEVICE = 1,                      /* CPU -> Device (TX) */
    DMA_FROM_DEVICE = 2,                    /* Device -> CPU (RX) */
    DMA_BIDIRECTIONAL = 3                   /* Both directions */
} dma_direction_t;

/* Device DMA capability descriptor - GPT-5 recommendation */
typedef struct {
    uint8_t dma_addr_bits;                  /* 24 for ISA, 32 for PCI */
    uint16_t max_sg_entries;                /* Scatter-gather capability */
    uint16_t sg_boundary;                   /* 64KB boundary limit */
    uint16_t alignment;                     /* Buffer alignment requirement */
    uint16_t descriptor_alignment;          /* Descriptor ring alignment */
    bool needs_vds;                         /* VDS required for V86/Windows */
    uint16_t rx_copybreak;                  /* Dynamic threshold from testing */
    uint16_t tx_copybreak;                  /* Dynamic threshold from testing */
    bool cache_coherent;                    /* From coherency analysis */
    bool supports_sg;                       /* Hardware scatter-gather support */
    /* GPT-5 Critical: ISA DMA constraints */
    bool no_64k_cross;                      /* ISA: Cannot cross 64KB boundaries */
    uint32_t max_segment_size;              /* Maximum DMA segment size */
    char device_name[32];                   /* "3C509B", "3C515-TX", "3C905", etc */
} device_caps_t;

/* Scatter-gather list for boundary-safe DMA */
typedef struct {
    uint16_t segment_count;                 /* Number of segments */
    struct {
        void* virt_addr;                    /* Virtual address */
        uint32_t phys_addr;                 /* Physical address */
        uint16_t length;                    /* Segment length */
    } segments[8];                          /* Maximum 8 segments */
    uint32_t total_length;                  /* Total buffer length */
    bool needs_bounce;                      /* Requires bounce buffers */
} dma_sg_list_t;

/* DMA buffer descriptor - opaque structure */
typedef struct dma_buffer_descriptor dma_buffer_descriptor_t;

/* Core DMA safety functions */
int dma_safety_init(void);
int dma_safety_shutdown(void);
int register_3com_device_constraints(void);

/* Buffer allocation and management */
dma_buffer_descriptor_t* dma_allocate_buffer(uint32_t size, uint32_t alignment, 
                                           dma_buffer_type_t type, const char* device_name);
int dma_free_buffer(dma_buffer_descriptor_t* desc);

/* Enhanced device-aware allocation - GPT-5 recommendation */
dma_buffer_descriptor_t* dma_allocate_hybrid_buffer(uint32_t size, device_caps_t* caps,
                                                   dma_direction_t direction, 
                                                   const char* device_name);

/* Scatter-gather safety utility - GPT-5 recommendation */
dma_sg_list_t* dma_build_safe_sg(void* buf, uint32_t len, device_caps_t* caps);
int dma_free_sg_list(dma_sg_list_t* sg_list);

/* Buffer access functions */
void* dma_get_virtual_address(const dma_buffer_descriptor_t* desc);
uint32_t dma_get_physical_address(const dma_buffer_descriptor_t* desc);
uint32_t dma_get_buffer_size(const dma_buffer_descriptor_t* desc);
bool dma_is_bounce_buffer(const dma_buffer_descriptor_t* desc);

/* Synchronization for bounce buffers - Enhanced with direction awareness */
int dma_sync_for_device(dma_buffer_descriptor_t* desc, dma_direction_t direction);
int dma_sync_for_cpu(dma_buffer_descriptor_t* desc, dma_direction_t direction);

/* Legacy compatibility - will be deprecated */
int dma_sync_for_device_legacy(dma_buffer_descriptor_t* desc);
int dma_sync_for_cpu_legacy(dma_buffer_descriptor_t* desc);

/* Device-specific allocation helpers */
dma_buffer_descriptor_t* dma_allocate_tx_buffer(uint32_t size, const char* device_name);
dma_buffer_descriptor_t* dma_allocate_rx_buffer(uint32_t size, const char* device_name);
dma_buffer_descriptor_t* dma_allocate_descriptor_ring(uint32_t count, uint32_t entry_size, const char* device_name);

/* Constraint validation */
bool dma_validate_address_range(uint32_t physical_addr, uint32_t size, const char* device_name);
bool dma_check_64kb_boundary(uint32_t physical_addr, uint32_t size);
bool dma_check_16mb_limit(uint32_t physical_addr, uint32_t size);
bool dma_check_alignment(uint32_t physical_addr, uint32_t required_alignment);

/* Statistics and debugging */
void dma_print_statistics(void);
void dma_print_buffer_info(const dma_buffer_descriptor_t* desc);
uint32_t dma_get_bounce_buffer_usage(void);
uint32_t dma_get_total_allocations(void);

/* TSR Defensive Functions */
int dma_check_integrity(void);              /* Check all structures for corruption */
int dma_emergency_recovery(void);           /* Attempt recovery from corruption */
bool dma_periodic_validation(void);         /* Periodic integrity validation */

/* 3Com device-specific helpers */
dma_buffer_descriptor_t* dma_allocate_3c509b_buffer(uint32_t size, dma_buffer_type_t type);
dma_buffer_descriptor_t* dma_allocate_3c589_buffer(uint32_t size, dma_buffer_type_t type);
dma_buffer_descriptor_t* dma_allocate_3c905_buffer(uint32_t size, dma_buffer_type_t type);
dma_buffer_descriptor_t* dma_allocate_3c515tx_buffer(uint32_t size, dma_buffer_type_t type);

/* Constants */
#define DMA_MAX_ETHERNET_FRAME     1518    /* Maximum Ethernet frame size */
#define DMA_DESCRIPTOR_ALIGNMENT   16      /* Descriptor alignment requirement */
#define DMA_BUFFER_ALIGNMENT       8       /* General buffer alignment */
#define DMA_ISA_LIMIT             0x1000000 /* 16MB ISA limit */

/* Error codes specific to DMA operations */
#define ERROR_DMA_64KB_BOUNDARY    -1001   /* Buffer crosses 64KB boundary */
#define ERROR_DMA_16MB_LIMIT       -1002   /* Buffer exceeds 16MB limit */  
#define ERROR_DMA_ALIGNMENT        -1003   /* Buffer not properly aligned */
#define ERROR_DMA_NOT_CONTIGUOUS   -1004   /* Buffer not physically contiguous */
#define ERROR_DMA_BOUNCE_FAILED    -1005   /* Bounce buffer allocation failed */
#define ERROR_DMA_SYNC_FAILED      -1006   /* Buffer synchronization failed */

/* Inline helper functions */

/**
 * @brief Check if DMA framework is initialized
 */
static inline bool dma_is_initialized(void) {
    extern bool g_dma_framework_initialized;
    return g_dma_framework_initialized;
}

/**
 * @brief Get recommended buffer size for device
 */
static inline uint32_t dma_get_recommended_buffer_size(dma_buffer_type_t type) {
    switch (type) {
        case DMA_BUFFER_TYPE_TX:
        case DMA_BUFFER_TYPE_RX:
            return DMA_MAX_ETHERNET_FRAME;
        case DMA_BUFFER_TYPE_DESCRIPTOR:
            return 64; /* Typical descriptor size */
        default:
            return 2048; /* General purpose default */
    }
}

/**
 * @brief Get recommended alignment for device
 */
static inline uint32_t dma_get_recommended_alignment(const char* device_name) {
    if (strstr(device_name, "3C509") || strstr(device_name, "3C589")) {
        return 4; /* ISA cards need 4-byte alignment */
    } else {
        return 16; /* PCI cards prefer 16-byte alignment */
    }
}

/**
 * @brief Check if device requires bounce buffers
 */
static inline bool dma_device_needs_bounce_buffers(const char* device_name) {
    /* ISA devices are more likely to need bounce buffers */
    return (strstr(device_name, "3C509") != NULL) || 
           (strstr(device_name, "3C589") != NULL) ||
           (strstr(device_name, "3C515") != NULL);
}

/* Macro helpers for common operations */

/**
 * @brief Allocate standard Ethernet frame buffer
 */
#define DMA_ALLOC_ETHERNET_FRAME(device) \
    dma_allocate_buffer(DMA_MAX_ETHERNET_FRAME, \
                       dma_get_recommended_alignment(device), \
                       DMA_BUFFER_TYPE_GENERAL, device)

/**
 * @brief Allocate descriptor ring with proper alignment
 */
#define DMA_ALLOC_DESCRIPTOR_RING(count, entry_size, device) \
    dma_allocate_buffer((count) * (entry_size), \
                       DMA_DESCRIPTOR_ALIGNMENT, \
                       DMA_BUFFER_TYPE_DESCRIPTOR, device)

/**
 * @brief Safe buffer access with bounds checking
 */
#define DMA_SAFE_MEMCPY(desc, offset, src, size) \
    do { \
        if ((offset) + (size) <= dma_get_buffer_size(desc)) { \
            memcpy((char*)dma_get_virtual_address(desc) + (offset), (src), (size)); \
        } \
    } while(0)

/* GPT-5 Safety Validation Macros */

/**
 * @brief Validate buffer for ISA DMA safety
 */
#define DMA_VALIDATE_ISA_SAFE(physical_addr, size) \
    (dma_check_64kb_boundary(physical_addr, size) && \
     dma_check_16mb_limit(physical_addr, size))

/**
 * @brief Validate buffer for PCI DMA safety  
 */
#define DMA_VALIDATE_PCI_SAFE(physical_addr, size) \
    (dma_check_alignment(physical_addr, 16) && \
     (physical_addr + size) <= 0xFFFFFFFF)

/**
 * @brief Ensure proper buffer synchronization
 */
#define DMA_ENSURE_SYNC(desc, for_device) \
    do { \
        if (dma_is_bounce_buffer(desc)) { \
            if (for_device) dma_sync_for_device_legacy(desc); \
            else dma_sync_for_cpu_legacy(desc); \
        } \
    } while(0)

/* Enhanced synchronization with direction awareness - GPT-5 recommendation */
#define DMA_ENSURE_SYNC_DIRECTION(desc, direction, for_device) \
    do { \
        if (dma_is_bounce_buffer(desc)) { \
            if (for_device) dma_sync_for_device(desc, direction); \
            else dma_sync_for_cpu(desc, direction); \
        } \
    } while(0)

/* ============================================================================
 * Device Capability Descriptors - GPT-5 Recommendation
 * ============================================================================ */

/* 3Com device capability constants */
extern const device_caps_t caps_3c509b;    /* ISA, PIO only */
extern const device_caps_t caps_3c515tx;   /* ISA bus master, 24-bit DMA */
extern const device_caps_t caps_3c589;     /* PCMCIA version of 3C509B */
extern const device_caps_t caps_3c590;     /* Vortex PCI, 32-bit DMA */
extern const device_caps_t caps_3c595;     /* Vortex 100Mbps */
extern const device_caps_t caps_3c900;     /* Boomerang TPO */
extern const device_caps_t caps_3c905;     /* Boomerang TX/B */
extern const device_caps_t caps_3c905b;    /* Cyclone */
extern const device_caps_t caps_3c905c;    /* Tornado */

/* Device capability lookup */
const device_caps_t* dma_get_device_caps(const char* device_name);
int dma_register_device_caps(const char* device_name, const device_caps_t* caps);

/* GPT-5 Enhancement: Device capability validation */
bool validate_device_caps(const device_caps_t* caps, const char* device_name);
bool validate_all_device_caps(void);

#ifdef __cplusplus
}
#endif

#endif /* DMA_SAFETY_H */