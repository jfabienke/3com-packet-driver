/**
 * @file dma_safe_alloc.c
 * @brief DMA-safe memory allocator for DOS packet driver
 * 
 * Provides physically contiguous memory allocation with alignment guarantees
 * for DMA descriptors and buffers. Handles DOS memory managers (EMM386/QEMM)
 * and ensures proper physical addressing for bus master DMA.
 * 
 * Critical for production reliability on diverse DOS configurations.
 */

#include <dos.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <malloc.h>
#include "dmasafa.h"
#include "vds.h"
#include "logging.h"
#include "common.h"

/* DOS memory allocation strategies */
#define DOS_ALLOC_FIRST_FIT    0x00
#define DOS_ALLOC_BEST_FIT     0x01
#define DOS_ALLOC_LAST_FIT     0x02

/* Memory allocation flags */
#define DMAMEM_BELOW_1M         0x01
#define DMAMEM_BELOW_16M        0x02
#define DMAMEM_CONTIGUOUS       0x04
#define DMAMEM_ALIGNED          0x08
#define DMAMEM_NO_CROSS_4K      0x10
#define DMAMEM_NO_CROSS_64K     0x20

/* Maximum allocations to track */
#define MAX_DMA_ALLOCS          32

/* DMA allocation entry */
typedef struct {
    void *virt_addr;            /* Virtual address */
    uint32_t phys_addr;         /* Physical address */
    uint32_t size;              /* Allocation size */
    uint32_t alignment;         /* Required alignment */
    uint32_t flags;             /* Allocation flags */
    bool in_use;                /* Slot in use */
    bool locked;                /* VDS locked */
    uint32_t vds_handle;        /* VDS lock handle */
} dma_alloc_t;

/* DMA allocation table */
static dma_alloc_t dma_allocs[MAX_DMA_ALLOCS] = {0};

/* Memory manager detection results */
static struct {
    bool checked;
    bool emm386_present;
    bool qemm_present;
    bool vds_available;
    bool paging_enabled;
    uint32_t page_size;
} mem_mgr_info = {0};

/**
 * @brief Detect DOS memory manager presence
 * 
 * Checks for EMM386, QEMM, and other memory managers that affect
 * physical/virtual address mapping.
 */
static void detect_memory_manager(void) {
    union REGS regs;
    
    if (mem_mgr_info.checked) {
        return;
    }
    
    mem_mgr_info.checked = true;
    
    /* Check for EMM386 via INT 67h */
    memset(&regs, 0, sizeof(regs));
    regs.h.ah = 0x40;  /* Get EMM status */
    int86(0x67, &regs, &regs);
    if (regs.h.ah == 0) {
        mem_mgr_info.emm386_present = true;
        LOG_INFO("EMM386 or compatible EMM detected");
    }
    
    /* Check for QEMM via INT 2Fh */
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0xD200;
    int86(0x2F, &regs, &regs);
    if (regs.h.al == 0xFF) {
        mem_mgr_info.qemm_present = true;
        LOG_INFO("QEMM detected");
    }
    
    /* Check for VDS availability */
    if (vds_present()) {
        mem_mgr_info.vds_available = true;
        LOG_INFO("Virtual DMA Services (VDS) available");
    }
    
    /* Determine if paging is enabled */
    if (mem_mgr_info.emm386_present || mem_mgr_info.qemm_present) {
        mem_mgr_info.paging_enabled = true;
        mem_mgr_info.page_size = 4096;  /* Standard x86 page size */
        LOG_WARNING("Paging enabled - physical addresses may not match virtual");
    }
}

/**
 * @brief Convert virtual address to physical address
 * 
 * Handles both real mode (identity mapping) and protected mode with
 * memory managers. FAILS SAFELY when paging enabled without VDS.
 * 
 * @param virt_addr Virtual address
 * @return Physical address, or 0xFFFFFFFF on error
 */
static uint32_t virt_to_phys(void *virt_addr) {
    uint32_t phys_addr;
    uint16_t segment, offset;
    
    /* Get segment:offset from pointer */
    segment = FP_SEG(virt_addr);
    offset = FP_OFF(virt_addr);
    
    /* In real mode without memory manager, physical = segment*16 + offset */
    if (!mem_mgr_info.paging_enabled) {
        phys_addr = ((uint32_t)segment << 4) + offset;
        return phys_addr;
    }
    
    /* With memory manager, use VDS if available */
    if (mem_mgr_info.vds_available) {
        vds_dds_t dds;
        memset(&dds, 0, sizeof(dds));
        dds.size = 1;  /* Just need address translation */
        dds.offset = offset;
        dds.segment = segment;
        
        if (vds_lock_region(&dds) == VDS_SUCCESS) {
            phys_addr = dds.physical_address;
            vds_unlock_region(&dds);
            return phys_addr;
        }
    }
    
    /* CRITICAL: With paging enabled but no VDS, we CANNOT determine physical address */
    /* This is unsafe for DMA - must fail rather than guess */
    LOG_ERROR("Paging enabled but VDS unavailable - cannot determine physical address");
    return 0xFFFFFFFF;  /* Indicate error */
}

/**
 * @brief Verify physical contiguity of memory region
 * 
 * When using VDS with paging, verifies that all pages in the region
 * are physically contiguous. Critical for DMA safety.
 * 
 * @param virt_addr Virtual address
 * @param size Size in bytes
 * @return true if physically contiguous, false otherwise
 */
static bool verify_physical_contiguity(void *virt_addr, uint32_t size) {
    uint32_t offset;
    uint32_t first_phys, expected_phys, actual_phys;
    vds_dds_t dds;
    
    /* Only needed when paging is enabled with VDS */
    if (!mem_mgr_info.paging_enabled || !mem_mgr_info.vds_available) {
        return true;  /* Assume contiguous in real mode */
    }
    
    /* Get physical address of first byte */
    memset(&dds, 0, sizeof(dds));
    dds.size = 1;
    dds.segment = FP_SEG(virt_addr);
    dds.offset = FP_OFF(virt_addr);
    
    if (vds_lock_region(&dds) != VDS_SUCCESS) {
        return false;
    }
    first_phys = dds.physical_address;
    vds_unlock_region(&dds);
    
    /* Check each 4K page boundary */
    for (offset = 4096; offset < size; offset += 4096) {
        void *page_addr = (char*)virt_addr + offset;
        
        dds.segment = FP_SEG(page_addr);
        dds.offset = FP_OFF(page_addr);
        
        if (vds_lock_region(&dds) != VDS_SUCCESS) {
            return false;
        }
        actual_phys = dds.physical_address;
        vds_unlock_region(&dds);
        
        expected_phys = first_phys + offset;
        if (actual_phys != expected_phys) {
            LOG_WARNING("Physical discontinuity at offset %lu: expected 0x%08lX, got 0x%08lX",
                       offset, expected_phys, actual_phys);
            return false;
        }
    }
    
    return true;
}

/**
 * @brief Check if memory region crosses boundary
 * 
 * @param phys_addr Physical address
 * @param size Region size
 * @param boundary Boundary size (e.g., 4096 for 4K)
 * @return true if crosses boundary, false otherwise
 */
static bool crosses_boundary(uint32_t phys_addr, uint32_t size, uint32_t boundary) {
    uint32_t start_page = phys_addr / boundary;
    uint32_t end_page = (phys_addr + size - 1) / boundary;
    return (start_page != end_page);
}

/**
 * @brief Allocate physically contiguous DMA-safe memory
 * 
 * Allocates memory suitable for bus master DMA with specified constraints.
 * 
 * @param size Size in bytes
 * @param alignment Required alignment (must be power of 2)
 * @param flags Allocation flags (DMAMEM_*)
 * @return DMA allocation info, or NULL on failure
 */
dma_alloc_info_t* dma_alloc_coherent(uint32_t size, uint32_t alignment, uint32_t flags) {
    dma_alloc_t *alloc = NULL;
    void *virt_addr = NULL;
    uint32_t phys_addr;
    uint32_t alloc_size;
    int i, attempts;
    
    /* Validate parameters */
    if (size == 0 || size > 65536) {
        LOG_ERROR("Invalid DMA allocation size: %lu", size);
        return NULL;
    }
    
    if (alignment & (alignment - 1)) {
        LOG_ERROR("Alignment must be power of 2: %lu", alignment);
        return NULL;
    }
    
    if (alignment < 16) {
        alignment = 16;  /* Minimum alignment for DMA */
    }
    
    /* Detect memory manager if not done */
    detect_memory_manager();
    
    /* Find free allocation slot */
    for (i = 0; i < MAX_DMA_ALLOCS; i++) {
        if (!dma_allocs[i].in_use) {
            alloc = &dma_allocs[i];
            break;
        }
    }
    
    if (!alloc) {
        LOG_ERROR("No free DMA allocation slots");
        return NULL;
    }
    
    /* Calculate allocation size with alignment padding */
    alloc_size = size + alignment + 4096;  /* Extra for boundary avoidance */
    
    /* CRITICAL: If paging enabled without VDS, MUST use conventional memory only */
    uint8_t saved_strategy = 0;
    uint16_t saved_umb_link = 0;
    bool dos_state_changed = false;
    
    if (mem_mgr_info.paging_enabled && !mem_mgr_info.vds_available) {
        LOG_WARNING("Paging without VDS - forcing conventional memory allocation");
        flags |= DMAMEM_BELOW_1M;  /* Force below 1MB */
        
        /* Save and modify DOS allocation state for safety */
        union REGS regs;
        
        /* 1. Get current allocation strategy */
        memset(&regs, 0, sizeof(regs));
        regs.x.ax = 0x5800;  /* Get allocation strategy */
        int86(0x21, &regs, &regs);
        if (!regs.x.cflag) {
            saved_strategy = regs.h.al;
        }
        
        /* 2. Get current UMB link state */
        memset(&regs, 0, sizeof(regs));
        regs.x.ax = 0x5802;  /* Get UMB link state */
        int86(0x21, &regs, &regs);
        if (!regs.x.cflag) {
            saved_umb_link = regs.x.bx;
        }
        
        /* 3. Unlink UMBs to prevent allocation from upper memory */
        memset(&regs, 0, sizeof(regs));
        regs.x.ax = 0x5803;  /* Set UMB link state */
        regs.x.bx = 0x0000;  /* 0 = unlinked */
        int86(0x21, &regs, &regs);
        
        /* 4. Set allocation strategy to low memory first fit */
        memset(&regs, 0, sizeof(regs));
        regs.x.ax = 0x5801;  /* Set allocation strategy */
        regs.x.bx = 0x0000;  /* 0 = first fit, low memory */
        int86(0x21, &regs, &regs);
        
        dos_state_changed = true;
    }
    
    /* Try allocation with different strategies */
    for (attempts = 0; attempts < 3; attempts++) {
        /* Allocate from DOS conventional memory */
        if (flags & DMAMEM_BELOW_1M) {
            /* Use DOS allocation for < 1MB requirement */
            union REGS regs;
            memset(&regs, 0, sizeof(regs));
            regs.h.ah = 0x48;  /* Allocate memory */
            regs.x.bx = (alloc_size + 15) / 16;  /* Paragraphs */
            int86(0x21, &regs, &regs);
            
            if (!regs.x.cflag) {
                virt_addr = MK_FP(regs.x.ax, 0);
                
                /* Verify ENTIRE block is in conventional memory (< 640K) */
                uint32_t base = ((uint32_t)regs.x.ax << 4);
                uint32_t end = base + alloc_size - 1;
                if (base >= 0xA0000 || end >= 0xA0000) {
                    LOG_WARNING("DOS allocation outside conventional memory (base=0x%05lX, end=0x%05lX)",
                               base, end);
                    /* Free it */
                    memset(&regs, 0, sizeof(regs));
                    regs.h.ah = 0x49;
                    regs.x.es = FP_SEG(virt_addr);
                    int86(0x21, &regs, &regs);
                    virt_addr = NULL;
                    continue;
                }
            }
        } else {
            /* Use C library allocation */
            virt_addr = malloc(alloc_size);
        }
        
        if (!virt_addr) {
            continue;
        }
        
        /* Align the address */
        uint32_t unaligned = (uint32_t)virt_addr;
        uint32_t aligned = (unaligned + alignment - 1) & ~(alignment - 1);
        virt_addr = (void*)aligned;
        
        /* Get physical address */
        phys_addr = virt_to_phys(virt_addr);
        if (phys_addr == 0xFFFFFFFF) {
            free(virt_addr);
            continue;
        }
        
        /* Check constraints */
        bool valid = true;
        
        /* Check memory range constraints */
        if ((flags & DMAMEM_BELOW_1M) && (phys_addr + size > 0x100000)) {
            valid = false;
        }
        if ((flags & DMAMEM_BELOW_16M) && (phys_addr + size > 0x1000000)) {
            valid = false;
        }
        
        /* Check boundary crossing */
        if ((flags & DMAMEM_NO_CROSS_4K) && crosses_boundary(phys_addr, size, 4096)) {
            valid = false;
        }
        if ((flags & DMAMEM_NO_CROSS_64K) && crosses_boundary(phys_addr, size, 65536)) {
            valid = false;
        }
        
        /* Verify physical contiguity if required */
        if (valid && (flags & DMAMEM_CONTIGUOUS)) {
            if (!verify_physical_contiguity(virt_addr, size)) {
                LOG_WARNING("Memory not physically contiguous - retrying");
                valid = false;
            }
        }
        
        if (valid) {
            /* Success - fill allocation info */
            memset(alloc, 0, sizeof(*alloc));
            alloc->virt_addr = virt_addr;
            alloc->phys_addr = phys_addr;
            alloc->size = size;
            alloc->alignment = alignment;
            alloc->flags = flags;
            alloc->in_use = true;
            
            /* Lock with VDS if available */
            if (mem_mgr_info.vds_available) {
                vds_dds_t dds;
                memset(&dds, 0, sizeof(dds));
                dds.size = size;
                dds.offset = FP_OFF(virt_addr);
                dds.segment = FP_SEG(virt_addr);
                
                if (vds_lock_region(&dds) == VDS_SUCCESS) {
                    alloc->locked = true;
                    alloc->vds_handle = dds.region_id;
                    alloc->phys_addr = dds.physical_address;  /* Use VDS result */
                }
            }
            
            /* Zero the memory */
            memset(virt_addr, 0, size);
            
            LOG_INFO("DMA allocation successful: virt=%p, phys=0x%08lX, size=%lu, align=%lu",
                    virt_addr, phys_addr, size, alignment);
            
            /* Restore DOS allocation state if we changed it */
            if (dos_state_changed) {
                union REGS regs;
                
                /* Restore allocation strategy */
                memset(&regs, 0, sizeof(regs));
                regs.x.ax = 0x5801;
                regs.x.bx = saved_strategy;
                int86(0x21, &regs, &regs);
                
                /* Restore UMB link state */
                memset(&regs, 0, sizeof(regs));
                regs.x.ax = 0x5803;
                regs.x.bx = saved_umb_link;
                int86(0x21, &regs, &regs);
            }
            
            /* Return public info structure */
            static dma_alloc_info_t info;
            info.virt_addr = virt_addr;
            info.phys_addr = phys_addr;
            info.size = size;
            info.alignment = alignment;
            info.flags = flags;
            
            return &info;
        }
        
        /* Constraints not met, free and retry */
        free(virt_addr);
    }
    
    LOG_ERROR("Failed to allocate DMA-safe memory after %d attempts", attempts);
    
    /* Restore DOS allocation state if we changed it */
    if (dos_state_changed) {
        union REGS regs;
        
        /* Restore allocation strategy */
        memset(&regs, 0, sizeof(regs));
        regs.x.ax = 0x5801;
        regs.x.bx = saved_strategy;
        int86(0x21, &regs, &regs);
        
        /* Restore UMB link state */
        memset(&regs, 0, sizeof(regs));
        regs.x.ax = 0x5803;
        regs.x.bx = saved_umb_link;
        int86(0x21, &regs, &regs);
    }
    
    return NULL;
}

/**
 * @brief Free DMA-safe memory
 * 
 * @param info DMA allocation info from dma_alloc_coherent()
 */
void dma_free_coherent(dma_alloc_info_t *info) {
    int i;
    
    if (!info || !info->virt_addr) {
        return;
    }
    
    /* Find allocation in table */
    for (i = 0; i < MAX_DMA_ALLOCS; i++) {
        if (dma_allocs[i].in_use && 
            dma_allocs[i].virt_addr == info->virt_addr) {
            
            /* Unlock VDS if locked */
            if (dma_allocs[i].locked && mem_mgr_info.vds_available) {
                vds_dds_t dds;
                memset(&dds, 0, sizeof(dds));
                dds.region_id = dma_allocs[i].vds_handle;
                vds_unlock_region(&dds);
            }
            
            /* Free memory */
            if (dma_allocs[i].flags & DMAMEM_BELOW_1M) {
                /* DOS memory - use INT 21h AH=49h */
                union REGS regs;
                memset(&regs, 0, sizeof(regs));
                regs.h.ah = 0x49;
                regs.x.es = FP_SEG(dma_allocs[i].virt_addr);
                int86(0x21, &regs, &regs);
            } else {
                free(dma_allocs[i].virt_addr);
            }
            
            /* Clear allocation entry */
            memset(&dma_allocs[i], 0, sizeof(dma_allocs[i]));
            
            LOG_DEBUG("Freed DMA allocation at %p", info->virt_addr);
            return;
        }
    }
    
    LOG_WARNING("DMA allocation not found in table");
}

/**
 * @brief Allocate DMA descriptor ring
 * 
 * Specialized allocator for descriptor rings with strict alignment.
 * 
 * @param num_descriptors Number of descriptors
 * @param descriptor_size Size of each descriptor
 * @param alignment Required alignment (typically 16 or 32)
 * @return DMA allocation info, or NULL on failure
 */
dma_alloc_info_t* dma_alloc_ring(uint32_t num_descriptors, uint32_t descriptor_size,
                                 uint32_t alignment) {
    uint32_t total_size;
    uint32_t flags;
    
    /* Calculate total size */
    total_size = num_descriptors * descriptor_size;
    
    /* Rings typically need strong alignment and no 4K crossing */
    flags = DMAMEM_CONTIGUOUS | DMAMEM_ALIGNED | DMAMEM_NO_CROSS_4K;
    
    /* Most NICs need rings below 4GB (32-bit addressing) */
    /* For older NICs, may need below 16MB */
    if (total_size <= 4096) {
        flags |= DMAMEM_BELOW_16M;  /* Conservative for small rings */
    }
    
    LOG_INFO("Allocating DMA ring: %lu descriptors × %lu bytes, align=%lu",
            num_descriptors, descriptor_size, alignment);
    
    return dma_alloc_coherent(total_size, alignment, flags);
}

/**
 * @brief Allocate DMA packet buffer
 * 
 * Allocates buffer suitable for packet data with relaxed alignment.
 * 
 * @param size Buffer size (typically 1518 for Ethernet)
 * @return DMA allocation info, or NULL on failure
 */
dma_alloc_info_t* dma_alloc_packet_buffer(uint32_t size) {
    uint32_t flags;
    
    /* Packet buffers have relaxed requirements */
    flags = DMAMEM_CONTIGUOUS;
    
    /* Round up to cache line for performance */
    size = (size + 63) & ~63;
    
    return dma_alloc_coherent(size, 64, flags);
}

/**
 * @brief Check if DMA address is valid for device
 * 
 * Verifies that physical address is within device's DMA capability.
 * 
 * @param phys_addr Physical address
 * @param dma_mask Device DMA mask (e.g., 0xFFFFFFFF for 32-bit)
 * @return true if valid, false if bounce buffer needed
 */
bool dma_addr_valid(uint32_t phys_addr, uint32_t dma_mask) {
    return (phys_addr & ~dma_mask) == 0;
}

/* Bounce buffer pool for memory-constrained environments */
#define BOUNCE_BUFFER_COUNT     8       /* Maximum bounce buffers */
#define BOUNCE_BUFFER_SIZE      2048    /* Buffer size (max Ethernet frame + overhead) */

typedef struct {
    void *buffer;               /* Bounce buffer address */
    uint32_t phys_addr;        /* Physical address */
    bool in_use;               /* Buffer in use */
    void *original_addr;       /* Original buffer address */
    uint32_t size;             /* Data size */
    bool tx_direction;         /* true for TX, false for RX */
} bounce_buffer_t;

static bounce_buffer_t bounce_buffers[BOUNCE_BUFFER_COUNT] = {0};
static bool bounce_pool_initialized = false;

/**
 * @brief Initialize bounce buffer pool
 * 
 * @return true on success, false on failure
 */
static bool init_bounce_pool(void) {
    int i;
    
    if (bounce_pool_initialized) {
        return true;
    }
    
    LOG_INFO("Initializing bounce buffer pool (%d buffers of %d bytes each)",
            BOUNCE_BUFFER_COUNT, BOUNCE_BUFFER_SIZE);
    
    for (i = 0; i < BOUNCE_BUFFER_COUNT; i++) {
        /* Allocate below 16MB for ISA compatibility */
        dma_alloc_info_t *info = dma_alloc_coherent(
            BOUNCE_BUFFER_SIZE, 
            16,  /* 16-byte alignment */
            DMAMEM_BELOW_16M | DMAMEM_CONTIGUOUS | DMAMEM_ALIGNED
        );
        
        if (!info) {
            LOG_WARNING("Failed to allocate bounce buffer %d", i);
            continue;
        }
        
        bounce_buffers[i].buffer = info->virt_addr;
        bounce_buffers[i].phys_addr = info->phys_addr;
        bounce_buffers[i].in_use = false;
        
        LOG_DEBUG("Bounce buffer %d: virt=%p phys=0x%08lX", 
                 i, info->virt_addr, info->phys_addr);
    }
    
    bounce_pool_initialized = true;
    
    /* Count successful allocations */
    int allocated = 0;
    for (i = 0; i < BOUNCE_BUFFER_COUNT; i++) {
        if (bounce_buffers[i].buffer) allocated++;
    }
    
    if (allocated == 0) {
        LOG_ERROR("No bounce buffers allocated - will fail on incompatible memory");
        return false;
    }
    
    LOG_INFO("Bounce buffer pool initialized with %d/%d buffers", 
            allocated, BOUNCE_BUFFER_COUNT);
    return true;
}

/**
 * @brief Allocate bounce buffer for DMA operation
 * 
 * @param original_addr Original buffer address
 * @param size Data size
 * @param tx_direction true for TX, false for RX
 * @return Bounce buffer physical address, or 0xFFFFFFFF on failure
 */
uint32_t dma_alloc_bounce_buffer(void *original_addr, uint32_t size, bool tx_direction) {
    int i;
    
    if (!init_bounce_pool()) {
        return 0xFFFFFFFF;
    }
    
    if (size > BOUNCE_BUFFER_SIZE) {
        LOG_ERROR("Data size %lu exceeds bounce buffer size %d", size, BOUNCE_BUFFER_SIZE);
        return 0xFFFFFFFF;
    }
    
    /* Find free bounce buffer */
    for (i = 0; i < BOUNCE_BUFFER_COUNT; i++) {
        if (bounce_buffers[i].buffer && !bounce_buffers[i].in_use) {
            bounce_buffers[i].in_use = true;
            bounce_buffers[i].original_addr = original_addr;
            bounce_buffers[i].size = size;
            bounce_buffers[i].tx_direction = tx_direction;
            
            /* For TX, copy data to bounce buffer */
            if (tx_direction && original_addr) {
                memcpy(bounce_buffers[i].buffer, original_addr, size);
                LOG_DEBUG("Copied %lu bytes to bounce buffer %d for TX", size, i);
            }
            
            LOG_DEBUG("Allocated bounce buffer %d: phys=0x%08lX size=%lu %s",
                     i, bounce_buffers[i].phys_addr, size, 
                     tx_direction ? "TX" : "RX");
            
            return bounce_buffers[i].phys_addr;
        }
    }
    
    LOG_ERROR("No free bounce buffers available (%d in use)", BOUNCE_BUFFER_COUNT);
    return 0xFFFFFFFF;
}

/**
 * @brief Free bounce buffer and copy data back if needed
 * 
 * @param phys_addr Physical address returned by dma_alloc_bounce_buffer()
 * @return true on success, false on error
 */
bool dma_free_bounce_buffer(uint32_t phys_addr) {
    int i;
    
    /* Find bounce buffer by physical address */
    for (i = 0; i < BOUNCE_BUFFER_COUNT; i++) {
        if (bounce_buffers[i].in_use && 
            bounce_buffers[i].phys_addr == phys_addr) {
            
            /* For RX, copy data back to original buffer */
            if (!bounce_buffers[i].tx_direction && 
                bounce_buffers[i].original_addr) {
                memcpy(bounce_buffers[i].original_addr, 
                       bounce_buffers[i].buffer, 
                       bounce_buffers[i].size);
                LOG_DEBUG("Copied %lu bytes from bounce buffer %d after RX", 
                         bounce_buffers[i].size, i);
            }
            
            /* Clear bounce buffer info */
            bounce_buffers[i].in_use = false;
            bounce_buffers[i].original_addr = NULL;
            bounce_buffers[i].size = 0;
            bounce_buffers[i].tx_direction = false;
            
            LOG_DEBUG("Freed bounce buffer %d", i);
            return true;
        }
    }
    
    LOG_ERROR("Bounce buffer with phys_addr 0x%08lX not found", phys_addr);
    return false;
}

/**
 * @brief Check if address needs bounce buffer
 * 
 * @param virt_addr Virtual address
 * @param phys_addr Physical address
 * @param size Data size
 * @param dma_mask Device DMA capability mask
 * @return true if bounce buffer needed, false if direct DMA ok
 */
bool dma_needs_bounce_buffer(void *virt_addr, uint32_t phys_addr, 
                            uint32_t size, uint32_t dma_mask) {
    /* Check if physical address is outside device capability */
    if (!dma_addr_valid(phys_addr, dma_mask)) {
        LOG_DEBUG("Bounce needed: phys_addr 0x%08lX outside DMA mask 0x%08lX",
                 phys_addr, dma_mask);
        return true;
    }
    
    /* Check if buffer crosses DMA boundary */
    if (!dma_addr_valid(phys_addr + size - 1, dma_mask)) {
        LOG_DEBUG("Bounce needed: buffer end 0x%08lX outside DMA mask 0x%08lX",
                 phys_addr + size - 1, dma_mask);
        return true;
    }
    
    /* Check for physical contiguity if large buffer */
    if (size > 4096 && mem_mgr_info.paging_enabled) {
        if (!verify_physical_contiguity(virt_addr, size)) {
            LOG_DEBUG("Bounce needed: buffer not physically contiguous");
            return true;
        }
    }
    
    return false;  /* Direct DMA is safe */
}

/**
 * @brief Get bounce buffer pool statistics
 * 
 * @param total_buffers Output: Total bounce buffers
 * @param free_buffers Output: Available bounce buffers
 * @param buffer_size Output: Size of each bounce buffer
 */
void dma_get_bounce_stats(uint32_t *total_buffers, uint32_t *free_buffers, 
                         uint32_t *buffer_size) {
    int i, free_count = 0;
    
    if (!bounce_pool_initialized) {
        if (total_buffers) *total_buffers = 0;
        if (free_buffers) *free_buffers = 0;
        if (buffer_size) *buffer_size = 0;
        return;
    }
    
    for (i = 0; i < BOUNCE_BUFFER_COUNT; i++) {
        if (bounce_buffers[i].buffer && !bounce_buffers[i].in_use) {
            free_count++;
        }
    }
    
    if (total_buffers) *total_buffers = BOUNCE_BUFFER_COUNT;
    if (free_buffers) *free_buffers = free_count;
    if (buffer_size) *buffer_size = BOUNCE_BUFFER_SIZE;
}

/**
 * @brief Get DMA allocator statistics
 * 
 * @param total_allocs Output: Total allocations
 * @param active_allocs Output: Currently active allocations
 * @param total_bytes Output: Total bytes allocated
 */
void dma_get_stats(uint32_t *total_allocs, uint32_t *active_allocs, uint32_t *total_bytes) {
    int i;
    uint32_t active = 0, bytes = 0;
    static uint32_t total = 0;
    
    for (i = 0; i < MAX_DMA_ALLOCS; i++) {
        if (dma_allocs[i].in_use) {
            active++;
            bytes += dma_allocs[i].size;
        }
    }
    
    if (total_allocs) *total_allocs = total;
    if (active_allocs) *active_allocs = active;
    if (total_bytes) *total_bytes = bytes;
}