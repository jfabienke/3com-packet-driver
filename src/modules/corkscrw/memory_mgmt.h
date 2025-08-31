/**
 * @file memory_mgmt.h
 * @brief Memory Management Integration for CORKSCRW.MOD
 * 
 * Agent Team B (07-08): Week 1 Implementation
 * 
 * This header defines the integration interface with Agent 11's memory
 * management system for DMA-safe buffer allocation, bounce buffer management,
 * and hot/cold section optimization.
 * 
 * Key Integration Points:
 * - DMA-safe buffer allocation with 64KB boundary checking
 * - Coherent memory management for bus-master operations
 * - XMS/conventional memory pool coordination
 * - Physical address translation services
 * - Cache coherency management
 * 
 * Memory Constraints:
 * - ISA DMA buffers must be below 16MB physical
 * - No buffer may cross 64KB boundaries
 * - Descriptors must be physically contiguous
 * - Hot section ≤6KB after cold section discard
 * 
 * This file is part of the CORKSCRW.MOD module.
 * Copyright (c) 2025 3Com/Phase3A Team B
 */

#ifndef MEMORY_MGMT_H
#define MEMORY_MGMT_H

#include <stdint.h>
#include <stdbool.h>

/* Memory Management Constants */
#define DMA_ISA_LIMIT           0x1000000  /* 16MB ISA DMA limit */
#define DMA_BOUNDARY_64KB       0x10000    /* 64KB boundary */
#define CACHE_LINE_SIZE         32         /* Typical cache line size */
#define PAGE_SIZE               4096       /* 4KB page size */

/* DMA Device Types */
#define DMA_DEVICE_NETWORK      0x01       /* Network controller */
#define DMA_DEVICE_STORAGE      0x02       /* Storage controller */
#define DMA_DEVICE_AUDIO        0x03       /* Audio controller */

/* Memory Allocation Flags */
#define MEM_FLAG_DMA_SAFE       0x0001     /* DMA-safe allocation */
#define MEM_FLAG_COHERENT       0x0002     /* Cache-coherent memory */
#define MEM_FLAG_BELOW_16MB     0x0004     /* Below 16MB physical */
#define MEM_FLAG_BOUNDARY_SAFE  0x0008     /* 64KB boundary safe */
#define MEM_FLAG_ZERO_INIT      0x0010     /* Zero-initialize memory */

/* Memory Types */
typedef enum {
    MEM_TYPE_CONVENTIONAL,      /* DOS conventional memory */
    MEM_TYPE_XMS,              /* Extended memory (XMS) */
    MEM_TYPE_UMB,              /* Upper memory blocks */
    MEM_TYPE_COHERENT          /* DMA-coherent memory */
} mem_type_t;

/* DMA Buffer Descriptor */
typedef struct {
    void *virt_addr;           /* Virtual address */
    uint32_t phys_addr;        /* Physical address */
    uint16_t size;             /* Buffer size in bytes */
    uint8_t device_type;       /* DMA device type */
    uint8_t flags;             /* Allocation flags */
    bool boundary_safe;        /* 64KB boundary safe */
    bool coherent;             /* Cache coherent */
} dma_buffer_desc_t;

/* Memory Statistics */
typedef struct {
    uint32_t total_conventional;   /* Total conventional memory */
    uint32_t free_conventional;    /* Free conventional memory */
    uint32_t total_xms;           /* Total XMS memory */
    uint32_t free_xms;            /* Free XMS memory */
    uint32_t dma_buffers_allocated; /* DMA buffers allocated */
    uint32_t boundary_violations;   /* 64KB boundary violations */
    uint32_t coherent_allocations;  /* Coherent allocations */
} mem_stats_t;

/**
 * ============================================================================
 * MEMORY MANAGEMENT API (Agent 11 Integration)
 * ============================================================================
 */

/**
 * @brief Allocate DMA-safe buffer
 * 
 * @param size Size in bytes
 * @param device_type DMA device type
 * @param alignment Required alignment (power of 2)
 * @param flags Allocation flags
 * @return Pointer to buffer descriptor, NULL on failure
 */
dma_buffer_desc_t* mem_alloc_dma_buffer(uint16_t size, uint8_t device_type, 
                                        uint8_t alignment, uint16_t flags);

/**
 * @brief Free DMA buffer
 * 
 * @param buffer Buffer descriptor to free
 * @return 0 on success, negative on error
 */
int mem_free_dma_buffer(dma_buffer_desc_t *buffer);

/**
 * @brief Allocate coherent memory for bus-master DMA
 * 
 * @param size Size in bytes
 * @param phys_addr Pointer to store physical address
 * @param alignment Required alignment
 * @return Virtual address, NULL on failure
 */
void* mem_alloc_coherent(uint16_t size, uint32_t *phys_addr, uint8_t alignment);

/**
 * @brief Free coherent memory
 * 
 * @param virt_addr Virtual address
 * @param size Size in bytes
 */
void mem_free_coherent(void *virt_addr, uint16_t size);

/**
 * @brief Check if buffer crosses 64KB boundary
 * 
 * @param phys_addr Physical address
 * @param size Buffer size
 * @return true if crosses boundary, false otherwise
 */
bool mem_check_64kb_boundary(uint32_t phys_addr, uint16_t size);

/**
 * @brief Convert virtual address to physical address
 * 
 * @param virt_addr Virtual address
 * @return Physical address, 0 on error
 */
uint32_t mem_virt_to_phys(void *virt_addr);

/**
 * @brief Convert physical address to virtual address
 * 
 * @param phys_addr Physical address
 * @return Virtual address, NULL on error
 */
void* mem_phys_to_virt(uint32_t phys_addr);

/**
 * @brief Flush cache range for DMA coherency
 * 
 * @param addr Address to flush
 * @param size Size to flush
 * @return 0 on success, negative on error
 */
int mem_cache_flush(void *addr, uint16_t size);

/**
 * @brief Invalidate cache range for DMA coherency
 * 
 * @param addr Address to invalidate
 * @param size Size to invalidate
 * @return 0 on success, negative on error
 */
int mem_cache_invalidate(void *addr, uint16_t size);

/**
 * @brief Get memory statistics
 * 
 * @param stats Pointer to statistics structure
 * @return 0 on success, negative on error
 */
int mem_get_statistics(mem_stats_t *stats);

/**
 * ============================================================================
 * HOT/COLD SECTION OPTIMIZATION (Week 1 Stubs)
 * ============================================================================
 */

/**
 * @brief Mark memory section as hot (performance critical)
 * 
 * @param addr Start address of section
 * @param size Size of section
 * @return 0 on success, negative on error
 */
int mem_mark_hot_section(void *addr, uint16_t size);

/**
 * @brief Mark memory section as cold (initialization only)
 * 
 * @param addr Start address of section
 * @param size Size of section
 * @return 0 on success, negative on error
 */
int mem_mark_cold_section(void *addr, uint16_t size);

/**
 * @brief Discard cold sections after initialization
 * 
 * @return Bytes freed, negative on error
 */
int mem_discard_cold_sections(void);

/**
 * ============================================================================
 * WEEK 1 STUB IMPLEMENTATIONS
 * ============================================================================
 */

#ifdef WEEK_1_STUBS

/* Stub implementations for Week 1 development */
static inline dma_buffer_desc_t* mem_alloc_dma_buffer(uint16_t size, uint8_t device_type, 
                                                      uint8_t alignment, uint16_t flags) {
    static dma_buffer_desc_t stub_buffer = {
        .virt_addr = (void*)0x100000,
        .phys_addr = 0x100000,
        .size = 1536,
        .device_type = DMA_DEVICE_NETWORK,
        .flags = MEM_FLAG_DMA_SAFE | MEM_FLAG_BOUNDARY_SAFE,
        .boundary_safe = true,
        .coherent = true
    };
    return &stub_buffer;
}

static inline int mem_free_dma_buffer(dma_buffer_desc_t *buffer) {
    return 0;  /* Success */
}

static inline void* mem_alloc_coherent(uint16_t size, uint32_t *phys_addr, uint8_t alignment) {
    if (phys_addr) *phys_addr = 0x100000;  /* Fake physical address */
    return (void*)0x100000;  /* Fake virtual address */
}

static inline void mem_free_coherent(void *virt_addr, uint16_t size) {
    /* Stub - no operation */
}

static inline bool mem_check_64kb_boundary(uint32_t phys_addr, uint16_t size) {
    uint32_t start_page = phys_addr >> 16;
    uint32_t end_page = (phys_addr + size - 1) >> 16;
    return (start_page != end_page);  /* true if crosses boundary */
}

static inline uint32_t mem_virt_to_phys(void *virt_addr) {
    return (uint32_t)virt_addr;  /* DOS: virtual == physical */
}

static inline void* mem_phys_to_virt(uint32_t phys_addr) {
    return (void*)phys_addr;  /* DOS: virtual == physical */
}

static inline int mem_cache_flush(void *addr, uint16_t size) {
    return 0;  /* Success - no operation needed in DOS */
}

static inline int mem_cache_invalidate(void *addr, uint16_t size) {
    return 0;  /* Success - no operation needed in DOS */
}

static inline int mem_get_statistics(mem_stats_t *stats) {
    if (!stats) return -1;
    
    /* Fill with stub values */
    stats->total_conventional = 640 * 1024;  /* 640KB */
    stats->free_conventional = 400 * 1024;   /* 400KB free */
    stats->total_xms = 16 * 1024 * 1024;     /* 16MB XMS */
    stats->free_xms = 15 * 1024 * 1024;      /* 15MB free */
    stats->dma_buffers_allocated = 10;
    stats->boundary_violations = 0;
    stats->coherent_allocations = 5;
    
    return 0;
}

static inline int mem_mark_hot_section(void *addr, uint16_t size) {
    return 0;  /* Success - stub */
}

static inline int mem_mark_cold_section(void *addr, uint16_t size) {
    return 0;  /* Success - stub */
}

static inline int mem_discard_cold_sections(void) {
    return 3072;  /* Fake: 3KB freed */
}

#endif /* WEEK_1_STUBS */

#endif /* MEMORY_MGMT_H */