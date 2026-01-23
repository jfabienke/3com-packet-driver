/**
 * @file far_copy_enhanced.h
 * @brief Enhanced far pointer copy routines for DOS real mode
 * 
 * Production-quality far pointer handling addressing GPT-5's 
 * suggestion for proper segment handling in DOS environment.
 */

#ifndef FAR_COPY_ENHANCED_H
#define FAR_COPY_ENHANCED_H

#include <stdint.h>
#include <stdbool.h>

/* Far pointer structure for explicit segment:offset handling */
typedef struct {
    uint16_t segment;
    uint16_t offset;
} far_ptr_t;

/* Copy operation types */
typedef enum {
    COPY_TYPE_NEAR_TO_NEAR = 0,     /* Both pointers in same segment */
    COPY_TYPE_NEAR_TO_FAR = 1,      /* Near source, far destination */
    COPY_TYPE_FAR_TO_NEAR = 2,      /* Far source, near destination */  
    COPY_TYPE_FAR_TO_FAR = 3,       /* Both pointers are far */
    COPY_TYPE_XMS_TO_CONV = 4,      /* XMS to conventional memory */
    COPY_TYPE_CONV_TO_XMS = 5,      /* Conventional to XMS memory */
    COPY_TYPE_SEGMENT_CROSS = 6     /* Copy crosses segment boundaries */
} copy_type_t;

/* Copy statistics and performance tracking */
struct far_copy_stats {
    uint32_t copies_performed;      /* Total copy operations */
    uint32_t near_to_near;         /* Same-segment copies */
    uint32_t near_to_far;          /* Near to far copies */
    uint32_t far_to_near;          /* Far to near copies */
    uint32_t far_to_far;           /* Far to far copies */
    uint32_t xms_operations;       /* XMS memory operations */
    uint32_t segment_crossings;    /* Segment boundary crossings */
    uint32_t alignment_adjustments; /* Alignment corrections */
    uint32_t copy_failures;        /* Failed copy operations */
    
    /* Performance metrics */
    uint32_t total_bytes_copied;   /* Total bytes transferred */
    uint16_t avg_copy_size;        /* Average copy size */
    uint16_t max_copy_size;        /* Maximum copy size */
    uint8_t rep_movsw_percentage;  /* Percentage using REP MOVSW */
    uint8_t byte_copy_percentage;  /* Percentage using byte copy */
    
    /* Error tracking */
    uint16_t segment_wrap_errors;  /* Segment wraparound errors */
    uint16_t null_pointer_errors;  /* NULL pointer access attempts */
    uint16_t xms_handle_errors;    /* XMS handle failures */
};

/* Function prototypes */

/**
 * Initialize enhanced far copy services
 */
int far_copy_enhanced_init(void);

/**
 * Enhanced far memory copy with automatic type detection
 */
int far_copy_enhanced(void *dst, const void *src, uint16_t size);

/**
 * Explicit far pointer copy with segment handling
 */
int far_copy_explicit(far_ptr_t dst, far_ptr_t src, uint16_t size);

/**
 * Copy with segment boundary checking
 */
int far_copy_safe(void *dst, const void *src, uint16_t size);

/**
 * High-performance aligned copy (requires 16-byte alignment)
 */
int far_copy_aligned(void *dst, const void *src, uint16_t size);

/**
 * XMS memory copy operations
 */
int far_copy_from_xms(void *dst, uint16_t xms_handle, uint32_t xms_offset, uint16_t size);
int far_copy_to_xms(uint16_t xms_handle, uint32_t xms_offset, const void *src, uint16_t size);

/**
 * Utility functions
 */
far_ptr_t make_far_ptr(void *ptr);
void *resolve_far_ptr(far_ptr_t fptr);
bool is_same_segment(const void *ptr1, const void *ptr2);
bool crosses_segment_boundary(const void *ptr, uint16_t size);
uint16_t get_segment_remaining(const void *ptr);

/**
 * Copy type detection
 */
copy_type_t detect_copy_type(const void *dst, const void *src, uint16_t size);
const char *copy_type_name(copy_type_t type);

/**
 * Statistics and diagnostics
 */
void far_copy_get_stats(struct far_copy_stats *stats);
int far_copy_health_check(void);
void far_copy_debug_print(void);

/**
 * Memory validation
 */
bool validate_pointer(const void *ptr, uint16_t size);
bool validate_far_pointer(far_ptr_t fptr, uint16_t size);

/* Low-level assembly routines */
extern void far_copy_rep_movsw(void *dst, const void *src, uint16_t words);
extern void far_copy_rep_movsb(void *dst, const void *src, uint16_t bytes);
extern void far_copy_segments(uint16_t dst_seg, uint16_t dst_off,
                             uint16_t src_seg, uint16_t src_off,
                             uint16_t size);

/* Convenience macros */

/* Create far pointer from near pointer */
#define MAKE_FAR_PTR(ptr) make_far_ptr(ptr)

/* Get segment from far pointer */
#define GET_SEGMENT(fptr) ((fptr).segment)

/* Get offset from far pointer */  
#define GET_OFFSET(fptr) ((fptr).offset)

/* Calculate linear address from far pointer */
#define FAR_TO_LINEAR(fptr) (((uint32_t)(fptr).segment << 4) + (fptr).offset)

/* Check if pointer is in conventional memory */
#define IS_CONVENTIONAL_MEM(ptr) (((uint32_t)(ptr)) < 0xA0000UL)

/* Check if pointer is in UMB range */
#define IS_UMB_MEM(ptr) (((uint32_t)(ptr)) >= 0xA0000UL && ((uint32_t)(ptr)) < 0x100000UL)

/* Optimal copy size thresholds based on CPU type */
#define OPTIMAL_COPY_THRESHOLD_286  32   /* 286: Prefer byte copies for small sizes */
#define OPTIMAL_COPY_THRESHOLD_386  16   /* 386: Word copies more efficient */
#define OPTIMAL_COPY_THRESHOLD_486  8    /* 486: Word copies even more efficient */
#define OPTIMAL_COPY_THRESHOLD_PENTIUM 4 /* Pentium: Almost always word copy */

/* Segment boundary constants */
#define SEGMENT_SIZE            65536    /* 64KB segment size */
#define PARAGRAPH_SIZE          16       /* 16-byte paragraph */
#define SEGMENT_MASK            0xFFFF   /* Segment offset mask */

/**
 * Integration with enhanced copy-break
 * 
 * These routines are called from copy_break_enhanced.c for:
 * - fast_packet_copy(): High-performance packet copying
 * - fast_far_copy(): Cross-segment packet copying
 * - Segment boundary detection and handling
 */

/* Performance-critical inline functions */

/**
 * Fast inline segment check
 */
static inline bool fast_same_segment_check(const void *ptr1, const void *ptr2)
{
    uint16_t seg1, seg2;
    
    __asm {
        mov ax, ds
        mov seg1, ax
        mov seg2, ax  ; Assume same data segment for near pointers
    }
    
    return seg1 == seg2;
}

/**
 * Fast segment boundary check
 */
static inline bool fast_boundary_check(const void *ptr, uint16_t size)
{
    uint16_t offset = (uint16_t)ptr;
    return (offset + size) < offset;  /* Overflow indicates boundary crossing */
}

#endif /* FAR_COPY_ENHANCED_H */