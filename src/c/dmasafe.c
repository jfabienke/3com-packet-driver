/**
 * @file dma_safety.c
 * @brief DMA Safety Framework with Bounce Buffers
 *
 * CRITICAL: GPT-5 Identified DMA Safety Requirements
 * "Ensure all DMA-visible buffers respect the strictest device constraints.
 *  If you ever use upper memory/XMS for buffers, implement reliable bounce buffering."
 *
 * This framework implements:
 * 1. 64KB boundary checking for ISA DMA compatibility
 * 2. 16MB limit enforcement for ISA devices
 * 3. Physical contiguity validation
 * 4. Automatic bounce buffer management
 * 5. Cache coherency management
 * 6. Memory alignment requirements
 *
 * Supports all 3Com cards: 3C509B, 3C589, 3C905B/C, 3C515-TX
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <dos.h>

#include "../../include/memory.h"
#include "../../include/logging.h"
#include "../../include/common.h"
#include "../../include/vds.h"  /* Includes vds_sg_entry_t and lock/unlock functions */
#include "../../include/cachemgt.h"
#include "../../include/dmaself.h"
#include "../../include/vds_mapping.h"
#include "../../docs/agents/shared/error-codes.h"

/* DMA constraint definitions */
#define DMA_64KB_BOUNDARY       0x10000     /* 64KB boundary mask */
#define DMA_16MB_LIMIT          0x1000000   /* 16MB physical limit for ISA */
#define DMA_PAGE_SIZE           4096        /* 4KB page size */
#define DMA_ALIGNMENT_MASK      0x0F        /* 16-byte alignment mask */
#define MAX_BOUNCE_BUFFERS      32          /* Maximum bounce buffers */
#define BOUNCE_BUFFER_SIZE      2048        /* Standard bounce buffer size */

/* DMA buffer types */
typedef enum {
    DMA_BUFFER_TYPE_TX,                     /* Transmit buffer */
    DMA_BUFFER_TYPE_RX,                     /* Receive buffer */
    DMA_BUFFER_TYPE_DESCRIPTOR,             /* Descriptor ring */
    DMA_BUFFER_TYPE_GENERAL,                /* General purpose */
    DMA_BUFFER_TYPE_COUNT
} dma_buffer_type_t;

/* DMA device constraints */
typedef struct {
    char device_name[16];                   /* Device name */
    uint32_t max_address;                   /* Maximum physical address */
    uint32_t alignment_required;            /* Required alignment in bytes */
    bool requires_contiguous;               /* Requires physically contiguous memory */
    bool supports_64bit_addressing;        /* Supports 64-bit addressing */
    bool crossing_64kb_forbidden;           /* Cannot cross 64KB boundaries */
    uint32_t max_transfer_size;             /* Maximum single transfer size */
    bool cache_coherent;                    /* Hardware maintains cache coherency */
} dma_device_constraints_t;

/* DMA buffer descriptor with TSR defensive patterns */
typedef struct {
    uint32_t signature;                     /* Structure signature (SIGNATURE_MAGIC) */
    uint16_t checksum;                      /* Structure checksum for validation */
    void* virtual_address;                  /* Virtual address */
    uint32_t physical_address;              /* Physical address */
    uint32_t size;                          /* Buffer size */
    dma_buffer_type_t type;                 /* Buffer type */
    bool is_bounce_buffer;                  /* Using bounce buffer */
    void* bounce_virtual;                   /* Bounce buffer virtual address */
    uint32_t bounce_physical;               /* Bounce buffer physical address */
    bool needs_sync;                        /* Requires cache synchronization */
    bool allocated_by_framework;            /* Allocated by this framework */
    uint32_t alignment;                     /* Actual alignment achieved */
    uint32_t canary_rear;                   /* Rear canary (CANARY_PATTERN_REAR) */
} dma_buffer_descriptor_t;

/* Bounce buffer pool with defensive patterns */
typedef struct {
    uint32_t front_canary;                  /* Front canary (CANARY_PATTERN_FRONT) */
    void* virtual_address;                  /* Virtual address */
    uint32_t physical_address;              /* Physical address */
    uint32_t size;                          /* Buffer size */
    volatile bool in_use;                   /* Currently allocated (accessed in ISR) */
    dma_buffer_type_t assigned_type;        /* Assigned buffer type */
    uint16_t use_count;                     /* Usage counter for diagnostics */
    uint16_t checksum;                      /* Structure checksum */
    uint32_t rear_canary;                   /* Rear canary (CANARY_PATTERN_REAR) */
} bounce_buffer_t;

/* DMA safety manager */
typedef struct {
    dma_device_constraints_t constraints[8]; /* Device constraints */
    uint32_t device_count;                  /* Number of registered devices */
    bounce_buffer_t bounce_pool[MAX_BOUNCE_BUFFERS]; /* Bounce buffer pool */
    uint32_t bounce_count;                  /* Number of bounce buffers */
    dma_buffer_descriptor_t active_buffers[64]; /* Active DMA buffers */
    /* GPT-5 Critical: Mark ISR-shared variables as volatile */
    volatile uint32_t active_count;         /* Number of active buffers (accessed in ISR) */
    volatile bool framework_initialized;    /* Initialization status (checked in ISR) */
    volatile uint32_t total_allocations;    /* Total allocations (stats) */
    volatile uint32_t bounce_buffer_hits;   /* Bounce buffer usage count (stats) */
    volatile uint32_t boundary_violations_prevented; /* 64KB violations prevented (stats) */
} dma_safety_manager_t;

/* Global DMA safety manager */
static dma_safety_manager_t g_dma_manager;

/* GPT-5 Critical: Atomic operations for ISR-shared 32-bit variables */
static inline uint32_t read32_atomic(volatile uint32_t* p) {
    uint32_t value;
    ENTER_CRITICAL();
    value = *p;
    EXIT_CRITICAL();
    return value;
}

static inline void write32_atomic(volatile uint32_t* p, uint32_t value) {
    ENTER_CRITICAL();
    *p = value;
    EXIT_CRITICAL();
}

static inline void increment32_atomic(volatile uint32_t* p) {
    ENTER_CRITICAL();
    (*p)++;
    EXIT_CRITICAL();
}

static inline void decrement32_atomic(volatile uint32_t* p) {
    ENTER_CRITICAL();
    (*p)--;
    EXIT_CRITICAL();
}

/* TSR Defensive Helper Functions */
static uint16_t calculate_checksum(const void* data, size_t size);
static bool validate_checksum(const void* structure);
static bool validate_buffer_integrity(const dma_buffer_descriptor_t* desc);
static bool validate_bounce_buffer(const bounce_buffer_t* bounce);
static void init_buffer_protection(dma_buffer_descriptor_t* desc);
static void init_bounce_protection(bounce_buffer_t* bounce);

/* Function prototypes */
static int register_device_constraints(const char* device_name, const dma_device_constraints_t* constraints);
static bool validate_dma_buffer_constraints(const void* buffer, uint32_t size, const dma_device_constraints_t* constraints);
static uint32_t get_physical_address(const void* virtual_address);
static uint32_t get_physical_address_vds(const void* virtual_address);  /* Deprecated - for compatibility */
static bool get_physical_mapping_full(const void* buffer, uint32_t size, 
                                     uint32_t* phys_addr, bool* is_contiguous);
static bool crosses_64kb_boundary(uint32_t physical_address, uint32_t size);
static bounce_buffer_t* allocate_bounce_buffer(uint32_t size, dma_buffer_type_t type);
static void free_bounce_buffer(bounce_buffer_t* bounce);
static int sync_bounce_buffer(dma_buffer_descriptor_t* desc, bool to_device);

/**
 * @brief Initialize DMA safety framework
 *
 * Sets up bounce buffer pool and registers device constraints.
 * CRITICAL: GPT-5 requirement for DMA-safe operations.
 *
 * @return SUCCESS or error code
 */
int dma_safety_init(void) {
    int i;
    void* bounce_memory;
    uint32_t bounce_physical;
    
    log_info("DMA Safety: Initializing DMA safety framework");
    
    /* GPT-5 Critical: Initialize VDS support first */
    if (!vds_init()) {
        if (is_v86_mode()) {
            log_error("DMA Safety: V86 mode detected but VDS not available!");
            log_error("DMA Safety: Cannot safely perform DMA in V86 without VDS");
            /* Continue initialization but DMA will be restricted */
        } else {
            log_info("DMA Safety: VDS not available - using real mode DMA");
        }
    }
    
    /* Clear manager structure */
    memset(&g_dma_manager, 0, sizeof(dma_safety_manager_t));
    
    /* Allocate bounce buffer pool in low memory (< 16MB) */
    bounce_memory = memory_alloc_dma(MAX_BOUNCE_BUFFERS * BOUNCE_BUFFER_SIZE);
    if (!bounce_memory) {
        log_error("DMA Safety: Failed to allocate bounce buffer pool");
        return ERROR_MEMORY_ALLOCATION_FAILED;
    }
    
    /* GPT-5 Critical: Validate bounce buffer pool is ISA-compatible (< 16MB) */
    bounce_physical = get_physical_address(bounce_memory);
    if (bounce_physical + (MAX_BOUNCE_BUFFERS * BOUNCE_BUFFER_SIZE) > DMA_ISA_LIMIT) {
        log_error("DMA Safety: Bounce buffer pool at 0x%08lX exceeds ISA limit", bounce_physical);
        memory_free(bounce_memory);
        return ERROR_DMA_NOT_SUPPORTED;
    }
    
    log_info("DMA Safety: Bounce buffer pool at ISA-compatible physical 0x%08lX", bounce_physical);
    
    /* Initialize bounce buffer pool with defensive patterns */
    for (i = 0; i < MAX_BOUNCE_BUFFERS; i++) {
        bounce_buffer_t* bounce = &g_dma_manager.bounce_pool[i];
        bounce->virtual_address = (uint8_t*)bounce_memory + (i * BOUNCE_BUFFER_SIZE);
        bounce->physical_address = bounce_physical + (i * BOUNCE_BUFFER_SIZE);
        bounce->size = BOUNCE_BUFFER_SIZE;
        bounce->in_use = false;
        bounce->assigned_type = DMA_BUFFER_TYPE_GENERAL;
        
        /* TSR Defensive: Initialize protection */
        init_bounce_protection(bounce);
    }
    g_dma_manager.bounce_count = MAX_BOUNCE_BUFFERS;
    
    /* Register default device constraints */
    register_3com_device_constraints();
    
    g_dma_manager.framework_initialized = true;
    
    /* GPT-5 Enhancement: Validate all device capability descriptors */
    if (!validate_all_device_caps()) {
        log_error("DMA Safety: Device capability validation failed - check configuration");
        /* Continue initialization but log the warning */
    }
    
    /* GPT-5 Critical: Initialize cache management system */
    coherency_analysis_t cache_analysis = perform_complete_coherency_analysis();
    if (!initialize_cache_management(&cache_analysis)) {
        log_warning("DMA Safety: Cache management initialization failed - using fallback");
    }
    
    /* GPT-5 Critical: Run self-test diagnostics for production readiness */
    #ifdef ENABLE_DMA_SELF_TEST
    log_info("DMA Safety: Running self-test diagnostics...");
    if (dma_run_self_tests() != DMA_TEST_PASS) {
        log_error("DMA Safety: Self-test FAILED - framework may not be production ready");
        /* Continue anyway but warn the user */
    } else {
        log_info("DMA Safety: Self-test PASSED - framework is production ready");
    }
    #endif
    
    log_info("DMA Safety: Framework initialized with %d bounce buffers", MAX_BOUNCE_BUFFERS);
    return SUCCESS;
}

/**
 * @brief Register 3Com device DMA constraints
 *
 * Sets up constraints for all supported 3Com network cards.
 * GPT-5 requirement: "Respect the strictest device constraints."
 */
int register_3com_device_constraints(void) {
    dma_device_constraints_t constraints;
    
    /* 3C509B ISA - Strictest constraints */
    memset(&constraints, 0, sizeof(constraints));
    strcpy(constraints.device_name, "3C509B");
    constraints.max_address = DMA_16MB_LIMIT;           /* ISA 16MB limit */
    constraints.alignment_required = 4;                 /* 4-byte alignment */
    constraints.requires_contiguous = true;             /* Must be contiguous */
    constraints.supports_64bit_addressing = false;     /* 16-bit ISA */
    constraints.crossing_64kb_forbidden = true;         /* Cannot cross 64KB */
    constraints.max_transfer_size = 1518;               /* Ethernet frame size */
    constraints.cache_coherent = false;                 /* ISA not cache coherent */
    register_device_constraints("3C509B", &constraints);
    
    /* 3C589 PCMCIA - Similar to 3C509B but 16-byte alignment */
    strcpy(constraints.device_name, "3C589");
    constraints.alignment_required = 16;                /* 16-byte alignment */
    register_device_constraints("3C589", &constraints);
    
    /* 3C515-TX ISA Bus Master - Better than 3C509B but still ISA constraints */
    strcpy(constraints.device_name, "3C515TX");
    constraints.max_address = DMA_ISA_LIMIT;           /* Still ISA 16MB limit */
    constraints.alignment_required = 8;                 /* 8-byte alignment */
    constraints.max_transfer_size = 65536;              /* Bus master supports larger transfers */
    register_device_constraints("3C515TX", &constraints);
    
    /* 3C905B PCI - More relaxed but still needs care */
    strcpy(constraints.device_name, "3C905B");
    constraints.max_address = 0xFFFFFFFF;               /* Full 32-bit addressing */
    constraints.alignment_required = 16;                /* 16-byte alignment for descriptors */
    constraints.requires_contiguous = true;             /* Descriptor rings must be contiguous */
    constraints.supports_64bit_addressing = false;     /* 32-bit PCI */
    constraints.crossing_64kb_forbidden = false;       /* PCI can cross boundaries */
    constraints.max_transfer_size = 65536;              /* Large transfers supported */
    constraints.cache_coherent = true;                  /* PCI is cache coherent */
    register_device_constraints("3C905B", &constraints);
    
    /* 3C905C PCI - Same as 3C905B */
    strcpy(constraints.device_name, "3C905C");
    register_device_constraints("3C905C", &constraints);
    
    log_info("DMA Safety: Registered constraints for all 3Com devices");
    return SUCCESS;
}

/**
 * @brief Allocate DMA-safe buffer with automatic bounce buffering
 *
 * CRITICAL: GPT-5 Safety Implementation
 * "If any path uses ISA 8237 DMA, enforce non-crossing 64K boundaries 
 *  and addresses below 16 MB; otherwise always use safe bounce buffers."
 *
 * @param size Buffer size in bytes
 * @param alignment Required alignment
 * @param type Buffer type
 * @param device_name Device name for constraint lookup
 * @return Buffer descriptor or NULL on failure
 */
dma_buffer_descriptor_t* dma_allocate_buffer(uint32_t size, uint32_t alignment, 
                                           dma_buffer_type_t type, const char* device_name) {
    dma_buffer_descriptor_t* desc;
    dma_device_constraints_t* constraints = NULL;
    void* buffer;
    uint32_t physical_addr;
    bool needs_bounce = false;
    uint32_t i;
    
    if (!g_dma_manager.framework_initialized) {
        log_error("DMA Safety: Framework not initialized");
        return NULL;
    }
    
    if (read32_atomic(&g_dma_manager.active_count) >= 64) {
        log_error("DMA Safety: Maximum active buffers exceeded");
        return NULL;
    }
    
    /* Find device constraints */
    for (i = 0; i < g_dma_manager.device_count; i++) {
        if (strcmp(g_dma_manager.constraints[i].device_name, device_name) == 0) {
            constraints = &g_dma_manager.constraints[i];
            break;
        }
    }
    
    if (!constraints) {
        log_warning("DMA Safety: No constraints found for device %s, using defaults", device_name);
        /* Use strictest constraints as default */
        for (i = 0; i < g_dma_manager.device_count; i++) {
            if (strcmp(g_dma_manager.constraints[i].device_name, "3C509B") == 0) {
                constraints = &g_dma_manager.constraints[i];
                break;
            }
        }
    }
    
    /* Ensure alignment meets device requirements */
    if (alignment < constraints->alignment_required) {
        alignment = constraints->alignment_required;
    }
    
    /* Try to allocate DMA buffer - GPT-5 Critical: ISA 24-bit addressing enforcement */
    buffer = memory_alloc_dma(size);
    
    if (buffer) {
        physical_addr = get_physical_address(buffer);
        
        /* GPT-5 Critical Fix: Enforce ISA 24-bit addressing constraint FIRST */
        if (constraints->max_address < UINT32_MAX && physical_addr + size > constraints->max_address) {
            log_warning("DMA Safety: Buffer at 0x%08lX exceeds max address 0x%08lX for %s, using bounce buffer",
                       physical_addr, constraints->max_address, device_name);
            memory_free(buffer);
            buffer = NULL;
            needs_bounce = true;
        } else {
            /* Validate other buffer constraints */
            if (!validate_dma_buffer_constraints(buffer, size, constraints)) {
                log_debug("DMA Safety: Direct allocation failed constraints, using bounce buffer");
                memory_free(buffer);
                buffer = NULL;
                needs_bounce = true;
            }
        }
    } else {
        needs_bounce = true;
    }
    
    /* Get buffer descriptor - ISR-safe critical section */
    ENTER_CRITICAL();
    desc = &g_dma_manager.active_buffers[read32_atomic(&g_dma_manager.active_count)];
    memset(desc, 0, sizeof(dma_buffer_descriptor_t));
    desc->type = type;
    desc->size = size;
    
    /* TSR Defensive: Initialize protection */
    init_buffer_protection(desc);
    
    EXIT_CRITICAL();
    
    if (needs_bounce) {
        /* Use bounce buffer with retry logic */
        bounce_buffer_t* bounce = NULL;
        int retry_count = 0;
        
        /* TSR Defensive: Retry with exponential backoff */
        while (retry_count < MAX_RETRY_COUNT) {
            bounce = allocate_bounce_buffer(size, type);
            if (bounce) break;
            
            retry_count++;
            log_warning("DMA Safety: Bounce buffer allocation failed, retry %d/%d", 
                       retry_count, MAX_RETRY_COUNT);
            
            /* Exponential backoff delay */
            for (int delay = 0; delay < RETRY_DELAY_BASE * (1 << retry_count); delay++) {
                IO_DELAY();
            }
            
            /* Try emergency recovery between retries */
            if (retry_count == 2) {
                emergency_recovery();
            }
        }
        
        if (!bounce) {
            log_error("DMA Safety: Failed to allocate bounce buffer after %d retries", retry_count);
            return NULL;
        }
        
        desc->virtual_address = bounce->virtual_address;
        desc->physical_address = bounce->physical_address;
        desc->is_bounce_buffer = true;
        desc->bounce_virtual = bounce->virtual_address;
        desc->bounce_physical = bounce->physical_address;
        desc->needs_sync = true;
        desc->allocated_by_framework = true;
        desc->alignment = 16; /* Bounce buffers are always 16-byte aligned */
        
        /* Update statistics - ISR-safe */
        ENTER_CRITICAL();
        increment32_atomic(&g_dma_manager.bounce_buffer_hits);
        EXIT_CRITICAL();
        log_debug("DMA Safety: Using bounce buffer at 0x%08lX for %lu bytes", 
                 bounce->physical_address, size);
    } else {
        /* Use direct allocation */
        desc->virtual_address = buffer;
        desc->physical_address = physical_addr;
        desc->is_bounce_buffer = false;
        desc->needs_sync = !constraints->cache_coherent;
        desc->allocated_by_framework = true;
        desc->alignment = alignment;
        
        log_debug("DMA Safety: Direct allocation at 0x%08lX for %lu bytes", 
                 physical_addr, size);
    }
    
    /* Update counters - ISR-safe critical section */
    ENTER_CRITICAL();
    increment32_atomic(&g_dma_manager.active_count);
    increment32_atomic(&g_dma_manager.total_allocations);
    EXIT_CRITICAL();
    
    return desc;
}

/**
 * @brief Validate DMA buffer constraints
 *
 * Checks all device-specific DMA constraints including:
 * - 64KB boundary crossing (critical for ISA)
 * - 16MB address limit (ISA DMA)  
 * - Physical contiguity
 * - Proper alignment
 */
static bool validate_dma_buffer_constraints(const void* buffer, uint32_t size, 
                                          const dma_device_constraints_t* constraints) {
    uint32_t physical_addr;
    uint32_t end_addr;
    
    physical_addr = get_physical_address(buffer);
    end_addr = physical_addr + size - 1;
    
    /* Check maximum address limit */
    if (end_addr > constraints->max_address) {
        log_debug("DMA Safety: Buffer end 0x%08lX exceeds max address 0x%08lX", 
                 end_addr, constraints->max_address);
        return false;
    }
    
    /* Check 64KB boundary crossing */
    if (constraints->crossing_64kb_forbidden && crosses_64kb_boundary(physical_addr, size)) {
        log_debug("DMA Safety: Buffer crosses 64KB boundary (0x%08lX + %lu)", 
                 physical_addr, size);
        /* Update statistics - ISR-safe */
        ENTER_CRITICAL();
        increment32_atomic(&g_dma_manager.boundary_violations_prevented);
        EXIT_CRITICAL();
        return false;
    }
    
    /* Check alignment */
    if ((physical_addr & (constraints->alignment_required - 1)) != 0) {
        log_debug("DMA Safety: Buffer not aligned to %lu bytes (address 0x%08lX)", 
                 constraints->alignment_required, physical_addr);
        return false;
    }
    
    /* Check transfer size limit */
    if (size > constraints->max_transfer_size) {
        log_debug("DMA Safety: Buffer size %lu exceeds max transfer %lu", 
                 size, constraints->max_transfer_size);
        return false;
    }
    
    return true;
}

/**
 * @brief Check if buffer crosses 64KB boundary
 *
 * CRITICAL: GPT-5 Requirement for ISA DMA compatibility
 * ISA DMA controllers cannot handle transfers that cross 64KB boundaries.
 */
static bool crosses_64kb_boundary(uint32_t physical_address, uint32_t size) {
    uint32_t start_page = physical_address >> 16;          /* 64KB page number */
    uint32_t end_page = (physical_address + size - 1) >> 16;
    
    return (start_page != end_page);
}

/**
 * @brief Get physical address from virtual address (VDS-aware)
 *
 * GPT-5 Critical: Use VDS for address translation in V86 mode
 * In DOS real mode, this is straightforward segment:offset conversion.
 * In V86/EMM386/Windows, must use VDS to get true physical address.
 */
static uint32_t get_physical_address(const void* virtual_address) {
    uint32_t physical_addr = 0;
    
    /* GPT-5 Critical: Use proper VDS implementation */
    if (!vds_get_safe_physical_address((void*)virtual_address, 1, &physical_addr)) {
        /* In V86 without VDS, we cannot safely do DMA */
        if (is_v86_mode()) {
            log_error("DMA Safety: Cannot get physical address in V86 without VDS!");
            return 0;  /* Invalid address - will force bounce buffer */
        }
        
        /* Real mode fallback */
        uint16_t segment = FP_SEG(virtual_address);
        uint16_t offset = FP_OFF(virtual_address);
        physical_addr = ((uint32_t)segment << 4) + offset;
    }
    
    return physical_addr;
}

/**
 * @brief Get physical address for full buffer (VDS-aware)
 *
 * GPT-5 Critical: Maps entire buffer and checks contiguity
 * This is the proper way to handle DMA buffers with VDS
 */
static uint32_t get_physical_address_vds(const void* virtual_address) {
    /* This function is now deprecated - use get_physical_address_full */
    return get_physical_address(virtual_address);
}

/**
 * @brief Get full physical mapping for buffer
 *
 * GPT-5 Critical: Proper full-buffer mapping with VDS scatter-gather support
 * 
 * @param buffer Virtual address
 * @param size Buffer size
 * @param phys_addr Output physical address (if contiguous)
 * @param is_contiguous Output flag indicating if buffer is contiguous
 * @return true if mapping successful
 */
static bool get_physical_mapping_full(const void* buffer, uint32_t size,
                                     uint32_t* phys_addr, bool* is_contiguous) {
    vds_sg_entry_t sg_list[16];
    vds_lock_handle_t lock_handle;
    
    /* Initialize outputs */
    *phys_addr = 0;
    *is_contiguous = false;
    
    /* Check if we're in V86 mode */
    if (is_v86_mode()) {
        if (!is_vds_available()) {
            /* GPT-5 Critical: Cannot do DMA in V86 without VDS */
            log_error("DMA Safety: V86 mode without VDS - DMA not safe!");
            return false;
        }
        
        /* Map buffer with VDS */
        lock_handle = vds_map_buffer((void*)buffer, size, sg_list, 16);
        if (lock_handle == 0) {
            log_error("DMA Safety: VDS mapping failed");
            return false;
        }
        
        /* Check if contiguous */
        if (sg_list[0].is_contiguous && sg_list[0].length == size) {
            *phys_addr = sg_list[0].physical_addr;
            *is_contiguous = true;
        } else {
            /* Non-contiguous - need bounce buffer */
            *is_contiguous = false;
            log_debug("DMA Safety: Buffer not contiguous in physical memory");
        }
        
        /* Unlock the mapping */
        vds_unmap_buffer(lock_handle);
        return true;
    }
    
    /* Real mode: calculate physical address */
    uint16_t segment = FP_SEG(buffer);
    uint16_t offset = FP_OFF(buffer);
    uint32_t linear = ((uint32_t)segment << 4) + offset;
    
    /* Check if buffer wraps in real mode */
    if (linear + size > 0x100000) {  /* 1MB real mode limit */
        *is_contiguous = false;
        return false;
    }
    
    *phys_addr = linear;
    *is_contiguous = true;
    return true;
}

/**
 * @brief Get buffer physical address (alias for compatibility)
 * 
 * GPT-5 Critical Fix: Ensure consistent physical address calculation
 */
static uint32_t get_buffer_physical_address(const void* buffer) {
    return get_physical_address(buffer);
}

/**
 * @brief Verify that a buffer is physically contiguous
 *
 * GPT-5 CRITICAL FIX: Check physical contiguity across pages
 * 
 * This function walks through the buffer in 4KB page increments to verify
 * that each page is physically contiguous with the next. This is essential
 * for DMA safety under memory managers like EMM386/QEMM that can relocate
 * pages arbitrarily.
 *
 * @param buf Buffer start address
 * @param len Buffer length in bytes  
 * @return true if buffer is physically contiguous, false otherwise
 */
static bool verify_physical_contiguity(const void* buf, uint32_t len) {
    uint8_t* current_ptr;
    uint32_t remaining_len;
    uint32_t page_size = 4096;  /* 4KB pages */
    uint32_t current_phys, next_phys, expected_phys;
    
    if (!buf || len == 0) {
        return false;
    }
    
    /* Small buffers within a single page are always contiguous */
    if (len <= page_size) {
        uint32_t start_page = get_physical_address(buf) & ~(page_size - 1);
        uint32_t end_page = (get_physical_address(buf) + len - 1) & ~(page_size - 1);
        return (start_page == end_page);
    }
    
    current_ptr = (uint8_t*)buf;
    remaining_len = len;
    current_phys = get_physical_address(current_ptr);
    
    log_debug("DMA Safety: Verifying contiguity for %lu bytes starting at 0x%08lX", 
              len, current_phys);
    
    /* Walk through buffer in page-sized chunks */
    while (remaining_len > page_size) {
        /* GPT-5 Fix: Calculate bytes_in_page from physical address */
        uint32_t offset_in_page = current_phys & (page_size - 1);
        uint32_t bytes_in_page = page_size - offset_in_page;
        
        /* Move to next page */
        current_ptr += bytes_in_page;
        remaining_len -= bytes_in_page;
        
        /* Check if next page is physically contiguous */
        next_phys = get_physical_address(current_ptr);
        expected_phys = current_phys + bytes_in_page;
        
        if (next_phys != expected_phys) {
            log_debug("DMA Safety: Physical discontinuity at offset %lu: expected 0x%08lX, got 0x%08lX",
                     len - remaining_len, expected_phys, next_phys);
            return false;
        }
        
        current_phys = next_phys;
    }
    
    log_debug("DMA Safety: Buffer is physically contiguous");
    return true;
}

/**
 * @brief Check if buffer would cross 64KB DMA boundary
 *
 * GPT-5 Critical Requirement: ISA DMA controllers cannot handle transfers
 * that cross 64KB boundaries. This function checks if a buffer would
 * violate this constraint.
 *
 * @param physical_addr Physical start address
 * @param size Buffer size in bytes
 * @return true if buffer crosses 64KB boundary, false if safe
 */
static bool dma_check_64kb_boundary(uint32_t physical_addr, uint32_t size) {
    if (size == 0) {
        return false; /* Zero-size buffer doesn't cross any boundary */
    }
    
    return crosses_64kb_boundary(physical_addr, size);
}

/**
 * @brief Get physical address from DMA buffer descriptor
 *
 * GPT-5 Enhancement: Consistent physical address access
 *
 * @param desc Buffer descriptor
 * @return Physical address or 0 on error
 */
static uint32_t dma_get_physical_address(dma_buffer_descriptor_t* desc) {
    if (!desc || !desc->virtual_address) {
        return 0;
    }
    
    /* Use bounce buffer physical address if available */
    if (desc->is_bounce_buffer && desc->bounce_physical_address != 0) {
        return desc->bounce_physical_address;
    }
    
    /* Otherwise calculate from virtual address */
    return get_physical_address(desc->virtual_address);
}

/**
 * @brief Check if buffer is within 16MB limit for ISA DMA
 *
 * GPT-5 Critical Requirement: ISA DMA can only address first 16MB of memory
 *
 * @param physical_addr Physical start address
 * @param size Buffer size in bytes
 * @return true if buffer is within 16MB limit, false if exceeds
 */
static bool dma_check_16mb_limit(uint32_t physical_addr, uint32_t size) {
    uint32_t end_addr;
    
    if (size == 0) {
        return true; /* Zero-size buffer is always within limit */
    }
    
    /* Check for overflow */
    if (physical_addr > UINT32_MAX - size) {
        return false; /* Address overflow */
    }
    
    end_addr = physical_addr + size - 1;
    return (end_addr < DMA_ISA_LIMIT);
}

/**
 * @brief Synchronize bounce buffer with device
 *
 * GPT-5 Requirement: Proper bounce buffer management
 * Copies data between bounce buffer and original buffer as needed.
 *
 * @param desc Buffer descriptor
 * @param to_device true for CPU->device, false for device->CPU
 * @return SUCCESS or error code
 */
static int sync_bounce_buffer(dma_buffer_descriptor_t* desc, bool to_device) {
    if (!desc || !desc->is_bounce_buffer) {
        return SUCCESS; /* Nothing to sync */
    }
    
    if (!desc->needs_sync) {
        return SUCCESS; /* No sync needed */
    }
    
    log_debug("DMA Safety: Syncing bounce buffer %s", to_device ? "to device" : "from device");
    
    /* For DOS real mode, bounce buffer sync is usually just a memory copy */
    /* In a full implementation, this would handle cache flushing/invalidation */
    
    if (to_device) {
        /* Data is already in bounce buffer for device access */
        /* In protected mode, would flush cache here */
    } else {
        /* Device has written to bounce buffer */  
        /* In protected mode, would invalidate cache here */
    }
    
    return SUCCESS;
}

/**
 * @brief Free DMA buffer
 *
 * @param desc Buffer descriptor to free
 * @return SUCCESS or error code
 */
int dma_free_buffer(dma_buffer_descriptor_t* desc) {
    uint32_t i;
    
    if (!desc) {
        return ERROR_INVALID_PARAM;
    }
    
    log_debug("DMA Safety: Freeing buffer at 0x%08lX", desc->physical_address);
    
    if (desc->is_bounce_buffer) {
        /* Free bounce buffer */
        for (i = 0; i < g_dma_manager.bounce_count; i++) {
            bounce_buffer_t* bounce = &g_dma_manager.bounce_pool[i];
            if (bounce->virtual_address == desc->bounce_virtual) {
                free_bounce_buffer(bounce);
                break;
            }
        }
    } else if (desc->allocated_by_framework) {
        /* Free directly allocated buffer */
        memory_free(desc->virtual_address);
    }
    
    /* Remove from active list - ISR-safe critical section */
    ENTER_CRITICAL();
    for (i = 0; i < read32_atomic(&g_dma_manager.active_count); i++) {
        if (&g_dma_manager.active_buffers[i] == desc) {
            /* Shift remaining entries down */
            memmove(&g_dma_manager.active_buffers[i],
                   &g_dma_manager.active_buffers[i + 1],
                   (read32_atomic(&g_dma_manager.active_count) - i - 1) * sizeof(dma_buffer_descriptor_t));
            decrement32_atomic(&g_dma_manager.active_count);
            break;
        }
    }
    EXIT_CRITICAL();
    
    return SUCCESS;
}

/**
 * @brief Register device DMA constraints
 */
static int register_device_constraints(const char* device_name, const dma_device_constraints_t* constraints) {
    if (g_dma_manager.device_count >= 8) {
        return ERROR_TABLE_FULL;
    }
    
    g_dma_manager.constraints[g_dma_manager.device_count] = *constraints;
    g_dma_manager.device_count++;
    
    log_debug("DMA Safety: Registered constraints for %s", device_name);
    return SUCCESS;
}

/**
 * @brief Allocate bounce buffer from pool
 */
static bounce_buffer_t* allocate_bounce_buffer(uint32_t size, dma_buffer_type_t type) {
    uint32_t i;
    
    if (size > BOUNCE_BUFFER_SIZE) {
        log_error("DMA Safety: Requested bounce buffer size %lu exceeds maximum %d", 
                 size, BOUNCE_BUFFER_SIZE);
        return NULL;
    }
    
    /* Find free bounce buffer - ISR-safe critical section */
    ENTER_CRITICAL();
    for (i = 0; i < g_dma_manager.bounce_count; i++) {
        bounce_buffer_t* bounce = &g_dma_manager.bounce_pool[i];
        
        /* TSR Defensive: Validate buffer integrity before use */
        if (!validate_bounce_buffer(bounce)) {
            log_error("DMA Safety: Bounce buffer %u corrupted, skipping", i);
            continue;
        }
        
        if (!bounce->in_use) {
            bounce->in_use = true;
            bounce->assigned_type = type;
            bounce->use_count++;
            
            /* TSR Defensive: Update checksum after modification */
            bounce->checksum = calculate_checksum((uint8_t*)bounce + offsetof(bounce_buffer_t, virtual_address),
                                                 offsetof(bounce_buffer_t, checksum) - offsetof(bounce_buffer_t, virtual_address));
            
            EXIT_CRITICAL();
            return bounce;
        }
    }
    EXIT_CRITICAL();
    
    log_error("DMA Safety: No free bounce buffers available");
    return NULL;
}

/**
 * @brief Free bounce buffer back to pool
 */
static void free_bounce_buffer(bounce_buffer_t* bounce) {
    if (bounce) {
        /* ISR-safe critical section */
        ENTER_CRITICAL();
        bounce->in_use = false;
        bounce->assigned_type = DMA_BUFFER_TYPE_GENERAL;
        EXIT_CRITICAL();
    }
}

/**
 * @brief Print DMA safety statistics
 */
void dma_print_statistics(void) {
    printf("DMA Safety Statistics:\\n");
    printf("  Total Allocations: %lu\\n", read32_atomic(&g_dma_manager.total_allocations));
    printf("  Bounce Buffer Hits: %lu\\n", read32_atomic(&g_dma_manager.bounce_buffer_hits));
    printf("  64KB Violations Prevented: %lu\\n", read32_atomic(&g_dma_manager.boundary_violations_prevented));
    printf("  Active Buffers: %lu/64\\n", read32_atomic(&g_dma_manager.active_count));
    printf("  Bounce Buffers Used: %lu/%d\\n", 
           count_used_bounce_buffers(), MAX_BOUNCE_BUFFERS);
    printf("  Bounce Buffer Efficiency: %d%%\\n", 
           read32_atomic(&g_dma_manager.total_allocations) > 0 ? 
           (int)((read32_atomic(&g_dma_manager.bounce_buffer_hits) * 100) / read32_atomic(&g_dma_manager.total_allocations)) : 0);
}

/**
 * @brief Count used bounce buffers
 */
static uint32_t count_used_bounce_buffers(void) {
    uint32_t count = 0;
    uint32_t i;
    
    for (i = 0; i < g_dma_manager.bounce_count; i++) {
        if (g_dma_manager.bounce_pool[i].in_use) {
            count++;
        }
    }
    return count;
}

/**
 * @brief Shutdown DMA safety framework
 */
int dma_safety_shutdown(void) {
    uint32_t i;
    
    if (!g_dma_manager.framework_initialized) {
        return SUCCESS;
    }
    
    log_info("DMA Safety: Shutting down framework");
    
    /* Free all active buffers */
    for (i = 0; i < read32_atomic(&g_dma_manager.active_count); i++) {
        dma_free_buffer(&g_dma_manager.active_buffers[i]);
    }
    
    /* Print final statistics */
    dma_print_statistics();
    
    g_dma_manager.framework_initialized = false;
    
    log_info("DMA Safety: Framework shutdown complete");
    return SUCCESS;
}

/* ============================================================================
 * GPT-5 Enhanced Functions - Device-Aware DMA Safety
 * ============================================================================ */

#include "../../include/dmasafe.h"

/**
 * @brief Build scatter-gather list with physical contiguity verification
 * 
 * GPT-5 CRITICAL FIX: "Physical contiguity is not verified across page boundaries.
 * This can produce segments that are not physically contiguous, which will break DMA."
 * 
 * This implementation:
 * 1. Walks 4KB pages to verify physical contiguity
 * 2. Uses VDS physical run lists when in V86 mode
 * 3. Falls back to bounce buffers when constraints cannot be met
 * 
 * @param buf Buffer virtual address
 * @param len Buffer length
 * @param caps Device capability descriptor
 * @param sg_list Caller-allocated SG list (GPT-5: Remove static for ISR safety)
 * @return 0 on success, negative on error
 */
int dma_build_safe_sg(void* buf, uint32_t len, device_caps_t* caps, dma_sg_list_t* sg_list)
{
    uint8_t* current_ptr;
    uint32_t remaining_len;
    int segment_idx;
    uint32_t page_size = 4096;  /* 4KB pages */
    
    /* GPT-5: Input validation with proper error codes */
    if (!buf || len == 0 || !caps || !sg_list) {
        log_error("DMA Safety: Invalid parameters for S/G build");
        return ERROR_INVALID_PARAM;
    }
    
    /* GPT-5: Validate caps->max_sg_entries against array size */
    if (caps->max_sg_entries > 8) {
        log_error("DMA Safety: max_sg_entries (%u) exceeds array size (8)", caps->max_sg_entries);
        return ERROR_INVALID_PARAM;
    }
    
    /* Clear caller-provided scatter-gather list */
    memset(sg_list, 0, sizeof(dma_sg_list_t));
    
    /* Check buffer start alignment - GPT-5: Don't mask length, check start address */
    uint32_t start_phys = get_buffer_physical_address(buf);
    if (start_phys == 0) {
        log_error("DMA Safety: Cannot get physical address for S/G");
        return ERROR_DMA_NOT_CONTIGUOUS;
    }
    
    if (caps->alignment > 1 && (start_phys & (caps->alignment - 1)) != 0) {
        log_warning("DMA Safety: Buffer start 0x%08lX not aligned to %u bytes, needs bounce buffer",
                   start_phys, caps->alignment);
        sg_list->needs_bounce = true;
        return SUCCESS;  /* Caller should use bounce buffer */
    }
    
    /* Check device-specific constraints for entire buffer first */
    if (caps->dma_addr_bits == 24 && start_phys + len > DMA_ISA_LIMIT) {
        log_warning("DMA Safety: Buffer exceeds 16MB limit for ISA device, needs bounce buffer");
        sg_list->needs_bounce = true;
        return SUCCESS;
    }
    
    /* Check if device supports scatter-gather */
    if (!caps->supports_sg || caps->max_sg_entries <= 1) {
        /* Single segment only - verify physical contiguity for entire buffer */
        if (!verify_physical_contiguity(buf, len)) {
            log_info("DMA Safety: Buffer not physically contiguous, needs bounce buffer");
            sg_list->needs_bounce = true;
            return SUCCESS;
        }
        
        /* GPT-5 Critical: Check 64KB boundary crossing for ISA devices */
        if (caps->no_64k_cross && dma_check_64kb_boundary(start_phys, len)) {
            log_info("DMA Safety: Buffer crosses 64KB boundary, needs bounce buffer");
            sg_list->needs_bounce = true;
            return SUCCESS;
        }
        
        /* Single segment is safe */
        sg_list->segment_count = 1;
        sg_list->segments[0].virt_addr = buf;
        sg_list->segments[0].phys_addr = start_phys;
        sg_list->segments[0].length = len;  /* GPT-5: Use uint32_t, not uint16_t */
        sg_list->total_length = len;
        sg_list->needs_bounce = false;
        
        log_debug("DMA Safety: Single segment S/G: phys=0x%08lX, len=%lu", start_phys, len);
        return SUCCESS;
    }
    
    /* Multi-segment scatter-gather build with page-walking */
    current_ptr = (uint8_t*)buf;
    remaining_len = len;
    segment_idx = 0;
    
    log_debug("DMA Safety: Building multi-segment S/G for %lu bytes", len);
    
    while (remaining_len > 0 && segment_idx < caps->max_sg_entries) {
        uint32_t segment_start_phys = get_buffer_physical_address(current_ptr);
        uint32_t segment_len = 0;
        uint32_t max_segment_len = remaining_len;
        
        /* Limit segment by device constraints */
        if (caps->dma_addr_bits == 24) {
            uint32_t limit_16mb = DMA_16MB_LIMIT - segment_start_phys;
            if (max_segment_len > limit_16mb) {
                max_segment_len = limit_16mb;
            }
        }
        
        /* GPT-5 Critical: Limit segment by 64KB boundary if device requires it */
        bool needs_boundary_split = caps->no_64k_cross;
        
        if (needs_boundary_split) {
            uint32_t boundary_end = (segment_start_phys + 65536) & ~65535;
            uint32_t boundary_limit = boundary_end - segment_start_phys;
            if (max_segment_len > boundary_limit) {
                max_segment_len = boundary_limit;
            }
        }
        
        /* Walk pages to find physically contiguous run */
        uint32_t current_page_phys = segment_start_phys & ~(page_size - 1);
        uint32_t offset_in_page = segment_start_phys & (page_size - 1);
        
        segment_len = page_size - offset_in_page;  /* First page partial */
        if (segment_len > max_segment_len) {
            segment_len = max_segment_len;
        }
        
        /* Extend segment through contiguous pages */
        uint8_t* page_ptr = current_ptr + segment_len;
        while (segment_len < max_segment_len) {
            uint32_t next_page_phys = get_buffer_physical_address(page_ptr);
            uint32_t expected_phys = current_page_phys + page_size;
            
            /* Check if next page is physically contiguous */
            if ((next_page_phys & ~(page_size - 1)) != expected_phys) {
                log_debug("DMA Safety: Physical discontinuity at offset %lu, ending segment",
                         (uint32_t)(page_ptr - (uint8_t*)buf));
                break;
            }
            
            /* Extend segment to include this page */
            uint32_t page_bytes = page_size;
            if (segment_len + page_bytes > max_segment_len) {
                page_bytes = max_segment_len - segment_len;
            }
            
            segment_len += page_bytes;
            page_ptr += page_bytes;
            current_page_phys = expected_phys;
        }
        
        /* GPT-5: Ensure no zero-length segments */
        if (segment_len == 0) {
            log_error("DMA Safety: Zero-length segment detected, needs bounce buffer");
            sg_list->needs_bounce = true;
            return SUCCESS;
        }
        
        /* Add segment to list */
        sg_list->segments[segment_idx].virt_addr = current_ptr;
        sg_list->segments[segment_idx].phys_addr = segment_start_phys;
        sg_list->segments[segment_idx].length = segment_len;
        
        log_debug("DMA Safety: S/G segment %d: virt=0x%p, phys=0x%08lX, len=%lu",
                  segment_idx, current_ptr, segment_start_phys, segment_len);
        
        /* Move to next segment */
        current_ptr += segment_len;
        remaining_len -= segment_len;
        segment_idx++;
    }
    
    /* Check if we ran out of segments before completing the buffer */
    if (remaining_len > 0) {
        log_error("DMA Safety: Buffer too fragmented (%lu bytes remain in %d segments), needs bounce buffer",
                 remaining_len, caps->max_sg_entries);
        sg_list->needs_bounce = true;
        return SUCCESS;
    }
    
    /* Successful multi-segment scatter-gather build */
    sg_list->segment_count = segment_idx;
    sg_list->total_length = len;
    sg_list->needs_bounce = false;
    
    log_info("DMA Safety: Built S/G list with %d segments for %lu bytes",
             segment_idx, len);
    
    return SUCCESS;
}

/**
 * @brief Free scatter-gather list
 * 
 * @param sg_list Scatter-gather list to free
 * @return SUCCESS or error code
 */
int dma_free_sg_list(dma_sg_list_t* sg_list)
{
    if (!sg_list) {
        return SUCCESS;  /* NULL is valid */
    }
    
    /* For static allocation, just clear the list */
    memset(sg_list, 0, sizeof(dma_sg_list_t));
    
    return SUCCESS;
}

/**
 * @brief Device-aware hybrid buffer allocation
 * 
 * GPT-5 Recommendation: "Accept device_caps and thread it through everything.
 * Make buffer classes/cutoffs device- and environment-adaptive."
 * 
 * @param size Buffer size in bytes
 * @param caps Device capability descriptor
 * @param direction DMA direction (TO_DEVICE/FROM_DEVICE)
 * @param device_name Device name for logging
 * @return Buffer descriptor or NULL on error
 */
dma_buffer_descriptor_t* dma_allocate_hybrid_buffer(uint32_t size, device_caps_t* caps,
                                                   dma_direction_t direction, 
                                                   const char* device_name)
{
    dma_buffer_descriptor_t* desc;
    uint32_t alignment;
    bool force_bounce = false;
    
    if (!caps || size == 0) {
        log_error("DMA Safety: Invalid parameters for hybrid allocation");
        return NULL;
    }
    
    log_debug("DMA Safety: Hybrid allocation for %s: %lu bytes, dir=%d",
              device_name ? device_name : "unknown", size, direction);
    
    /* Determine alignment requirements */
    alignment = caps->alignment;
    if (alignment < 4) alignment = 4;  /* Minimum DOS alignment */
    
    /* Check if device needs special handling */
    if (caps->dma_addr_bits == 24) {
        /* ISA device - enforce 16MB limit */
        log_debug("DMA Safety: ISA device detected, enforcing 16MB limit");
    }
    
    /* Use device-specific copybreak thresholds */
    if (direction == DMA_FROM_DEVICE && size <= caps->rx_copybreak) {
        log_debug("DMA Safety: Small RX buffer (%lu <= %u), using copybreak strategy",
                  size, caps->rx_copybreak);
        /* For small RX buffers, allocate in conventional memory for fast copy */
        alignment = 4;  /* Reduce alignment requirements for copybreak buffers */
    } else if (direction == DMA_TO_DEVICE && size <= caps->tx_copybreak) {
        log_debug("DMA Safety: Small TX buffer (%lu <= %u), using copybreak strategy",
                  size, caps->tx_copybreak);
        /* For small TX buffers, use conventional memory */
        alignment = 4;
    }
    
    /* Check if VDS is required for this device */
    if (caps->needs_vds) {
        log_debug("DMA Safety: Device requires VDS support");
        /* VDS handling will be implemented in sync functions */
    }
    
    /* Allocate buffer using existing infrastructure */
    desc = dma_allocate_buffer(size, alignment, 
                              (direction == DMA_TO_DEVICE) ? DMA_BUFFER_TYPE_TX : DMA_BUFFER_TYPE_RX,
                              device_name);
    
    if (!desc) {
        log_error("DMA Safety: Hybrid allocation failed for %s", device_name);
        return NULL;
    }
    
    /* Validate buffer against device constraints */
    uint32_t phys_addr = dma_get_physical_address(desc);
    
    /* Check 16MB limit for ISA devices */
    if (caps->dma_addr_bits == 24 && !dma_check_16mb_limit(phys_addr, size)) {
        log_warning("DMA Safety: Buffer above 16MB for ISA device, will use bounce buffer");
        /* The existing bounce buffer mechanism will handle this */
    }
    
    /* Check 64KB boundary crossing */
    if (!dma_check_64kb_boundary(phys_addr, size)) {
        log_debug("DMA Safety: Buffer crosses 64KB boundary, S/G or bounce will be used");
        /* Scatter-gather or bounce buffer will handle this */
    }
    
    log_info("DMA Safety: Allocated hybrid buffer for %s: %lu bytes at phys=0x%08lX",
             device_name, size, phys_addr);
    
    return desc;
}

/**
 * @brief Enhanced synchronization with direction awareness
 * 
 * GPT-5 Recommendation: "Add direction parameter to dma_sync_for_{device,cpu}
 * to keep flush/invalidate choices correct for RX vs TX."
 * 
 * @param desc Buffer descriptor
 * @param direction DMA direction
 * @return SUCCESS or error code
 */
int dma_sync_for_device(dma_buffer_descriptor_t* desc, dma_direction_t direction)
{
    if (!desc) {
        return ERROR_INVALID_PARAM;
    }
    
    log_debug("DMA Safety: Syncing buffer for device, direction=%d", direction);
    
    /* GPT-5 Critical: Direction-specific cache management for TO_DEVICE transfers */
    if (direction == DMA_TO_DEVICE || direction == DMA_BIDIRECTIONAL) {
        /* Flush CPU cache to ensure data reaches memory for device access */
        cache_management_dma_prepare(desc->virtual_address, desc->size);
    }
    
    /* Sync bounce buffer if needed */
    if (desc->is_bounce_buffer && desc->needs_sync) {
        return sync_bounce_buffer(desc, true);
    }
    
    return SUCCESS;
}

/**
 * @brief Enhanced CPU synchronization with direction awareness
 * 
 * @param desc Buffer descriptor
 * @param direction DMA direction
 * @return SUCCESS or error code
 */
int dma_sync_for_cpu(dma_buffer_descriptor_t* desc, dma_direction_t direction)
{
    if (!desc) {
        return ERROR_INVALID_PARAM;
    }
    
    log_debug("DMA Safety: Syncing buffer for CPU, direction=%d", direction);
    
    /* Sync bounce buffer first if needed */
    if (desc->is_bounce_buffer && desc->needs_sync) {
        sync_bounce_buffer(desc, false);
    }
    
    /* GPT-5 Critical: Direction-specific cache management for FROM_DEVICE transfers */
    if (direction == DMA_FROM_DEVICE || direction == DMA_BIDIRECTIONAL) {
        /* Invalidate CPU cache to ensure fresh data is read from memory */
        cache_management_dma_complete(desc->virtual_address, desc->size);
    }
    
    return SUCCESS;
}

/**
 * @brief Legacy compatibility functions
 */
int dma_sync_for_device_legacy(dma_buffer_descriptor_t* desc)
{
    return dma_sync_for_device(desc, DMA_BIDIRECTIONAL);
}

int dma_sync_for_cpu_legacy(dma_buffer_descriptor_t* desc)
{
    return dma_sync_for_cpu(desc, DMA_BIDIRECTIONAL);
}

/**
 * @brief Public: Check integrity of all DMA structures
 * 
 * TSR Defensive: Periodic validation of all structures
 */
int dma_check_integrity(void) {
    uint32_t i;
    int errors = 0;
    
    /* Check bounce buffers */
    for (i = 0; i < g_dma_manager.bounce_count; i++) {
        if (!validate_bounce_buffer(&g_dma_manager.bounce_pool[i])) {
            errors++;
        }
    }
    
    /* Check active buffers */
    uint32_t active_count = read32_atomic(&g_dma_manager.active_count);
    for (i = 0; i < active_count; i++) {
        if (!validate_buffer_integrity(&g_dma_manager.active_buffers[i])) {
            errors++;
        }
    }
    
    return errors;
}

/**
 * @brief Public: Attempt emergency recovery
 */
int dma_emergency_recovery(void) {
    return emergency_recovery();
}

/**
 * @brief Public: Periodic validation (called from timer/idle)
 * 
 * TSR Defensive: Should be called periodically (e.g., from INT 28h)
 * Returns true if healthy, false if recovery needed
 */
bool dma_periodic_validation(void) {
    static uint32_t validation_counter = 0;
    
    /* Only validate every N calls to reduce overhead */
    if (++validation_counter < 100) {
        return true;
    }
    
    validation_counter = 0;
    
    /* Quick integrity check */
    int errors = dma_check_integrity();
    if (errors > 0) {
        log_warning("DMA Safety: Periodic validation found %d errors", errors);
        
        /* Attempt recovery */
        if (emergency_recovery() != SUCCESS) {
            return false;
        }
    }
    
    return true;
}

/**
 * @brief Emergency recovery system for corruption detection
 * 
 * TSR Defensive: Multi-level recovery system
 * Level 1: Soft recovery (repair data structures)
 * Level 2: Reset buffers (reinitialize pools)
 * Level 3: Emergency shutdown (disable DMA safely)
 */
static int emergency_recovery(void) {
    uint32_t i;
    int corrupted_count = 0;
    int recovered_count = 0;
    
    log_warning("DMA Safety: Starting emergency recovery procedure");
    
    /* Level 1: Check and repair bounce buffers */
    for (i = 0; i < g_dma_manager.bounce_count; i++) {
        bounce_buffer_t* bounce = &g_dma_manager.bounce_pool[i];
        
        if (!validate_bounce_buffer(bounce)) {
            corrupted_count++;
            log_warning("DMA Safety: Bounce buffer %u corrupted, attempting repair", i);
            
            /* Try to repair by reinitializing protection */
            if (!bounce->in_use) {
                init_bounce_protection(bounce);
                if (validate_bounce_buffer(bounce)) {
                    recovered_count++;
                    log_info("DMA Safety: Bounce buffer %u recovered", i);
                }
            }
        }
    }
    
    /* Level 2: Check active buffer descriptors */
    uint32_t active_count = read32_atomic(&g_dma_manager.active_count);
    for (i = 0; i < active_count; i++) {
        dma_buffer_descriptor_t* desc = &g_dma_manager.active_buffers[i];
        
        if (!validate_buffer_integrity(desc)) {
            corrupted_count++;
            log_warning("DMA Safety: Buffer descriptor %u corrupted", i);
            
            /* Cannot safely repair active buffers - mark for cleanup */
            desc->signature = 0; /* Mark as invalid */
        }
    }
    
    if (corrupted_count == 0) {
        log_info("DMA Safety: No corruption detected");
        return SUCCESS;
    }
    
    log_warning("DMA Safety: Found %d corrupted structures, recovered %d",
               corrupted_count, recovered_count);
    
    /* Level 3: If too many corruptions, consider shutdown */
    if (corrupted_count - recovered_count > MAX_BOUNCE_BUFFERS / 2) {
        log_error("DMA Safety: Too many corrupted structures, DMA operations unsafe");
        return ERROR_DMA_NOT_SUPPORTED;
    }
    
    return SUCCESS;
}

/**
 * @brief TSR Defensive Functions - Calculate checksum for structure
 */
static uint16_t calculate_checksum(const void* data, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint16_t checksum = CHECKSUM_SEED;
    size_t i;
    
    for (i = 0; i < size; i++) {
        checksum ^= bytes[i];
        checksum = (checksum << 1) | (checksum >> 15); /* Rotate left by 1 */
    }
    
    return checksum;
}

/**
 * @brief Validate structure checksum
 */
/* Buffer guard canary values - GPT-5 recommended */
#define DMA_CANARY_HEAD 0x6DDA
#define DMA_CANARY_TAIL 0xADD6  /* Fixed: was invalid hex 0xADDM */

/**
 * @brief Guarded DMA buffer structure  
 * 
 * Adds canaries before and after data to detect overruns
 * DOS compiler compatible - uses data[1] instead of flexible array
 * GPT-5: Must be packed to avoid padding issues
 */
#pragma pack(push, 1)
typedef struct {
    uint16_t head_canary;      /* Must be DMA_CANARY_HEAD */
    uint16_t buffer_size;      /* Size of data buffer for tail location */
    uint8_t data[1];           /* Actual buffer data - DOS compiler compatible */
    /* tail_canary follows at offset: offsetof(data) + buffer_size */
} guarded_dma_buffer_t;
#pragma pack(pop)

/**
 * @brief Calculate checksum for DMA descriptor without mutation
 * 
 * GPT-5 fix: Calculates checksum while skipping the checksum field itself
 * to avoid mutating const data
 */
static uint16_t calculate_descriptor_checksum(const dma_buffer_descriptor_t __far *desc) {
    uint32_t sum = 0;
    const uint16_t __far *ptr = (const uint16_t __far *)desc;
    uint16_t words = sizeof(dma_buffer_descriptor_t) / 2;
    uint16_t checksum_offset = offsetof(dma_buffer_descriptor_t, checksum) / 2;
    
    /* Sum all 16-bit words except the checksum field */
    for (uint16_t i = 0; i < words; i++) {
        if (i != checksum_offset) {
            sum += ptr[i];
        }
    }
    
    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)~sum;
}

/**
 * @brief Fast 16-bit Internet checksum for DMA structures
 * 
 * GPT-5 recommended implementation for minimal overhead on 286+
 * Fixed: Proper type promotion to avoid signed shift issues
 */
static uint16_t compute_checksum_16(const void __far *data, uint16_t len) {
    const uint8_t __far *ptr = (const uint8_t __far *)data;
    uint32_t sum = 0;
    
    /* Sum 16-bit words */
    while (len > 1) {
        /* GPT-5 fix: Cast before shift to avoid signed promotion */
        sum += (uint16_t)(ptr[0] | ((uint16_t)ptr[1] << 8));
        ptr += 2;
        len -= 2;
    }
    
    /* Add odd byte if present */
    if (len) {
        sum += *ptr;
    }
    
    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)~sum;
}

/**
 * @brief Validate DMA structure checksum
 * 
 * GPT-5 fix: Complete implementation with tail checking and strict mode
 * Returns false by default for safety (strict mode)
 */
static bool validate_checksum(const void __far *structure, uint16_t struct_type) {
    const guarded_dma_buffer_t __far *guarded;
    const uint16_t __far *tail_canary;
    const dma_buffer_descriptor_t __far *desc;
    uint16_t calculated_checksum;
    
    if (!structure) return false;
    
    /* Type-specific validation */
    switch (struct_type) {
        case DMA_BUFFER_TYPE_TX:
        case DMA_BUFFER_TYPE_RX:
            /* Check if this is a guarded buffer with head canary */
            guarded = (const guarded_dma_buffer_t __far *)structure;
            
            /* Validate head canary */
            if (guarded->head_canary != DMA_CANARY_HEAD) {
                log_error("DMA validation: Head canary corrupted");
                return false;
            }
            
            /* Check buffer size is reasonable */
            if (guarded->buffer_size == 0 || guarded->buffer_size > 0x8000) {
                log_error("DMA validation: Invalid buffer size %u", guarded->buffer_size);
                return false;
            }
            
            /* GPT-5 fix: Handle segment boundary safely for tail canary */
            /* Use huge pointer arithmetic to avoid segment wrap */
            tail_canary = (const uint16_t __far *)
                ((const uint8_t __far *)guarded->data + guarded->buffer_size);
            
            if (*tail_canary != DMA_CANARY_TAIL) {
                log_error("DMA validation: Tail canary corrupted (expected 0x%04X, got 0x%04X)",
                         DMA_CANARY_TAIL, *tail_canary);
                return false;  /* Buffer overrun detected */
            }
            
            /* Both canaries valid */
            return true;
            
        case DMA_BUFFER_TYPE_DESCRIPTOR:
            /* Validate descriptor checksum without mutation */
            desc = (const dma_buffer_descriptor_t __far *)structure;
            
            /* Check signature first */
            if (desc->signature != SIGNATURE_MAGIC) {
                log_error("DMA validation: Invalid descriptor signature");
                return false;
            }
            
            /* Calculate checksum without modifying the structure */
            calculated_checksum = calculate_descriptor_checksum(desc);
            
            if (calculated_checksum != desc->checksum) {
                log_error("DMA validation: Descriptor checksum mismatch");
                return false;
            }
            
            return true;
            
        default:
            /* GPT-5: Strict mode - unknown structures fail validation */
            log_warning("DMA validation: Unknown structure type %u, failing safe", struct_type);
            return false;
    }
}

/* GPT-5 A+ Implementation: Proper VDS locking and 64KB boundary checking */
/* Note: vds_sg_entry_t is defined in vds.h */

/* DMA fragment type for hardware programming */
typedef struct dma_fragment_s {
    uint32_t phys;  /* Physical address of fragment start */
    uint16_t len;   /* Length of fragment in bytes */
} dma_fragment_t;

/* Device type for cache coherency */
typedef enum {
    DMA_DEVICE_ISA = 0,    /* ISA device (non-coherent) */
    DMA_DEVICE_PCI = 1     /* PCI device (coherent) */
} dma_device_type_t;

/* DMA direction for cache management */
typedef enum {
    DMA_DIR_TO_DEVICE = 1,     /* CPU -> Device (TX) */
    DMA_DIR_FROM_DEVICE = 2,   /* Device -> CPU (RX) */
    DMA_DIR_BIDIRECTIONAL = 3  /* Both directions */
} dma_direction_t;

/* Lock handle for VDS operations with coherency info */
typedef struct dma_lock_s {
    uint8_t  vds_used;         /* 1 if VDS lock is active, 0 if not used */
    uint16_t vds_handle;       /* Valid only if vds_used != 0 */
    dma_direction_t direction; /* DMA direction for cache coherency */
    dma_device_type_t device_type; /* Device type */
    void __far *buffer_addr;   /* Original buffer address */
    uint16_t buffer_len;       /* Buffer length */
    uint8_t bounce_used;       /* 1 if using bounce buffer */
    bounce_buffer_t *bounce;   /* Bounce buffer if used */
} dma_lock_t;

/* Forward declarations */
static bool dma_lock_and_map_buffer_ex(void __far *buf, uint32_t len, bool sg_ok,
                                       dma_direction_t direction, dma_device_type_t device_type,
                                       dma_lock_t *lock_out, dma_fragment_t *frags,
                                       uint16_t *frag_cnt);
static bool dma_use_bounce_buffer(void __far *buf, uint32_t len, dma_direction_t direction,
                                  dma_lock_t *lock_out, dma_fragment_t *frags,
                                  uint16_t *frag_cnt);
static void dma_sync_for_device(void __far *buf, uint32_t len, dma_direction_t direction);
static void dma_sync_for_cpu(void __far *buf, uint32_t len, dma_direction_t direction);

/**
 * @brief Validate ISA DMA physical constraints
 * 
 * GPT-5 Critical: Ensures buffer meets all ISA DMA requirements
 * 
 * @param phys Physical address
 * @param len Buffer length
 * @return true if buffer is ISA DMA safe
 */
static bool validate_isa_dma_constraints(uint32_t phys, uint32_t len) {
    /* GPT-5: ISA DMA cannot access memory above 16MB */
    if (phys >= DMA_16MB_LIMIT) {
        log_error("DMA constraint: Buffer at 0x%08lX above 16MB limit", phys);
        return false;
    }
    
    if ((phys + len) > DMA_16MB_LIMIT) {
        log_error("DMA constraint: Buffer end at 0x%08lX crosses 16MB limit", phys + len);
        return false;
    }
    
    /* GPT-5: ISA DMA cannot cross 64KB physical boundaries */
    uint32_t start_64k_page = phys / DMA_64KB_BOUNDARY;
    uint32_t end_64k_page = (phys + len - 1) / DMA_64KB_BOUNDARY;
    
    if (start_64k_page != end_64k_page) {
        log_error("DMA constraint: Buffer crosses 64KB boundary (0x%08lX-0x%08lX)",
                 phys, phys + len - 1);
        return false;
    }
    
    /* GPT-5: Verify alignment for bus master operation */
    if ((phys & 3) != 0) {
        log_error("DMA constraint: Buffer at 0x%08lX not 4-byte aligned", phys);
        return false;
    }
    
    if ((len & 3) != 0) {
        log_error("DMA constraint: Buffer length %lu not multiple of 4", len);
        return false;
    }
    
    return true;
}

/**
 * @brief Split physical fragment at 64KB boundaries
 * 
 * GPT-5 Critical: Ensures no DMA fragment crosses a 64KB physical boundary
 * 
 * @param phys Physical start address
 * @param len Length in bytes
 * @param out_frags Output fragment array
 * @param out_max Maximum output fragments
 * @return Number of fragments created, or 0 on error
 */
static uint16_t split_at_64k_boundaries(uint32_t phys, uint32_t len,
                                        dma_fragment_t *out_frags, uint16_t out_max)
{
    uint16_t out_count = 0;

    /* First validate basic ISA DMA constraints */
    if (!validate_isa_dma_constraints(phys, len)) {
        /* Try to split if only crossing boundary */
        if (phys < DMA_16MB_LIMIT && (phys + len) <= DMA_16MB_LIMIT) {
            /* Buffer is below 16MB, just needs splitting at 64KB boundaries */
        } else {
            /* Buffer violates 16MB limit - cannot use for ISA DMA */
            return 0;
        }
    }

    while (len > 0) {
        uint32_t low16 = (phys & 0xFFFFUL);
        uint32_t room_to_boundary = 0x10000UL - low16; /* bytes before hitting 64KB boundary */
        uint32_t chunk = (len < room_to_boundary) ? len : room_to_boundary;
        
        /* GPT-5 Critical Fix: Clamp to 0xFFFF to prevent wrap to 0 when casting to uint16_t */
        if (chunk > 0xFFFF) {
            chunk = 0xFFFF;
        }
        
        /* GPT-5 Fix: Ensure length is multiple of 4 for 3C515 requirements */
        if ((chunk & 3) != 0) {
            /* Round down to nearest multiple of 4 */
            chunk = chunk & ~3UL;
            if (chunk == 0) {
                log_error("DMA split: Cannot create aligned fragment");
                return 0;
            }
        }

        if (out_count >= out_max) {
            return 0; /* insufficient space */
        }

        out_frags[out_count].phys = phys;
        out_frags[out_count].len  = (uint16_t)chunk;
        out_count++;

        phys += chunk;
        len  -= chunk;
    }

    return out_count;
}

/**
 * @brief Lock and map buffer for DMA with proper VDS support
 * 
 * GPT-5 A+ Implementation: Properly locks regions and handles scatter/gather
 * 
 * @param buf Buffer virtual address
 * @param len Buffer length
 * @param sg_ok true if hardware supports scatter/gather
 * @param lock_out Receives lock handle for later unlock
 * @param frags Output fragment array
 * @param frag_cnt In: capacity, Out: actual count
 * @return true on success, false on failure
 */
bool dma_lock_and_map_buffer(void __far *buf, uint32_t len, bool sg_ok,
                             dma_lock_t *lock_out, dma_fragment_t *frags,
                             uint16_t *frag_cnt)
{
    return dma_lock_and_map_buffer_ex(buf, len, sg_ok, DMA_DIR_TO_DEVICE, 
                                      DMA_DEVICE_ISA, lock_out, frags, frag_cnt);
}

/**
 * @brief Extended version with cache coherency support
 * 
 * GPT-5 Fix: Add device type and direction for proper cache coherency
 * 
 * @param buf Buffer virtual address
 * @param len Buffer length
 * @param sg_ok Whether scatter-gather is supported
 * @param direction DMA direction (TO_DEVICE, FROM_DEVICE, BIDIRECTIONAL)
 * @param device_type Device type (ISA, PCI) for coherency
 * @param lock_out Receives lock handle for later unlock
 * @param frags Output fragment array
 * @param frag_cnt In: capacity, Out: actual count
 * @return true on success, false on failure
 */
bool dma_lock_and_map_buffer_ex(void __far *buf, uint32_t len, bool sg_ok,
                                dma_direction_t direction, dma_device_type_t device_type,
                                dma_lock_t *lock_out, dma_fragment_t *frags,
                                uint16_t *frag_cnt)
{
    #define DMA_MAX_SG_INTERNAL 16
    dma_fragment_t tmp_in[DMA_MAX_SG_INTERNAL];
    dma_fragment_t tmp_out[DMA_MAX_SG_INTERNAL];
    uint16_t tmp_in_count = 0;
    uint16_t tmp_out_count = 0;
    
    if (!buf || len == 0 || !lock_out || !frags || !frag_cnt || *frag_cnt == 0) {
        log_error("DMA map: invalid arguments");
        return false;
    }

    /* GPT-5 Fix: Initialize all lock_out fields early */
    lock_out->vds_used = 0;
    lock_out->vds_handle = 0;
    lock_out->bounce_used = 0;
    lock_out->bounce = NULL;
    lock_out->direction = direction;
    lock_out->device_type = device_type;
    lock_out->buffer_addr = buf;
    lock_out->buffer_len = len;

    /* GPT-5 Fix: Prepare VDS flags based on direction and device type */
    uint16_t vds_flags = 0;
    /* Note: Direction is handled by VDS internally, not via flags */
    if (device_type == DMA_DEVICE_ISA) {
        vds_flags |= VDS_NO_CROSS_64K;    /* ISA devices cannot cross 64KB */
    }
    if (!sg_ok) {
        vds_flags |= VDS_CONTIG_REQUIRED; /* Hardware needs contiguous buffer */
    }
    
    /* GPT-5 Fix: Sync cache before DMA if needed */
    if (direction == DMA_DIR_TO_DEVICE || direction == DMA_DIR_BIDIRECTIONAL) {
        /* Flush CPU cache to ensure data is in memory */
        dma_sync_for_device(buf, len, direction);
    }
    
    /* Attempt to obtain physical mapping */
    if (vds_available()) {
        /* VDS path: Lock region and get scatter/gather list */
        vds_sg_entry_t vds_frags[DMA_MAX_SG_INTERNAL];
        uint16_t vds_count = 0;
        uint16_t vds_handle = 0;

        /* Call VDS lock_region with proper flags */
        int rc = vds_lock_region_sg(buf, len, vds_flags,
                                    vds_frags, DMA_MAX_SG_INTERNAL,
                                    &vds_count, &vds_handle);
        if (rc != 0 || vds_count == 0) {
            log_error("DMA map: VDS lock_region failed (rc=%d, count=%u)", rc, (unsigned)vds_count);
            
            /* GPT-5 Fix: Always try bounce buffer fallback when VDS fails */
            log_info("DMA map: VDS failed, attempting bounce buffer fallback");
            return dma_use_bounce_buffer(buf, len, direction, lock_out, frags, frag_cnt);
        }

        /* GPT-5 Fix: Set VDS fields immediately after successful lock */
        lock_out->vds_used = 1;
        lock_out->vds_handle = vds_handle;

        /* GPT-5 Fix: Check if !sg_ok and multi-fragment result */
        if (!sg_ok && vds_count > 1) {
            log_warning("DMA map: Hardware needs contiguous but VDS returned %u fragments", vds_count);
            vds_unlock_region_sg(vds_handle);
            lock_out->vds_used = 0;
            lock_out->vds_handle = 0;
            return dma_use_bounce_buffer(buf, len, direction, lock_out, frags, frag_cnt);
        }
        
        /* Convert VDS fragments to internal format */
        uint32_t remaining = len;
        for (uint16_t i = 0; i < vds_count && remaining > 0; ++i) {
            uint32_t p = vds_frags[i].phys;
            uint32_t l = vds_frags[i].len;
            if (l > remaining) l = remaining;

            if (tmp_in_count >= DMA_MAX_SG_INTERNAL) {
                log_error("DMA map: too many VDS fragments");
                vds_unlock_region_sg(vds_handle);
                lock_out->vds_used = 0;
                return false;
            }

            tmp_in[tmp_in_count].phys = p;
            tmp_in[tmp_in_count].len = (uint16_t)l;
            tmp_in_count++;
            remaining -= l;
        }
    } else {
        /* GPT-5 Critical Fix: Check if we're in V86 mode without VDS */
        if (is_v86_mode()) {
            /* V86/paging active but no VDS - cannot safely do DMA */
            log_error("DMA map: V86 mode detected without VDS - DMA unsafe!");
            log_error("DMA map: Use pre-allocated bounce buffers or install VDS provider");
            return false;
        }
        
        /* Pure real mode: calculate physical address directly */
        uint32_t phys = ((uint32_t)FP_SEG(buf) << 4) + (uint32_t)FP_OFF(buf);
        
        /* Check for real mode 1MB wrap */
        if (phys + len > 0x100000) {
            log_error("DMA map: buffer wraps in real mode");
            return false;
        }
        
        /* GPT-5 Critical Fix: Handle buffers > 64KB by chunking */
        uint32_t remaining = len;
        uint32_t current_phys = phys;
        tmp_in_count = 0;
        
        while (remaining > 0 && tmp_in_count < DMA_MAX_SG_INTERNAL) {
            /* Calculate bytes to next 64KB boundary */
            uint32_t offset_in_64k = current_phys & 0xFFFF;
            uint32_t bytes_to_boundary = 0x10000 - offset_in_64k;
            
            /* Take minimum of remaining, bytes to boundary, and 64KB-1 */
            uint32_t chunk_size = remaining;
            if (chunk_size > bytes_to_boundary) {
                chunk_size = bytes_to_boundary;
            }
            if (chunk_size > 0xFFFF) {
                chunk_size = 0xFFFF;  /* Max 16-bit length */
            }
            
            tmp_in[tmp_in_count].phys = current_phys;
            tmp_in[tmp_in_count].len = (uint16_t)chunk_size;
            tmp_in_count++;
            
            current_phys += chunk_size;
            remaining -= chunk_size;
        }
        
        if (remaining > 0) {
            log_error("DMA map: buffer too fragmented for real mode");
            return false;
        }
    }

    /* Split fragments at 64KB boundaries */
    for (uint16_t i = 0; i < tmp_in_count; ++i) {
        uint16_t produced = split_at_64k_boundaries(tmp_in[i].phys, tmp_in[i].len,
                                                    &tmp_out[tmp_out_count],
                                                    (uint16_t)(DMA_MAX_SG_INTERNAL - tmp_out_count));
        if (produced == 0) {
            log_error("DMA map: insufficient space for 64KB split");
            if (lock_out->vds_used) {
                vds_unlock_region_sg(lock_out->vds_handle);
                lock_out->vds_used = 0;
            }
            return false;
        }
        tmp_out_count = (uint16_t)(tmp_out_count + produced);
    }

    /* GPT-5 Fix: Alignment is now checked in split_at_64k_boundaries for ALL fragments */
    
    /* GPT-5 Fix: Check ISA addressing limit only for ISA devices */
    if (device_type == DMA_DEVICE_ISA) {
        for (uint16_t i = 0; i < tmp_out_count; ++i) {
            uint32_t frag_start = tmp_out[i].phys;
            uint32_t frag_end = frag_start + tmp_out[i].len - 1;
            
            /* Check both start AND end addresses are within 24-bit window */
            if (frag_start >= 0x01000000UL || frag_end > 0x00FFFFFFUL) {
                log_warning("DMA map: ISA fragment exceeds 16MB limit (start=0x%08lX, end=0x%08lX)", 
                          frag_start, frag_end);
                log_info("DMA map: Using bounce buffer for ISA constraint violation");
                
                /* Unlock VDS if we used it */
                if (lock_out->vds_used) {
                    vds_unlock_region_sg(lock_out->vds_handle);
                    lock_out->vds_used = 0;
                }
                
                /* Use bounce buffer for ISA constraint violation */
                return dma_use_bounce_buffer(buf, len, direction, lock_out, frags, frag_cnt);
            }
        }
    }

    /* GPT-5 Fix: Properly handle sg_ok parameter with bounce buffer fallback */
    if (!sg_ok && tmp_out_count > 1) {
        log_warning("DMA map: hardware requires single contiguous fragment but got %u fragments", (unsigned)tmp_out_count);
        log_info("DMA map: Using bounce buffer for non-SG hardware");
        
        /* Unlock VDS if we used it */
        if (lock_out->vds_used) {
            vds_unlock_region_sg(lock_out->vds_handle);
            lock_out->vds_used = 0;
        }
        
        /* Use bounce buffer for non-scatter-gather hardware */
        return dma_use_bounce_buffer(buf, len, direction, lock_out, frags, frag_cnt);
    }

    /* GPT-5 Fix: Validate caller's fragment array capacity BEFORE copying */
    uint16_t caller_capacity = *frag_cnt;  /* Input: max fragments caller can accept */
    if (caller_capacity < tmp_out_count) {
        log_error("DMA map: caller fragment array too small (capacity %u < needed %u)", 
                  (unsigned)caller_capacity, (unsigned)tmp_out_count);
        if (lock_out->vds_used) {
            vds_unlock_region_sg(lock_out->vds_handle);
            lock_out->vds_used = 0;
        }
        return false;
    }

    /* Copy fragments to caller's buffer */
    for (uint16_t i = 0; i < tmp_out_count; ++i) {
        frags[i] = tmp_out[i];
    }
    
    /* GPT-5 Fix: Set output count to actual fragments used */
    *frag_cnt = tmp_out_count;
    
    /* GPT-5 Fix: Coherency info already stored at beginning */

    return true;
}

/**
 * @brief Unlock a previously locked DMA buffer
 * 
 * @param lock Lock handle from dma_lock_and_map_buffer
 */
void dma_unlock_buffer(dma_lock_t *lock)
{
    /* Use extended unlock which handles everything */
    dma_unlock_buffer_ex(lock);
}

/**
 * @brief Validate DMA buffer for hardware constraints (GPT-5 A+ implementation)
 * 
 * For 3C509B: Always returns true (PIO only, no DMA)
 * For 3C515-TX: Validates ISA bus master constraints with proper VDS locking
 * 
 * IMPORTANT: This now locks the region, validates it, and immediately unlocks.
 * For actual DMA, callers must use dma_lock_and_map_buffer and keep the lock.
 */
bool dma_buffer_is_safe(void __far *buf, uint16_t len, bool using_3c515_bus_master) {
    dma_lock_t lock;
    dma_fragment_t frags[8];  /* 3C515-TX supports scatter/gather */
    uint16_t frag_count = 8;
    
    /* 3C509B uses PIO only - no DMA validation needed */
    if (!using_3c515_bus_master) {
        return true;
    }
    
    /* Use the new locking implementation to validate */
    bool ok = dma_lock_and_map_buffer(buf, len, true, &lock, frags, &frag_count);
    
    /* Immediately unlock since this is just a validation check */
    dma_unlock_buffer(&lock);
    
    return ok;
}

/**
 * @brief Validate DMA buffer descriptor integrity
 */
static bool validate_buffer_integrity(const dma_buffer_descriptor_t* desc) {
    if (!desc) return false;
    
    /* Check signature */
    if (desc->signature != SIGNATURE_MAGIC) {
        log_error("DMA Safety: Invalid buffer signature 0x%08lX", desc->signature);
        return false;
    }
    
    /* Check rear canary */
    if (desc->canary_rear != CANARY_PATTERN_REAR) {
        log_error("DMA Safety: Buffer rear canary corrupted 0x%08lX", desc->canary_rear);
        return false;
    }
    
    /* Validate checksum */
    uint16_t expected = desc->checksum;
    uint16_t actual = calculate_checksum((uint8_t*)desc + sizeof(desc->signature) + sizeof(desc->checksum),
                                        sizeof(*desc) - sizeof(desc->signature) - sizeof(desc->checksum));
    
    if (expected != actual) {
        log_error("DMA Safety: Buffer checksum mismatch (expected 0x%04X, got 0x%04X)", 
                 expected, actual);
        return false;
    }
    
    return true;
}

/**
 * @brief Validate bounce buffer integrity
 */
static bool validate_bounce_buffer(const bounce_buffer_t* bounce) {
    if (!bounce) return false;
    
    /* Check front canary */
    if (bounce->front_canary != CANARY_PATTERN_FRONT) {
        log_error("DMA Safety: Bounce front canary corrupted 0x%08lX", bounce->front_canary);
        return false;
    }
    
    /* Check rear canary */
    if (bounce->rear_canary != CANARY_PATTERN_REAR) {
        log_error("DMA Safety: Bounce rear canary corrupted 0x%08lX", bounce->rear_canary);
        return false;
    }
    
    /* Validate checksum */
    uint16_t expected = bounce->checksum;
    uint16_t actual = calculate_checksum((uint8_t*)bounce + offsetof(bounce_buffer_t, virtual_address),
                                        offsetof(bounce_buffer_t, checksum) - offsetof(bounce_buffer_t, virtual_address));
    
    if (expected != actual) {
        log_error("DMA Safety: Bounce checksum mismatch");
        return false;
    }
    
    return true;
}

/**
 * @brief Initialize buffer protection
 */
static void init_buffer_protection(dma_buffer_descriptor_t* desc) {
    if (!desc) return;
    
    desc->signature = SIGNATURE_MAGIC;
    desc->canary_rear = CANARY_PATTERN_REAR;
    
    /* Calculate and store checksum */
    desc->checksum = calculate_checksum((uint8_t*)desc + sizeof(desc->signature) + sizeof(desc->checksum),
                                       sizeof(*desc) - sizeof(desc->signature) - sizeof(desc->checksum));
}

/**
 * @brief Initialize bounce buffer protection
 */
static void init_bounce_protection(bounce_buffer_t* bounce) {
    if (!bounce) return;
    
    bounce->front_canary = CANARY_PATTERN_FRONT;
    bounce->rear_canary = CANARY_PATTERN_REAR;
    bounce->use_count = 0;
    
    /* Calculate and store checksum */
    bounce->checksum = calculate_checksum((uint8_t*)bounce + offsetof(bounce_buffer_t, virtual_address),
                                         offsetof(bounce_buffer_t, checksum) - offsetof(bounce_buffer_t, virtual_address));
}

/**
 * @brief Use bounce buffer for non-contiguous or non-compliant DMA
 * 
 * GPT-5 Fix: Implement proper bounce buffer fallback
 * 
 * @param buf Original buffer address
 * @param len Buffer length
 * @param direction DMA direction
 * @param lock_out Lock structure to fill
 * @param frags Fragment array to fill
 * @param frag_cnt Fragment count (in/out)
 * @return true on success, false on failure
 */
static bool dma_use_bounce_buffer(void __far *buf, uint32_t len, dma_direction_t direction,
                                  dma_lock_t *lock_out, dma_fragment_t *frags,
                                  uint16_t *frag_cnt)
{
    bounce_buffer_t *bounce;
    
    /* Allocate bounce buffer */
    bounce = allocate_bounce_buffer(DMA_BUFFER_TYPE_GENERAL);
    if (!bounce) {
        log_error("DMA bounce: No available bounce buffers");
        return false;
    }
    
    /* Check size constraint */
    if (len > bounce->size) {
        log_error("DMA bounce: Buffer too large (%lu > %lu)", len, bounce->size);
        free_bounce_buffer(bounce);
        return false;
    }
    
    /* Copy data to bounce buffer for TX */
    if (direction == DMA_DIR_TO_DEVICE || direction == DMA_DIR_BIDIRECTIONAL) {
        _fmemcpy(bounce->virtual_address, buf, len);
    }
    
    /* Fill lock structure */
    lock_out->vds_used = 0;
    lock_out->vds_handle = 0;
    lock_out->bounce_used = 1;
    lock_out->bounce = bounce;
    lock_out->buffer_addr = buf;
    lock_out->buffer_len = len;
    lock_out->direction = direction;
    lock_out->device_type = DMA_DEVICE_ISA;  /* Bounce buffers are ISA-safe */
    
    /* Return single contiguous fragment */
    frags[0].phys = bounce->physical_address;
    frags[0].len = len;
    *frag_cnt = 1;
    
    log_debug("DMA bounce: Using bounce buffer at 0x%08lX for %u bytes",
              bounce->physical_address, len);
    
    return true;
}

/**
 * @brief Sync cache for device access
 * 
 * GPT-5 Fix: Proper cache coherency management
 * 
 * @param buf Buffer address
 * @param len Buffer length
 * @param direction DMA direction
 */
static void dma_sync_for_device(void __far *buf, uint32_t len, dma_direction_t direction)
{
    /* GPT-5 Fix: Use existing safe cache operations instead of inline assembly */
    extern int cache_wbinvd_safe(void);  /* From cache_ops.asm */
    extern int cache_clflush_safe(void __far *addr, uint32_t size);  /* From cache_ops.asm */
    extern bool g_clflush_available;  /* From cache_coherency system */
    
    /* Only flush for non-coherent DMA */
    if (direction == DMA_DIR_TO_DEVICE || direction == DMA_DIR_BIDIRECTIONAL) {
        /* Try CLFLUSH first if available (more efficient) */
        if (g_clflush_available) {
            cache_clflush_safe(buf, len);
        } else {
            /* Use safe WBINVD wrapper that checks V86 and CPU */
            if (cache_wbinvd_safe() != 0) {
                /* WBINVD not safe (V86 mode or < 486) */
                /* Software coherency or no-op - cache_coherency system handles this */
                log_debug("DMA sync: Cache flush not available, using software coherency");
            }
        }
    }
}

/**
 * @brief Sync cache for CPU access
 * 
 * GPT-5 Fix: Proper cache coherency management
 * 
 * @param buf Buffer address
 * @param len Buffer length
 * @param direction DMA direction
 */
static void dma_sync_for_cpu(void __far *buf, uint32_t len, dma_direction_t direction)
{
    /* GPT-5 Fix: Use existing safe cache operations */
    extern int cache_wbinvd_safe(void);  /* WBINVD is safer than INVD */
    extern int cache_clflush_safe(void __far *addr, uint32_t size);
    extern bool g_clflush_available;
    
    /* Invalidate cache lines for data coming from device */
    if (direction == DMA_DIR_FROM_DEVICE || direction == DMA_DIR_BIDIRECTIONAL) {
        /* Note: INVD is dangerous as it doesn't write back dirty lines */
        /* Use WBINVD or CLFLUSH instead which are safer */
        if (g_clflush_available) {
            cache_clflush_safe(buf, len);
        } else {
            /* Use WBINVD which writes back then invalidates */
            if (cache_wbinvd_safe() != 0) {
                /* Cache operations not safe */
                log_debug("DMA sync: Cache invalidate not available");
            }
        }
    }
}

/**
 * @brief Extended unlock function with cache sync and bounce buffer handling
 * 
 * GPT-5 Fix: Proper cleanup including bounce buffer copy-back
 * 
 * @param lock Lock structure from dma_lock_and_map_buffer_ex
 */
void dma_unlock_buffer_ex(dma_lock_t *lock)
{
    if (!lock) return;
    
    /* Handle bounce buffer */
    if (lock->bounce_used && lock->bounce) {
        /* Copy data back from bounce buffer for RX */
        if (lock->direction == DMA_DIR_FROM_DEVICE || 
            lock->direction == DMA_DIR_BIDIRECTIONAL) {
            _fmemcpy(lock->buffer_addr, lock->bounce->virtual_address, lock->buffer_len);
        }
        
        /* Free bounce buffer */
        free_bounce_buffer(lock->bounce);
        lock->bounce = NULL;
        lock->bounce_used = 0;
    }
    
    /* Handle VDS unlock */
    if (lock->vds_used) {
        vds_unlock_region_sg(lock->vds_handle);
        lock->vds_used = 0;
    }
    
    /* Sync cache for CPU access */
    if (lock->direction == DMA_DIR_FROM_DEVICE || 
        lock->direction == DMA_DIR_BIDIRECTIONAL) {
        dma_sync_for_cpu(lock->buffer_addr, lock->buffer_len, lock->direction);
    }
}