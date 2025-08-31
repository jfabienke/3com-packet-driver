/**
 * @file test_perf_cpu_detection.c
 * @brief CPU Detection and Performance Framework Integration Tests
 *
 * 3Com Packet Driver - Performance Framework Tests
 * 
 * Integration tests for CPU detection framework and performance
 * optimization system using Agent 03's test harness.
 * 
 * Agent 04 - Performance Engineer - Week 1 Day 5 Critical Deliverable
 */

#include "../common/test_framework.h"
#include "../../include/performance_api.h"
#include "../../include/cpu_detect.h"
#include "../../include/smc_patches.h"
#include "../../docs/agents/shared/timing-measurement.h"
#include <string.h>

/* Test configuration */
#define TEST_ITERATIONS             100     /* Iterations per test */
#define MIN_PERFORMANCE_GAIN        25      /* Minimum performance gain % */
#define MAX_CLI_DURATION_US         8       /* Maximum CLI duration */

/* Test data */
static uint8_t test_buffer_src[2048] __attribute__((aligned(16)));
static uint8_t test_buffer_dst[2048] __attribute__((aligned(16)));

/* Test function prototypes */
static int test_cpu_detection_accuracy(void);
static int test_performance_api_initialization(void);
static int test_memory_copy_optimization(void);
static int test_register_save_optimization(void);
static int test_smc_patch_application(void);
static int test_performance_measurement(void);
static int test_integration_with_timing_framework(void);
static int test_cross_cpu_compatibility(void);

/* Helper functions */
static void setup_test_data(void);
static bool validate_cpu_features(void);
static bool validate_performance_improvement(uint32_t baseline_us, uint32_t optimized_us);

/**
 * Main test entry point for performance framework
 */
int run_performance_framework_tests(void) {
    int result = 0;
    
    printf("=== Performance Framework Integration Tests ===\n");
    
    /* Setup test environment */
    setup_test_data();
    
    /* Initialize PIT timing */
    PIT_INIT();
    
    /* Run test suite */
    RUN_TEST(test_cpu_detection_accuracy);
    RUN_TEST(test_performance_api_initialization);
    RUN_TEST(test_memory_copy_optimization);
    RUN_TEST(test_register_save_optimization);
    RUN_TEST(test_smc_patch_application);
    RUN_TEST(test_performance_measurement);
    RUN_TEST(test_integration_with_timing_framework);
    RUN_TEST(test_cross_cpu_compatibility);
    
    printf("================================================\n");
    
    return result;
}

/**
 * Test CPU detection accuracy across all target processors
 */
static int test_cpu_detection_accuracy(void) {
    TEST_START("CPU Detection Accuracy");
    
    /* Initialize CPU detection */
    int init_result = cpu_detect_init();
    TEST_ASSERT(init_result == 0, "CPU detection initialization failed");
    
    /* Get detected CPU type */
    cpu_type_t detected_type = cpu_detect_type();
    TEST_ASSERT(detected_type >= CPU_TYPE_80286, "CPU type below minimum requirement");
    TEST_ASSERT(detected_type <= CPU_TYPE_PENTIUM_PRO, "CPU type above expected range");
    
    /* Validate CPU features are consistent with type */
    uint32_t features = cpu_get_features();
    
    if (detected_type >= CPU_TYPE_80286) {
        TEST_ASSERT(cpu_has_feature(CPU_FEATURE_PUSHA), "286+ should support PUSHA");
    }
    
    if (detected_type >= CPU_TYPE_80386) {
        TEST_ASSERT(cpu_supports_32bit(), "386+ should support 32-bit operations");
    }
    
    if (detected_type >= CPU_TYPE_80486) {
        TEST_ASSERT(cpu_has_cpuid(), "486+ should support CPUID");
    }
    
    /* Test CPU vendor detection */
    const char* vendor = cpu_get_vendor_string();
    TEST_ASSERT(vendor != NULL, "CPU vendor string should not be NULL");
    
    /* Test feature consistency */
    TEST_ASSERT(validate_cpu_features(), "CPU features inconsistent with detected type");
    
    printf("Detected CPU: %s, Features: 0x%08lX\n", 
           cpu_type_to_string(detected_type), features);
    
    TEST_END();
}

/**
 * Test performance API initialization and basic functionality
 */
static int test_performance_api_initialization(void) {
    TEST_START("Performance API Initialization");
    
    /* Test API initialization */
    int result = perf_api_init("TEST_MODULE");
    TEST_ASSERT(result == PERF_SUCCESS, "Performance API initialization failed");
    
    /* Test API compatibility */
    TEST_ASSERT(perf_api_compatible(), "Performance API not compatible with system");
    
    /* Get API version */
    const perf_api_version_t* version = perf_get_api_version();
    TEST_ASSERT(version != NULL, "API version should not be NULL");
    TEST_ASSERT(version->major == PERFORMANCE_API_VERSION_MAJOR, "API major version mismatch");
    
    /* Get CPU capabilities */
    const cpu_capabilities_t* caps = perf_get_cpu_capabilities();
    TEST_ASSERT(caps != NULL, "CPU capabilities should not be NULL");
    TEST_ASSERT(caps->cpu_type >= CPU_TYPE_80286, "CPU capabilities show unsupported CPU");
    
    /* Test self-test */
    result = perf_self_test();
    TEST_ASSERT(result == PERF_SUCCESS, "Performance framework self-test failed");
    
    /* Cleanup */
    perf_api_shutdown();
    
    printf("Performance API v%d.%d.%d initialized successfully\n",
           version->major, version->minor, version->patch);
    
    TEST_END();
}

/**
 * Test memory copy optimization functionality
 */
static int test_memory_copy_optimization(void) {
    TEST_START("Memory Copy Optimization");
    
    /* Initialize performance API */
    int result = perf_api_init("MEMCOPY_TEST");
    TEST_ASSERT(result == PERF_SUCCESS, "Performance API initialization failed");
    
    /* Test different copy sizes */
    size_t test_sizes[] = {64, 256, 1514, 4096};
    uint32_t total_improvements = 0;
    uint32_t successful_optimizations = 0;
    
    for (size_t i = 0; i < sizeof(test_sizes)/sizeof(test_sizes[0]); i++) {
        size_t size = test_sizes[i];
        
        /* Clear destination buffer */
        memset(test_buffer_dst, 0, size);
        
        /* Apply optimization */
        perf_optimization_result_t opt_result = perf_optimize_memory_copy(
            test_buffer_dst, test_buffer_src, size);
        
        /* Validate optimization */
        if (opt_result.optimization_applied) {
            TEST_ASSERT(opt_result.baseline_time_us > 0, "Baseline time should be positive");
            TEST_ASSERT(opt_result.optimized_time_us > 0, "Optimized time should be positive");
            
            if (opt_result.performance_improved) {
                total_improvements += opt_result.improvement_percent;
                successful_optimizations++;
                
                printf("Size %zu: %lu%% improvement (%luμs -> %luμs)\n",
                       size, opt_result.improvement_percent,
                       opt_result.baseline_time_us, opt_result.optimized_time_us);
            }
        }
        
        /* Verify data integrity */
        bool data_valid = (memcmp(test_buffer_dst, test_buffer_src, size) == 0);
        TEST_ASSERT(data_valid, "Memory copy corrupted data");
    }
    
    /* Check overall performance */
    if (successful_optimizations > 0) {
        uint32_t average_improvement = total_improvements / successful_optimizations;
        TEST_ASSERT(average_improvement >= MIN_PERFORMANCE_GAIN, 
                   "Average performance improvement below target");
        
        printf("Average improvement: %lu%% across %lu optimizations\n",
               average_improvement, successful_optimizations);
    }
    
    /* Cleanup */
    perf_api_shutdown();
    
    TEST_END();
}

/**
 * Test register save optimization (PUSHA/POPA)
 */
static int test_register_save_optimization(void) {
    TEST_START("Register Save Optimization");
    
    /* Skip test if CPU doesn't support PUSHA */
    if (!cpu_has_feature(CPU_FEATURE_PUSHA)) {
        printf("Skipping PUSHA test - CPU doesn't support PUSHA/POPA\n");
        TEST_END();
    }
    
    /* Initialize performance API */
    int result = perf_api_init("REGSAVE_TEST");
    TEST_ASSERT(result == PERF_SUCCESS, "Performance API initialization failed");
    
    /* Create a dummy ISR address for testing */
    void* dummy_isr = &&test_isr_label;
    
    /* Apply ISR optimization */
    perf_optimization_result_t opt_result = perf_optimize_interrupt_handler(dummy_isr);
    
    if (opt_result.optimization_applied) {
        TEST_ASSERT(opt_result.patch_status == PATCH_STATUS_APPLIED, 
                   "Register save patch should be applied");
        
        if (opt_result.performance_improved) {
            printf("Register save optimization: %lu%% improvement\n",
                   opt_result.improvement_percent);
            
            TEST_ASSERT(validate_performance_improvement(
                opt_result.baseline_time_us, opt_result.optimized_time_us),
                "Performance improvement validation failed");
        }
    }
    
test_isr_label:
    /* Dummy ISR code for testing */
    __asm__ __volatile__("nop");
    
    /* Cleanup */
    perf_api_shutdown();
    
    TEST_END();
}

/**
 * Test self-modifying code patch application
 */
static int test_smc_patch_application(void) {
    TEST_START("SMC Patch Application");
    
    /* Initialize SMC framework */
    int result = smc_patches_init();
    TEST_ASSERT(result == 0, "SMC patches initialization failed");
    
    /* Create a test patch site */
    patch_cpu_requirements_t requirements = {
        .min_cpu_type = CPU_TYPE_80286,
        .required_features = CPU_FEATURE_PUSHA,
        .requires_32bit = false,
        .requires_alignment = true,
        .alignment_bytes = 1
    };
    
    /* Test patch code (NOP instruction) */
    uint8_t test_code[] = {0x90}; /* NOP */
    void* patch_site = test_code;
    
    uint32_t patch_id = register_patch_site(patch_site, PATCH_TYPE_CUSTOM, &requirements);
    TEST_ASSERT(patch_id != 0, "Patch site registration failed");
    
    /* Validate patch safety */
    TEST_ASSERT(validate_patch_site(patch_id) == 0, "Patch site validation failed");
    
    /* Prepare custom patch */
    uint8_t patch_bytes[] = {0x90}; /* NOP (same as original for safety) */
    result = prepare_custom_patch(patch_id, patch_bytes, sizeof(patch_bytes));
    TEST_ASSERT(result == 0, "Patch preparation failed");
    
    /* Apply patch atomically */
    patch_application_result_t app_result = apply_single_patch_atomic(patch_id);
    TEST_ASSERT(app_result.status == PATCH_STATUS_APPLIED, "Patch application failed");
    
    /* Validate CLI timing constraint */
    if (app_result.cli_duration_valid) {
        TEST_ASSERT(app_result.cli_duration.elapsed_us <= MAX_CLI_DURATION_US,
                   "CLI duration exceeded maximum allowed time");
        
        printf("CLI duration: %luμs (limit: %dμs)\n", 
               app_result.cli_duration.elapsed_us, MAX_CLI_DURATION_US);
    }
    
    /* Verify patch integrity */
    TEST_ASSERT(verify_patch_integrity(patch_id), "Patch integrity check failed");
    
    /* Rollback patch */
    result = rollback_single_patch(patch_id);
    TEST_ASSERT(result == 0, "Patch rollback failed");
    
    /* Cleanup */
    smc_patches_shutdown();
    
    TEST_END();
}

/**
 * Test performance measurement accuracy
 */
static int test_performance_measurement(void) {
    TEST_START("Performance Measurement");
    
    /* Initialize performance API */
    int result = perf_api_init("MEASUREMENT_TEST");
    TEST_ASSERT(result == PERF_SUCCESS, "Performance API initialization failed");
    
    /* Test basic measurement */
    perf_measurement_context_t context;
    perf_begin_measurement(&context, "test_operation");
    
    /* Simulate some work */
    for (volatile int i = 0; i < 1000; i++) {
        /* Busy work */
    }
    
    perf_end_measurement(&context, 1000);
    
    /* Validate measurement */
    TEST_ASSERT(context.timing_valid, "Performance measurement should be valid");
    TEST_ASSERT(context.timing.elapsed_us > 0, "Elapsed time should be positive");
    TEST_ASSERT(context.bytes_processed == 1000, "Bytes processed should match input");
    
    printf("Test operation: %luμs for %lu bytes\n",
           context.timing.elapsed_us, context.bytes_processed);
    
    /* Test measurement statistics */
    perf_update_profile(&context);
    const nic_performance_profile_t* profile = perf_get_module_profile();
    TEST_ASSERT(profile != NULL, "Performance profile should be available");
    TEST_ASSERT(profile->profile_valid, "Performance profile should be valid");
    
    /* Cleanup */
    perf_api_shutdown();
    
    TEST_END();
}

/**
 * Test integration with Agent 03's timing framework
 */
static int test_integration_with_timing_framework(void) {
    TEST_START("Timing Framework Integration");
    
    /* Test PIT timing integration */
    pit_timing_t timing;
    
    PIT_START_TIMING(&timing);
    
    /* Simulate measured operation */
    for (volatile int i = 0; i < 500; i++) {
        /* Busy work */
    }
    
    PIT_END_TIMING(&timing);
    
    /* Validate timing */
    TEST_ASSERT(!timing.overflow, "PIT timing should not overflow");
    TEST_ASSERT(timing.elapsed_us > 0, "Elapsed time should be positive");
    TEST_ASSERT(timing.elapsed_us < 10000, "Elapsed time should be reasonable");
    
    /* Test timing validation macros */
    timing.elapsed_us = 5; /* Set to within CLI limit */
    TEST_ASSERT(VALIDATE_CLI_TIMING(&timing), "CLI timing should validate");
    
    timing.elapsed_us = 50; /* Set to within ISR limit */
    TEST_ASSERT(VALIDATE_ISR_TIMING(&timing), "ISR timing should validate");
    
    printf("PIT timing measurement: %luμs\n", timing.elapsed_us);
    
    TEST_END();
}

/**
 * Test cross-CPU compatibility
 */
static int test_cross_cpu_compatibility(void) {
    TEST_START("Cross-CPU Compatibility");
    
    /* Initialize performance API */
    int result = perf_api_init("COMPAT_TEST");
    TEST_ASSERT(result == PERF_SUCCESS, "Performance API initialization failed");
    
    /* Get CPU capabilities */
    const cpu_capabilities_t* caps = perf_get_cpu_capabilities();
    TEST_ASSERT(caps != NULL, "CPU capabilities should be available");
    
    /* Test that optimizations degrade gracefully */
    cpu_type_t cpu_type = caps->cpu_type;
    
    /* Test memory copy optimization on different CPU types */
    perf_optimization_result_t result_64 = perf_optimize_memory_copy(
        test_buffer_dst, test_buffer_src, 64);
    
    /* Optimization should either succeed or gracefully fall back */
    if (result_64.optimization_applied) {
        TEST_ASSERT(result_64.patch_status != PATCH_STATUS_FAILED,
                   "Applied optimization should not have failed status");
    }
    
    /* Verify data integrity regardless of optimization success */
    bool data_valid = (memcmp(test_buffer_dst, test_buffer_src, 64) == 0);
    TEST_ASSERT(data_valid, "Data should be copied correctly even without optimization");
    
    /* Test CPU-specific feature availability */
    if (cpu_type >= CPU_TYPE_80286) {
        printf("CPU supports 286+ features\n");
    }
    if (cpu_type >= CPU_TYPE_80386) {
        printf("CPU supports 386+ features\n");
    }
    if (cpu_type >= CPU_TYPE_80486) {
        printf("CPU supports 486+ features\n");
    }
    
    printf("Cross-CPU compatibility verified for %s\n", 
           cpu_type_to_string(cpu_type));
    
    /* Cleanup */
    perf_api_shutdown();
    
    TEST_END();
}

/**
 * Setup test data
 */
static void setup_test_data(void) {
    /* Initialize source buffer with test pattern */
    for (size_t i = 0; i < sizeof(test_buffer_src); i++) {
        test_buffer_src[i] = (uint8_t)(i ^ 0xAA);
    }
    
    /* Clear destination buffer */
    memset(test_buffer_dst, 0, sizeof(test_buffer_dst));
}

/**
 * Validate CPU features are consistent with detected type
 */
static bool validate_cpu_features(void) {
    cpu_type_t type = cpu_detect_type();
    uint32_t features = cpu_get_features();
    
    /* Check feature consistency */
    if (type >= CPU_TYPE_80286) {
        if (!cpu_has_feature(CPU_FEATURE_PUSHA)) {
            return false;
        }
    }
    
    if (type >= CPU_TYPE_80386) {
        if (!cpu_supports_32bit()) {
            return false;
        }
    }
    
    if (type >= CPU_TYPE_80486) {
        if (!cpu_has_cpuid()) {
            return false;
        }
    }
    
    return true;
}

/**
 * Validate performance improvement
 */
static bool validate_performance_improvement(uint32_t baseline_us, uint32_t optimized_us) {
    if (baseline_us == 0 || optimized_us == 0) {
        return false;
    }
    
    if (optimized_us >= baseline_us) {
        return false; /* No improvement */
    }
    
    uint32_t improvement = ((baseline_us - optimized_us) * 100) / baseline_us;
    return (improvement >= MIN_PERFORMANCE_GAIN);
}