/**
 * @file runner_stress.c
 * @brief Stress Test Runner - Resource limits and system stability testing
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This test runner executes stress tests that push the system to its limits:
 * - Resource exhaustion testing (memory, descriptors, buffers)
 * - High load stability testing
 * - Multi-threaded stress scenarios
 * - Error injection and recovery testing
 * - Long-duration stability validation
 * - Memory leak detection under stress
 */

#include "../common/test_framework.h"
#include "../common/hardware_mock.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include "../../include/packet_ops.h"
#include <stdio.h>
#include <string.h>

/* External stress test functions */
extern int test_stress_resource_main(void);
extern int test_stress_stability_main(void);

/* Stress test configuration */
typedef struct {
    bool run_resource_stress_tests;
    bool run_stability_stress_tests;
    bool run_memory_stress_tests;
    bool run_network_stress_tests;
    bool run_error_injection_tests;
    bool run_long_duration_tests;
    bool verbose_output;
    bool stop_on_critical_failure;
    uint32_t stress_duration_ms;
    uint32_t max_memory_mb;
    uint32_t max_packet_rate_pps;
    uint32_t error_injection_rate_percent;
} stress_test_config_t;

/* Stress test statistics */
typedef struct {
    int total_stress_tests_run;
    int stress_tests_passed;
    int stress_tests_failed;
    int critical_failures;
    uint32_t total_duration_ms;
    uint32_t peak_memory_usage_mb;
    uint32_t total_packets_processed;
    uint64_t total_errors_injected;
    uint64_t successful_recoveries;
    const char *longest_test_name;
    uint32_t longest_test_duration_ms;
} stress_test_stats_t;

/* Stress test definition */
typedef struct {
    const char *name;
    const char *description;
    int (*stress_test_main)(void);
    bool *enabled_flag;
    bool is_critical;
    uint32_t expected_duration_ms;
    uint32_t max_allowed_failures;
} stress_test_t;

static stress_test_config_t g_stress_config = {
    .run_resource_stress_tests = true,
    .run_stability_stress_tests = true,
    .run_memory_stress_tests = true,
    .run_network_stress_tests = true,
    .run_error_injection_tests = true,
    .run_long_duration_tests = false, /* Disabled by default due to time */
    .verbose_output = false,
    .stop_on_critical_failure = true,
    .stress_duration_ms = 30000,      /* 30 seconds default */
    .max_memory_mb = 64,              /* 64 MB memory limit */
    .max_packet_rate_pps = 100000,    /* 100K PPS max rate */
    .error_injection_rate_percent = 5  /* 5% error injection rate */
};

static stress_test_stats_t g_stress_stats = {0};

/* Forward declarations for stress test functions */
static int test_resource_exhaustion_stress(void);
static int test_memory_pressure_stress(void);
static int test_network_load_stress(void);
static int test_error_injection_stress(void);
static int test_concurrent_operations_stress(void);
static int test_long_duration_stability_stress(void);

/**
 * @brief Parse command line arguments for stress test configuration
 */
static int parse_stress_test_arguments(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_stress_config.verbose_output = true;
        } else if (strcmp(argv[i], "--no-critical-stop") == 0) {
            g_stress_config.stop_on_critical_failure = false;
        } else if (strcmp(argv[i], "--resource-only") == 0) {
            g_stress_config.run_resource_stress_tests = true;
            g_stress_config.run_stability_stress_tests = false;
            g_stress_config.run_memory_stress_tests = false;
            g_stress_config.run_network_stress_tests = false;
            g_stress_config.run_error_injection_tests = false;
            g_stress_config.run_long_duration_tests = false;
        } else if (strcmp(argv[i], "--memory-only") == 0) {
            g_stress_config.run_resource_stress_tests = false;
            g_stress_config.run_stability_stress_tests = false;
            g_stress_config.run_memory_stress_tests = true;
            g_stress_config.run_network_stress_tests = false;
            g_stress_config.run_error_injection_tests = false;
            g_stress_config.run_long_duration_tests = false;
        } else if (strcmp(argv[i], "--long-duration") == 0) {
            g_stress_config.run_long_duration_tests = true;
            g_stress_config.stress_duration_ms = 300000; /* 5 minutes */
        } else if (strcmp(argv[i], "--duration") == 0) {
            if (i + 1 < argc) {
                g_stress_config.stress_duration_ms = atoi(argv[++i]) * 1000; /* Convert seconds to ms */
            } else {
                log_error("--duration requires a value in seconds");
                return -1;
            }
        } else if (strcmp(argv[i], "--memory-limit") == 0) {
            if (i + 1 < argc) {
                g_stress_config.max_memory_mb = atoi(argv[++i]);
            } else {
                log_error("--memory-limit requires a value in MB");
                return -1;
            }
        } else if (strcmp(argv[i], "--packet-rate") == 0) {
            if (i + 1 < argc) {
                g_stress_config.max_packet_rate_pps = atoi(argv[++i]);
            } else {
                log_error("--packet-rate requires a value in PPS");
                return -1;
            }
        } else if (strcmp(argv[i], "--error-rate") == 0) {
            if (i + 1 < argc) {
                g_stress_config.error_injection_rate_percent = atoi(argv[++i]);
                if (g_stress_config.error_injection_rate_percent > 100) {
                    g_stress_config.error_injection_rate_percent = 100;
                }
            } else {
                log_error("--error-rate requires a percentage value");
                return -1;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Stress Test Runner - 3Com Packet Driver\n");
            printf("Usage: %s [options]\n\n", argv[0]);
            printf("Options:\n");
            printf("  -v, --verbose            Enable verbose output\n");
            printf("  --no-critical-stop       Don't stop on critical failures\n");
            printf("  --resource-only          Run only resource stress tests\n");
            printf("  --memory-only            Run only memory stress tests\n");
            printf("  --long-duration          Enable long duration tests (5+ minutes)\n");
            printf("  --duration <seconds>     Set stress test duration\n");
            printf("  --memory-limit <MB>      Set memory limit for tests\n");
            printf("  --packet-rate <PPS>      Set maximum packet rate\n");
            printf("  --error-rate <percent>   Set error injection rate (0-100)\n");
            printf("  -h, --help               Show this help\n");
            printf("\nStress test categories:\n");
            printf("  Resource Exhaustion      - Memory, descriptors, buffer limits\n");
            printf("  Memory Pressure          - Memory allocation stress and leaks\n");
            printf("  Network Load            - High packet rate stress testing\n");
            printf("  Error Injection         - Fault injection and recovery\n");
            printf("  Concurrent Operations   - Multi-threaded stress scenarios\n");
            printf("  Long Duration           - Extended stability validation\n");
            return 1;
        }
    }
    
    return 0;
}

/**
 * @brief Initialize stress test environment
 */
static int initialize_stress_test_environment(void) {
    log_info("Initializing stress test environment");
    
    /* Initialize logging with appropriate level */
    int result = logging_init();
    if (result != 0) {
        printf("Failed to initialize logging system\n");
        return -1;
    }
    
    if (g_stress_config.verbose_output) {
        log_set_level(LOG_LEVEL_DEBUG);
    } else {
        log_set_level(LOG_LEVEL_INFO);
    }
    
    /* Initialize memory management with stress test limits */
    result = memory_init();
    if (result != 0) {
        log_error("Failed to initialize memory management");
        return -2;
    }
    
    /* Set memory limits for stress testing */
    memory_set_limit(g_stress_config.max_memory_mb * 1024 * 1024);
    
    /* Initialize hardware mock framework with stress testing features */
    result = mock_framework_init();
    if (result != 0) {
        log_error("Failed to initialize hardware mock framework");
        return -3;
    }
    
    /* Enable stress testing features */
    mock_enable_error_injection(true);
    mock_set_error_injection_rate(g_stress_config.error_injection_rate_percent);
    mock_enable_resource_limits(true);
    mock_enable_multi_nic_simulation(true);
    
    /* Initialize test framework with stress test configuration */
    test_config_t test_config;
    test_config_init_default(&test_config);
    test_config.test_hardware = true;
    test_config.test_memory = true;
    test_config.test_packet_ops = true;
    test_config.run_benchmarks = false;
    test_config.run_stress_tests = true;
    test_config.verbose_output = g_stress_config.verbose_output;
    test_config.stress_duration_ms = g_stress_config.stress_duration_ms;
    test_config.init_hardware = true;
    test_config.init_memory = true;
    test_config.init_diagnostics = true;
    
    result = test_framework_init(&test_config);
    if (result != 0) {
        log_error("Failed to initialize test framework");
        return -4;
    }
    
    log_info("Stress test environment initialized successfully");
    log_info("Stress duration: %d ms, Memory limit: %d MB, Error rate: %d%%", 
             g_stress_config.stress_duration_ms, g_stress_config.max_memory_mb,
             g_stress_config.error_injection_rate_percent);
    
    return 0;
}

/**
 * @brief Cleanup stress test environment
 */
static void cleanup_stress_test_environment(void) {
    log_info("Cleaning up stress test environment");
    
    /* Disable stress features */
    mock_enable_error_injection(false);
    mock_enable_resource_limits(false);
    
    /* Cleanup frameworks */
    test_framework_cleanup();
    mock_framework_cleanup();
    memory_cleanup();
    logging_cleanup();
    
    log_info("Stress test environment cleanup completed");
}

/**
 * @brief Test resource exhaustion scenarios
 */
static int test_resource_exhaustion_stress(void) {
    log_info("Testing resource exhaustion stress scenarios");
    
    int overall_result = 0;
    
    /* Test packet buffer exhaustion */
    log_info("Testing packet buffer exhaustion");
    
    const int MAX_BUFFERS = 1000;
    packet_buffer_t *buffers[MAX_BUFFERS];
    int allocated_buffers = 0;
    
    /* Allocate buffers until exhaustion */
    for (int i = 0; i < MAX_BUFFERS; i++) {
        buffers[i] = packet_buffer_alloc(1500);
        if (buffers[i]) {
            allocated_buffers++;
        } else {
            break; /* Resource exhaustion reached */
        }
    }
    
    log_info("Allocated %d packet buffers before exhaustion", allocated_buffers);
    
    if (allocated_buffers < 10) {
        log_error("Too few buffers allocated, system may have resource issues");
        overall_result = -1;
    }
    
    /* Test system stability under resource exhaustion */
    packet_buffer_t *test_buffer = packet_buffer_alloc(64);
    if (test_buffer) {
        log_warning("System allowed allocation during exhaustion");
        packet_buffer_free(test_buffer);
    }
    
    /* Test graceful recovery */
    for (int i = 0; i < allocated_buffers / 2; i++) {
        if (buffers[i]) {
            packet_buffer_free(buffers[i]);
            buffers[i] = NULL;
        }
    }
    
    /* Verify system recovers */
    test_buffer = packet_buffer_alloc(64);
    if (!test_buffer) {
        log_error("System failed to recover from resource exhaustion");
        overall_result = -1;
    } else {
        packet_buffer_free(test_buffer);
        log_info("System successfully recovered from resource exhaustion");
    }
    
    /* Cleanup remaining buffers */
    for (int i = 0; i < allocated_buffers; i++) {
        if (buffers[i]) {
            packet_buffer_free(buffers[i]);
        }
    }
    
    /* Test descriptor exhaustion (simulate hardware descriptor rings) */
    log_info("Testing hardware descriptor exhaustion");
    
    mock_set_descriptor_limit(64); /* Limit to 64 descriptors */
    
    int descriptors_allocated = 0;
    for (int i = 0; i < 100; i++) {
        if (mock_allocate_descriptor() == 0) {
            descriptors_allocated++;
        } else {
            break;
        }
    }
    
    log_info("Allocated %d descriptors before exhaustion", descriptors_allocated);
    
    if (descriptors_allocated < 32) {
        log_error("Descriptor allocation lower than expected");
        overall_result = -1;
    }
    
    /* Test system behavior under descriptor exhaustion */
    packet_buffer_t *desc_test_packet = packet_buffer_alloc(64);
    if (desc_test_packet) {
        int tx_result = packet_transmit(desc_test_packet, 0);
        if (tx_result == 0) {
            log_warning("Packet transmission succeeded despite descriptor exhaustion");
        } else {
            log_info("Packet transmission correctly failed due to descriptor exhaustion");
        }
        packet_buffer_free(desc_test_packet);
    }
    
    /* Cleanup descriptors */
    mock_free_all_descriptors();
    mock_set_descriptor_limit(0); /* Remove limit */
    
    if (overall_result == 0) {
        log_info("Resource exhaustion stress test PASSED");
    } else {
        log_error("Resource exhaustion stress test FAILED");
    }
    
    return overall_result;
}

/**
 * @brief Test memory pressure stress scenarios
 */
static int test_memory_pressure_stress(void) {
    log_info("Testing memory pressure stress scenarios");
    
    const mem_stats_t *initial_stats = memory_get_stats();
    int overall_result = 0;
    
    /* Test progressive memory allocation */
    log_info("Testing progressive memory allocation pressure");
    
    const int CHUNK_SIZE = 1024 * 1024; /* 1MB chunks */
    const int MAX_CHUNKS = g_stress_config.max_memory_mb;
    void *memory_chunks[MAX_CHUNKS];
    int allocated_chunks = 0;
    
    for (int i = 0; i < MAX_CHUNKS; i++) {
        memory_chunks[i] = malloc(CHUNK_SIZE);
        if (memory_chunks[i]) {
            /* Touch the memory to ensure it's actually allocated */
            memset(memory_chunks[i], 0x55, CHUNK_SIZE);
            allocated_chunks++;
            
            /* Update peak memory usage */
            const mem_stats_t *current_stats = memory_get_stats();
            uint32_t current_usage_mb = current_stats->used_memory / (1024 * 1024);
            if (current_usage_mb > g_stress_stats.peak_memory_usage_mb) {
                g_stress_stats.peak_memory_usage_mb = current_usage_mb;
            }
        } else {
            log_info("Memory allocation failed at chunk %d (%d MB)", i, i);
            break;
        }
        
        /* Test system responsiveness under memory pressure */
        if (i % 10 == 0) {
            packet_buffer_t *responsiveness_test = packet_buffer_alloc(64);
            if (responsiveness_test) {
                packet_buffer_free(responsiveness_test);
            } else {
                log_warning("System becoming unresponsive under memory pressure at %d MB", i);
            }
        }
    }
    
    log_info("Allocated %d MB before memory pressure limits", allocated_chunks);
    
    /* Test memory fragmentation resistance */
    log_info("Testing memory fragmentation resistance");
    
    /* Free every other chunk to create fragmentation */
    for (int i = 1; i < allocated_chunks; i += 2) {
        if (memory_chunks[i]) {
            free(memory_chunks[i]);
            memory_chunks[i] = NULL;
        }
    }
    
    /* Try to allocate large contiguous blocks */
    const int LARGE_BLOCK_SIZE = 2 * 1024 * 1024; /* 2MB */
    void *large_block = malloc(LARGE_BLOCK_SIZE);
    if (large_block) {
        log_info("Successfully allocated large block despite fragmentation");
        free(large_block);
    } else {
        log_warning("Failed to allocate large block due to fragmentation");
    }
    
    /* Test memory leak detection under stress */
    log_info("Testing memory leak detection");
    
    const mem_stats_t *fragmented_stats = memory_get_stats();
    uint32_t expected_usage = initial_stats->used_memory + (allocated_chunks / 2) * CHUNK_SIZE;
    uint32_t actual_usage = fragmented_stats->used_memory;
    
    if (actual_usage > expected_usage * 1.1) { /* Allow 10% overhead */
        log_warning("Possible memory leak detected: expected %u, actual %u", 
                   expected_usage, actual_usage);
    }
    
    /* Cleanup all allocated memory */
    for (int i = 0; i < allocated_chunks; i++) {
        if (memory_chunks[i]) {
            free(memory_chunks[i]);
        }
    }
    
    /* Verify memory cleanup */
    const mem_stats_t *final_stats = memory_get_stats();
    if (final_stats->used_memory > initial_stats->used_memory + 1024 * 1024) { /* Allow 1MB overhead */
        log_error("Memory leak detected after cleanup: %lu bytes leaked", 
                 final_stats->used_memory - initial_stats->used_memory);
        overall_result = -1;
    } else {
        log_info("Memory cleanup successful");
    }
    
    if (overall_result == 0) {
        log_info("Memory pressure stress test PASSED");
    } else {
        log_error("Memory pressure stress test FAILED");
    }
    
    return overall_result;
}

/**
 * @brief Test network load stress scenarios
 */
static int test_network_load_stress(void) {
    log_info("Testing network load stress scenarios");
    
    int overall_result = 0;
    uint32_t start_time = get_system_timestamp_ms();
    uint32_t end_time = start_time + g_stress_config.stress_duration_ms;
    uint32_t packets_processed = 0;
    
    /* Create multiple mock NICs for load testing */
    int num_nics = mock_create_test_nics(4);
    if (num_nics < 2) {
        log_error("Failed to create sufficient NICs for network stress test");
        return -1;
    }
    
    log_info("Testing high packet rate stress (%u PPS target)", g_stress_config.max_packet_rate_pps);
    
    /* Calculate target packet interval */
    uint32_t target_interval_us = 1000000 / g_stress_config.max_packet_rate_pps;
    uint32_t last_packet_time = get_system_timestamp_ms() * 1000; /* Convert to microseconds */
    
    while (get_system_timestamp_ms() < end_time) {
        uint32_t current_time_us = get_system_timestamp_ms() * 1000;
        
        /* Send packets at target rate */
        if (current_time_us - last_packet_time >= target_interval_us) {
            packet_buffer_t *stress_packet = packet_buffer_alloc(64 + (packets_processed % 1400));
            if (stress_packet) {
                /* Build test packet */
                uint8_t src_mac[ETH_ALEN] = {0x00, 0x10, 0x4B, 0x00, 
                                           (uint8_t)(packets_processed >> 8), 
                                           (uint8_t)(packets_processed & 0xFF)};
                uint8_t dst_mac[ETH_ALEN] = {0x00, 0x10, 0x4B, 0xFF, 
                                           (uint8_t)(packets_processed >> 8), 
                                           (uint8_t)(packets_processed & 0xFF)};
                
                memcpy(stress_packet->data, dst_mac, ETH_ALEN);
                memcpy(stress_packet->data + ETH_ALEN, src_mac, ETH_ALEN);
                *(uint16_t*)(stress_packet->data + 12) = htons(ETH_P_IP);
                
                /* Send packet through random NIC */
                int nic_id = packets_processed % num_nics;
                int tx_result = packet_transmit(stress_packet, nic_id);
                
                if (tx_result != 0 && packets_processed % 1000 == 0) {
                    log_warning("Packet transmission failed under load (packet %u)", packets_processed);
                }
                
                packet_buffer_free(stress_packet);
                packets_processed++;
                last_packet_time = current_time_us;
            } else {
                log_warning("Packet allocation failed under load stress");
                overall_result = -1;
                break;
            }
        }
        
        /* Process received packets */
        int rx_result = packet_process_received();
        if (rx_result < 0) {
            log_warning("Packet processing failed under load stress");
        }
        
        /* Periodically check system health */
        if (packets_processed % 10000 == 0 && packets_processed > 0) {
            const mem_stats_t *stress_stats = memory_get_stats();
            if (stress_stats->allocation_failures > 100) {
                log_warning("High allocation failure rate under network stress");
            }
            
            log_info("Network stress progress: %u packets processed", packets_processed);
        }
    }
    
    uint32_t actual_duration_ms = get_system_timestamp_ms() - start_time;
    double actual_pps = (double)packets_processed / (actual_duration_ms / 1000.0);
    
    log_info("Network load stress results:");
    log_info("  Target PPS: %u", g_stress_config.max_packet_rate_pps);
    log_info("  Actual PPS: %.0f", actual_pps);
    log_info("  Packets processed: %u", packets_processed);
    log_info("  Duration: %u ms", actual_duration_ms);
    
    g_stress_stats.total_packets_processed += packets_processed;
    
    /* Verify system stability after stress */
    mock_statistics_t mock_stats;
    mock_get_statistics(&mock_stats);
    
    if (mock_stats.total_io_operations == 0) {
        log_error("No I/O operations recorded during network stress");
        overall_result = -1;
    }
    
    if (actual_pps < g_stress_config.max_packet_rate_pps * 0.5) {
        log_warning("Achieved packet rate significantly lower than target");
    }
    
    if (overall_result == 0) {
        log_info("Network load stress test PASSED");
    } else {
        log_error("Network load stress test FAILED");
    }
    
    return overall_result;
}

/**
 * @brief Test error injection stress scenarios
 */
static int test_error_injection_stress(void) {
    log_info("Testing error injection stress scenarios");
    
    int overall_result = 0;
    uint32_t start_time = get_system_timestamp_ms();
    uint32_t end_time = start_time + (g_stress_config.stress_duration_ms / 2); /* Shorter for error injection */
    uint32_t operations_attempted = 0;
    uint32_t successful_recoveries = 0;
    
    /* Enable various error injection modes */
    mock_enable_memory_allocation_failures(true, g_stress_config.error_injection_rate_percent);
    mock_enable_hardware_errors(true, g_stress_config.error_injection_rate_percent);
    mock_enable_packet_corruption(true, g_stress_config.error_injection_rate_percent);
    
    log_info("Testing error injection at %d%% rate", g_stress_config.error_injection_rate_percent);
    
    while (get_system_timestamp_ms() < end_time) {
        operations_attempted++;
        
        /* Test packet operations under error injection */
        packet_buffer_t *error_test_packet = packet_buffer_alloc(256);
        if (error_test_packet) {
            /* Build test packet */
            uint8_t test_mac[ETH_ALEN] = {0x00, 0x10, 0x4B, 0xAA, 0xBB, 0xCC};
            memcpy(error_test_packet->data, test_mac, ETH_ALEN);
            memcpy(error_test_packet->data + ETH_ALEN, test_mac, ETH_ALEN);
            *(uint16_t*)(error_test_packet->data + 12) = htons(ETH_P_IP);
            
            /* Test transmission with error injection */
            int tx_result = packet_transmit(error_test_packet, 0);
            if (tx_result == 0) {
                successful_recoveries++;
            }
            
            packet_buffer_free(error_test_packet);
        }
        
        /* Test hardware operations under error injection */
        if (operations_attempted % 100 == 0) {
            int hw_result = hardware_self_test_all();
            if (hw_result == 0) {
                successful_recoveries++;
            }
        }
        
        /* Test memory operations under error injection */
        void *error_test_memory = malloc(1024);
        if (error_test_memory) {
            memset(error_test_memory, 0xAA, 1024);
            free(error_test_memory);
            successful_recoveries++;
        }
        
        /* Simulate error recovery attempts */
        if (operations_attempted % 50 == 0) {
            int recovery_result = mock_simulate_error_recovery();
            if (recovery_result == 0) {
                successful_recoveries++;
            }
        }
    }
    
    /* Disable error injection */
    mock_enable_memory_allocation_failures(false, 0);
    mock_enable_hardware_errors(false, 0);
    mock_enable_packet_corruption(false, 0);
    
    double success_rate = (double)successful_recoveries / operations_attempted * 100.0;
    
    log_info("Error injection stress results:");
    log_info("  Operations attempted: %u", operations_attempted);
    log_info("  Successful recoveries: %u", successful_recoveries);
    log_info("  Success rate: %.1f%%", success_rate);
    
    g_stress_stats.total_errors_injected += operations_attempted * g_stress_config.error_injection_rate_percent / 100;
    g_stress_stats.successful_recoveries += successful_recoveries;
    
    /* Verify system recovered from error injection */
    packet_buffer_t *recovery_test = packet_buffer_alloc(64);
    if (!recovery_test) {
        log_error("System failed to recover from error injection stress");
        overall_result = -1;
    } else {
        packet_buffer_free(recovery_test);
        log_info("System successfully recovered from error injection stress");
    }
    
    if (success_rate < 50.0) {
        log_warning("Low success rate during error injection: %.1f%%", success_rate);
    }
    
    if (overall_result == 0) {
        log_info("Error injection stress test PASSED");
    } else {
        log_error("Error injection stress test FAILED");
    }
    
    return overall_result;
}

/**
 * @brief Test concurrent operations stress scenarios
 */
static int test_concurrent_operations_stress(void) {
    log_info("Testing concurrent operations stress scenarios");
    
    /* Note: This is a simplified version as true multi-threading 
     * would require thread synchronization primitives */
    
    int overall_result = 0;
    uint32_t start_time = get_system_timestamp_ms();
    uint32_t end_time = start_time + g_stress_config.stress_duration_ms;
    
    /* Simulate concurrent operations by interleaving different operations rapidly */
    uint32_t concurrent_operations = 0;
    
    log_info("Simulating concurrent operations stress");
    
    while (get_system_timestamp_ms() < end_time) {
        concurrent_operations++;
        
        /* Interleave packet operations */
        packet_buffer_t *concurrent_packet = packet_buffer_alloc(64 + (concurrent_operations % 1400));
        if (concurrent_packet) {
            /* Simulate concurrent TX/RX operations */
            int tx_result = packet_transmit(concurrent_packet, concurrent_operations % 2);
            int rx_result = packet_process_received();
            
            packet_buffer_free(concurrent_packet);
            
            if (tx_result != 0 || rx_result < 0) {
                if (concurrent_operations % 1000 == 0) {
                    log_warning("Concurrent operation failures detected");
                }
            }
        }
        
        /* Interleave memory operations */
        if (concurrent_operations % 10 == 0) {
            void *concurrent_memory = malloc(256);
            if (concurrent_memory) {
                memset(concurrent_memory, 0x55, 256);
                free(concurrent_memory);
            }
        }
        
        /* Interleave hardware operations */
        if (concurrent_operations % 100 == 0) {
            hardware_get_nic_count();
            mock_get_statistics(NULL);
        }
        
        /* Check for resource contention issues */
        if (concurrent_operations % 1000 == 0) {
            const mem_stats_t *contention_stats = memory_get_stats();
            if (contention_stats->allocation_failures > concurrent_operations / 100) {
                log_warning("High allocation failure rate suggesting resource contention");
            }
        }
    }
    
    uint32_t actual_duration_ms = get_system_timestamp_ms() - start_time;
    double ops_per_second = (double)concurrent_operations / (actual_duration_ms / 1000.0);
    
    log_info("Concurrent operations stress results:");
    log_info("  Operations completed: %u", concurrent_operations);
    log_info("  Operations per second: %.0f", ops_per_second);
    log_info("  Duration: %u ms", actual_duration_ms);
    
    /* Verify system stability after concurrent stress */
    int stability_check = hardware_self_test_all();
    if (stability_check != 0) {
        log_error("System stability check failed after concurrent operations stress");
        overall_result = -1;
    }
    
    if (overall_result == 0) {
        log_info("Concurrent operations stress test PASSED");
    } else {
        log_error("Concurrent operations stress test FAILED");
    }
    
    return overall_result;
}

/**
 * @brief Test long duration stability stress
 */
static int test_long_duration_stability_stress(void) {
    log_info("Testing long duration stability stress");
    
    if (!g_stress_config.run_long_duration_tests) {
        log_info("Long duration tests disabled, skipping");
        return 0;
    }
    
    int overall_result = 0;
    uint32_t start_time = get_system_timestamp_ms();
    uint32_t end_time = start_time + g_stress_config.stress_duration_ms;
    uint32_t check_interval_ms = 30000; /* Check every 30 seconds */
    uint32_t next_check = start_time + check_interval_ms;
    uint32_t stability_operations = 0;
    
    log_info("Running long duration stability test for %u seconds", 
             g_stress_config.stress_duration_ms / 1000);
    
    const mem_stats_t *initial_stability_stats = memory_get_stats();
    
    while (get_system_timestamp_ms() < end_time) {
        stability_operations++;
        
        /* Continuous low-rate operations */
        packet_buffer_t *stability_packet = packet_buffer_alloc(512);
        if (stability_packet) {
            packet_transmit(stability_packet, 0);
            packet_buffer_free(stability_packet);
        }
        
        /* Periodic memory allocation/deallocation */
        if (stability_operations % 100 == 0) {
            void *stability_memory = malloc(4096);
            if (stability_memory) {
                memset(stability_memory, 0x33, 4096);
                free(stability_memory);
            }
        }
        
        /* Periodic stability checks */
        if (get_system_timestamp_ms() >= next_check) {
            log_info("Stability checkpoint: %u operations completed", stability_operations);
            
            /* Check for memory leaks */
            const mem_stats_t *current_stability_stats = memory_get_stats();
            uint32_t memory_growth = current_stability_stats->used_memory - initial_stability_stats->used_memory;
            
            if (memory_growth > 1024 * 1024) { /* 1MB growth threshold */
                log_warning("Memory growth detected: %u bytes", memory_growth);
            }
            
            /* System health check */
            int health_check = hardware_self_test_all();
            if (health_check != 0) {
                log_error("System health check failed during long duration test");
                overall_result = -1;
                break;
            }
            
            next_check = get_system_timestamp_ms() + check_interval_ms;
        }
        
        /* Small delay to simulate realistic operation timing */
        /* In a real implementation, this might be a very short sleep */
    }
    
    uint32_t actual_duration_ms = get_system_timestamp_ms() - start_time;
    
    log_info("Long duration stability results:");
    log_info("  Operations completed: %u", stability_operations);
    log_info("  Actual duration: %u ms (%.1f minutes)", 
             actual_duration_ms, actual_duration_ms / 60000.0);
    
    /* Final stability verification */
    const mem_stats_t *final_stability_stats = memory_get_stats();
    uint32_t total_memory_growth = final_stability_stats->used_memory - initial_stability_stats->used_memory;
    
    if (total_memory_growth > 2 * 1024 * 1024) { /* 2MB final threshold */
        log_error("Significant memory leak detected: %u bytes", total_memory_growth);
        overall_result = -1;
    }
    
    if (overall_result == 0) {
        log_info("Long duration stability stress test PASSED");
    } else {
        log_error("Long duration stability stress test FAILED");
    }
    
    return overall_result;
}

/**
 * @brief Run a specific stress test
 */
static int run_stress_test(const stress_test_t *stress_test) {
    if (!stress_test || !stress_test->stress_test_main) {
        log_error("Invalid stress test");
        return -1;
    }
    
    log_info("=== Running Stress Test: %s ===", stress_test->name);
    log_info("Description: %s", stress_test->description);
    log_info("Expected duration: %lu ms", stress_test->expected_duration_ms);
    
    uint32_t start_time = get_system_timestamp_ms();
    
    int result = stress_test->stress_test_main();
    
    uint32_t end_time = get_system_timestamp_ms();
    uint32_t duration = end_time - start_time;
    
    g_stress_stats.total_stress_tests_run++;
    
    /* Track longest test */
    if (duration > g_stress_stats.longest_test_duration_ms) {
        g_stress_stats.longest_test_duration_ms = duration;
        g_stress_stats.longest_test_name = stress_test->name;
    }
    
    if (result == 0) {
        g_stress_stats.stress_tests_passed++;
        log_info("✓ Stress Test PASSED: %s (duration: %lu ms)", stress_test->name, duration);
        
        if (duration > stress_test->expected_duration_ms * 2) {
            log_warning("Test took significantly longer than expected (%lu ms vs %lu ms expected)", 
                       duration, stress_test->expected_duration_ms);
        }
    } else {
        g_stress_stats.stress_tests_failed++;
        log_error("✗ Stress Test FAILED: %s (duration: %lu ms, code: %d)", 
                  stress_test->name, duration, result);
        
        if (stress_test->is_critical) {
            g_stress_stats.critical_failures++;
            if (g_stress_config.stop_on_critical_failure) {
                log_error("Critical stress test failed, stopping execution");
                return result;
            }
        }
    }
    
    return result;
}

/**
 * @brief Print stress test summary
 */
static void print_stress_test_summary(void) {
    log_info("");
    log_info("===================================================================");
    log_info("                   STRESS TEST SUITE SUMMARY");
    log_info("===================================================================");
    log_info("Stress Tests Executed:");
    log_info("  Total Tests: %d", g_stress_stats.total_stress_tests_run);
    log_info("  Passed: %d", g_stress_stats.stress_tests_passed);
    log_info("  Failed: %d", g_stress_stats.stress_tests_failed);
    log_info("  Critical Failures: %d", g_stress_stats.critical_failures);
    log_info("");
    log_info("Stress Test Data:");
    log_info("  Peak Memory Usage: %u MB", g_stress_stats.peak_memory_usage_mb);
    log_info("  Total Packets Processed: %u", g_stress_stats.total_packets_processed);
    log_info("  Total Errors Injected: %llu", g_stress_stats.total_errors_injected);
    log_info("  Successful Recoveries: %llu", g_stress_stats.successful_recoveries);
    log_info("");
    log_info("Execution Time:");
    log_info("  Total Duration: %lu ms (%.2f minutes)", 
             g_stress_stats.total_duration_ms, g_stress_stats.total_duration_ms / 60000.0);
    log_info("  Longest Test: %s (%lu ms)", 
             g_stress_stats.longest_test_name ? g_stress_stats.longest_test_name : "N/A",
             g_stress_stats.longest_test_duration_ms);
    log_info("");
    
    if (g_stress_stats.stress_tests_failed == 0) {
        log_info("Success Rate: 100%% - ALL STRESS TESTS PASSED! ✓");
    } else {
        float success_rate = (float)g_stress_stats.stress_tests_passed / g_stress_stats.total_stress_tests_run * 100.0;
        log_info("Success Rate: %.1f%% (%d/%d tests passed)", 
                 success_rate, g_stress_stats.stress_tests_passed, g_stress_stats.total_stress_tests_run);
        
        if (g_stress_stats.critical_failures > 0) {
            log_error("CRITICAL: %d critical failures detected", g_stress_stats.critical_failures);
        } else if (success_rate >= 80.0) {
            log_info("Result: GOOD - Most stress tests passed");
        } else if (success_rate >= 60.0) {
            log_warning("Result: ACCEPTABLE - Some stress tests failed");
        } else {
            log_error("Result: POOR - Many stress tests failed");
        }
    }
    
    /* Performance recovery analysis */
    if (g_stress_stats.total_errors_injected > 0) {
        double recovery_rate = (double)g_stress_stats.successful_recoveries / g_stress_stats.total_errors_injected * 100.0;
        log_info("Error Recovery Rate: %.1f%%", recovery_rate);
        
        if (recovery_rate < 50.0) {
            log_warning("Low error recovery rate suggests system stability issues");
        }
    }
    
    log_info("===================================================================");
}

/**
 * @brief Main stress test runner entry point (called from master runner)
 */
int run_stress_tests(int argc, char *argv[]) {
    log_info("Starting Stress Test Suite Runner");
    log_info("=================================");
    
    /* Parse stress test specific arguments */
    int parse_result = parse_stress_test_arguments(argc, argv);
    if (parse_result == 1) {
        return 0;  /* Help was shown */
    } else if (parse_result < 0) {
        return 1;  /* Error in arguments */
    }
    
    /* Initialize stress test environment */
    int init_result = initialize_stress_test_environment();
    if (init_result != 0) {
        log_error("Failed to initialize stress test environment");
        return 1;
    }
    
    uint32_t overall_start_time = get_system_timestamp_ms();
    
    /* Define all stress tests */
    stress_test_t stress_tests[] = {
        {
            .name = "Resource Exhaustion",
            .description = "Buffer and descriptor exhaustion stress testing",
            .stress_test_main = test_resource_exhaustion_stress,
            .enabled_flag = &g_stress_config.run_resource_stress_tests,
            .is_critical = true,
            .expected_duration_ms = 10000,
            .max_allowed_failures = 0
        },
        {
            .name = "Memory Pressure",
            .description = "Memory allocation and pressure stress testing",
            .stress_test_main = test_memory_pressure_stress,
            .enabled_flag = &g_stress_config.run_memory_stress_tests,
            .is_critical = true,
            .expected_duration_ms = 15000,
            .max_allowed_failures = 0
        },
        {
            .name = "Network Load",
            .description = "High packet rate and network load stress testing",
            .stress_test_main = test_network_load_stress,
            .enabled_flag = &g_stress_config.run_network_stress_tests,
            .is_critical = false,
            .expected_duration_ms = g_stress_config.stress_duration_ms,
            .max_allowed_failures = 1
        },
        {
            .name = "Error Injection",
            .description = "Fault injection and error recovery stress testing",
            .stress_test_main = test_error_injection_stress,
            .enabled_flag = &g_stress_config.run_error_injection_tests,
            .is_critical = false,
            .expected_duration_ms = g_stress_config.stress_duration_ms / 2,
            .max_allowed_failures = 2
        },
        {
            .name = "Concurrent Operations",
            .description = "Concurrent operation and resource contention stress testing",
            .stress_test_main = test_concurrent_operations_stress,
            .enabled_flag = &g_stress_config.run_network_stress_tests,
            .is_critical = false,
            .expected_duration_ms = g_stress_config.stress_duration_ms,
            .max_allowed_failures = 1
        },
        {
            .name = "Long Duration Stability",
            .description = "Extended stability and memory leak detection",
            .stress_test_main = test_long_duration_stability_stress,
            .enabled_flag = &g_stress_config.run_long_duration_tests,
            .is_critical = true,
            .expected_duration_ms = g_stress_config.stress_duration_ms,
            .max_allowed_failures = 0
        }
    };
    
    /* Run external stress test modules */
    stress_test_t external_stress_tests[] = {
        {
            .name = "Resource Stress Module",
            .description = "External resource stress test module",
            .stress_test_main = test_stress_resource_main,
            .enabled_flag = &g_stress_config.run_resource_stress_tests,
            .is_critical = false,
            .expected_duration_ms = 20000,
            .max_allowed_failures = 1
        },
        {
            .name = "Stability Stress Module",
            .description = "External stability stress test module",
            .stress_test_main = test_stress_stability_main,
            .enabled_flag = &g_stress_config.run_stability_stress_tests,
            .is_critical = false,
            .expected_duration_ms = 25000,
            .max_allowed_failures = 1
        }
    };
    
    int num_stress_tests = sizeof(stress_tests) / sizeof(stress_tests[0]);
    int num_external_tests = sizeof(external_stress_tests) / sizeof(external_stress_tests[0]);
    int overall_result = 0;
    
    /* Run internal stress tests */
    for (int i = 0; i < num_stress_tests; i++) {
        if (!(*stress_tests[i].enabled_flag)) {
            log_info("Skipping disabled stress test: %s", stress_tests[i].name);
            continue;
        }
        
        int result = run_stress_test(&stress_tests[i]);
        if (result != 0) {
            overall_result = 1;
            
            if (stress_tests[i].is_critical && g_stress_config.stop_on_critical_failure) {
                log_error("Critical stress test failed, stopping execution");
                break;
            }
        }
    }
    
    /* Run external stress test modules if no critical failures */
    if (overall_result == 0 || !g_stress_config.stop_on_critical_failure) {
        for (int i = 0; i < num_external_tests; i++) {
            if (!(*external_stress_tests[i].enabled_flag)) {
                log_info("Skipping disabled external stress test: %s", external_stress_tests[i].name);
                continue;
            }
            
            int result = run_stress_test(&external_stress_tests[i]);
            if (result != 0) {
                overall_result = 1;
            }
        }
    }
    
    uint32_t overall_end_time = get_system_timestamp_ms();
    g_stress_stats.total_duration_ms = overall_end_time - overall_start_time;
    
    /* Print comprehensive summary */
    print_stress_test_summary();
    
    /* Cleanup */
    cleanup_stress_test_environment();
    
    if (overall_result == 0) {
        log_info("Stress Test Suite: ALL TESTS COMPLETED SUCCESSFULLY");
    } else {
        log_error("Stress Test Suite: SOME TESTS FAILED");
        
        if (g_stress_stats.critical_failures > 0) {
            log_error("CRITICAL FAILURES DETECTED - SYSTEM STABILITY COMPROMISED");
        }
    }
    
    return overall_result;
}

/**
 * @brief Standalone entry point (when run directly)
 */
int main(int argc, char *argv[]) {
    printf("3Com Packet Driver - Stress Test Suite Runner\n");
    printf("============================================\n\n");
    
    return run_stress_tests(argc, argv);
}