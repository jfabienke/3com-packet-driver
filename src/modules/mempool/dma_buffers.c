/**
 * @file dma_buffers.c
 * @brief DMA-Safe Buffer Allocator with 64KB Boundary Compliance
 * 
 * Agent 11 - Memory Management - Day 3-4 Deliverable
 * 
 * Provides DMA-safe buffer allocation with guaranteed 64KB boundary compliance,
 * proper alignment for descriptor rings, and ISA bus-master constraints.
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../../include/common.h"
#include "../../../include/memory_api.h"
#include "../../../include/logging.h"
#include "../../../include/cpu_detect.h"
#include "xms_service.h"
#include <dos.h>
#include <string.h>

/* DMA buffer allocation constants */
#define DMA_64KB_BOUNDARY       0x10000    /* 64KB boundary */
#define DMA_16MB_LIMIT          0x1000000  /* 16MB limit for ISA */
#define DMA_MIN_ALIGNMENT       16         /* Minimum alignment */
#define DMA_DESCRIPTOR_ALIGNMENT 32        /* Descriptor ring alignment */
#define DMA_MAX_BUFFERS         64         /* Maximum tracked buffers */
#define DMA_GUARD_PATTERN       0xDEADBEEF /* Guard pattern for corruption detection */

/* DMA buffer types */
typedef enum {
    DMA_BUFFER_TYPE_PACKET,     /* Packet data buffer */
    DMA_BUFFER_TYPE_DESCRIPTOR, /* Descriptor ring */
    DMA_BUFFER_TYPE_STATUS,     /* Status buffer */
    DMA_BUFFER_TYPE_BOUNCE      /* Bounce buffer for ISA DMA */
} dma_buffer_type_t;

/* DMA buffer descriptor */
typedef struct {
    void* virtual_address;      /* Virtual address */
    uint32_t physical_address;  /* Physical address (same as virtual in DOS) */
    size_t size;                /* Buffer size */
    size_t alignment;           /* Alignment requirement */
    dma_buffer_type_t type;     /* Buffer type */
    dma_device_type_t device_type; /* Device type */
    uint8_t device_id;          /* Device ID */
    bool in_use;                /* Buffer is allocated */
    bool locked;                /* Buffer is locked for DMA */
    uint16_t xms_handle;        /* XMS handle (if XMS allocation) */
    uint32_t guard_before;      /* Guard pattern before buffer */
    uint32_t guard_after;       /* Guard pattern after buffer */
    uint32_t allocation_time;   /* Allocation timestamp */
} dma_buffer_desc_t;

/* DMA allocator state */
typedef struct {
    bool initialized;
    bool xms_preferred;
    uint32_t total_allocated;
    uint32_t peak_usage;
    uint16_t buffer_count;
    uint32_t allocation_failures;
    uint32_t boundary_violations;
    uint32_t corruption_detected;
    dma_buffer_desc_t buffers[DMA_MAX_BUFFERS];
} dma_allocator_state_t;

static dma_allocator_state_t g_dma_allocator = {0};

/* Forward declarations */
static int dma_find_free_slot(void);
static bool dma_check_64kb_boundary(void* address, size_t size);
static bool dma_check_16mb_limit(void* address, size_t size);
static void* dma_alloc_conventional(size_t size, size_t alignment);
static void* dma_alloc_xms(size_t size, size_t alignment, uint16_t* xms_handle_out);
static bool dma_validate_buffer(int slot);
static void dma_setup_guard_patterns(int slot);
static bool dma_check_guard_patterns(int slot);
static size_t dma_calculate_aligned_size(size_t size, size_t alignment);
static uint32_t dma_get_timestamp(void);

/**
 * @brief Initialize DMA buffer allocator
 * @return 0 on success, negative on error
 */
int dma_buffer_allocator_init(void) {
    if (g_dma_allocator.initialized) {
        return 0;
    }
    
    log_info("DMA Allocator: Initializing DMA-safe buffer allocator");
    
    /* Clear allocator state */
    memset(&g_dma_allocator, 0, sizeof(dma_allocator_state_t));
    
    /* Initialize all buffer slots as free */
    for (int i = 0; i < DMA_MAX_BUFFERS; i++) {
        g_dma_allocator.buffers[i].in_use = false;
        g_dma_allocator.buffers[i].locked = false;
        g_dma_allocator.buffers[i].xms_handle = 0;
    }
    
    /* Check if XMS is available and preferred for DMA buffers */
    g_dma_allocator.xms_preferred = xms_service_is_available();
    
    if (g_dma_allocator.xms_preferred) {
        log_info("DMA Allocator: XMS available, will use for DMA buffers");
    } else {
        log_info("DMA Allocator: XMS not available, using conventional memory");
    }
    
    g_dma_allocator.initialized = true;
    log_info("DMA Allocator: Initialization completed");
    
    return 0;
}

/**
 * @brief Cleanup DMA buffer allocator
 */
void dma_buffer_allocator_cleanup(void) {
    if (!g_dma_allocator.initialized) {
        return;
    }
    
    log_info("DMA Allocator: Cleaning up DMA buffer allocator");
    
    /* Free all allocated buffers */
    for (int i = 0; i < DMA_MAX_BUFFERS; i++) {
        if (g_dma_allocator.buffers[i].in_use) {
            log_warning("DMA Allocator: Freeing unreleased buffer slot %d", i);
            dma_buffer_free(g_dma_allocator.buffers[i].virtual_address);
        }
    }
    
    /* Print final statistics */
    log_info("DMA Allocator: Final stats - Total: %lu, Peak: %lu, Failures: %lu",
             g_dma_allocator.total_allocated,
             g_dma_allocator.peak_usage,
             g_dma_allocator.allocation_failures);
    
    if (g_dma_allocator.boundary_violations > 0) {
        log_warning("DMA Allocator: %lu boundary violations detected", 
                   g_dma_allocator.boundary_violations);
    }
    
    if (g_dma_allocator.corruption_detected > 0) {
        log_error("DMA Allocator: %lu buffer corruption instances detected", 
                 g_dma_allocator.corruption_detected);
    }
    
    g_dma_allocator.initialized = false;
    log_info("DMA Allocator: Cleanup completed");
}

/**
 * @brief Allocate DMA-safe buffer with 64KB boundary compliance
 * @param size Buffer size in bytes
 * @param alignment Required alignment (must be power of 2)
 * @param device_type Type of DMA device
 * @param device_id Device identifier
 * @return Pointer to DMA-safe buffer, NULL on failure
 */
void* dma_buffer_alloc(size_t size, size_t alignment, dma_device_type_t device_type, uint8_t device_id) {
    if (!g_dma_allocator.initialized) {
        log_error("DMA Allocator: Not initialized");
        return NULL;
    }
    
    /* Validate parameters */
    if (size == 0 || size > 0xFFFF) {
        log_error("DMA Allocator: Invalid size %u", size);
        g_dma_allocator.allocation_failures++;
        return NULL;
    }
    
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        log_error("DMA Allocator: Invalid alignment %u (must be power of 2)", alignment);
        g_dma_allocator.allocation_failures++;
        return NULL;
    }
    
    /* Ensure minimum alignment for DMA */
    if (alignment < DMA_MIN_ALIGNMENT) {
        alignment = DMA_MIN_ALIGNMENT;
    }
    
    /* For descriptor rings, use larger alignment */
    if (device_type == DMA_DEVICE_NETWORK && size >= 64) {
        if (alignment < DMA_DESCRIPTOR_ALIGNMENT) {
            alignment = DMA_DESCRIPTOR_ALIGNMENT;
        }
    }
    
    /* Find free buffer slot */
    int slot = dma_find_free_slot();
    if (slot < 0) {
        log_error("DMA Allocator: No free buffer slots");
        g_dma_allocator.allocation_failures++;
        return NULL;
    }
    
    /* Calculate total size including alignment padding and guard bytes */
    size_t aligned_size = dma_calculate_aligned_size(size, alignment);
    size_t total_size = aligned_size + 8; /* Guard bytes before and after */
    
    /* Attempt allocation */
    void* buffer = NULL;
    uint16_t xms_handle = 0;
    
    /* Try XMS first if preferred and available */
    if (g_dma_allocator.xms_preferred) {
        buffer = dma_alloc_xms(total_size, alignment, &xms_handle);
        if (buffer) {
            log_debug("DMA Allocator: Allocated %u bytes from XMS at %p", size, buffer);
        }
    }
    
    /* Fall back to conventional memory */
    if (!buffer) {
        buffer = dma_alloc_conventional(total_size, alignment);
        if (buffer) {
            log_debug("DMA Allocator: Allocated %u bytes from conventional memory at %p", size, buffer);
        }
    }
    
    if (!buffer) {
        log_error("DMA Allocator: Failed to allocate %u bytes", size);
        g_dma_allocator.allocation_failures++;
        return NULL;
    }
    
    /* Verify 64KB boundary compliance */
    uint8_t* aligned_buffer = (uint8_t*)buffer + 4; /* Skip guard bytes */
    aligned_buffer = (uint8_t*)ALIGN_POINTER(aligned_buffer, alignment);
    
    if (!dma_check_64kb_boundary(aligned_buffer, size)) {
        log_error("DMA Allocator: Buffer violates 64KB boundary at %p + %u", aligned_buffer, size);
        g_dma_allocator.boundary_violations++;
        
        /* Free the problematic buffer */
        if (xms_handle != 0) {
            xms_service_free(xms_handle);
        } else {
            memory_free(buffer);
        }
        
        g_dma_allocator.allocation_failures++;
        return NULL;
    }
    
    /* Verify ISA 16MB limit if applicable */
    if (device_type == DMA_DEVICE_NETWORK || device_type == DMA_DEVICE_STORAGE) {
        if (!dma_check_16mb_limit(aligned_buffer, size)) {
            log_warning("DMA Allocator: Buffer above 16MB limit at %p (ISA compatibility issue)", aligned_buffer);
        }
    }
    
    /* Set up buffer descriptor */
    g_dma_allocator.buffers[slot].virtual_address = aligned_buffer;
    g_dma_allocator.buffers[slot].physical_address = (uint32_t)aligned_buffer; /* DOS real mode */
    g_dma_allocator.buffers[slot].size = size;
    g_dma_allocator.buffers[slot].alignment = alignment;
    g_dma_allocator.buffers[slot].type = DMA_BUFFER_TYPE_PACKET; /* Default */
    g_dma_allocator.buffers[slot].device_type = device_type;
    g_dma_allocator.buffers[slot].device_id = device_id;
    g_dma_allocator.buffers[slot].in_use = true;
    g_dma_allocator.buffers[slot].locked = false;
    g_dma_allocator.buffers[slot].xms_handle = xms_handle;
    g_dma_allocator.buffers[slot].allocation_time = dma_get_timestamp();
    
    /* Set up guard patterns for corruption detection */
    dma_setup_guard_patterns(slot);
    
    /* Update statistics */
    g_dma_allocator.buffer_count++;
    g_dma_allocator.total_allocated += size;
    
    if (g_dma_allocator.total_allocated > g_dma_allocator.peak_usage) {
        g_dma_allocator.peak_usage = g_dma_allocator.total_allocated;
    }
    
    log_debug("DMA Allocator: Allocated %u bytes at %p (slot %d, alignment %u, device %u:%u)",
             size, aligned_buffer, slot, alignment, device_type, device_id);
    
    return aligned_buffer;
}

/**
 * @brief Free DMA buffer
 * @param ptr Pointer to DMA buffer
 * @return true on success, false on error
 */
bool dma_buffer_free(void* ptr) {
    if (!ptr || !g_dma_allocator.initialized) {
        return false;
    }
    
    /* Find buffer in tracking table */
    int slot = -1;
    for (int i = 0; i < DMA_MAX_BUFFERS; i++) {
        if (g_dma_allocator.buffers[i].in_use && 
            g_dma_allocator.buffers[i].virtual_address == ptr) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        log_error("DMA Allocator: Buffer %p not found in tracking table", ptr);
        return false;
    }
    
    /* Check guard patterns for corruption */
    if (!dma_check_guard_patterns(slot)) {
        log_error("DMA Allocator: Buffer corruption detected in slot %d", slot);
        g_dma_allocator.corruption_detected++;
    }
    
    /* Ensure buffer is not locked for DMA */
    if (g_dma_allocator.buffers[slot].locked) {
        log_warning("DMA Allocator: Freeing locked DMA buffer at %p", ptr);
    }
    
    size_t size = g_dma_allocator.buffers[slot].size;
    uint16_t xms_handle = g_dma_allocator.buffers[slot].xms_handle;
    
    /* Free the underlying memory */
    void* base_buffer = (uint8_t*)ptr - 4 - g_dma_allocator.buffers[slot].alignment; /* Account for guards and alignment */
    
    if (xms_handle != 0) {
        /* Free XMS memory */
        void* linear_addr;
        if (xms_service_lock(xms_handle, &linear_addr) == 0) {
            xms_service_unlock(xms_handle);
        }
        xms_service_free(xms_handle);
        log_debug("DMA Allocator: Freed XMS buffer slot %d, handle %04X", slot, xms_handle);
    } else {
        /* Free conventional memory */
        memory_free(base_buffer);
        log_debug("DMA Allocator: Freed conventional buffer slot %d", slot);
    }
    
    /* Clear buffer descriptor */
    memset(&g_dma_allocator.buffers[slot], 0, sizeof(dma_buffer_desc_t));
    g_dma_allocator.buffers[slot].in_use = false;
    
    /* Update statistics */
    g_dma_allocator.buffer_count--;
    g_dma_allocator.total_allocated -= size;
    
    log_debug("DMA Allocator: Freed %u bytes from slot %d", size, slot);
    return true;
}

/**
 * @brief Lock DMA buffer for hardware access
 * @param ptr Pointer to DMA buffer
 * @return Physical address for DMA, 0 on error
 */
uint32_t dma_buffer_lock(void* ptr) {
    if (!ptr || !g_dma_allocator.initialized) {
        return 0;
    }
    
    /* Find buffer in tracking table */
    int slot = -1;
    for (int i = 0; i < DMA_MAX_BUFFERS; i++) {
        if (g_dma_allocator.buffers[i].in_use && 
            g_dma_allocator.buffers[i].virtual_address == ptr) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        log_error("DMA Allocator: Buffer %p not found for locking", ptr);
        return 0;
    }
    
    /* Validate buffer before locking */
    if (!dma_validate_buffer(slot)) {
        log_error("DMA Allocator: Buffer validation failed for slot %d", slot);
        return 0;
    }
    
    /* Mark as locked */
    g_dma_allocator.buffers[slot].locked = true;
    
    /* In DOS real mode, physical address equals virtual address */
    uint32_t physical_addr = g_dma_allocator.buffers[slot].physical_address;
    
    log_debug("DMA Allocator: Locked buffer at %p, physical %08lX", ptr, physical_addr);
    return physical_addr;
}

/**
 * @brief Unlock DMA buffer after hardware access
 * @param ptr Pointer to DMA buffer
 * @return true on success, false on error
 */
bool dma_buffer_unlock(void* ptr) {
    if (!ptr || !g_dma_allocator.initialized) {
        return false;
    }
    
    /* Find buffer in tracking table */
    int slot = -1;
    for (int i = 0; i < DMA_MAX_BUFFERS; i++) {
        if (g_dma_allocator.buffers[i].in_use && 
            g_dma_allocator.buffers[i].virtual_address == ptr) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        log_error("DMA Allocator: Buffer %p not found for unlocking", ptr);
        return false;
    }
    
    if (!g_dma_allocator.buffers[slot].locked) {
        log_warning("DMA Allocator: Buffer %p not locked", ptr);
        return true; /* Not an error */
    }
    
    /* Check for corruption after DMA operation */
    if (!dma_check_guard_patterns(slot)) {
        log_error("DMA Allocator: Buffer corruption detected after DMA operation in slot %d", slot);
        g_dma_allocator.corruption_detected++;
    }
    
    /* Mark as unlocked */
    g_dma_allocator.buffers[slot].locked = false;
    
    log_debug("DMA Allocator: Unlocked buffer at %p", ptr);
    return true;
}

/**
 * @brief Get DMA allocator statistics
 * @param stats Pointer to receive statistics
 * @return true on success, false on error
 */
bool dma_buffer_get_stats(memory_stats_t* stats) {
    if (!stats || !g_dma_allocator.initialized) {
        return false;
    }
    
    /* Clear stats first */
    memset(stats, 0, sizeof(memory_stats_t));
    
    /* Fill in DMA-specific statistics */
    stats->current_usage = g_dma_allocator.total_allocated;
    stats->peak_usage = g_dma_allocator.peak_usage;
    stats->total_allocations = g_dma_allocator.buffer_count;  /* Current, not total */
    
    /* Count active buffers by type */
    uint16_t xms_buffers = 0;
    uint16_t conventional_buffers = 0;
    
    for (int i = 0; i < DMA_MAX_BUFFERS; i++) {
        if (g_dma_allocator.buffers[i].in_use) {
            if (g_dma_allocator.buffers[i].xms_handle != 0) {
                xms_buffers++;
            } else {
                conventional_buffers++;
            }
        }
    }
    
    stats->xms_handles_used = xms_buffers;
    stats->fragmentation_pct = 0; /* Would need more complex calculation */
    
    return true;
}

/**
 * @brief Validate all DMA buffers for corruption
 * @return Number of corrupted buffers found
 */
int dma_buffer_validate_all(void) {
    if (!g_dma_allocator.initialized) {
        return -1;
    }
    
    int corrupted = 0;
    
    for (int i = 0; i < DMA_MAX_BUFFERS; i++) {
        if (g_dma_allocator.buffers[i].in_use) {
            if (!dma_validate_buffer(i)) {
                log_error("DMA Allocator: Validation failed for buffer slot %d", i);
                corrupted++;
            }
            
            if (!dma_check_guard_patterns(i)) {
                log_error("DMA Allocator: Guard pattern corruption in slot %d", i);
                corrupted++;
            }
        }
    }
    
    if (corrupted > 0) {
        log_error("DMA Allocator: Found %d corrupted buffers", corrupted);
        g_dma_allocator.corruption_detected += corrupted;
    }
    
    return corrupted;
}

/* === Internal Helper Functions === */

/**
 * @brief Find free buffer slot
 * @return Slot index, -1 if none available
 */
static int dma_find_free_slot(void) {
    for (int i = 0; i < DMA_MAX_BUFFERS; i++) {
        if (!g_dma_allocator.buffers[i].in_use) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Check if buffer violates 64KB boundary
 * @param address Buffer address
 * @param size Buffer size
 * @return true if safe, false if boundary violation
 */
static bool dma_check_64kb_boundary(void* address, size_t size) {
    uint32_t start_addr = (uint32_t)address;
    uint32_t end_addr = start_addr + size - 1;
    
    /* Check if start and end are in the same 64KB segment */
    return ((start_addr & 0xFFFF0000) == (end_addr & 0xFFFF0000));
}

/**
 * @brief Check if buffer is below 16MB limit (ISA constraint)
 * @param address Buffer address
 * @param size Buffer size
 * @return true if within limit, false if above
 */
static bool dma_check_16mb_limit(void* address, size_t size) {
    uint32_t end_addr = (uint32_t)address + size;
    return (end_addr <= DMA_16MB_LIMIT);
}

/**
 * @brief Allocate DMA buffer from conventional memory
 * @param size Buffer size
 * @param alignment Alignment requirement
 * @return Buffer pointer, NULL on failure
 */
static void* dma_alloc_conventional(size_t size, size_t alignment) {
    /* Use existing memory allocation with DMA flag */
    void* buffer = memory_alloc_aligned(size, alignment, MEM_TYPE_DMA_BUFFER);
    
    if (buffer && dma_check_64kb_boundary(buffer, size)) {
        return buffer;
    }
    
    /* If boundary violation, try again with larger allocation */
    if (buffer) {
        memory_free(buffer);
    }
    
    /* Allocate larger buffer and align within 64KB boundary */
    size_t large_size = size + DMA_64KB_BOUNDARY;
    void* large_buffer = memory_alloc(large_size, MEM_TYPE_DMA_BUFFER, MEM_FLAG_ALIGNED);
    
    if (!large_buffer) {
        return NULL;
    }
    
    /* Find aligned address within 64KB boundary */
    uint32_t addr = (uint32_t)large_buffer;
    uint32_t aligned_addr = ALIGN_UP(addr, alignment);
    uint32_t boundary_start = aligned_addr & 0xFFFF0000;
    uint32_t boundary_end = boundary_start + DMA_64KB_BOUNDARY;
    
    if (aligned_addr + size <= boundary_end) {
        /* Store original buffer for later freeing - this is simplified */
        return (void*)aligned_addr;
    }
    
    /* Find next suitable address in the same 64KB boundary */
    aligned_addr = boundary_start + alignment;
    if (aligned_addr + size <= boundary_end) {
        return (void*)aligned_addr;
    }
    
    memory_free(large_buffer);
    return NULL;
}

/**
 * @brief Allocate DMA buffer from XMS memory
 * @param size Buffer size
 * @param alignment Alignment requirement
 * @param xms_handle_out Output XMS handle
 * @return Buffer pointer, NULL on failure
 */
static void* dma_alloc_xms(size_t size, size_t alignment, uint16_t* xms_handle_out) {
    *xms_handle_out = 0;
    
    /* XMS allocates in KB units */
    size_t size_kb = (size + 1023) / 1024;
    if (size_kb == 0) size_kb = 1;
    
    /* Allocate XMS memory */
    uint16_t handle;
    if (xms_service_alloc(size_kb, &handle) != 0) {
        return NULL;
    }
    
    /* Lock to get linear address */
    void* linear_addr;
    if (xms_service_lock(handle, &linear_addr) != 0) {
        xms_service_free(handle);
        return NULL;
    }
    
    /* Check 64KB boundary compliance */
    uint32_t addr = (uint32_t)linear_addr;
    uint32_t aligned_addr = ALIGN_UP(addr, alignment);
    
    if (!dma_check_64kb_boundary((void*)aligned_addr, size)) {
        xms_service_unlock(handle);
        xms_service_free(handle);
        return NULL;
    }
    
    *xms_handle_out = handle;
    return (void*)aligned_addr;
}

/**
 * @brief Validate DMA buffer integrity
 * @param slot Buffer slot index
 * @return true if valid, false if corrupted
 */
static bool dma_validate_buffer(int slot) {
    dma_buffer_desc_t* buf = &g_dma_allocator.buffers[slot];
    
    /* Check basic validity */
    if (!buf->in_use || !buf->virtual_address) {
        return false;
    }
    
    /* Verify 64KB boundary compliance */
    if (!dma_check_64kb_boundary(buf->virtual_address, buf->size)) {
        return false;
    }
    
    /* Check alignment */
    if (((uint32_t)buf->virtual_address % buf->alignment) != 0) {
        return false;
    }
    
    return true;
}

/**
 * @brief Set up guard patterns around buffer
 * @param slot Buffer slot index
 */
static void dma_setup_guard_patterns(int slot) {
    dma_buffer_desc_t* buf = &g_dma_allocator.buffers[slot];
    
    /* Place guard patterns before and after buffer */
    uint32_t* guard_before = (uint32_t*)((uint8_t*)buf->virtual_address - 4);
    uint32_t* guard_after = (uint32_t*)((uint8_t*)buf->virtual_address + buf->size);
    
    *guard_before = DMA_GUARD_PATTERN;
    *guard_after = DMA_GUARD_PATTERN;
    
    buf->guard_before = DMA_GUARD_PATTERN;
    buf->guard_after = DMA_GUARD_PATTERN;
}

/**
 * @brief Check guard patterns for corruption
 * @param slot Buffer slot index
 * @return true if intact, false if corrupted
 */
static bool dma_check_guard_patterns(int slot) {
    dma_buffer_desc_t* buf = &g_dma_allocator.buffers[slot];
    
    uint32_t* guard_before = (uint32_t*)((uint8_t*)buf->virtual_address - 4);
    uint32_t* guard_after = (uint32_t*)((uint8_t*)buf->virtual_address + buf->size);
    
    if (*guard_before != DMA_GUARD_PATTERN) {
        log_error("DMA Allocator: Guard before corruption in slot %d: %08lX", slot, *guard_before);
        return false;
    }
    
    if (*guard_after != DMA_GUARD_PATTERN) {
        log_error("DMA Allocator: Guard after corruption in slot %d: %08lX", slot, *guard_after);
        return false;
    }
    
    return true;
}

/**
 * @brief Calculate aligned size including padding
 * @param size Original size
 * @param alignment Alignment requirement
 * @return Aligned size
 */
static size_t dma_calculate_aligned_size(size_t size, size_t alignment) {
    return ALIGN_SIZE(size, alignment);
}

/**
 * @brief Get timestamp for allocation tracking
 * @return Timestamp value
 */
static uint32_t dma_get_timestamp(void) {
    /* Simple timestamp - could use PIT or RTC */
    static uint32_t counter = 0;
    return ++counter;
}

/**
 * @brief Print DMA allocator status and active buffers
 */
void dma_buffer_print_status(void) {
    if (!g_dma_allocator.initialized) {
        log_info("DMA Allocator: Not initialized");
        return;
    }
    
    log_info("=== DMA Buffer Allocator Status ===");
    log_info("Total Allocated: %lu bytes", g_dma_allocator.total_allocated);
    log_info("Peak Usage: %lu bytes", g_dma_allocator.peak_usage);
    log_info("Active Buffers: %u / %u", g_dma_allocator.buffer_count, DMA_MAX_BUFFERS);
    log_info("Allocation Failures: %lu", g_dma_allocator.allocation_failures);
    log_info("Boundary Violations: %lu", g_dma_allocator.boundary_violations);
    log_info("Corruption Detected: %lu", g_dma_allocator.corruption_detected);
    log_info("XMS Preferred: %s", g_dma_allocator.xms_preferred ? "Yes" : "No");
    
    /* List active buffers */
    log_info("Active DMA Buffers:");
    for (int i = 0; i < DMA_MAX_BUFFERS; i++) {
        if (g_dma_allocator.buffers[i].in_use) {
            dma_buffer_desc_t* buf = &g_dma_allocator.buffers[i];
            log_info("  Slot %d: %p, %u bytes, align %u, device %u:%u, %s%s",
                    i, buf->virtual_address, buf->size, buf->alignment,
                    buf->device_type, buf->device_id,
                    buf->locked ? "LOCKED " : "",
                    buf->xms_handle ? "XMS" : "CONV");
        }
    }
}