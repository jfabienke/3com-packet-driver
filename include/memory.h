/**
 * @file memory.h
 * @brief Enhanced memory management
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _MEMORY_H_
#define _MEMORY_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"

/* Memory allocation types */
typedef enum {
    MEM_TYPE_GENERAL = 0,                   /* General purpose memory */
    MEM_TYPE_PACKET_BUFFER,                 /* Packet buffer memory */
    MEM_TYPE_DMA_BUFFER,                    /* DMA-compatible memory */
    MEM_TYPE_DESCRIPTOR,                    /* Descriptor memory */
    MEM_TYPE_STACK,                         /* Stack memory */
    MEM_TYPE_DRIVER_DATA                    /* Driver-specific data */
} mem_type_t;

/* Memory flags */
#define MEM_FLAG_ZERO           BIT(0)      /* Zero-initialize memory */
#define MEM_FLAG_DMA_CAPABLE    BIT(1)      /* DMA-accessible memory */
#define MEM_FLAG_ALIGNED        BIT(2)      /* Aligned memory */
#define MEM_FLAG_PERSISTENT     BIT(3)      /* Persistent across operations */
#define MEM_FLAG_TEMPORARY      BIT(4)      /* Temporary allocation */
#define MEM_FLAG_LOCKED         BIT(5)      /* Lock in physical memory */

/* Memory block header */
typedef struct mem_block {
    uint32_t size;                          /* Block size */
    uint32_t flags;                         /* Block flags */
    mem_type_t type;                        /* Memory type */
    uint32_t magic;                         /* Magic number for validation */
    struct mem_block *next;                 /* Next block in free list */
    struct mem_block *prev;                 /* Previous block */
} mem_block_t;

/* Memory pool structure */
typedef struct mem_pool {
    void *base;                             /* Pool base address */
    uint32_t size;                          /* Total pool size */
    uint32_t used;                          /* Used memory */
    uint32_t free;                          /* Free memory */
    uint32_t largest_free;                  /* Largest free block */
    mem_block_t *free_list;                 /* Free block list */
    uint32_t block_count;                   /* Number of blocks */
    uint32_t alloc_count;                   /* Allocation count */
    uint32_t free_count;                    /* Free count */
    bool initialized;                       /* Pool initialized */
} mem_pool_t;

/* Memory statistics */
typedef struct mem_stats {
    uint32_t total_memory;                  /* Total memory available */
    uint32_t used_memory;                   /* Memory currently in use */
    uint32_t free_memory;                   /* Free memory available */
    uint32_t peak_usage;                    /* Peak memory usage */
    uint32_t total_allocations;             /* Total allocations made */
    uint32_t total_frees;                   /* Total frees made */
    uint32_t allocation_failures;           /* Failed allocations */
    uint32_t fragmentation_ratio;           /* Fragmentation percentage */
    uint32_t largest_allocation;            /* Largest single allocation */
    uint32_t smallest_allocation;           /* Smallest allocation */
} mem_stats_t;

/* Global memory pools */
extern mem_pool_t g_general_pool;
extern mem_pool_t g_packet_pool;
extern mem_pool_t g_dma_pool;
extern mem_stats_t g_mem_stats;

/* Memory initialization and cleanup */
int memory_init(void);
int memory_init_core(config_t *config);     /* Phase 5: Core memory init */
int memory_init_dma(config_t *config);      /* Phase 9: DMA buffer init */
void memory_cleanup(void);
int memory_pool_init(mem_pool_t *pool, void *base, uint32_t size);
void memory_pool_cleanup(mem_pool_t *pool);

/* Memory allocation and deallocation */
void* memory_alloc(uint32_t size, mem_type_t type, uint32_t flags);
void* memory_calloc(uint32_t count, uint32_t size, mem_type_t type);
void* memory_realloc(void *ptr, uint32_t new_size);
void memory_free(void *ptr);

/* Aligned memory allocation */
void* memory_alloc_aligned(uint32_t size, uint32_t alignment, mem_type_t type);
void* memory_alloc_dma(uint32_t size);
void memory_free_dma(void *ptr);

/* Memory pool operations */
void* memory_pool_alloc(mem_pool_t *pool, uint32_t size, uint32_t flags);
void memory_pool_free(mem_pool_t *pool, void *ptr);
uint32_t memory_pool_get_free_size(const mem_pool_t *pool);
uint32_t memory_pool_get_used_size(const mem_pool_t *pool);
uint32_t memory_pool_get_largest_free(const mem_pool_t *pool);

/* Memory utilities */
void memory_set(void *ptr, uint8_t value, uint32_t size);
void memory_copy(void *dest, const void *src, uint32_t size);
void memory_move(void *dest, const void *src, uint32_t size);
int memory_compare(const void *ptr1, const void *ptr2, uint32_t size);
void memory_zero(void *ptr, uint32_t size);

/* CPU-optimized memory operations */
void memory_copy_optimized(void *dest, const void *src, uint32_t size);
void memory_set_optimized(void *ptr, uint8_t value, uint32_t size);
int memory_init_cpu_optimized(void);

/* ASM-optimized packet copy for ISR paths */
void asm_packet_copy_fast(void *dest, const void *src, uint16_t size);

/* Memory validation and debugging */
bool memory_is_valid_pointer(const void *ptr);
bool memory_is_allocated(const void *ptr);
int memory_validate_heap(void);
void memory_dump_blocks(void);
void memory_dump_pool(const mem_pool_t *pool);

/* Memory statistics */
void memory_stats_init(mem_stats_t *stats);
void memory_stats_update_alloc(mem_stats_t *stats, uint32_t size);
void memory_stats_update_free(mem_stats_t *stats, uint32_t size);
const mem_stats_t* memory_get_stats(void);
void memory_clear_stats(void);
void memory_print_stats(void);

/* Memory defragmentation */
int memory_defragment_pool(mem_pool_t *pool);
uint32_t memory_calculate_fragmentation(const mem_pool_t *pool);
void memory_compact_free_list(mem_pool_t *pool);

/* Low-level memory operations */
void* memory_get_physical_address(void *virtual_addr);
void* memory_map_physical(uint32_t physical_addr, uint32_t size);
void memory_unmap_physical(void *virtual_addr, uint32_t size);

/* Memory protection and locking */
int memory_lock_pages(void *ptr, uint32_t size);
int memory_unlock_pages(void *ptr, uint32_t size);
int memory_set_protection(void *ptr, uint32_t size, uint32_t protection);

/* DOS-specific memory management */
int memory_allocate_dos_memory(uint16_t paragraphs, uint16_t *segment);
int memory_free_dos_memory(uint16_t segment);
int memory_resize_dos_memory(uint16_t segment, uint16_t new_paragraphs);
void* memory_get_conventional_memory(uint32_t size);
void memory_free_conventional_memory(void *ptr);

/* Extended memory (XMS) support */
int memory_init_xms(void);
bool memory_xms_available(void);
uint32_t memory_get_xms_size(void);
void* memory_alloc_xms(uint32_t size);
void memory_free_xms(void *ptr);
int memory_copy_to_xms(void *xms_ptr, const void *conv_ptr, uint32_t size);
int memory_copy_from_xms(void *conv_ptr, const void *xms_ptr, uint32_t size);

/* Expanded memory (EMS) support */
int memory_init_ems(void);
bool memory_ems_available(void);
uint32_t memory_get_ems_size(void);
int memory_alloc_ems_pages(uint16_t pages, uint16_t *handle);
int memory_free_ems_pages(uint16_t handle);
int memory_map_ems_page(uint16_t handle, uint16_t logical_page, uint16_t physical_page);

/* Memory error handling */
typedef enum {
    MEM_ERROR_NONE = 0,
    MEM_ERROR_OUT_OF_MEMORY,
    MEM_ERROR_INVALID_POINTER,
    MEM_ERROR_DOUBLE_FREE,
    MEM_ERROR_CORRUPTION,
    MEM_ERROR_ALIGNMENT,
    MEM_ERROR_POOL_FULL,
    MEM_ERROR_INVALID_SIZE
} mem_error_t;

mem_error_t memory_get_last_error(void);
const char* memory_error_to_string(mem_error_t error);
void memory_set_error_handler(void (*handler)(mem_error_t error, const char* message));

/* Comprehensive stress testing */
int memory_comprehensive_stress_test(void);

/* Memory leak detection */
#ifdef DEBUG
typedef struct mem_alloc_info {
    void *ptr;
    uint32_t size;
    const char *file;
    uint32_t line;
    const char *function;
    struct mem_alloc_info *next;
} mem_alloc_info_t;

void memory_track_allocation(void *ptr, uint32_t size, const char *file,
                            uint32_t line, const char *function);
void memory_track_free(void *ptr);
void memory_dump_leaks(void);
void memory_clear_tracking(void);

#define MEMORY_ALLOC(size, type, flags) \
    memory_track_alloc(memory_alloc(size, type, flags), size, __FILE__, __LINE__, __FUNCTION__)
#define MEMORY_FREE(ptr) \
    do { memory_track_free(ptr); memory_free(ptr); } while(0)
#else
#define MEMORY_ALLOC(size, type, flags) memory_alloc(size, type, flags)
#define MEMORY_FREE(ptr) memory_free(ptr)
#endif

#ifdef __cplusplus
}
#endif

#endif /* _MEMORY_H_ */
