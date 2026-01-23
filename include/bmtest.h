/**
 * @file busmaster_test.h
 * @brief Comprehensive 45-second automated bus mastering capability testing framework
 *
 * Sprint 0B.5: Automated Bus Mastering Test for 80286 Systems
 * Final critical safety feature needed to complete Phase 0
 *
 * This implements comprehensive bus mastering testing that safely enables 
 * bus mastering on 80286 systems where chipset compatibility varies significantly.
 * Failed tests automatically fall back to programmed I/O for safety.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _BUSMASTER_TEST_H_
#define _BUSMASTER_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "errhndl.h"
#include "config.h"
#include <stdint.h>
#include <stdbool.h>

/* Test mode enumeration */
typedef enum {
    BM_TEST_MODE_FULL = 0,      /* Full 45-second test */
    BM_TEST_MODE_QUICK = 1      /* Quick 10-second test */
} busmaster_test_mode_t;

/* Test phase enumeration */
typedef enum {
    BM_TEST_BASIC = 0,          /* Basic functionality tests */
    BM_TEST_STRESS = 1,         /* Stress testing phase */
    BM_TEST_STABILITY = 2       /* Long-duration stability testing */
} busmaster_test_phase_t;

/* Confidence level enumeration */
typedef enum {
    BM_CONFIDENCE_FAILED = 0,   /* Testing failed - use PIO */
    BM_CONFIDENCE_LOW = 1,      /* Low confidence - recommend PIO */
    BM_CONFIDENCE_MEDIUM = 2,   /* Medium confidence - conditional use */
    BM_CONFIDENCE_HIGH = 3      /* High confidence - safe to use */
} busmaster_confidence_t;

/* Individual test scoring maximums */
#define BM_SCORE_DMA_CONTROLLER_MAX         70  /* DMA controller presence */
#define BM_SCORE_MEMORY_COHERENCY_MAX       80  /* Memory coherency */
#define BM_SCORE_TIMING_CONSTRAINTS_MAX     100 /* Timing constraints */
#define BM_SCORE_DATA_INTEGRITY_MAX         85  /* Data integrity patterns */
#define BM_SCORE_BURST_TRANSFER_MAX         82  /* Burst transfer capability */
#define BM_SCORE_ERROR_RECOVERY_MAX         85  /* Error recovery mechanisms */
#define BM_SCORE_STABILITY_MAX              50  /* Long duration stability */

/* Total maximum possible score */
#define BM_SCORE_TOTAL_MAX                  552 /* Sum of all maximums */

/* Confidence thresholds (out of 552 total points) */
#define BM_CONFIDENCE_HIGH_THRESHOLD        400 /* >= 400 points = HIGH confidence */
#define BM_CONFIDENCE_MEDIUM_THRESHOLD      250 /* >= 250 points = MEDIUM confidence */
#define BM_CONFIDENCE_LOW_THRESHOLD         150 /* >= 150 points = LOW confidence */
#define BM_CONFIDENCE_FAILED_THRESHOLD      150 /* < 150 points = FAILED */

/* Test duration constants */
#define BM_TEST_DURATION_FULL_MS            45000   /* 45 seconds full test */
#define BM_TEST_DURATION_QUICK_MS           10000   /* 10 seconds quick test */
#define BM_TEST_DURATION_STABILITY_MS       30000   /* 30 seconds stability test */

/* Test pattern constants */
#define BM_TEST_PATTERN_COUNT               16      /* Number of test patterns */
#define BM_TEST_PATTERN_SIZE                1024    /* Size of each test pattern */
#define BM_TEST_BUFFER_SIZE                 4096    /* DMA test buffer size */

/* Error thresholds for testing */
#define BM_TEST_MAX_ERRORS_BASIC            2       /* Max errors in basic tests */
#define BM_TEST_MAX_ERRORS_STRESS           5       /* Max errors in stress tests */
#define BM_TEST_MAX_ERRORS_STABILITY        10      /* Max errors in stability tests */

/* Timing constraint test parameters */
#define BM_TEST_MIN_BURST_SIZE              64      /* Minimum burst size bytes */
#define BM_TEST_MAX_BURST_SIZE              4096    /* Maximum burst size bytes */
#define BM_TEST_TIMING_TOLERANCE_PERCENT    10      /* Timing tolerance percentage */

/**
 * @brief Comprehensive bus mastering test results structure
 * 
 * Contains detailed results from all phases of bus mastering capability testing
 * with 0-552 point scoring system and confidence level determination.
 */
typedef struct {
    /* Overall test results */
    uint16_t confidence_score;          /* Total confidence score (0-552) */
    busmaster_confidence_t confidence_level; /* Confidence level determination */
    busmaster_test_phase_t test_phase;  /* Current/last test phase */
    uint32_t test_duration_ms;          /* Actual test duration */
    bool test_completed;                /* Test completed successfully */
    
    /* Individual test scores */
    uint16_t dma_controller_score;      /* DMA controller test score (0-70) */
    uint16_t memory_coherency_score;    /* Memory coherency test score (0-80) */
    uint16_t timing_constraints_score;  /* Timing test score (0-100) */
    uint16_t data_integrity_score;      /* Data integrity test score (0-85) */
    uint16_t burst_transfer_score;      /* Burst transfer test score (0-82) */
    uint16_t error_recovery_score;      /* Error recovery test score (0-85) */
    uint16_t stability_score;           /* Stability test score (0-50) */
    
    /* Test pass/fail flags */
    bool dma_coherency_passed;          /* DMA coherency test passed */
    bool burst_timing_passed;           /* Burst timing test passed */
    bool error_recovery_passed;         /* Error recovery test passed */
    bool stability_passed;              /* Stability test passed */
    
    /* Detailed test metrics */
    uint32_t patterns_verified;         /* Number of patterns verified */
    uint16_t error_count;               /* Total errors encountered */
    uint16_t recovery_attempts;         /* Recovery attempts made */
    uint32_t bytes_transferred;         /* Total bytes transferred */
    uint32_t transfers_completed;       /* Total transfers completed */
    
    /* Performance metrics */
    uint32_t avg_transfer_rate_bps;     /* Average transfer rate (bytes/sec) */
    uint32_t peak_transfer_rate_bps;    /* Peak transfer rate (bytes/sec) */
    uint32_t min_latency_us;            /* Minimum latency (microseconds) */
    uint32_t max_latency_us;            /* Maximum latency (microseconds) */
    uint32_t avg_latency_us;            /* Average latency (microseconds) */
    
    /* Error breakdown by category */
    uint16_t dma_errors;                /* DMA-related errors */
    uint16_t timing_errors;             /* Timing constraint violations */
    uint16_t coherency_errors;          /* Memory coherency errors */
    uint16_t burst_errors;              /* Burst transfer errors */
    uint16_t stability_errors;          /* Stability test errors */
    
    /* System compatibility information */
    bool cpu_supports_busmaster;       /* CPU supports bus mastering */
    bool chipset_compatible;            /* Chipset appears compatible */
    bool dma_controller_present;        /* DMA controller detected */
    bool memory_coherent;               /* Memory appears coherent */
    
    /* Safety and fallback information */
    bool safe_for_production;           /* Safe for production use */
    bool requires_fallback;             /* Should fall back to PIO */
    char failure_reason[128];           /* Reason for failure if any */
    char recommendations[256];          /* Usage recommendations */
} busmaster_test_results_t;

/**
 * @brief DMA controller test parameters
 */
typedef struct {
    uint16_t controller_id;             /* DMA controller ID */
    uint8_t channel_mask;               /* Available DMA channels */
    bool supports_16bit;                /* Supports 16-bit transfers */
    bool supports_32bit;                /* Supports 32-bit transfers */
    uint32_t max_transfer_size;         /* Maximum transfer size */
    uint32_t alignment_requirement;     /* Address alignment requirement */
} dma_controller_info_t;

/**
 * @brief Memory coherency test parameters
 */
typedef struct {
    uint32_t test_address;              /* Test memory address */
    uint32_t test_size;                 /* Test memory size */
    uint8_t *test_pattern;              /* Test pattern data */
    uint32_t pattern_size;              /* Pattern size */
    bool cache_coherent;                /* Cache coherency detected */
    bool write_coherent;                /* Write coherency detected */
    bool read_coherent;                 /* Read coherency detected */
} memory_coherency_info_t;

/**
 * @brief Timing constraint test parameters
 */
typedef struct {
    uint32_t min_setup_time_ns;         /* Minimum setup time */
    uint32_t min_hold_time_ns;          /* Minimum hold time */
    uint32_t max_burst_duration_ns;     /* Maximum burst duration */
    uint32_t measured_setup_time_ns;    /* Measured setup time */
    uint32_t measured_hold_time_ns;     /* Measured hold time */
    uint32_t measured_burst_time_ns;    /* Measured burst time */
    bool timing_constraints_met;        /* All timing constraints met */
} timing_constraint_info_t;

/**
 * @brief Data integrity test patterns
 */
typedef struct {
    uint8_t walking_ones[256];          /* Walking ones pattern */
    uint8_t walking_zeros[256];         /* Walking zeros pattern */
    uint8_t alternating_55[256];        /* Alternating 0x55 pattern */
    uint8_t alternating_AA[256];        /* Alternating 0xAA pattern */
    uint8_t random_pattern[256];        /* Random data pattern */
    uint8_t address_pattern[256];       /* Address-based pattern */
    uint8_t checksum_pattern[256];      /* Checksum verification pattern */
    uint8_t burst_pattern[256];         /* Burst transfer pattern */
} data_integrity_patterns_t;

/**
 * @brief Test result cache entry for persistent storage
 */
typedef struct {
    char signature[8];                  /* "3CPKT" + version */
    uint32_t cache_version;             /* Cache format version */
    uint32_t test_date;                 /* Date of test (seconds since epoch) */
    cpu_type_t cpu_type;                /* CPU type when test was performed */
    uint32_t chipset_id;                /* Chipset identifier for validation */
    uint16_t io_base;                   /* NIC I/O base address */
    
    /* Test results */
    busmaster_test_mode_t test_mode;    /* Test mode used (QUICK/FULL) */
    uint16_t confidence_score;          /* Total confidence score */
    busmaster_confidence_t confidence_level; /* Confidence level */
    bool test_completed;                /* Test completed successfully */
    bool safe_for_production;           /* Safe for production use */
    bool busmaster_enabled;             /* Bus mastering was enabled */
    
    /* Individual test scores */
    uint16_t dma_controller_score;      /* DMA controller test score */
    uint16_t memory_coherency_score;    /* Memory coherency test score */
    uint16_t timing_constraints_score;  /* Timing constraints test score */
    uint16_t data_integrity_score;      /* Data integrity test score */
    uint16_t burst_transfer_score;      /* Burst transfer test score */
    uint16_t error_recovery_score;      /* Error recovery test score */
    uint16_t stability_score;           /* Stability test score */
    
    uint32_t checksum;                  /* Cache integrity checksum */
} busmaster_test_cache_t;

/**
 * @brief Cache validation and management
 */
typedef struct {
    bool cache_valid;                   /* Cache is valid and usable */
    bool hardware_changed;              /* Hardware configuration changed */
    bool driver_version_changed;        /* Driver version changed */
    bool force_retest;                  /* User requested forced retest */
    char cache_file_path[256];          /* Path to cache file */
    char invalidation_reason[128];      /* Reason for cache invalidation */
} cache_validation_info_t;

/* Function prototypes for the main testing framework */

/**
 * @brief Perform comprehensive automated bus mastering capability test
 * 
 * This is the main entry point for the 45-second automated testing framework.
 * It performs three-phase testing (Basic, Stress, Stability) and returns
 * detailed results with confidence scoring and safety recommendations.
 * 
 * @param ctx NIC context for testing
 * @param mode Test mode (FULL or QUICK)
 * @param results Detailed test results structure
 * @return 0 on success, negative on error
 */
int perform_automated_busmaster_test(nic_context_t *ctx, 
                                   busmaster_test_mode_t mode,
                                   busmaster_test_results_t *results);

/**
 * @brief Initialize bus mastering test framework
 * 
 * @param ctx NIC context to initialize for testing
 * @return 0 on success, negative on error
 */
int busmaster_test_init(nic_context_t *ctx);

/**
 * @brief Cleanup bus mastering test framework
 * 
 * @param ctx NIC context to cleanup
 */
void busmaster_test_cleanup(nic_context_t *ctx);

/* Individual test function prototypes */

/**
 * @brief Test DMA controller presence and capabilities (70 points max)
 * 
 * Tests for presence of DMA controller, available channels, transfer modes,
 * and basic register accessibility. Critical for bus mastering capability.
 * 
 * @param ctx NIC context
 * @param info DMA controller information structure
 * @return Score 0-70 based on DMA controller capabilities
 */
uint16_t test_dma_controller_presence(nic_context_t *ctx, dma_controller_info_t *info);

/**
 * @brief Test memory coherency between CPU and DMA (80 points max)
 * 
 * Verifies that memory writes by CPU are visible to DMA controller and
 * vice versa. Critical for data integrity in bus mastering operations.
 * 
 * @param ctx NIC context
 * @param info Memory coherency information structure
 * @return Score 0-80 based on memory coherency capabilities
 */
uint16_t test_memory_coherency(nic_context_t *ctx, memory_coherency_info_t *info);

/**
 * @brief Test timing constraints for bus mastering (100 points max)
 * 
 * Verifies setup times, hold times, and burst duration constraints
 * required for reliable bus mastering operation on 80286 systems.
 * 
 * @param ctx NIC context
 * @param info Timing constraint information structure
 * @return Score 0-100 based on timing constraint compliance
 */
uint16_t test_timing_constraints(nic_context_t *ctx, timing_constraint_info_t *info);

/**
 * @brief Test data integrity with various patterns (85 points max)
 * 
 * Transfers multiple data patterns via bus mastering and verifies
 * data integrity using checksums and pattern verification.
 * 
 * @param ctx NIC context
 * @param patterns Data integrity test patterns
 * @return Score 0-85 based on data integrity verification
 */
uint16_t test_data_integrity_patterns(nic_context_t *ctx, data_integrity_patterns_t *patterns);

/**
 * @brief Test burst transfer capability (82 points max)
 * 
 * Tests various burst sizes and transfer modes to verify
 * bus mastering can handle different transfer scenarios.
 * 
 * @param ctx NIC context
 * @return Score 0-82 based on burst transfer capabilities
 */
uint16_t test_burst_transfer_capability(nic_context_t *ctx);

/**
 * @brief Test error recovery mechanisms (85 points max)
 * 
 * Intentionally triggers error conditions and verifies that
 * the system can recover gracefully from bus mastering errors.
 * 
 * @param ctx NIC context
 * @return Score 0-85 based on error recovery capabilities
 */
uint16_t test_error_recovery_mechanisms(nic_context_t *ctx);

/**
 * @brief Test long duration stability (50 points max)
 * 
 * Runs continuous bus mastering operations for 30 seconds
 * to verify stability under sustained load.
 * 
 * @param ctx NIC context
 * @param duration_ms Test duration in milliseconds
 * @return Score 0-50 based on stability performance
 */
uint16_t test_long_duration_stability(nic_context_t *ctx, uint32_t duration_ms);

/* Configuration and utility functions */

/**
 * @brief Determine confidence level from test score
 * 
 * @param score Total test score (0-552)
 * @return Confidence level enumeration
 */
busmaster_confidence_t determine_confidence_level(uint16_t score);

/**
 * @brief Check if CPU supports bus mastering
 * 
 * @return true if CPU supports bus mastering, false otherwise
 */
bool cpu_supports_busmaster_operations(void);

/**
 * @brief Check if CPU requires conservative testing approach
 * 
 * @return true for 286 systems (require exhaustive testing), false for 386+
 */
bool cpu_requires_conservative_testing(void);

/**
 * @brief Get minimum confidence threshold for bus mastering based on CPU
 * 
 * @return HIGH threshold for 286, MEDIUM threshold for 386+
 */
uint16_t get_cpu_appropriate_confidence_threshold(void);

/* Cache management function prototypes */

/**
 * @brief Load cached test results from disk
 * 
 * @param ctx NIC context
 * @param cache Loaded cache data
 * @return 0 on success, negative on error or cache not found
 */
int load_busmaster_test_cache(nic_context_t *ctx, busmaster_test_cache_t *cache);

/**
 * @brief Save test results to cache file
 * 
 * @param ctx NIC context 
 * @param results Test results to cache
 * @return 0 on success, negative on error
 */
int save_busmaster_test_cache(nic_context_t *ctx, const busmaster_test_results_t *results);

/**
 * @brief Validate cached test results
 * 
 * @param ctx NIC context
 * @param cache Cached data to validate
 * @param validation Validation results
 * @return 0 if cache is valid, negative if invalid
 */
int validate_busmaster_test_cache(nic_context_t *ctx, const busmaster_test_cache_t *cache, 
                                cache_validation_info_t *validation);

/**
 * @brief Invalidate cached test results (force retest)
 * 
 * @param ctx NIC context
 * @param reason Reason for invalidation
 * @return 0 on success, negative on error
 */
int invalidate_busmaster_test_cache(nic_context_t *ctx, const char *reason);

/**
 * @brief Convert cached results back to test results structure
 * 
 * @param cache Cached data
 * @param results Output test results
 * @return 0 on success, negative on error
 */
int cache_to_test_results(const busmaster_test_cache_t *cache, busmaster_test_results_t *results);

/**
 * @brief Apply bus mastering configuration based on test results
 * 
 * Automatically configures bus mastering mode based on test confidence
 * level and applies safe fallback if tests failed.
 * 
 * @param ctx NIC context
 * @param results Test results
 * @param config Configuration structure to update
 * @return 0 on success, negative on error
 */
int apply_busmaster_configuration(nic_context_t *ctx, 
                                const busmaster_test_results_t *results,
                                config_t *config);

/**
 * @brief Generate detailed test report
 * 
 * Creates a comprehensive human-readable report of test results
 * including recommendations and safety considerations.
 * 
 * @param results Test results
 * @param buffer Output buffer for report
 * @param buffer_size Size of output buffer
 * @return 0 on success, negative on error
 */
int generate_busmaster_test_report(const busmaster_test_results_t *results,
                                 char *buffer, size_t buffer_size);

/**
 * @brief Log test progress and results
 * 
 * Provides detailed logging throughout the testing process for
 * debugging and monitoring purposes.
 * 
 * @param results Current test results
 * @param phase Current test phase
 * @param message Additional message to log
 */
void log_busmaster_test_progress(const busmaster_test_results_t *results,
                               busmaster_test_phase_t phase,
                               const char *message);

/* Safety and fallback functions */

/**
 * @brief Safe fallback to programmed I/O mode
 * 
 * Safely disables bus mastering and configures NIC for programmed I/O
 * operation when bus mastering tests fail or indicate unsafe conditions.
 * 
 * @param ctx NIC context
 * @param config Configuration to update
 * @param reason Reason for fallback
 * @return 0 on success, negative on error
 */
int fallback_to_programmed_io(nic_context_t *ctx, config_t *config, const char *reason);

/**
 * @brief Validate test environment safety
 * 
 * Performs initial safety checks before beginning bus mastering tests
 * to ensure the test environment is safe and appropriate.
 * 
 * @param ctx NIC context
 * @return true if environment is safe for testing, false otherwise
 */
bool validate_test_environment_safety(nic_context_t *ctx);

/**
 * @brief Emergency stop function for testing
 * 
 * Immediately stops all bus mastering test operations and puts
 * the system in a safe state in case of serious errors.
 * 
 * @param ctx NIC context
 */
void emergency_stop_busmaster_test(nic_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _BUSMASTER_TEST_H_ */