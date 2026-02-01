/**
 * @file dma_boundary.h
 * @brief Enhanced DMA Boundary Checking - GPT-5 Improvements
 *
 * This module implements the GPT-5 recommended improvements for DMA safety:
 * - Proper physical address calculation with EMM386/QEMM awareness
 * - Separate TX/RX bounce buffer pools
 * - 16MB wraparound checking
 * - Descriptor splitting support
 * - Direction-specific cache operations
 */

#ifndef DMA_BOUNDARY_H
#define DMA_BOUNDARY_H

#include "portabl.h"   /* C89 compatibility: bool, uint32_t, etc. */
#include <stddef.h>    /* size_t */

/* DOS-specific includes - only for DOS compilers */
#if defined(__TURBOC__) || defined(__BORLANDC__) || defined(__WATCOMC__) || (defined(_MSC_VER) && _MSC_VER <= 1200)
#include <dos.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* DMA constraint definitions - GPT-5 Enhanced */
#define DMA_64KB_BOUNDARY       0x10000UL   /* 64KB boundary */
#define DMA_16MB_LIMIT          0x1000000UL /* 16MB ISA limit */
#define ISA_DMA_MAX_ADDR        0x00FFFFFFUL /* GPT-5 A+: ISA bus masters 24-bit limit */
#define DMA_4GB_LIMIT           0xFFFFFFFFUL /* 4GB limit for 32-bit */
#define DMA_CONVENTIONAL_LIMIT  0xA0000UL   /* 640KB conventional memory */

/* Alignment requirements */
#define DMA_ALIGNMENT_WORD      2           /* Word alignment */
#define DMA_ALIGNMENT_DWORD     4           /* DWORD alignment */
#define DMA_ALIGNMENT_PARA      16          /* Paragraph alignment */
#define DMA_ALIGNMENT_CACHE     64          /* Cache line alignment */

/**
 * @brief DMA safety check results - GPT-5 Enhanced Physical Memory Handling
 */
typedef struct {
    uint32_t phys_addr;         /* Physical address */
    uint32_t end_addr;          /* Physical end address */
    bool crosses_64k;           /* Crosses 64KB boundary */
    bool crosses_16m;           /* Crosses or exceeds 16MB limit */
    bool exceeds_4gb;           /* Exceeds 32-bit addressing */
    bool exceeds_isa_24bit;     /* GPT-5 A+: Exceeds ISA 24-bit addressing */
    bool needs_bounce;          /* Requires bounce buffer */
    bool in_conventional;       /* Buffer in conventional memory */
    bool in_umb;                /* Buffer in UMB region */
    bool in_xms;                /* Buffer in XMS region */
    uint16_t alignment_error;   /* Alignment violation (0 = aligned) */
    uint16_t split_count;       /* Number of segments if split */
    
    /* GPT-5 Critical: Physical contiguity and page locking */
    bool is_contiguous;         /* Entire buffer is physically contiguous */
    bool pages_locked;          /* Pages are locked against remapping */
    bool translation_reliable;  /* Physical address translation is reliable */
    uint32_t first_page_phys;   /* Physical address of first 4KB page */
    uint32_t last_page_phys;    /* Physical address of last 4KB page */
    uint16_t page_count;        /* Number of 4KB pages spanned */
    bool v86_mode_detected;     /* V86/paging mode detected */
    bool dpmi_available;        /* DPMI services available */
    uint16_t lock_handle;       /* DPMI lock handle (if locked) */
} dma_check_result_t;

/**
 * @brief Memory region type for EMM386/QEMM awareness
 */
typedef enum {
    MEM_REGION_CONVENTIONAL,    /* 0-640KB */
    MEM_REGION_UMB,            /* Upper memory blocks */
    MEM_REGION_XMS,            /* Extended memory */
    MEM_REGION_EMS_WINDOW,     /* EMS page frame */
    MEM_REGION_UNKNOWN         /* Unknown or unmapped */
} memory_region_t;

/**
 * @brief Bounce buffer pool configuration
 */
typedef struct {
    void **buffers;            /* Array of pre-allocated buffers */
    uint32_t *phys_addrs;      /* Physical addresses */
    bool *in_use;              /* Usage flags */
    uint16_t buffer_count;     /* Number of buffers in pool */
    uint16_t buffer_size;      /* Size of each buffer */
    uint16_t free_count;       /* Number of free buffers */
    uint16_t alignment;        /* Buffer alignment */
    const char *pool_name;     /* Pool identifier */
} bounce_pool_t;

/* Core boundary checking functions - GPT-5 Enhanced */
/* Note: Return int instead of bool for C89 compatibility with mixed stdbool.h usage */
int dma_check_buffer_safety(void *buffer, size_t len, dma_check_result_t *result);
int dma_check_64kb_boundary_enhanced(uint32_t phys_addr, size_t len);
int dma_check_16mb_limit_enhanced(uint32_t phys_addr, size_t len);
int dma_check_alignment_enhanced(uint32_t phys_addr, uint16_t required_alignment);

/* Physical address calculation with EMM386/QEMM awareness */
uint32_t virt_to_phys_safe(void *virt_addr, memory_region_t *region_type);
int is_dma_safe_memory_region(void *buffer, size_t len);
memory_region_t detect_memory_region(void *buffer);

/* GPT-5 Critical: Physical memory contiguity and page locking */
/* Note: Return int instead of bool for C89 compatibility with mixed stdbool.h usage */
int verify_physical_contiguity(void *buffer, size_t len, dma_check_result_t *result);
int lock_pages_for_dma(void *buffer, size_t len, uint16_t *lock_handle);
void unlock_pages_for_dma(uint16_t lock_handle);
int detect_v86_paging_mode(void);
int dpmi_services_available(void);
uint32_t translate_linear_to_physical(uint32_t linear_addr);
int is_safe_for_direct_dma(void *buffer, size_t len);

/* Bounce buffer pool management - GPT-5 Separate TX/RX */
int dma_init_bounce_pools(void);
void dma_shutdown_bounce_pools(void);
bounce_pool_t* dma_get_tx_bounce_pool(void);
bounce_pool_t* dma_get_rx_bounce_pool(void);

/* Bounce buffer allocation/release */
void* dma_get_tx_bounce_buffer(size_t size);
void* dma_get_rx_bounce_buffer(size_t size);
void dma_release_tx_bounce_buffer(void *buffer);
void dma_release_rx_bounce_buffer(void *buffer);

/* Descriptor splitting for scatter-gather - GPT-5 Enhancement */
typedef struct {
    uint32_t phys_addr;        /* Physical address */
    uint16_t length;           /* Segment length */
    bool is_bounce;            /* Uses bounce buffer */
    void *bounce_ptr;          /* Bounce buffer pointer if used */
} dma_segment_t;

typedef struct {
    dma_segment_t segments[8]; /* Up to 8 segments */
    uint16_t segment_count;    /* Number of segments */
    uint32_t total_length;     /* Total buffer length */
    bool uses_bounce;          /* Any segment uses bounce */
    void *original_buffer;     /* Original buffer pointer */
} dma_sg_descriptor_t;

/* Scatter-gather operations */
dma_sg_descriptor_t* dma_create_sg_descriptor(void *buffer, size_t len, uint16_t max_segments);
void dma_free_sg_descriptor(dma_sg_descriptor_t *desc);
int dma_split_at_64k_boundary(void *buffer, size_t len, dma_sg_descriptor_t *desc);

/* Statistics and debugging */
typedef struct {
    uint32_t total_checks;             /* Total safety checks */
    uint32_t bounce_tx_used;           /* TX bounce buffers used */
    uint32_t bounce_rx_used;           /* RX bounce buffers used */
    uint32_t boundary_64k_violations;  /* 64KB boundary hits */
    uint32_t boundary_16m_violations;  /* 16MB limit hits */
    uint32_t isa_24bit_violations;     /* GPT-5 A+: ISA 24-bit limit hits */
    uint32_t alignment_violations;     /* Alignment errors */
    uint32_t splits_performed;        /* Buffer splits */
    uint32_t conventional_hits;        /* Conventional memory usage */
    uint32_t umb_rejections;           /* UMB memory rejections */
    uint32_t xms_rejections;           /* XMS memory rejections */
} dma_boundary_stats_t;

void dma_get_boundary_stats(dma_boundary_stats_t *stats);
void dma_print_boundary_stats(void);
void dma_reset_boundary_stats(void);

/* C89 helper functions (static, not inline) */

/**
 * @brief Fast 64KB boundary check without full structure
 * GPT-5 recommendation for performance-critical paths
 */
static bool dma_crosses_64k_fast(uint32_t phys_addr, size_t len) {
    return ((phys_addr & 0xFFFFUL) + len) > 0x10000UL;
}

/**
 * @brief Fast 16MB limit check
 * GPT-5 recommendation for ISA DMA validation
 */
static bool dma_exceeds_16m_fast(uint32_t phys_addr, size_t len) {
    return (phys_addr >= DMA_16MB_LIMIT) || ((phys_addr + len) > DMA_16MB_LIMIT);
}

/**
 * @brief Check if buffer needs bounce for ISA DMA
 */
static bool dma_needs_bounce_isa(uint32_t phys_addr, size_t len) {
    return dma_crosses_64k_fast(phys_addr, len) || dma_exceeds_16m_fast(phys_addr, len);
}

/**
 * @brief Safe physical address calculation
 * Handles segment:offset to linear conversion with overflow check
 */
static uint32_t seg_off_to_phys(uint16_t segment, uint16_t offset) {
    uint32_t linear = ((uint32_t)segment << 4) + offset;
    /* Check for wraparound in 16-bit segment arithmetic */
    if (linear < ((uint32_t)segment << 4)) {
        return 0xFFFFFFFFUL; /* Signal overflow */
    }
    return linear;
}

/**
 * @brief Convert far pointer to physical address
 */
static uint32_t far_ptr_to_phys(void far *ptr) {
    return seg_off_to_phys(FP_SEG(ptr), FP_OFF(ptr));
}

/* Pool management macros */
#define DMA_TX_POOL_SIZE        16          /* 16 TX bounce buffers */
#define DMA_RX_POOL_SIZE        16          /* 16 RX bounce buffers */
#define DMA_BOUNCE_BUFFER_SIZE  2048        /* 2KB per buffer */
#define DMA_POOL_ALIGNMENT      64          /* 64-byte alignment */

/* Safety validation macros - GPT-5 Enhanced */
#define DMA_VALIDATE_ISA_BUFFER(addr, len) \
    (!dma_crosses_64k_fast(addr, len) && !dma_exceeds_16m_fast(addr, len))

#define DMA_VALIDATE_CONVENTIONAL_ONLY(addr, len) \
    ((addr) < DMA_CONVENTIONAL_LIMIT && ((addr) + (len)) <= DMA_CONVENTIONAL_LIMIT)

#define DMA_REQUIRE_BOUNCE_ISA(addr, len) \
    (dma_needs_bounce_isa(addr, len) ? true : false)

/* Error codes */
#define DMA_ERROR_64K_BOUNDARY      -2001   /* 64KB boundary violation */
#define DMA_ERROR_16M_LIMIT         -2002   /* 16MB limit violation */
#define DMA_ERROR_ALIGNMENT         -2003   /* Alignment violation */
#define DMA_ERROR_NO_BOUNCE_BUFFER  -2004   /* No bounce buffer available */
#define DMA_ERROR_INVALID_REGION    -2005   /* Invalid memory region */
#define DMA_ERROR_POOL_EXHAUSTED    -2006   /* Bounce pool exhausted */

#ifdef __cplusplus
}
#endif

#endif /* DMA_BOUNDARY_H */
