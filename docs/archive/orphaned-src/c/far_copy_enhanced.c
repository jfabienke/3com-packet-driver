/**
 * @file far_copy_enhanced.c
 * @brief Enhanced far pointer copy routines for DOS real mode
 * 
 * Production-quality implementation addressing GPT-5's recommendations
 * for proper segment handling and memory safety in DOS environment.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <dos.h>
#include "far_copy_enhanced.h"
#include "logging.h"
#include "xms_detect.h"

/* Global statistics */
static struct far_copy_stats far_stats;
static bool far_copy_initialized = false;
static uint8_t detected_cpu_type = 0;

/* CPU type constants for optimization */
#define CPU_TYPE_286        2
#define CPU_TYPE_386        3
#define CPU_TYPE_486        4
#define CPU_TYPE_PENTIUM    5

/**
 * Initialize enhanced far copy services
 */
int far_copy_enhanced_init(void)
{
    if (far_copy_initialized) {
        return 0;
    }
    
    /* Clear statistics */
    memset(&far_stats, 0, sizeof(far_stats));
    
    /* Detect CPU type for optimization */
    detected_cpu_type = detect_cpu_type();
    
    LOG_INFO("Far copy enhanced services initialized");
    LOG_INFO("  CPU type: %u", detected_cpu_type);
    LOG_INFO("  Segment size: %u bytes", SEGMENT_SIZE);
    
    far_copy_initialized = true;
    return 0;
}

/**
 * Detect CPU type for copy optimization
 */
static uint8_t detect_cpu_type(void)
{
    uint8_t cpu_type = CPU_TYPE_286;  /* Conservative default */
    
    __asm {
        ; Simple CPU detection - not comprehensive but sufficient
        ; for copy optimization decisions
        
        ; Test for 386+
        pushf
        pushf
        pop     ax
        mov     bx, ax
        xor     ax, 4000h       ; Toggle AC bit
        push    ax
        popf
        pushf
        pop     ax
        popf
        
        cmp     ax, bx
        je      cpu_286
        
        ; 386 or higher detected
        mov     cpu_type, CPU_TYPE_386
        
        ; Could add more sophisticated detection for 486/Pentium
        ; but 386+ is sufficient for our copy optimizations
        
    cpu_286:
    }
    
    return cpu_type;
}

/**
 * Create far pointer from near pointer
 */
far_ptr_t make_far_ptr(void *ptr)
{
    far_ptr_t fptr;
    
    __asm {
        mov     ax, ds
        mov     fptr.segment, ax
        mov     bx, ptr
        mov     fptr.offset, bx
    }
    
    return fptr;
}

/**
 * Resolve far pointer to near pointer (if in current segment)
 */
void *resolve_far_ptr(far_ptr_t fptr)
{
    uint16_t current_seg;
    
    __asm {
        mov     ax, ds
        mov     current_seg, ax
    }
    
    if (fptr.segment == current_seg) {
        return (void *)fptr.offset;
    }
    
    /* Cannot resolve to near pointer - different segment */
    return NULL;
}

/**
 * Check if two pointers are in the same segment
 */
bool is_same_segment(const void *ptr1, const void *ptr2)
{
    /* For near pointers in DOS, they're always in the same data segment */
    /* This function is mainly for explicit far pointer comparisons */
    
    far_stats.copies_performed++;  /* Count as a check operation */
    return true;  /* Near pointers are always same segment */
}

/**
 * Check if copy crosses segment boundary
 */
bool crosses_segment_boundary(const void *ptr, uint16_t size)
{
    uint16_t offset = (uint16_t)ptr;
    uint32_t end_addr = (uint32_t)offset + size;
    
    /* Check for offset wraparound (indicates segment boundary crossing) */
    if (end_addr > SEGMENT_MASK) {
        far_stats.segment_crossings++;
        return true;
    }
    
    return false;
}

/**
 * Get remaining bytes in current segment
 */
uint16_t get_segment_remaining(const void *ptr)
{
    uint16_t offset = (uint16_t)ptr;
    return SEGMENT_SIZE - offset;
}

/**
 * Detect copy operation type
 */
copy_type_t detect_copy_type(const void *dst, const void *src, uint16_t size)
{
    /* For DOS real mode with near pointers, most copies are NEAR_TO_NEAR */
    /* Check for segment boundary crossings */
    
    if (crosses_segment_boundary(dst, size) || crosses_segment_boundary(src, size)) {
        return COPY_TYPE_SEGMENT_CROSS;
    }
    
    /* Check for XMS memory ranges (would need XMS handle context) */
    /* For now, assume conventional memory operations */
    
    return COPY_TYPE_NEAR_TO_NEAR;
}

/**
 * Validate pointer and size
 */
bool validate_pointer(const void *ptr, uint16_t size)
{
    if (ptr == NULL) {
        far_stats.null_pointer_errors++;
        return false;
    }
    
    /* Check for segment boundary issues */
    if (crosses_segment_boundary(ptr, size)) {
        far_stats.segment_wrap_errors++;
        LOG_WARNING("Pointer 0x%04X crosses segment boundary (size %u)", 
                   (uint16_t)ptr, size);
        return false;
    }
    
    return true;
}

/**
 * Enhanced far memory copy with automatic optimization
 */
int far_copy_enhanced(void *dst, const void *src, uint16_t size)
{
    if (!far_copy_initialized) {
        if (far_copy_enhanced_init() != 0) {
            return -1;
        }
    }
    
    /* Validate parameters */
    if (!validate_pointer(dst, size) || !validate_pointer(src, size)) {
        far_stats.copy_failures++;
        return -1;
    }
    
    if (size == 0) {
        return 0;  /* Nothing to copy */
    }
    
    /* Update statistics */
    far_stats.copies_performed++;
    far_stats.total_bytes_copied += size;
    
    if (size > far_stats.max_copy_size) {
        far_stats.max_copy_size = size;
    }
    
    /* Detect copy type and optimize accordingly */
    copy_type_t copy_type = detect_copy_type(dst, src, size);
    
    switch (copy_type) {
        case COPY_TYPE_NEAR_TO_NEAR:
            far_stats.near_to_near++;
            return far_copy_optimized_near(dst, src, size);
            
        case COPY_TYPE_SEGMENT_CROSS:
            far_stats.segment_crossings++;
            return far_copy_with_boundary_handling(dst, src, size);
            
        default:
            /* Fallback to standard copy */
            memcpy(dst, src, size);
            return 0;
    }
}

/**
 * Optimized near-to-near copy based on CPU type and size
 */
static int far_copy_optimized_near(void *dst, const void *src, uint16_t size)
{
    /* Choose copy method based on size and CPU type */
    uint16_t threshold;
    
    switch (detected_cpu_type) {
        case CPU_TYPE_286:
            threshold = OPTIMAL_COPY_THRESHOLD_286;
            break;
        case CPU_TYPE_386:
            threshold = OPTIMAL_COPY_THRESHOLD_386;
            break;
        case CPU_TYPE_486:
            threshold = OPTIMAL_COPY_THRESHOLD_486;
            break;
        default:
            threshold = OPTIMAL_COPY_THRESHOLD_PENTIUM;
            break;
    }
    
    if (size >= threshold && (size & 1) == 0 && 
        ((uint16_t)dst & 1) == 0 && ((uint16_t)src & 1) == 0) {
        /* Use word copy for aligned, larger transfers */
        far_copy_rep_movsw(dst, src, size >> 1);
        far_stats.rep_movsw_percentage++;
    } else {
        /* Use byte copy for small or unaligned transfers */
        far_copy_rep_movsb(dst, src, size);
        far_stats.byte_copy_percentage++;
    }
    
    return 0;
}

/**
 * Copy with segment boundary handling
 */
static int far_copy_with_boundary_handling(void *dst, const void *src, uint16_t size)
{
    uint16_t src_remaining = get_segment_remaining(src);
    uint16_t dst_remaining = get_segment_remaining(dst);
    uint16_t chunk_size = size;
    
    /* Limit chunk size to stay within segment boundaries */
    if (chunk_size > src_remaining) {
        chunk_size = src_remaining;
    }
    if (chunk_size > dst_remaining) {
        chunk_size = dst_remaining;
    }
    
    /* If we can do it in one chunk, use optimized copy */
    if (chunk_size == size) {
        return far_copy_optimized_near(dst, src, size);
    }
    
    /* Multi-chunk copy required */
    LOG_WARNING("Multi-chunk copy required: %u bytes in chunks of %u", 
               size, chunk_size);
    
    uint16_t copied = 0;
    while (copied < size) {
        uint16_t this_chunk = size - copied;
        if (this_chunk > chunk_size) {
            this_chunk = chunk_size;
        }
        
        /* Copy this chunk */
        memcpy((uint8_t *)dst + copied, (uint8_t *)src + copied, this_chunk);
        copied += this_chunk;
        
        /* Recalculate remaining space for next chunk */
        if (copied < size) {
            src_remaining = get_segment_remaining((uint8_t *)src + copied);
            dst_remaining = get_segment_remaining((uint8_t *)dst + copied);
            chunk_size = size - copied;
            
            if (chunk_size > src_remaining) {
                chunk_size = src_remaining;
            }
            if (chunk_size > dst_remaining) {
                chunk_size = dst_remaining;
            }
        }
    }
    
    return 0;
}

/**
 * Explicit far pointer copy
 */
int far_copy_explicit(far_ptr_t dst, far_ptr_t src, uint16_t size)
{
    if (!far_copy_initialized) {
        if (far_copy_enhanced_init() != 0) {
            return -1;
        }
    }
    
    /* Update statistics */
    far_stats.copies_performed++;
    far_stats.far_to_far++;
    far_stats.total_bytes_copied += size;
    
    /* Use assembly routine for explicit segment handling */
    far_copy_segments(dst.segment, dst.offset, src.segment, src.offset, size);
    
    return 0;
}

/**
 * High-performance aligned copy
 */
int far_copy_aligned(void *dst, const void *src, uint16_t size)
{
    if (!validate_pointer(dst, size) || !validate_pointer(src, size)) {
        return -1;
    }
    
    /* Verify alignment */
    if (((uint16_t)dst & 15) != 0 || ((uint16_t)src & 15) != 0) {
        LOG_WARNING("Unaligned pointers in aligned copy: dst=0x%04X src=0x%04X", 
                   (uint16_t)dst, (uint16_t)src);
        far_stats.alignment_adjustments++;
        return far_copy_enhanced(dst, src, size);  /* Fall back to normal copy */
    }
    
    /* Use optimized aligned copy */
    far_stats.copies_performed++;
    far_stats.total_bytes_copied += size;
    
    /* For 16-byte aligned data, use DWORD operations if 386+ */
    if (detected_cpu_type >= CPU_TYPE_386 && (size & 3) == 0) {
        /* Assembly routine would use REP MOVSD */
        far_copy_rep_movsw(dst, src, size >> 1);  /* Use word copy for now */
    } else {
        far_copy_rep_movsw(dst, src, size >> 1);
    }
    
    return 0;
}

/**
 * XMS memory copy operations
 */
int far_copy_from_xms(void *dst, uint16_t xms_handle, uint32_t xms_offset, uint16_t size)
{
    if (!validate_pointer(dst, size)) {
        return -1;
    }
    
    /* Check XMS availability */
    if (!xms_is_available()) {
        LOG_ERROR("XMS not available for far copy operation");
        return -1;
    }
    
    /* Use XMS copy services */
    xms_copy_result_t result = xms_copy_from_handle(dst, xms_handle, xms_offset, size);
    
    if (result.success) {
        far_stats.copies_performed++;
        far_stats.xms_operations++;
        far_stats.total_bytes_copied += size;
        return 0;
    } else {
        far_stats.copy_failures++;
        far_stats.xms_handle_errors++;
        LOG_ERROR("XMS copy failed: handle %u, offset %lu, size %u", 
                 xms_handle, xms_offset, size);
        return -1;
    }
}

int far_copy_to_xms(uint16_t xms_handle, uint32_t xms_offset, const void *src, uint16_t size)
{
    if (!validate_pointer(src, size)) {
        return -1;
    }
    
    /* Check XMS availability */
    if (!xms_is_available()) {
        LOG_ERROR("XMS not available for far copy operation");
        return -1;
    }
    
    /* Use XMS copy services */
    xms_copy_result_t result = xms_copy_to_handle(xms_handle, xms_offset, src, size);
    
    if (result.success) {
        far_stats.copies_performed++;
        far_stats.xms_operations++;
        far_stats.total_bytes_copied += size;
        return 0;
    } else {
        far_stats.copy_failures++;
        far_stats.xms_handle_errors++;
        LOG_ERROR("XMS copy failed: handle %u, offset %lu, size %u", 
                 xms_handle, xms_offset, size);
        return -1;
    }
}

/**
 * Get copy type name for debugging
 */
const char *copy_type_name(copy_type_t type)
{
    switch (type) {
        case COPY_TYPE_NEAR_TO_NEAR:  return "NEAR_TO_NEAR";
        case COPY_TYPE_NEAR_TO_FAR:   return "NEAR_TO_FAR";
        case COPY_TYPE_FAR_TO_NEAR:   return "FAR_TO_NEAR";
        case COPY_TYPE_FAR_TO_FAR:    return "FAR_TO_FAR";
        case COPY_TYPE_XMS_TO_CONV:   return "XMS_TO_CONV";
        case COPY_TYPE_CONV_TO_XMS:   return "CONV_TO_XMS";
        case COPY_TYPE_SEGMENT_CROSS: return "SEGMENT_CROSS";
        default: return "UNKNOWN";
    }
}

/**
 * Get statistics
 */
void far_copy_get_stats(struct far_copy_stats *stats)
{
    if (!stats) return;
    
    *stats = far_stats;
    
    /* Calculate percentages */
    if (far_stats.copies_performed > 0) {
        stats->rep_movsw_percentage = 
            (far_stats.rep_movsw_percentage * 100) / far_stats.copies_performed;
        stats->byte_copy_percentage = 
            (far_stats.byte_copy_percentage * 100) / far_stats.copies_performed;
    }
    
    /* Calculate average copy size */
    if (far_stats.copies_performed > 0) {
        stats->avg_copy_size = 
            (uint16_t)(far_stats.total_bytes_copied / far_stats.copies_performed);
    }
}

/**
 * Health check
 */
int far_copy_health_check(void)
{
    int health_score = 0;
    
    /* Check error rates */
    if (far_stats.copies_performed > 0) {
        uint32_t error_rate = (far_stats.copy_failures * 100) / far_stats.copies_performed;
        
        if (error_rate > 5) {
            health_score -= 3;  /* High error rate */
        } else if (error_rate > 1) {
            health_score -= 1;  /* Moderate error rate */
        }
        
        /* Check for excessive segment crossings */
        uint32_t crossing_rate = (far_stats.segment_crossings * 100) / far_stats.copies_performed;
        if (crossing_rate > 10) {
            health_score -= 1;  /* Many segment crossings - suboptimal */
        }
    }
    
    /* Check for pointer validation errors */
    if (far_stats.null_pointer_errors > 0) {
        health_score -= 2;
    }
    
    if (far_stats.segment_wrap_errors > 0) {
        health_score -= 1;
    }
    
    return health_score;
}

/**
 * Debug print statistics
 */
void far_copy_debug_print(void)
{
    LOG_INFO("=== Far Copy Enhanced Statistics ===");
    LOG_INFO("Total copies: %lu", far_stats.copies_performed);
    LOG_INFO("Total bytes: %lu", far_stats.total_bytes_copied);
    LOG_INFO("Average size: %u bytes", 
             far_stats.copies_performed > 0 ? 
             (uint16_t)(far_stats.total_bytes_copied / far_stats.copies_performed) : 0);
    
    LOG_INFO("Copy types:");
    LOG_INFO("  Near-to-near: %lu", far_stats.near_to_near);
    LOG_INFO("  XMS operations: %lu", far_stats.xms_operations);
    LOG_INFO("  Segment crossings: %lu", far_stats.segment_crossings);
    
    LOG_INFO("Errors:");
    LOG_INFO("  Copy failures: %lu", far_stats.copy_failures);
    LOG_INFO("  NULL pointer errors: %u", far_stats.null_pointer_errors);
    LOG_INFO("  Segment wrap errors: %u", far_stats.segment_wrap_errors);
    LOG_INFO("  XMS handle errors: %u", far_stats.xms_handle_errors);
}

/* Forward declarations for static functions */
static int far_copy_optimized_near(void *dst, const void *src, uint16_t size);
static int far_copy_with_boundary_handling(void *dst, const void *src, uint16_t size);
static uint8_t detect_cpu_type(void);