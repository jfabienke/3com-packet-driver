/**
 * @file nic_driver_tests.h
 * @brief Header file for 3C509B and 3C515-TX network card driver tests
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This header provides the interface for comprehensive testing of both
 * network card drivers, including function prototypes, test configuration
 * structures, and result definitions.
 */

#ifndef _NIC_DRIVER_TESTS_H_
#define _NIC_DRIVER_TESTS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "testfw.h"
#include "hwmock.h"

/* Test result structure for detailed reporting */
typedef struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
    char last_error[256];
} test_results_t;

/* 3C509B Driver Test Functions */

/**
 * @brief Run comprehensive 3C509B driver tests
 * @return 0 on success, negative on failure
 */
int run_3c509b_comprehensive_tests(void);

/**
 * @brief Run specific 3C509B test by name
 * @param test_name Name of the test to run
 * @return Test result
 */
test_result_t run_3c509b_test_by_name(const char *test_name);

/* Available 3C509B test names:
 * - "window_selection"    : Test window selection mechanism
 * - "eeprom_read"        : Test EEPROM read operations
 * - "mac_address"        : Test MAC address reading from EEPROM
 * - "media_setup"        : Test media auto-detection and setup
 * - "rx_filter"          : Test receive filter configuration
 * - "packet_tx"          : Test packet transmission
 * - "packet_rx"          : Test packet reception
 * - "error_handling"     : Test error handling and edge cases
 * - "self_test"          : Test self-test functionality
 * - "interrupts"         : Test interrupt handling
 * - "stress"             : Test stress conditions
 */

/* 3C515-TX Driver Test Functions */

/**
 * @brief Run comprehensive 3C515-TX driver tests
 * @return 0 on success, negative on failure
 */
int run_3c515_comprehensive_tests(void);

/**
 * @brief Run specific 3C515-TX test by name
 * @param test_name Name of the test to run
 * @return Test result
 */
test_result_t run_3c515_test_by_name(const char *test_name);

/* Available 3C515-TX test names:
 * - "descriptor_init"    : Test descriptor ring initialization
 * - "dma_setup"          : Test DMA engine setup and configuration
 * - "dma_tx"             : Test DMA transmission
 * - "dma_rx"             : Test DMA reception
 * - "ring_management"    : Test descriptor ring management
 * - "pci_config"         : Test PCI configuration
 * - "performance"        : Test performance optimization paths
 * - "error_recovery"     : Test error recovery mechanisms
 * - "bus_mastering"      : Test bus mastering DMA operations
 * - "stress"             : Test stress conditions
 */

/* Utility Test Functions */

/**
 * @brief Quick validation test for basic functionality
 * @return 0 on success, negative on failure
 */
int quick_validation_test(void);

/**
 * @brief Validate that both drivers can be initialized
 * @return 0 on success, negative on failure
 */
int driver_initialization_test(void);

/**
 * @brief Test hardware mock framework functionality
 * @return 0 on success, negative on failure
 */
int hardware_mock_validation_test(void);

/* Test Configuration and Reporting */

/**
 * @brief Get test statistics for specific driver
 * @param driver_name "3c509b" or "3c515"
 * @param stats Output statistics structure
 * @return 0 on success, negative on failure
 */
int get_driver_test_statistics(const char *driver_name, test_results_t *stats);

/**
 * @brief Print comprehensive test report
 * @param overall_result Overall test result
 * @param detailed_output Whether to include detailed output
 */
void print_comprehensive_test_report(int overall_result, bool detailed_output);

/**
 * @brief Validate test environment setup
 * @return 0 if environment is ready, negative otherwise
 */
int validate_test_environment(void);

/* Test Macros for Convenience */

#define RUN_3C509B_TEST(test_name) \
    do { \
        LOG_INFO("Running 3C509B test: %s", #test_name); \
        test_result_t result = run_3c509b_test_by_name(#test_name); \
        if (result != TEST_RESULT_PASS) { \
            LOG_ERROR("3C509B test failed: %s", #test_name); \
            return -1; \
        } \
    } while(0)

#define RUN_3C515_TEST(test_name) \
    do { \
        LOG_INFO("Running 3C515-TX test: %s", #test_name); \
        test_result_t result = run_3c515_test_by_name(#test_name); \
        if (result != TEST_RESULT_PASS) { \
            LOG_ERROR("3C515-TX test failed: %s", #test_name); \
            return -1; \
        } \
    } while(0)

#define REQUIRE_TEST_ENVIRONMENT() \
    do { \
        if (validate_test_environment() != 0) { \
            LOG_ERROR("Test environment validation failed"); \
            return TEST_RESULT_ERROR; \
        } \
    } while(0)

/* Test Suite Categories */
typedef enum {
    TEST_SUITE_3C509B = 0,
    TEST_SUITE_3C515,
    TEST_SUITE_INTEGRATION,
    TEST_SUITE_STRESS,
    TEST_SUITE_PERFORMANCE,
    TEST_SUITE_ALL
} test_suite_category_t;

/**
 * @brief Run tests by category
 * @param category Test suite category to run
 * @param verbose Enable verbose output
 * @return 0 on success, negative on failure
 */
int run_test_suite_by_category(test_suite_category_t category, bool verbose);

/* Performance Test Functions */

/**
 * @brief Run performance benchmark tests
 * @param duration_ms Test duration in milliseconds
 * @return 0 on success, negative on failure
 */
int run_performance_benchmarks(uint32_t duration_ms);

/**
 * @brief Test packet throughput performance
 * @param driver_type "3c509b" or "3c515"
 * @param packet_count Number of packets to process
 * @return Packets per second, or negative on error
 */
int test_packet_throughput(const char *driver_type, int packet_count);

/**
 * @brief Test interrupt latency
 * @param driver_type "3c509b" or "3c515"
 * @param interrupt_count Number of interrupts to test
 * @return Average latency in microseconds, or negative on error
 */
int test_interrupt_latency(const char *driver_type, int interrupt_count);

/* Stress Test Functions */

/**
 * @brief Run memory stress test
 * @param duration_ms Test duration in milliseconds
 * @return 0 on success, negative on failure
 */
int run_memory_stress_test(uint32_t duration_ms);

/**
 * @brief Run I/O operation stress test
 * @param operation_count Number of operations to perform
 * @return 0 on success, negative on failure
 */
int run_io_stress_test(int operation_count);

/**
 * @brief Run concurrent operation stress test
 * @param concurrent_operations Number of concurrent operations
 * @return 0 on success, negative on failure
 */
int run_concurrent_stress_test(int concurrent_operations);

/* Integration Test Functions */

/**
 * @brief Test integration between 3C509B and packet API
 * @return 0 on success, negative on failure
 */
int test_3c509b_packet_api_integration(void);

/**
 * @brief Test integration between 3C515-TX and packet API
 * @return 0 on success, negative on failure
 */
int test_3c515_packet_api_integration(void);

/**
 * @brief Test memory management integration
 * @return 0 on success, negative on failure
 */
int test_memory_management_integration(void);

/**
 * @brief Test logging system integration
 * @return 0 on success, negative on failure
 */
int test_logging_integration(void);

/* Test Data and Scenarios */

/* Standard test packet definitions */
extern const uint8_t test_packet_small[];
extern const size_t test_packet_small_size;
extern const uint8_t test_packet_large[];
extern const size_t test_packet_large_size;
extern const uint8_t test_packet_broadcast[];
extern const size_t test_packet_broadcast_size;

/* Test MAC addresses */
extern const uint8_t test_mac_3c509b[];
extern const uint8_t test_mac_3c515[];
extern const uint8_t test_mac_broadcast[];

/* Test EEPROM data */
extern const uint16_t test_eeprom_3c509b[];
extern const uint16_t test_eeprom_3c515[];

/* Error injection scenarios */
typedef struct {
    const char *name;
    mock_error_type_t error_type;
    uint32_t trigger_count;
    const char *description;
} error_scenario_t;

extern const error_scenario_t error_scenarios[];
extern const int error_scenario_count;

/* Performance test scenarios */
typedef struct {
    const char *name;
    int packet_count;
    size_t packet_size;
    uint32_t duration_ms;
    const char *description;
} performance_scenario_t;

extern const performance_scenario_t performance_scenarios[];
extern const int performance_scenario_count;

#ifdef __cplusplus
}
#endif

#endif /* _NIC_DRIVER_TESTS_H_ */