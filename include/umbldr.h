/**
 * @file umb_loader.h
 * @brief UMB detection and loading interface
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _UMB_LOADER_H_
#define _UMB_LOADER_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include <stdint.h>
#include <stdbool.h>

/* Memory manager types */
typedef enum {
    MEMORY_MANAGER_NONE,            /* 0: No memory manager */
    MEMORY_MANAGER_HIMEM,           /* 1: HIMEM.SYS */
    MEMORY_MANAGER_EMM386,          /* 2: EMM386 */
    MEMORY_MANAGER_QEMM,            /* 3: QEMM */
    MEMORY_MANAGER_UNKNOWN          /* 4: Unknown memory manager */
} memory_manager_type_t;

/* UMB allocation information */
typedef struct {
    bool umb_available;                     /* UMB support detected */
    uint8_t memory_manager_type;            /* Type of memory manager */
    char memory_manager_name[16];           /* Memory manager name */
    uint16_t allocated_segment;             /* Allocated segment address */
    uint16_t allocated_size;                /* Size in paragraphs */
    bool using_conventional_fallback;       /* Using conventional memory */
    uint32_t conventional_memory_saved;     /* Bytes saved in conventional memory */
} umb_allocation_info_t;

/* TSR memory layout information */
typedef struct {
    /* Resident components (stay in memory) */
    uint16_t resident_code_size;            /* Resident code size in bytes */
    uint16_t resident_data_size;            /* Resident data size in bytes */
    uint16_t resident_stack_size;           /* Resident stack size in bytes */
    uint16_t psp_size;                      /* PSP size in bytes */
    uint16_t total_resident_bytes;          /* Total resident size in bytes */
    uint16_t resident_paragraphs;           /* Resident size in paragraphs */
    
    /* Initialization components (discarded after init) */
    uint16_t init_code_size;                /* Init code size in bytes */
    uint16_t init_data_size;                /* Init data size in bytes */
    uint16_t total_init_bytes;              /* Total init size in bytes */
    uint16_t init_paragraphs;               /* Init size in paragraphs */
    uint16_t discarded_init_bytes;          /* Bytes discarded after init */
    
    /* Memory optimization results */
    uint16_t conventional_memory_used;      /* Conventional memory paragraphs used */
    uint16_t umb_memory_used;               /* UMB memory paragraphs used */
    bool memory_optimization_achieved;      /* True if optimization successful */
} tsr_memory_layout_t;

/* Memory usage report */
typedef struct {
    bool umb_support_available;             /* UMB support detected */
    bool memory_manager_detected;           /* Memory manager found */
    bool allocation_attempted;              /* Allocation was attempted */
    bool allocation_successful;             /* Allocation succeeded */
    bool using_umb;                         /* Currently using UMB */
    
    char memory_manager_name[16];           /* Memory manager name */
    uint16_t allocated_segment;             /* Allocated segment */
    uint16_t allocated_paragraphs;          /* Allocated size */
    uint32_t conventional_memory_saved;     /* Conventional memory saved */
} umb_memory_report_t;

/* Error codes */
#define UMB_SUCCESS                     0
#define UMB_ERROR_NOT_INITIALIZED      -1
#define UMB_ERROR_INVALID_PARAMETER    -2
#define UMB_ERROR_ALLOCATION_FAILED    -3
#define UMB_ERROR_DOS_TOO_OLD          -4
#define UMB_ERROR_NO_MEMORY_MANAGER    -5
#define UMB_ERROR_UMB_NOT_SUPPORTED    -6

/* Function prototypes */

/**
 * @brief Initialize UMB loader subsystem
 * @return 0 on success, negative on error
 */
int umb_loader_init(void);

/**
 * @brief Attempt to allocate UMB for TSR
 * @param required_paragraphs Size needed in paragraphs
 * @return 0 on success, negative on error
 */
int umb_allocate_tsr_memory(uint16_t required_paragraphs);

/**
 * @brief Get UMB allocation information
 * @param info Pointer to structure to fill with UMB info
 * @return 0 on success, negative on error
 */
int umb_get_allocation_info(umb_allocation_info_t *info);

/**
 * @brief Calculate optimal TSR memory layout
 * @param layout Pointer to structure to fill with layout info
 * @return 0 on success, negative on error
 */
int umb_calculate_tsr_layout(tsr_memory_layout_t *layout);

/**
 * @brief Get memory usage report
 * @param report Pointer to structure to fill with usage report
 * @return 0 on success, negative on error
 */
int umb_get_memory_usage_report(umb_memory_report_t *report);

/**
 * @brief Clean up UMB resources
 * @return 0 on success, negative on error
 */
int umb_loader_cleanup(void);

/**
 * @brief Check if UMB allocation was successful
 * @return true if UMB allocated, false otherwise
 */
bool umb_is_allocated(void);

/**
 * @brief Get allocated segment address
 * @return Segment address, or 0 if not allocated
 */
uint16_t umb_get_allocated_segment(void);

/**
 * @brief Get conventional memory savings in bytes
 * @return Bytes saved in conventional memory
 */
uint32_t umb_get_conventional_memory_saved(void);

/* Utility functions for memory optimization */

/**
 * @brief Print UMB status information
 */
void umb_print_status(void);

/**
 * @brief Print memory layout information
 * @param layout Pointer to layout structure
 */
void umb_print_layout(const tsr_memory_layout_t *layout);

/**
 * @brief Print memory usage report
 * @param report Pointer to report structure
 */
void umb_print_report(const umb_memory_report_t *report);

/* Advanced UMB management functions */

/**
 * @brief Test UMB allocation without actually allocating
 * @param test_paragraphs Size to test
 * @return true if allocation would succeed, false otherwise
 */
bool umb_test_allocation(uint16_t test_paragraphs);

/**
 * @brief Get largest available UMB block
 * @return Size of largest UMB block in paragraphs, 0 if none
 */
uint16_t umb_get_largest_available_block(void);

/**
 * @brief Check memory manager compatibility
 * @param manager_type Memory manager to check
 * @return Compatibility score (0-100), higher is better
 */
int umb_check_manager_compatibility(memory_manager_type_t manager_type);

/* Memory optimization strategies */

/**
 * @brief Optimize TSR for minimal conventional memory usage
 * @param current_size Current TSR size in paragraphs
 * @return Optimized size in paragraphs
 */
uint16_t umb_optimize_tsr_size(uint16_t current_size);

/**
 * @brief Calculate memory savings from UMB usage
 * @param tsr_size TSR size in paragraphs
 * @return Potential savings in bytes
 */
uint32_t umb_calculate_potential_savings(uint16_t tsr_size);

/* Diagnostic and debugging functions */

/**
 * @brief Perform comprehensive UMB system test
 * @return 0 if all tests pass, negative on failure
 */
int umb_run_diagnostic_tests(void);

/**
 * @brief Dump UMB memory map
 */
void umb_dump_memory_map(void);

/**
 * @brief Validate UMB allocation integrity
 * @return true if allocation is valid, false if corrupted
 */
bool umb_validate_allocation(void);

/* Configuration and tuning */

/**
 * @brief Set UMB allocation preferences
 * @param prefer_best_fit Use best-fit allocation strategy
 * @param min_block_size Minimum acceptable block size
 * @return 0 on success, negative on error
 */
int umb_set_allocation_preferences(bool prefer_best_fit, uint16_t min_block_size);

/**
 * @brief Get current UMB configuration
 * @param config Pointer to configuration structure
 * @return 0 on success, negative on error
 */
int umb_get_configuration(void *config);

#ifdef __cplusplus
}
#endif

#endif /* _UMB_LOADER_H_ */