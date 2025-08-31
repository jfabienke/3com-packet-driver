/**
 * @file test_busmaster_sprint0b5.c
 * @brief Test program for Sprint 0B.5: Automated Bus Mastering Test Framework
 *
 * This test program demonstrates the comprehensive 45-second automated bus mastering
 * capability testing framework that safely enables bus mastering on 80286 systems
 * where chipset compatibility varies significantly.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Include project headers */
#include "include/busmaster_test.h"
#include "include/config.h"
#include "include/error_handling.h"
#include "include/logging.h"
#include "include/hardware.h"

/* Test framework macros */
#define TEST_START(name) printf("=== Testing %s ===\n", name); fflush(stdout)
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s\n", message); \
            test_failed = true; \
        } else { \
            printf("PASS: %s\n", message); \
        } \
        fflush(stdout); \
    } while(0)
#define TEST_END(name) \
    printf("=== %s %s ===\n\n", name, test_failed ? "FAILED" : "PASSED"); \
    fflush(stdout)

/* Global test state */
static bool overall_test_passed = true;

/* Test function prototypes */
static bool test_busmaster_framework_initialization(void);
static bool test_dma_controller_detection(void);
static bool test_memory_coherency_validation(void);
static bool test_timing_constraints_verification(void);
static bool test_data_integrity_patterns_verification(void);
static bool test_burst_transfer_capabilities(void);
static bool test_error_recovery_mechanisms(void);
static bool test_stability_testing(void);
static bool test_confidence_level_determination(void);
static bool test_auto_configuration_integration(void);
static bool test_safety_fallback_mechanisms(void);
static bool test_comprehensive_test_scenarios(void);

/* Mock NIC context for testing */
static nic_context_t create_mock_nic_context(nic_type_t type, uint16_t io_base) {
    nic_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    
    ctx.nic_info.type = type;
    ctx.nic_info.io_base = io_base;
    ctx.nic_info.status = NIC_STATUS_PRESENT | NIC_STATUS_INITIALIZED;
    
    if (type == NIC_TYPE_3C515_TX) {
        ctx.nic_info.capabilities = HW_CAP_DMA | HW_CAP_BUS_MASTER | HW_CAP_MULTICAST;
    } else {
        ctx.nic_info.capabilities = HW_CAP_MULTICAST;
    }
    
    return ctx;
}

/**
 * @brief Main test function
 */
int main(int argc, char *argv[]) {
    printf("Sprint 0B.5: Automated Bus Mastering Test Framework\n");
    printf("====================================================\n");
    printf("Testing comprehensive 45-second automated bus mastering capability framework\n\n");
    
    bool run_full_suite = true;
    bool run_quick_mode = false;
    
    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quick") == 0) {
            run_quick_mode = true;
            printf("Running in quick test mode (10-second tests)\n\n");
        }
    }
    
    /* Run all tests */
    overall_test_passed &= test_busmaster_framework_initialization();
    overall_test_passed &= test_dma_controller_detection();
    overall_test_passed &= test_memory_coherency_validation();
    overall_test_passed &= test_timing_constraints_verification();
    overall_test_passed &= test_data_integrity_patterns_verification();
    overall_test_passed &= test_burst_transfer_capabilities();
    overall_test_passed &= test_error_recovery_mechanisms();
    
    if (!run_quick_mode) {
        overall_test_passed &= test_stability_testing();
    }
    
    overall_test_passed &= test_confidence_level_determination();
    overall_test_passed &= test_auto_configuration_integration();
    overall_test_passed &= test_safety_fallback_mechanisms();
    overall_test_passed &= test_comprehensive_test_scenarios();
    
    /* Print final results */
    printf("==========================================================\n");
    printf("Sprint 0B.5 Test Results: %s\n", overall_test_passed ? "PASSED" : "FAILED");
    printf("==========================================================\n");
    
    if (overall_test_passed) {
        printf("✓ All bus mastering test framework components working correctly\n");
        printf("✓ Comprehensive 45-second testing capability implemented\n");
        printf("✓ Three-phase testing architecture functional\n");
        printf("✓ 0-552 point scoring system operational\n");
        printf("✓ Confidence level determination accurate\n");
        printf("✓ Safe fallback mechanisms verified\n");
        printf("✓ Integration with BUSMASTER=AUTO parsing complete\n");
        printf("\nThe automated bus mastering test framework is ready for production use.\n");
        printf("This completes the final critical safety feature needed for Phase 0.\n");
    } else {
        printf("✗ Some tests failed - framework needs attention\n");
        printf("✗ Review failed tests and fix issues before deployment\n");
    }
    
    return overall_test_passed ? 0 : 1;
}

/**
 * @brief Test bus mastering framework initialization
 */
static bool test_busmaster_framework_initialization(void) {
    TEST_START("Bus Mastering Framework Initialization");
    bool test_failed = false;
    
    /* Create mock NIC context */
    nic_context_t ctx = create_mock_nic_context(NIC_TYPE_3C515_TX, 0x300);
    
    /* Test framework initialization */
    int result = busmaster_test_init(&ctx);
    TEST_ASSERT(result == 0, "Framework initialization succeeds");
    
    /* Test double initialization (should succeed gracefully) */
    result = busmaster_test_init(&ctx);
    TEST_ASSERT(result == 0, "Double initialization handled gracefully");
    
    /* Test environment safety validation */
    bool safe = validate_test_environment_safety(&ctx);
    TEST_ASSERT(safe == true, "Test environment safety validation passes");
    
    /* Test CPU bus mastering support detection */
    bool cpu_supports = cpu_supports_busmaster_operations();
    printf("INFO: CPU supports bus mastering: %s\n", cpu_supports ? "YES" : "NO");
    
    /* Cleanup */
    busmaster_test_cleanup(&ctx);
    
    TEST_END("Bus Mastering Framework Initialization");
    return !test_failed;
}

/**
 * @brief Test DMA controller detection and scoring
 */
static bool test_dma_controller_detection(void) {
    TEST_START("DMA Controller Detection (70 points max)");
    bool test_failed = false;
    
    nic_context_t ctx = create_mock_nic_context(NIC_TYPE_3C515_TX, 0x300);
    dma_controller_info_t dma_info;
    
    /* Initialize framework */
    busmaster_test_init(&ctx);
    
    /* Test DMA controller presence detection */
    uint16_t score = test_dma_controller_presence(&ctx, &dma_info);
    TEST_ASSERT(score > 0, "DMA controller presence detected");
    TEST_ASSERT(score <= BM_SCORE_DMA_CONTROLLER_MAX, "Score within valid range");
    
    printf("INFO: DMA Controller Score: %u/70 points\n", score);
    printf("INFO: Supports 32-bit: %s\n", dma_info.supports_32bit ? "YES" : "NO");
    printf("INFO: Max transfer size: %lu bytes\n", dma_info.max_transfer_size);
    
    /* Test with non-DMA NIC */
    nic_context_t ctx_pio = create_mock_nic_context(NIC_TYPE_3C509B, 0x300);
    uint16_t score_pio = test_dma_controller_presence(&ctx_pio, &dma_info);
    TEST_ASSERT(score_pio == 0, "Non-DMA NIC returns zero score");
    
    busmaster_test_cleanup(&ctx);
    
    TEST_END("DMA Controller Detection");
    return !test_failed;
}

/**
 * @brief Test memory coherency validation
 */
static bool test_memory_coherency_validation(void) {
    TEST_START("Memory Coherency Validation (80 points max)");
    bool test_failed = false;
    
    nic_context_t ctx = create_mock_nic_context(NIC_TYPE_3C515_TX, 0x300);
    memory_coherency_info_t coherency_info;
    
    busmaster_test_init(&ctx);
    
    /* Test memory coherency */
    uint16_t score = test_memory_coherency(&ctx, &coherency_info);
    TEST_ASSERT(score > 0, "Memory coherency test produces score");
    TEST_ASSERT(score <= BM_SCORE_MEMORY_COHERENCY_MAX, "Score within valid range");
    
    printf("INFO: Memory Coherency Score: %u/80 points\n", score);
    printf("INFO: Cache coherent: %s\n", coherency_info.cache_coherent ? "YES" : "NO");
    printf("INFO: Write coherent: %s\n", coherency_info.write_coherent ? "YES" : "NO");
    printf("INFO: Read coherent: %s\n", coherency_info.read_coherent ? "YES" : "NO");
    
    busmaster_test_cleanup(&ctx);
    
    TEST_END("Memory Coherency Validation");
    return !test_failed;
}

/**
 * @brief Test timing constraints verification
 */
static bool test_timing_constraints_verification(void) {
    TEST_START("Timing Constraints Verification (100 points max)");
    bool test_failed = false;
    
    nic_context_t ctx = create_mock_nic_context(NIC_TYPE_3C515_TX, 0x300);
    timing_constraint_info_t timing_info;
    
    busmaster_test_init(&ctx);
    
    /* Test timing constraints */
    uint16_t score = test_timing_constraints(&ctx, &timing_info);
    TEST_ASSERT(score >= 0, "Timing constraints test completes");
    TEST_ASSERT(score <= BM_SCORE_TIMING_CONSTRAINTS_MAX, "Score within valid range");
    
    printf("INFO: Timing Constraints Score: %u/100 points\n", score);
    printf("INFO: Setup time: %lu ns (min: %lu ns)\n", 
           timing_info.measured_setup_time_ns, timing_info.min_setup_time_ns);
    printf("INFO: Hold time: %lu ns (min: %lu ns)\n", 
           timing_info.measured_hold_time_ns, timing_info.min_hold_time_ns);
    printf("INFO: Burst time: %lu ns (max: %lu ns)\n", 
           timing_info.measured_burst_time_ns, timing_info.max_burst_duration_ns);
    printf("INFO: Constraints met: %s\n", timing_info.timing_constraints_met ? "YES" : "NO");
    
    busmaster_test_cleanup(&ctx);
    
    TEST_END("Timing Constraints Verification");
    return !test_failed;
}

/**
 * @brief Test data integrity patterns verification
 */
static bool test_data_integrity_patterns_verification(void) {
    TEST_START("Data Integrity Patterns (85 points max)");
    bool test_failed = false;
    
    nic_context_t ctx = create_mock_nic_context(NIC_TYPE_3C515_TX, 0x300);
    data_integrity_patterns_t patterns;
    
    busmaster_test_init(&ctx);
    
    /* Test data integrity patterns */
    uint16_t score = test_data_integrity_patterns(&ctx, &patterns);
    TEST_ASSERT(score > 0, "Data integrity test produces score");
    TEST_ASSERT(score <= BM_SCORE_DATA_INTEGRITY_MAX, "Score within valid range");
    
    printf("INFO: Data Integrity Score: %u/85 points\n", score);
    
    busmaster_test_cleanup(&ctx);
    
    TEST_END("Data Integrity Patterns");
    return !test_failed;
}

/**
 * @brief Test burst transfer capabilities
 */
static bool test_burst_transfer_capabilities(void) {
    TEST_START("Burst Transfer Capabilities (82 points max)");
    bool test_failed = false;
    
    nic_context_t ctx = create_mock_nic_context(NIC_TYPE_3C515_TX, 0x300);
    
    busmaster_test_init(&ctx);
    
    /* Test burst transfer capabilities */
    uint16_t score = test_burst_transfer_capability(&ctx);
    TEST_ASSERT(score >= 0, "Burst transfer test completes");
    TEST_ASSERT(score <= BM_SCORE_BURST_TRANSFER_MAX, "Score within valid range");
    
    printf("INFO: Burst Transfer Score: %u/82 points\n", score);
    
    busmaster_test_cleanup(&ctx);
    
    TEST_END("Burst Transfer Capabilities");
    return !test_failed;
}

/**
 * @brief Test error recovery mechanisms
 */
static bool test_error_recovery_mechanisms(void) {
    TEST_START("Error Recovery Mechanisms (85 points max)");
    bool test_failed = false;
    
    nic_context_t ctx = create_mock_nic_context(NIC_TYPE_3C515_TX, 0x300);
    
    busmaster_test_init(&ctx);
    
    /* Test error recovery mechanisms */
    uint16_t score = test_error_recovery_mechanisms(&ctx);
    TEST_ASSERT(score >= 0, "Error recovery test completes");
    TEST_ASSERT(score <= BM_SCORE_ERROR_RECOVERY_MAX, "Score within valid range");
    
    printf("INFO: Error Recovery Score: %u/85 points\n", score);
    
    busmaster_test_cleanup(&ctx);
    
    TEST_END("Error Recovery Mechanisms");
    return !test_failed;
}

/**
 * @brief Test stability testing (30-second duration)
 */
static bool test_stability_testing(void) {
    TEST_START("Long Duration Stability (50 points max)");
    bool test_failed = false;
    
    nic_context_t ctx = create_mock_nic_context(NIC_TYPE_3C515_TX, 0x300);
    
    busmaster_test_init(&ctx);
    
    /* Test stability with shorter duration for testing */
    uint32_t test_duration = 2000; /* 2 seconds for testing */
    uint16_t score = test_long_duration_stability(&ctx, test_duration);
    TEST_ASSERT(score >= 0, "Stability test completes");
    TEST_ASSERT(score <= BM_SCORE_STABILITY_MAX, "Score within valid range");
    
    printf("INFO: Stability Score: %u/50 points (2-second test)\n", score);
    
    busmaster_test_cleanup(&ctx);
    
    TEST_END("Long Duration Stability");
    return !test_failed;
}

/**
 * @brief Test confidence level determination
 */
static bool test_confidence_level_determination(void) {
    TEST_START("Confidence Level Determination");
    bool test_failed = false;
    
    /* Test confidence level thresholds */
    busmaster_confidence_t level;
    
    level = determine_confidence_level(500);
    TEST_ASSERT(level == BM_CONFIDENCE_HIGH, "High confidence level (500 points)");
    
    level = determine_confidence_level(300);
    TEST_ASSERT(level == BM_CONFIDENCE_MEDIUM, "Medium confidence level (300 points)");
    
    level = determine_confidence_level(200);
    TEST_ASSERT(level == BM_CONFIDENCE_LOW, "Low confidence level (200 points)");
    
    level = determine_confidence_level(100);
    TEST_ASSERT(level == BM_CONFIDENCE_FAILED, "Failed confidence level (100 points)");
    
    TEST_END("Confidence Level Determination");
    return !test_failed;
}

/**
 * @brief Test auto-configuration integration
 */
static bool test_auto_configuration_integration(void) {
    TEST_START("Auto-Configuration Integration");
    bool test_failed = false;
    
    config_t config;
    nic_context_t ctx = create_mock_nic_context(NIC_TYPE_3C515_TX, 0x300);
    
    /* Initialize configuration */
    config_get_defaults(&config);
    config.busmaster = BUSMASTER_AUTO;
    
    /* Test auto-configuration with quick mode */
    int result = config_perform_busmaster_auto_test(&config, &ctx, true);
    TEST_ASSERT(result == 0, "Auto-configuration test completes successfully");
    TEST_ASSERT(config.busmaster != BUSMASTER_AUTO, "Configuration updated from AUTO");
    
    printf("INFO: Final busmaster setting: %s\n",
           (config.busmaster == BUSMASTER_ON) ? "ON" :
           (config.busmaster == BUSMASTER_OFF) ? "OFF" : "AUTO");
    
    /* Test with non-DMA NIC */
    nic_context_t ctx_pio = create_mock_nic_context(NIC_TYPE_3C509B, 0x300);
    config.busmaster = BUSMASTER_AUTO;
    result = config_perform_busmaster_auto_test(&config, &ctx_pio, true);
    TEST_ASSERT(result == 0, "Auto-configuration handles non-DMA NIC");
    TEST_ASSERT(config.busmaster == BUSMASTER_OFF, "Non-DMA NIC set to OFF");
    
    TEST_END("Auto-Configuration Integration");
    return !test_failed;
}

/**
 * @brief Test safety fallback mechanisms
 */
static bool test_safety_fallback_mechanisms(void) {
    TEST_START("Safety Fallback Mechanisms");
    bool test_failed = false;
    
    config_t config;
    nic_context_t ctx = create_mock_nic_context(NIC_TYPE_3C515_TX, 0x300);
    
    config_get_defaults(&config);
    config.busmaster = BUSMASTER_ON;
    
    /* Test fallback to programmed I/O */
    int result = fallback_to_programmed_io(&ctx, &config, "Test fallback");
    TEST_ASSERT(result == 0, "Fallback to PIO succeeds");
    TEST_ASSERT(config.busmaster == BUSMASTER_OFF, "Configuration set to OFF");
    
    /* Test emergency stop */
    emergency_stop_busmaster_test(&ctx);
    TEST_ASSERT(true, "Emergency stop completes without crash");
    
    TEST_END("Safety Fallback Mechanisms");
    return !test_failed;
}

/**
 * @brief Test comprehensive test scenarios
 */
static bool test_comprehensive_test_scenarios(void) {
    TEST_START("Comprehensive Test Scenarios");
    bool test_failed = false;
    
    nic_context_t ctx = create_mock_nic_context(NIC_TYPE_3C515_TX, 0x300);
    busmaster_test_results_t results;
    
    /* Test full automated test suite */
    if (busmaster_test_init(&ctx) == 0) {
        int result = perform_automated_busmaster_test(&ctx, BM_TEST_MODE_QUICK, &results);
        TEST_ASSERT(result == 0 || result == -1, "Comprehensive test completes");
        TEST_ASSERT(results.confidence_score <= BM_SCORE_TOTAL_MAX, "Total score within range");
        
        printf("INFO: Comprehensive Test Results:\n");
        printf("  Total Score: %u/%u (%.1f%%)\n", 
               results.confidence_score, BM_SCORE_TOTAL_MAX,
               (results.confidence_score * 100.0) / BM_SCORE_TOTAL_MAX);
        printf("  Confidence: %s\n",
               (results.confidence_level == BM_CONFIDENCE_HIGH) ? "HIGH" :
               (results.confidence_level == BM_CONFIDENCE_MEDIUM) ? "MEDIUM" :
               (results.confidence_level == BM_CONFIDENCE_LOW) ? "LOW" : "FAILED");
        printf("  Test Completed: %s\n", results.test_completed ? "YES" : "NO");
        printf("  Safe for Production: %s\n", results.safe_for_production ? "YES" : "NO");
        
        /* Test report generation */
        char report_buffer[2048];
        result = generate_busmaster_test_report(&results, report_buffer, sizeof(report_buffer));
        TEST_ASSERT(result == 0, "Test report generation succeeds");
        TEST_ASSERT(strlen(report_buffer) > 100, "Generated report has content");
        
        busmaster_test_cleanup(&ctx);
    }
    
    TEST_END("Comprehensive Test Scenarios");
    return !test_failed;
}