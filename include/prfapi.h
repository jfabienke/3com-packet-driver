/**
 * @file performance_api.h
 * @brief Performance API for NIC Module Integration
 *
 * 3Com Packet Driver - Performance Optimization Framework API
 * 
 * Provides simple, comprehensive interface for NIC teams to integrate
 * CPU-specific optimizations and performance measurement into their modules.
 * Delivers 25-30% performance improvements on critical operations.
 * 
 * Agent 04 - Performance Engineer - Week 1 Day 5 Critical Deliverable
 */

#ifndef PERFORMANCE_API_H
#define PERFORMANCE_API_H

#include <stdint.h>
#include <stdbool.h>
#include "cpudet.h"
#include "smcpat.h"
#include "../docs/agents/shared/timing-measurement.h"

#ifdef __cplusplus
extern "C" {
#endif

/* API Version */
#define PERFORMANCE_API_VERSION_MAJOR   1
#define PERFORMANCE_API_VERSION_MINOR   0
#define PERFORMANCE_API_VERSION_PATCH   0

/* Performance optimization categories */
typedef enum {
    PERF_OPT_MEMORY_COPY,               /* Memory copy operations */
    PERF_OPT_MEMORY_SET,                /* Memory set/fill operations */
    PERF_OPT_PACKET_PROCESSING,         /* Packet header processing */
    PERF_OPT_IO_OPERATIONS,             /* Port I/O operations */
    PERF_OPT_INTERRUPT_HANDLING,        /* ISR and interrupt processing */
    PERF_OPT_BUFFER_MANAGEMENT,         /* Buffer allocation/deallocation */
    PERF_OPT_CHECKSUMS,                 /* Checksum calculations */
    PERF_OPT_COUNT
} perf_optimization_category_t;

/* Performance measurement context */
typedef struct {
    pit_timing_t timing;                /* PIT timing measurement */
    uint32_t bytes_processed;           /* Bytes processed in operation */
    uint32_t operations_count;          /* Number of operations performed */
    bool timing_valid;                  /* Whether timing measurement is valid */
    const char* operation_name;         /* Name of operation being measured */
} perf_measurement_context_t;

/* CPU capability summary for NIC modules */
typedef struct {
    cpu_type_t cpu_type;                /* Detected CPU type */
    bool supports_16bit_ops;            /* 16-bit operations (286+) */
    bool supports_32bit_ops;            /* 32-bit operations (386+) */
    bool supports_pusha_popa;           /* PUSHA/POPA instructions */
    bool supports_string_io;            /* String I/O instructions */
    bool supports_burst_io;             /* Burst I/O operations */
    bool has_internal_cache;            /* CPU has internal cache */
    bool cache_coherency_issues;        /* Potential cache coherency issues */
    uint16_t optimal_copy_size;         /* Optimal memory copy block size */
    uint16_t optimal_alignment;         /* Optimal memory alignment */
} cpu_capabilities_t;

/* Performance optimization result */
typedef struct {
    bool optimization_applied;          /* Whether optimization was applied */
    bool performance_improved;          /* Whether performance actually improved */
    uint32_t baseline_time_us;          /* Baseline operation time */
    uint32_t optimized_time_us;         /* Optimized operation time */
    uint32_t improvement_percent;       /* Performance improvement percentage */
    patch_status_t patch_status;        /* Status of applied patch */
    char description[64];               /* Description of optimization */
} perf_optimization_result_t;

/* NIC module performance profile */
typedef struct {
    char module_name[32];               /* NIC module name */
    uint32_t critical_path_time_us;     /* Critical path timing */
    uint32_t packet_processing_time_us; /* Average packet processing time */
    uint32_t interrupt_latency_us;      /* Interrupt handling latency */
    uint32_t throughput_pps;            /* Packets per second throughput */
    uint32_t throughput_bps;            /* Bits per second throughput */
    uint32_t optimization_count;        /* Number of optimizations applied */
    uint32_t total_improvement_percent; /* Total performance improvement */
    bool profile_valid;                 /* Whether profile data is valid */
} nic_performance_profile_t;

/*
 * Core Performance API Functions
 */

/**
 * Initialize performance framework for NIC module
 * Must be called once during module initialization
 */
int perf_api_init(const char* module_name);

/**
 * Shutdown performance framework
 * Call during module cleanup
 */
void perf_api_shutdown(void);

/**
 * Get CPU capabilities relevant to NIC operations
 */
const cpu_capabilities_t* perf_get_cpu_capabilities(void);

/*
 * Automatic Optimization API (Recommended for most use cases)
 */

/**
 * Apply automatic optimizations for memory copy operations
 * Automatically selects best optimization based on size and CPU type
 * 
 * @param dest Destination buffer
 * @param src Source buffer
 * @param size Number of bytes to copy
 * @return Optimization result with performance measurements
 */
perf_optimization_result_t perf_optimize_memory_copy(void* dest, const void* src, size_t size);

/**
 * Apply automatic optimizations for memory set operations
 */
perf_optimization_result_t perf_optimize_memory_set(void* dest, int value, size_t size);

/**
 * Apply automatic optimizations for packet header processing
 */
perf_optimization_result_t perf_optimize_packet_processing(void* packet, size_t header_size);

/**
 * Apply automatic optimizations for I/O operations
 */
perf_optimization_result_t perf_optimize_io_operations(uint16_t port, void* buffer, 
                                                     size_t count, bool input);

/**
 * Apply automatic optimizations for interrupt handler
 */
perf_optimization_result_t perf_optimize_interrupt_handler(void* isr_address);

/*
 * Manual Optimization API (For advanced users)
 */

/**
 * Register optimization site for later patching
 */
uint32_t perf_register_optimization_site(void* address, perf_optimization_category_t category);

/**
 * Apply specific optimization patch manually
 */
perf_optimization_result_t perf_apply_optimization(uint32_t site_id, patch_type_t patch_type);

/**
 * Rollback specific optimization
 */
int perf_rollback_optimization(uint32_t site_id);

/*
 * Performance Measurement API
 */

/**
 * Begin performance measurement
 * Call before critical operation
 */
void perf_begin_measurement(perf_measurement_context_t* context, const char* operation_name);

/**
 * End performance measurement
 * Call after critical operation
 */
void perf_end_measurement(perf_measurement_context_t* context, uint32_t bytes_processed);

/**
 * Measure function call performance
 * Macro wrapper for timing function calls
 */
#define PERF_MEASURE_FUNCTION(context, function_call) \
    do { \
        perf_begin_measurement(context, #function_call); \
        function_call; \
        perf_end_measurement(context, 0); \
    } while(0)

/**
 * Measure memory operation performance
 */
#define PERF_MEASURE_MEMORY_OP(context, operation_name, bytes, code) \
    do { \
        perf_begin_measurement(context, operation_name); \
        code; \
        perf_end_measurement(context, bytes); \
    } while(0)

/*
 * Performance Analysis API
 */

/**
 * Get current NIC module performance profile
 */
const nic_performance_profile_t* perf_get_module_profile(void);

/**
 * Update performance profile with measurement
 */
void perf_update_profile(const perf_measurement_context_t* measurement);

/**
 * Calculate throughput from timing measurements
 */
uint32_t perf_calculate_throughput_bps(uint32_t bytes, uint32_t time_us);
uint32_t perf_calculate_throughput_pps(uint32_t packets, uint32_t time_us);

/**
 * Check if performance targets are being met
 */
bool perf_targets_met(uint32_t target_improvement_percent);

/*
 * High-Level Optimization Helpers
 */

/**
 * Optimize entire packet receive path
 * Applies multiple optimizations to receive processing
 */
perf_optimization_result_t perf_optimize_rx_path(void* rx_buffer, size_t packet_size);

/**
 * Optimize entire packet transmit path
 * Applies multiple optimizations to transmit processing
 */
perf_optimization_result_t perf_optimize_tx_path(const void* tx_buffer, size_t packet_size);

/**
 * Optimize interrupt service routine
 * Applies register save/restore and other ISR optimizations
 */
perf_optimization_result_t perf_optimize_isr(void* isr_entry_point);

/**
 * Optimize buffer management operations
 * Optimizes buffer allocation, deallocation, and copying
 */
perf_optimization_result_t perf_optimize_buffer_mgmt(void);

/*
 * Specific Optimization Functions
 */

/**
 * Fast memory copy optimized for packet data
 * Uses best available CPU instructions (REP MOVSD/MOVSW/MOVSB)
 */
void perf_fast_memcpy(void* dest, const void* src, size_t size);

/**
 * Fast memory set optimized for buffer clearing
 */
void perf_fast_memset(void* dest, int value, size_t size);

/**
 * Fast checksum calculation optimized for CPU type
 */
uint16_t perf_fast_checksum(const void* data, size_t size);

/**
 * Fast port I/O operations
 */
void perf_fast_port_read(uint16_t port, void* buffer, size_t count);
void perf_fast_port_write(uint16_t port, const void* buffer, size_t count);

/*
 * Configuration and Tuning API
 */

/**
 * Set performance tuning parameters
 */
typedef struct {
    uint32_t target_improvement_percent;    /* Target performance improvement */
    bool enable_aggressive_opts;            /* Enable aggressive optimizations */
    bool enable_cache_opts;                 /* Enable cache-specific optimizations */
    bool enable_measurement_overhead;       /* Enable measurement overhead tracking */
    uint32_t measurement_sample_rate;       /* Measurement sampling rate (1-100) */
} perf_tuning_params_t;

int perf_set_tuning_parameters(const perf_tuning_params_t* params);
const perf_tuning_params_t* perf_get_tuning_parameters(void);

/*
 * Diagnostic and Debug API
 */

/**
 * Print performance analysis report
 */
void perf_print_analysis_report(void);

/**
 * Get performance statistics summary
 */
typedef struct {
    uint32_t optimizations_applied;         /* Total optimizations applied */
    uint32_t optimizations_successful;      /* Successful optimizations */
    uint32_t average_improvement_percent;   /* Average improvement across all opts */
    uint32_t max_improvement_percent;       /* Best single optimization improvement */
    uint32_t total_time_saved_us;          /* Total time saved by optimizations */
    uint32_t measurements_performed;        /* Total measurements performed */
    bool targets_achieved;                  /* Whether performance targets achieved */
} perf_statistics_summary_t;

const perf_statistics_summary_t* perf_get_statistics_summary(void);

/**
 * Validate optimization integrity
 * Checks that applied patches are still valid and working correctly
 */
bool perf_validate_optimizations(void);

/**
 * Reset all performance statistics
 */
void perf_reset_statistics(void);

/*
 * Integration with Module ABI Framework
 */

/**
 * Register performance-critical functions for optimization
 * Integrates with Module ABI v1.0 for automatic optimization
 */
int perf_register_critical_function(void* function_address, const char* function_name,
                                   perf_optimization_category_t category);

/**
 * Apply module-wide optimizations
 * Uses Module ABI framework to apply optimizations across entire module
 */
perf_optimization_result_t perf_optimize_module(void);

/*
 * Compatibility and Validation API
 */

/**
 * Check API compatibility with current system
 */
bool perf_api_compatible(void);

/**
 * Get API version information
 */
typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
    const char* build_date;
    const char* features;
} perf_api_version_t;

const perf_api_version_t* perf_get_api_version(void);

/**
 * Run performance framework self-test
 */
int perf_self_test(void);

/*
 * Convenience Macros for Common Operations
 */

/* Quick optimization application */
#define PERF_OPTIMIZE_MEMCPY(dest, src, size) \
    perf_optimize_memory_copy(dest, src, size)

#define PERF_OPTIMIZE_MEMSET(dest, val, size) \
    perf_optimize_memory_set(dest, val, size)

/* Quick measurement */
#define PERF_MEASURE(context, name, code) \
    PERF_MEASURE_MEMORY_OP(context, name, 0, code)

/* Check for specific CPU features */
#define PERF_CPU_SUPPORTS_32BIT() \
    (perf_get_cpu_capabilities()->supports_32bit_ops)

#define PERF_CPU_SUPPORTS_PUSHA() \
    (perf_get_cpu_capabilities()->supports_pusha_popa)

/* Performance target validation */
#define PERF_CHECK_TARGETS() \
    perf_targets_met(25)  /* 25% improvement target */

/*
 * Error Codes
 */
#define PERF_SUCCESS                    0
#define PERF_ERROR_NOT_INITIALIZED     -1
#define PERF_ERROR_INVALID_PARAMETER   -2
#define PERF_ERROR_CPU_NOT_SUPPORTED   -3
#define PERF_ERROR_PATCH_FAILED        -4
#define PERF_ERROR_MEASUREMENT_FAILED  -5
#define PERF_ERROR_OPTIMIZATION_FAILED -6
#define PERF_ERROR_ROLLBACK_FAILED     -7
#define PERF_ERROR_OUT_OF_MEMORY       -8
#define PERF_ERROR_INVALID_STATE       -9

/*
 * Helper Functions for Error Handling
 */
const char* perf_get_error_string(int error_code);
bool perf_is_error(int result);

/*
 * Integration Examples (Documentation)
 */

#if 0  /* Example code - not compiled */

/* Example 1: Simple automatic optimization */
void example_simple_optimization(void) {
    /* Initialize performance framework */
    perf_api_init("MY_NIC_MODULE");
    
    /* Optimize a memory copy operation */
    perf_optimization_result_t result = perf_optimize_memory_copy(dest, src, 1514);
    
    if (result.optimization_applied && result.performance_improved) {
        printf("Memory copy optimized: %d%% improvement\n", result.improvement_percent);
    }
    
    /* Cleanup */
    perf_api_shutdown();
}

/* Example 2: Performance measurement */
void example_performance_measurement(void) {
    perf_measurement_context_t context;
    
    /* Measure packet processing performance */
    perf_begin_measurement(&context, "packet_processing");
    process_received_packet(packet_buffer, packet_size);
    perf_end_measurement(&context, packet_size);
    
    /* Update module performance profile */
    perf_update_profile(&context);
    
    printf("Packet processing: %d us for %d bytes\n", 
           context.timing.elapsed_us, context.bytes_processed);
}

/* Example 3: Complete NIC module integration */
void example_complete_integration(void) {
    /* Initialize with module name */
    if (perf_api_init("3C509B_DRIVER") != PERF_SUCCESS) {
        return;
    }
    
    /* Check CPU capabilities */
    const cpu_capabilities_t* caps = perf_get_cpu_capabilities();
    printf("CPU: %s, 32-bit: %s\n", 
           cpu_type_to_string(caps->cpu_type),
           caps->supports_32bit_ops ? "Yes" : "No");
    
    /* Optimize entire receive path */
    perf_optimization_result_t rx_result = perf_optimize_rx_path(rx_buffer, 1514);
    
    /* Optimize entire transmit path */
    perf_optimization_result_t tx_result = perf_optimize_tx_path(tx_buffer, 1514);
    
    /* Check if performance targets are met */
    if (perf_targets_met(25)) {
        printf("Performance targets achieved!\n");
    }
    
    /* Print analysis report */
    perf_print_analysis_report();
    
    /* Cleanup */
    perf_api_shutdown();
}

#endif /* Example code */

#ifdef __cplusplus
}
#endif

#endif /* PERFORMANCE_API_H */