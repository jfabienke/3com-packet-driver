/**
 * @file test_hardware.h
 * @brief Hardware abstraction layer and multi-NIC management test interface
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 * 
 * This header provides the interface for comprehensive hardware abstraction 
 * layer testing including multi-NIC scenarios, error recovery, and failover.
 */

#ifndef _HARDWARE_TEST_H_
#define _HARDWARE_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "../../include/test_framework.h"
#include "../../include/hardware.h"

/* Hardware test result codes */
#define HW_TEST_SUCCESS         0       /* All tests passed */
#define HW_TEST_FAILURE         -1      /* Some tests failed */
#define HW_TEST_SETUP_ERROR     -2      /* Test setup failed */
#define HW_TEST_INSUFFICIENT    -3      /* Insufficient hardware for test */

/* Hardware test categories */
typedef enum {
    HW_TEST_VTABLE = 0,                 /* Vtable operations */
    HW_TEST_DETECTION,                  /* Multi-NIC detection */
    HW_TEST_ENUMERATION,                /* Multi-NIC enumeration */
    HW_TEST_ERROR_RECOVERY,             /* Error recovery mechanisms */
    HW_TEST_FAILOVER,                   /* NIC failover */
    HW_TEST_RESOURCE_ALLOC,             /* Resource allocation */
    HW_TEST_CAPABILITIES,               /* Capability detection */
    HW_TEST_CONTENTION,                 /* Resource contention */
    HW_TEST_CONCURRENT,                 /* Concurrent operations */
    HW_TEST_LOAD_BALANCE,               /* Load balancing */
    HW_TEST_FAILURE_INJECTION,          /* Hardware failure injection */
    HW_TEST_MAX
} hw_test_category_t;

/* Hardware test configuration */
typedef struct {
    bool enable_vtable_tests;           /* Enable vtable operation tests */
    bool enable_multi_nic_tests;        /* Enable multi-NIC tests */
    bool enable_error_recovery_tests;   /* Enable error recovery tests */
    bool enable_failover_tests;         /* Enable failover tests */
    bool enable_resource_tests;         /* Enable resource allocation tests */
    bool enable_capability_tests;       /* Enable capability detection tests */
    bool enable_stress_tests;           /* Enable stress/contention tests */
    bool enable_failure_injection;     /* Enable failure injection tests */
    int max_test_nics;                  /* Maximum NICs to create for testing */
    uint32_t test_timeout_ms;           /* Test timeout in milliseconds */
    bool verbose_output;                /* Enable verbose test output */
} hw_test_config_t;

/* Multi-NIC simulation configuration */
typedef struct {
    int nic_count;                      /* Number of NICs to simulate */
    nic_type_t nic_types[8];            /* Types of NICs to create */
    uint16_t io_bases[8];               /* I/O base addresses */
    uint8_t irqs[8];                    /* IRQ assignments */
    bool link_status[8];                /* Initial link status */
    uint16_t link_speeds[8];            /* Link speeds (10/100) */
    uint8_t mac_addresses[8][6];        /* MAC addresses */
} multi_nic_sim_config_t;

/**
 * @brief Initialize hardware test configuration with defaults
 * @param config Configuration structure to initialize
 */
static inline void hw_test_config_init_default(hw_test_config_t *config) {
    if (config) {
        config->enable_vtable_tests = true;
        config->enable_multi_nic_tests = true;
        config->enable_error_recovery_tests = true;
        config->enable_failover_tests = true;
        config->enable_resource_tests = true;
        config->enable_capability_tests = true;
        config->enable_stress_tests = true;
        config->enable_failure_injection = true;
        config->max_test_nics = 4;
        config->test_timeout_ms = 5000;
        config->verbose_output = false;
    }
}

/**
 * @brief Initialize minimal hardware test configuration
 * @param config Configuration structure to initialize
 */
static inline void hw_test_config_init_minimal(hw_test_config_t *config) {
    if (config) {
        config->enable_vtable_tests = true;
        config->enable_multi_nic_tests = true;
        config->enable_error_recovery_tests = false;
        config->enable_failover_tests = false;
        config->enable_resource_tests = true;
        config->enable_capability_tests = true;
        config->enable_stress_tests = false;
        config->enable_failure_injection = false;
        config->max_test_nics = 2;
        config->test_timeout_ms = 2000;
        config->verbose_output = false;
    }
}

/**
 * @brief Initialize multi-NIC simulation configuration with defaults
 * @param config Simulation configuration to initialize
 * @param nic_count Number of NICs to simulate
 */
static inline void multi_nic_sim_config_init_default(multi_nic_sim_config_t *config, int nic_count) {
    if (config && nic_count > 0 && nic_count <= 8) {
        config->nic_count = nic_count;
        
        for (int i = 0; i < nic_count; i++) {
            /* Alternate between 3C509B and 3C515 */
            config->nic_types[i] = (i % 2 == 0) ? NIC_TYPE_3C509B : NIC_TYPE_3C515_TX;
            config->io_bases[i] = 0x200 + (i * 0x20);
            config->irqs[i] = 10 + i;
            config->link_status[i] = true;
            config->link_speeds[i] = (config->nic_types[i] == NIC_TYPE_3C515_TX) ? 100 : 10;
            
            /* Generate unique MAC addresses */
            config->mac_addresses[i][0] = 0x00;
            config->mac_addresses[i][1] = 0x60;
            config->mac_addresses[i][2] = 0x8C;
            config->mac_addresses[i][3] = 0x12 + i;
            config->mac_addresses[i][4] = 0x34 + i;
            config->mac_addresses[i][5] = 0x56 + i;
        }
    }
}

/* Primary hardware test functions */

/**
 * @brief Run all hardware abstraction layer tests
 * @return HW_TEST_SUCCESS on success, negative error code on failure
 */
int run_hardware_tests(void);

/**
 * @brief Run hardware tests with custom configuration
 * @param config Test configuration
 * @return HW_TEST_SUCCESS on success, negative error code on failure
 */
int run_hardware_tests_with_config(const hw_test_config_t *config);

/**
 * @brief Run specific category of hardware tests
 * @param category Test category to run
 * @return HW_TEST_SUCCESS on success, negative error code on failure
 */
int run_hardware_test_category(hw_test_category_t category);

/* Multi-NIC simulation functions */

/**
 * @brief Create multi-NIC simulation environment
 * @param config Simulation configuration
 * @return Number of NICs created, negative on error
 */
int hw_test_create_multi_nic_simulation(const multi_nic_sim_config_t *config);

/**
 * @brief Cleanup multi-NIC simulation environment
 */
void hw_test_cleanup_multi_nic_simulation(void);

/**
 * @brief Get number of simulated NICs
 * @return Number of simulated NICs
 */
int hw_test_get_simulated_nic_count(void);

/**
 * @brief Get simulated NIC by index
 * @param index NIC index
 * @return Pointer to NIC info, NULL on error
 */
nic_info_t* hw_test_get_simulated_nic(int index);

/* Hardware failure simulation functions */

/**
 * @brief Simulate hardware failure on specific NIC
 * @param nic_index NIC index
 * @param failure_type Type of failure to simulate
 * @return 0 on success, negative on error
 */
int hw_test_inject_hardware_failure(int nic_index, int failure_type);

/**
 * @brief Clear hardware failure simulation
 * @param nic_index NIC index
 * @return 0 on success, negative on error
 */
int hw_test_clear_hardware_failure(int nic_index);

/**
 * @brief Test hardware failure recovery
 * @param nic_index NIC index
 * @param failure_type Failure type to test
 * @return 0 on success, negative on error
 */
int hw_test_validate_failure_recovery(int nic_index, int failure_type);

/* Hardware test validation functions */

/**
 * @brief Validate NIC vtable operations
 * @param nic_type NIC type to validate
 * @return test_result_t indicating validation result
 */
test_result_t hw_test_validate_nic_vtable(nic_type_t nic_type);

/**
 * @brief Validate multi-NIC enumeration
 * @param expected_count Expected number of NICs
 * @return test_result_t indicating validation result
 */
test_result_t hw_test_validate_multi_nic_enumeration(int expected_count);

/**
 * @brief Validate NIC capabilities
 * @param nic Pointer to NIC info structure
 * @return test_result_t indicating validation result
 */
test_result_t hw_test_validate_nic_capabilities(const nic_info_t *nic);

/**
 * @brief Validate failover functionality
 * @param primary_nic Primary NIC index
 * @param backup_nic Backup NIC index
 * @return test_result_t indicating validation result
 */
test_result_t hw_test_validate_nic_failover(int primary_nic, int backup_nic);

/* Hardware test reporting functions */

/**
 * @brief Generate comprehensive hardware test report
 * @param results Array of test results
 * @param result_count Number of test results
 */
void hw_test_generate_report(const test_result_entry_t *results, int result_count);

/**
 * @brief Get hardware test statistics
 * @param stats Pointer to store statistics
 * @return 0 on success, negative on error
 */
int hw_test_get_statistics(test_framework_stats_t *stats);

/**
 * @brief Print hardware test summary
 * @param passed Number of passed tests
 * @param failed Number of failed tests
 * @param skipped Number of skipped tests
 */
void hw_test_print_summary(int passed, int failed, int skipped);

/* Utility macros for hardware testing */

#define HW_TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            LOG_ERROR("HW_TEST ASSERTION FAILED: %s", message); \
            return TEST_RESULT_FAIL; \
        } \
    } while(0)

#define HW_TEST_EXPECT(condition, message) \
    do { \
        if (!(condition)) { \
            LOG_WARNING("HW_TEST EXPECTATION FAILED: %s", message); \
        } \
    } while(0)

#define HW_TEST_LOG_START(test_name) \
    LOG_INFO("=== Starting Hardware Test: %s ===", test_name)

#define HW_TEST_LOG_END(test_name, result) \
    LOG_INFO("=== Hardware Test %s: %s ===", test_name, \
             (result == TEST_RESULT_PASS) ? "PASSED" : "FAILED")

/* Hardware test constants */
#define HW_TEST_DEFAULT_TIMEOUT_MS  5000    /* Default test timeout */
#define HW_TEST_MAX_SIMULATED_NICS  8       /* Maximum simulated NICs */
#define HW_TEST_PACKET_SIZE         1518    /* Default test packet size */
#define HW_TEST_STRESS_CYCLES       100     /* Default stress test cycles */

#ifdef __cplusplus
}
#endif

#endif /* _HARDWARE_TEST_H_ */