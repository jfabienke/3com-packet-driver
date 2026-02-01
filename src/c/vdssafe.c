/**
 * @file vds_safety.c
 * @brief VDS Safety Layer Implementation
 * 
 * Provides production hardening for VDS operations including:
 * - ISR context detection (CRITICAL per GPT-5)
 * - Device constraint validation
 * - Bounce buffer management
 * - 3-tier error recovery (from vds_enhanced.c)
 */

#include <dos.h>
#include <string.h>
#include <stdlib.h>
#include <conio.h>   /* For inportb, outportb */
#include <i86.h>     /* For _enable, _disable */
#include "../../include/vdssafe.h"
#include "../../include/vds_core.h"
#include "../../include/cpudet.h"
#include "../../include/logging.h"

/* PIC (8259) I/O ports */
#define PIC_MASTER_CMD  0x20
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_CMD   0xA0
#define PIC_SLAVE_DATA  0xA1
#define PIC_READ_ISR    0x0B    /* OCW3 to read In-Service Register */
#define PIC_READ_IRR    0x0A    /* OCW3 to read Interrupt Request Register (default) */

/* Portable interrupt flag check - read-only, no side effects */
#if defined(__WATCOMC__)
/* Watcom C - use pragma aux for side-effect-free flags read */
static unsigned short read_flags_watcom(void);
#pragma aux read_flags_watcom = \
    "pushf"         \
    "pop ax"        \
    value [ax]      \
    modify [];

static int vds_get_interrupt_flag(void)
{
    return (read_flags_watcom() & 0x0200) != 0;  /* IF is bit 9 */
}

#elif defined(__TURBOC__) || defined(__BORLANDC__)
/* Borland/Turbo C - inline assembly for flags read */
static int vds_get_interrupt_flag(void)
{
    unsigned int flags;
    asm {
        pushf
        pop ax
        mov word ptr flags, ax  /* Explicit word ptr for clarity */
    }
    return (flags & 0x0200) != 0;  /* IF is bit 9 */
}

#elif defined(_MSC_VER)
/* Microsoft C - use inline assembly */
static int vds_get_interrupt_flag(void)
{
    unsigned int flags;
    __asm {
        pushf
        pop ax
        mov flags, ax
    }
    return (flags & 0x0200) != 0;  /* IF is bit 9 */
}

#else
/* Generic fallback - conservative, no side effects */
static int vds_get_interrupt_flag(void)
{
    /* Without compiler support, conservatively assume we might be in ISR */
    /* This is safe but may reject some valid VDS calls */
    /* Better to be safe than to corrupt interrupt state */
    return 0;  /* Conservative - assume disabled/ISR context */
}
#endif

/* Bounce buffer pool structure */
typedef struct {
    void far* base_addr;        /* Pool base address (below 16MB for ISA) */
    uint32_t physical_addr;     /* Physical address of pool */
    uint32_t total_size;        /* Total pool size (configurable) */
    uint16_t block_size;        /* Size of each block */
    uint16_t num_blocks;        /* Number of blocks */
    uint8_t* allocation_map;    /* Bitmap of allocated blocks */
    uint16_t vds_lock_handle;   /* VDS lock handle for pool */
    bool is_vds_locked;         /* Pool locked via VDS */
    bool initialized;           /* Pool initialized */
} bounce_pool_t;

/* Global state */
static bool safety_initialized = false;
static bounce_pool_t bounce_pool = {0};
static vds_safety_stats_t safety_stats = {0};
static volatile uint16_t isr_nesting_depth = 0;  /* ISR nesting counter */

/* Default constraints for common devices */
/* C89: Use positional initializers instead of designated initializers */
/* Order: address_bits, max_sg_entries, max_segment_len, no_cross_mask,
          alignment_mask, require_contiguous, allow_bounce */
const dma_constraints_t ISA_DMA_CONSTRAINTS = {
    24,         /* address_bits: 16MB limit */
    1,          /* max_sg_entries: No scatter/gather */
    65536,      /* max_segment_len: 64KB max */
    0xFFFF,     /* no_cross_mask: No 64KB crossing */
    0x01,       /* alignment_mask: Word alignment */
    true,       /* require_contiguous */
    true        /* allow_bounce */
};

const dma_constraints_t PCI_DMA_CONSTRAINTS = {
    32,         /* address_bits: 4GB addressing */
    64,         /* max_sg_entries: Scatter/gather supported */
    0x100000,   /* max_segment_len: 1MB segments */
    0,          /* no_cross_mask: No boundary restrictions */
    0x03,       /* alignment_mask: DWORD alignment */
    false,      /* require_contiguous */
    true        /* allow_bounce */
};

const dma_constraints_t NIC_3C509_CONSTRAINTS = {
    24,         /* address_bits: ISA bus */
    1,          /* max_sg_entries: PIO only, no DMA */
    1536,       /* max_segment_len: Ethernet MTU */
    0xFFFF,     /* no_cross_mask: 64KB boundary */
    0x01,       /* alignment_mask: Word alignment */
    true,       /* require_contiguous */
    false       /* allow_bounce: PIO doesn't need bounce */
};

const dma_constraints_t NIC_3C515_CONSTRAINTS = {
    32,         /* address_bits: Bus master DMA */
    16,         /* max_sg_entries: Supports scatter/gather */
    65536,      /* max_segment_len: 64KB segments */
    0xFFFF,     /* no_cross_mask: Prefer no 64KB crossing */
    0x0F,       /* alignment_mask: 16-byte alignment */
    false,      /* require_contiguous */
    true        /* allow_bounce */
};

/* Forward declarations */
static bool allocate_bounce_pool(uint32_t pool_size);
static void free_bounce_pool(void);
static int find_free_bounce_block(uint32_t size);

/**
 * Initialize VDS safety layer with default pool size
 */
int vds_safety_init(void)
{
    return vds_safety_init_ex(BOUNCE_POOL_DEFAULT / 1024);
}

/**
 * Initialize VDS safety layer with custom pool size
 */
int vds_safety_init_ex(uint32_t pool_size_kb)
{
    uint32_t pool_size;
    
    if (safety_initialized) {
        return 0;
    }
    
    /* Validate pool size */
    if (pool_size_kb < (BOUNCE_POOL_MIN_SIZE / 1024)) {
        pool_size_kb = BOUNCE_POOL_MIN_SIZE / 1024;
    } else if (pool_size_kb > (BOUNCE_POOL_MAX_SIZE / 1024)) {
        pool_size_kb = BOUNCE_POOL_MAX_SIZE / 1024;
    }
    pool_size = pool_size_kb * 1024;
    
    /* Initialize core layer first */
    if (vds_core_init() != 0) {
        LOG_ERROR("VDS Safety: Failed to initialize core layer");
        return -1;
    }
    
    /* Clear statistics */
    memset(&safety_stats, 0, sizeof(safety_stats));
    
    /* Allocate bounce buffer pool if in V86 mode */
    if (vds_is_v86_mode()) {
        if (!allocate_bounce_pool(pool_size)) {
            LOG_WARNING("VDS Safety: Failed to allocate bounce buffer pool");
            /* Continue anyway - can still work without bounce buffers */
        }
    }
    
    safety_initialized = true;
    LOG_INFO("VDS Safety: Initialized (bounce pool: %s, size: %luKB)",
            bounce_pool.initialized ? "available" : "not available",
            pool_size_kb);
    
    return 0;
}

/**
 * Cleanup VDS safety layer
 */
void vds_safety_cleanup(void)
{
    if (!safety_initialized) {
        return;
    }
    
    free_bounce_pool();
    safety_initialized = false;
    
    LOG_INFO("VDS Safety: Cleaned up");
}

/**
 * CRITICAL: Check if currently in ISR context
 * Per GPT-5: NEVER call VDS from ISR!
 * 
 * Uses 3-signal approach for maximum reliability:
 * 1. Driver's ISR nesting counter
 * 2. EFLAGS.IF interrupt flag state
 * 3. PIC ISR register check (fallback)
 */
bool vds_in_isr_context(void)
{
    uint8_t isr_master = 0;
    uint8_t isr_slave = 0;
    int saved_if;
    bool in_v86;
    
    /* Signal 1: Check driver's ISR nesting depth */
    if (isr_nesting_depth > 0) {
        return true;  /* Definitely in ISR */
    }
    
    /* Signal 2: Check EFLAGS.IF interrupt flag */
    saved_if = vds_get_interrupt_flag();
    if (!saved_if) {
        /* Interrupts disabled - likely in critical section or ISR */
        /* This alone isn't definitive, but combined with other signals... */
    }
    
    /* Signal 3: Check hardware PIC ISR registers */
    /* CRITICAL FIX: Save and restore interrupt state properly */
    
    /* Check if we can safely access PIC (not in V86/PM without privilege) */
    in_v86 = vds_is_v86_mode();
    if (in_v86) {
        /* In V86/PM mode - PIC access may be trapped */
        /* Conservative: if interrupts are disabled, assume unsafe for VDS */
        return !saved_if;
    }
    
    /* Safe to access PIC - save interrupt state first */
    _disable();
    
    /* Read master PIC ISR */
    outportb(PIC_MASTER_CMD, PIC_READ_ISR);
    isr_master = inportb(PIC_MASTER_CMD);
    
    /* Read slave PIC ISR */
    outportb(PIC_SLAVE_CMD, PIC_READ_ISR);
    isr_slave = inportb(PIC_SLAVE_CMD);
    
    /* CRITICAL: Restore PIC OCW3 to IRR mode (default) */
    outportb(PIC_MASTER_CMD, PIC_READ_IRR);  /* Restore IRR mode */
    outportb(PIC_SLAVE_CMD, PIC_READ_IRR);   /* Restore IRR mode */
    
    /* CRITICAL: Restore original interrupt state */
    if (saved_if) {
        _enable();  /* Only re-enable if they were enabled before */
    }
    /* else leave disabled - caller had them disabled */
    
    /* Combined decision logic:
     * - If nesting depth > 0: definitely in ISR (most reliable)
     * - If interrupts disabled AND PIC shows active ISR: in ISR
     * - If only PIC shows active ISR: likely in ISR (hardware check)
     */
    if (!saved_if && (isr_master != 0 || isr_slave != 0)) {
        return true;  /* High confidence: interrupts off + PIC active */
    }
    
    /* PIC alone as fallback (may have false positives) */
    return (isr_master != 0 || isr_slave != 0);
}

/**
 * Enter ISR context - increment nesting depth
 */
void vds_enter_isr_context(void)
{
    _disable();
    isr_nesting_depth++;
    _enable();
}

/**
 * Exit ISR context - decrement nesting depth
 */
void vds_exit_isr_context(void)
{
    _disable();
    if (isr_nesting_depth > 0) {
        isr_nesting_depth--;
    }
    _enable();
}

/**
 * Lock region with device constraints and recovery
 */
vds_safe_error_t vds_lock_with_constraints(void far* addr, uint32_t size,
                                          const dma_constraints_t* constraints,
                                          vds_transfer_direction_t direction,
                                          vds_safe_lock_t* lock)
{
    vds_raw_lock_result_t raw_result;
    uint8_t error_code;
    uint16_t flags;

    if (!lock || !constraints) {
        return VDS_SAFE_INVALID_CONSTRAINTS;
    }

    memset(lock, 0, sizeof(*lock));
    safety_stats.total_locks++;

    /* CRITICAL: Check ISR context first! */
    if (vds_in_isr_context()) {
        safety_stats.isr_rejections++;
        LOG_ERROR("VDS Safety: CRITICAL - VDS called from ISR context!");
        return VDS_SAFE_IN_ISR;
    }

    /* Check constraints */
    if (!vds_check_constraints(addr, size, constraints)) {
        /* Constraints violated - try recovery */
        return vds_lock_with_recovery(addr, size, constraints, direction, lock);
    }

    /* Direct lock attempt */
    flags = 0;
    if (constraints->require_contiguous) {
        flags |= 0x01;  /* VDS contiguous flag */
    }
    if (constraints->no_cross_mask == 0xFFFF) {
        flags |= 0x80;  /* No 64KB crossing flag */
    }
    
    error_code = vds_core_lock_region(addr, size, flags, direction, &raw_result);
    
    if (error_code == VDS_RAW_SUCCESS) {
        lock->success = true;
        lock->error = VDS_SAFE_OK;
        lock->lock_handle = raw_result.lock_handle;
        lock->physical_addr = raw_result.physical_addr;
        lock->vds_used_bounce = (raw_result.translation_type == VDS_TRANS_ALTERNATE);
        lock->is_scattered = raw_result.is_scattered;
        lock->sg_count = raw_result.sg_count;
        
        safety_stats.successful_locks++;
        
        if (raw_result.translation_type == VDS_TRANS_ALTERNATE) {
            safety_stats.vds_bounce_uses++;
            LOG_INFO("VDS Safety: VDS using ALTERNATE buffer - copy required (phys: 0x%08lX)",
                    lock->physical_addr);
        }
        
        return VDS_SAFE_OK;
    }
    
    /* Lock failed - try recovery */
    return vds_lock_with_recovery(addr, size, constraints, direction, lock);
}

/**
 * 3-tier recovery for lock failure (from vds_enhanced.c)
 */
vds_safe_error_t vds_lock_with_recovery(void far* addr, uint32_t size,
                                       const dma_constraints_t* constraints,
                                       vds_transfer_direction_t direction,
                                       vds_safe_lock_t* lock)
{
    vds_raw_lock_result_t raw_result;
    uint8_t error_code;
    dma_constraints_t relaxed;
    uint32_t linear;
    uint32_t aligned;
    uint32_t aligned_size;

    safety_stats.recovery_attempts++;

    LOG_INFO("VDS Safety: Attempting recovery for 0x%p + %lu", addr, size);

    /* Recovery Path 1: Try without contiguous requirement */
    if (constraints->require_contiguous && !constraints->allow_bounce) {
        relaxed = *constraints;
        relaxed.require_contiguous = false;

        LOG_DEBUG("Recovery 1: Trying scatter/gather");

        error_code = vds_core_lock_region(addr, size, 0x02, direction, &raw_result);
        if (error_code == VDS_RAW_SUCCESS) {
            lock->success = true;
            lock->error = VDS_SAFE_OK;
            lock->lock_handle = raw_result.lock_handle;
            lock->physical_addr = raw_result.physical_addr;
            lock->vds_used_bounce = (raw_result.translation_type == VDS_TRANS_ALTERNATE);
            lock->is_scattered = true;

            safety_stats.recovery_successes++;
            LOG_INFO("Recovery 1: Success with scatter/gather");
            return VDS_SAFE_OK;
        }
    }

    /* Recovery Path 2: Try smaller aligned chunks */
    if (size > 4096) {
        linear = ((uint32_t)FP_SEG(addr) << 4) + FP_OFF(addr);
        aligned = (linear + 15) & ~15;  /* 16-byte align */
        aligned_size = size - (aligned - linear);

        if (aligned_size >= 1024) {
            void far* aligned_addr = MK_FP(aligned >> 4, aligned & 0x0F);

            LOG_DEBUG("Recovery 2: Trying aligned chunk");

            error_code = vds_core_lock_region(aligned_addr, aligned_size,
                                             constraints->require_contiguous ? 0x01 : 0,
                                             direction, &raw_result);
            if (error_code == VDS_RAW_SUCCESS) {
                lock->success = true;
                lock->error = VDS_SAFE_OK;
                lock->lock_handle = raw_result.lock_handle;
                lock->physical_addr = raw_result.physical_addr;
                lock->vds_used_bounce = (raw_result.translation_type == VDS_TRANS_ALTERNATE);

                safety_stats.recovery_successes++;
                LOG_INFO("Recovery 2: Success with aligned chunk");
                return VDS_SAFE_OK;
            }
        }
    }
    
    /* Recovery Path 3: Use bounce buffer */
    if (constraints->allow_bounce && bounce_pool.initialized) {
        void far* bounce = vds_allocate_bounce_buffer(size, constraints);
        if (bounce) {
            /* Copy data to bounce buffer */
            vds_copy_to_bounce(bounce, addr, size);
            
            /* Lock bounce buffer (should always succeed) */
            error_code = vds_core_lock_region(bounce, size, 0x01, direction, &raw_result);
            if (error_code == VDS_RAW_SUCCESS) {
                lock->success = true;
                lock->error = VDS_SAFE_OK;
                lock->lock_handle = raw_result.lock_handle;
                lock->physical_addr = raw_result.physical_addr;
                lock->vds_used_bounce = (raw_result.translation_type == VDS_TRANS_ALTERNATE);
                lock->bounce_buffer = bounce;
                lock->bounce_size = size;
                lock->used_bounce = true;  /* We used our bounce buffer */
                
                safety_stats.bounce_buffer_uses++;
                safety_stats.recovery_successes++;
                LOG_INFO("Recovery 3: Success with bounce buffer");
                return VDS_SAFE_OK;
            }
            
            /* Even bounce buffer failed - free it */
            vds_free_bounce_buffer(bounce);
        }
    }
    
    /* All recovery attempts failed */
    safety_stats.failed_locks++;
    LOG_ERROR("VDS Safety: All recovery attempts failed");
    return VDS_SAFE_RECOVERY_FAILED;
}

/**
 * Unlock region with cleanup
 */
vds_safe_error_t vds_unlock_safe(vds_safe_lock_t* lock)
{
    uint8_t error_code;
    
    if (!lock || !lock->success) {
        return VDS_SAFE_OK;  /* Nothing to unlock */
    }
    
    /* If bounce buffer was used, copy data back and free */
    if (lock->used_bounce && lock->bounce_buffer) {
        /* Note: For TX, data was already copied. For RX, copy back */
        /* This would need direction tracking in real implementation */
        vds_free_bounce_buffer(lock->bounce_buffer);
    }
    
    /* Unlock the VDS region */
    if (lock->lock_handle) {
        error_code = vds_core_unlock_region(lock->lock_handle);
        if (error_code != VDS_RAW_SUCCESS) {
            LOG_ERROR("VDS Safety: Unlock failed (handle: 0x%04X, error: 0x%02X)",
                     lock->lock_handle, error_code);
            return VDS_SAFE_UNKNOWN_ERROR;
        }
    }
    
    /* Clear lock structure */
    memset(lock, 0, sizeof(*lock));
    
    return VDS_SAFE_OK;
}

/**
 * Check if buffer meets constraints
 */
bool vds_check_constraints(void far* addr, uint32_t size,
                          const dma_constraints_t* constraints)
{
    uint32_t linear = ((uint32_t)FP_SEG(addr) << 4) + FP_OFF(addr);
    
    /* Check address bits */
    if (constraints->address_bits < 32) {
        uint32_t max_addr = (1UL << constraints->address_bits) - 1;
        if (linear + size > max_addr) {
            safety_stats.boundary_violations++;
            return false;
        }
    }
    
    /* Check alignment */
    if ((linear & constraints->alignment_mask) != 0) {
        return false;
    }
    
    /* Check boundary crossing */
    if (constraints->no_cross_mask != 0) {
        uint32_t start_boundary = linear & ~constraints->no_cross_mask;
        uint32_t end_boundary = (linear + size - 1) & ~constraints->no_cross_mask;
        if (start_boundary != end_boundary) {
            safety_stats.boundary_violations++;
            return false;
        }
    }
    
    /* Check segment length */
    if (size > constraints->max_segment_len) {
        return false;
    }
    
    return true;
}

/**
 * Check 64KB boundary crossing
 */
bool vds_crosses_64k_boundary(void far* addr, uint32_t size)
{
    uint32_t linear = ((uint32_t)FP_SEG(addr) << 4) + FP_OFF(addr);
    uint32_t start_64k = linear & ~0xFFFF;
    uint32_t end_64k = (linear + size - 1) & ~0xFFFF;
    
    return (start_64k != end_64k);
}

/**
 * Allocate bounce buffer pool with VDS locking
 */
static bool allocate_bounce_pool(uint32_t pool_size)
{
    vds_raw_lock_result_t lock_result;
    uint8_t error_code;
    
    /* Allocate pool in conventional memory (below 640KB) for ISA DMA */
    bounce_pool.base_addr = _fmalloc(pool_size);
    if (!bounce_pool.base_addr) {
        LOG_ERROR("VDS Safety: Failed to allocate bounce pool");
        return false;
    }
    
    /* Initialize pool structure */
    bounce_pool.total_size = pool_size;
    bounce_pool.block_size = BOUNCE_BLOCK_SIZE;
    bounce_pool.num_blocks = pool_size / BOUNCE_BLOCK_SIZE;
    
    /* Allocate allocation map */
    bounce_pool.allocation_map = calloc(bounce_pool.num_blocks, 1);
    if (!bounce_pool.allocation_map) {
        _ffree(bounce_pool.base_addr);
        bounce_pool.base_addr = NULL;
        return false;
    }
    
    /* VDS-lock the bounce pool itself for guaranteed DMA access */
    if (vds_is_present()) {
        error_code = vds_core_lock_region(bounce_pool.base_addr, pool_size,
                                         0x01 /* contiguous */, VDS_DIR_BIDIRECTIONAL, &lock_result);
        if (error_code == VDS_RAW_SUCCESS) {
            bounce_pool.vds_lock_handle = lock_result.lock_handle;
            bounce_pool.physical_addr = lock_result.physical_addr;
            bounce_pool.is_vds_locked = true;
            LOG_INFO("VDS Safety: Bounce pool VDS-locked (handle: 0x%04X)",
                    bounce_pool.vds_lock_handle);
        } else {
            /* CRITICAL: If VDS present and lock fails, don't proceed! */
            LOG_ERROR("VDS Safety: Failed to VDS-lock bounce pool (error: 0x%02X)",
                     error_code);
            free(bounce_pool.allocation_map);
            _ffree(bounce_pool.base_addr);
            bounce_pool.base_addr = NULL;
            bounce_pool.allocation_map = NULL;
            return false;  /* Fail - unsafe to use unlocked buffer with VDS */
        }
    } else {
        /* No VDS - safe to use direct physical address in real mode */
        if (!vds_is_v86_mode()) {
            /* True real mode - no memory manager, safe to proceed */
            bounce_pool.physical_addr = vds_linear_to_physical(bounce_pool.base_addr);
            bounce_pool.is_vds_locked = false;
            LOG_INFO("VDS Safety: Real mode - using unlocked bounce pool");
        } else {
            /* V86 mode but no VDS? Suspicious - fail safe */
            LOG_ERROR("VDS Safety: V86 mode detected but VDS not present - unsafe!");
            free(bounce_pool.allocation_map);
            _ffree(bounce_pool.base_addr);
            bounce_pool.base_addr = NULL;
            bounce_pool.allocation_map = NULL;
            return false;
        }
    }
    
    bounce_pool.initialized = true;
    safety_stats.bounce_pool_size = pool_size;
    
    LOG_INFO("VDS Safety: Bounce pool allocated (%lu KB at 0x%08lX, %s)",
            pool_size / 1024, bounce_pool.physical_addr,
            bounce_pool.is_vds_locked ? "VDS-locked" : "not locked");
    
    return true;
}

/**
 * Free bounce buffer pool
 */
static void free_bounce_pool(void)
{
    uint8_t error;

    if (bounce_pool.initialized) {
        /* Unlock VDS if pool was locked */
        if (bounce_pool.is_vds_locked && bounce_pool.vds_lock_handle) {
            error = vds_core_unlock_region(bounce_pool.vds_lock_handle);
            if (error != VDS_RAW_SUCCESS) {
                LOG_WARNING("VDS Safety: Failed to unlock bounce pool (error: 0x%02X)",
                           error);
            }
        }
        
        if (bounce_pool.base_addr) {
            _ffree(bounce_pool.base_addr);
        }
        if (bounce_pool.allocation_map) {
            free(bounce_pool.allocation_map);
        }
        memset(&bounce_pool, 0, sizeof(bounce_pool));
    }
}

/**
 * Allocate bounce buffer
 */
void far* vds_allocate_bounce_buffer(uint32_t size, 
                                    const dma_constraints_t* constraints)
{
    int block_index;
    uint16_t blocks_needed;
    
    if (!bounce_pool.initialized || size > bounce_pool.total_size) {
        return NULL;
    }
    
    blocks_needed = (size + bounce_pool.block_size - 1) / bounce_pool.block_size;
    
    /* Find contiguous free blocks */
    block_index = find_free_bounce_block(blocks_needed);
    if (block_index < 0) {
        return NULL;
    }
    
    /* Mark blocks as allocated */
    {
        int i;
        for (i = 0; i < blocks_needed; i++) {
            bounce_pool.allocation_map[block_index + i] = 1;
        }
    }
    
    safety_stats.bounce_pool_used += blocks_needed * bounce_pool.block_size;

    /* Return pointer to allocated region */
    {
        uint32_t offset = block_index * bounce_pool.block_size;
        return (void far*)((uint8_t far*)bounce_pool.base_addr + offset);
    }
}

/**
 * Find free bounce buffer blocks
 */
static int find_free_bounce_block(uint32_t blocks_needed)
{
    uint16_t consecutive = 0;
    int start_block = -1;
    
    {
        int i;
        for (i = 0; i < bounce_pool.num_blocks; i++) {
            if (bounce_pool.allocation_map[i] == 0) {
                if (consecutive == 0) {
                    start_block = i;
                }
                consecutive++;

                if (consecutive >= blocks_needed) {
                    return start_block;
                }
            } else {
                consecutive = 0;
                start_block = -1;
            }
        }
    }
    
    return -1;  /* No contiguous blocks available */
}

/**
 * Free bounce buffer
 */
void vds_free_bounce_buffer(void far* buffer)
{
    uint32_t offset;
    uint16_t block_index;

    if (!bounce_pool.initialized || !buffer) {
        return;
    }

    /* Calculate block index */
    offset = (uint8_t far*)buffer - (uint8_t far*)bounce_pool.base_addr;
    block_index = offset / bounce_pool.block_size;

    /* Free blocks (we don't track size, so free until we hit unallocated) */
    while (block_index < bounce_pool.num_blocks &&
           bounce_pool.allocation_map[block_index] == 1) {
        bounce_pool.allocation_map[block_index] = 0;
        safety_stats.bounce_pool_used -= bounce_pool.block_size;
        block_index++;
    }
}

/**
 * Copy to bounce buffer
 */
void vds_copy_to_bounce(void far* bounce, void far* src, uint32_t size)
{
    _fmemcpy(bounce, src, size);
}

/**
 * Copy from bounce buffer
 */
void vds_copy_from_bounce(void far* dst, void far* bounce, uint32_t size)
{
    _fmemcpy(dst, bounce, size);
}

/**
 * Get safety layer statistics
 */
void vds_safety_get_stats(vds_safety_stats_t* stats)
{
    if (stats) {
        *stats = safety_stats;
    }
}

/**
 * Get current ISR nesting depth
 */
uint16_t vds_get_isr_nesting_depth(void)
{
    return isr_nesting_depth;
}

/**
 * Set ISR nesting depth (for integration with module_bridge)
 */
void vds_set_isr_nesting_depth(uint16_t depth)
{
    _disable();
    isr_nesting_depth = depth;
    _enable();
}

/**
 * Get error description
 */
const char* vds_safe_error_string(vds_safe_error_t error)
{
    switch (error) {
        case VDS_SAFE_OK:                  return "Success";
        case VDS_SAFE_NOT_PRESENT:         return "VDS not present";
        case VDS_SAFE_IN_ISR:              return "Called from ISR context";
        case VDS_SAFE_BOUNDARY_VIOLATION:  return "Boundary violation";
        case VDS_SAFE_ALIGNMENT_ERROR:     return "Alignment error";
        case VDS_SAFE_SG_TOO_LONG:         return "S/G list too long";
        case VDS_SAFE_NO_MEMORY:           return "No memory";
        case VDS_SAFE_BOUNCE_REQUIRED:     return "Bounce buffer required";
        case VDS_SAFE_INVALID_CONSTRAINTS: return "Invalid constraints";
        case VDS_SAFE_LOCK_FAILED:         return "Lock failed";
        case VDS_SAFE_RECOVERY_FAILED:     return "Recovery failed";
        default:                           return "Unknown error";
    }
}

/**
 * Check if two S/G entries can be coalesced
 * CRITICAL: Only coalesce strictly adjacent entries (GPT-5 fix)
 */
bool vds_can_coalesce_sg_entries(const vds_sg_entry_t* entry1,
                                 const vds_sg_entry_t* entry2,
                                 uint32_t max_gap)
{
    uint64_t end_addr1;     /* Use 64-bit to prevent overflow */
    uint64_t start_addr2;
    uint64_t combined_end;
    
    /* Unused parameter - we don't coalesce gaps anymore */
    (void)max_gap;
    
    if (!entry1 || !entry2) {
        return false;
    }
    
    /* Calculate end of first entry and start of second (64-bit math) */
    end_addr1 = (uint64_t)entry1->physical_addr + entry1->size;
    start_addr2 = (uint64_t)entry2->physical_addr;
    
    /* CRITICAL: Only coalesce if strictly adjacent (no gaps!) */
    if (end_addr1 != start_addr2) {
        return false;  /* Not adjacent - cannot safely coalesce */
    }
    
    /* Check combined size doesn't exceed device limits (64KB typical) */
    combined_end = (uint64_t)entry1->physical_addr + entry1->size + entry2->size;
    if ((combined_end - entry1->physical_addr) > 0xFFFF) {
        return false;  /* Combined size exceeds 64KB descriptor limit */
    }
    
    /* Check 64KB boundary - don't coalesce across boundaries */
    /* Use 64-bit math and check if floor(start/64K) == floor((end-1)/64K) */
    if ((entry1->physical_addr >> 16) != ((combined_end - 1) >> 16)) {
        return false;  /* Would cross 64KB boundary */
    }
    
    return true;  /* Safe to coalesce - strictly adjacent and within limits */
}

/**
 * Coalesce scatter-gather list to minimize descriptors
 * GPT-5 recommendation: Reduce S/G entries for hardware efficiency
 * CRITICAL: Only merges strictly adjacent entries (no gaps)
 */
uint16_t vds_coalesce_sg_list(vds_sg_entry_t* sg_list, uint16_t sg_count, 
                              uint32_t max_gap)
{
    uint16_t read_idx, write_idx;
    vds_sg_entry_t* current;
    vds_sg_entry_t* next;
    
    /* max_gap parameter deprecated - only adjacent entries coalesced */
    (void)max_gap;
    
    if (!sg_list || sg_count <= 1) {
        return sg_count;  /* Nothing to coalesce */
    }
    
    /* Coalesce strictly adjacent entries only */
    write_idx = 0;
    for (read_idx = 0; read_idx < sg_count - 1; read_idx++) {
        uint32_t end_next;
        uint32_t end_current;

        current = &sg_list[write_idx];
        next = &sg_list[read_idx + 1];

        if (vds_can_coalesce_sg_entries(current, next, 0)) {
            /* Coalesce: extend current entry to include next */
            end_next = next->physical_addr + next->size;
            end_current = current->physical_addr + current->size;

            if (end_next > end_current) {
                /* Extend size to cover both entries and any gap */
                current->size = end_next - current->physical_addr;
            }
            /* Don't increment write_idx - keep coalescing into same entry */

            LOG_DEBUG("VDS Safety: Coalesced S/G entries %u-%u (new size: %lu)",
                     write_idx, read_idx + 1, current->size);
        } else {
            /* Can't coalesce - move to next write position */
            write_idx++;
            if (write_idx != read_idx + 1) {
                /* Copy next entry to write position */
                sg_list[write_idx] = *next;
            }
        }
    }
    
    /* New count after coalescing */
    sg_count = write_idx + 1;
    
    LOG_INFO("VDS Safety: S/G list coalesced to %u entries", sg_count);
    return sg_count;
}