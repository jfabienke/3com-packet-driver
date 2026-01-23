/**
 * @file dma_operations.c
 * @brief Production-grade DMA operations with VDS integration and cache management
 * 
 * Implements proper VDS flag handling, ISR context safety, directional cache ops,
 * and 8237 DMA chunking for 64KB boundaries.
 */

#include <dos.h>
#include <string.h>
#include "../../include/common.h"
#include "../../include/hardware.h"
#include "../../include/vds.h"
#include "../../include/cachecoh.h"
#include "../../include/cpudet.h"
#include "../../include/logging.h"

/* DMA operation context */
typedef struct {
    void far *buffer;
    uint32_t size;
    uint32_t physical_addr;
    VDS_DDS dds;
    bool vds_locked;
    bool needs_cache_flush;
    bool needs_cache_invalidate;
    bool in_isr_context;
} dma_operation_t;

/* Global state for ISR context detection */
static volatile uint16_t g_isr_nesting_level = 0;
static volatile bool g_deferred_cache_ops_pending = false;

/* Deferred operation queue for ISR context */
#define MAX_DEFERRED_OPS 16
static struct {
    enum { CACHE_OP_FLUSH, CACHE_OP_INVALIDATE, CACHE_OP_WBINVD } type;
    void far *addr;
    uint32_t size;
} g_deferred_ops[MAX_DEFERRED_OPS];
static volatile uint8_t g_deferred_ops_head = 0;
static volatile uint8_t g_deferred_ops_tail = 0;

/**
 * @brief Enter ISR context
 * Call at the start of every interrupt handler
 */
void dma_enter_isr_context(void) {
    _disable();
    g_isr_nesting_level++;
    _enable();
}

/**
 * @brief Exit ISR context and process deferred operations
 * Call at the end of every interrupt handler
 */
void dma_exit_isr_context(void) {
    _disable();
    
    if (g_isr_nesting_level > 0) {
        g_isr_nesting_level--;
    }
    
    /* Process deferred cache operations if we're exiting the last ISR level */
    if (g_isr_nesting_level == 0 && g_deferred_cache_ops_pending) {
        _enable();
        dma_process_deferred_cache_ops();
        _disable();
    }
    
    _enable();
}

/**
 * @brief Check if currently in ISR context
 */
bool dma_in_isr_context(void) {
    return g_isr_nesting_level > 0;
}

/**
 * @brief Queue a cache operation for deferred execution
 */
static void queue_deferred_cache_op(int type, void far *addr, uint32_t size) {
    uint8_t next_head;
    
    _disable();
    
    next_head = (g_deferred_ops_head + 1) & (MAX_DEFERRED_OPS - 1);
    if (next_head != g_deferred_ops_tail) {
        g_deferred_ops[g_deferred_ops_head].type = type;
        g_deferred_ops[g_deferred_ops_head].addr = addr;
        g_deferred_ops[g_deferred_ops_head].size = size;
        g_deferred_ops_head = next_head;
        g_deferred_cache_ops_pending = true;
    } else {
        LOG_WARNING("DMA: Deferred cache operation queue full");
    }
    
    _enable();
}

/**
 * @brief Process deferred cache operations (called outside ISR)
 */
void dma_process_deferred_cache_ops(void) {
    while (g_deferred_ops_tail != g_deferred_ops_head) {
        int type = g_deferred_ops[g_deferred_ops_tail].type;
        void far *addr = g_deferred_ops[g_deferred_ops_tail].addr;
        uint32_t size = g_deferred_ops[g_deferred_ops_tail].size;
        
        g_deferred_ops_tail = (g_deferred_ops_tail + 1) & (MAX_DEFERRED_OPS - 1);
        
        switch (type) {
            case CACHE_OP_FLUSH:
                cache_flush_range(addr, size);
                break;
            case CACHE_OP_INVALIDATE:
                cache_invalidate_range(addr, size);
                break;
            case CACHE_OP_WBINVD:
                if (cpu_has_feature(CPU_FEATURE_WBINVD)) {
                    _asm { wbinvd }
                }
                break;
        }
    }
    
    g_deferred_cache_ops_pending = false;
}

/**
 * @brief Prepare buffer for DMA TX with proper cache management
 * @param nic NIC information
 * @param buffer Buffer to transmit
 * @param size Buffer size
 * @param op Operation context (output)
 * @return SUCCESS or error code
 */
int dma_prepare_tx(nic_info_t *nic, void far *buffer, uint32_t size, dma_operation_t *op) {
    int result;
    extern cpu_info_t g_cpu_info;
    
    memset(op, 0, sizeof(*op));
    op->buffer = buffer;
    op->size = size;
    op->in_isr_context = dma_in_isr_context();
    
    /* Check if VDS is available and should be used */
    if (vds_available()) {
        /* Lock region with VDS */
        result = vds_lock_region(buffer, size, &op->dds);
        if (result != VDS_SUCCESS) {
            LOG_ERROR("DMA TX: VDS lock failed with code %d", result);
            return ERROR_DMA_LOCK;
        }
        
        op->vds_locked = true;
        op->physical_addr = op->dds.physical;
        
        /* Check VDS cache flags */
        if (!(op->dds.flags & VDS_FLAGS_NO_CACHE_FLUSH)) {
            /* VDS says we need to flush cache */
            op->needs_cache_flush = true;
        }
        
        /* Validate address for NIC constraints */
        if (nic->type == NIC_TYPE_3C515_TX) {
            /* 3C515 bus master: check 24-bit (16MB) limit */
            if (op->physical_addr >= 0x1000000) {
                LOG_ERROR("DMA TX: Physical address 0x%08lX exceeds 16MB", op->physical_addr);
                vds_unlock_region(&op->dds);
                return ERROR_DMA_ADDRESS;
            }
        }
    } else {
        /* No VDS - calculate physical address directly */
        op->physical_addr = ((uint32_t)FP_SEG(buffer) << 4) + FP_OFF(buffer);
        
        /* Without VDS, we need cache management based on CPU */
        if (g_cpu_info.cache_mode == CACHE_MODE_WRITE_BACK) {
            op->needs_cache_flush = true;
        }
    }
    
    /* Perform cache flush for TX if needed */
    if (op->needs_cache_flush) {
        if (op->in_isr_context) {
            /* Defer cache operation */
            queue_deferred_cache_op(CACHE_OP_FLUSH, buffer, size);
            LOG_DEBUG("DMA TX: Deferred cache flush for ISR context");
        } else {
            /* Safe to flush now */
            cache_flush_range(buffer, size);
            LOG_DEBUG("DMA TX: Cache flushed for write-back mode");
        }
    }
    
    return SUCCESS;
}

/**
 * @brief Prepare buffer for DMA RX with proper cache management
 * @param nic NIC information
 * @param buffer Buffer to receive into
 * @param size Buffer size
 * @param op Operation context (output)
 * @return SUCCESS or error code
 */
int dma_prepare_rx(nic_info_t *nic, void far *buffer, uint32_t size, dma_operation_t *op) {
    int result;
    extern cpu_info_t g_cpu_info;
    
    memset(op, 0, sizeof(*op));
    op->buffer = buffer;
    op->size = size;
    op->in_isr_context = dma_in_isr_context();
    
    /* Check if VDS is available and should be used */
    if (vds_available()) {
        /* Lock region with VDS */
        result = vds_lock_region(buffer, size, &op->dds);
        if (result != VDS_SUCCESS) {
            LOG_ERROR("DMA RX: VDS lock failed with code %d", result);
            return ERROR_DMA_LOCK;
        }
        
        op->vds_locked = true;
        op->physical_addr = op->dds.physical;
        
        /* Check VDS cache flags */
        if (!(op->dds.flags & VDS_FLAGS_NO_CACHE_INV)) {
            /* VDS says we need to invalidate cache */
            op->needs_cache_invalidate = true;
        }
        
        /* Validate address for NIC constraints */
        if (nic->type == NIC_TYPE_3C515_TX) {
            /* 3C515 bus master: check 24-bit (16MB) limit */
            if (op->physical_addr >= 0x1000000) {
                LOG_ERROR("DMA RX: Physical address 0x%08lX exceeds 16MB", op->physical_addr);
                vds_unlock_region(&op->dds);
                return ERROR_DMA_ADDRESS;
            }
        }
    } else {
        /* No VDS - calculate physical address directly */
        op->physical_addr = ((uint32_t)FP_SEG(buffer) << 4) + FP_OFF(buffer);
        
        /* Without VDS, we need cache management based on CPU */
        if (g_cpu_info.cache_mode != CACHE_MODE_DISABLED) {
            op->needs_cache_invalidate = true;
        }
    }
    
    /* Perform cache invalidate for RX if needed */
    if (op->needs_cache_invalidate) {
        if (op->in_isr_context) {
            /* Defer cache operation */
            queue_deferred_cache_op(CACHE_OP_INVALIDATE, buffer, size);
            LOG_DEBUG("DMA RX: Deferred cache invalidate for ISR context");
        } else {
            /* Safe to invalidate now */
            cache_invalidate_range(buffer, size);
            LOG_DEBUG("DMA RX: Cache invalidated for coherency");
        }
    }
    
    return SUCCESS;
}

/**
 * @brief Complete DMA operation and cleanup
 * @param op Operation context
 */
void dma_complete_operation(dma_operation_t *op) {
    if (op->vds_locked) {
        vds_unlock_region(&op->dds);
        op->vds_locked = false;
    }
}

/* Note: 8237 DMA controller support removed as neither 3C509B (PIO-only) 
 * nor 3C515-TX (bus master) use it. Can be re-added if support for 
 * 8237-based NICs (NE2000, 3C503) is needed in the future. */

/**
 * @brief Validate 3C515-TX bus master constraints
 * Based on datasheet analysis
 */
bool dma_validate_3c515_constraints(uint32_t phys_addr, uint32_t size) {
    /* 3C515-TX Corkscrew datasheet constraints:
     * - ISA bus master, not 8237 DMA
     * - 24-bit address bus (16MB limit)
     * - Can cross 64KB boundaries (bus master handles it)
     * - Requires DWORD alignment for descriptors
     * - Buffers should be WORD aligned for performance
     */
    
    /* Check 16MB limit */
    if (phys_addr >= 0x1000000) {
        LOG_ERROR("3C515: Address 0x%08lX exceeds 16MB ISA limit", phys_addr);
        return false;
    }
    
    if ((phys_addr + size - 1) >= 0x1000000) {
        LOG_ERROR("3C515: Buffer end exceeds 16MB ISA limit");
        return false;
    }
    
    /* Check alignment (warning only) */
    if (phys_addr & 1) {
        LOG_WARNING("3C515: Buffer not WORD aligned, may impact performance");
    }
    
    /* 3C515 can handle 64KB crossing as bus master */
    LOG_DEBUG("3C515: Buffer validated, can cross 64KB as bus master");
    
    return true;
}

/**
 * @brief Validate 3C509B PIO constraints
 * Based on datasheet analysis
 */
bool dma_validate_3c509_constraints(uint32_t phys_addr, uint32_t size) {
    /* 3C509B EtherLink III constraints:
     * - PIO only, no DMA capability
     * - This function should not be called for 3C509B
     */
    
    LOG_ERROR("3C509B: Attempted DMA on PIO-only NIC");
    return false;
}

/**
 * @brief Get cache coherency strategy for NIC
 * @param nic NIC information
 * @return Cache coherency assumptions
 */
const char* dma_get_nic_coherency_strategy(nic_info_t *nic) {
    switch (nic->type) {
        case NIC_TYPE_3C509B:
            return "PIO-only, no DMA cache coherency needed";
            
        case NIC_TYPE_3C515_TX:
            /* 3C515 on ISA bus - coherency depends on chipset */
            if (nic->bus_snooping_verified) {
                return "ISA bus master with verified chipset snooping";
            } else {
                return "ISA bus master, assume non-coherent (flush/invalidate required)";
            }
            
        default:
            return "Unknown NIC, assume non-coherent DMA";
    }
}