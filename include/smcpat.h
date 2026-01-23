/**
 * @file smc_patches.h
 * @brief Self-Modifying Code Patch Framework
 *
 * 3Com Packet Driver - Safe Self-Modifying Code Implementation
 * 
 * Provides atomic, interrupt-safe code patching with prefetch flush
 * for CPU-specific optimizations. Integrates with Module ABI v1.0
 * for performance optimization delivery.
 * 
 * Agent 04 - Performance Engineer - Week 1 Day 4-5 Critical Deliverable
 */

#ifndef SMC_PATCHES_H
#define SMC_PATCHES_H

#include <stdint.h>
#include <stdbool.h>
#include "cpudet.h"
#include "../docs/agents/shared/timing-measurement.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum patch size in bytes */
#define MAX_PATCH_SIZE          32      /* Maximum instruction sequence size */
#define MAX_PATCH_SITES         64      /* Maximum patch sites per module */
#define MAX_ROLLBACK_ENTRIES    16      /* Maximum rollback entries */

/* Patch validation constraints */
#define MAX_CLI_DURATION_US     8       /* Maximum CLI duration for atomic patches */
#define MIN_PATCH_ALIGNMENT     1       /* Minimum patch alignment (byte) */
#define MAX_PATCH_ALIGNMENT     16      /* Maximum patch alignment (paragraph) */

/* Patch types */
typedef enum {
    PATCH_TYPE_MEMORY_COPY,             /* Memory copy optimization */
    PATCH_TYPE_MEMORY_SET,              /* Memory set optimization */
    PATCH_TYPE_REGISTER_SAVE,           /* Register save/restore optimization */
    PATCH_TYPE_IO_OPERATION,            /* I/O operation optimization */
    PATCH_TYPE_INTERRUPT_HANDLER,       /* ISR optimization */
    PATCH_TYPE_FUNCTION_CALL,           /* Function call optimization */
    PATCH_TYPE_CUSTOM,                  /* Custom patch sequence */
    PATCH_TYPE_COUNT
} patch_type_t;

/* Patch application method */
typedef enum {
    PATCH_METHOD_DIRECT,                /* Direct instruction replacement */
    PATCH_METHOD_JUMP_TABLE,            /* Jump table redirection */
    PATCH_METHOD_FUNCTION_POINTER,      /* Function pointer replacement */
    PATCH_METHOD_COUNT
} patch_method_t;

/* Patch status */
typedef enum {
    PATCH_STATUS_PENDING,               /* Patch prepared but not applied */
    PATCH_STATUS_APPLIED,               /* Patch successfully applied */
    PATCH_STATUS_FAILED,                /* Patch application failed */
    PATCH_STATUS_ROLLED_BACK,           /* Patch was rolled back */
    PATCH_STATUS_COUNT
} patch_status_t;

/* CPU requirements for patch */
typedef struct {
    cpu_type_t min_cpu_type;            /* Minimum required CPU type */
    uint32_t required_features;         /* Required CPU feature flags */
    bool requires_32bit;                /* Requires 32-bit operations */
    bool requires_alignment;            /* Requires specific alignment */
    uint8_t alignment_bytes;            /* Required alignment in bytes */
} patch_cpu_requirements_t;

/* Patch site information */
typedef struct {
    void* target_address;               /* Address to patch */
    uint8_t original_code[MAX_PATCH_SIZE]; /* Original instruction bytes */
    uint8_t patch_code[MAX_PATCH_SIZE]; /* Replacement instruction bytes */
    uint8_t original_size;              /* Size of original code */
    uint8_t patch_size;                 /* Size of patch code */
    patch_type_t type;                  /* Type of patch */
    patch_method_t method;              /* Application method */
    patch_cpu_requirements_t requirements; /* CPU requirements */
    bool is_active;                     /* Whether patch is currently active */
    bool validated;                     /* Whether patch has been validated */
    uint32_t patch_id;                  /* Unique patch identifier */
} patch_site_t;

/* Patch application result */
typedef struct {
    patch_status_t status;              /* Application status */
    uint32_t patches_applied;           /* Number of patches applied */
    uint32_t patches_failed;            /* Number of patches that failed */
    uint32_t patches_skipped;           /* Number of patches skipped */
    pit_timing_t cli_duration;          /* Time spent with interrupts disabled */
    bool cli_duration_valid;            /* Whether CLI timing is valid */
    char error_message[128];            /* Error description if failed */
} patch_application_result_t;

/* Patch rollback entry */
typedef struct {
    void* address;                      /* Address that was patched */
    uint8_t original_code[MAX_PATCH_SIZE]; /* Original code to restore */
    uint8_t size;                       /* Size of code to restore */
    uint32_t patch_id;                  /* Associated patch ID */
    bool is_valid;                      /* Whether entry is valid */
} patch_rollback_entry_t;

/* Patch manager state */
typedef struct {
    patch_site_t sites[MAX_PATCH_SITES]; /* Registered patch sites */
    uint32_t site_count;                /* Number of registered sites */
    patch_rollback_entry_t rollback[MAX_ROLLBACK_ENTRIES]; /* Rollback entries */
    uint32_t rollback_count;            /* Number of rollback entries */
    uint32_t next_patch_id;             /* Next patch ID to assign */
    bool interrupts_were_enabled;       /* State before CLI */
    cpu_type_t target_cpu;              /* Target CPU type */
    uint32_t available_features;        /* Available CPU features */
    bool framework_initialized;         /* Framework initialization state */
} patch_manager_t;

/* Global patch manager instance */
extern patch_manager_t g_patch_manager;

/* Core patch management functions */
int smc_patches_init(void);
int smc_patches_shutdown(void);
bool smc_patches_enabled(void);

/* Patch site registration */
uint32_t register_patch_site(void* target_address, patch_type_t type,
                            const patch_cpu_requirements_t* requirements);
int unregister_patch_site(uint32_t patch_id);
int validate_patch_site(uint32_t patch_id);

/* Patch code preparation */
int prepare_memory_copy_patch(uint32_t patch_id, size_t copy_size, bool use_32bit);
int prepare_register_save_patch(uint32_t patch_id, bool use_pusha);
int prepare_custom_patch(uint32_t patch_id, const uint8_t* patch_code, uint8_t size);

/* Atomic patch application */
patch_application_result_t apply_patches_atomic(void);
patch_application_result_t apply_single_patch_atomic(uint32_t patch_id);
int rollback_patches(void);
int rollback_single_patch(uint32_t patch_id);

/* Patch validation and safety */
bool validate_patch_safety(const patch_site_t* site);
bool check_cpu_requirements(const patch_cpu_requirements_t* requirements);
bool verify_patch_integrity(uint32_t patch_id);
int test_patch_functionality(uint32_t patch_id);

/* Prefetch management */
void flush_instruction_prefetch(void);
void flush_prefetch_at_address(void* address);

/* Utility functions */
const char* get_patch_type_name(patch_type_t type);
const char* get_patch_method_name(patch_method_t method);
const char* get_patch_status_name(patch_status_t status);
void print_patch_manager_status(void);
void print_patch_site_info(uint32_t patch_id);

/* Assembly helpers */
extern void asm_atomic_patch_bytes(void* target, const void* patch, uint8_t size);
extern void asm_flush_prefetch_near_jump(void);
extern void asm_save_interrupt_state(void);
extern void asm_restore_interrupt_state(void);

/* Predefined optimization patches */

/* Memory copy optimizations */
int create_rep_movsw_patch(void* target_address, size_t copy_size);
int create_rep_movsd_patch(void* target_address, size_t copy_size);
int create_unrolled_copy_patch(void* target_address, size_t copy_size);

/* Register save optimizations */
int create_pusha_popa_patch(void* target_address);
int create_optimized_save_patch(void* target_address, uint16_t register_mask);

/* I/O operation optimizations */
int create_string_io_patch(void* target_address, bool input, bool word_size);
int create_burst_io_patch(void* target_address, uint16_t port, uint8_t count);

/* Function call optimizations */
int create_near_call_patch(void* target_address, void* function_address);
int create_inline_patch(void* target_address, const void* inline_code, uint8_t size);

/* Patch templates for common optimizations */
extern const uint8_t patch_template_rep_movsw[];
extern const uint8_t patch_template_rep_movsd[];
extern const uint8_t patch_template_pusha_popa[];
extern const uint8_t patch_template_string_io[];

extern const uint8_t patch_template_sizes[];

/* Safety constraints and validation */
#define PATCH_SAFETY_CHECK_ALIGNMENT   0x01    /* Check instruction alignment */
#define PATCH_SAFETY_CHECK_SIZE        0x02    /* Check patch size validity */
#define PATCH_SAFETY_CHECK_CPU         0x04    /* Check CPU requirements */
#define PATCH_SAFETY_CHECK_MEMORY      0x08    /* Check memory accessibility */
#define PATCH_SAFETY_CHECK_ALL         0x0F    /* All safety checks */

/* Performance monitoring integration */
typedef struct {
    uint32_t patches_applied_total;     /* Total patches applied */
    uint32_t patches_failed_total;      /* Total patches failed */
    uint32_t rollbacks_performed;       /* Total rollbacks performed */
    uint32_t cli_violations;            /* Number of CLI duration violations */
    uint32_t max_cli_duration_us;       /* Maximum CLI duration observed */
    uint32_t avg_cli_duration_us;       /* Average CLI duration */
    uint32_t performance_gain_percent;  /* Overall performance gain */
} patch_performance_stats_t;

extern patch_performance_stats_t g_patch_stats;

/* Get performance statistics */
const patch_performance_stats_t* get_patch_performance_stats(void);
void reset_patch_performance_stats(void);
void update_patch_performance_stats(const patch_application_result_t* result);

/* Inline helpers for common operations */

/**
 * Begin atomic patch section - disables interrupts and measures time
 */
static inline void begin_atomic_patch_section(pit_timing_t* timing) {
    PIT_START_TIMING(timing);
    __asm__ __volatile__("cli" : : : "memory");
}

/**
 * End atomic patch section - restores interrupts and measures time
 */
static inline void end_atomic_patch_section(pit_timing_t* timing) {
    __asm__ __volatile__("sti" : : : "memory");
    PIT_END_TIMING(timing);
}

/**
 * Check if interrupts can be safely disabled for patch duration
 */
static inline bool can_disable_interrupts_safely(uint32_t estimated_duration_us) {
    return (estimated_duration_us <= MAX_CLI_DURATION_US);
}

/**
 * Validate patch timing constraints
 */
static inline bool validate_patch_timing(const pit_timing_t* timing) {
    return VALIDATE_CLI_TIMING(timing);
}

/* Advanced patch features */

/* Conditional patching based on runtime detection */
typedef struct {
    bool (*condition_check)(void);      /* Function to check condition */
    uint32_t patch_id_true;             /* Patch to apply if condition is true */
    uint32_t patch_id_false;            /* Patch to apply if condition is false */
    bool evaluated;                     /* Whether condition has been evaluated */
    bool last_result;                   /* Last evaluation result */
} conditional_patch_t;

int register_conditional_patch(bool (*condition_check)(void),
                              uint32_t patch_id_true, uint32_t patch_id_false);
int evaluate_conditional_patches(void);

/* Patch chaining for complex optimizations */
typedef struct {
    uint32_t patch_ids[8];              /* Chain of patch IDs */
    uint8_t count;                      /* Number of patches in chain */
    bool all_or_nothing;                /* Whether all patches must succeed */
} patch_chain_t;

int create_patch_chain(const uint32_t* patch_ids, uint8_t count, bool all_or_nothing);
patch_application_result_t apply_patch_chain(const patch_chain_t* chain);

#ifdef __cplusplus
}
#endif

#endif /* SMC_PATCHES_H */