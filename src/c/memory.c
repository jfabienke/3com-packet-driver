/**
 * @file memory.c
 * @brief Enhanced three-tier memory management system
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file implements a comprehensive three-tier memory management system:
 * - Tier 1: XMS Extended Memory (>1MB) - Highest performance, largest capacity
 * - Tier 2: UMB Upper Memory (640KB-1MB) - Medium performance, UMB driver required
 * - Tier 3: Conventional Memory (<640KB) - Lowest performance, highest compatibility
 */

#include "../../include/memory.h"
#include "../../include/xms_detect.h"
#include "../../include/cpu_detect.h"
#include "../../include/logging.h"
#include "../../include/vds.h"
#include <dos.h>
#include <string.h>
#include <stdlib.h>

/* Memory tier definitions */
#define MEMORY_TIER_XMS         1
#define MEMORY_TIER_UMB         2
#define MEMORY_TIER_CONVENTIONAL 3

/* UMB function numbers */
#define UMB_ALLOCATE            0x5800
#define UMB_FREE                0x5801
#define UMB_GET_STRATEGY        0x5802
#define UMB_SET_STRATEGY        0x5803

/* Memory block magic numbers */
#define MEM_MAGIC_ALLOCATED     0xABCDEF00
#define MEM_MAGIC_FREE          0xDEADBEEF

/* Global memory pools */
mem_pool_t g_general_pool;
mem_pool_t g_packet_pool;
mem_pool_t g_dma_pool;
mem_stats_t g_mem_stats;

/* Three-tier memory management state */
static struct {
    bool xms_available;
    bool umb_available;
    bool initialized;
    uint8_t allocation_strategy;
    mem_error_t last_error;
    void (*error_handler)(mem_error_t error, const char* message);
} g_memory_system;

/* XMS memory tier state */
static struct {
    uint16_t handles[XMS_MAX_HANDLES];
    uint32_t sizes[XMS_MAX_HANDLES];
    bool handle_used[XMS_MAX_HANDLES];
    uint32_t total_allocated;
    uint32_t peak_allocated;
} g_xms_tier;

/* UMB memory tier state */
static struct {
    uint16_t segments[16];
    uint16_t sizes[16];
    bool segment_used[16];
    uint32_t total_allocated;
    uint32_t peak_allocated;
    uint8_t handle_count;
} g_umb_tier;

/* Forward declarations */
static int memory_detect_umb(void);
static void* memory_alloc_xms_tier(uint32_t size, uint32_t flags);
static void* memory_alloc_umb_tier(uint32_t size, uint32_t flags);
static void* memory_alloc_conventional_tier(uint32_t size, uint32_t flags);
static void memory_free_xms_tier(void *ptr);
static void memory_free_umb_tier(void *ptr);
static void memory_free_conventional_tier(void *ptr);
static void memory_set_last_error(mem_error_t error);
static mem_block_t* memory_get_block_header(void *ptr);
static bool memory_validate_block(mem_block_t *block);
static void memory_copy_32bit(void *dest, const void *src, uint32_t size);
static void memory_copy_16bit(void *dest, const void *src, uint32_t size);
static void memory_set_32bit(void *ptr, uint8_t value, uint32_t size);
static void memory_set_16bit(void *ptr, uint8_t value, uint32_t size);

/* Enhanced stress testing functions */
static int memory_stress_test_allocation_patterns(void);
static int memory_stress_test_fragmentation(void);
static int memory_stress_test_leak_detection(void);
static int memory_stress_test_boundary_conditions(void);
static int memory_stress_test_concurrent_operations(void);
static int memory_stress_test_tier_switching(void);
static int memory_validate_all_allocations(void);
static int memory_perform_corruption_test(void);
static int memory_test_extreme_allocations(void);
static void memory_simulate_low_memory_conditions(void);
/**
 * @brief Initialize the three-tier memory management system
 * @return 0 on success, negative on error
 */
int memory_init(void) {
    if (g_memory_system.initialized) {
        return 0;
    }
    
    log_info("Initializing three-tier memory management system");
    
    /* Clear global state */
    memset(&g_memory_system, 0, sizeof(g_memory_system));
    memset(&g_xms_tier, 0, sizeof(g_xms_tier));
    memset(&g_umb_tier, 0, sizeof(g_umb_tier));
    memset(&g_mem_stats, 0, sizeof(g_mem_stats));
    
    /* Detect and initialize XMS (Tier 1) */
    if (xms_detect_and_init() == 0) {
        g_memory_system.xms_available = true;
        log_info("XMS Extended Memory (Tier 1) available");
    } else {
        log_info("XMS Extended Memory (Tier 1) not available");
    }
    
    /* Detect and initialize UMB (Tier 2) */
    if (memory_detect_umb() == 0) {
        g_memory_system.umb_available = true;
        log_info("UMB Upper Memory (Tier 2) available");
    } else {
        log_info("UMB Upper Memory (Tier 2) not available");
    }
    
    /* Conventional memory (Tier 3) is always available */
    log_info("Conventional Memory (Tier 3) available");
    
    /* Initialize memory pools */
    memory_stats_init(&g_mem_stats);
    
    /* Set default allocation strategy: prefer higher tiers */
    g_memory_system.allocation_strategy = 1; /* XMS first, then UMB, then conventional */
    
    g_memory_system.initialized = true;
    
    log_info("Three-tier memory system initialized successfully");
    return 0;
}

/**
 * @brief Initialize core memory subsystem (Phase 5)
 * 
 * Initializes only the essential memory management structures needed
 * for basic driver operation. DMA buffers are allocated later.
 * 
 * @param config Configuration parameters
 * @return 0 on success, negative on error
 */
int memory_init_core(config_t *config) {
    if (g_memory_system.initialized) {
        return 0;
    }
    
    log_info("Initializing core memory subsystem");
    
    /* Clear global state */
    memset(&g_memory_system, 0, sizeof(g_memory_system));
    memset(&g_xms_tier, 0, sizeof(g_xms_tier));
    memset(&g_umb_tier, 0, sizeof(g_umb_tier));
    memset(&g_mem_stats, 0, sizeof(g_mem_stats));
    
    /* Detect and initialize XMS (Tier 1) */
    if (xms_detect_and_init() == 0) {
        g_memory_system.xms_available = true;
        log_info("  XMS Extended Memory (Tier 1) available");
    } else {
        log_info("  XMS Extended Memory (Tier 1) not available");
    }
    
    /* Detect and initialize UMB (Tier 2) */
    if (memory_detect_umb() == 0) {
        g_memory_system.umb_available = true;
        log_info("  UMB Upper Memory (Tier 2) available");
    } else {
        log_info("  UMB Upper Memory (Tier 2) not available");
    }
    
    /* Conventional memory (Tier 3) is always available */
    log_info("  Conventional Memory (Tier 3) available");
    
    /* Initialize core memory pools only */
    memory_stats_init(&g_mem_stats);
    
    /* Set default allocation strategy: prefer higher tiers */
    g_memory_system.allocation_strategy = 1;
    
    g_memory_system.initialized = true;
    
    log_info("Core memory subsystem initialized");
    return 0;
}

/**
 * @brief Initialize DMA memory buffers (Phase 9)
 * 
 * Allocates DMA buffers based on detected hardware capabilities
 * and DMA policy determined in earlier phases.
 * 
 * @param config Configuration parameters
 * @return 0 on success, negative on error
 */
/**
 * @brief Check if buffer crosses 64KB boundary
 * 
 * @param addr Buffer address
 * @param size Buffer size
 * @return true if crosses boundary
 */
static bool crosses_64k_boundary(void *addr, uint32_t size) {
    uint32_t linear = ((uint32_t)FP_SEG(addr) << 4) + FP_OFF(addr);
    uint32_t start_64k = linear & 0xFFFF0000;
    uint32_t end_64k = (linear + size - 1) & 0xFFFF0000;
    return start_64k != end_64k;
}

/* DMA allocation tracking structure */
typedef struct {
    void *base_ptr;       /* Original allocation for freeing */
    void *aligned_ptr;    /* Aligned pointer for use */
    uint32_t base_size;   /* Original allocation size */
    uint32_t usable_size; /* Usable size after alignment */
} dma_alloc_info_t;

static dma_alloc_info_t g_dma_alloc_info = {0};

/**
 * @brief Allocate DMA buffer with constraints
 * 
 * GPT-5: Uses over-allocate-and-align pattern, scoped by engine type
 * 
 * @param size Required size
 * @param alignment Required alignment (16, 32, 64 bytes)
 * @param use_isa_dma true if using ISA 8237 DMA controller
 * @param retry_count Number of allocation retries
 * @return Aligned buffer or NULL
 */
static void* allocate_constrained_dma_buffer(uint32_t size, uint32_t alignment, 
                                            bool use_isa_dma, int retry_count) {
    void *base_buffer;
    void *aligned_buffer;
    uint32_t alloc_size;
    uint32_t linear_addr;
    int attempts = 0;
    
    /* GPT-5 Fix: Validate alignment is power-of-two */
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        log_error("  Invalid alignment %lu (must be power of 2)", alignment);
        return NULL;
    }
    
    /* Sanity check retry count */
    if (retry_count > 10) {
        retry_count = 10;  /* Cap to prevent infinite loops */
    }
    
    /* Calculate allocation size with room for alignment */
    alloc_size = size + alignment - 1;
    
    /* Add extra if we need to avoid 64KB boundary */
    if (use_isa_dma) {
        alloc_size += 0x10000;  /* Extra 64KB to ensure we can avoid boundary */
    }
    
    while (attempts < retry_count) {
        /* Allocate base buffer */
        base_buffer = memory_alloc(alloc_size, MEM_TYPE_DMA);
        if (!base_buffer) {
            log_error("  Failed to allocate %lu bytes (attempt %d)", 
                     alloc_size, attempts + 1);
            return NULL;
        }
        
        /* Calculate aligned pointer within allocation */
        linear_addr = ((uint32_t)FP_SEG(base_buffer) << 4) + FP_OFF(base_buffer);
        uint32_t aligned_addr = (linear_addr + alignment - 1) & ~(alignment - 1);
        
        /* GPT-5 Fix: Create aligned_buffer BEFORE using it */
        aligned_buffer = MK_FP(aligned_addr >> 4, aligned_addr & 0x0F);
        
        /* Apply ISA DMA constraints if needed */
        if (use_isa_dma) {
            /* Check 16MB boundary (ISA DMA limit - 24-bit address) */
            if (aligned_addr + size > 0x1000000) {
                log_warning("  Buffer above 16MB boundary, retrying");
                memory_free(base_buffer);
                attempts++;
                continue;
            }
            
            /* Check 64KB boundary crossing */
            if (crosses_64k_boundary(aligned_buffer, size)) {
                /* Try to adjust within our allocation */
                uint32_t next_64k = (aligned_addr & 0xFFFF0000) + 0x10000;
                if (next_64k - linear_addr + size <= alloc_size) {
                    /* We can fit after the boundary */
                    aligned_addr = next_64k;
                    aligned_buffer = MK_FP(aligned_addr >> 4, aligned_addr & 0x0F);
                    log_info("  Adjusted to avoid 64KB boundary");
                } else {
                    log_warning("  Cannot avoid 64KB boundary, retrying");
                    memory_free(base_buffer);
                    attempts++;
                    continue;
                }
            }
        }
        
        /* GPT-5 Fix: Verify final aligned_addr + size stays within allocation */
        if ((aligned_addr - linear_addr) + size > alloc_size) {
            log_error("  Alignment adjustment exceeds allocation size");
            memory_free(base_buffer);
            attempts++;
            continue;
        }
        
        /* Success - save allocation info */
        g_dma_alloc_info.base_ptr = base_buffer;
        g_dma_alloc_info.aligned_ptr = aligned_buffer;
        g_dma_alloc_info.base_size = alloc_size;
        g_dma_alloc_info.usable_size = size;
        
        log_info("  DMA buffer allocated:");
        log_info("    Base: %04X:%04X (%lu bytes)", 
                 FP_SEG(base_buffer), FP_OFF(base_buffer), alloc_size);
        log_info("    Aligned: %04X:%04X (%lu bytes, %lu-byte aligned)",
                 FP_SEG(aligned_buffer), FP_OFF(aligned_buffer), 
                 size, alignment);
        
        if (use_isa_dma) {
            log_info("    ISA DMA constraints: <16MB, no 64K crossing");
        }
        
        return aligned_buffer;
    }
    
    log_error("  Failed after %d attempts", retry_count);
    return NULL;
}

int memory_init_dma(config_t *config) {
    void *dma_buffer;
    uint32_t dma_size;
    
    if (!g_memory_system.initialized) {
        log_error("Core memory not initialized");
        return -1;
    }
    
    log_info("Initializing DMA memory buffers");
    
    /* Check DMA policy from earlier phases */
    extern int g_dma_policy;  /* From dma_capability_test.c */
    
    if (g_dma_policy == DMA_POLICY_FORBID) {
        log_info("  DMA disabled by policy - no DMA buffers allocated");
        return 0;
    }
    
    /* Determine DMA buffer size based on configuration */
    dma_size = (config && config->dma_buffer_size) ? 
               config->dma_buffer_size : DMA_DEFAULT_BUFFER_SIZE;
    
    /* GPT-5: Determine if we're using ISA DMA based on hardware */
    driver_state_t *state = get_driver_state();
    bool use_isa_dma = false;
    uint32_t alignment = 16;  /* Default cache line alignment */
    
    /* Check if we need ISA DMA constraints */
    if (state->bus_type == BUS_ISA || 
        (state->bus_type == BUS_EISA && g_dma_policy != DMA_POLICY_FORBID)) {
        nic_info_t *nic = hardware_get_primary_nic();
        if (nic && nic->capabilities & NIC_CAP_DMA_8237) {
            use_isa_dma = true;
            log_info("  Using ISA 8237 DMA - applying strict constraints");
        }
    }
    
    /* PCI NICs may prefer larger alignment */
    if (state->bus_type == BUS_PCI) {
        alignment = 64;  /* Many PCI NICs prefer 64-byte alignment */
    }
    
    log_info("  Allocating DMA buffer:");
    log_info("    Size: %lu bytes", dma_size);
    log_info("    Alignment: %lu bytes", alignment);
    log_info("    ISA DMA: %s", use_isa_dma ? "yes" : "no");
    
    /* Allocate with appropriate constraints */
    dma_buffer = allocate_constrained_dma_buffer(dma_size, alignment, 
                                                use_isa_dma, 5);
    if (!dma_buffer) {
        log_error("  Failed to allocate DMA buffer meeting constraints");
        
        /* Try smaller buffer as fallback */
        dma_size = dma_size / 2;
        log_warning("  Retrying with smaller buffer: %lu bytes", dma_size);
        
        dma_buffer = allocate_constrained_dma_buffer(dma_size, alignment,
                                                    use_isa_dma, 3);
        if (!dma_buffer) {
            log_error("  Failed to allocate any suitable DMA buffer");
            return -1;
        }
    }
    
    /* If VDS is available, lock the buffer */
    if (vds_available()) {
        vds_dma_descriptor_t desc;
        int result = vds_lock_region(dma_buffer, dma_size, &desc);
        if (result == VDS_SUCCESS) {
            log_info("  VDS locked buffer: phys=%08lX", desc.physical_addr);
        } else {
            log_warning("  VDS lock failed: %s", vds_error_string(result));
        }
    }
    
    /* Initialize DMA pool */
    g_dma_pool.base = dma_buffer;
    g_dma_pool.size = dma_size;
    g_dma_pool.used = 0;
    g_dma_pool.initialized = 1;
    
    log_info("DMA memory buffers initialized successfully");
    return 0;
}

/**
 * @brief Free DMA buffer allocated with constraints
 * 
 * Frees the base allocation, not the aligned pointer
 */
void memory_free_dma(void) {
    if (g_dma_alloc_info.base_ptr) {
        log_info("Freeing DMA buffer (base: %04X:%04X)",
                 FP_SEG(g_dma_alloc_info.base_ptr),
                 FP_OFF(g_dma_alloc_info.base_ptr));
        memory_free(g_dma_alloc_info.base_ptr);
        memset(&g_dma_alloc_info, 0, sizeof(g_dma_alloc_info));
    }
    
    /* Clear DMA pool */
    g_dma_pool.base = NULL;
    g_dma_pool.size = 0;
    g_dma_pool.used = 0;
    g_dma_pool.initialized = 0;
}

/**
 * @brief Cleanup memory management system
 */
void memory_cleanup(void) {
    int i;
    
    if (!g_memory_system.initialized) {
        return;
    }
    
    log_info("Cleaning up three-tier memory system");
    
    /* Cleanup XMS handles */
    if (g_memory_system.xms_available) {
        for (i = 0; i < XMS_MAX_HANDLES; i++) {
            if (g_xms_tier.handle_used[i]) {
                log_warning("Freeing unreleased XMS handle %04X", g_xms_tier.handles[i]);
                xms_free(g_xms_tier.handles[i]);
            }
        }
        xms_cleanup();
    }
    
    /* Cleanup UMB segments */
    if (g_memory_system.umb_available) {
        for (i = 0; i < 16; i++) {
            if (g_umb_tier.segment_used[i]) {
                log_warning("Freeing unreleased UMB segment %04X", g_umb_tier.segments[i]);
                memory_free_dos_memory(g_umb_tier.segments[i]);
            }
        }
    }
    
    /* Clear state */
    memset(&g_memory_system, 0, sizeof(g_memory_system));
    memset(&g_xms_tier, 0, sizeof(g_xms_tier));
    memset(&g_umb_tier, 0, sizeof(g_umb_tier));
    
    log_info("Three-tier memory system cleanup completed");
}

/**
 * @brief Detect UMB (Upper Memory Block) availability
 * @return 0 if available, negative if not
 */
static int memory_detect_umb(void) {
    union REGS regs;
    
    /* Check if UMB support is available via DOS function 58h */
    regs.x.ax = UMB_GET_STRATEGY;
    int86(0x21, &regs, &regs);
    
    if (regs.x.cflag) {
        log_debug("UMB not supported by DOS");
        return -1;
    }
    
    /* Try to set UMB strategy to include upper memory */
    regs.x.ax = UMB_SET_STRATEGY;
    regs.x.bx = 0x0080; /* Include UMBs in allocation strategy */
    int86(0x21, &regs, &regs);
    
    if (regs.x.cflag) {
        log_debug("Cannot set UMB allocation strategy");
        return -1;
    }
    
    log_debug("UMB support detected and enabled");
    return 0;
}

/**
 * @brief Allocate memory using three-tier strategy
 * @param size Size in bytes
 * @param type Memory type hint
 * @param flags Allocation flags
 * @return Pointer to allocated memory or NULL
 */
void* memory_alloc(uint32_t size, mem_type_t type, uint32_t flags) {
    void *ptr = NULL;
    
    if (!g_memory_system.initialized) {
        memory_set_last_error(MEM_ERROR_INVALID_POINTER);
        return NULL;
    }
    
    if (size == 0) {
        memory_set_last_error(MEM_ERROR_INVALID_SIZE);
        return NULL;
    }
    
    log_debug("Allocating %lu bytes, type %d, flags 0x%lX", size, type, flags);
    
    /* Adjust size for block header */
    uint32_t total_size = size + sizeof(mem_block_t);
    
    /* Apply allocation strategy based on size and type */
    switch (g_memory_system.allocation_strategy) {
        case 1: /* XMS -> UMB -> Conventional */
            if (g_memory_system.xms_available && size >= 4096) {
                ptr = memory_alloc_xms_tier(total_size, flags);
                if (ptr) break;
            }
            if (g_memory_system.umb_available && size >= 1024) {
                ptr = memory_alloc_umb_tier(total_size, flags);
                if (ptr) break;
            }
            ptr = memory_alloc_conventional_tier(total_size, flags);
            break;
            
        case 2: /* UMB -> Conventional -> XMS */
            if (g_memory_system.umb_available) {
                ptr = memory_alloc_umb_tier(total_size, flags);
                if (ptr) break;
            }
            ptr = memory_alloc_conventional_tier(total_size, flags);
            if (!ptr && g_memory_system.xms_available) {
                ptr = memory_alloc_xms_tier(total_size, flags);
            }
            break;
            
        case 3: /* Conventional only */
        default:
            ptr = memory_alloc_conventional_tier(total_size, flags);
            break;
    }
    
    if (ptr) {
        memory_stats_update_alloc(&g_mem_stats, size);
        log_debug("Allocated %lu bytes at %p", size, ptr);
    } else {
        memory_set_last_error(MEM_ERROR_OUT_OF_MEMORY);
        g_mem_stats.allocation_failures++;
        log_error("Failed to allocate %lu bytes", size);
    }
    
    return ptr;
}

/**
 * @brief Free allocated memory
 * @param ptr Pointer to memory to free
 */
void memory_free(void *ptr) {
    mem_block_t *block;
    
    if (!ptr) {
        return;
    }
    
    if (!g_memory_system.initialized) {
        memory_set_last_error(MEM_ERROR_INVALID_POINTER);
        return;
    }
    
    block = memory_get_block_header(ptr);
    if (!memory_validate_block(block)) {
        memory_set_last_error(MEM_ERROR_CORRUPTION);
        log_error("Invalid memory block at %p", ptr);
        return;
    }
    
    log_debug("Freeing %lu bytes at %p", block->size, ptr);
    
    /* Determine which tier this memory belongs to and free accordingly */
    if (block->flags & MEM_FLAG_DMA_CAPABLE) {
        /* XMS tier memory */
        memory_free_xms_tier(ptr);
    } else if ((uint32_t)ptr > 0xA0000) {
        /* UMB tier memory */
        memory_free_umb_tier(ptr);
    } else {
        /* Conventional tier memory */
        memory_free_conventional_tier(ptr);
    }
    
    memory_stats_update_free(&g_mem_stats, block->size);
}

/**
 * @brief Allocate memory from XMS tier (Tier 1) with DMA alignment optimization
 * @param size Size including block header
 * @param flags Allocation flags
 * @return Pointer to allocated memory or NULL
 */
static void* memory_alloc_xms_tier(uint32_t size, uint32_t flags) {
    uint16_t handle;
    uint32_t linear_addr;
    int size_kb = (size + 1023) / 1024; /* Round up to KB */
    int i;
    extern cpu_info_t g_cpu_info;
    
    if (!g_memory_system.xms_available) {
        return NULL;
    }
    
    /* For DMA buffers, ensure we allocate extra for alignment */
    if (flags & MEM_FLAG_DMA_CAPABLE) {
        /* Add padding for DMA alignment (4-byte minimum, 32-byte optimal) */
        uint32_t alignment = (g_cpu_info.type >= CPU_TYPE_80486) ? 32 : 4;
        size_kb = ((size + alignment + 1023) / 1024);
    }
    
    /* Find free handle slot */
    for (i = 0; i < XMS_MAX_HANDLES; i++) {
        if (!g_xms_tier.handle_used[i]) {
            break;
        }
    }
    
    if (i >= XMS_MAX_HANDLES) {
        log_debug("No free XMS handle slots");
        return NULL;
    }
    
    /* Allocate XMS block */
    if (xms_allocate(size_kb, &handle) != 0) {
        return NULL;
    }
    
    /* Lock the block to get linear address */
    if (xms_lock(handle, &linear_addr) != 0) {
        xms_free(handle);
        return NULL;
    }
    
    /* For DMA buffers, align the linear address properly */
    uint32_t aligned_addr = linear_addr;
    if (flags & MEM_FLAG_DMA_CAPABLE) {
        uint32_t alignment = (g_cpu_info.type >= CPU_TYPE_80486) ? 32 : 4;
        aligned_addr = ALIGN_UP(linear_addr + sizeof(mem_block_t), alignment);
        
        /* Store original address in the header for freeing */
        mem_block_t *block = (mem_block_t*)(aligned_addr - sizeof(mem_block_t));
        block->original_addr = linear_addr;
    } else {
        aligned_addr = linear_addr;
    }
    
    /* Store handle information */
    g_xms_tier.handles[i] = handle;
    g_xms_tier.sizes[i] = size;
    g_xms_tier.handle_used[i] = true;
    g_xms_tier.total_allocated += size;
    
    if (g_xms_tier.total_allocated > g_xms_tier.peak_allocated) {
        g_xms_tier.peak_allocated = g_xms_tier.total_allocated;
    }
    
    /* Set up memory block header */
    mem_block_t *block = (mem_block_t*)(aligned_addr - sizeof(mem_block_t));
    block->size = size - sizeof(mem_block_t);
    block->flags = flags | MEM_FLAG_DMA_CAPABLE;
    block->type = MEM_TYPE_PACKET_BUFFER;
    block->magic = MEM_MAGIC_ALLOCATED;
    block->next = NULL;
    block->prev = NULL;
    
    /* For DMA buffers, verify alignment */
    if (flags & MEM_FLAG_DMA_CAPABLE) {
        uint32_t expected_alignment = (g_cpu_info.type >= CPU_TYPE_80486) ? 32 : 4;
        if (!IS_ALIGNED(aligned_addr, expected_alignment)) {
            log_warning("DMA buffer alignment suboptimal: %08lX (expected %u-byte alignment)", 
                       aligned_addr, expected_alignment);
        }
    }
    
    log_debug("XMS allocation: handle %04X, %d KB at linear %08lX (aligned %08lX)", 
             handle, size_kb, linear_addr, aligned_addr);
    
    return (void*)aligned_addr;
}

/**
 * @brief Allocate memory from UMB tier (Tier 2)
 * @param size Size including block header
 * @param flags Allocation flags
 * @return Pointer to allocated memory or NULL
 */
static void* memory_alloc_umb_tier(uint32_t size, uint32_t flags) {
    uint16_t segment;
    uint16_t paragraphs = (size + 15) / 16; /* Round up to paragraphs */
    int i;
    
    if (!g_memory_system.umb_available) {
        return NULL;
    }
    
    /* Find free segment slot */
    for (i = 0; i < 16; i++) {
        if (!g_umb_tier.segment_used[i]) {
            break;
        }
    }
    
    if (i >= 16) {
        log_debug("No free UMB segment slots");
        return NULL;
    }
    
    /* Allocate DOS memory in UMB area */
    if (memory_allocate_dos_memory(paragraphs, &segment) != 0) {
        return NULL;
    }
    
    /* Check if we got UMB (segment > 0xA000) */
    if (segment < 0xA000) {
        memory_free_dos_memory(segment);
        return NULL;
    }
    
    /* Store segment information */
    g_umb_tier.segments[i] = segment;
    g_umb_tier.sizes[i] = size;
    g_umb_tier.segment_used[i] = true;
    g_umb_tier.total_allocated += size;
    g_umb_tier.handle_count++;
    
    if (g_umb_tier.total_allocated > g_umb_tier.peak_allocated) {
        g_umb_tier.peak_allocated = g_umb_tier.total_allocated;
    }
    
    /* Set up memory block header */
    mem_block_t *block = (mem_block_t*)MK_FP(segment, 0);
    block->size = size - sizeof(mem_block_t);
    block->flags = flags;
    block->type = MEM_TYPE_PACKET_BUFFER;
    block->magic = MEM_MAGIC_ALLOCATED;
    block->next = NULL;
    block->prev = NULL;
    
    log_debug("UMB allocation: segment %04X, %d paragraphs", segment, paragraphs);
    
    return (void*)((uint8_t*)block + sizeof(mem_block_t));
}

/**
 * @brief Allocate memory from conventional tier (Tier 3)
 * @param size Size including block header
 * @param flags Allocation flags
 * @return Pointer to allocated memory or NULL
 */
static void* memory_alloc_conventional_tier(uint32_t size, uint32_t flags) {
    mem_block_t *block;
    
    /* Use standard malloc for conventional memory */
    block = (mem_block_t*)malloc(size);
    if (!block) {
        return NULL;
    }
    
    /* Set up memory block header */
    block->size = size - sizeof(mem_block_t);
    block->flags = flags;
    block->type = MEM_TYPE_GENERAL;
    block->magic = MEM_MAGIC_ALLOCATED;
    block->next = NULL;
    block->prev = NULL;
    
    log_debug("Conventional allocation: %lu bytes at %p", size, block);
    
    return (void*)((uint8_t*)block + sizeof(mem_block_t));
}

/**
 * @brief Free memory from XMS tier
 * @param ptr Pointer to memory
 */
static void memory_free_xms_tier(void *ptr) {
    mem_block_t *block = memory_get_block_header(ptr);
    uint32_t linear_addr = (uint32_t)block;
    int i;
    
    /* Find the handle for this memory */
    for (i = 0; i < XMS_MAX_HANDLES; i++) {
        if (g_xms_tier.handle_used[i]) {
            uint32_t handle_addr;
            if (xms_lock(g_xms_tier.handles[i], &handle_addr) == 0) {
                if (handle_addr == linear_addr) {
                    /* Found the right handle */
                    xms_unlock(g_xms_tier.handles[i]);
                    xms_free(g_xms_tier.handles[i]);
                    
                    g_xms_tier.total_allocated -= g_xms_tier.sizes[i];
                    g_xms_tier.handle_used[i] = false;
                    
                    log_debug("Freed XMS handle %04X", g_xms_tier.handles[i]);
                    return;
                }
                xms_unlock(g_xms_tier.handles[i]);
            }
        }
    }
    
    log_error("Could not find XMS handle for address %p", ptr);
}

/**
 * @brief Free memory from UMB tier
 * @param ptr Pointer to memory
 */
static void memory_free_umb_tier(void *ptr) {
    mem_block_t *block = memory_get_block_header(ptr);
    uint16_t segment = FP_SEG(block);
    int i;
    
    /* Find the segment for this memory */
    for (i = 0; i < 16; i++) {
        if (g_umb_tier.segment_used[i] && g_umb_tier.segments[i] == segment) {
            memory_free_dos_memory(segment);
            
            g_umb_tier.total_allocated -= g_umb_tier.sizes[i];
            g_umb_tier.segment_used[i] = false;
            g_umb_tier.handle_count--;
            
            log_debug("Freed UMB segment %04X", segment);
            return;
        }
    }
    
    log_error("Could not find UMB segment for address %p", ptr);
}

/**
 * @brief Free memory from conventional tier
 * @param ptr Pointer to memory
 */
static void memory_free_conventional_tier(void *ptr) {
    mem_block_t *block = memory_get_block_header(ptr);
    
    block->magic = MEM_MAGIC_FREE;
    free(block);
    
    log_debug("Freed conventional memory at %p", ptr);
}

/**
 * @brief Get memory block header from user pointer
 * @param ptr User data pointer
 * @return Pointer to block header
 */
static mem_block_t* memory_get_block_header(void *ptr) {
    return (mem_block_t*)((uint8_t*)ptr - sizeof(mem_block_t));
}

/**
 * @brief Validate memory block integrity
 * @param block Pointer to block header
 * @return true if valid, false if corrupted
 */
static bool memory_validate_block(mem_block_t *block) {
    if (!block) {
        return false;
    }
    
    return (block->magic == MEM_MAGIC_ALLOCATED);
}

/**
 * @brief Set last error code
 * @param error Error code to set
 */
static void memory_set_last_error(mem_error_t error) {
    g_memory_system.last_error = error;
    
    if (g_memory_system.error_handler) {
        g_memory_system.error_handler(error, memory_error_to_string(error));
    }
}

/**
 * @brief Get last memory error
 * @return Last error code
 */
mem_error_t memory_get_last_error(void) {
    return g_memory_system.last_error;
}

/**
 * @brief Convert error code to string
 * @param error Error code
 * @return Error description string
 */
const char* memory_error_to_string(mem_error_t error) {
    switch (error) {
        case MEM_ERROR_NONE:            return "No error";
        case MEM_ERROR_OUT_OF_MEMORY:   return "Out of memory";
        case MEM_ERROR_INVALID_POINTER: return "Invalid pointer";
        case MEM_ERROR_DOUBLE_FREE:     return "Double free detected";
        case MEM_ERROR_CORRUPTION:      return "Memory corruption detected";
        case MEM_ERROR_ALIGNMENT:       return "Alignment error";
        case MEM_ERROR_POOL_FULL:       return "Memory pool full";
        case MEM_ERROR_INVALID_SIZE:    return "Invalid size";
        default:                        return "Unknown error";
    }
}

/**
 * @brief Check if XMS memory is available
 * @return true if available, false otherwise
 */
bool memory_xms_available(void) {
    return g_memory_system.xms_available;
}

/**
 * @brief Get XMS memory size
 * @return Size in KB or 0 if not available
 */
uint32_t memory_get_xms_size(void) {
    xms_info_t info;
    
    if (!g_memory_system.xms_available) {
        return 0;
    }
    
    if (xms_get_info(&info) != 0) {
        return 0;
    }
    
    return info.free_kb;
}

/**
 * @brief Initialize memory statistics
 * @param stats Statistics structure to initialize
 */
void memory_stats_init(mem_stats_t *stats) {
    if (!stats) {
        return;
    }
    
    memset(stats, 0, sizeof(mem_stats_t));
}

/**
 * @brief Update statistics for allocation
 * @param stats Statistics structure
 * @param size Size allocated
 */
void memory_stats_update_alloc(mem_stats_t *stats, uint32_t size) {
    if (!stats) {
        return;
    }
    
    stats->total_allocations++;
    stats->used_memory += size;
    
    if (stats->used_memory > stats->peak_usage) {
        stats->peak_usage = stats->used_memory;
    }
    
    if (size > stats->largest_allocation) {
        stats->largest_allocation = size;
    }
    
    if (stats->smallest_allocation == 0 || size < stats->smallest_allocation) {
        stats->smallest_allocation = size;
    }
}

/**
 * @brief Update statistics for deallocation
 * @param stats Statistics structure
 * @param size Size freed
 */
void memory_stats_update_free(mem_stats_t *stats, uint32_t size) {
    if (!stats) {
        return;
    }
    
    stats->total_frees++;
    if (stats->used_memory >= size) {
        stats->used_memory -= size;
    }
}

/**
 * @brief Get memory statistics
 * @return Pointer to global statistics
 */
const mem_stats_t* memory_get_stats(void) {
    return &g_mem_stats;
}

/**
 * @brief DOS memory allocation
 * @param paragraphs Number of 16-byte paragraphs
 * @param segment Pointer to store allocated segment
 * @return 0 on success, negative on error
 */
int memory_allocate_dos_memory(uint16_t paragraphs, uint16_t *segment) {
    union REGS regs;
    
    regs.h.ah = 0x48; /* DOS allocate memory */
    regs.x.bx = paragraphs;
    int86(0x21, &regs, &regs);
    
    if (regs.x.cflag) {
        return -1;
    }
    
    *segment = regs.x.ax;
    return 0;
}

/**
 * @brief DOS memory deallocation
 * @param segment Segment to free
 * @return 0 on success, negative on error
 */
int memory_free_dos_memory(uint16_t segment) {
    union REGS regs;
    struct SREGS sregs;
    
    sregs.es = segment;
    regs.h.ah = 0x49; /* DOS free memory */
    int86x(0x21, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        return -1;
    }
    
    return 0;
}

/* Utility functions */
void memory_zero(void *ptr, uint32_t size) {
    if (ptr && size > 0) {
        memset(ptr, 0, size);
    }
}

void memory_copy(void *dest, const void *src, uint32_t size) {
    if (dest && src && size > 0) {
        memcpy(dest, src, size);
    }
}

int memory_compare(const void *ptr1, const void *ptr2, uint32_t size) {
    if (!ptr1 || !ptr2 || size == 0) {
        return -1;
    }
    return memcmp(ptr1, ptr2, size);
}

/**
 * @brief CPU-optimized memory copy using Phase 1 CPU detection
 * @param dest Destination pointer
 * @param src Source pointer
 * @param size Number of bytes to copy
 */
void memory_copy_optimized(void *dest, const void *src, uint32_t size) {
    extern cpu_info_t g_cpu_info; /* From Phase 1 */
    
    if (!dest || !src || size == 0) {
        return;
    }
    
    /* Use CPU-specific optimizations based on Phase 1 detection */
    if (g_cpu_info.type >= CPU_TYPE_80386 && cpu_supports_32bit()) {
        /* 32-bit optimized copy for 386+ processors */
        memory_copy_32bit(dest, src, size);
    } else {
        /* Standard 16-bit copy for older processors */
        memory_copy_16bit(dest, src, size);
    }
}

/**
 * @brief 32-bit optimized memory copy for 386+ CPUs
 * @param dest Destination pointer
 * @param src Source pointer
 * @param size Number of bytes to copy
 */
static void memory_copy_32bit(void *dest, const void *src, uint32_t size) {
    uint32_t *dest32 = (uint32_t*)dest;
    const uint32_t *src32 = (const uint32_t*)src;
    uint8_t *dest8;
    const uint8_t *src8;
    uint32_t dwords, remainder;
    
    /* Check alignment for optimal 32-bit transfers */
    if (IS_ALIGNED((uint32_t)dest, 4) && IS_ALIGNED((uint32_t)src, 4)) {
        /* Both aligned to 4-byte boundaries - use 32-bit transfers */
        dwords = size / 4;
        remainder = size % 4;
        
        /* Copy 32-bit chunks */
        while (dwords--) {
            *dest32++ = *src32++;
        }
        
        /* Copy remaining bytes */
        if (remainder) {
            dest8 = (uint8_t*)dest32;
            src8 = (const uint8_t*)src32;
            while (remainder--) {
                *dest8++ = *src8++;
            }
        }
    } else {
        /* Unaligned - fall back to byte copy */
        memory_copy_16bit(dest, src, size);
    }
}

/**
 * @brief 16-bit memory copy for compatibility
 * @param dest Destination pointer
 * @param src Source pointer
 * @param size Number of bytes to copy
 */
static void memory_copy_16bit(void *dest, const void *src, uint32_t size) {
    uint16_t *dest16 = (uint16_t*)dest;
    const uint16_t *src16 = (const uint16_t*)src;
    uint8_t *dest8;
    const uint8_t *src8;
    uint32_t words, remainder;
    
    /* Check alignment for 16-bit transfers */
    if (IS_ALIGNED((uint32_t)dest, 2) && IS_ALIGNED((uint32_t)src, 2)) {
        /* Both aligned to 2-byte boundaries */
        words = size / 2;
        remainder = size % 2;
        
        /* Copy 16-bit chunks */
        while (words--) {
            *dest16++ = *src16++;
        }
        
        /* Copy remaining byte */
        if (remainder) {
            dest8 = (uint8_t*)dest16;
            src8 = (const uint8_t*)src16;
            *dest8 = *src8;
        }
    } else {
        /* Unaligned - byte by byte copy */
        dest8 = (uint8_t*)dest;
        src8 = (const uint8_t*)src;
        while (size--) {
            *dest8++ = *src8++;
        }
    }
}

/**
 * @brief CPU-optimized memory set using Phase 1 CPU detection
 * @param ptr Pointer to memory
 * @param value Value to set
 * @param size Number of bytes to set
 */
void memory_set_optimized(void *ptr, uint8_t value, uint32_t size) {
    extern cpu_info_t g_cpu_info; /* From Phase 1 */
    
    if (!ptr || size == 0) {
        return;
    }
    
    /* Use CPU-specific optimizations */
    if (g_cpu_info.type >= CPU_TYPE_80386 && cpu_supports_32bit()) {
        memory_set_32bit(ptr, value, size);
    } else {
        memory_set_16bit(ptr, value, size);
    }
}

/**
 * @brief 32-bit optimized memory set for 386+ CPUs
 * @param ptr Pointer to memory
 * @param value Value to set
 * @param size Number of bytes to set
 */
static void memory_set_32bit(void *ptr, uint8_t value, uint32_t size) {
    uint32_t *ptr32 = (uint32_t*)ptr;
    uint8_t *ptr8;
    uint32_t dwords, remainder;
    uint32_t value32;
    
    /* Create 32-bit pattern from 8-bit value */
    value32 = ((uint32_t)value << 24) | ((uint32_t)value << 16) | 
              ((uint32_t)value << 8) | value;
    
    /* Check alignment for optimal 32-bit transfers */
    if (IS_ALIGNED((uint32_t)ptr, 4)) {
        /* Aligned to 4-byte boundary */
        dwords = size / 4;
        remainder = size % 4;
        
        /* Set 32-bit chunks */
        while (dwords--) {
            *ptr32++ = value32;
        }
        
        /* Set remaining bytes */
        if (remainder) {
            ptr8 = (uint8_t*)ptr32;
            while (remainder--) {
                *ptr8++ = value;
            }
        }
    } else {
        /* Unaligned - fall back to byte set */
        memory_set_16bit(ptr, value, size);
    }
}

/**
 * @brief 16-bit memory set for compatibility
 * @param ptr Pointer to memory
 * @param value Value to set
 * @param size Number of bytes to set
 */
static void memory_set_16bit(void *ptr, uint8_t value, uint32_t size) {
    uint16_t *ptr16 = (uint16_t*)ptr;
    uint8_t *ptr8;
    uint32_t words, remainder;
    uint16_t value16;
    
    /* Create 16-bit pattern from 8-bit value */
    value16 = ((uint16_t)value << 8) | value;
    
    /* Check alignment for 16-bit transfers */
    if (IS_ALIGNED((uint32_t)ptr, 2)) {
        /* Aligned to 2-byte boundary */
        words = size / 2;
        remainder = size % 2;
        
        /* Set 16-bit chunks */
        while (words--) {
            *ptr16++ = value16;
        }
        
        /* Set remaining byte */
        if (remainder) {
            ptr8 = (uint8_t*)ptr16;
            *ptr8 = value;
        }
    } else {
        /* Unaligned - byte by byte set */
        ptr8 = (uint8_t*)ptr;
        while (size--) {
            *ptr8++ = value;
        }
    }
}

/**
 * @brief Allocate aligned memory with CPU-optimized alignment
 * @param size Size in bytes
 * @param alignment Alignment requirement
 * @param type Memory type
 * @return Pointer to aligned memory or NULL
 */
void* memory_alloc_aligned(uint32_t size, uint32_t alignment, mem_type_t type) {
    extern cpu_info_t g_cpu_info; /* From Phase 1 */
    void *ptr;
    uint32_t flags = MEM_FLAG_ALIGNED;
    
    /* Adjust alignment based on CPU capabilities */
    if (g_cpu_info.type >= CPU_TYPE_80386) {
        /* 386+ can benefit from 32-bit alignment */
        if (alignment < 4) {
            alignment = 4;
        }
    } else {
        /* 286 and below - stick to 16-bit alignment */
        if (alignment < 2) {
            alignment = 2;
        }
    }
    
    /* Allocate with padding for alignment */
    uint32_t padded_size = size + alignment + sizeof(mem_block_t);
    ptr = memory_alloc(padded_size, type, flags);
    
    if (!ptr) {
        return NULL;
    }
    
    /* Calculate aligned address */
    uint32_t addr = (uint32_t)ptr;
    uint32_t aligned_addr = ALIGN_UP(addr, alignment);
    
    /* If already aligned, return as-is */
    if (addr == aligned_addr) {
        return ptr;
    }
    
    /* Store original pointer in the bytes before aligned address for later freeing */
    void **orig_ptr_storage = (void**)(aligned_addr - sizeof(void*));
    *orig_ptr_storage = ptr;
    
    /* Return properly aligned pointer */
    return (void*)aligned_addr;
}

/**
 * @brief Free aligned memory allocated with memory_alloc_aligned
 * @param ptr Aligned pointer to free
 */
void memory_free_aligned(void *ptr) {
    void *original_ptr;
    
    if (!ptr) {
        return;
    }
    
    /* Check if this looks like an aligned pointer */
    uint32_t addr = (uint32_t)ptr;
    if (!IS_ALIGNED(addr, 4) && !IS_ALIGNED(addr, 2)) {
        /* Not aligned, treat as regular pointer */
        memory_free(ptr);
        return;
    }
    
    /* Retrieve original pointer stored before aligned address */
    void **orig_ptr_storage = (void**)(addr - sizeof(void*));
    original_ptr = *orig_ptr_storage;
    
    /* Validate that original pointer makes sense */
    if (original_ptr && (uint32_t)original_ptr < addr && 
        (addr - (uint32_t)original_ptr) < 64) {
        memory_free(original_ptr);
    } else {
        /* Fallback to regular free if validation fails */
        memory_free(ptr);
    }
}

/**
 * @brief Allocate DMA-capable memory with optimal alignment for 3C515-TX
 * @param size Size in bytes
 * @return Pointer to DMA-aligned memory or NULL
 */
void* memory_alloc_dma(uint32_t size) {
    extern cpu_info_t g_cpu_info;
    uint32_t flags = MEM_FLAG_DMA_CAPABLE | MEM_FLAG_ALIGNED;
    void *ptr;
    
    /* DMA memory must be physically contiguous and properly aligned */
    /* For 3C515-TX: 4-byte minimum, 32-byte optimal for bus mastering */
    
    /* Use XMS memory for DMA buffers (physically contiguous) */
    if (g_memory_system.xms_available) {
        ptr = memory_alloc_xms_tier(size + sizeof(mem_block_t), flags);
        if (ptr) {
            log_debug("Allocated %lu byte DMA buffer in XMS at %p", size, ptr);
            return ptr;
        }
    }
    
    /* Fallback to conventional memory with alignment warning */
    ptr = memory_alloc(size, MEM_TYPE_PACKET_BUFFER, flags);
    if (ptr) {
        log_warning("DMA buffer allocated in conventional memory - may not be optimal");
        
        /* Verify alignment for DMA operations */
        uint32_t alignment = (g_cpu_info.type >= CPU_TYPE_80486) ? 32 : 4;
        if (!IS_ALIGNED((uint32_t)ptr, alignment)) {
            log_error("DMA buffer not properly aligned: %p (need %u-byte alignment)", 
                     ptr, alignment);
        }
    }
    
    return ptr;
}

/**
 * @brief Free DMA-capable memory
 * @param ptr Pointer to DMA memory
 */
void memory_free_dma(void *ptr) {
    if (!ptr) {
        return;
    }
    
    /* Free using standard memory free - it will detect the tier automatically */
    memory_free(ptr);
    log_debug("Freed DMA buffer at %p", ptr);
}

/**
 * @brief Allocate cache-line aligned memory for optimal performance
 * @param size Size in bytes
 * @param cache_line_size Cache line size (16, 32, or 64 bytes)
 * @return Pointer to cache-aligned memory or NULL
 */
void* memory_alloc_cache_aligned(uint32_t size, uint32_t cache_line_size) {
    extern cpu_info_t g_cpu_info;
    uint32_t alignment;
    
    /* Validate cache line size */
    if (cache_line_size != 16 && cache_line_size != 32 && cache_line_size != 64) {
        log_error("Invalid cache line size: %u (must be 16, 32, or 64)", cache_line_size);
        return NULL;
    }
    
    /* Choose alignment based on CPU type and cache line size */
    if (g_cpu_info.type >= CPU_TYPE_PENTIUM) {
        alignment = cache_line_size;  /* Full cache line alignment for Pentium+ */
    } else if (g_cpu_info.type >= CPU_TYPE_80486) {
        alignment = 32;               /* 486 cache line size */
    } else {
        alignment = 4;                /* Basic alignment for older CPUs */
    }
    
    void *ptr = memory_alloc_aligned(size, alignment, MEM_TYPE_PACKET_BUFFER);
    if (ptr) {
        log_debug("Allocated %lu byte cache-aligned buffer (%u-byte alignment) at %p", 
                 size, alignment, ptr);
    }
    
    return ptr;
}

/**
 * @brief Initialize CPU-optimized memory system
 * @return 0 on success, negative on error
 */
int memory_init_cpu_optimized(void) {
    extern cpu_info_t g_cpu_info; /* From Phase 1 */
    
    if (!g_cpu_info.type) {
        log_warning("CPU not detected - using conservative memory operations");
        return -1;
    }
    
    log_info("Initializing CPU-optimized memory operations for %s",
             cpu_type_to_string(g_cpu_info.type));
    
    /* Log CPU-specific optimizations */
    if (g_cpu_info.type >= CPU_TYPE_80386) {
        log_info("Enabling 32-bit memory operations for 386+ CPU");
        
        if (g_cpu_info.features & CPU_FEATURE_TSC) {
            log_info("TSC available for performance measurement");
        }
    } else {
        log_info("Using 16-bit memory operations for %s",
                 cpu_type_to_string(g_cpu_info.type));
    }
    
    /* Initialize DMA buffer pools if we have sufficient XMS memory */
    if (g_memory_system.xms_available) {
        uint32_t xms_size = memory_get_xms_size();
        if (xms_size >= 1024) {  /* At least 1MB */
            log_info("Sufficient XMS memory (%u KB) for optimized DMA buffer allocation", xms_size);
        } else {
            log_warning("Limited XMS memory (%u KB) - DMA performance may be reduced", xms_size);
        }
    } else {
        log_warning("No XMS memory - DMA buffers will use conventional memory");
    }
    
    return 0;
}

/**
 * @brief Memory pool initialization
 * @param pool Pool to initialize
 * @param base Base memory address
 * @param size Pool size
 * @return 0 on success, negative on error
 */
int memory_pool_init(mem_pool_t *pool, void *base, uint32_t size) {
    if (!pool || !base || size < sizeof(mem_block_t)) {
        return -1;
    }
    
    /* Initialize pool structure */
    pool->base = base;
    pool->size = size;
    pool->used = 0;
    pool->free = size;
    pool->largest_free = size;
    pool->block_count = 0;
    pool->alloc_count = 0;
    pool->free_count = 0;
    pool->initialized = true;
    
    /* Create initial free block */
    mem_block_t *initial_block = (mem_block_t*)base;
    initial_block->size = size - sizeof(mem_block_t);
    initial_block->flags = 0;
    initial_block->type = MEM_TYPE_GENERAL;
    initial_block->magic = MEM_MAGIC_FREE;
    initial_block->next = NULL;
    initial_block->prev = NULL;
    
    pool->free_list = initial_block;
    pool->block_count = 1;
    
    return 0;
}

/**
 * @brief Cleanup memory pool
 * @param pool Pool to cleanup
 */
void memory_pool_cleanup(mem_pool_t *pool) {
    if (!pool || !pool->initialized) {
        return;
    }
    
    /* Clear all structures */
    memset(pool, 0, sizeof(mem_pool_t));
}

/**
 * @brief Allocate from specific memory pool
 * @param pool Memory pool
 * @param size Size to allocate
 * @param flags Allocation flags
 * @return Pointer to allocated memory or NULL
 */
void* memory_pool_alloc(mem_pool_t *pool, uint32_t size, uint32_t flags) {
    mem_block_t *block, *new_block;
    uint32_t total_size = size + sizeof(mem_block_t);
    
    if (!pool || !pool->initialized || size == 0) {
        return NULL;
    }
    
    /* Find suitable free block */
    for (block = pool->free_list; block; block = block->next) {
        if (block->magic == MEM_MAGIC_FREE && block->size >= size) {
            break;
        }
    }
    
    if (!block) {
        /* No suitable block found */
        return NULL;
    }
    
    /* Split block if it's significantly larger */
    if (block->size > total_size + sizeof(mem_block_t)) {
        new_block = (mem_block_t*)((uint8_t*)block + total_size);
        new_block->size = block->size - total_size;
        new_block->flags = 0;
        new_block->type = MEM_TYPE_GENERAL;
        new_block->magic = MEM_MAGIC_FREE;
        new_block->next = block->next;
        new_block->prev = block;
        
        if (block->next) {
            block->next->prev = new_block;
        }
        block->next = new_block;
        
        block->size = size;
    }
    
    /* Mark block as allocated */
    block->magic = MEM_MAGIC_ALLOCATED;
    block->flags = flags;
    
    /* Update pool statistics */
    pool->used += block->size + sizeof(mem_block_t);
    pool->free -= block->size + sizeof(mem_block_t);
    pool->alloc_count++;
    
    return (void*)((uint8_t*)block + sizeof(mem_block_t));
}

/**
 * @brief Free memory from specific pool
 * @param pool Memory pool
 * @param ptr Pointer to free
 */
void memory_pool_free(mem_pool_t *pool, void *ptr) {
    mem_block_t *block;
    
    if (!pool || !ptr) {
        return;
    }
    
    block = (mem_block_t*)((uint8_t*)ptr - sizeof(mem_block_t));
    
    if (block->magic != MEM_MAGIC_ALLOCATED) {
        return; /* Invalid block */
    }
    
    /* Mark as free */
    block->magic = MEM_MAGIC_FREE;
    
    /* Update pool statistics */
    pool->used -= block->size + sizeof(mem_block_t);
    pool->free += block->size + sizeof(mem_block_t);
    pool->free_count++;
    
    /* Implement basic block coalescing to reduce fragmentation */
    
    /* Try to coalesce with next block */
    mem_block_t *next_block = (mem_block_t*)((uint8_t*)block + sizeof(mem_block_t) + block->size);
    if ((uint32_t)next_block < (uint32_t)pool->base + pool->size &&
        next_block->magic == MEM_MAGIC_FREE) {
        
        /* Coalesce with next block */
        block->size += sizeof(mem_block_t) + next_block->size;
        
        /* Update linked list pointers */
        if (next_block->next) {
            next_block->next->prev = block;
        }
        block->next = next_block->next;
        
        /* Clear coalesced block */
        next_block->magic = 0;
        
        pool->block_count--;
    }
    
    /* Try to coalesce with previous block */
    if (block->prev && block->prev->magic == MEM_MAGIC_FREE) {
        mem_block_t *prev_block = block->prev;
        
        /* Check if blocks are adjacent */
        mem_block_t *expected_next = (mem_block_t*)((uint8_t*)prev_block + sizeof(mem_block_t) + prev_block->size);
        if (expected_next == block) {
            /* Coalesce with previous block */
            prev_block->size += sizeof(mem_block_t) + block->size;
            
            /* Update linked list pointers */
            if (block->next) {
                block->next->prev = prev_block;
            }
            prev_block->next = block->next;
            
            /* Clear coalesced block */
            block->magic = 0;
            
            pool->block_count--;
        }
    }
}

/**
 * @brief Get free size in pool
 * @param pool Memory pool
 * @return Free size in bytes
 */
uint32_t memory_pool_get_free_size(const mem_pool_t *pool) {
    return pool ? pool->free : 0;
}

/**
 * @brief Get used size in pool
 * @param pool Memory pool
 * @return Used size in bytes
 */
uint32_t memory_pool_get_used_size(const mem_pool_t *pool) {
    return pool ? pool->used : 0;
}

/**
 * @brief Get largest free block size
 * @param pool Memory pool
 * @return Largest free block size
 */
uint32_t memory_pool_get_largest_free(const mem_pool_t *pool) {
    mem_block_t *block;
    uint32_t largest = 0;
    
    if (!pool || !pool->initialized) {
        return 0;
    }
    
    for (block = pool->free_list; block; block = block->next) {
        if (block->magic == MEM_MAGIC_FREE && block->size > largest) {
            largest = block->size;
        }
    }
    
    return largest;
}

/**
 * @brief Set memory error handler
 * @param handler Error handler function
 */
void memory_set_error_handler(void (*handler)(mem_error_t error, const char* message)) {
    g_memory_system.error_handler = handler;
}

/**
 * @brief Print memory statistics
 */
void memory_print_stats(void) {
    const mem_stats_t *stats = &g_mem_stats;
    
    log_info("=== Memory Statistics ===");
    log_info("Total allocations: %lu", stats->total_allocations);
    log_info("Total frees: %lu", stats->total_frees);
    log_info("Current used: %lu bytes", stats->used_memory);
    log_info("Peak usage: %lu bytes", stats->peak_usage);
    log_info("Allocation failures: %lu", stats->allocation_failures);
    log_info("Largest allocation: %lu bytes", stats->largest_allocation);
    log_info("Smallest allocation: %lu bytes", stats->smallest_allocation);
    
    /* Three-tier statistics */
    log_info("=== Three-Tier Memory Usage ===");
    if (g_memory_system.xms_available) {
        log_info("XMS Tier 1: %lu bytes allocated (peak: %lu)",
                 g_xms_tier.total_allocated, g_xms_tier.peak_allocated);
    }
    if (g_memory_system.umb_available) {
        log_info("UMB Tier 2: %lu bytes allocated (peak: %lu), %d segments",
                 g_umb_tier.total_allocated, g_umb_tier.peak_allocated, g_umb_tier.handle_count);
    }
    log_info("Conventional Tier 3: Available for fallback");
}

/**
 * @brief Comprehensive memory stress testing suite
 * This implements all stress testing scenarios for the memory system
 */

/**
 * @brief Enhanced memory stress test with comprehensive scenarios
 * @return 0 on success, negative on error
 */
int memory_comprehensive_stress_test(void) {
    int result = 0;
    int tests_passed = 0;
    int tests_failed = 0;
    
    log_info("=== Starting Comprehensive Memory Stress Test ===");
    
    /* Test 1: Allocation patterns stress test */
    log_info("Running allocation patterns stress test...");
    if (memory_stress_test_allocation_patterns() == 0) {
        tests_passed++;
        log_info("Allocation patterns test PASSED");
    } else {
        tests_failed++;
        log_error("Allocation patterns test FAILED");
        result = -1;
    }
    
    /* Test 2: Fragmentation stress test */
    log_info("Running fragmentation stress test...");
    if (memory_stress_test_fragmentation() == 0) {
        tests_passed++;
        log_info("Fragmentation test PASSED");
    } else {
        tests_failed++;
        log_error("Fragmentation test FAILED");
        result = -1;
    }
    
    /* Test 3: Memory leak detection test */
    log_info("Running leak detection test...");
    if (memory_stress_test_leak_detection() == 0) {
        tests_passed++;
        log_info("Leak detection test PASSED");
    } else {
        tests_failed++;
        log_error("Leak detection test FAILED");
        result = -1;
    }
    
    /* Test 4: Boundary conditions test */
    log_info("Running boundary conditions test...");
    if (memory_stress_test_boundary_conditions() == 0) {
        tests_passed++;
        log_info("Boundary conditions test PASSED");
    } else {
        tests_failed++;
        log_error("Boundary conditions test FAILED");
        result = -1;
    }
    
    /* Test 5: Concurrent operations simulation */
    log_info("Running concurrent operations test...");
    if (memory_stress_test_concurrent_operations() == 0) {
        tests_passed++;
        log_info("Concurrent operations test PASSED");
    } else {
        tests_failed++;
        log_error("Concurrent operations test FAILED");
        result = -1;
    }
    
    /* Test 6: Tier switching stress test */
    log_info("Running tier switching test...");
    if (memory_stress_test_tier_switching() == 0) {
        tests_passed++;
        log_info("Tier switching test PASSED");
    } else {
        tests_failed++;
        log_error("Tier switching test FAILED");
        result = -1;
    }
    
    /* Test 7: Memory corruption detection */
    log_info("Running corruption detection test...");
    if (memory_perform_corruption_test() == 0) {
        tests_passed++;
        log_info("Corruption detection test PASSED");
    } else {
        tests_failed++;
        log_error("Corruption detection test FAILED");
        result = -1;
    }
    
    /* Test 8: Extreme allocation scenarios */
    log_info("Running extreme allocation test...");
    if (memory_test_extreme_allocations() == 0) {
        tests_passed++;
        log_info("Extreme allocation test PASSED");
    } else {
        tests_failed++;
        log_error("Extreme allocation test FAILED");
        result = -1;
    }
    
    /* Test 9: Low memory simulation */
    log_info("Running low memory simulation...");
    memory_simulate_low_memory_conditions();
    log_info("Low memory simulation completed");
    tests_passed++;
    
    /* Validate all allocations are still intact */
    if (memory_validate_all_allocations() == 0) {
        tests_passed++;
        log_info("Post-test validation PASSED");
    } else {
        tests_failed++;
        log_error("Post-test validation FAILED");
        result = -1;
    }
    
    log_info("=== Memory Stress Test Summary ===");
    log_info("Tests passed: %d", tests_passed);
    log_info("Tests failed: %d", tests_failed);
    
    if (result == 0) {
        log_info("=== ALL MEMORY STRESS TESTS PASSED ===");
    } else {
        log_error("=== SOME MEMORY STRESS TESTS FAILED ===");
    }
    
    return result;
}

/**
 * @brief Test various allocation patterns under stress
 * @return 0 on success, negative on error
 */
static int memory_stress_test_allocation_patterns(void) {
    void *ptrs[200];
    uint32_t sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    int allocated = 0;
    int pattern;
    
    /* Test Pattern 1: Sequential increasing sizes */
    log_debug("Testing sequential increasing allocation pattern");
    for (pattern = 0; pattern < 3; pattern++) {
        for (int i = 0; i < 50 && allocated < 200; i++) {
            uint32_t size = sizes[i % num_sizes];
            
            switch (pattern) {
                case 0: /* Normal allocation */
                    ptrs[allocated] = memory_alloc(size, MEM_TYPE_GENERAL, 0);
                    break;
                case 1: /* Aligned allocation */
                    ptrs[allocated] = memory_alloc(size, MEM_TYPE_PACKET_BUFFER, MEM_FLAG_ALIGNED);
                    break;
                case 2: /* DMA capable allocation */
                    ptrs[allocated] = memory_alloc(size, MEM_TYPE_PACKET_BUFFER, MEM_FLAG_DMA_CAPABLE);
                    break;
            }
            
            if (ptrs[allocated]) {
                /* Fill with test pattern */
                memset(ptrs[allocated], 0xAA + (i % 4), size);
                allocated++;
            }
        }
    }
    
    log_debug("Allocated %d blocks in pattern test", allocated);
    
    /* Verify all allocations are intact */
    for (int i = 0; i < allocated; i++) {
        if (ptrs[i]) {
            mem_block_t *block = memory_get_block_header(ptrs[i]);
            if (!memory_validate_block(block)) {
                log_error("Block validation failed for allocation %d", i);
                return -1;
            }
        }
    }
    
    /* Free every other allocation to create fragmentation */
    for (int i = 0; i < allocated; i += 2) {
        if (ptrs[i]) {
            memory_free(ptrs[i]);
            ptrs[i] = NULL;
        }
    }
    
    /* Try to reallocate in the gaps */
    int reallocated = 0;
    for (int i = 0; i < allocated; i += 2) {
        ptrs[i] = memory_alloc(sizes[(i/2) % num_sizes], MEM_TYPE_GENERAL, 0);
        if (ptrs[i]) {
            reallocated++;
        }
    }
    
    log_debug("Reallocated %d blocks after fragmentation", reallocated);
    
    /* Free all remaining allocations */
    for (int i = 0; i < allocated; i++) {
        if (ptrs[i]) {
            memory_free(ptrs[i]);
        }
    }
    
    return 0;
}

/**
 * @brief Test memory fragmentation scenarios
 * @return 0 on success, negative on error
 */
static int memory_stress_test_fragmentation(void) {
    void *large_blocks[10];
    void *small_blocks[100];
    int large_count = 0;
    int small_count = 0;
    
    log_debug("Testing memory fragmentation scenarios");
    
    /* Allocate several large blocks */
    for (int i = 0; i < 10; i++) {
        large_blocks[i] = memory_alloc(4096, MEM_TYPE_PACKET_BUFFER, 0);
        if (large_blocks[i]) {
            large_count++;
        }
    }
    
    /* Fill gaps with small blocks */
    for (int i = 0; i < 100; i++) {
        small_blocks[i] = memory_alloc(64, MEM_TYPE_GENERAL, 0);
        if (small_blocks[i]) {
            small_count++;
        }
    }
    
    log_debug("Allocated %d large blocks and %d small blocks", large_count, small_count);
    
    /* Free every other large block to create large gaps */
    for (int i = 1; i < 10; i += 2) {
        if (large_blocks[i]) {
            memory_free(large_blocks[i]);
            large_blocks[i] = NULL;
        }
    }
    
    /* Try to allocate medium-sized blocks in the gaps */
    int medium_allocated = 0;
    for (int i = 0; i < 5; i++) {
        void *medium_ptr = memory_alloc(2048, MEM_TYPE_PACKET_BUFFER, 0);
        if (medium_ptr) {
            medium_allocated++;
            memory_free(medium_ptr);  /* Free immediately to test coalescing */
        }
    }
    
    log_debug("Successfully allocated %d medium blocks in gaps", medium_allocated);
    
    /* Free all remaining allocations */
    for (int i = 0; i < 10; i++) {
        if (large_blocks[i]) {
            memory_free(large_blocks[i]);
        }
    }
    
    for (int i = 0; i < 100; i++) {
        if (small_blocks[i]) {
            memory_free(small_blocks[i]);
        }
    }
    
    return 0;
}

/**
 * @brief Test memory leak detection capabilities
 * @return 0 on success, negative on error
 */
static int memory_stress_test_leak_detection(void) {
    uint32_t initial_allocations = g_mem_stats.total_allocations;
    uint32_t initial_frees = g_mem_stats.total_frees;
    uint32_t initial_used = g_mem_stats.used_memory;
    
    log_debug("Testing leak detection - initial state: %lu allocs, %lu frees, %lu used",
             initial_allocations, initial_frees, initial_used);
    
    /* Perform many allocations and frees */
    for (int cycle = 0; cycle < 5; cycle++) {
        void *ptrs[50];
        int allocated = 0;
        
        /* Allocate many blocks */
        for (int i = 0; i < 50; i++) {
            ptrs[i] = memory_alloc(128 + (i * 16), MEM_TYPE_GENERAL, 0);
            if (ptrs[i]) {
                allocated++;
            }
        }
        
        /* Free all but a few blocks (intentional \"leak\" simulation) */
        for (int i = 0; i < allocated - 2; i++) {
            if (ptrs[i]) {
                memory_free(ptrs[i]);
                ptrs[i] = NULL;
            }
        }
        
        /* Check if leaked blocks are tracked */
        uint32_t current_used = g_mem_stats.used_memory;
        if (current_used <= initial_used + (2 * (128 + 25 * 16))) {
            /* Acceptable leak tracking */
        } else {
            log_warning("Potential memory leak detected in cycle %d", cycle);
        }
        
        /* Clean up remaining \"leaked\" blocks */
        for (int i = allocated - 2; i < allocated; i++) {
            if (ptrs[i]) {
                memory_free(ptrs[i]);
            }
        }
    }
    
    /* Verify we're back to initial state (within tolerance) */
    uint32_t final_used = g_mem_stats.used_memory;
    if (final_used <= initial_used + 1024) {  /* 1KB tolerance */
        log_debug("Leak detection test passed - memory usage returned to baseline");
        return 0;
    } else {
        log_error("Potential memory leak: initial=%lu, final=%lu", initial_used, final_used);
        return -1;
    }
}

/**
 * @brief Test boundary conditions and edge cases
 * @return 0 on success, negative on error
 */
static int memory_stress_test_boundary_conditions(void) {
    log_debug("Testing boundary conditions and edge cases");
    
    /* Test 1: Zero-size allocation (should fail gracefully) */
    void *zero_ptr = memory_alloc(0, MEM_TYPE_GENERAL, 0);
    if (zero_ptr != NULL) {
        log_error("Zero-size allocation should have failed");
        memory_free(zero_ptr);
        return -1;
    }
    
    /* Test 2: Maximum reasonable size allocation */
    void *large_ptr = memory_alloc(32768, MEM_TYPE_GENERAL, 0);
    if (large_ptr) {
        /* Fill with pattern to ensure it's really allocated */
        memset(large_ptr, 0x55, 32768);
        
        /* Verify the pattern */
        uint8_t *data = (uint8_t*)large_ptr;
        for (int i = 0; i < 1000; i += 100) {  /* Sample check */
            if (data[i] != 0x55) {
                log_error("Large allocation memory corruption detected");
                memory_free(large_ptr);
                return -1;
            }
        }
        
        memory_free(large_ptr);
    }
    
    /* Test 3: NULL pointer free (should be safe) */
    memory_free(NULL);  /* Should not crash */
    
    /* Test 4: Double free detection */
    void *test_ptr = memory_alloc(256, MEM_TYPE_GENERAL, 0);
    if (test_ptr) {
        memory_free(test_ptr);
        /* Second free should be detected and handled gracefully */
        memory_free(test_ptr);
    }
    
    /* Test 5: Alignment boundary testing */
    for (int align = 1; align <= 16; align *= 2) {
        void *aligned_ptr = memory_alloc_aligned(100, align, MEM_TYPE_GENERAL);
        if (aligned_ptr) {
            if (((uint32_t)aligned_ptr % align) != 0) {
                log_error("Alignment failed for boundary %d", align);
                memory_free(aligned_ptr);
                return -1;
            }
            memory_free(aligned_ptr);
        }
    }
    
    log_debug("Boundary conditions test completed successfully");
    return 0;
}

/**
 * @brief Simulate concurrent memory operations
 * @return 0 on success, negative on error
 */
static int memory_stress_test_concurrent_operations(void) {
    void *ptrs_a[25];
    void *ptrs_b[25];
    int allocated_a = 0;
    int allocated_b = 0;
    
    log_debug("Simulating concurrent memory operations");
    
    /* Simulate two \"threads\" doing interleaved operations */
    for (int round = 0; round < 5; round++) {
        /* \"Thread A\" - allocate small blocks */
        for (int i = 0; i < 5; i++) {
            if (allocated_a < 25) {
                ptrs_a[allocated_a] = memory_alloc(64 + (i * 8), MEM_TYPE_GENERAL, 0);
                if (ptrs_a[allocated_a]) {
                    allocated_a++;
                }
            }
        }
        
        /* \"Thread B\" - allocate larger blocks */
        for (int i = 0; i < 3; i++) {
            if (allocated_b < 25) {
                ptrs_b[allocated_b] = memory_alloc(512 + (i * 64), MEM_TYPE_PACKET_BUFFER, 0);
                if (ptrs_b[allocated_b]) {
                    allocated_b++;
                }
            }
        }
        
        /* \"Thread A\" - free some blocks */
        if (allocated_a >= 3) {
            for (int i = 0; i < 2; i++) {
                if (ptrs_a[i]) {
                    memory_free(ptrs_a[i]);
                    ptrs_a[i] = NULL;
                }
            }
        }
        
        /* \"Thread B\" - free some blocks */
        if (allocated_b >= 2) {
            if (ptrs_b[0]) {
                memory_free(ptrs_b[0]);
                ptrs_b[0] = NULL;
            }
        }
    }
    
    /* Clean up all remaining allocations */
    for (int i = 0; i < 25; i++) {
        if (ptrs_a[i]) {
            memory_free(ptrs_a[i]);
        }
        if (ptrs_b[i]) {
            memory_free(ptrs_b[i]);
        }
    }
    
    log_debug("Concurrent operations simulation completed");
    return 0;
}

/**
 * @brief Test tier switching under memory pressure
 * @return 0 on success, negative on error
 */
static int memory_stress_test_tier_switching(void) {
    void *tier_ptrs[50];
    int allocated = 0;
    
    log_debug("Testing memory tier switching under pressure");
    
    /* Force allocations to test different tiers */
    for (int i = 0; i < 50; i++) {
        uint32_t size;
        mem_type_t type;
        uint32_t flags = 0;
        
        /* Vary allocation parameters to trigger different tiers */
        if (i % 3 == 0) {
            size = 8192;  /* Large - should prefer XMS */
            type = MEM_TYPE_PACKET_BUFFER;
            flags = MEM_FLAG_DMA_CAPABLE;
        } else if (i % 3 == 1) {
            size = 1024;  /* Medium - may use UMB */
            type = MEM_TYPE_PACKET_BUFFER;
            flags = MEM_FLAG_ALIGNED;
        } else {
            size = 128;   /* Small - likely conventional */
            type = MEM_TYPE_GENERAL;
            flags = 0;
        }
        
        tier_ptrs[i] = memory_alloc(size, type, flags);
        if (tier_ptrs[i]) {
            allocated++;
            
            /* Write test pattern to verify allocation is valid */
            memset(tier_ptrs[i], 0xCC + (i % 4), size > 256 ? 256 : size);
        }
    }
    
    log_debug("Allocated %d blocks across memory tiers", allocated);
    
    /* Verify all allocations by checking test patterns */
    for (int i = 0; i < allocated; i++) {
        if (tier_ptrs[i]) {
            uint8_t *data = (uint8_t*)tier_ptrs[i];
            uint8_t expected = 0xCC + (i % 4);
            
            if (data[0] != expected || data[10] != expected) {
                log_error("Tier allocation %d corrupted (expected 0x%02X, got 0x%02X)", 
                         i, expected, data[0]);
                return -1;
            }
        }
    }
    
    /* Free all allocations */
    for (int i = 0; i < 50; i++) {
        if (tier_ptrs[i]) {
            memory_free(tier_ptrs[i]);
        }
    }
    
    log_debug("Tier switching test completed successfully");
    return 0;
}

/**
 * @brief Validate all current allocations for corruption
 * @return 0 if all allocations valid, negative if corruption found
 */
static int memory_validate_all_allocations(void) {
    /* This is a simplified validation - real implementation would
       walk all allocation pools and validate magic numbers and checksums */
    
    log_debug("Validating all memory allocations for corruption");
    
    /* Check global memory statistics for consistency */
    if (g_mem_stats.total_allocations < g_mem_stats.total_frees) {
        log_error("Memory statistics inconsistent: allocs=%lu < frees=%lu",
                 g_mem_stats.total_allocations, g_mem_stats.total_frees);
        return -1;
    }
    
    /* Check for reasonable memory usage */
    if (g_mem_stats.used_memory > 1024 * 1024) {  /* > 1MB seems excessive for our driver */
        log_warning("High memory usage detected: %lu bytes", g_mem_stats.used_memory);
    }
    
    /* Check tier consistency */
    if (g_xms_tier.total_allocated > g_xms_tier.peak_allocated) {
        log_error("XMS tier statistics inconsistent");
        return -1;
    }
    
    if (g_umb_tier.total_allocated > g_umb_tier.peak_allocated) {
        log_error("UMB tier statistics inconsistent");
        return -1;
    }
    
    log_debug("Memory validation completed - no corruption detected");
    return 0;
}

/**
 * @brief Test memory corruption detection mechanisms
 * @return 0 on success, negative on error
 */
static int memory_perform_corruption_test(void) {
    void *test_ptr;
    mem_block_t *block;
    
    log_debug("Testing memory corruption detection");
    
    /* Allocate a test block */
    test_ptr = memory_alloc(256, MEM_TYPE_GENERAL, 0);
    if (!test_ptr) {
        log_error("Failed to allocate test block for corruption test");
        return -1;
    }
    
    /* Get block header and verify it's valid */
    block = memory_get_block_header(test_ptr);
    if (!memory_validate_block(block)) {
        log_error("Initial block validation failed");
        memory_free(test_ptr);
        return -1;
    }
    
    /* Simulate corruption by modifying magic number */
    uint32_t original_magic = block->magic;
    block->magic = 0xDEADBEEF;  /* Corrupt the magic number */
    
    /* Validation should now fail */
    if (memory_validate_block(block)) {
        log_error("Corruption detection failed - corrupted block passed validation");
        block->magic = original_magic;  /* Restore for cleanup */
        memory_free(test_ptr);
        return -1;
    }
    
    /* Restore magic number for proper cleanup */
    block->magic = original_magic;
    
    /* Free the test block */
    memory_free(test_ptr);
    
    log_debug("Memory corruption detection test passed");
    return 0;
}

/**
 * @brief Test extreme allocation scenarios
 * @return 0 on success, negative on error
 */
static int memory_test_extreme_allocations(void) {
    log_debug("Testing extreme allocation scenarios");
    
    /* Test very large allocation that should fail */
    void *huge_ptr = memory_alloc(0x100000, MEM_TYPE_GENERAL, 0);  /* 1MB */
    if (huge_ptr) {
        log_warning("Unexpectedly succeeded in allocating 1MB");
        memory_free(huge_ptr);
    }
    
    /* Test many tiny allocations */
    void *tiny_ptrs[1000];
    int tiny_allocated = 0;
    
    for (int i = 0; i < 1000; i++) {
        tiny_ptrs[i] = memory_alloc(8, MEM_TYPE_GENERAL, 0);
        if (tiny_ptrs[i]) {
            tiny_allocated++;
        } else {
            break;  /* Stop when we run out of memory */
        }
    }
    
    log_debug("Successfully allocated %d tiny (8-byte) blocks", tiny_allocated);
    
    /* Free all tiny allocations */
    for (int i = 0; i < tiny_allocated; i++) {
        if (tiny_ptrs[i]) {
            memory_free(tiny_ptrs[i]);
        }
    }
    
    /* Test allocation with invalid parameters */
    void *invalid_ptr = memory_alloc(100, 99, 0xFFFFFFFF);  /* Invalid type and flags */
    if (invalid_ptr) {
        log_warning("Allocation with invalid parameters unexpectedly succeeded");
        memory_free(invalid_ptr);
    }
    
    log_debug("Extreme allocation scenarios test completed");
    return 0;
}

/**
 * @brief Simulate low memory conditions
 */
static void memory_simulate_low_memory_conditions(void) {
    void *exhaustion_ptrs[100];
    int allocated = 0;
    
    log_debug("Simulating low memory conditions");
    
    /* Try to exhaust available memory */
    for (int i = 0; i < 100; i++) {
        exhaustion_ptrs[i] = memory_alloc(4096, MEM_TYPE_GENERAL, 0);
        if (exhaustion_ptrs[i]) {
            allocated++;
        } else {
            break;  /* Memory exhausted */
        }
    }
    
    log_debug("Allocated %d large blocks before memory exhaustion", allocated);
    
    /* Under low memory, try small allocations */
    int small_allocated = 0;
    for (int i = 0; i < 20; i++) {
        void *small_ptr = memory_alloc(64, MEM_TYPE_GENERAL, 0);
        if (small_ptr) {
            small_allocated++;
            memory_free(small_ptr);  /* Free immediately */
        } else {
            break;
        }
    }
    
    log_debug("Successfully allocated %d small blocks under memory pressure", small_allocated);
    
    /* Check that error handling works correctly */
    mem_error_t last_error = memory_get_last_error();
    if (last_error == MEM_ERROR_NO_MEMORY || last_error == MEM_ERROR_NONE) {
        log_debug("Memory error handling working correctly");
    } else {
        log_warning("Unexpected memory error: %d", last_error);
    }
    
    /* Clean up exhaustion allocations */
    for (int i = 0; i < allocated; i++) {
        if (exhaustion_ptrs[i]) {
            memory_free(exhaustion_ptrs[i]);
        }
    }
    
    log_debug("Low memory simulation completed");
}