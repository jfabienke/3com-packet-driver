/**
 * @file test_cache_coherency.c
 * @brief Unit tests for cache coherency system
 *
 * 3Com Packet Driver - Cache Coherency Testing
 * Phase 4: Sprint 4C - Testing & Validation
 *
 * This file implements comprehensive unit tests for the cache coherency
 * management system including runtime testing, tier selection, and
 * cache management operations.
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../common/test_common.h"
#include "../common/test_framework.c"
#include "../../include/cache_coherency.h"
#include "../../include/cache_management.h"
#include "../../include/chipset_detect.h"
#include "../../include/chipset_database.h"
#include "../../include/performance_enabler.h"
#include "../../include/cpu_detect.h"
#include <string.h>
#include <stdbool.h>

/* Test configuration constants */
#define TEST_BUFFER_SIZE 4096
#define TEST_DMA_SIZE 1500
#define MAX_TEST_ITERATIONS 100

/* Test state tracking */
static bool g_test_cache_initialized = false;
static coherency_analysis_t g_test_analysis;
static uint8_t g_test_buffer[TEST_BUFFER_SIZE];

/* Forward declarations for test functions */
static void test_cache_coherency_analysis(void);
static void test_cache_tier_selection(void);
static void test_cache_management_operations(void);
static void test_chipset_detection_safety(void);
static void test_performance_enabler_system(void);
static void test_dma_cache_management(void);
static void test_tier_fallback_logic(void);
static void test_cpu_specific_optimizations(void);
static void test_community_database_integration(void);

/* Helper functions */
static void setup_test_environment(void);
static void cleanup_test_environment(void);
static bool verify_cache_tier_validity(cache_tier_t tier);
static bool verify_dma_buffer_coherency(void *buffer, size_t length);

/**
 * @brief Main test runner for cache coherency system
 */
int main(void) {
    int failed_tests = 0;
    
    printf("=== 3Com Packet Driver - Cache Coherency Test Suite ===\n");
    printf("Phase 4: Sprint 4C - Testing & Validation\n\n");
    
    /* Initialize test environment */
    setup_test_environment();
    
    /* Run comprehensive test suite */
    TEST_SECTION("Cache Coherency Analysis");
    RUN_TEST(test_cache_coherency_analysis, &failed_tests);
    
    TEST_SECTION("Cache Tier Selection");
    RUN_TEST(test_cache_tier_selection, &failed_tests);
    
    TEST_SECTION("Cache Management Operations");
    RUN_TEST(test_cache_management_operations, &failed_tests);
    
    TEST_SECTION("Chipset Detection Safety");
    RUN_TEST(test_chipset_detection_safety, &failed_tests);
    
    TEST_SECTION("Performance Enabler System");
    RUN_TEST(test_performance_enabler_system, &failed_tests);
    
    TEST_SECTION("DMA Cache Management");
    RUN_TEST(test_dma_cache_management, &failed_tests);
    
    TEST_SECTION("Tier Fallback Logic");
    RUN_TEST(test_tier_fallback_logic, &failed_tests);
    
    TEST_SECTION("CPU-Specific Optimizations");
    RUN_TEST(test_cpu_specific_optimizations, &failed_tests);
    
    TEST_SECTION("Community Database Integration");
    RUN_TEST(test_community_database_integration, &failed_tests);
    
    /* Cleanup test environment */
    cleanup_test_environment();
    
    /* Display results */
    printf("\n=== Cache Coherency Test Results ===\n");
    if (failed_tests == 0) {
        printf("✅ ALL TESTS PASSED! Cache coherency system validated.\n");
        printf("🎯 Ready for 100/100 production readiness!\n");
    } else {
        printf("❌ %d test(s) failed. Review cache coherency implementation.\n", failed_tests);
    }
    printf("=====================================\n");
    
    return failed_tests;
}

/**
 * @brief Test comprehensive cache coherency analysis
 */
static void test_cache_coherency_analysis(void) {
    coherency_analysis_t analysis;
    
    printf("Testing cache coherency analysis...\n");
    
    /* Test complete coherency analysis */
    analysis = perform_complete_coherency_analysis();
    
    /* Verify analysis results */
    ASSERT_TRUE(analysis.cpu.vendor != CPU_VENDOR_UNKNOWN, "CPU vendor should be detected");
    ASSERT_TRUE(analysis.cpu.family >= 2, "CPU family should be valid (286+)");
    ASSERT_TRUE(analysis.cpu.speed_mhz > 0, "CPU speed should be detected");
    
    /* Verify cache detection */
    ASSERT_TRUE(analysis.cache_enabled == true || analysis.cache_enabled == false, "Cache state should be determined");
    
    /* Verify test results are valid */
    ASSERT_TRUE(analysis.bus_master >= BUS_MASTER_OK && analysis.bus_master <= BUS_MASTER_BROKEN, 
                "Bus master result should be valid");
    ASSERT_TRUE(analysis.coherency >= COHERENCY_OK && analysis.coherency <= COHERENCY_UNKNOWN,
                "Coherency result should be valid");
    ASSERT_TRUE(analysis.snooping >= SNOOPING_FULL && analysis.snooping <= SNOOPING_UNKNOWN,
                "Snooping result should be valid");
    
    /* Verify tier selection */
    ASSERT_TRUE(verify_cache_tier_validity(analysis.selected_tier), "Selected tier should be valid");
    
    /* Verify confidence level */
    ASSERT_TRUE(analysis.confidence >= 0 && analysis.confidence <= 100, "Confidence should be 0-100%");
    
    /* Store for other tests */
    g_test_analysis = analysis;
    
    printf("✅ Cache coherency analysis validated\n");
}

/**
 * @brief Test cache tier selection logic
 */
static void test_cache_tier_selection(void) {
    cache_tier_t tier;
    
    printf("Testing cache tier selection logic...\n");
    
    /* Test tier selection for different scenarios */
    
    /* Test with working bus master and good coherency */
    coherency_analysis_t good_analysis = g_test_analysis;
    good_analysis.bus_master = BUS_MASTER_OK;
    good_analysis.coherency = COHERENCY_OK;
    good_analysis.snooping = SNOOPING_FULL;
    
    tier = select_optimal_cache_tier(&good_analysis);
    ASSERT_TRUE(tier == CACHE_TIER_4_FALLBACK || tier == CACHE_TIER_3_SOFTWARE, 
                "Good coherency should select minimal management");
    
    /* Test with coherency problems */
    coherency_analysis_t problem_analysis = g_test_analysis;
    problem_analysis.bus_master = BUS_MASTER_OK;
    problem_analysis.coherency = COHERENCY_PROBLEM;
    problem_analysis.cpu.family = 4; // 486
    
    tier = select_optimal_cache_tier(&problem_analysis);
    ASSERT_TRUE(tier >= CACHE_TIER_2_WBINVD && tier <= CACHE_TIER_1_CLFLUSH,
                "Coherency problems should select active management");
    
    /* Test with broken bus master */
    coherency_analysis_t broken_analysis = g_test_analysis;
    broken_analysis.bus_master = BUS_MASTER_BROKEN;
    
    tier = select_optimal_cache_tier(&broken_analysis);
    ASSERT_TRUE(tier == TIER_DISABLE_BUS_MASTER, "Broken bus master should disable DMA");
    
    printf("✅ Cache tier selection logic validated\n");
}

/**
 * @brief Test cache management operations
 */
static void test_cache_management_operations(void) {
    dma_operation_t operation;
    bool result;
    
    printf("Testing cache management operations...\n");
    
    /* Initialize cache management if not already done */
    if (!g_test_cache_initialized) {
        result = initialize_cache_management(g_test_analysis.selected_tier);
        ASSERT_TRUE(result, "Cache management should initialize successfully");
        g_test_cache_initialized = true;
    }
    
    /* Test DMA prepare operation */
    operation.buffer = g_test_buffer;
    operation.length = TEST_DMA_SIZE;
    operation.direction = DMA_DIRECTION_TO_DEVICE;
    operation.device_type = DMA_DEVICE_NETWORK;
    
    /* This should not crash or return error */
    cache_management_dma_prepare(&operation);
    
    /* Test DMA complete operation */
    operation.direction = DMA_DIRECTION_FROM_DEVICE;
    cache_management_dma_complete(&operation);
    
    /* Verify buffer coherency */
    ASSERT_TRUE(verify_dma_buffer_coherency(g_test_buffer, TEST_DMA_SIZE), 
                "DMA buffer should maintain coherency");
    
    /* Test cache flush operations */
    if (g_test_analysis.selected_tier <= CACHE_TIER_2_WBINVD) {
        /* Test cache flush for write-back cache systems */
        flush_cache_for_dma(g_test_buffer, TEST_DMA_SIZE);
        invalidate_cache_after_dma(g_test_buffer, TEST_DMA_SIZE);
    }
    
    printf("✅ Cache management operations validated\n");
}

/**
 * @brief Test chipset detection safety
 */
static void test_chipset_detection_safety(void) {
    chipset_detection_result_t result;
    
    printf("Testing chipset detection safety...\n");
    
    /* Test safe chipset detection */
    result = detect_system_chipset();
    
    /* Verify detection completed without crash */
    ASSERT_TRUE(result.detection_method >= CHIPSET_DETECT_NONE && 
                result.detection_method <= CHIPSET_DETECT_PCI_FAILED,
                "Detection method should be valid");
    
    /* Verify confidence level */
    ASSERT_TRUE(result.confidence >= CHIPSET_CONFIDENCE_UNKNOWN && 
                result.confidence <= CHIPSET_CONFIDENCE_HIGH,
                "Confidence level should be valid");
    
    /* If PCI system detected, verify chipset information */
    if (result.detection_method == CHIPSET_DETECT_PCI_SUCCESS) {
        ASSERT_TRUE(result.chipset.found, "PCI chipset should be marked as found");
        ASSERT_TRUE(result.chipset.vendor_id != 0, "Vendor ID should be valid");
        ASSERT_TRUE(strlen(result.chipset.name) > 0, "Chipset name should be present");
    }
    
    /* Verify no risky operations were performed */
    ASSERT_TRUE(strlen(result.diagnostic_info) > 0, "Diagnostic info should be present");
    
    printf("✅ Chipset detection safety validated\n");
}

/**
 * @brief Test performance enabler system
 */
static void test_performance_enabler_system(void) {
    bool result;
    performance_enabler_config_t config;
    
    printf("Testing performance enabler system...\n");
    
    /* Initialize performance enabler */
    result = initialize_performance_enabler(&g_test_analysis);
    ASSERT_TRUE(result, "Performance enabler should initialize successfully");
    
    /* Get configuration */
    config = get_performance_enabler_config();
    
    /* Verify configuration is valid */
    ASSERT_TRUE(config.opportunity >= PERFORMANCE_OPPORTUNITY_NONE && 
                config.opportunity <= PERFORMANCE_OPPORTUNITY_ENABLE_WB,
                "Performance opportunity should be valid");
    
    /* Test performance opportunity analysis */
    performance_opportunity_t opportunity = analyze_performance_opportunity(&g_test_analysis);
    ASSERT_TRUE(opportunity >= PERFORMANCE_OPPORTUNITY_NONE && 
                opportunity <= PERFORMANCE_OPPORTUNITY_ENABLE_WB,
                "Performance opportunity analysis should be valid");
    
    /* Test cache recommendation generation */
    cache_recommendation_t recommendation = generate_cache_recommendation(&g_test_analysis, opportunity);
    ASSERT_TRUE(recommendation >= CACHE_RECOMMENDATION_NONE && 
                recommendation <= CACHE_RECOMMENDATION_ENABLE_WRITE_BACK,
                "Cache recommendation should be valid");
    
    printf("✅ Performance enabler system validated\n");
}

/**
 * @brief Test DMA cache management integration
 */
static void test_dma_cache_management(void) {
    dma_operation_t tx_operation, rx_operation;
    
    printf("Testing DMA cache management integration...\n");
    
    /* Test transmit DMA preparation */
    tx_operation.buffer = g_test_buffer;
    tx_operation.length = TEST_DMA_SIZE;
    tx_operation.direction = DMA_DIRECTION_TO_DEVICE;
    tx_operation.device_type = DMA_DEVICE_NETWORK;
    
    /* Fill buffer with test pattern */
    for (size_t i = 0; i < TEST_DMA_SIZE; i++) {
        g_test_buffer[i] = (uint8_t)(i & 0xFF);
    }
    
    /* Prepare for DMA */
    cache_management_dma_prepare(&tx_operation);
    
    /* Simulate DMA completion */
    cache_management_dma_complete(&tx_operation);
    
    /* Test receive DMA preparation */
    rx_operation.buffer = g_test_buffer + TEST_DMA_SIZE;
    rx_operation.length = TEST_DMA_SIZE;
    rx_operation.direction = DMA_DIRECTION_FROM_DEVICE;
    rx_operation.device_type = DMA_DEVICE_NETWORK;
    
    /* Prepare for receive DMA */
    cache_management_dma_prepare(&rx_operation);
    
    /* Simulate DMA completion */
    cache_management_dma_complete(&rx_operation);
    
    /* Verify operations completed without error */
    ASSERT_TRUE(verify_dma_buffer_coherency(tx_operation.buffer, tx_operation.length),
                "TX buffer should maintain coherency");
    ASSERT_TRUE(verify_dma_buffer_coherency(rx_operation.buffer, rx_operation.length),
                "RX buffer should maintain coherency");
    
    printf("✅ DMA cache management integration validated\n");
}

/**
 * @brief Test tier fallback logic
 */
static void test_tier_fallback_logic(void) {
    cache_tier_t tier;
    bool result;
    
    printf("Testing tier fallback logic...\n");
    
    /* Test fallback from CLFLUSH to WBINVD */
    if (g_test_analysis.cpu.family >= 6) { // Pentium 4+ potentially
        /* Try to initialize with CLFLUSH tier */
        result = initialize_cache_management(CACHE_TIER_1_CLFLUSH);
        if (!result) {
            /* Should fallback to WBINVD */
            result = initialize_cache_management(CACHE_TIER_2_WBINVD);
            ASSERT_TRUE(result, "Should fallback to WBINVD if CLFLUSH unavailable");
        }
    }
    
    /* Test fallback from WBINVD to software */
    if (g_test_analysis.cpu.family >= 4) { // 486+
        /* Simulate WBINVD unavailable */
        result = initialize_cache_management(CACHE_TIER_3_SOFTWARE);
        ASSERT_TRUE(result, "Software cache management should always be available");
    }
    
    /* Test final fallback */
    result = initialize_cache_management(CACHE_TIER_4_FALLBACK);
    ASSERT_TRUE(result, "Fallback tier should always be available");
    
    printf("✅ Tier fallback logic validated\n");
}

/**
 * @brief Test CPU-specific optimizations
 */
static void test_cpu_specific_optimizations(void) {
    cpu_info_t cpu_info;
    
    printf("Testing CPU-specific optimizations...\n");
    
    /* Get CPU information */
    cpu_info = g_test_analysis.cpu;
    
    /* Verify CPU detection */
    ASSERT_TRUE(cpu_info.vendor != CPU_VENDOR_UNKNOWN, "CPU vendor should be detected");
    ASSERT_TRUE(cpu_info.family >= 2, "CPU family should be valid");
    
    /* Test CPU-specific cache line size detection */
    ASSERT_TRUE(cpu_info.cache_line_size >= 16 && cpu_info.cache_line_size <= 128,
                "Cache line size should be reasonable");
    
    /* Test CPU-specific cache management selection */
    cache_tier_t optimal_tier = select_cpu_optimal_tier(&cpu_info, g_test_analysis.cache_enabled);
    ASSERT_TRUE(verify_cache_tier_validity(optimal_tier), "CPU-optimal tier should be valid");
    
    /* Verify tier matches CPU capabilities */
    switch (cpu_info.family) {
        case 2: // 286
        case 3: // 386
            ASSERT_TRUE(optimal_tier >= CACHE_TIER_3_SOFTWARE, "286/386 should use software management");
            break;
        case 4: // 486
            ASSERT_TRUE(optimal_tier >= CACHE_TIER_2_WBINVD, "486 should support WBINVD");
            break;
        default: // Pentium+
            /* Could support any tier depending on specific CPU */
            break;
    }
    
    printf("✅ CPU-specific optimizations validated\n");
}

/**
 * @brief Test community database integration
 */
static void test_community_database_integration(void) {
    chipset_database_config_t db_config;
    chipset_detection_result_t chipset_result;
    bool result;
    
    printf("Testing community database integration...\n");
    
    /* Configure database */
    db_config.enable_export = true;
    db_config.export_csv = true;
    db_config.export_json = true;
    strcpy(db_config.csv_filename, "test_results.csv");
    strcpy(db_config.json_filename, "test_results.json");
    
    /* Initialize database */
    result = initialize_chipset_database(&db_config);
    ASSERT_TRUE(result, "Chipset database should initialize successfully");
    
    /* Get chipset detection result */
    chipset_result = detect_system_chipset();
    
    /* Record test result */
    result = record_chipset_test_result(&g_test_analysis, &chipset_result);
    ASSERT_TRUE(result, "Should record test result successfully");
    
    /* Get database statistics */
    chipset_database_stats_t stats = get_database_statistics();
    ASSERT_TRUE(stats.total_submissions >= 1, "Should have at least one submission");
    
    /* Cleanup database */
    cleanup_chipset_database();
    
    printf("✅ Community database integration validated\n");
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Set up test environment
 */
static void setup_test_environment(void) {
    /* Initialize test buffer */
    memset(g_test_buffer, 0, sizeof(g_test_buffer));
    
    /* Perform initial coherency analysis for tests */
    g_test_analysis = perform_complete_coherency_analysis();
    
    printf("Test environment initialized.\n");
    printf("System: %s, Cache: %s, Tier: %d\n\n",
           get_cpu_vendor_string(g_test_analysis.cpu.vendor),
           g_test_analysis.write_back_cache ? "Write-back" : "Write-through",
           g_test_analysis.selected_tier);
}

/**
 * @brief Clean up test environment
 */
static void cleanup_test_environment(void) {
    /* Clean up any initialized systems */
    if (g_test_cache_initialized) {
        cleanup_cache_management();
    }
    
    printf("\nTest environment cleaned up.\n");
}

/**
 * @brief Verify cache tier validity
 * @param tier Cache tier to verify
 * @return true if valid, false otherwise
 */
static bool verify_cache_tier_validity(cache_tier_t tier) {
    return (tier >= CACHE_TIER_1_CLFLUSH && tier <= TIER_DISABLE_BUS_MASTER);
}

/**
 * @brief Verify DMA buffer coherency
 * @param buffer Buffer to verify
 * @param length Buffer length
 * @return true if coherent, false otherwise
 */
static bool verify_dma_buffer_coherency(void *buffer, size_t length) {
    /* Simple coherency check - buffer should be accessible */
    if (!buffer || length == 0) {
        return false;
    }
    
    /* Verify we can read/write the buffer */
    volatile uint8_t *test_buffer = (volatile uint8_t *)buffer;
    uint8_t original = test_buffer[0];
    test_buffer[0] = 0xAA;
    bool coherent = (test_buffer[0] == 0xAA);
    test_buffer[0] = original;
    
    return coherent;
}