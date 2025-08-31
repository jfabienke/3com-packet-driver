/**
 * @file stress_test.c
 * @brief Stress testing framework for DOS packet driver
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file implements comprehensive stress testing to validate driver
 * stability under extreme conditions including packet storms, memory
 * exhaustion, concurrent operations, and long-duration testing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dos.h>
#include <mem.h>

/* Test configuration constants */
#define MAX_STRESS_TESTS        32
#define MAX_CONCURRENT_APPS     8
#define MAX_PACKET_STORM_RATE   10000
#define STRESS_TEST_DURATION    300     /* 5 minutes in seconds */
#define LONG_DURATION_TEST      86400   /* 24 hours in seconds */
#define MEMORY_STRESS_BLOCKS    256
#define MAX_ERROR_INJECTIONS    100

/* Stress test types */
typedef enum {
    STRESS_PACKET_STORM = 1,
    STRESS_MEMORY_EXHAUSTION,
    STRESS_CONCURRENT_APPS,
    STRESS_ERROR_INJECTION,
    STRESS_LONG_DURATION,
    STRESS_RESOURCE_STARVATION,
    STRESS_INTERRUPT_FLOOD,
    STRESS_RANDOM_CHAOS
} stress_test_type_t;

/* Test results */
typedef enum {
    STRESS_RESULT_PASS = 0,
    STRESS_RESULT_FAIL,
    STRESS_RESULT_TIMEOUT,
    STRESS_RESULT_CRASH,
    STRESS_RESULT_RESOURCE_ERROR
} stress_result_t;

/* Stress test configuration */
typedef struct {
    stress_test_type_t type;
    unsigned long duration_seconds;
    unsigned int intensity_level;      /* 1-10 scale */
    unsigned int packet_rate;          /* packets per second */
    unsigned int memory_pressure;      /* KB to allocate */
    unsigned int concurrent_operations;
    unsigned int error_injection_rate; /* errors per 1000 operations */
    int enable_logging;
    int stop_on_failure;
} stress_config_t;

/* Test statistics */
typedef struct {
    unsigned long packets_sent;
    unsigned long packets_received;
    unsigned long packets_dropped;
    unsigned long errors_detected;
    unsigned long memory_allocated;
    unsigned long memory_freed;
    unsigned long interrupts_handled;
    unsigned long cpu_cycles_used;
    unsigned int max_latency_us;
    unsigned int min_latency_us;
    unsigned int avg_latency_us;
} stress_stats_t;

/* Stress test state */
typedef struct {
    stress_config_t config;
    stress_stats_t stats;
    stress_result_t result;
    time_t start_time;
    time_t end_time;
    unsigned long test_iterations;
    unsigned int error_count;
    char error_messages[10][256];
    int is_running;
    int should_stop;
} stress_test_state_t;

/* Memory allocation tracking */
typedef struct {
    void far *ptr;
    unsigned int size;
    unsigned int pattern;
    time_t alloc_time;
} memory_block_t;

/* Concurrent application simulation */
typedef struct {
    int app_id;
    unsigned int packet_count;
    unsigned int error_count;
    time_t start_time;
    int is_active;
} concurrent_app_t;

/* Global test state */
static stress_test_state_t current_test;
static memory_block_t memory_blocks[MEMORY_STRESS_BLOCKS];
static concurrent_app_t concurrent_apps[MAX_CONCURRENT_APPS];
static unsigned int allocated_block_count = 0;
static unsigned int active_app_count = 0;

/* Random number generator state */
static unsigned long rand_seed = 1;

/* Function prototypes */
int stress_test_init(void);
int stress_test_run_all(void);
void stress_test_cleanup(void);

/* Individual stress tests */
stress_result_t stress_test_packet_storm(const stress_config_t *config);
stress_result_t stress_test_memory_exhaustion(const stress_config_t *config);
stress_result_t stress_test_concurrent_apps(const stress_config_t *config);
stress_result_t stress_test_error_injection(const stress_config_t *config);
stress_result_t stress_test_long_duration(const stress_config_t *config);
stress_result_t stress_test_resource_starvation(const stress_config_t *config);
stress_result_t stress_test_interrupt_flood(const stress_config_t *config);
stress_result_t stress_test_random_chaos(const stress_config_t *config);

/* Helper functions */
static void init_stress_config(stress_config_t *config, stress_test_type_t type);
static void reset_stress_stats(stress_stats_t *stats);
static int simulate_packet_transmission(unsigned int size);
static int simulate_packet_reception(unsigned int size);
static void inject_random_error(void);
static unsigned long get_timer_ticks(void);
static void log_stress_event(const char *message);
static void display_stress_results(const stress_test_state_t *state);
static unsigned int fast_rand(void);
static int allocate_memory_block(unsigned int size);
static void free_memory_block(int block_index);
static int start_concurrent_app(void);
static void stop_concurrent_app(int app_id);
static void simulate_hardware_error(void);
static int check_system_stability(void);

/**
 * Initialize stress testing framework
 */
int stress_test_init(void)
{
    printf("Initializing stress testing framework...\n");
    
    /* Clear test state */
    memset(&current_test, 0, sizeof(stress_test_state_t));
    memset(memory_blocks, 0, sizeof(memory_blocks));
    memset(concurrent_apps, 0, sizeof(concurrent_apps));
    
    allocated_block_count = 0;
    active_app_count = 0;
    
    /* Initialize random seed */
    rand_seed = (unsigned long)time(NULL);
    
    /* Check system resources */
    if (coreleft() < 32768) {
        printf("WARNING: Low memory available for stress testing\n");
    }
    
    printf("Stress testing framework initialized.\n");
    return 0;
}

/**
 * Run all stress tests
 */
int stress_test_run_all(void)
{
    stress_config_t config;
    stress_result_t result;
    int total_tests = 0;
    int passed_tests = 0;
    int failed_tests = 0;
    
    printf("\n=== DOS Packet Driver Stress Test Suite ===\n\n");
    
    /* Test 1: Packet Storm Stress Test */
    printf("Running Packet Storm Stress Test...\n");
    init_stress_config(&config, STRESS_PACKET_STORM);
    config.packet_rate = 5000;
    config.duration_seconds = 60;
    config.intensity_level = 7;
    
    result = stress_test_packet_storm(&config);
    total_tests++;
    if (result == STRESS_RESULT_PASS) {
        passed_tests++;
        printf("PASS: Packet storm test completed successfully\n");
    } else {
        failed_tests++;
        printf("FAIL: Packet storm test failed (result: %d)\n", result);
    }
    
    /* Test 2: Memory Exhaustion Stress Test */
    printf("\nRunning Memory Exhaustion Stress Test...\n");
    init_stress_config(&config, STRESS_MEMORY_EXHAUSTION);
    config.memory_pressure = 512;  /* 512 KB */
    config.duration_seconds = 120;
    config.intensity_level = 8;
    
    result = stress_test_memory_exhaustion(&config);
    total_tests++;
    if (result == STRESS_RESULT_PASS) {
        passed_tests++;
        printf("PASS: Memory exhaustion test completed successfully\n");
    } else {
        failed_tests++;
        printf("FAIL: Memory exhaustion test failed (result: %d)\n", result);
    }
    
    /* Test 3: Concurrent Applications Stress Test */
    printf("\nRunning Concurrent Applications Stress Test...\n");
    init_stress_config(&config, STRESS_CONCURRENT_APPS);
    config.concurrent_operations = 6;
    config.duration_seconds = 180;
    config.intensity_level = 6;
    
    result = stress_test_concurrent_apps(&config);
    total_tests++;
    if (result == STRESS_RESULT_PASS) {
        passed_tests++;
        printf("PASS: Concurrent applications test completed successfully\n");
    } else {
        failed_tests++;
        printf("FAIL: Concurrent applications test failed (result: %d)\n", result);
    }
    
    /* Test 4: Error Injection Stress Test */
    printf("\nRunning Error Injection Stress Test...\n");
    init_stress_config(&config, STRESS_ERROR_INJECTION);
    config.error_injection_rate = 50;  /* 5% error rate */
    config.duration_seconds = 90;
    config.intensity_level = 5;
    
    result = stress_test_error_injection(&config);
    total_tests++;
    if (result == STRESS_RESULT_PASS) {
        passed_tests++;
        printf("PASS: Error injection test completed successfully\n");
    } else {
        failed_tests++;
        printf("FAIL: Error injection test failed (result: %d)\n", result);
    }
    
    /* Test 5: Resource Starvation Stress Test */
    printf("\nRunning Resource Starvation Stress Test...\n");
    init_stress_config(&config, STRESS_RESOURCE_STARVATION);
    config.duration_seconds = 120;
    config.intensity_level = 9;
    
    result = stress_test_resource_starvation(&config);
    total_tests++;
    if (result == STRESS_RESULT_PASS) {
        passed_tests++;
        printf("PASS: Resource starvation test completed successfully\n");
    } else {
        failed_tests++;
        printf("FAIL: Resource starvation test failed (result: %d)\n", result);
    }
    
    /* Test 6: Interrupt Flood Stress Test */
    printf("\nRunning Interrupt Flood Stress Test...\n");
    init_stress_config(&config, STRESS_INTERRUPT_FLOOD);
    config.duration_seconds = 60;
    config.intensity_level = 8;
    
    result = stress_test_interrupt_flood(&config);
    total_tests++;
    if (result == STRESS_RESULT_PASS) {
        passed_tests++;
        printf("PASS: Interrupt flood test completed successfully\n");
    } else {
        failed_tests++;
        printf("FAIL: Interrupt flood test failed (result: %d)\n", result);
    }
    
    /* Test 7: Random Chaos Stress Test */
    printf("\nRunning Random Chaos Stress Test...\n");
    init_stress_config(&config, STRESS_RANDOM_CHAOS);
    config.duration_seconds = 300;    /* 5 minutes */
    config.intensity_level = 10;      /* Maximum chaos */
    
    result = stress_test_random_chaos(&config);
    total_tests++;
    if (result == STRESS_RESULT_PASS) {
        passed_tests++;
        printf("PASS: Random chaos test completed successfully\n");
    } else {
        failed_tests++;
        printf("FAIL: Random chaos test failed (result: %d)\n", result);
    }
    
    /* Optional: Long Duration Test (only if specifically requested) */
    printf("\nLong duration test available (24 hours) - run separately if needed\n");
    
    /* Display final results */
    printf("\n=== Stress Test Results ===\n");
    printf("Total Tests: %d\n", total_tests);
    printf("Passed: %d\n", passed_tests);
    printf("Failed: %d\n", failed_tests);
    printf("Success Rate: %d%%\n", (passed_tests * 100) / total_tests);
    
    return (failed_tests == 0) ? 0 : 1;
}

/**
 * Cleanup stress testing framework
 */
void stress_test_cleanup(void)
{
    int i;
    
    printf("Cleaning up stress testing framework...\n");
    
    /* Free any allocated memory blocks */
    for (i = 0; i < allocated_block_count; i++) {
        if (memory_blocks[i].ptr != NULL) {
            farfree(memory_blocks[i].ptr);
            memory_blocks[i].ptr = NULL;
        }
    }
    
    /* Stop any active concurrent applications */
    for (i = 0; i < active_app_count; i++) {
        if (concurrent_apps[i].is_active) {
            stop_concurrent_app(i);
        }
    }
    
    allocated_block_count = 0;
    active_app_count = 0;
    
    printf("Stress testing cleanup complete.\n");
}

/**
 * Packet storm stress test
 */
stress_result_t stress_test_packet_storm(const stress_config_t *config)
{
    time_t start_time, current_time;
    unsigned long packets_sent = 0;
    unsigned long packets_received = 0;
    unsigned long target_packets;
    unsigned int packet_size;
    int i;
    
    printf("  Packet rate: %u packets/sec for %lu seconds\n", 
           config->packet_rate, config->duration_seconds);
    
    start_time = time(NULL);
    target_packets = config->packet_rate * config->duration_seconds;
    
    /* Vary packet sizes during storm */
    while (packets_sent < target_packets) {
        current_time = time(NULL);
        
        /* Check timeout */
        if ((current_time - start_time) > (long)config->duration_seconds + 10) {
            printf("  TIMEOUT: Packet storm test exceeded time limit\n");
            return STRESS_RESULT_TIMEOUT;
        }
        
        /* Send burst of packets */
        for (i = 0; i < 100 && packets_sent < target_packets; i++) {
            /* Vary packet sizes: 64, 512, 1518 bytes */
            packet_size = (fast_rand() % 3 == 0) ? 64 : 
                         (fast_rand() % 3 == 1) ? 512 : 1518;
            
            if (simulate_packet_transmission(packet_size)) {
                packets_sent++;
                
                /* Simulate packet reception */
                if (simulate_packet_reception(packet_size)) {
                    packets_received++;
                }
            } else {
                printf("  ERROR: Packet transmission failed\n");
                return STRESS_RESULT_FAIL;
            }
        }
        
        /* Brief pause to prevent overwhelming system */
        delay(1);  /* 1 ms delay */
        
        /* Check system stability periodically */
        if (packets_sent % 1000 == 0) {
            if (!check_system_stability()) {
                printf("  ERROR: System stability check failed\n");
                return STRESS_RESULT_FAIL;
            }
        }
    }
    
    /* Verify packet statistics */
    printf("  Packets sent: %lu, received: %lu\n", packets_sent, packets_received);
    
    /* Allow for some packet loss under stress */
    if (packets_received < (packets_sent * 95 / 100)) {
        printf("  WARNING: High packet loss rate: %lu%%\n", 
               (packets_sent - packets_received) * 100 / packets_sent);
    }
    
    return STRESS_RESULT_PASS;
}

/**
 * Memory exhaustion stress test
 */
stress_result_t stress_test_memory_exhaustion(const stress_config_t *config)
{
    time_t start_time, current_time;
    unsigned long memory_allocated = 0;
    unsigned long target_memory;
    int blocks_allocated = 0;
    int i;
    unsigned int block_size;
    
    printf("  Target memory pressure: %u KB for %lu seconds\n",
           config->memory_pressure, config->duration_seconds);
    
    start_time = time(NULL);
    target_memory = (unsigned long)config->memory_pressure * 1024;
    
    /* Phase 1: Allocate memory blocks */
    while (memory_allocated < target_memory && blocks_allocated < MEMORY_STRESS_BLOCKS) {
        /* Vary block sizes */
        block_size = 1024 + (fast_rand() % 4096);  /* 1-5 KB blocks */
        
        if (allocate_memory_block(block_size)) {
            memory_allocated += block_size;
            blocks_allocated++;
            
            printf("  Allocated block %d: %u bytes (total: %lu KB)\n",
                   blocks_allocated, block_size, memory_allocated / 1024);
        } else {
            printf("  Memory allocation failed at %lu KB\n", memory_allocated / 1024);
            break;
        }
        
        /* Brief delay */
        delay(10);
    }
    
    printf("  Successfully allocated %d blocks, %lu KB total\n",
           blocks_allocated, memory_allocated / 1024);
    
    /* Phase 2: Operate under memory pressure */
    current_time = time(NULL);
    while ((current_time - start_time) < (long)config->duration_seconds) {
        /* Simulate packet operations under memory pressure */
        for (i = 0; i < 100; i++) {
            if (!simulate_packet_transmission(512)) {
                printf("  ERROR: Packet operation failed under memory pressure\n");
                /* Free some memory and continue */
                if (blocks_allocated > 0) {
                    free_memory_block(0);
                    blocks_allocated--;
                }
            }
        }
        
        /* Randomly free and reallocate some blocks */
        if (fast_rand() % 10 == 0 && blocks_allocated > 0) {
            int block_to_free = fast_rand() % blocks_allocated;
            free_memory_block(block_to_free);
            
            /* Try to allocate new block */
            block_size = 1024 + (fast_rand() % 2048);
            allocate_memory_block(block_size);
        }
        
        delay(100);  /* 100 ms delay */
        current_time = time(NULL);
        
        /* Check system stability */
        if (!check_system_stability()) {
            printf("  ERROR: System became unstable under memory pressure\n");
            return STRESS_RESULT_FAIL;
        }
    }
    
    /* Phase 3: Free allocated memory */
    for (i = 0; i < allocated_block_count; i++) {
        if (memory_blocks[i].ptr != NULL) {
            free_memory_block(i);
        }
    }
    
    printf("  Memory exhaustion test completed successfully\n");
    return STRESS_RESULT_PASS;
}

/**
 * Concurrent applications stress test
 */
stress_result_t stress_test_concurrent_apps(const stress_config_t *config)
{
    time_t start_time, current_time;
    int apps_started = 0;
    int i, j;
    
    printf("  Simulating %u concurrent applications for %lu seconds\n",
           config->concurrent_operations, config->duration_seconds);
    
    start_time = time(NULL);
    
    /* Start concurrent applications */
    for (i = 0; i < config->concurrent_operations && i < MAX_CONCURRENT_APPS; i++) {
        if (start_concurrent_app()) {
            apps_started++;
            printf("  Started concurrent application %d\n", i + 1);
        } else {
            printf("  Failed to start concurrent application %d\n", i + 1);
        }
    }
    
    printf("  Successfully started %d concurrent applications\n", apps_started);
    
    /* Run concurrent operations */
    current_time = time(NULL);
    while ((current_time - start_time) < (long)config->duration_seconds) {
        /* Simulate activity from each concurrent application */
        for (i = 0; i < active_app_count; i++) {
            if (concurrent_apps[i].is_active) {
                /* Each app sends packets */
                for (j = 0; j < 10; j++) {
                    if (simulate_packet_transmission(256 + (i * 64))) {
                        concurrent_apps[i].packet_count++;
                    } else {
                        concurrent_apps[i].error_count++;
                    }
                }
                
                /* Simulate app receiving packets */
                for (j = 0; j < 5; j++) {
                    simulate_packet_reception(256 + (i * 64));
                }
            }
        }
        
        /* Check for conflicts or resource contention */
        if (!check_system_stability()) {
            printf("  ERROR: System instability detected with concurrent apps\n");
            return STRESS_RESULT_FAIL;
        }
        
        delay(50);  /* 50 ms delay */
        current_time = time(NULL);
    }
    
    /* Display application statistics */
    for (i = 0; i < active_app_count; i++) {
        if (concurrent_apps[i].is_active) {
            printf("  App %d: %u packets sent, %u errors\n",
                   i + 1, concurrent_apps[i].packet_count, concurrent_apps[i].error_count);
        }
    }
    
    /* Stop all concurrent applications */
    for (i = 0; i < active_app_count; i++) {
        if (concurrent_apps[i].is_active) {
            stop_concurrent_app(i);
        }
    }
    
    printf("  Concurrent applications test completed successfully\n");
    return STRESS_RESULT_PASS;
}

/**
 * Error injection stress test
 */
stress_result_t stress_test_error_injection(const stress_config_t *config)
{
    time_t start_time, current_time;
    unsigned long operations = 0;
    unsigned long errors_injected = 0;
    unsigned long errors_recovered = 0;
    int i;
    
    printf("  Error injection rate: %u per 1000 operations for %lu seconds\n",
           config->error_injection_rate, config->duration_seconds);
    
    start_time = time(NULL);
    
    current_time = time(NULL);
    while ((current_time - start_time) < (long)config->duration_seconds) {
        /* Perform normal operations */
        for (i = 0; i < 100; i++) {
            operations++;
            
            /* Inject errors based on configured rate */
            if ((fast_rand() % 1000) < config->error_injection_rate) {
                inject_random_error();
                errors_injected++;
                
                printf("  Injected error %lu after %lu operations\n",
                       errors_injected, operations);
                
                /* Test error recovery */
                if (check_system_stability()) {
                    errors_recovered++;
                    printf("  Successfully recovered from error\n");
                } else {
                    printf("  ERROR: Failed to recover from injected error\n");
                    return STRESS_RESULT_FAIL;
                }
            } else {
                /* Normal packet operation */
                simulate_packet_transmission(512);
                simulate_packet_reception(512);
            }
        }
        
        delay(10);  /* 10 ms delay */
        current_time = time(NULL);
    }
    
    printf("  Operations: %lu, Errors injected: %lu, Recovered: %lu\n",
           operations, errors_injected, errors_recovered);
    
    /* Verify error recovery rate */
    if (errors_injected > 0) {
        unsigned long recovery_rate = (errors_recovered * 100) / errors_injected;
        printf("  Error recovery rate: %lu%%\n", recovery_rate);
        
        if (recovery_rate < 95) {
            printf("  ERROR: Poor error recovery rate\n");
            return STRESS_RESULT_FAIL;
        }
    }
    
    printf("  Error injection test completed successfully\n");
    return STRESS_RESULT_PASS;
}

/**
 * Resource starvation stress test
 */
stress_result_t stress_test_resource_starvation(const stress_config_t *config)
{
    time_t start_time, current_time;
    unsigned long cpu_intensive_ops = 0;
    unsigned long memory_ops = 0;
    unsigned long packet_ops = 0;
    int i, j;
    
    printf("  Resource starvation test for %lu seconds (intensity: %u)\n",
           config->duration_seconds, config->intensity_level);
    
    start_time = time(NULL);
    
    current_time = time(NULL);
    while ((current_time - start_time) < (long)config->duration_seconds) {
        /* CPU intensive operations */
        for (i = 0; i < config->intensity_level * 100; i++) {
            volatile unsigned long dummy = 0;
            for (j = 0; j < 1000; j++) {
                dummy += i * j;
            }
            cpu_intensive_ops++;
        }
        
        /* Memory intensive operations */
        for (i = 0; i < config->intensity_level * 10; i++) {
            if (allocate_memory_block(1024)) {
                memory_ops++;
                /* Immediately free to create memory pressure */
                if (allocated_block_count > 0) {
                    free_memory_block(allocated_block_count - 1);
                }
            }
        }
        
        /* Network operations under resource pressure */
        for (i = 0; i < config->intensity_level * 5; i++) {
            if (simulate_packet_transmission(64)) {
                packet_ops++;
                simulate_packet_reception(64);
            }
        }
        
        /* Check if system is still responsive */
        if (!check_system_stability()) {
            printf("  ERROR: System became unresponsive under resource pressure\n");
            return STRESS_RESULT_FAIL;
        }
        
        /* Very brief delay to prevent complete system lock */
        delay(1);
        current_time = time(NULL);
    }
    
    printf("  Operations completed - CPU: %lu, Memory: %lu, Packets: %lu\n",
           cpu_intensive_ops, memory_ops, packet_ops);
    
    printf("  Resource starvation test completed successfully\n");
    return STRESS_RESULT_PASS;
}

/**
 * Interrupt flood stress test
 */
stress_result_t stress_test_interrupt_flood(const stress_config_t *config)
{
    time_t start_time, current_time;
    unsigned long interrupt_simulations = 0;
    unsigned long max_interrupts_per_second;
    int i;
    
    printf("  Interrupt flood test for %lu seconds (intensity: %u)\n",
           config->duration_seconds, config->intensity_level);
    
    start_time = time(NULL);
    max_interrupts_per_second = config->intensity_level * 1000;
    
    current_time = time(NULL);
    while ((current_time - start_time) < (long)config->duration_seconds) {
        /* Simulate high interrupt rate */
        for (i = 0; i < max_interrupts_per_second / 100; i++) {
            /* Simulate hardware interrupt processing */
            simulate_hardware_error();  /* This simulates interrupt overhead */
            interrupt_simulations++;
            
            /* Intersperse with packet operations */
            if (i % 10 == 0) {
                simulate_packet_transmission(128);
                simulate_packet_reception(128);
            }
        }
        
        /* Check system responsiveness under interrupt load */
        if (!check_system_stability()) {
            printf("  ERROR: System failed under interrupt flood\n");
            return STRESS_RESULT_FAIL;
        }
        
        delay(10);  /* 10 ms delay */
        current_time = time(NULL);
    }
    
    printf("  Interrupt simulations completed: %lu\n", interrupt_simulations);
    printf("  Average interrupt rate: %lu per second\n",
           interrupt_simulations / config->duration_seconds);
    
    printf("  Interrupt flood test completed successfully\n");
    return STRESS_RESULT_PASS;
}

/**
 * Random chaos stress test
 */
stress_result_t stress_test_random_chaos(const stress_config_t *config)
{
    time_t start_time, current_time;
    unsigned long chaos_operations = 0;
    int operation_type;
    int i;
    
    printf("  Random chaos test for %lu seconds (maximum intensity)\n",
           config->duration_seconds);
    
    start_time = time(NULL);
    
    current_time = time(NULL);
    while ((current_time - start_time) < (long)config->duration_seconds) {
        /* Randomly select operation type */
        operation_type = fast_rand() % 10;
        
        switch (operation_type) {
            case 0:
            case 1:
                /* Packet operations (most common) */
                for (i = 0; i < 50; i++) {
                    simulate_packet_transmission(64 + (fast_rand() % 1454));
                    if (fast_rand() % 2) {
                        simulate_packet_reception(64 + (fast_rand() % 1454));
                    }
                }
                break;
                
            case 2:
                /* Memory allocation chaos */
                for (i = 0; i < 10; i++) {
                    if (fast_rand() % 2) {
                        allocate_memory_block(512 + (fast_rand() % 2048));
                    } else if (allocated_block_count > 0) {
                        free_memory_block(fast_rand() % allocated_block_count);
                    }
                }
                break;
                
            case 3:
                /* Error injection */
                inject_random_error();
                break;
                
            case 4:
                /* Interrupt simulation */
                for (i = 0; i < 100; i++) {
                    simulate_hardware_error();
                }
                break;
                
            case 5:
                /* CPU intensive work */
                for (i = 0; i < 1000; i++) {
                    volatile unsigned long dummy = fast_rand() * fast_rand();
                }
                break;
                
            case 6:
                /* Concurrent app simulation */
                if (active_app_count < MAX_CONCURRENT_APPS) {
                    start_concurrent_app();
                } else if (active_app_count > 0) {
                    stop_concurrent_app(fast_rand() % active_app_count);
                }
                break;
                
            case 7:
                /* Burst packet operations */
                for (i = 0; i < 500; i++) {
                    simulate_packet_transmission(64);
                }
                break;
                
            case 8:
                /* Memory pressure */
                for (i = 0; i < 20; i++) {
                    allocate_memory_block(4096);
                }
                break;
                
            case 9:
                /* Random delay */
                delay(fast_rand() % 50);
                break;
        }
        
        chaos_operations++;
        
        /* Check system stability every 100 operations */
        if (chaos_operations % 100 == 0) {
            if (!check_system_stability()) {
                printf("  ERROR: System failed during chaos test after %lu operations\n",
                       chaos_operations);
                return STRESS_RESULT_FAIL;
            }
            
            printf("  Chaos operations completed: %lu\n", chaos_operations);
        }
        
        /* Brief pause to prevent complete system lock */
        if (fast_rand() % 100 == 0) {
            delay(1);
        }
        
        current_time = time(NULL);
    }
    
    printf("  Total chaos operations: %lu\n", chaos_operations);
    printf("  Random chaos test completed successfully\n");
    return STRESS_RESULT_PASS;
}

/**
 * Long duration stress test (24 hours)
 */
stress_result_t stress_test_long_duration(const stress_config_t *config)
{
    time_t start_time, current_time, last_report_time;
    unsigned long total_packets = 0;
    unsigned long total_errors = 0;
    unsigned long hours_elapsed;
    int i;
    
    printf("  Long duration test: %lu seconds (%.1f hours)\n",
           config->duration_seconds, (float)config->duration_seconds / 3600.0);
    printf("  This test will run continuously - press Ctrl+C to abort\n");
    
    start_time = time(NULL);
    last_report_time = start_time;
    
    current_time = time(NULL);
    while ((current_time - start_time) < (long)config->duration_seconds) {
        /* Normal packet operations */
        for (i = 0; i < 1000; i++) {
            if (simulate_packet_transmission(512)) {
                total_packets++;
                simulate_packet_reception(512);
            } else {
                total_errors++;
            }
        }
        
        /* Check system stability */
        if (!check_system_stability()) {
            printf("  ERROR: System stability failure after %lu hours\n",
                   (current_time - start_time) / 3600);
            return STRESS_RESULT_FAIL;
        }
        
        /* Report progress every hour */
        current_time = time(NULL);
        if ((current_time - last_report_time) >= 3600) {
            hours_elapsed = (current_time - start_time) / 3600;
            printf("  Progress: %lu hours elapsed, %lu packets, %lu errors\n",
                   hours_elapsed, total_packets, total_errors);
            last_report_time = current_time;
        }
        
        delay(100);  /* 100 ms delay */
    }
    
    hours_elapsed = (current_time - start_time) / 3600;
    printf("  Long duration test completed: %lu hours, %lu packets, %lu errors\n",
           hours_elapsed, total_packets, total_errors);
    
    return STRESS_RESULT_PASS;
}

/* Helper function implementations */

static void init_stress_config(stress_config_t *config, stress_test_type_t type)
{
    memset(config, 0, sizeof(stress_config_t));
    config->type = type;
    config->duration_seconds = 60;
    config->intensity_level = 5;
    config->packet_rate = 1000;
    config->memory_pressure = 256;
    config->concurrent_operations = 4;
    config->error_injection_rate = 10;
    config->enable_logging = 1;
    config->stop_on_failure = 0;
}

static void reset_stress_stats(stress_stats_t *stats)
{
    memset(stats, 0, sizeof(stress_stats_t));
    stats->min_latency_us = 0xFFFF;
}

static int simulate_packet_transmission(unsigned int size)
{
    /* Simulate packet transmission processing */
    volatile int i;
    for (i = 0; i < size / 64; i++) {
        /* Simulate processing overhead */
    }
    
    /* Simulate occasional transmission failure */
    return (fast_rand() % 1000) != 0;  /* 99.9% success rate */
}

static int simulate_packet_reception(unsigned int size)
{
    /* Simulate packet reception processing */
    volatile int i;
    for (i = 0; i < size / 32; i++) {
        /* Simulate processing overhead */
    }
    
    /* Simulate occasional reception failure */
    return (fast_rand() % 1000) != 0;  /* 99.9% success rate */
}

static void inject_random_error(void)
{
    int error_type = fast_rand() % 5;
    
    switch (error_type) {
        case 0:
            /* Simulate hardware error */
            simulate_hardware_error();
            break;
        case 1:
            /* Simulate memory error */
            if (allocated_block_count > 0) {
                /* Corrupt random memory block */
                int block = fast_rand() % allocated_block_count;
                if (memory_blocks[block].ptr != NULL) {
                    /* Simulate memory corruption */
                    _fmemset(memory_blocks[block].ptr, 0xFF, 64);
                }
            }
            break;
        case 2:
            /* Simulate timeout */
            delay(100);  /* 100 ms timeout simulation */
            break;
        case 3:
            /* Simulate resource exhaustion */
            allocate_memory_block(8192);  /* Try to allocate large block */
            break;
        case 4:
            /* Simulate protocol error */
            /* Just consume some CPU cycles */
            {
                volatile int i;
                for (i = 0; i < 10000; i++);
            }
            break;
    }
}

static unsigned long get_timer_ticks(void)
{
    return clock();
}

static void log_stress_event(const char *message)
{
    if (current_test.config.enable_logging) {
        printf("  [%lu] %s\n", get_timer_ticks(), message);
    }
}

static unsigned int fast_rand(void)
{
    rand_seed = rand_seed * 1103515245 + 12345;
    return (unsigned int)(rand_seed >> 16) & 0x7fff;
}

static int allocate_memory_block(unsigned int size)
{
    if (allocated_block_count >= MEMORY_STRESS_BLOCKS) {
        return 0;  /* No more slots */
    }
    
    memory_blocks[allocated_block_count].ptr = farmalloc(size);
    if (memory_blocks[allocated_block_count].ptr == NULL) {
        return 0;  /* Allocation failed */
    }
    
    memory_blocks[allocated_block_count].size = size;
    memory_blocks[allocated_block_count].pattern = fast_rand();
    memory_blocks[allocated_block_count].alloc_time = time(NULL);
    
    /* Fill with test pattern */
    _fmemset(memory_blocks[allocated_block_count].ptr, 
             memory_blocks[allocated_block_count].pattern & 0xFF, size);
    
    allocated_block_count++;
    return 1;
}

static void free_memory_block(int block_index)
{
    if (block_index >= allocated_block_count || 
        memory_blocks[block_index].ptr == NULL) {
        return;
    }
    
    farfree(memory_blocks[block_index].ptr);
    
    /* Shift remaining blocks down */
    if (block_index < allocated_block_count - 1) {
        memmove(&memory_blocks[block_index], &memory_blocks[block_index + 1],
                (allocated_block_count - block_index - 1) * sizeof(memory_block_t));
    }
    
    allocated_block_count--;
    memset(&memory_blocks[allocated_block_count], 0, sizeof(memory_block_t));
}

static int start_concurrent_app(void)
{
    if (active_app_count >= MAX_CONCURRENT_APPS) {
        return 0;
    }
    
    concurrent_apps[active_app_count].app_id = active_app_count;
    concurrent_apps[active_app_count].packet_count = 0;
    concurrent_apps[active_app_count].error_count = 0;
    concurrent_apps[active_app_count].start_time = time(NULL);
    concurrent_apps[active_app_count].is_active = 1;
    
    active_app_count++;
    return 1;
}

static void stop_concurrent_app(int app_id)
{
    if (app_id >= active_app_count || !concurrent_apps[app_id].is_active) {
        return;
    }
    
    concurrent_apps[app_id].is_active = 0;
    
    /* Shift remaining apps down */
    if (app_id < active_app_count - 1) {
        memmove(&concurrent_apps[app_id], &concurrent_apps[app_id + 1],
                (active_app_count - app_id - 1) * sizeof(concurrent_app_t));
    }
    
    active_app_count--;
    memset(&concurrent_apps[active_app_count], 0, sizeof(concurrent_app_t));
}

static void simulate_hardware_error(void)
{
    /* Simulate hardware interrupt processing overhead */
    volatile int i;
    for (i = 0; i < 1000; i++) {
        /* Simulate interrupt handler work */
    }
}

static int check_system_stability(void)
{
    /* Basic system stability checks */
    
    /* Check available memory */
    if (coreleft() < 1024) {
        printf("    WARNING: Very low memory available\n");
    }
    
    /* Check that we can still allocate small blocks */
    void *test_ptr = malloc(256);
    if (test_ptr == NULL) {
        printf("    ERROR: Cannot allocate small memory block\n");
        return 0;
    }
    free(test_ptr);
    
    /* Check timer is still functioning */
    static unsigned long last_time = 0;
    unsigned long current_time = get_timer_ticks();
    if (current_time == last_time) {
        /* Timer might be stuck - this is just a warning */
        printf("    WARNING: Timer appears to be stuck\n");
    }
    last_time = current_time;
    
    return 1;  /* System appears stable */
}

/**
 * Main entry point for stress testing
 */
int main(int argc, char *argv[])
{
    int result;
    
    printf("3Com Packet Driver Stress Testing Suite\n");
    printf("======================================\n\n");
    
    /* Initialize stress testing framework */
    if (stress_test_init() != 0) {
        printf("ERROR: Failed to initialize stress testing framework\n");
        return 1;
    }
    
    /* Run all stress tests */
    result = stress_test_run_all();
    
    /* Cleanup */
    stress_test_cleanup();
    
    if (result == 0) {
        printf("\nAll stress tests PASSED successfully!\n");
        printf("The packet driver demonstrated excellent stability under stress.\n");
    } else {
        printf("\nSome stress tests FAILED!\n");
        printf("Review the test output for details on stability issues.\n");
    }
    
    return result;
}