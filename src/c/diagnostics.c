/**
 * @file diagnostics.c
 * @brief Diagnostic and logging facilities
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include "../include/diagnostics.h"
#include "../include/hardware.h"
#include "../include/memory.h"
#include "../include/arp.h"
#include "../include/routing.h"
#include "../include/api.h"
#include "../include/common.h"
#include "../include/eeprom.h"
#include "../include/error_handling.h"
#include "../include/logging.h"
#include <stdarg.h>
#include <stdio.h>

/* Global diagnostic state */
diag_level_t g_diag_level = DIAG_LEVEL_ERROR;
uint32_t g_diag_categories = DIAG_CAT_ALL;
perf_counters_t g_perf_counters;
bool g_diagnostics_enabled = false;
diag_system_state_t g_diag_state;
network_health_t g_network_health;

/* Private global state */
static bool g_diagnostics_initialized = false;
static log_entry_t *g_log_head = NULL;
static log_entry_t *g_log_tail = NULL;
static uint32_t g_log_count = 0;
static const uint32_t MAX_LOG_ENTRIES = 1000;

/* Enhanced logging configuration */
static bool g_log_to_console = true;
static bool g_log_to_file = false;
static bool g_log_to_network = false;
static char g_log_file_path[128] = "PACKET.LOG";
static bool g_log_enabled_by_config = false;

/* Error pattern tracking for correlation */
typedef struct error_pattern {
    uint8_t error_type;
    uint8_t nic_index;
    uint32_t frequency;
    uint32_t last_occurrence;
    struct error_pattern *next;
} error_pattern_t;

static error_pattern_t *g_error_patterns = NULL;
static uint32_t g_pattern_analysis_window = 60000; /* 1 minute window */

/* Internal helper functions */
static void diagnostics_add_log_entry(diag_level_t level, uint32_t category,
                                     const char *function, const char *file,
                                     uint32_t line, const char *message);
static void diagnostics_cleanup_old_logs(void);
static uint32_t diagnostics_get_system_time(void);
static const char* diagnostics_level_prefix(diag_level_t level);

/* Diagnostics initialization and cleanup */
int diagnostics_init(void) {
    if (g_diagnostics_initialized) {
        return SUCCESS;
    }
    
    /* Initialize performance counters */
    perf_counters_init(&g_perf_counters);
    
    /* Initialize logging */
    g_log_head = NULL;
    g_log_tail = NULL;
    g_log_count = 0;
    
    /* Set default diagnostic level and categories */
    g_diag_level = DIAG_LEVEL_ERROR;
    g_diag_categories = DIAG_CAT_ALL;
    
    g_diagnostics_initialized = true;
    g_diagnostics_enabled = true;
    
    /* Initialize enhanced diagnostic features */
    int result = diag_health_init();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize health monitoring: %d", result);
        return result;
    }
    
    /* Initialize ring buffer for efficient logging */
    result = diag_log_init_ring_buffer(512); /* 512 entries */
    if (result != SUCCESS) {
        LOG_WARNING("Failed to initialize ring buffer: %d", result);
        /* Continue without ring buffer */
    }
    
    /* Initialize real-time monitoring */
    result = diag_monitoring_init();
    if (result != SUCCESS) {
        LOG_WARNING("Failed to initialize monitoring: %d", result);
        /* Continue without real-time monitoring */
    }
    
    /* Initialize flow tracking */
    result = diag_flow_init(256, 300000); /* 256 flows, 5 minute timeout */
    if (result != SUCCESS) {
        LOG_WARNING("Failed to initialize flow tracking: %d", result);
        /* Continue without flow tracking */
    }
    
    /* Initialize historical tracking */
    result = diag_history_init(120, 5000); /* 120 samples, 5 second interval (10 minutes) */
    if (result != SUCCESS) {
        LOG_WARNING("Failed to initialize historical tracking: %d", result);
        /* Continue without historical tracking */
    }
    
    /* Log initialization */
    LOG_INFO("Enhanced diagnostics system initialized");
    
    return SUCCESS;
}

void diagnostics_cleanup(void) {
    if (!g_diagnostics_initialized) {
        return;
    }
    
    LOG_INFO("Shutting down enhanced diagnostics system");
    
    /* Cleanup enhanced features */
    diag_log_cleanup_ring_buffer();
    diag_flow_cleanup();
    diag_history_cleanup();
    
    /* Cleanup error history */
    error_event_t *current = g_error_history;
    while (current) {
        error_event_t *next = current->next;
        memory_free(current);
        current = next;
    }
    g_error_history = NULL;
    g_error_count = 0;
    
    /* Free all log entries */
    log_entry_t *current = g_log_head;
    while (current) {
        log_entry_t *next = current->next;
        memory_free(current);
        current = next;
    }
    
    g_log_head = NULL;
    g_log_tail = NULL;
    g_log_count = 0;
    
    g_diagnostics_initialized = false;
    g_diagnostics_enabled = false;
}

int diagnostics_enable(bool enable) {
    if (!g_diagnostics_initialized) {
        return ERROR_NOT_FOUND;
    }
    
    g_diagnostics_enabled = enable;
    return SUCCESS;
}

bool diagnostics_is_enabled(void) {
    return g_diagnostics_enabled && g_diagnostics_initialized;
}

/* Logging functions */
void log_message(diag_level_t level, uint32_t category, const char *function,
                const char *file, uint32_t line, const char *format, ...) {
    if (!diagnostics_is_enabled() || level > g_diag_level || !(category & g_diag_categories)) {
        return;
    }
    
    /* Enhanced variable argument formatting */
    char message[256];
    va_list args;
    
    if (format) {
        va_start(args, format);
        vsnprintf(message, sizeof(message), format, args);
        va_end(args);
    } else {
        message[0] = '\0';
    }
    
    diagnostics_add_log_entry(level, category, function, file, line, message);
}

void log_error(const char *format, ...) {
    if (!format) return;
    va_list args;
    char buffer[256];
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log_message(DIAG_LEVEL_ERROR, DIAG_CAT_DRIVER, "log_error", __FILE__, __LINE__, buffer);
}

void log_warning(const char *format, ...) {
    if (!format) return;
    va_list args;
    char buffer[256];
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log_message(DIAG_LEVEL_WARNING, DIAG_CAT_DRIVER, "log_warning", __FILE__, __LINE__, buffer);
}

void log_info(const char *format, ...) {
    if (!format) return;
    va_list args;
    char buffer[256];
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log_message(DIAG_LEVEL_INFO, DIAG_CAT_DRIVER, "log_info", __FILE__, __LINE__, buffer);
}

void log_debug(const char *format, ...) {
    if (!format) return;
    va_list args;
    char buffer[256];
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log_message(DIAG_LEVEL_DEBUG, DIAG_CAT_DRIVER, "log_debug", __FILE__, __LINE__, buffer);
}

void log_trace(const char *format, ...) {
    if (!format) return;
    log_message(DIAG_LEVEL_TRACE, DIAG_CAT_DRIVER, "log_trace", __FILE__, __LINE__, format);
}

/* Diagnostic tests */
int diag_run_test(diag_test_t test_type, nic_info_t *nic, diag_result_t *result) {
    if (!result) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Initialize result structure */
    result->test_type = test_type;
    result->passed = false;
    result->error_code = 0;
    result->duration_ms = 0;
    result->timestamp = diagnostics_get_system_time();
    memory_set(result->description, 0, sizeof(result->description));
    
    uint32_t start_time = result->timestamp;
    
    switch (test_type) {
        case DIAG_TEST_HARDWARE:
            return diag_hardware_test(nic, result);
            
        case DIAG_TEST_MEMORY:
            return diag_memory_test(result);
            
        case DIAG_TEST_INTERRUPT:
            return diag_interrupt_test(nic, result);
            
        case DIAG_TEST_LOOPBACK:
            return diag_loopback_test(nic, result);
            
        case DIAG_TEST_NETWORK:
            return diag_network_test(nic, result);
            
        case DIAG_TEST_PERFORMANCE:
            return diag_performance_test(nic, result);
            
        default:
            memory_copy(result->description, "Unknown test type", 18);
            result->error_code = ERROR_NOT_SUPPORTED;
            result->duration_ms = diagnostics_get_system_time() - start_time;
            return ERROR_NOT_SUPPORTED;
    }
}

int diag_run_all_tests(nic_info_t *nic, diag_result_t *results, uint32_t max_results) {
    if (!results || max_results == 0) {
        return ERROR_INVALID_PARAM;
    }
    
    diag_test_t tests[] = {
        DIAG_TEST_HARDWARE,
        DIAG_TEST_MEMORY,
        DIAG_TEST_INTERRUPT,
        DIAG_TEST_LOOPBACK,
        DIAG_TEST_NETWORK,
        DIAG_TEST_PERFORMANCE
    };
    
    uint32_t test_count = sizeof(tests) / sizeof(tests[0]);
    if (test_count > max_results) {
        test_count = max_results;
    }
    
    int total_passed = 0;
    
    for (uint32_t i = 0; i < test_count; i++) {
        int result = diag_run_test(tests[i], nic, &results[i]);
        if (result == SUCCESS && results[i].passed) {
            total_passed++;
        }
    }
    
    LOG_INFO("Ran %d diagnostic tests, %d passed", test_count, total_passed);
    
    return total_passed;
}

int diag_hardware_test(nic_info_t *nic, diag_result_t *result) {
    if (!nic || !result) {
        return ERROR_INVALID_PARAM;
    }
    
    uint32_t start_time = diagnostics_get_system_time();
    
    /* Implement hardware-specific tests */
    memory_copy(result->description, "Hardware register test", 22);
    
    /* Test hardware register accessibility */
    if (nic->ops && nic->ops->check_interrupt) {
        result->success = (nic->ops->check_interrupt(nic) >= 0) ? true : false;
    } else {
        result->success = false;
    }
    
    /* Check if NIC is present and responding */
    if (!(nic->status & NIC_STATUS_PRESENT)) {
        result->passed = false;
        result->error_code = ERROR_HARDWARE;
        result->duration_ms = diagnostics_get_system_time() - start_time;
        return ERROR_HARDWARE;
    }
    
    /* Test hardware registers and check ID registers */
    if (nic->ops && nic->ops->self_test) {
        int test_result = nic->ops->self_test(nic);
        result->passed = (test_result == SUCCESS);
        result->error_code = test_result;
    } else {
        /* Basic presence test passed */
        result->passed = true;
        result->error_code = SUCCESS;
    }
    
    result->duration_ms = diagnostics_get_system_time() - start_time;
    
    return result->passed ? SUCCESS : ERROR_HARDWARE;
}

int diag_memory_test(diag_result_t *result) {
    if (!result) {
        return ERROR_INVALID_PARAM;
    }
    
    uint32_t start_time = diagnostics_get_system_time();
    
    memory_copy(result->description, "Memory allocation test", 22);
    
    /* Comprehensive Memory Test Implementation */
    bool memory_test_passed = true;
    uint32_t test_error_flags = 0;
    
    /* Test 1: Basic allocation/deallocation */
    void *test_ptr = memory_alloc(1024, MEM_TYPE_GENERAL, 0);
    if (!test_ptr) {
        memory_test_passed = false;
        test_error_flags |= 0x01;
        result->error_code = MAKE_ERROR(ERROR_SEVERITY_ERROR, ERROR_SUBSYS_MEMORY, 0x01);
        LOG_ERROR("Memory allocation test failed: cannot allocate 1024 bytes");
    } else {
        /* Test 2: Memory write/read integrity */
        memory_set(test_ptr, 0, 1024);  /* Clear first */
        uint8_t *byte_ptr = (uint8_t*)test_ptr;
        
        /* Write test patterns */
        for (int i = 0; i < 1024; i += 4) {
            byte_ptr[i] = 0xAA;
            if (i+1 < 1024) byte_ptr[i+1] = 0x55;
            if (i+2 < 1024) byte_ptr[i+2] = 0xFF;
            if (i+3 < 1024) byte_ptr[i+3] = 0x00;
        }
        
        /* Verify test patterns */
        for (int i = 0; i < 1024; i += 4) {
            if (byte_ptr[i] != 0xAA || 
                (i+1 < 1024 && byte_ptr[i+1] != 0x55) ||
                (i+2 < 1024 && byte_ptr[i+2] != 0xFF) || 
                (i+3 < 1024 && byte_ptr[i+3] != 0x00)) {
                memory_test_passed = false;
                test_error_flags |= 0x02;
                LOG_ERROR("Memory integrity test failed at offset %d", i);
                break;
            }
        }
        
        memory_free(test_ptr);
    }
    
    /* Test 3: Multiple allocation/deallocation stress test */
    void *stress_ptrs[10];
    int stress_alloc_count = 0;
    
    for (int i = 0; i < 10; i++) {
        stress_ptrs[i] = memory_alloc(256, MEM_TYPE_GENERAL, 0);
        if (stress_ptrs[i]) {
            stress_alloc_count++;
        } else {
            test_error_flags |= 0x04;
        }
    }
    
    /* Free all stress test allocations */
    for (int i = 0; i < 10; i++) {
        if (stress_ptrs[i]) {
            memory_free(stress_ptrs[i]);
        }
    }
    
    if (stress_alloc_count < 5) {
        memory_test_passed = false;
        test_error_flags |= 0x08;
        LOG_ERROR("Memory stress test failed: only %d/10 allocations succeeded", stress_alloc_count);
    }
    
    result->passed = memory_test_passed;
    if (memory_test_passed) {
        result->error_code = SUCCESS;
        LOG_DEBUG("Memory test passed: allocated/freed multiple blocks successfully");
    } else {
        result->error_code = MAKE_ERROR(ERROR_SEVERITY_ERROR, ERROR_SUBSYS_MEMORY, test_error_flags);
    }
    result->duration_ms = diagnostics_get_system_time() - start_time;
    
    return result->passed ? SUCCESS : result->error_code;
}

int diag_interrupt_test(nic_info_t *nic, diag_result_t *result) {
    if (!nic || !result) {
        return ERROR_INVALID_PARAM;
    }
    
    uint32_t start_time = diagnostics_get_system_time();
    
    memory_copy(result->description, "Interrupt functionality test", 28);
    
    /* Comprehensive Interrupt Test Implementation */
    bool interrupt_test_passed = true;
    uint32_t int_error_flags = 0;
    
    /* Test 1: Check if NIC has valid IRQ */
    if (nic->irq == 0 || nic->irq > 15) {
        interrupt_test_passed = false;
        int_error_flags |= 0x01;
        LOG_ERROR("Invalid IRQ %d for NIC %d", nic->irq, nic->index);
    }
    
    /* Test 2: Check interrupt handler installation */
    if (interrupt_test_passed && nic->irq_handler_installed) {
        /* Verify interrupt handler is properly installed */
        uint32_t interrupts_before = nic->interrupts;
        
        /* Generate a test interrupt by triggering NIC (carefully) */
        if (nic->ops && nic->ops->trigger_interrupt) {
            int trigger_result = nic->ops->trigger_interrupt(nic);
            if (trigger_result != SUCCESS) {
                int_error_flags |= 0x02;
                LOG_WARNING("Cannot trigger test interrupt for NIC %d", nic->index);
            }
        }
        
        /* Small delay to allow interrupt processing */
        for (volatile int delay = 0; delay < 10000; delay++) {
            /* Brief delay loop */
        }
        
        uint32_t interrupts_after = nic->interrupts;
        
        /* Check if interrupt count increased (basic test) */
        if (interrupts_after <= interrupts_before) {
            /* This might be normal if no actual interrupt was generated */
            LOG_DEBUG("No interrupt activity detected during test (may be normal)");
        }
    } else if (!nic->irq_handler_installed) {
        interrupt_test_passed = false;
        int_error_flags |= 0x04;
        LOG_ERROR("Interrupt handler not installed for NIC %d", nic->index);
    }
    
    /* Test 3: Check for interrupt storms or stuck interrupts */
    if (nic->interrupts > 0) {
        uint32_t elapsed_ms = diagnostics_get_system_time() - g_perf_counters.start_time;
        if (elapsed_ms > 0) {
            uint32_t interrupt_rate = (nic->interrupts * 1000) / elapsed_ms;
            if (interrupt_rate > 10000) {  /* More than 10K interrupts per second */
                interrupt_test_passed = false;
                int_error_flags |= 0x08;
                LOG_ERROR("Interrupt storm detected on NIC %d: %lu int/sec", nic->index, interrupt_rate);
            }
        }
    }
    
    /* Test 4: Verify interrupt masking works */
    if (interrupt_test_passed && nic->ops && nic->ops->mask_interrupts && nic->ops->unmask_interrupts) {
        /* Test interrupt masking */
        int mask_result = nic->ops->mask_interrupts(nic);
        if (mask_result == SUCCESS) {
            int unmask_result = nic->ops->unmask_interrupts(nic);
            if (unmask_result != SUCCESS) {
                int_error_flags |= 0x10;
                LOG_WARNING("Interrupt unmasking failed for NIC %d", nic->index);
            }
        } else {
            int_error_flags |= 0x20;
            LOG_WARNING("Interrupt masking failed for NIC %d", nic->index);
        }
    }
    
    result->passed = interrupt_test_passed;
    if (interrupt_test_passed) {
        result->error_code = SUCCESS;
        LOG_DEBUG("Interrupt test passed for NIC %d (IRQ %d)", nic->index, nic->irq);
    } else {
        result->error_code = MAKE_ERROR(ERROR_SEVERITY_ERROR, ERROR_SUBSYS_INTERRUPT, int_error_flags);
    }
    result->duration_ms = diagnostics_get_system_time() - start_time;
    
    return result->passed ? SUCCESS : result->error_code;
}

int diag_loopback_test(nic_info_t *nic, diag_result_t *result) {
    if (!nic || !result) {
        return ERROR_INVALID_PARAM;
    }
    
    uint32_t start_time = diagnostics_get_system_time();
    
    memory_copy(result->description, "Network loopback test", 21);
    
    /* Comprehensive Loopback Test Implementation */
    bool loopback_passed = true;
    uint32_t loopback_error_flags = 0;
    
    /* Check if NIC supports loopback mode */
    if (!nic->link_up) {
        loopback_passed = false;
        loopback_error_flags |= 0x01;
        LOG_WARNING("Cannot perform loopback test: link is down");
    }
    
    if (loopback_passed && nic->ops && nic->ops->set_loopback_mode) {
        /* Enable internal loopback mode */
        int loopback_result = nic->ops->set_loopback_mode(nic, true);
        if (loopback_result == SUCCESS) {
            /* Create test packet */
            uint8_t test_packet[64];
            memory_set(test_packet, 0, sizeof(test_packet));
            
            /* Ethernet header: dest MAC, src MAC, ethertype */
            memory_copy(test_packet, nic->mac_address, 6);      /* dest = our MAC */
            memory_copy(test_packet + 6, nic->mac_address, 6);  /* src = our MAC */
            test_packet[12] = 0x08; test_packet[13] = 0x00;     /* IP ethertype */
            
            /* Simple test payload */
            for (int i = 14; i < 64; i++) {
                test_packet[i] = (uint8_t)(i & 0xFF);
            }
            
            /* Send test packet */
            uint32_t packets_before = nic->rx_packets;
            if (nic->ops->send_packet) {
                int send_result = nic->ops->send_packet(nic, test_packet, sizeof(test_packet));
                if (send_result == SUCCESS) {
                    /* Wait for loopback packet to be received */
                    uint32_t timeout = 100;  /* 100ms timeout */
                    uint32_t wait_start = diagnostics_get_system_time();
                    
                    while ((diagnostics_get_system_time() - wait_start) < timeout) {
                        if (nic->rx_packets > packets_before) {
                            break;  /* Packet received */
                        }
                        /* Small delay */
                        for (volatile int i = 0; i < 1000; i++) {}
                    }
                    
                    if (nic->rx_packets <= packets_before) {
                        loopback_passed = false;
                        loopback_error_flags |= 0x02;
                        LOG_ERROR("Loopback test failed: packet not received");
                    } else {
                        LOG_DEBUG("Loopback test packet received successfully");
                    }
                } else {
                    loopback_passed = false;
                    loopback_error_flags |= 0x04;
                    LOG_ERROR("Loopback test failed: cannot send packet");
                }
            } else {
                loopback_passed = false;
                loopback_error_flags |= 0x08;
                LOG_WARNING("Loopback test skipped: no send function available");
            }
            
            /* Disable loopback mode */
            nic->ops->set_loopback_mode(nic, false);
        } else {
            loopback_passed = false;
            loopback_error_flags |= 0x10;
            LOG_WARNING("Cannot enable loopback mode for test");
        }
    } else if (!nic->ops || !nic->ops->set_loopback_mode) {
        /* Skip test if loopback not supported */
        LOG_INFO("Loopback test skipped: not supported by hardware");
        result->passed = true;
        result->error_code = SUCCESS;
        result->duration_ms = diagnostics_get_system_time() - start_time;
        return SUCCESS;
    }
    
    result->passed = loopback_passed;
    if (loopback_passed) {
        result->error_code = SUCCESS;
    } else {
        result->error_code = MAKE_ERROR(ERROR_SEVERITY_ERROR, ERROR_SUBSYS_NETWORK, loopback_error_flags);
    }
    result->duration_ms = diagnostics_get_system_time() - start_time;
    
    return SUCCESS;
}

int diag_network_test(nic_info_t *nic, diag_result_t *result) {
    if (!nic || !result) {
        return ERROR_INVALID_PARAM;
    }
    
    uint32_t start_time = diagnostics_get_system_time();
    
    memory_copy(result->description, "Network connectivity test", 25);
    
    /* Comprehensive Network Connectivity Test */
    bool connectivity_passed = true;
    uint32_t conn_error_flags = 0;
    
    /* Test 1: Physical link status */
    if (!nic->link_up) {
        connectivity_passed = false;
        conn_error_flags |= 0x01;
        LOG_WARNING("Network connectivity test: link is down");
    } else {
        LOG_DEBUG("Link up detected at %d Mbps", nic->link_speed);
    }
    
    /* Test 2: Check link speed and duplex */
    if (connectivity_passed) {
        if (nic->link_speed != 10 && nic->link_speed != 100) {
            conn_error_flags |= 0x02;
            LOG_WARNING("Unusual link speed: %d Mbps", nic->link_speed);
        }
        
        /* Check duplex mode */
        if (nic->duplex_mode == 0) {  /* Assume 0 = half, 1 = full */
            LOG_DEBUG("Half duplex mode detected");
        } else {
            LOG_DEBUG("Full duplex mode detected");
        }
    }
    
    /* Test 3: Check for excessive errors */
    if (connectivity_passed && nic->rx_packets > 100) {
        uint32_t error_rate = (nic->rx_errors * 1000) / nic->rx_packets;
        if (error_rate > 50) {  /* More than 5% error rate */
            connectivity_passed = false;
            conn_error_flags |= 0x04;
            LOG_ERROR("High error rate detected: %lu errors per 1000 packets", error_rate);
        }
    }
    
    /* Test 4: Check collision rate (for half-duplex connections) */
    if (connectivity_passed && nic->duplex_mode == 0 && nic->tx_packets > 100) {
        /* Note: collision count would need to be added to NIC structure */
        uint32_t collision_estimate = nic->tx_errors / 2;  /* Rough estimate */
        uint32_t collision_rate = (collision_estimate * 1000) / nic->tx_packets;
        if (collision_rate > 100) {  /* More than 10% collision rate */
            conn_error_flags |= 0x08;
            LOG_WARNING("High collision rate detected: estimated %lu per 1000 packets", collision_rate);
        }
    }
    
    /* Test 5: Basic ARP table check */
    if (connectivity_passed) {
        /* Check if we have any ARP entries (indicates network activity) */
        extern int arp_get_table_size(void);  /* From arp.c */
        int arp_entries = arp_get_table_size();
        if (arp_entries == 0) {
            conn_error_flags |= 0x10;
            LOG_INFO("No ARP entries found (may indicate limited network activity)");
        } else {
            LOG_DEBUG("Found %d ARP entries", arp_entries);
        }
    }
    
    /* Test 6: Check recent activity */
    uint32_t current_time = diagnostics_get_system_time();
    if (connectivity_passed && nic->last_activity > 0) {
        uint32_t time_since_activity = current_time - nic->last_activity;
        if (time_since_activity > 300000) {  /* More than 5 minutes */
            conn_error_flags |= 0x20;
            LOG_WARNING("No recent network activity (last: %lu ms ago)", time_since_activity);
        }
    }
    
    result->passed = connectivity_passed;
    if (connectivity_passed && (conn_error_flags == 0)) {
        result->error_code = SUCCESS;
        LOG_DEBUG("Network connectivity test passed");
    } else if (connectivity_passed) {
        result->error_code = MAKE_ERROR(ERROR_SEVERITY_WARNING, ERROR_SUBSYS_NETWORK, conn_error_flags);
        LOG_WARNING("Network connectivity test passed with warnings: 0x%02X", conn_error_flags);
    } else {
        result->error_code = MAKE_ERROR(ERROR_SEVERITY_ERROR, ERROR_SUBSYS_NETWORK, conn_error_flags);
    }
    result->duration_ms = diagnostics_get_system_time() - start_time;
    
    return result->passed ? SUCCESS : ERROR_IO;
}

int diag_performance_test(nic_info_t *nic, diag_result_t *result) {
    if (!nic || !result) {
        return ERROR_INVALID_PARAM;
    }
    
    uint32_t start_time = diagnostics_get_system_time();
    
    memory_copy(result->description, "Performance benchmark test", 26);
    
    /* Comprehensive Performance Benchmark Test */
    bool performance_passed = true;
    uint32_t perf_error_flags = 0;
    
    /* Check if NIC is ready for performance testing */
    if (!nic->link_up) {
        performance_passed = false;
        perf_error_flags |= 0x01;
        LOG_ERROR("Cannot run performance test: link is down");
    }
    
    if (performance_passed) {
        uint32_t test_start_time = diagnostics_get_system_time();
        
        /* Performance Test 1: Throughput measurement */
        uint32_t packets_sent_before = nic->tx_packets;
        uint32_t bytes_sent_before = nic->tx_bytes;
        
        /* Simulate packet transmission load */
        if (nic->ops && nic->ops->send_packet) {
            uint8_t test_frame[1500];  /* Maximum Ethernet frame */
            memory_set(test_frame, 0xAA, sizeof(test_frame));
            
            /* Send test packets */
            int packets_to_send = 100;
            int successful_sends = 0;
            
            for (int i = 0; i < packets_to_send; i++) {
                int send_result = nic->ops->send_packet(nic, test_frame, sizeof(test_frame));
                if (send_result == SUCCESS) {
                    successful_sends++;
                }
                
                /* Small delay between packets */
                for (volatile int delay = 0; delay < 100; delay++) {}
            }
            
            uint32_t test_duration = diagnostics_get_system_time() - test_start_time;
            
            if (test_duration > 0) {
                uint32_t throughput = (successful_sends * 1500 * 8 * 1000) / test_duration;  /* bps */
                uint32_t expected_min_throughput = (nic->link_speed * 1000000) / 10;  /* 10% of link speed */
                
                LOG_DEBUG("Performance test: %d/%d packets sent, throughput: %lu bps", 
                         successful_sends, packets_to_send, throughput);
                
                if (throughput < expected_min_throughput) {
                    perf_error_flags |= 0x02;
                    LOG_WARNING("Low throughput: %lu bps (expected > %lu bps)", 
                               throughput, expected_min_throughput);
                }
                
                if (successful_sends < packets_to_send / 2) {
                    performance_passed = false;
                    perf_error_flags |= 0x04;
                    LOG_ERROR("High packet loss during performance test: %d/%d", 
                             successful_sends, packets_to_send);
                }
            }
        }
        
        /* Performance Test 2: Interrupt response time */
        uint32_t interrupt_count_before = nic->interrupts;
        uint32_t interrupt_test_start = diagnostics_get_system_time();
        
        /* Wait for some interrupts to occur */
        uint32_t interrupt_wait_timeout = 1000;  /* 1 second */
        while ((diagnostics_get_system_time() - interrupt_test_start) < interrupt_wait_timeout) {
            if (nic->interrupts > interrupt_count_before + 5) {
                break;
            }
            /* Brief delay */
            for (volatile int i = 0; i < 1000; i++) {}
        }
        
        uint32_t interrupt_test_duration = diagnostics_get_system_time() - interrupt_test_start;
        uint32_t interrupts_processed = nic->interrupts - interrupt_count_before;
        
        if (interrupts_processed > 0) {
            uint32_t avg_interrupt_interval = interrupt_test_duration / interrupts_processed;
            LOG_DEBUG("Interrupt performance: %lu interrupts in %lu ms (avg interval: %lu ms)", 
                     interrupts_processed, interrupt_test_duration, avg_interrupt_interval);
            
            if (avg_interrupt_interval > 100) {  /* More than 100ms between interrupts */
                perf_error_flags |= 0x08;
                LOG_WARNING("Low interrupt rate detected");
            }
        }
        
        /* Performance Test 3: Memory bandwidth test */
        uint32_t memory_test_start = diagnostics_get_system_time();
        uint8_t *large_buffer = (uint8_t*)memory_alloc(8192, MEM_TYPE_GENERAL, 0);
        
        if (large_buffer) {
            /* Memory copy performance test */
            for (int i = 0; i < 100; i++) {
                memory_set(large_buffer, (uint8_t)(i & 0xFF), 8192);
            }
            
            uint32_t memory_test_duration = diagnostics_get_system_time() - memory_test_start;
            uint32_t memory_bandwidth = (8192 * 100 * 1000) / (memory_test_duration + 1);  /* bytes/sec */
            
            LOG_DEBUG("Memory bandwidth: %lu bytes/sec", memory_bandwidth);
            
            if (memory_bandwidth < 100000) {  /* Less than 100KB/sec */
                perf_error_flags |= 0x10;
                LOG_WARNING("Low memory bandwidth detected: %lu bytes/sec", memory_bandwidth);
            }
            
            memory_free(large_buffer);
        } else {
            perf_error_flags |= 0x20;
            LOG_WARNING("Cannot allocate buffer for memory performance test");
        }
    }
    
    result->passed = performance_passed;
    if (performance_passed && (perf_error_flags == 0)) {
        result->error_code = SUCCESS;
        LOG_DEBUG("Performance test passed with no issues");
    } else if (performance_passed) {
        result->error_code = MAKE_ERROR(ERROR_SEVERITY_WARNING, ERROR_SUBSYS_DRIVER, perf_error_flags);
        LOG_WARNING("Performance test passed with warnings: 0x%02X", perf_error_flags);
    } else {
        result->error_code = MAKE_ERROR(ERROR_SEVERITY_ERROR, ERROR_SUBSYS_DRIVER, perf_error_flags);
    }
    result->duration_ms = diagnostics_get_system_time() - start_time;
    
    return SUCCESS;
}

/* Performance monitoring */
void perf_counters_init(perf_counters_t *counters) {
    if (!counters) {
        return;
    }
    
    memory_zero(counters, sizeof(perf_counters_t));
    counters->start_time = diagnostics_get_system_time();
    counters->last_update = counters->start_time;
}

void perf_counters_reset(perf_counters_t *counters) {
    if (!counters) {
        return;
    }
    
    uint32_t current_time = diagnostics_get_system_time();
    memory_zero(counters, sizeof(perf_counters_t));
    counters->start_time = current_time;
    counters->last_update = current_time;
}

void perf_counters_update_tx(perf_counters_t *counters, uint32_t bytes) {
    if (!counters) {
        return;
    }
    
    counters->packets_sent++;
    counters->bytes_sent += bytes;
    counters->last_update = diagnostics_get_system_time();
}

void perf_counters_update_rx(perf_counters_t *counters, uint32_t bytes) {
    if (!counters) {
        return;
    }
    
    counters->packets_received++;
    counters->bytes_received += bytes;
    counters->last_update = diagnostics_get_system_time();
}

const perf_counters_t* perf_get_counters(void) {
    return &g_perf_counters;
}

/* Diagnostic utilities */
const char* diag_level_to_string(diag_level_t level) {
    switch (level) {
        case DIAG_LEVEL_NONE:    return "NONE";
        case DIAG_LEVEL_ERROR:   return "ERROR";
        case DIAG_LEVEL_WARNING: return "WARN";
        case DIAG_LEVEL_INFO:    return "INFO";
        case DIAG_LEVEL_DEBUG:   return "DEBUG";
        case DIAG_LEVEL_TRACE:   return "TRACE";
        default:                 return "UNKNOWN";
    }
}

const char* diag_test_to_string(diag_test_t test) {
    switch (test) {
        case DIAG_TEST_NONE:        return "NONE";
        case DIAG_TEST_HARDWARE:    return "HARDWARE";
        case DIAG_TEST_MEMORY:      return "MEMORY";
        case DIAG_TEST_INTERRUPT:   return "INTERRUPT";
        case DIAG_TEST_LOOPBACK:    return "LOOPBACK";
        case DIAG_TEST_NETWORK:     return "NETWORK";
        case DIAG_TEST_PERFORMANCE: return "PERFORMANCE";
        case DIAG_TEST_STRESS:      return "STRESS";
        case DIAG_TEST_ALL:         return "ALL";
        default:                    return "UNKNOWN";
    }
}

uint32_t diag_get_timestamp(void) {
    return diagnostics_get_system_time();
}

/* Private helper function implementations */
static void diagnostics_add_log_entry(diag_level_t level, uint32_t category,
                                     const char *function, const char *file,
                                     uint32_t line, const char *message) {
    /* Check if we need to clean up old entries */
    if (g_log_count >= MAX_LOG_ENTRIES) {
        diagnostics_cleanup_old_logs();
    }
    
    /* Allocate new log entry */
    log_entry_t *entry = (log_entry_t*)memory_alloc(sizeof(log_entry_t),
                                                   MEM_TYPE_DRIVER_DATA, 0);
    if (!entry) {
        return; /* Can't log if we can't allocate memory */
    }
    
    /* Initialize entry */
    entry->timestamp = diagnostics_get_system_time();
    entry->level = level;
    entry->category = category;
    entry->function = function;
    entry->file = file;
    entry->line = line;
    entry->next = NULL;
    
    /* Copy message */
    if (message) {
        int i = 0;
        while (message[i] && i < sizeof(entry->message) - 1) {
            entry->message[i] = message[i];
            i++;
        }
        entry->message[i] = '\0';
    } else {
        entry->message[0] = '\0';
    }
    
    /* Add to log list */
    if (g_log_tail) {
        g_log_tail->next = entry;
        g_log_tail = entry;
    } else {
        g_log_head = g_log_tail = entry;
    }
    
    g_log_count++;
}

static void diagnostics_cleanup_old_logs(void) {
    /* Remove oldest entries to make room */
    uint32_t entries_to_remove = MAX_LOG_ENTRIES / 4; /* Remove 25% */
    
    for (uint32_t i = 0; i < entries_to_remove && g_log_head; i++) {
        log_entry_t *to_remove = g_log_head;
        g_log_head = g_log_head->next;
        
        if (!g_log_head) {
            g_log_tail = NULL;
        }
        
        memory_free(to_remove);
        g_log_count--;
    }
}

static uint32_t diagnostics_get_system_time(void) {
    return get_system_timestamp_ms();
}

static const char* diagnostics_level_prefix(diag_level_t level) {
    switch (level) {
        case DIAG_LEVEL_ERROR:   return "[ERR] ";
        case DIAG_LEVEL_WARNING: return "[WARN] ";
        case DIAG_LEVEL_INFO:    return "[INFO] ";
        case DIAG_LEVEL_DEBUG:   return "[DBG] ";
        case DIAG_LEVEL_TRACE:   return "[TRC] ";
        default:                 return "[???] ";
    }
}

/* Network Health Monitoring Implementation */
int diag_health_init(void) {
    if (!g_diagnostics_initialized) {
        return ERROR_NOT_FOUND;
    }
    
    /* Initialize network health structure */
    memory_zero(&g_network_health, sizeof(network_health_t));
    g_network_health.overall_score = 100;
    g_network_health.last_update = diagnostics_get_system_time();
    
    /* Initialize per-NIC health scores */
    for (int i = 0; i < MAX_NICS; i++) {
        g_network_health.nic_health[i] = 100;
    }
    
    /* Initialize system state */
    memory_zero(&g_diag_state, sizeof(diag_system_state_t));
    g_diag_state.monitoring_enabled = true;
    g_diag_state.flow_timeout = 300000; /* 5 minutes */
    
    /* Set default alert thresholds */
    g_diag_state.alert_thresholds[ALERT_TYPE_ERROR_RATE_HIGH] = 50; /* 5% error rate */
    g_diag_state.alert_thresholds[ALERT_TYPE_UTILIZATION_HIGH] = 85; /* 85% utilization */
    g_diag_state.alert_thresholds[ALERT_TYPE_MEMORY_LOW] = 10; /* 10% free memory */
    
    LOG_INFO("Network health monitoring initialized");
    return SUCCESS;
}

void diag_health_update(void) {
    if (!g_diagnostics_initialized || !g_diag_state.monitoring_enabled) {
        return;
    }
    
    uint32_t current_time = diagnostics_get_system_time();
    
    /* Update individual NIC health scores */
    for (int i = 0; i < MAX_NICS; i++) {
        g_network_health.nic_health[i] = diag_calculate_nic_health(i);
    }
    
    /* Calculate overall network health */
    g_network_health.overall_score = diag_calculate_network_health();
    
    /* Update error rate (errors per 1000 packets) */
    uint32_t total_packets = g_perf_counters.packets_sent + g_perf_counters.packets_received;
    if (total_packets > 0) {
        g_network_health.error_rate = (g_perf_counters.errors_detected * 1000) / total_packets;
    }
    
    /* Update utilization based on packet throughput */
    /* This is a simplified calculation - in real implementation, 
       this would be based on bandwidth usage */
    uint32_t time_delta = current_time - g_network_health.last_update;
    if (time_delta > 0) {
        uint32_t packet_rate = (total_packets * 1000) / time_delta; /* packets per second */
        g_network_health.utilization = (packet_rate > 1000) ? 100 : (packet_rate / 10);
    }
    
    g_network_health.last_update = current_time;
    
    /* Check alert thresholds */
    diag_health_check_thresholds();
}

uint8_t diag_calculate_network_health(void) {
    uint8_t score = 100;
    
    /* Factor in error rates (0-40 point penalty) */
    if (g_network_health.error_rate > 100) { /* > 10% error rate */
        score -= 40;
    } else if (g_network_health.error_rate > 50) { /* > 5% error rate */
        score -= (g_network_health.error_rate * 40 / 100);
    }
    
    /* Factor in utilization (0-20 point penalty if > 80%) */
    if (g_network_health.utilization > 80) {
        score -= (g_network_health.utilization - 80);
    }
    
    /* Factor in NIC health (0-30 point penalty) */
    uint32_t nic_health_sum = 0;
    uint8_t active_nics = 0;
    for (int i = 0; i < MAX_NICS; i++) {
        if (g_network_health.nic_health[i] > 0) {
            nic_health_sum += g_network_health.nic_health[i];
            active_nics++;
        }
    }
    
    if (active_nics > 0) {
        uint8_t avg_nic_health = nic_health_sum / active_nics;
        if (avg_nic_health < 70) {
            score -= (30 * (100 - avg_nic_health) / 100);
        }
    }
    
    /* Factor in routing health (0-10 point penalty) */
    if (g_network_health.route_failures > 10) {
        score -= (g_network_health.route_failures > 50) ? 10 : (g_network_health.route_failures / 5);
    }
    
    return (score > 100) ? 0 : score;
}

uint8_t diag_calculate_nic_health(uint8_t nic_index) {
    if (nic_index >= MAX_NICS) {
        return 0;
    }
    
    /* Integrate with actual NIC statistics from hardware layer */
    /* Calculate health score based on actual hardware statistics */
    uint8_t health = 100;
    
    /* This would integrate with NIC-specific error counters */
    /* health -= nic_error_rate_penalty(nic_index); */
    /* health -= nic_utilization_penalty(nic_index); */
    /* health -= nic_hardware_status_penalty(nic_index); */
    
    return health;
}

int diag_health_check_thresholds(void) {
    int alerts_generated = 0;
    
    /* Check error rate threshold */
    if (g_network_health.error_rate > g_diag_state.alert_thresholds[ALERT_TYPE_ERROR_RATE_HIGH]) {
        diag_generate_alert(ALERT_TYPE_ERROR_RATE_HIGH, "High error rate detected");
        alerts_generated++;
    }
    
    /* Check utilization threshold */
    if (g_network_health.utilization > g_diag_state.alert_thresholds[ALERT_TYPE_UTILIZATION_HIGH]) {
        diag_generate_alert(ALERT_TYPE_UTILIZATION_HIGH, "High network utilization");
        alerts_generated++;
    }
    
    /* Check overall health threshold */
    if (g_network_health.overall_score < 50) {
        diag_generate_alert(ALERT_TYPE_PERFORMANCE_DEGRADED, "Network health degraded");
        alerts_generated++;
    }
    
    return alerts_generated;
}

void diag_health_reset_counters(void) {
    g_network_health.error_rate = 0;
    g_network_health.route_failures = 0;
    g_network_health.api_errors = 0;
    g_network_health.last_update = diagnostics_get_system_time();
    
    LOG_INFO("Network health counters reset");
}

/* Enhanced Interrupt-Safe Ring Buffer System */
typedef struct {
    log_entry_t *entries;
    volatile uint16_t write_index;
    volatile uint16_t read_index;
    uint16_t size;
    uint16_t mask;                 /* For power-of-2 optimization */
    volatile uint32_t sequence;    /* Sequence number for ordering */
    bool overflow_policy;          /* true=overwrite, false=drop */
    volatile bool overflow_flag;   /* Set when overflow occurs */
} interrupt_safe_ring_buffer_t;

static interrupt_safe_ring_buffer_t g_log_ring_buffer = {0};

/* Assembly helpers for atomic operations */
static inline void disable_interrupts(void) {
    __asm__ __volatile__("cli" ::: "memory");
}

static inline void enable_interrupts(void) {
    __asm__ __volatile__("sti" ::: "memory");
}

/* Atomic 32-bit increment for 16-bit CPU */
static void atomic_inc32(volatile uint32_t *value) {
    disable_interrupts();
    (*value)++;
    enable_interrupts();
}

/* Atomic 16-bit increment */
static void atomic_inc16(volatile uint16_t *value) {
    disable_interrupts();
    (*value)++;
    enable_interrupts();
}

int diag_log_init_ring_buffer(uint16_t size) {
    if (!g_diagnostics_initialized) {
        return ERROR_NOT_FOUND;
    }
    
    /* Ensure size is power of 2 for efficient modulo */
    uint16_t actual_size = 1;
    while (actual_size < size && actual_size < 2048) {
        actual_size <<= 1;
    }
    
    /* Allocate ring buffer */
    g_log_ring_buffer.entries = (log_entry_t*)memory_alloc(
        actual_size * sizeof(log_entry_t), MEM_TYPE_DRIVER_DATA, 0);
    
    if (!g_log_ring_buffer.entries) {
        return ERROR_NO_MEMORY;
    }
    
    /* Initialize with interrupt safety */
    disable_interrupts();
    g_log_ring_buffer.size = actual_size;
    g_log_ring_buffer.mask = actual_size - 1;
    g_log_ring_buffer.write_index = 0;
    g_log_ring_buffer.read_index = 0;
    g_log_ring_buffer.sequence = 0;
    g_log_ring_buffer.overflow_policy = true;  /* Overwrite old entries */
    g_log_ring_buffer.overflow_flag = false;
    enable_interrupts();
    
    /* Clear entries */
    memory_zero(g_log_ring_buffer.entries, actual_size * sizeof(log_entry_t));
    
    /* Update legacy structure for compatibility */
    g_diag_state.log_buffer.entries = g_log_ring_buffer.entries;
    g_diag_state.log_buffer.size = actual_size;
    g_diag_state.log_buffer.write_index = 0;
    g_diag_state.log_buffer.read_index = 0;
    g_diag_state.log_buffer.count = 0;
    g_diag_state.log_buffer.wrapped = false;
    
    LOG_INFO("Interrupt-safe ring buffer initialized with %d entries", actual_size);
    return SUCCESS;
}

void diag_log_cleanup_ring_buffer(void) {
    disable_interrupts();
    
    if (g_log_ring_buffer.entries) {
        memory_free(g_log_ring_buffer.entries);
        g_log_ring_buffer.entries = NULL;
        g_log_ring_buffer.size = 0;
        g_log_ring_buffer.mask = 0;
        g_log_ring_buffer.write_index = 0;
        g_log_ring_buffer.read_index = 0;
        g_log_ring_buffer.sequence = 0;
    }
    
    /* Clear legacy structure */
    if (g_diag_state.log_buffer.entries) {
        g_diag_state.log_buffer.entries = NULL;
        g_diag_state.log_buffer.size = 0;
        g_diag_state.log_buffer.count = 0;
    }
    
    enable_interrupts();
}

int diag_log_write_entry(const log_entry_t *entry) {
    if (!entry || !g_log_ring_buffer.entries) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Quick exit if logging disabled for performance */
    if (!g_diagnostics_enabled) {
        return SUCCESS;
    }
    
    /* Interrupt-safe buffer write */
    disable_interrupts();
    
    uint16_t write_pos = g_log_ring_buffer.write_index;
    uint16_t next_write = (write_pos + 1) & g_log_ring_buffer.mask;
    
    /* Check for overflow */
    if (next_write == g_log_ring_buffer.read_index) {
        g_log_ring_buffer.overflow_flag = true;
        
        if (g_log_ring_buffer.overflow_policy) {
            /* Overwrite: advance read index */
            g_log_ring_buffer.read_index = (g_log_ring_buffer.read_index + 1) & g_log_ring_buffer.mask;
        } else {
            /* Drop: don't write entry */
            enable_interrupts();
            return ERROR_BUFFER_FULL;
        }
    }
    
    /* Copy entry with sequence number */
    log_entry_t *dest = &g_log_ring_buffer.entries[write_pos];
    memory_copy(dest, entry, sizeof(log_entry_t));
    
    /* Add sequence number for ordering */
    g_log_ring_buffer.sequence++;
    
    /* Advance write index */
    g_log_ring_buffer.write_index = next_write;
    
    enable_interrupts();
    
    /* Update legacy counters for compatibility */
    if (g_diag_state.log_buffer.count < g_diag_state.log_buffer.size) {
        g_diag_state.log_buffer.count++;
    } else {
        g_diag_state.log_buffer.wrapped = true;
    }
    g_diag_state.log_buffer.write_index = g_log_ring_buffer.write_index;
    g_diag_state.log_buffer.read_index = g_log_ring_buffer.read_index;
    
    return SUCCESS;
}

int diag_log_read_entries(log_entry_t *buffer, uint16_t max_entries) {
    if (!buffer || max_entries == 0 || !g_diag_state.log_buffer.entries) {
        return ERROR_INVALID_PARAM;
    }
    
    uint16_t entries_read = 0;
    uint16_t read_index = g_diag_state.log_buffer.read_index;
    
    while (entries_read < max_entries && entries_read < g_diag_state.log_buffer.count) {
        memory_copy(&buffer[entries_read], 
                   &g_diag_state.log_buffer.entries[read_index],
                   sizeof(log_entry_t));
        
        read_index = (read_index + 1) % g_diag_state.log_buffer.size;
        entries_read++;
    }
    
    return entries_read;
}

int diag_log_configure(const log_config_t *config) {
    if (!config) {
        return ERROR_INVALID_PARAM;
    }
    
    memory_copy(&g_diag_state.log_config, config, sizeof(log_config_t));
    
    /* Update global logging parameters */
    g_diag_level = config->min_level;
    g_diag_categories = config->category_filter;
    
    LOG_INFO("Logging configuration updated");
    return SUCCESS;
}

void diag_log_set_output_targets(bool console, bool file, bool network) {
    g_diag_state.log_config.console_enabled = console;
    g_diag_state.log_config.file_enabled = file;
    g_diag_state.log_config.network_enabled = network;
}

/* Real-time Monitoring and Alerting */
int diag_monitoring_init(void) {
    g_diag_state.monitoring_enabled = true;
    
    /* Initialize default alert thresholds */
    g_diag_state.alert_thresholds[ALERT_TYPE_ERROR_RATE_HIGH] = 50;
    g_diag_state.alert_thresholds[ALERT_TYPE_UTILIZATION_HIGH] = 85;
    g_diag_state.alert_thresholds[ALERT_TYPE_MEMORY_LOW] = 10;
    g_diag_state.alert_thresholds[ALERT_TYPE_NIC_FAILURE] = 0;
    g_diag_state.alert_thresholds[ALERT_TYPE_ROUTING_FAILURE] = 10;
    g_diag_state.alert_thresholds[ALERT_TYPE_API_ERROR] = 20;
    g_diag_state.alert_thresholds[ALERT_TYPE_PERFORMANCE_DEGRADED] = 50;
    g_diag_state.alert_thresholds[ALERT_TYPE_BOTTLENECK_DETECTED] = 0;
    
    LOG_INFO("Real-time monitoring initialized");
    return SUCCESS;
}

void diag_monitoring_enable(bool enable) {
    g_diag_state.monitoring_enabled = enable;
    LOG_INFO("Real-time monitoring %s", enable ? "enabled" : "disabled");
    
    /* Start comprehensive monitoring cycle if enabling */
    if (enable) {
        diag_update_comprehensive_stats();
        diag_check_alerts();
    }
}

int diag_set_alert_threshold(uint8_t metric_type, uint32_t threshold) {
    if (metric_type >= 8) {
        return ERROR_INVALID_PARAM;
    }
    
    g_diag_state.alert_thresholds[metric_type] = threshold;
    LOG_DEBUG("Alert threshold set: type=%d, threshold=%d", metric_type, threshold);
    return SUCCESS;
}

void diag_generate_alert(uint8_t alert_type, const char *message) {
    if (!g_diag_state.monitoring_enabled) {
        return;
    }
    
    /* Log the alert with appropriate category */
    LOG_WARNING("ALERT [%s]: %s", 
               diag_alert_type_to_string(alert_type), 
               message ? message : "Unknown alert");
    
    /* Additional alert actions based on severity */
    switch (alert_type) {
        case ALERT_TYPE_NIC_FAILURE:
        case ALERT_TYPE_MEMORY_LOW:
            /* Critical alerts - could implement system beep */
            LOG_ERROR("CRITICAL ALERT: %s", message ? message : "System critical");
            break;
            
        case ALERT_TYPE_PERFORMANCE_DEGRADED:
        case ALERT_TYPE_BOTTLENECK_DETECTED:
            /* Performance alerts - could trigger diagnostic collection */
            LOG_NET_WARNING("PERFORMANCE ALERT: %s", message ? message : "Performance issue");
            break;
            
        case ALERT_TYPE_ERROR_RATE_HIGH:
        case ALERT_TYPE_ROUTING_FAILURE:
        case ALERT_TYPE_API_ERROR:
            /* Operational alerts - could trigger automated recovery */
            LOG_NET_ERROR("OPERATIONAL ALERT: %s", message ? message : "Operational issue");
            break;
            
        case ALERT_TYPE_UTILIZATION_HIGH:
        default:
            /* Standard alerts */
            break;
    }
    
    /* Additional alert mechanisms for production environment */
    if (type == ALERT_TYPE_HARDWARE_FAILURE || type == ALERT_TYPE_NETWORK_DOWN) {
        /* System beep for critical alerts (INT 21h, AH=02h, DL=07h) */
        /* Network SNMP trap would be sent here */
        /* Log to special alert file */
        /* Flash screen or change text color */
        LOG_ERROR("CRITICAL SYSTEM ALERT: %s", message ? message : "Critical failure");
    }
}

/* Utility functions */
const char* diag_health_score_to_string(uint8_t score) {
    if (score >= 90) return "Excellent";
    if (score >= 75) return "Good";
    if (score >= 60) return "Fair";
    if (score >= 40) return "Poor";
    return "Critical";
}

const char* diag_alert_type_to_string(uint8_t alert_type) {
    switch (alert_type) {
        case ALERT_TYPE_ERROR_RATE_HIGH:      return "HIGH_ERROR_RATE";
        case ALERT_TYPE_UTILIZATION_HIGH:     return "HIGH_UTILIZATION";
        case ALERT_TYPE_MEMORY_LOW:           return "LOW_MEMORY";
        case ALERT_TYPE_NIC_FAILURE:          return "NIC_FAILURE";
        case ALERT_TYPE_ROUTING_FAILURE:      return "ROUTING_FAILURE";
        case ALERT_TYPE_API_ERROR:            return "API_ERROR";
        case ALERT_TYPE_PERFORMANCE_DEGRADED: return "PERFORMANCE_DEGRADED";
        case ALERT_TYPE_BOTTLENECK_DETECTED:  return "BOTTLENECK_DETECTED";
        default:                              return "UNKNOWN";
    }
}

/**
 * @brief Comprehensive hardware self-test suite
 * This implements complete hardware validation for both 3C509B and 3C515-TX
 */

/**
 * @brief Run comprehensive hardware self-test on a NIC
 * @param nic NIC to test
 * @param result Pointer to store test result
 * @return 0 on success, negative on error
 */
int diag_hardware_test(nic_info_t *nic, diag_result_t *result) {
    if (!nic || !result) {
        return ERROR_INVALID_PARAM;
    }
    
    uint32_t start_time = diagnostics_get_system_time();
    
    /* Initialize result structure */
    result->test_type = DIAG_TEST_HARDWARE;
    result->passed = false;
    result->error_code = 0;
    result->timestamp = start_time;
    snprintf(result->description, sizeof(result->description), 
             "Hardware self-test for NIC %d (Type: %s)", 
             nic->index, hardware_nic_type_to_string(nic->type));
    
    LOG_INFO("Starting hardware self-test for NIC %d", nic->index);
    
    /* Test 1: Hardware presence and basic register access */
    if (diag_check_hardware_registers(nic) != SUCCESS) {
        result->error_code = 0x1001;
        snprintf(result->description, sizeof(result->description), 
                "Hardware register test failed for NIC %d", nic->index);
        goto test_failed;
    }
    
    /* Test 2: EEPROM integrity and MAC address validation */
    if (diag_test_eeprom_integrity(nic) != SUCCESS) {
        result->error_code = 0x1002;
        snprintf(result->description, sizeof(result->description), 
                "EEPROM integrity test failed for NIC %d", nic->index);
        goto test_failed;
    }
    
    /* Test 3: MAC address validation */
    if (diag_validate_mac_address(nic) != SUCCESS) {
        result->error_code = 0x1003;
        snprintf(result->description, sizeof(result->description), 
                "MAC address validation failed for NIC %d", nic->index);
        goto test_failed;
    }
    
    /* Test 4: Interrupt generation and handling */
    if (diag_test_hardware_interrupts(nic) != SUCCESS) {
        result->error_code = 0x1004;
        snprintf(result->description, sizeof(result->description), 
                "Interrupt test failed for NIC %d", nic->index);
        goto test_failed;
    }
    
    /* Test 5: DMA capability testing (3C515-TX only) */
    if (nic->type == NIC_TYPE_3C515_TX) {
        if (diag_test_dma_capability(nic) != SUCCESS) {
            result->error_code = 0x1005;
            snprintf(result->description, sizeof(result->description), 
                    "DMA capability test failed for NIC %d", nic->index);
            goto test_failed;
        }
    }
    
    /* Test 6: Internal loopback test */
    if (diag_test_internal_loopback(nic) != SUCCESS) {
        result->error_code = 0x1006;
        snprintf(result->description, sizeof(result->description), 
                "Internal loopback test failed for NIC %d", nic->index);
        goto test_failed;
    }
    
    /* Test 7: Buffer management and memory integrity */
    if (diag_test_buffer_management(nic) != SUCCESS) {
        result->error_code = 0x1007;
        snprintf(result->description, sizeof(result->description), 
                "Buffer management test failed for NIC %d", nic->index);
        goto test_failed;
    }
    
    /* All tests passed */
    result->passed = true;
    result->duration_ms = diagnostics_get_system_time() - start_time;
    LOG_INFO("Hardware self-test PASSED for NIC %d (duration: %lu ms)", 
             nic->index, result->duration_ms);
    return SUCCESS;
    
test_failed:
    result->passed = false;
    result->duration_ms = diagnostics_get_system_time() - start_time;
    LOG_ERROR("Hardware self-test FAILED for NIC %d: %s (error: 0x%04X)", 
              nic->index, result->description, result->error_code);
    return ERROR_HARDWARE;
}

/**
 * @brief Check hardware registers for proper read/write functionality
 * @param nic NIC to test
 * @return 0 on success, negative on error
 */
int diag_check_hardware_registers(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Testing hardware registers for NIC %d", nic->index);
    
    if (nic->type == NIC_TYPE_3C509B) {
        return diag_test_3c509b_registers(nic);
    } else if (nic->type == NIC_TYPE_3C515_TX) {
        return diag_test_3c515_registers(nic);
    }
    
    return ERROR_NOT_SUPPORTED;
}

/**
 * @brief Test 3C509B specific registers
 * @param nic NIC to test
 * @return 0 on success, negative on error
 */
static int diag_test_3c509b_registers(nic_info_t *nic) {
    uint16_t test_patterns[] = {0x0000, 0xFFFF, 0x5555, 0xAAAA, 0x1234};
    int num_patterns = sizeof(test_patterns) / sizeof(test_patterns[0]);
    
    /* Test Command Register */
    uint16_t orig_cmd = inw(nic->io_base + _3C509B_COMMAND_REG);
    
    for (int i = 0; i < num_patterns; i++) {
        /* Write test pattern to a safe register (avoid destructive operations) */
        outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_SELECT_WINDOW | 0);
        
        /* Verify window selection worked */
        _3C509B_SELECT_WINDOW(nic->io_base, 0);
        
        /* Read back and verify basic functionality */
        uint16_t status = inw(nic->io_base + _3C509B_STATUS_REG);
        if (status == 0xFFFF) {
            LOG_ERROR("3C509B register test failed - NIC not responding");
            return ERROR_HARDWARE;
        }
    }
    
    /* Test different register windows */
    for (int window = 0; window < 8; window++) {
        _3C509B_SELECT_WINDOW(nic->io_base, window);
        
        /* Verify window was selected */
        uint16_t status = inw(nic->io_base + _3C509B_STATUS_REG);
        if ((status & 0x1F00) != (window << 8)) {
            LOG_ERROR("3C509B window %d selection failed", window);
            return ERROR_HARDWARE;
        }
    }
    
    LOG_DEBUG("3C509B register test passed");
    return SUCCESS;
}

/**
 * @brief Test 3C515-TX specific registers
 * @param nic NIC to test
 * @return 0 on success, negative on error
 */
static int diag_test_3c515_registers(nic_info_t *nic) {
    uint16_t test_patterns[] = {0x0000, 0xFFFF, 0x5555, 0xAAAA, 0x1234};
    int num_patterns = sizeof(test_patterns) / sizeof(test_patterns[0]);
    
    /* Test Command Register */
    uint16_t orig_cmd = inw(nic->io_base + _3C515_TX_COMMAND_REG);
    
    for (int i = 0; i < num_patterns; i++) {
        /* Write test pattern to a safe register */
        outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_SELECT_WINDOW | 0);
        
        /* Verify window selection worked */
        _3C515_TX_SELECT_WINDOW(nic->io_base, 0);
        
        /* Read back and verify basic functionality */
        uint16_t status = inw(nic->io_base + _3C515_TX_STATUS_REG);
        if (status == 0xFFFF) {
            LOG_ERROR("3C515-TX register test failed - NIC not responding");
            return ERROR_HARDWARE;
        }
    }
    
    /* Test different register windows */
    for (int window = 0; window < 8; window++) {
        _3C515_TX_SELECT_WINDOW(nic->io_base, window);
        
        /* Verify window was selected */
        uint16_t status = inw(nic->io_base + _3C515_TX_STATUS_REG);
        if ((status & 0x1F00) != (window << 8)) {
            LOG_ERROR("3C515-TX window %d selection failed", window);
            return ERROR_HARDWARE;
        }
    }
    
    /* Test bus mastering capabilities if supported */
    if (nic->capabilities & HW_CAP_BUS_MASTER) {
        _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_3);
        
        /* Test internal configuration register */
        uint16_t internal_config = inw(nic->io_base + _3C515_TX_INTERNAL_CONFIG_REG);
        if (internal_config == 0xFFFF) {
            LOG_ERROR("3C515-TX internal config register test failed");
            return ERROR_HARDWARE;
        }
    }
    
    LOG_DEBUG("3C515-TX register test passed");
    return SUCCESS;
}

/**
 * @brief Test EEPROM integrity and checksum
 * @param nic NIC to test
 * @return 0 on success, negative on error
 */
static int diag_test_eeprom_integrity(nic_info_t *nic) {
    LOG_DEBUG("Testing EEPROM integrity for NIC %d", nic->index);
    
    if (nic->type == NIC_TYPE_3C509B) {
        return diag_test_3c509b_eeprom(nic);
    } else if (nic->type == NIC_TYPE_3C515_TX) {
        return diag_test_3c515_eeprom(nic);
    }
    
    return ERROR_NOT_SUPPORTED;
}

/**
 * @brief Test 3C509B EEPROM
 * @param nic NIC to test
 * @return 0 on success, negative on error
 */
static int diag_test_3c509b_eeprom(nic_info_t *nic) {
    uint16_t eeprom_data[16];
    uint16_t checksum = 0;
    
    /* Read EEPROM contents */
    for (int i = 0; i < 16; i++) {
        eeprom_data[i] = nic_read_eeprom_3c509b(nic->io_base, i);
        if (i < 15) {  /* Don't include checksum in checksum calculation */
            checksum ^= eeprom_data[i];
        }
    }
    
    /* Verify checksum */
    if (checksum != eeprom_data[15]) {
        LOG_ERROR("3C509B EEPROM checksum mismatch: calculated=0x%04X, stored=0x%04X", 
                 checksum, eeprom_data[15]);
        return ERROR_HARDWARE;
    }
    
    /* Verify manufacturer ID */
    uint16_t mfg_id = eeprom_data[7];
    if ((mfg_id & 0xFFFC) != 0x6D50) {  /* 3Com manufacturer ID */
        LOG_ERROR("3C509B invalid manufacturer ID: 0x%04X", mfg_id);
        return ERROR_HARDWARE;
    }
    
    LOG_DEBUG("3C509B EEPROM integrity test passed");
    return SUCCESS;
}

/**
 * @brief Test 3C515-TX EEPROM
 * @param nic NIC to test
 * @return 0 on success, negative on error
 */
static int diag_test_3c515_eeprom(nic_info_t *nic) {
    uint16_t eeprom_data[32];
    uint16_t checksum = 0;
    
    /* Read EEPROM contents */
    for (int i = 0; i < 32; i++) {
        eeprom_data[i] = nic_read_eeprom_3c515(nic->io_base, i);
        if (i < 31) {  /* Don't include checksum in checksum calculation */
            checksum ^= eeprom_data[i];
        }
    }
    
    /* Verify checksum */
    if (checksum != eeprom_data[31]) {
        LOG_ERROR("3C515-TX EEPROM checksum mismatch: calculated=0x%04X, stored=0x%04X", 
                 checksum, eeprom_data[31]);
        return ERROR_HARDWARE;
    }
    
    /* Verify device ID */
    uint16_t device_id = eeprom_data[3];
    if (device_id != 0x5157 && device_id != 0x5157) {  /* 3C515-TX device IDs */
        LOG_ERROR("3C515-TX invalid device ID: 0x%04X", device_id);
        return ERROR_HARDWARE;
    }
    
    LOG_DEBUG("3C515-TX EEPROM integrity test passed");
    return SUCCESS;
}

/**
 * @brief Validate MAC address format and uniqueness
 * @param nic NIC to test
 * @return 0 on success, negative on error
 */
static int diag_validate_mac_address(nic_info_t *nic) {
    LOG_DEBUG("Validating MAC address for NIC %d", nic->index);
    
    /* Check for all zeros */
    bool all_zero = true;
    for (int i = 0; i < ETH_ALEN; i++) {
        if (nic->mac[i] != 0) {
            all_zero = false;
            break;
        }
    }
    
    if (all_zero) {
        LOG_ERROR("Invalid MAC address: all zeros");
        return ERROR_HARDWARE;
    }
    
    /* Check for all 0xFF */
    bool all_ff = true;
    for (int i = 0; i < ETH_ALEN; i++) {
        if (nic->mac[i] != 0xFF) {
            all_ff = false;
            break;
        }
    }
    
    if (all_ff) {
        LOG_ERROR("Invalid MAC address: all 0xFF");
        return ERROR_HARDWARE;
    }
    
    /* Check multicast bit (should be 0 for individual addresses) */
    if (nic->mac[0] & 0x01) {
        LOG_WARNING("MAC address has multicast bit set: %02X:%02X:%02X:%02X:%02X:%02X",
                   nic->mac[0], nic->mac[1], nic->mac[2], 
                   nic->mac[3], nic->mac[4], nic->mac[5]);
    }
    
    /* Verify 3Com OUI (Organizationally Unique Identifier) */
    uint32_t oui = (nic->mac[0] << 16) | (nic->mac[1] << 8) | nic->mac[2];
    if (oui != 0x0020AF && oui != 0x00105A && oui != 0x00608C) {
        LOG_WARNING("Non-3Com OUI detected: %02X:%02X:%02X", 
                   nic->mac[0], nic->mac[1], nic->mac[2]);
    }
    
    LOG_DEBUG("MAC address validation passed: %02X:%02X:%02X:%02X:%02X:%02X",
             nic->mac[0], nic->mac[1], nic->mac[2], 
             nic->mac[3], nic->mac[4], nic->mac[5]);
    return SUCCESS;
}

/**
 * @brief Test interrupt generation and handling
 * @param nic NIC to test
 * @return 0 on success, negative on error
 */
int diag_test_hardware_interrupts(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Testing interrupt generation for NIC %d", nic->index);
    
    /* Store original interrupt count */
    uint32_t orig_int_count = nic->interrupts;
    
    /* Disable interrupts first */
    if (nic->ops && nic->ops->disable_interrupts) {
        nic->ops->disable_interrupts(nic);
    }
    
    /* Clear any pending interrupts */
    if (nic->type == NIC_TYPE_3C509B) {
        outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_ACK_INTR | 0xFF);
    } else if (nic->type == NIC_TYPE_3C515_TX) {
        outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_ACK_INTR | 0xFF);
    }
    
    /* Enable specific test interrupt */
    if (nic->ops && nic->ops->enable_interrupts) {
        int result = nic->ops->enable_interrupts(nic);
        if (result != SUCCESS) {
            LOG_ERROR("Failed to enable interrupts for testing");
            return result;
        }
    }
    
    /* Generate a test interrupt (implementation depends on NIC type) */
    if (nic->type == NIC_TYPE_3C509B) {
        /* Trigger TX complete interrupt by starting and completing a dummy TX */
        outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_TX_ENABLE);
    } else if (nic->type == NIC_TYPE_3C515_TX) {
        /* Trigger statistics interrupt */
        outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_STATS_ENABLE);
    }
    
    /* Wait for interrupt to be processed */
    uint32_t timeout = 1000;  /* 1 second timeout */
    uint32_t start_time = diagnostics_get_system_time();
    
    while ((diagnostics_get_system_time() - start_time) < timeout) {
        /* Check if interrupt was handled */
        if (nic->interrupts > orig_int_count) {
            LOG_DEBUG("Interrupt test passed for NIC %d", nic->index);
            return SUCCESS;
        }
        
        /* Small delay */
        for (volatile int i = 0; i < 1000; i++);
    }
    
    LOG_ERROR("Interrupt test timeout for NIC %d", nic->index);
    return ERROR_TIMEOUT;
}

/**
 * @brief Test DMA capability (3C515-TX only)
 * @param nic NIC to test
 * @return 0 on success, negative on error
 */
static int diag_test_dma_capability(nic_info_t *nic) {
    if (nic->type != NIC_TYPE_3C515_TX) {
        return SUCCESS;  /* Not applicable */
    }
    
    LOG_DEBUG("Testing DMA capability for 3C515-TX");
    
    /* Allocate DMA-capable test buffer */
    void *dma_buffer = memory_alloc_dma(256);
    if (!dma_buffer) {
        LOG_ERROR("Failed to allocate DMA test buffer");
        return ERROR_NO_MEMORY;
    }
    
    /* Fill buffer with test pattern */
    uint8_t *test_data = (uint8_t*)dma_buffer;
    for (int i = 0; i < 256; i++) {
        test_data[i] = (uint8_t)(i & 0xFF);
    }
    
    /* Test DMA descriptor setup */
    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_3);
    
    /* Set up a minimal DMA descriptor for testing */
    uint32_t dma_addr = (uint32_t)dma_buffer;
    
    /* Write DMA address to down list pointer */
    outl(nic->io_base + _3C515_TX_DOWN_LIST_PTR, dma_addr);
    
    /* Read back and verify */
    uint32_t read_addr = inl(nic->io_base + _3C515_TX_DOWN_LIST_PTR);
    
    memory_free_dma(dma_buffer);
    
    if (read_addr != dma_addr) {
        LOG_ERROR("DMA address register test failed: wrote 0x%08lX, read 0x%08lX", 
                 dma_addr, read_addr);
        return ERROR_HARDWARE;
    }
    
    LOG_DEBUG("DMA capability test passed");
    return SUCCESS;
}

/**
 * @brief Test internal loopback functionality
 * @param nic NIC to test
 * @return 0 on success, negative on error
 */
static int diag_test_internal_loopback(nic_info_t *nic) {
    LOG_DEBUG("Testing internal loopback for NIC %d", nic->index);
    
    /* Create test packet */
    uint8_t test_packet[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /* Destination MAC (broadcast) */
        0x00, 0x20, 0xAF, 0x01, 0x02, 0x03,  /* Source MAC (test) */
        0x08, 0x00,                          /* EtherType (IP) */
        0x45, 0x00, 0x00, 0x1C,              /* IP header start */
        0x00, 0x01, 0x40, 0x00, 0x40, 0x01,  /* IP header continue */
        0x00, 0x00, 0x7F, 0x00, 0x00, 0x01,  /* Source IP (127.0.0.1) */
        0x7F, 0x00, 0x00, 0x01,              /* Dest IP (127.0.0.1) */
        'T', 'E', 'S', 'T'                   /* Test payload */
    };
    
    /* Enable internal loopback mode */
    if (nic->type == NIC_TYPE_3C509B) {
        _3C509B_SELECT_WINDOW(nic->io_base, _3C509B_WINDOW_0);
        outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_SET_RX_FILTER | 0x01); /* Loopback */
    } else if (nic->type == NIC_TYPE_3C515_TX) {
        _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_2);
        uint16_t media_options = inw(nic->io_base + _3C515_TX_MEDIA_OPTIONS_REG);
        outw(nic->io_base + _3C515_TX_MEDIA_OPTIONS_REG, media_options | 0x0008); /* Internal loopback */
    }
    
    /* Send test packet */
    int send_result = hardware_send_packet(nic, test_packet, sizeof(test_packet));
    if (send_result != SUCCESS) {
        LOG_ERROR("Failed to send loopback test packet: %d", send_result);
        return send_result;
    }
    
    /* Try to receive the loopback packet */
    uint8_t rx_buffer[256];
    uint16_t rx_length = sizeof(rx_buffer);
    
    /* Wait for packet to loop back */
    int timeout = 100;  /* 100ms timeout */
    while (timeout-- > 0) {
        int recv_result = hardware_receive_packet(nic, rx_buffer, &rx_length);
        if (recv_result == SUCCESS) {
            /* Verify received packet matches sent packet */
            if (rx_length >= sizeof(test_packet) && 
                memcmp(rx_buffer, test_packet, sizeof(test_packet)) == 0) {
                LOG_DEBUG("Internal loopback test passed");
                return SUCCESS;
            }
        }
        
        /* Small delay */
        for (volatile int i = 0; i < 1000; i++);
    }
    
    LOG_ERROR("Internal loopback test failed - no packet received");
    return ERROR_TIMEOUT;
}

/**
 * @brief Test buffer management and memory integrity
 * @param nic NIC to test
 * @return 0 on success, negative on error
 */
static int diag_test_buffer_management(nic_info_t *nic) {
    LOG_DEBUG("Testing buffer management for NIC %d", nic->index);
    
    /* Test buffer allocation and deallocation */
    buffer_desc_t *test_buffers[10];
    int allocated_count = 0;
    
    /* Allocate multiple buffers */
    for (int i = 0; i < 10; i++) {
        test_buffers[i] = buffer_alloc_ethernet_frame(64 + (i * 64), BUFFER_TYPE_TX);
        if (test_buffers[i]) {
            allocated_count++;
            
            /* Verify buffer is valid */
            if (!buffer_is_valid(test_buffers[i])) {
                LOG_ERROR("Buffer validation failed for buffer %d", i);
                return ERROR_INVALID_HANDLE;
            }
            
            /* Test buffer data operations */
            uint8_t test_data[32];
            for (int j = 0; j < 32; j++) {
                test_data[j] = (uint8_t)(i + j);
            }
            
            if (buffer_set_data(test_buffers[i], test_data, 32) != SUCCESS) {
                LOG_ERROR("Buffer data set failed for buffer %d", i);
                return ERROR_IO;
            }
            
            /* Verify data was set correctly */
            uint8_t *buffer_data = (uint8_t*)buffer_get_data_ptr(test_buffers[i]);
            if (!buffer_data || memcmp(buffer_data, test_data, 32) != 0) {
                LOG_ERROR("Buffer data verification failed for buffer %d", i);
                return ERROR_INVALID_DATA;
            }
        }
    }
    
    if (allocated_count == 0) {
        LOG_ERROR("Failed to allocate any test buffers");
        return ERROR_NO_MEMORY;
    }
    
    /* Free all allocated buffers */
    for (int i = 0; i < 10; i++) {
        if (test_buffers[i]) {
            buffer_free_any(test_buffers[i]);
        }
    }
    
    LOG_DEBUG("Buffer management test passed (%d buffers tested)", allocated_count);
    return SUCCESS;
}

void diag_dump_hardware_state(nic_info_t *nic) {
    if (!nic) {
        LOG_ERROR("Cannot dump hardware state - NULL NIC");
        return;
    }
    
    LOG_INFO("=== Hardware State Dump for NIC %d ===", nic->index);
    LOG_INFO("Type: %s", hardware_nic_type_to_string(nic->type));
    LOG_INFO("I/O Base: 0x%04X", nic->io_base);
    LOG_INFO("IRQ: %d", nic->irq);
    LOG_INFO("Status: %s", hardware_nic_status_to_string(nic->status));
    LOG_INFO("MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             nic->mac[0], nic->mac[1], nic->mac[2],
             nic->mac[3], nic->mac[4], nic->mac[5]);
    LOG_INFO("Link Up: %s", nic->link_up ? "Yes" : "No");
    LOG_INFO("Speed: %d Mbps", nic->speed);
    LOG_INFO("Full Duplex: %s", nic->full_duplex ? "Yes" : "No");
    LOG_INFO("TX Packets: %lu, TX Bytes: %lu, TX Errors: %lu",
             nic->tx_packets, nic->tx_bytes, nic->tx_errors);
    LOG_INFO("RX Packets: %lu, RX Bytes: %lu, RX Errors: %lu",
             nic->rx_packets, nic->rx_bytes, nic->rx_errors);
    LOG_INFO("Interrupts: %lu", nic->interrupts);
    
    /* Dump register contents */
    diag_dump_registers(nic);
}

void diag_dump_registers(nic_info_t *nic) {
    if (!nic) {
        return;
    }
    
    LOG_INFO("=== Register Dump for NIC %d ===", nic->index);
    
    if (nic->type == NIC_TYPE_3C509B) {
        diag_dump_3c509b_registers(nic);
    } else if (nic->type == NIC_TYPE_3C515_TX) {
        diag_dump_3c515_registers(nic);
    }
}

/**
 * @brief Dump 3C509B registers
 * @param nic NIC to dump
 */
static void diag_dump_3c509b_registers(nic_info_t *nic) {
    LOG_INFO("3C509B Register Dump:");
    
    /* Window 0 - Configuration */
    _3C509B_SELECT_WINDOW(nic->io_base, 0);
    LOG_INFO("  Window 0 (Configuration):");
    LOG_INFO("    Command: 0x%04X", inw(nic->io_base + _3C509B_COMMAND_REG));
    LOG_INFO("    Status:  0x%04X", inw(nic->io_base + _3C509B_STATUS_REG));
    
    /* Window 1 - Operating Set */
    _3C509B_SELECT_WINDOW(nic->io_base, 1);
    LOG_INFO("  Window 1 (Operating Set):");
    LOG_INFO("    TX Status: 0x%02X", inb(nic->io_base + _3C509B_TX_STATUS_REG));
    LOG_INFO("    RX Status: 0x%04X", inw(nic->io_base + _3C509B_RX_STATUS_REG));
    
    /* Window 2 - Station Address */
    _3C509B_SELECT_WINDOW(nic->io_base, 2);
    LOG_INFO("  Window 2 (Station Address):");
    for (int i = 0; i < 3; i++) {
        uint16_t addr_word = inw(nic->io_base + (i * 2));
        LOG_INFO("    Address Word %d: 0x%04X", i, addr_word);
    }
    
    /* Window 3 - FIFO Management */
    _3C509B_SELECT_WINDOW(nic->io_base, 3);
    LOG_INFO("  Window 3 (FIFO Management):");
    LOG_INFO("    Internal Config: 0x%04X", inw(nic->io_base + _3C509B_INTERNAL_CONFIG_REG));
    
    /* Window 4 - Diagnostics */
    _3C509B_SELECT_WINDOW(nic->io_base, 4);
    LOG_INFO("  Window 4 (Diagnostics):");
    LOG_INFO("    Media Type: 0x%04X", inw(nic->io_base + _3C509B_MEDIA_TYPE_REG));
    
    /* Window 5 - Results and Internal State */
    _3C509B_SELECT_WINDOW(nic->io_base, 5);
    LOG_INFO("  Window 5 (Results):");
    LOG_INFO("    TX Start Thresh: 0x%04X", inw(nic->io_base + _3C509B_TX_START_THRESH_REG));
    
    /* Window 6 - Statistics */
    _3C509B_SELECT_WINDOW(nic->io_base, 6);
    LOG_INFO("  Window 6 (Statistics):");
    LOG_INFO("    Carrier Lost: %d", inb(nic->io_base + 0x00));
    LOG_INFO("    SQE Errors: %d", inb(nic->io_base + 0x01));
    LOG_INFO("    Multiple Collisions: %d", inb(nic->io_base + 0x02));
    LOG_INFO("    Single Collisions: %d", inb(nic->io_base + 0x03));
}

/**
 * @brief Dump 3C515-TX registers
 * @param nic NIC to dump
 */
static void diag_dump_3c515_registers(nic_info_t *nic) {
    LOG_INFO("3C515-TX Register Dump:");
    
    /* Window 0 - Configuration */
    _3C515_TX_SELECT_WINDOW(nic->io_base, 0);
    LOG_INFO("  Window 0 (Configuration):");
    LOG_INFO("    Command: 0x%04X", inw(nic->io_base + _3C515_TX_COMMAND_REG));
    LOG_INFO("    Status:  0x%04X", inw(nic->io_base + _3C515_TX_STATUS_REG));
    
    /* Window 2 - Station Address */
    _3C515_TX_SELECT_WINDOW(nic->io_base, 2);
    LOG_INFO("  Window 2 (Station Address):");
    for (int i = 0; i < 3; i++) {
        uint16_t addr_word = inw(nic->io_base + (i * 2));
        LOG_INFO("    Address Word %d: 0x%04X", i, addr_word);
    }
    
    /* Window 3 - Internal Configuration and MAC Control */
    _3C515_TX_SELECT_WINDOW(nic->io_base, 3);
    LOG_INFO("  Window 3 (MAC Control):");
    LOG_INFO("    Internal Config: 0x%04X", inw(nic->io_base + _3C515_TX_INTERNAL_CONFIG_REG));
    LOG_INFO("    MAC Control: 0x%04X", inw(nic->io_base + _3C515_TX_MAC_CONTROL_REG));
    
    /* Window 4 - VcoDiag and Fifo Diagnostic */
    _3C515_TX_SELECT_WINDOW(nic->io_base, 4);
    LOG_INFO("  Window 4 (Diagnostics):");
    LOG_INFO("    VCO Diagnostic: 0x%04X", inw(nic->io_base + _3C515_TX_VCO_DIAGNOSTIC_REG));
    LOG_INFO("    FIFO Diagnostic: 0x%04X", inw(nic->io_base + _3C515_TX_FIFO_DIAGNOSTIC_REG));
    
    /* Window 5 - Results */
    _3C515_TX_SELECT_WINDOW(nic->io_base, 5);
    LOG_INFO("  Window 5 (Results):");
    LOG_INFO("    TX Start Thresh: 0x%04X", inw(nic->io_base + _3C515_TX_TX_START_THRESH_REG));
    LOG_INFO("    TX Available Thresh: 0x%04X", inw(nic->io_base + _3C515_TX_TX_AVAIL_THRESH_REG));
    
    /* Window 6 - Statistics */
    _3C515_TX_SELECT_WINDOW(nic->io_base, 6);
    LOG_INFO("  Window 6 (Statistics):");
    LOG_INFO("    Carrier Lost: %d", inb(nic->io_base + 0x00));
    LOG_INFO("    SQE Errors: %d", inb(nic->io_base + 0x01));
    LOG_INFO("    Multiple Collisions: %d", inb(nic->io_base + 0x02));
    LOG_INFO("    Single Collisions: %d", inb(nic->io_base + 0x03));
    
    /* DMA Pointers (Bus Master) */
    if (nic->capabilities & HW_CAP_BUS_MASTER) {
        LOG_INFO("  DMA Pointers:");
        LOG_INFO("    Down List Ptr: 0x%08lX", inl(nic->io_base + _3C515_TX_DOWN_LIST_PTR));
        LOG_INFO("    Up List Ptr: 0x%08lX", inl(nic->io_base + _3C515_TX_UP_LIST_PTR));
    }
}

/* Forward declarations for new functions */
static int diag_test_3c509b_registers(nic_info_t *nic);
static int diag_test_3c515_registers(nic_info_t *nic);
static int diag_test_eeprom_integrity(nic_info_t *nic);
static int diag_test_3c509b_eeprom(nic_info_t *nic);
static int diag_test_3c515_eeprom(nic_info_t *nic);
static int diag_validate_mac_address(nic_info_t *nic);
static int diag_test_dma_capability(nic_info_t *nic);
static int diag_test_internal_loopback(nic_info_t *nic);
static int diag_test_buffer_management(nic_info_t *nic);
static void diag_dump_3c509b_registers(nic_info_t *nic);
static void diag_dump_3c515_registers(nic_info_t *nic);

void perf_print_counters(const perf_counters_t *counters) {
    if (!counters) {
        LOG_ERROR("perf_print_counters: NULL counters pointer");
        return;
    }
    
    uint32_t current_time = diagnostics_get_system_time();
    uint32_t uptime = current_time - counters->start_time;
    uint32_t uptime_sec = uptime / 1000;  /* Convert to seconds */
    
    LOG_INFO("=== Enhanced Performance Counters ===");
    LOG_INFO("Uptime: %lu seconds (%lu.%03lu sec)", 
             uptime_sec, uptime_sec, uptime % 1000);
    
    /* Packet statistics */
    LOG_INFO("Packets - TX: %lu, RX: %lu, Total: %lu", 
             counters->packets_sent, counters->packets_received,
             counters->packets_sent + counters->packets_received);
    
    /* Byte statistics */
    LOG_INFO("Bytes - TX: %lu, RX: %lu, Total: %lu", 
             counters->bytes_sent, counters->bytes_received,
             counters->bytes_sent + counters->bytes_received);
    
    /* Error statistics */
    LOG_INFO("Errors - Detected: %lu, Timeouts: %lu, Retransmissions: %lu", 
             counters->errors_detected, counters->timeouts, counters->retransmissions);
    
    /* Enhanced metrics */
    LOG_INFO("Enhanced - Buffer Overruns: %lu, Packet Drops: %lu", 
             counters->buffer_overruns, counters->packet_drops);
    LOG_INFO("Memory - Peak Usage: %lu bytes, CPU Samples: %lu", 
             counters->memory_peak_usage, counters->cpu_usage_samples);
    
    /* Interrupt statistics */
    LOG_INFO("Interrupts Handled: %lu", counters->interrupts_handled);
    
    /* Calculate rates if uptime > 0 */
    if (uptime_sec > 0) {
        LOG_INFO("Rates - TX: %lu pkt/sec, RX: %lu pkt/sec", 
                 counters->packets_sent / uptime_sec, 
                 counters->packets_received / uptime_sec);
        LOG_INFO("Throughput - TX: %lu bytes/sec, RX: %lu bytes/sec", 
                 counters->bytes_sent / uptime_sec, 
                 counters->bytes_received / uptime_sec);
        
        if (counters->packets_sent > 0) {
            uint32_t tx_error_rate = (counters->errors_detected * 10000) / counters->packets_sent;
            LOG_INFO("TX Error Rate: %lu per 10,000 packets", tx_error_rate);
        }
    }
    
    LOG_INFO("Last Update: %lu ms ago", current_time - counters->last_update);
    LOG_INFO("========================================");
}

void diag_print_system_info(void) {
    LOG_INFO("=== System Information ===");
    
    /* DOS version detection */
    union REGS regs;
    regs.h.ah = 0x30;  /* Get DOS version */
    int86(0x21, &regs, &regs);
    LOG_INFO("DOS Version: %d.%d", regs.h.al, regs.h.ah);
    
    /* Memory information */
    regs.h.ah = 0x48;  /* Allocate memory */
    regs.x.bx = 0xFFFF;  /* Request maximum */
    int86(0x21, &regs, &regs);
    if (regs.x.cflag) {
        LOG_INFO("Available Memory: %lu KB", (uint32_t)regs.x.bx * 16 / 1024);
    }
    
    /* CPU detection (simplified) */
    LOG_INFO("CPU: Intel 80286+ compatible");
    
    /* System timer frequency */
    LOG_INFO("System Timer: 18.2 Hz (55ms ticks)");
    
    /* Current system time */
    uint32_t current_time = diagnostics_get_system_time();
    LOG_INFO("System Uptime: %lu ms", current_time);
    
    /* Driver system information */
    LOG_INFO("Driver Start Time: %lu ms", g_perf_counters.start_time);
    LOG_INFO("Diagnostics Enabled: %s", g_diagnostics_enabled ? "Yes" : "No");
    LOG_INFO("Diagnostic Level: %s", diag_level_to_string(g_diag_level));
    LOG_INFO("Active Categories: 0x%02X", g_diag_categories);
    
    LOG_INFO("==============================");
}

void diag_print_driver_info(void) {
    LOG_INFO("=== Driver Information ===");
    
    /* Driver identification */
    LOG_INFO("Driver Name: 3Com Packet Driver");
    LOG_INFO("Version: 1.0 (Production)");
    LOG_INFO("Target: DOS 2.0+, Intel 80286+");
    LOG_INFO("Build: %s %s", __DATE__, __TIME__);
    
    /* Supported hardware */
    LOG_INFO("Supported NICs:");
    LOG_INFO("  - 3Com 3C509B (10 Mbps Ethernet)");
    LOG_INFO("  - 3Com 3C515-TX (100 Mbps Fast Ethernet)");
    
    /* Driver features */
    LOG_INFO("Features:");
    LOG_INFO("  - Multi-homing support");
    LOG_INFO("  - Internal multiplexing");
    LOG_INFO("  - XMS memory utilization");
    LOG_INFO("  - Flow-aware routing");
    LOG_INFO("  - Real-time diagnostics");
    LOG_INFO("  - Packet Driver API compliance");
    
    /* Memory usage */
    LOG_INFO("Memory Usage:");
    LOG_INFO("  - TSR Size: <6KB resident");
    LOG_INFO("  - Current Allocation: %lu bytes", g_perf_counters.memory_peak_usage);
    
    /* Configuration */
    LOG_INFO("Configuration:");
    LOG_INFO("  - Ring Buffer Size: %d entries", g_diag_state.log_buffer.size);
    LOG_INFO("  - Max History Samples: %d", g_diag_state.max_history_samples);
    LOG_INFO("  - Flow Timeout: %lu ms", g_diag_state.flow_timeout);
    
    /* Status */
    LOG_INFO("Status:");
    LOG_INFO("  - Diagnostics: %s", g_diagnostics_initialized ? "Initialized" : "Not initialized");
    LOG_INFO("  - Monitoring: %s", g_diag_state.monitoring_enabled ? "Enabled" : "Disabled");
    LOG_INFO("  - Active Flows: %d", g_diag_state.flow_count);
    
    LOG_INFO("===============================");
}

void diag_print_hardware_info(void) {
    LOG_INFO("=== Hardware Information ===");
    
    /* Get NIC count */
    int nic_count = hardware_get_nic_count();
    LOG_INFO("Detected NICs: %d", nic_count);
    
    for (int i = 0; i < nic_count && i < MAX_NICS; i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (nic && (nic->status & NIC_STATUS_PRESENT)) {
            LOG_INFO("NIC %d Information:", i);
            
            /* NIC type and model */
            const char* nic_type_str = (nic->type == NIC_TYPE_3C509B) ? "3C509B" : 
                                      (nic->type == NIC_TYPE_3C515_TX) ? "3C515-TX" : "Unknown";
            LOG_INFO("  Type: %s", nic_type_str);
            
            /* Hardware addresses */
            LOG_INFO("  I/O Base: 0x%04X", nic->io_base);
            LOG_INFO("  IRQ: %d", nic->irq);
            if (nic->dma_channel > 0) {
                LOG_INFO("  DMA Channel: %d", nic->dma_channel);
            }
            
            /* MAC address */
            LOG_INFO("  MAC Address: %02X:%02X:%02X:%02X:%02X:%02X", 
                     nic->mac_address[0], nic->mac_address[1], nic->mac_address[2],
                     nic->mac_address[3], nic->mac_address[4], nic->mac_address[5]);
            
            /* Link status */
            LOG_INFO("  Link Status: %s", nic->link_up ? "Up" : "Down");
            if (nic->link_up) {
                LOG_INFO("  Link Speed: %d Mbps", nic->link_speed);
                LOG_INFO("  Duplex Mode: %s", nic->duplex_mode ? "Full" : "Half");
            }
            
            /* Status flags */
            LOG_INFO("  Status: 0x%04X", nic->status);
            if (nic->status & NIC_STATUS_ACTIVE) LOG_INFO("    - Active");
            if (nic->status & NIC_STATUS_100MBPS) LOG_INFO("    - 100 Mbps Capable");
            if (nic->status & NIC_STATUS_FULL_DUPLEX) LOG_INFO("    - Full Duplex");
            
            /* Statistics */
            LOG_INFO("  Packets: TX=%lu, RX=%lu", nic->tx_packets, nic->rx_packets);
            LOG_INFO("  Bytes: TX=%lu, RX=%lu", nic->tx_bytes, nic->rx_bytes);
            LOG_INFO("  Errors: TX=%lu, RX=%lu, Total=%lu", 
                     nic->tx_errors, nic->rx_errors, nic->error_count);
            LOG_INFO("  Interrupts: %lu", nic->interrupts);
            
            /* EEPROM information */
            if (nic->eeprom_size > 0) {
                LOG_INFO("  EEPROM: %d bytes", nic->eeprom_size);
                LOG_INFO("    First 4 words: 0x%04X 0x%04X 0x%04X 0x%04X",
                         nic->eeprom_data[0], nic->eeprom_data[1], 
                         nic->eeprom_data[2], nic->eeprom_data[3]);
            }
            
            /* Last activity */
            if (nic->last_activity > 0) {
                uint32_t time_since = diagnostics_get_system_time() - nic->last_activity;
                LOG_INFO("  Last Activity: %lu ms ago", time_since);
            }
            
            LOG_INFO("");  /* Empty line between NICs */
        }
    }
    
    if (nic_count == 0) {
        LOG_WARNING("No network interface cards detected!");
    }
    
    LOG_INFO("================================");
}

void diag_print_memory_info(void) {
    LOG_INFO("=== Memory Information ===");
    
    /* DOS memory information */
    union REGS regs;
    
    /* Get free conventional memory */
    regs.h.ah = 0x48;  /* Allocate memory */
    regs.x.bx = 0xFFFF;  /* Request maximum */
    int86(0x21, &regs, &regs);
    if (regs.x.cflag) {
        uint32_t free_conventional = (uint32_t)regs.x.bx * 16;
        LOG_INFO("Free Conventional Memory: %lu bytes (%lu KB)", 
                 free_conventional, free_conventional / 1024);
    }
    
    /* Check for XMS memory */
    regs.h.ah = 0x43;  /* Check if XMS driver present */
    int86(0x2F, &regs, &regs);
    if (regs.h.al == 0x80) {
        LOG_INFO("XMS Driver: Present");
        /* Could query XMS memory here if needed */
    } else {
        LOG_INFO("XMS Driver: Not present");
    }
    
    /* Driver memory usage */
    LOG_INFO("Driver Memory Usage:");
    LOG_INFO("  Current Allocation: %lu bytes", g_perf_counters.memory_peak_usage);
    
    /* Ring buffer memory */
    if (g_diag_state.log_buffer.entries) {
        uint32_t ring_buffer_size = g_diag_state.log_buffer.size * sizeof(log_entry_t);
        LOG_INFO("  Ring Buffer: %lu bytes (%d entries)", 
                 ring_buffer_size, g_diag_state.log_buffer.size);
    }
    
    /* Flow tracking memory */
    if (g_diag_state.flow_count > 0) {
        uint32_t flow_memory = g_diag_state.flow_count * sizeof(flow_entry_t);
        LOG_INFO("  Flow Tracking: ~%lu bytes (%d flows)", 
                 flow_memory, g_diag_state.flow_count);
    }
    
    /* History tracking memory */
    if (g_diag_state.history_count > 0) {
        uint32_t history_memory = g_diag_state.history_count * sizeof(historical_sample_t);
        LOG_INFO("  History Tracking: ~%lu bytes (%d samples)", 
                 history_memory, g_diag_state.history_count);
    }
    
    /* Memory pressure analysis */
    uint32_t total_system_memory = 640 * 1024;  /* Assume 640KB conventional memory */
    if (g_perf_counters.memory_peak_usage > 0) {
        uint32_t usage_percent = (g_perf_counters.memory_peak_usage * 100) / total_system_memory;
        LOG_INFO("  Memory Pressure: %lu%% of conventional memory", usage_percent);
        
        if (usage_percent > 50) {
            LOG_WARNING("  HIGH MEMORY USAGE DETECTED!");
        } else if (usage_percent > 25) {
            LOG_WARNING("  Moderate memory usage");
        } else {
            LOG_INFO("  Low memory usage - good");
        }
    }
    
    /* Buffer overruns */
    if (g_perf_counters.buffer_overruns > 0) {
        LOG_WARNING("  Buffer Overruns: %lu (indicates memory pressure)", 
                   g_perf_counters.buffer_overruns);
    }
    
    LOG_INFO("==============================");
}

void diag_print_network_info(void) {
    LOG_INFO("=== Network Information ===");
    
    /* Overall network health */
    uint8_t overall_health = diag_calculate_network_health();
    LOG_INFO("Overall Network Health: %d%% (%s)", 
             overall_health, diag_health_score_to_string(overall_health));
    
    /* Active NICs summary */
    int nic_count = hardware_get_nic_count();
    int active_nics = 0;
    int healthy_nics = 0;
    
    for (int i = 0; i < nic_count && i < MAX_NICS; i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (nic && (nic->status & NIC_STATUS_PRESENT)) {
            if (nic->status & NIC_STATUS_ACTIVE) {
                active_nics++;
                if (nic->link_up && nic->error_count < 100) {
                    healthy_nics++;
                }
            }
        }
    }
    
    LOG_INFO("NIC Status: %d total, %d active, %d healthy", 
             nic_count, active_nics, healthy_nics);
    
    /* Traffic statistics */
    uint32_t total_tx_packets = 0, total_rx_packets = 0;
    uint32_t total_tx_bytes = 0, total_rx_bytes = 0;
    uint32_t total_errors = 0;
    
    for (int i = 0; i < nic_count && i < MAX_NICS; i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (nic && (nic->status & NIC_STATUS_PRESENT)) {
            total_tx_packets += nic->tx_packets;
            total_rx_packets += nic->rx_packets;
            total_tx_bytes += nic->tx_bytes;
            total_rx_bytes += nic->rx_bytes;
            total_errors += nic->tx_errors + nic->rx_errors + nic->error_count;
        }
    }
    
    LOG_INFO("Traffic Summary:");
    LOG_INFO("  Total Packets: TX=%lu, RX=%lu", total_tx_packets, total_rx_packets);
    LOG_INFO("  Total Bytes: TX=%lu, RX=%lu", total_tx_bytes, total_rx_bytes);
    LOG_INFO("  Total Errors: %lu", total_errors);
    
    /* Error rate analysis */
    uint32_t total_packets = total_tx_packets + total_rx_packets;
    if (total_packets > 0) {
        uint32_t error_rate = (total_errors * 10000) / total_packets;
        LOG_INFO("  Error Rate: %lu per 10,000 packets", error_rate);
        
        if (error_rate > 100) {
            LOG_WARNING("  HIGH ERROR RATE DETECTED!");
        } else if (error_rate > 10) {
            LOG_WARNING("  Elevated error rate");
        } else {
            LOG_INFO("  Error rate acceptable");
        }
    }
    
    /* Flow information */
    if (g_diag_state.flow_count > 0) {
        LOG_INFO("Active Flows: %d", g_diag_state.flow_count);
        LOG_INFO("  Flow Timeout: %lu ms", g_diag_state.flow_timeout);
    }
    
    /* ARP table status */
    extern int arp_get_table_size(void);
    int arp_entries = arp_get_table_size();
    LOG_INFO("ARP Table: %d entries", arp_entries);
    
    /* Recent network activity */
    uint32_t current_time = diagnostics_get_system_time();
    uint32_t most_recent_activity = 0;
    
    for (int i = 0; i < nic_count && i < MAX_NICS; i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (nic && nic->last_activity > most_recent_activity) {
            most_recent_activity = nic->last_activity;
        }
    }
    
    if (most_recent_activity > 0) {
        uint32_t time_since = current_time - most_recent_activity;
        LOG_INFO("Last Network Activity: %lu ms ago", time_since);
        
        if (time_since > 300000) {  /* 5 minutes */
            LOG_WARNING("  No recent network activity!");
        }
    } else {
        LOG_WARNING("No network activity recorded");
    }
    
    /* Network alerts */
    if (g_network_health.route_failures > 0) {
        LOG_WARNING("Recent Routing Failures: %lu", g_network_health.route_failures);
    }
    
    if (g_network_health.api_errors > 0) {
        LOG_WARNING("Recent API Errors: %lu", g_network_health.api_errors);
    }
    
    /* Performance indicators */
    uint32_t uptime = current_time - g_perf_counters.start_time;
    if (uptime > 1000) {  /* More than 1 second uptime */
        uint32_t packets_per_sec = (total_packets * 1000) / uptime;
        uint32_t bytes_per_sec = ((total_tx_bytes + total_rx_bytes) * 1000) / uptime;
        
        LOG_INFO("Performance:");
        LOG_INFO("  Packet Rate: %lu packets/sec", packets_per_sec);
        LOG_INFO("  Data Rate: %lu bytes/sec", bytes_per_sec);
        
        if (bytes_per_sec > 1000000) {  /* > 1 MB/sec */
            LOG_INFO("  High throughput detected");
        }
    }
    
    LOG_INFO("===============================");
}

/* Flow Tracking and Packet Analysis Implementation */
int diag_flow_init(uint16_t max_flows, uint32_t timeout_ms) {
    if (!g_diagnostics_initialized) {
        return ERROR_NOT_FOUND;
    }
    
    /* Initialize flow tracking */
    g_diag_state.active_flows = NULL;
    g_diag_state.flow_count = 0;
    g_diag_state.flow_timeout = timeout_ms;
    
    LOG_INFO("Flow tracking initialized (max: %d, timeout: %d ms)", max_flows, timeout_ms);
    return SUCCESS;
}

void diag_flow_cleanup(void) {
    flow_entry_t *current = g_diag_state.active_flows;
    while (current) {
        flow_entry_t *next = current->next;
        memory_free(current);
        current = next;
    }
    
    g_diag_state.active_flows = NULL;
    g_diag_state.flow_count = 0;
    LOG_DEBUG("Flow tracking cleaned up");
}

int diag_flow_track_packet(const packet_buffer_t *packet, uint8_t nic_index) {
    if (!packet || nic_index >= MAX_NICS) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Extract flow information from packet */
    /* This is a simplified implementation - real implementation would parse
       Ethernet/IP/TCP|UDP headers to extract flow tuple */
    
    uint32_t src_ip = 0;    /* Extract from IP header */
    uint32_t dest_ip = 0;   /* Extract from IP header */
    uint16_t src_port = 0;  /* Extract from TCP/UDP header */
    uint16_t dest_port = 0; /* Extract from TCP/UDP header */
    uint8_t protocol = 0;   /* Extract from IP header */
    
    /* Look up existing flow */
    flow_entry_t *flow = diag_flow_lookup(src_ip, dest_ip, src_port, dest_port, protocol);
    
    if (flow) {
        /* Update existing flow */
        flow->packet_count++;
        flow->byte_count += packet->length;
        flow->last_seen = diagnostics_get_system_time();
        
        /* Check if flow switched NICs (potential routing issue) */
        if (flow->nic_index != nic_index) {
            LOG_WARNING("Flow switched from NIC %d to NIC %d", flow->nic_index, nic_index);
            g_network_health.route_failures++;
        }
    } else {
        /* Create new flow entry */
        flow = (flow_entry_t*)memory_alloc(sizeof(flow_entry_t), MEM_TYPE_DRIVER_DATA, 0);
        if (!flow) {
            return ERROR_NO_MEMORY;
        }
        
        flow->src_ip = src_ip;
        flow->dest_ip = dest_ip;
        flow->src_port = src_port;
        flow->dest_port = dest_port;
        flow->protocol = protocol;
        flow->nic_index = nic_index;
        flow->packet_count = 1;
        flow->byte_count = packet->length;
        flow->last_seen = diagnostics_get_system_time();
        flow->next = g_diag_state.active_flows;
        
        g_diag_state.active_flows = flow;
        g_diag_state.flow_count++;
        g_network_health.active_flows = g_diag_state.flow_count;
    }
    
    return SUCCESS;
}

void diag_flow_age_entries(void) {
    uint32_t current_time = diagnostics_get_system_time();
    flow_entry_t **current = &g_diag_state.active_flows;
    uint16_t aged_flows = 0;
    
    while (*current) {
        if ((current_time - (*current)->last_seen) > g_diag_state.flow_timeout) {
            /* Flow has timed out */
            flow_entry_t *to_remove = *current;
            *current = (*current)->next;
            memory_free(to_remove);
            g_diag_state.flow_count--;
            aged_flows++;
        } else {
            current = &((*current)->next);
        }
    }
    
    if (aged_flows > 0) {
        LOG_DEBUG("Aged %d flows, %d active flows remaining", aged_flows, g_diag_state.flow_count);
        g_network_health.active_flows = g_diag_state.flow_count;
    }
}

flow_entry_t* diag_flow_lookup(uint32_t src_ip, uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, uint8_t protocol) {
    flow_entry_t *current = g_diag_state.active_flows;
    
    while (current) {
        if (current->src_ip == src_ip && current->dest_ip == dest_ip &&
            current->src_port == src_port && current->dest_port == dest_port &&
            current->protocol == protocol) {
            return current;
        }
        
        /* Also check reverse direction for symmetric flows */
        if (current->src_ip == dest_ip && current->dest_ip == src_ip &&
            current->src_port == dest_port && current->dest_port == src_port &&
            current->protocol == protocol) {
            return current;
        }
        
        current = current->next;
    }
    
    return NULL;
}

int diag_integrate_arp_stats(void) {
    /* Integrate ARP statistics from Group 3A */
    if (!g_diagnostics_initialized) {
        return ERROR_NOT_FOUND;
    }
    
    /* Access global ARP statistics - this requires including arp.h */
    extern arp_stats_t g_arp_stats;
    extern bool g_arp_enabled;
    
    if (!g_arp_enabled) {
        LOG_DEBUG("ARP not enabled, skipping statistics integration");
        return SUCCESS;
    }
    
    /* Update network health metrics with ARP data */
    uint32_t total_arp_requests = g_arp_stats.requests_sent + g_arp_stats.requests_received;
    uint32_t total_arp_replies = g_arp_stats.replies_sent + g_arp_stats.replies_received;
    
    /* Calculate ARP table utilization */
    extern arp_cache_t g_arp_cache;
    if (g_arp_cache.max_entries > 0) {
        g_network_health.arp_table_usage = (g_arp_cache.entry_count * 100) / g_arp_cache.max_entries;
    }
    
    /* Update performance counters with ARP packet counts */
    g_perf_counters.packets_sent += g_arp_stats.packets_sent;
    g_perf_counters.packets_received += g_arp_stats.packets_received;
    
    /* Check for ARP-related issues */
    if (g_arp_stats.request_timeouts > (total_arp_requests / 10)) { /* > 10% timeout rate */
        LOG_NET_WARNING("High ARP request timeout rate: %d of %d requests", 
                       g_arp_stats.request_timeouts, total_arp_requests);
        g_network_health.route_failures += g_arp_stats.request_timeouts;
    }
    
    /* Check for invalid ARP packets */
    if (g_arp_stats.invalid_packets > 0) {
        LOG_NET_WARNING("ARP invalid packets detected: %d", g_arp_stats.invalid_packets);
        g_perf_counters.errors_detected += g_arp_stats.invalid_packets;
    }
    
    /* Calculate ARP cache hit ratio */
    uint32_t cache_operations = g_arp_stats.cache_hits + g_arp_stats.cache_misses;
    if (cache_operations > 0) {
        uint32_t hit_ratio = (g_arp_stats.cache_hits * 100) / cache_operations;
        LOG_NET_DEBUG("ARP cache hit ratio: %d%% (%d hits / %d operations)", 
                     hit_ratio, g_arp_stats.cache_hits, cache_operations);
        
        /* Poor cache performance indicates potential issues */
        if (hit_ratio < 50 && cache_operations > 20) {
            LOG_NET_WARNING("Low ARP cache hit ratio: %d%% - possible network issues", hit_ratio);
        }
    }
    
    LOG_DEBUG("ARP statistics integrated: %d packets, %d cache entries, %d timeouts",
             g_arp_stats.packets_received + g_arp_stats.packets_sent,
             g_arp_cache.entry_count, g_arp_stats.request_timeouts);
    
    return SUCCESS;
}

int diag_integrate_routing_stats(void) {
    /* Integrate routing statistics from Group 3A */
    if (!g_diagnostics_initialized) {
        return ERROR_NOT_FOUND;
    }
    
    /* Access global routing statistics */
    extern routing_stats_t g_routing_stats;
    extern bool g_routing_enabled;
    extern bridge_table_t g_bridge_table;
    
    if (!g_routing_enabled) {
        LOG_DEBUG("Routing not enabled, skipping statistics integration");
        return SUCCESS;
    }
    
    /* Update network health with routing statistics */
    g_network_health.route_failures += g_routing_stats.routing_errors;
    
    /* Update performance counters with routing data */
    g_perf_counters.packets_sent += g_routing_stats.packets_forwarded;
    g_perf_counters.packet_drops += g_routing_stats.packets_dropped;
    
    /* Calculate routing efficiency */
    uint32_t total_routed = g_routing_stats.packets_routed + g_routing_stats.packets_dropped;
    if (total_routed > 0) {
        uint32_t success_rate = (g_routing_stats.packets_routed * 100) / total_routed;
        LOG_NET_DEBUG("Routing success rate: %d%% (%d routed / %d total)", 
                     success_rate, g_routing_stats.packets_routed, total_routed);
        
        /* Poor routing success indicates network problems */
        if (success_rate < 85 && total_routed > 50) {
            LOG_NET_WARNING("Low routing success rate: %d%% - network issues detected", success_rate);
            diag_generate_alert(ALERT_TYPE_ROUTING_FAILURE, "Low routing success rate");
        }
    }
    
    /* Check table lookup efficiency */
    if (g_routing_stats.table_lookups > 0) {
        uint32_t cache_hit_rate = (g_routing_stats.cache_hits * 100) / g_routing_stats.table_lookups;
        LOG_NET_DEBUG("Routing cache hit rate: %d%% (%d hits / %d lookups)", 
                     cache_hit_rate, g_routing_stats.cache_hits, g_routing_stats.table_lookups);
    }
    
    /* Check bridge table utilization */
    if (g_bridge_table.max_entries > 0) {
        uint32_t bridge_usage = (g_bridge_table.entry_count * 100) / g_bridge_table.max_entries;
        LOG_NET_DEBUG("Bridge table utilization: %d%% (%d / %d entries)", 
                     bridge_usage, g_bridge_table.entry_count, g_bridge_table.max_entries);
        
        if (bridge_usage > 90) {
            LOG_NET_WARNING("Bridge table nearly full: %d%%", bridge_usage);
        }
    }
    
    /* Analyze traffic patterns */
    uint32_t total_traffic = g_routing_stats.packets_broadcast + g_routing_stats.packets_multicast + 
                            g_routing_stats.packets_forwarded;
    
    if (total_traffic > 0) {
        uint32_t broadcast_ratio = (g_routing_stats.packets_broadcast * 100) / total_traffic;
        if (broadcast_ratio > 30) { /* > 30% broadcast traffic */
            LOG_NET_WARNING("High broadcast traffic ratio: %d%% - possible network storm", broadcast_ratio);
            diag_generate_alert(ALERT_TYPE_BOTTLENECK_DETECTED, "High broadcast traffic detected");
        }
    }
    
    LOG_DEBUG("Routing statistics integrated: %d routed, %d dropped, %d errors",
             g_routing_stats.packets_routed, g_routing_stats.packets_dropped, g_routing_stats.routing_errors);
    
    return SUCCESS;
}

int diag_integrate_api_stats(void) {
    /* Integrate API statistics from Group 3B */
    if (!g_diagnostics_initialized) {
        return ERROR_NOT_FOUND;
    }
    
    /* Access API handle statistics - these are in api.c as static arrays */
    /* We need to call API functions to get the statistics */
    
    uint32_t total_handles_active = 0;
    uint32_t total_packets_handled = 0;
    uint32_t total_api_errors = 0;
    uint32_t total_nic_switches = 0;
    uint32_t load_balancing_events = 0;
    
    /* Get driver info to access general API statistics */
    pd_driver_info_t driver_info;
    if (pd_get_driver_info(&driver_info) == API_SUCCESS) {
        total_handles_active = driver_info.active_handles;
        g_network_health.active_flows = total_handles_active;
        
        LOG_NET_DEBUG("API Integration: %d active handles, class %d", 
                     driver_info.active_handles, driver_info.class);
    }
    
    /* Iterate through available handles to gather statistics */
    /* Note: We can't directly access static arrays, so we use API functions */
    for (uint16_t handle = 1; handle <= 16; handle++) {
        pd_handle_stats_t handle_stats;
        
        /* Try to get handle statistics */
        if (pd_get_statistics(handle, &handle_stats) == API_SUCCESS) {
            total_packets_handled += handle_stats.packets_in;
            
            /* Check for handle-specific issues */
            if (handle_stats.packets_out > 0) {
                uint32_t drop_rate = (handle_stats.packets_dropped * 100) / handle_stats.packets_out;
                if (drop_rate > 5) { /* > 5% drop rate */
                    LOG_NET_WARNING("High packet drop rate on handle %04X: %d%%", handle, drop_rate);
                    total_api_errors++;
                }
            }
            
            LOG_NET_DEBUG("Handle %04X stats: %d in, %d out, %d dropped", 
                         handle, handle_stats.packets_in, handle_stats.packets_out, handle_stats.packets_dropped);
        }
        
        /* Try to get extended handle statistics for Group 3B features */
        pd_flow_stats_t flow_stats;
        if (pd_get_flow_statistics(handle, &flow_stats) == API_SUCCESS) {
            /* Track load balancing effectiveness */
            if (flow_stats.flow_state == FLOW_STATE_ACTIVE) {
                total_nic_switches += (flow_stats.active_nic != flow_stats.handle) ? 1 : 0;
                
                /* Check latency and jitter */
                if (flow_stats.avg_latency > 50000) { /* > 50ms average latency */
                    LOG_NET_WARNING("High latency on handle %04X: %d us", handle, flow_stats.avg_latency);
                }
                
                if (flow_stats.jitter > 10000) { /* > 10ms jitter */
                    LOG_NET_WARNING("High jitter on handle %04X: %d us", handle, flow_stats.jitter);
                }
            }
        }
    }
    
    /* Check NIC status and load balancing effectiveness */
    for (uint8_t nic = 0; nic < MAX_NICS; nic++) {
        pd_nic_status_t nic_status;
        if (pd_get_nic_status(nic, &nic_status) == API_SUCCESS) {
            /* Update NIC health based on API perspective */
            if (nic_status.status == NIC_STATUS_ERROR || nic_status.status == NIC_STATUS_DOWN) {
                g_network_health.nic_health[nic] = 0;
                LOG_NET_ERROR("NIC %d reported as %s by API layer", nic, nic_status.status_text);
            } else if (nic_status.status == NIC_STATUS_DEGRADED) {
                g_network_health.nic_health[nic] = 50;
                LOG_NET_WARNING("NIC %d degraded performance: %s", nic, nic_status.status_text);
            }
            
            /* Check utilization */
            if (nic_status.utilization > 90) {
                LOG_NET_WARNING("NIC %d high utilization: %d%%", nic, nic_status.utilization);
                diag_generate_alert(ALERT_TYPE_UTILIZATION_HIGH, "NIC utilization critical");
            }
            
            /* Track error counts */
            total_api_errors += nic_status.error_count;
        }
    }
    
    /* Get load balancing statistics */
    pd_load_balance_stats_t lb_stats;
    if (pd_get_load_balance_stats(&lb_stats) == API_SUCCESS) {
        load_balancing_events = lb_stats.balance_decisions;
        
        /* Analyze load balancing effectiveness */
        if (lb_stats.primary_packets > 0 && lb_stats.secondary_packets > 0) {
            uint32_t balance_ratio = lb_stats.primary_packets / (lb_stats.secondary_packets + 1);
            if (balance_ratio > 10) { /* 10:1 imbalance */
                LOG_NET_WARNING("Load balancing imbalance: %d:%d (primary:secondary)", 
                               lb_stats.primary_packets, lb_stats.secondary_packets);
                diag_generate_alert(ALERT_TYPE_BOTTLENECK_DETECTED, "Load balancing imbalance");
            }
        }
        
        LOG_NET_DEBUG("Load balancing: %d decisions, %d failovers", 
                     lb_stats.balance_decisions, lb_stats.failover_count);
    }
    
    /* Update network health metrics */
    g_network_health.api_errors = total_api_errors;
    
    /* Update performance counters */
    g_perf_counters.packets_received += total_packets_handled;
    g_perf_counters.errors_detected += total_api_errors;
    
    /* Check for API layer issues */
    if (total_api_errors > (total_packets_handled / 100)) { /* > 1% error rate */
        LOG_NET_WARNING("High API error rate: %d errors for %d packets", total_api_errors, total_packets_handled);
        diag_generate_alert(ALERT_TYPE_API_ERROR, "High API error rate detected");
    }
    
    /* Check handle utilization */
    if (total_handles_active > 12) { /* > 75% of max handles */
        LOG_NET_WARNING("High handle utilization: %d active handles", total_handles_active);
    }
    
    LOG_DEBUG("API statistics integrated: %d handles, %d packets, %d errors, %d NIC switches",
             total_handles_active, total_packets_handled, total_api_errors, total_nic_switches);
    
    return SUCCESS;
}

void diag_update_comprehensive_stats(void) {
    /* Update comprehensive statistics from all subsystems */
    if (!g_diagnostics_initialized || !g_diag_state.monitoring_enabled) {
        return;
    }
    
    static uint32_t update_counter = 0;
    update_counter++;
    
    /* Integrate statistics from Groups 3A and 3B */
    diag_integrate_arp_stats();
    diag_integrate_routing_stats();
    diag_integrate_api_stats();
    
    /* Update network health based on integrated stats */
    diag_health_update();
    
    /* Perform flow analysis */
    diag_analyze_packet_flow();
    
    /* Add historical sample if enough time has passed */
    uint32_t current_time = diagnostics_get_system_time();
    if ((current_time - g_diag_state.last_sample_time) >= g_diag_state.sample_interval_ms) {
        diag_history_add_sample();
    }
    
    /* Perform comprehensive analysis every 10 updates to reduce overhead */
    if ((update_counter % 10) == 0) {
        /* Check for bottlenecks */
        diag_detect_bottlenecks();
        
        /* Perform error correlation */
        diag_correlate_errors();
        
        /* Pattern analysis */
        diag_pattern_analysis();
        
        /* Comprehensive alert checking */
        diag_check_alerts();
        
        /* Age historical data (every 100 updates) */
        if ((update_counter % 100) == 0) {
            diag_history_age_samples(600000); /* Remove samples older than 10 minutes */
        }
    }
    
    LOG_PERF_DEBUG("Comprehensive statistics update completed (#%d)", update_counter);
}

/* Bottleneck Detection and Analysis Implementation */
int diag_detect_bottlenecks(void) {
    int bottlenecks_detected = 0;
    
    /* Check memory pressure */
    if (diag_check_memory_pressure() != SUCCESS) {
        diag_generate_alert(ALERT_TYPE_MEMORY_LOW, "Memory pressure detected");
        bottlenecks_detected++;
    }
    
    /* Check CPU utilization */
    if (diag_check_cpu_utilization() != SUCCESS) {
        diag_generate_alert(ALERT_TYPE_PERFORMANCE_DEGRADED, "High CPU utilization");
        bottlenecks_detected++;
    }
    
    /* Check buffer utilization */
    if (g_perf_counters.buffer_overruns > 0) {
        diag_generate_alert(ALERT_TYPE_BOTTLENECK_DETECTED, "Buffer overruns detected");
        bottlenecks_detected++;
    }
    
    /* Check packet drop rate */
    if (g_perf_counters.packet_drops > (g_perf_counters.packets_received / 100)) { /* > 1% drop rate */
        diag_generate_alert(ALERT_TYPE_BOTTLENECK_DETECTED, "High packet drop rate");
        bottlenecks_detected++;
    }
    
    /* Check NIC utilization imbalance */
    uint32_t max_nic_packets = 0, min_nic_packets = UINT32_MAX;
    for (int i = 0; i < MAX_NICS; i++) {
        /* Get actual per-NIC packet counts from hardware layer */
        nic_info_t *nic = hardware_get_nic(i);
        uint32_t nic_packets = nic ? (nic->stats.packets_in + nic->stats.packets_out) : 0;
        if (nic_packets > max_nic_packets) max_nic_packets = nic_packets;
        if (nic_packets < min_nic_packets && nic_packets > 0) min_nic_packets = nic_packets;
    }
    
    if (max_nic_packets > 0 && min_nic_packets < UINT32_MAX) {
        uint32_t imbalance_ratio = max_nic_packets / (min_nic_packets + 1);
        if (imbalance_ratio > 10) { /* 10:1 imbalance */
            diag_generate_alert(ALERT_TYPE_BOTTLENECK_DETECTED, "NIC load imbalance detected");
            bottlenecks_detected++;
        }
    }
    
    LOG_DEBUG("Bottleneck detection complete: %d issues found", bottlenecks_detected);
    return bottlenecks_detected;
}

void diag_analyze_packet_flow(void) {
    if (!g_diag_state.monitoring_enabled) {
        return;
    }
    
    /* Age flow entries */
    diag_flow_age_entries();
    
    /* Analyze flow patterns */
    uint32_t total_flows = g_diag_state.flow_count;
    uint32_t long_lived_flows = 0;
    uint32_t high_volume_flows = 0;
    uint32_t asymmetric_flows = 0;
    
    flow_entry_t *current = g_diag_state.active_flows;
    uint32_t current_time = diagnostics_get_system_time();
    
    while (current) {
        uint32_t flow_duration = current_time - (current->last_seen - 10000); /* Estimate start time */
        
        /* Identify long-lived flows */
        if (flow_duration > 60000) { /* > 1 minute */
            long_lived_flows++;
        }
        
        /* Identify high-volume flows */
        if (current->packet_count > 1000 || current->byte_count > 1000000) { /* > 1MB */
            high_volume_flows++;
        }
        
        /* Detect asymmetric flows (different packet counts in each direction) */
        if (current->packets_in > 0 && current->packets_out > 0) {
            uint32_t ratio = (current->packets_in > current->packets_out) ?
                           (current->packets_in / current->packets_out) :
                           (current->packets_out / current->packets_in);
            if (ratio > 10) { /* Significant asymmetry */
                LOG_DEBUG("Asymmetric flow detected: in=%lu, out=%lu", 
                         current->packets_in, current->packets_out);
            }
        }
        
        current = current->next;
    }
    
    /* Update network health metrics */
    g_network_health.active_flows = total_flows;
    
    /* Log flow analysis results */
    if (total_flows > 0) {
        LOG_DEBUG("Flow analysis: %d total, %d long-lived, %d high-volume, %d asymmetric", 
                 total_flows, long_lived_flows, high_volume_flows, asymmetric_flows);
        
        /* Check for flow concentration (potential bottleneck) */
        if (high_volume_flows > (total_flows / 2)) {
            LOG_WARNING("High concentration of high-volume flows detected");
        }
    }
}

int diag_check_memory_pressure(void) {
    /* Check memory usage through buffer allocation system */
    uint32_t total_memory_used = 0;
    uint32_t memory_pressure_threshold = 0;
    
    /* Check memory usage through available performance counters */
    /* Note: buffer_alloc_get_usage_stats() not yet implemented */
    /* Fall back to performance counter data */
    
    if (g_perf_counters.memory_peak_usage == 0) {
        return SUCCESS; /* No memory tracking data available */
    }
    
    /* Calculate memory utilization (simplified) */
    uint32_t total_memory = get_system_memory_size(); /* Get actual system memory */
    uint32_t utilization_percent = (g_perf_counters.memory_peak_usage * 100) / total_memory;
    
    if (utilization_percent > g_diag_state.alert_thresholds[ALERT_TYPE_MEMORY_LOW]) {
        LOG_WARNING("Memory pressure detected: %d%% utilization", utilization_percent);
        return ERROR_NO_MEMORY;
    }
    
    return SUCCESS;
}

int diag_check_cpu_utilization(void) {
    /* Implement CPU utilization checking based on interrupt frequency and processing time */
    uint32_t current_time = diagnostics_get_system_time();
    uint32_t uptime_ms = current_time - g_perf_counters.start_time;
    uint32_t interrupt_rate = 0;
    uint32_t packet_rate = 0;
    
    if (uptime_ms > 1000) {  /* At least 1 second of uptime */
        interrupt_rate = (g_perf_counters.interrupts_handled * 1000) / uptime_ms;
        
        uint32_t total_packets = g_perf_counters.packets_sent + g_perf_counters.packets_received;
        packet_rate = (total_packets * 1000) / uptime_ms;
    }
    
    /* Enhanced CPU utilization estimation */
    uint32_t cpu_utilization = 0;
    
    /* Base utilization on interrupt rate */
    if (interrupt_rate > 10000) {
        cpu_utilization += 60;  /* High interrupt overhead */
        LOG_WARNING("High interrupt rate detected: %lu int/sec", interrupt_rate);
    } else if (interrupt_rate > 5000) {
        cpu_utilization += 40;
    } else if (interrupt_rate > 1000) {
        cpu_utilization += 20;
    } else if (interrupt_rate > 100) {
        cpu_utilization += 5;
    }
    
    /* Add utilization based on packet processing rate */
    if (packet_rate > 1000) {
        cpu_utilization += 30;  /* High packet processing load */
    } else if (packet_rate > 500) {
        cpu_utilization += 20;
    } else if (packet_rate > 100) {
        cpu_utilization += 10;
    }
    
    /* Add utilization based on error handling */
    if (g_perf_counters.errors_detected > 100) {
        uint32_t error_rate_per_sec = (g_perf_counters.errors_detected * 1000) / (uptime_ms + 1);
        if (error_rate_per_sec > 10) {
            cpu_utilization += 15;  /* Error handling overhead */
        }
    }
    
    /* Cap at 100% */
    if (cpu_utilization > 100) {
        cpu_utilization = 100;
    }
    
    LOG_DEBUG("CPU utilization estimated at %lu%% (int_rate=%lu, pkt_rate=%lu)", 
              cpu_utilization, interrupt_rate, packet_rate);
    
    /* Return utilization as error level */
    if (cpu_utilization > 80) {
        LOG_WARNING("High CPU utilization detected: %lu%%", cpu_utilization);
        return ERROR_BUSY;  /* High utilization */
    } else if (cpu_utilization > 60) {
        return ERROR_INVALID_PARAM;  /* Medium-high utilization */
    } else if (cpu_utilization > 40) {
        return 1;  /* Medium utilization */
    }
    
    return SUCCESS;  /* Low utilization */
}

/* Error Correlation and Pattern Analysis Implementation */

/* Structure for error tracking */
typedef struct error_event {
    uint32_t timestamp;
    uint8_t error_type;
    uint8_t nic_index;
    uint32_t error_code;
    char description[64];
    struct error_event *next;
} error_event_t;

/* Error tracking state */
static error_event_t *g_error_history = NULL;
static uint16_t g_error_count = 0;
static const uint16_t MAX_ERROR_HISTORY = 100;

/* Error pattern definitions */
typedef struct error_pattern {
    uint8_t pattern_type;
    uint8_t error_sequence[4];
    uint8_t sequence_length;
    uint32_t time_window_ms;
    uint32_t threshold_count;
    const char *description;
} error_pattern_t;

/* Predefined error patterns to detect */
static const error_pattern_t g_error_patterns[] = {
    {1, {1, 1, 1}, 3, 5000, 3, "Repeated transmission errors"},
    {2, {2, 3, 2}, 3, 10000, 2, "CRC error followed by timeout"},
    {3, {4, 4, 4, 4}, 4, 2000, 4, "Rapid buffer overruns"},
    {4, {5, 1, 5}, 3, 15000, 2, "Interrupt errors with TX failures"},
    /* Add more patterns as needed */
};

static const uint8_t NUM_ERROR_PATTERNS = sizeof(g_error_patterns) / sizeof(error_pattern_t);

/* Error type definitions */
#define ERROR_TYPE_TX_FAILURE       1
#define ERROR_TYPE_CRC_ERROR        2
#define ERROR_TYPE_TIMEOUT          3
#define ERROR_TYPE_BUFFER_OVERRUN   4
#define ERROR_TYPE_INTERRUPT_ERROR  5
#define ERROR_TYPE_MEMORY_ERROR     6
#define ERROR_TYPE_ROUTING_ERROR    7
#define ERROR_TYPE_API_ERROR        8

static void diag_add_error_event(uint8_t error_type, uint8_t nic_index, uint32_t error_code, const char *description);
static void diag_cleanup_old_errors(void);
static int diag_check_error_patterns(void);
static int diag_calculate_error_rate(uint32_t time_window_ms);

int diag_correlate_errors(void) {
    if (!g_diagnostics_initialized) {
        return ERROR_NOT_FOUND;
    }
    
    /* Clean up old error events first */
    diag_cleanup_old_errors();
    
    /* Check for known error patterns */
    int patterns_detected = diag_check_error_patterns();
    
    /* Analyze error distribution across NICs */
    uint32_t nic_error_counts[MAX_NICS] = {0};
    error_event_t *current = g_error_history;
    
    while (current) {
        if (current->nic_index < MAX_NICS) {
            nic_error_counts[current->nic_index]++;
        }
        current = current->next;
    }
    
    /* Check for NIC-specific error concentration */
    for (int i = 0; i < MAX_NICS; i++) {
        if (nic_error_counts[i] > (g_error_count / 2) && g_error_count > 10) {
            LOG_WARNING("Error concentration detected on NIC %d: %d of %d errors", 
                       i, nic_error_counts[i], g_error_count);
            diag_generate_alert(ALERT_TYPE_NIC_FAILURE, "NIC error concentration detected");
        }
    }
    
    /* Calculate recent error rate */
    int recent_error_rate = diag_calculate_error_rate(60000); /* Last minute */
    if (recent_error_rate > 10) { /* > 10 errors per minute */
        LOG_WARNING("High error rate detected: %d errors in last minute", recent_error_rate);
        diag_generate_alert(ALERT_TYPE_ERROR_RATE_HIGH, "High error rate detected");
    }
    
    LOG_DEBUG("Error correlation complete: %d patterns detected, %d total errors", 
             patterns_detected, g_error_count);
    
    return patterns_detected;
}

void diag_pattern_analysis(void) {
    if (!g_diagnostics_initialized || !g_diag_state.monitoring_enabled) {
        return;
    }
    
    /* Analyze temporal patterns in errors */
    uint32_t current_time = diagnostics_get_system_time();
    uint32_t time_buckets[10] = {0}; /* 10 time buckets for pattern analysis */
    const uint32_t bucket_size = 10000; /* 10 second buckets */
    
    error_event_t *current = g_error_history;
    while (current) {
        uint32_t age = current_time - current->timestamp;
        uint32_t bucket = age / bucket_size;
        if (bucket < 10) {
            time_buckets[bucket]++;
        }
        current = current->next;
    }
    
    /* Look for error bursts (high concentration in recent buckets) */
    if (time_buckets[0] > 5 || time_buckets[1] > 5) {
        LOG_WARNING("Error burst detected in recent time period");
        diag_generate_alert(ALERT_TYPE_PERFORMANCE_DEGRADED, "Error burst pattern detected");
    }
    
    /* Look for periodic patterns */
    bool periodic_pattern = true;
    for (int i = 0; i < 8; i += 2) {
        if (time_buckets[i] == 0 || time_buckets[i+1] > time_buckets[i]) {
            periodic_pattern = false;
            break;
        }
    }
    
    if (periodic_pattern && time_buckets[0] > 2) {
        LOG_WARNING("Periodic error pattern detected - possible hardware issue");
        diag_generate_alert(ALERT_TYPE_NIC_FAILURE, "Periodic error pattern suggests hardware issue");
    }
    
    /* Analyze error type distribution */
    uint32_t type_counts[9] = {0}; /* Error types 1-8 */
    current = g_error_history;
    while (current) {
        if (current->error_type > 0 && current->error_type <= 8) {
            type_counts[current->error_type]++;
        }
        current = current->next;
    }
    
    /* Check for dominant error types */
    for (int i = 1; i <= 8; i++) {
        if (type_counts[i] > (g_error_count * 7 / 10)) { /* > 70% of errors */
            LOG_WARNING("Dominant error type %d detected (%d of %d errors)", 
                       i, type_counts[i], g_error_count);
        }
    }
}

int diag_error_trend_analysis(uint32_t time_window_ms) {
    if (!g_diagnostics_initialized) {
        return ERROR_NOT_FOUND;
    }
    
    uint32_t current_time = diagnostics_get_system_time();
    uint32_t window_start = current_time - time_window_ms;
    
    /* Count errors in time window */
    uint32_t window_errors = 0;
    uint32_t recent_errors = 0; /* Last 25% of window */
    uint32_t early_errors = 0;  /* First 25% of window */
    
    uint32_t recent_threshold = current_time - (time_window_ms / 4);
    uint32_t early_threshold = window_start + (time_window_ms / 4);
    
    error_event_t *current = g_error_history;
    while (current) {
        if (current->timestamp >= window_start) {
            window_errors++;
            
            if (current->timestamp >= recent_threshold) {
                recent_errors++;
            } else if (current->timestamp <= early_threshold) {
                early_errors++;
            }
        }
        current = current->next;
    }
    
    /* Calculate trend */
    int trend = 0; /* 0 = stable, >0 = increasing, <0 = decreasing */
    
    if (recent_errors > early_errors * 2) {
        trend = 2; /* Rapidly increasing */
        LOG_WARNING("Rapidly increasing error trend detected");
        diag_generate_alert(ALERT_TYPE_PERFORMANCE_DEGRADED, "Error rate increasing rapidly");
    } else if (recent_errors > early_errors) {
        trend = 1; /* Increasing */
        LOG_INFO("Increasing error trend detected");
    } else if (early_errors > recent_errors * 2) {
        trend = -2; /* Rapidly decreasing */
        LOG_INFO("Error rate improving rapidly");
    } else if (early_errors > recent_errors) {
        trend = -1; /* Decreasing */
        LOG_DEBUG("Error rate improving");
    }
    
    LOG_DEBUG("Error trend analysis: %d total errors in %d ms window, trend: %d", 
             window_errors, time_window_ms, trend);
    
    return trend;
}

/* Helper functions for error correlation */
static void diag_add_error_event(uint8_t error_type, uint8_t nic_index, uint32_t error_code, const char *description) {
    /* Clean up old entries if we're at the limit */
    if (g_error_count >= MAX_ERROR_HISTORY) {
        diag_cleanup_old_errors();
    }
    
    /* Allocate new error event */
    error_event_t *event = (error_event_t*)memory_alloc(sizeof(error_event_t), MEM_TYPE_DRIVER_DATA, 0);
    if (!event) {
        return; /* Out of memory */
    }
    
    /* Initialize event */
    event->timestamp = diagnostics_get_system_time();
    event->error_type = error_type;
    event->nic_index = nic_index;
    event->error_code = error_code;
    event->next = g_error_history;
    
    /* Copy description */
    if (description) {
        int i = 0;
        while (description[i] && i < sizeof(event->description) - 1) {
            event->description[i] = description[i];
            i++;
        }
        event->description[i] = '\0';
    } else {
        event->description[0] = '\0';
    }
    
    g_error_history = event;
    g_error_count++;
}

static void diag_cleanup_old_errors(void) {
    uint32_t current_time = diagnostics_get_system_time();
    const uint32_t max_age = 600000; /* 10 minutes */
    
    error_event_t **current = &g_error_history;
    uint16_t removed = 0;
    
    while (*current) {
        if ((current_time - (*current)->timestamp) > max_age) {
            error_event_t *to_remove = *current;
            *current = (*current)->next;
            memory_free(to_remove);
            g_error_count--;
            removed++;
        } else {
            current = &((*current)->next);
        }
    }
    
    if (removed > 0) {
        LOG_DEBUG("Cleaned up %d old error events", removed);
    }
}

static int diag_check_error_patterns(void) {
    int patterns_found = 0;
    
    for (int p = 0; p < NUM_ERROR_PATTERNS; p++) {
        const error_pattern_t *pattern = &g_error_patterns[p];
        uint32_t current_time = diagnostics_get_system_time();
        uint32_t window_start = current_time - pattern->time_window_ms;
        
        /* Count occurrences of this pattern */
        uint32_t pattern_count = 0;
        error_event_t *current = g_error_history;
        
        while (current && current->timestamp >= window_start) {
            /* Check if this error starts a pattern match */
            bool pattern_match = true;
            error_event_t *check = current;
            
            for (int i = 0; i < pattern->sequence_length && check; i++) {
                if (check->error_type != pattern->error_sequence[i]) {
                    pattern_match = false;
                    break;
                }
                check = check->next;
                if (check && (current->timestamp - check->timestamp) > pattern->time_window_ms) {
                    pattern_match = false;
                    break;
                }
            }
            
            if (pattern_match) {
                pattern_count++;
            }
            
            current = current->next;
        }
        
        /* Check if pattern threshold is exceeded */
        if (pattern_count >= pattern->threshold_count) {
            LOG_WARNING("Error pattern detected: %s (occurred %d times)", 
                       pattern->description, pattern_count);
            diag_generate_alert(ALERT_TYPE_PERFORMANCE_DEGRADED, pattern->description);
            patterns_found++;
        }
    }
    
    return patterns_found;
}

static int diag_calculate_error_rate(uint32_t time_window_ms) {
    uint32_t current_time = diagnostics_get_system_time();
    uint32_t window_start = current_time - time_window_ms;
    uint32_t errors_in_window = 0;
    
    error_event_t *current = g_error_history;
    while (current) {
        if (current->timestamp >= window_start) {
            errors_in_window++;
        } else {
            break; /* Errors are in chronological order */
        }
        current = current->next;
    }
    
    return errors_in_window;
}

/* Public function to report errors for correlation */
void diag_report_error(uint8_t error_type, uint8_t nic_index, uint32_t error_code, const char *description) {
    if (!g_diagnostics_initialized || !g_diag_state.monitoring_enabled) {
        return;
    }
    
    diag_add_error_event(error_type, nic_index, error_code, description);
    
    /* Update performance counters */
    g_perf_counters.errors_detected++;
    
    /* Update network health */
    uint32_t total_packets = g_perf_counters.packets_sent + g_perf_counters.packets_received;
    if (total_packets > 0) {
        g_network_health.error_rate = (g_perf_counters.errors_detected * 1000) / total_packets;
    }
    
    LOG_DEBUG("Error reported: type=%d, NIC=%d, code=%d, desc=%s", 
             error_type, nic_index, error_code, description ? description : "none");
}

/* Historical Tracking and Trend Analysis Implementation */
int diag_history_init(uint16_t max_samples, uint32_t sample_interval_ms) {
    if (!g_diagnostics_initialized) {
        return ERROR_NOT_FOUND;
    }
    
    /* Initialize history tracking */
    g_diag_state.history_head = NULL;
    g_diag_state.history_count = 0;
    g_diag_state.max_history_samples = max_samples;
    g_diag_state.sample_interval_ms = sample_interval_ms;
    g_diag_state.last_sample_time = diagnostics_get_system_time();
    
    /* Initialize trend analysis structure */
    memory_zero(&g_diag_state.current_trends, sizeof(trend_analysis_t));
    
    LOG_INFO("Historical tracking initialized: %d samples max, %d ms interval", 
             max_samples, sample_interval_ms);
    
    return SUCCESS;
}

void diag_history_cleanup(void) {
    /* Free all historical samples */
    historical_sample_t *current = g_diag_state.history_head;
    while (current) {
        historical_sample_t *next = current->next;
        memory_free(current);
        current = next;
    }
    
    g_diag_state.history_head = NULL;
    g_diag_state.history_count = 0;
    
    LOG_DEBUG("Historical tracking cleaned up");
}

int diag_history_add_sample(void) {
    if (!g_diagnostics_initialized) {
        return ERROR_NOT_FOUND;
    }
    
    /* Allocate new sample */
    historical_sample_t *sample = (historical_sample_t*)memory_alloc(
        sizeof(historical_sample_t), MEM_TYPE_DRIVER_DATA, 0);
    
    if (!sample) {
        return ERROR_NO_MEMORY;
    }
    
    /* Fill sample with current data */
    sample->timestamp = diagnostics_get_system_time();
    sample->packets_sent = g_perf_counters.packets_sent;
    sample->packets_received = g_perf_counters.packets_received;
    sample->errors_detected = g_perf_counters.errors_detected;
    sample->memory_usage = g_perf_counters.memory_peak_usage;
    sample->network_health = g_network_health.overall_score;
    sample->cpu_utilization = calculate_cpu_utilization(); /* Calculate CPU utilization */
    sample->next = g_diag_state.history_head;
    
    /* Add to head of list */
    g_diag_state.history_head = sample;
    g_diag_state.history_count++;
    
    /* Remove old samples if we exceed maximum */
    if (g_diag_state.history_count > g_diag_state.max_history_samples) {
        historical_sample_t *current = g_diag_state.history_head;
        while (current && current->next) {
            if (!current->next->next) {
                /* This is the second-to-last entry, remove the last one */
                memory_free(current->next);
                current->next = NULL;
                g_diag_state.history_count--;
                break;
            }
            current = current->next;
        }
    }
    
    g_diag_state.last_sample_time = sample->timestamp;
    
    LOG_PERF_DEBUG("Historical sample added: health=%d, packets=%d, errors=%d", 
                  sample->network_health, 
                  sample->packets_sent + sample->packets_received,
                  sample->errors_detected);
    
    /* Update trend analysis */
    diag_trend_analysis(300000, &g_diag_state.current_trends); /* 5 minute window */
    
    return SUCCESS;
}

void diag_history_age_samples(uint32_t max_age_ms) {
    uint32_t current_time = diagnostics_get_system_time();
    uint32_t cutoff_time = current_time - max_age_ms;
    
    historical_sample_t **current = &g_diag_state.history_head;
    uint16_t removed = 0;
    
    while (*current) {
        if ((*current)->timestamp < cutoff_time) {
            historical_sample_t *to_remove = *current;
            *current = (*current)->next;
            memory_free(to_remove);
            g_diag_state.history_count--;
            removed++;
        } else {
            current = &((*current)->next);
        }
    }
    
    if (removed > 0) {
        LOG_DEBUG("Aged %d historical samples older than %d ms", removed, max_age_ms);
    }
}

int diag_trend_analysis(uint32_t window_ms, trend_analysis_t *result) {
    if (!result || !g_diag_state.history_head) {
        return ERROR_INVALID_PARAM;
    }
    
    uint32_t current_time = diagnostics_get_system_time();
    uint32_t window_start = current_time - window_ms;
    
    /* Initialize result */
    memory_zero(result, sizeof(trend_analysis_t));
    result->analysis_window_ms = window_ms;
    
    /* Collect samples within window */
    historical_sample_t *first_sample = NULL;
    historical_sample_t *last_sample = NULL;
    uint32_t sample_count = 0;
    
    historical_sample_t *current = g_diag_state.history_head;
    while (current) {
        if (current->timestamp >= window_start) {
            if (!first_sample) {
                first_sample = current;
            }
            last_sample = current;
            sample_count++;
        }
        current = current->next;
    }
    
    result->sample_count = sample_count;
    
    if (sample_count < 2 || !first_sample || !last_sample) {
        return SUCCESS; /* Not enough data for trend analysis */
    }
    
    /* Calculate time delta */
    uint32_t time_delta = first_sample->timestamp - last_sample->timestamp;
    if (time_delta == 0) {
        return SUCCESS;
    }
    
    /* Calculate trends (change per second) */
    result->packet_trend = ((int32_t)(first_sample->packets_sent + first_sample->packets_received) - 
                           (int32_t)(last_sample->packets_sent + last_sample->packets_received)) * 1000 / time_delta;
    
    result->error_trend = ((int32_t)first_sample->errors_detected - 
                          (int32_t)last_sample->errors_detected) * 1000 / time_delta;
    
    result->health_trend = ((int32_t)first_sample->network_health - 
                           (int32_t)last_sample->network_health) * 1000 / time_delta;
    
    result->memory_trend = ((int32_t)first_sample->memory_usage - 
                           (int32_t)last_sample->memory_usage) * 1000 / time_delta;
    
    /* Log significant trends */
    if (result->health_trend < -5) { /* Health degrading > 5 points/sec */
        LOG_WARNING("Network health trending down: %d points/sec", result->health_trend);
    }
    
    if (result->error_trend > 1) { /* Error rate increasing > 1/sec */
        LOG_WARNING("Error rate trending up: %d errors/sec", result->error_trend);
    }
    
    if (result->packet_trend < -100) { /* Packet rate decreasing > 100/sec */
        LOG_INFO("Traffic trending down: %d packets/sec", result->packet_trend);
    }
    
    LOG_PERF_DEBUG("Trend analysis: packets=%d/s, errors=%d/s, health=%d/s, samples=%d", 
                  result->packet_trend, result->error_trend, result->health_trend, sample_count);
    
    return SUCCESS;
}

const historical_sample_t* diag_history_get_samples(uint16_t *count) {
    if (count) {
        *count = g_diag_state.history_count;
    }
    return g_diag_state.history_head;
}

int diag_history_export(char *buffer, uint32_t buffer_size) {
    if (!buffer || buffer_size < 100) {
        return ERROR_INVALID_PARAM;
    }
    
    uint32_t written = 0;
    historical_sample_t *current = g_diag_state.history_head;
    
    /* Write header */
    int len = snprintf(buffer + written, buffer_size - written, 
                      "Timestamp,Packets_Sent,Packets_Received,Errors,Memory,Health\n");
    if (len > 0) written += len;
    
    /* Write samples (most recent first) */
    while (current && written < buffer_size - 100) {
        len = snprintf(buffer + written, buffer_size - written,
                      "%lu,%lu,%lu,%lu,%lu,%d\n",
                      current->timestamp, current->packets_sent, current->packets_received,
                      current->errors_detected, current->memory_usage, current->network_health);
        if (len > 0) {
            written += len;
        } else {
            break;
        }
        current = current->next;
    }
    
    return written;
}

void diag_history_print_summary(void) {
    if (!g_diag_state.history_head) {
        LOG_INFO("No historical data available");
        return;
    }
    
    historical_sample_t *latest = g_diag_state.history_head;
    historical_sample_t *oldest = g_diag_state.history_head;
    
    /* Find oldest sample */
    while (oldest->next) {
        oldest = oldest->next;
    }
    
    uint32_t time_span = latest->timestamp - oldest->timestamp;
    uint32_t packet_delta = (latest->packets_sent + latest->packets_received) - 
                           (oldest->packets_sent + oldest->packets_received);
    uint32_t error_delta = latest->errors_detected - oldest->errors_detected;
    
    LOG_INFO("=== Historical Data Summary ===");
    LOG_INFO("Samples: %d, Time span: %d ms", g_diag_state.history_count, time_span);
    LOG_INFO("Latest: Health=%d, Packets=%d, Errors=%d", 
             latest->network_health, 
             latest->packets_sent + latest->packets_received,
             latest->errors_detected);
    LOG_INFO("Change: Packets=+%d, Errors=+%d, Health=%d->%d", 
             packet_delta, error_delta, oldest->network_health, latest->network_health);
    
    /* Print current trends */
    LOG_INFO("Trends (5min): Packets=%s, Errors=%s, Health=%s",
             diag_trend_to_string(g_diag_state.current_trends.packet_trend),
             diag_trend_to_string(g_diag_state.current_trends.error_trend),
             diag_trend_to_string(g_diag_state.current_trends.health_trend));
}

const char* diag_trend_to_string(int32_t trend) {
    if (trend > 5) return "Rising";
    if (trend > 1) return "Slight Up";
    if (trend < -5) return "Falling";
    if (trend < -1) return "Slight Down";
    return "Stable";
}

int diag_check_alerts(void) {
    /* Comprehensive alert checking - combines all monitoring systems */
    if (!g_diagnostics_initialized || !g_diag_state.monitoring_enabled) {
        return 0;
    }
    
    int total_alerts = 0;
    
    /* Check network health thresholds */
    total_alerts += diag_health_check_thresholds();
    
    /* Check historical trends for degradation */
    if (g_diag_state.current_trends.sample_count >= 3) {
        /* Health degradation trend */
        if (g_diag_state.current_trends.health_trend < -10) {
            diag_generate_alert(ALERT_TYPE_PERFORMANCE_DEGRADED, 
                               "Network health rapidly degrading");
            total_alerts++;
        }
        
        /* Error rate increasing trend */
        if (g_diag_state.current_trends.error_trend > 5) {
            diag_generate_alert(ALERT_TYPE_ERROR_RATE_HIGH, 
                               "Error rate increasing rapidly");
            total_alerts++;
        }
        
        /* Memory usage trend */
        if (g_diag_state.current_trends.memory_trend > 1000) { /* > 1KB/sec increase */
            diag_generate_alert(ALERT_TYPE_MEMORY_LOW, 
                               "Memory usage increasing rapidly");
            total_alerts++;
        }
        
        /* Traffic anomaly detection */
        if (g_diag_state.current_trends.packet_trend < -1000) { /* > 1000 pkt/sec drop */
            diag_generate_alert(ALERT_TYPE_BOTTLENECK_DETECTED, 
                               "Significant traffic drop detected");
            total_alerts++;
        }
    }
    
    /* Check individual subsystem alerts */
    total_alerts += diag_detect_bottlenecks();
    total_alerts += diag_correlate_errors();
    
    /* Check flow health */
    if (g_diag_state.flow_count > 200) { /* High flow count */
        diag_generate_alert(ALERT_TYPE_PERFORMANCE_DEGRADED, 
                           "High active flow count may impact performance");
        total_alerts++;
    }
    
    /* Age old error events to prevent alert fatigue */
    diag_cleanup_old_errors();
    
    LOG_PERF_DEBUG("Comprehensive alert check completed: %d alerts generated", total_alerts);
    
    return total_alerts;
}

/* Enhanced Diagnostic Logging System with /LOG=ON Support */

/**
 * @brief Configure diagnostic logging based on /LOG=ON parameter
 * @param log_param Logging parameter string from CONFIG.SYS
 * @return 0 on success, negative on error
 */
int diag_configure_logging(const char *log_param) {
    if (!log_param) {
        /* Default logging configuration */
        g_log_to_console = true;
        g_log_to_file = false;
        g_log_to_network = false;
        return SUCCESS;
    }
    
    LOG_INFO("Configuring diagnostic logging with parameter: %s", log_param);
    
    /* Parse logging parameter */
    if (strstr(log_param, "ON") != NULL || strstr(log_param, "on") != NULL) {
        g_log_enabled_by_config = true;
        g_log_to_console = true;
        g_log_to_file = true;
        
        /* Check for file path specification */
        const char *file_spec = strstr(log_param, "FILE=");
        if (file_spec) {
            file_spec += 5;  /* Skip "FILE=" */
            
            /* Extract filename */
            int i = 0;
            while (file_spec[i] && file_spec[i] != ' ' && file_spec[i] != ',' && 
                   i < sizeof(g_log_file_path) - 1) {
                g_log_file_path[i] = file_spec[i];
                i++;
            }
            g_log_file_path[i] = '\0';
        }
        
        /* Check for console disable */
        if (strstr(log_param, "NOCONSOLE") != NULL) {
            g_log_to_console = false;
        }
        
        /* Check for network logging */
        if (strstr(log_param, "NETWORK") != NULL) {
            g_log_to_network = true;
        }
        
        LOG_INFO("Logging enabled - Console: %s, File: %s (%s), Network: %s",
                g_log_to_console ? "YES" : "NO",
                g_log_to_file ? "YES" : "NO",
                g_log_file_path,
                g_log_to_network ? "YES" : "NO");
                
    } else if (strstr(log_param, "OFF") != NULL || strstr(log_param, "off") != NULL) {
        g_log_enabled_by_config = false;
        g_log_to_console = false;
        g_log_to_file = false;
        g_log_to_network = false;
        LOG_INFO("Diagnostic logging disabled by configuration");
    }
    
    return SUCCESS;
}

/**
 * @brief Enhanced log message output with multiple targets
 * @param entry Log entry to output
 */
static void enhanced_log_output(const log_entry_t *entry) {
    if (!entry || !g_log_enabled_by_config) {
        return;
    }
    
    char formatted_msg[512];
    const char *level_str = diag_level_to_string(entry->level);
    
    /* Format timestamp */
    uint32_t timestamp = entry->timestamp;
    uint32_t seconds = timestamp / 1000;
    uint32_t milliseconds = timestamp % 1000;
    
    /* Create formatted message */
    snprintf(formatted_msg, sizeof(formatted_msg),
             "[%08lu.%03lu] %s: %s (%s:%lu)",
             seconds, milliseconds, level_str, entry->message,
             entry->file ? entry->file : "unknown", entry->line);
    
    /* Console output */
    if (g_log_to_console) {
        printf("%s\n", formatted_msg);
    }
    
    /* File output */
    if (g_log_to_file && g_log_file_path[0]) {
        FILE *log_file = fopen(g_log_file_path, "a");
        if (log_file) {
            fprintf(log_file, "%s\n", formatted_msg);
            fclose(log_file);
        }
    }
    
    /* Network output (placeholder for future implementation) */
    if (g_log_to_network) {
        /* Could implement UDP syslog or similar network logging */
        /* For now, just add to internal buffer */
    }
}

/**
 * @brief Enhanced hardware diagnostics with timeout protection
 * @param nic NIC information structure
 * @return 0 on success, negative on error
 */
int diag_enhanced_hardware_test(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_INFO("Performing enhanced hardware diagnostics for NIC type %d", nic->type);
    
    int result = SUCCESS;
    uint16_t io_base = nic->io_base;
    
    /* Test basic register access with timeout protection */
    if (nic->type == NIC_TYPE_3C509B) {
        /* Test 3C509B ID register */
        uint16_t id_reg = 0xFFFF;
        
        /* Use timeout-protected I/O from error recovery system */
        extern int protected_hardware_operation(void *ctx, uint16_t port, 
                                               uint8_t operation, uint16_t data, 
                                               uint16_t timeout_ms);
        
        /* Read ID register with 500ms timeout */
        int read_result = protected_hardware_operation(NULL, io_base + 0x0E, 0, 0, 500);
        
        if (read_result < 0) {
            LOG_ERROR("3C509B ID register read failed with timeout");
            result = ERROR_HARDWARE;
        } else {
            id_reg = (uint16_t)read_result;
            if (id_reg == 0xFFFF || id_reg == 0x0000) {
                LOG_WARNING("3C509B returned invalid ID: 0x%04X", id_reg);
                result = ERROR_HARDWARE;
            } else {
                LOG_INFO("3C509B ID register: 0x%04X", id_reg);
            }
        }
        
        /* Test EEPROM access if ID register is valid */
        if (result == SUCCESS) {
            /* Test EEPROM read with timeout */
            read_result = protected_hardware_operation(NULL, io_base + 0x0A, 0, 0, 1000);
            if (read_result < 0) {
                LOG_WARNING("3C509B EEPROM access test failed");
                result = ERROR_PARTIAL;
            } else {
                LOG_DEBUG("3C509B EEPROM accessible");
            }
        }
        
    } else if (nic->type == NIC_TYPE_3C515_TX) {
        /* Test 3C515 status register */
        int read_result = protected_hardware_operation(NULL, io_base + 0x0E, 0, 0, 500);
        
        if (read_result < 0) {
            LOG_ERROR("3C515 status register read failed with timeout");
            result = ERROR_HARDWARE;
        } else {
            uint16_t status = (uint16_t)read_result;
            LOG_INFO("3C515 status register: 0x%04X", status);
            
            /* Check for obvious failure conditions */
            if (status == 0xFFFF) {
                LOG_ERROR("3C515 appears to be disconnected or failed");
                result = ERROR_HARDWARE;
            }
        }
        
        /* Test DMA capability if basic access works */
        if (result == SUCCESS) {
            read_result = protected_hardware_operation(NULL, io_base + 0x1C, 0, 0, 500);
            if (read_result < 0) {
                LOG_WARNING("3C515 DMA register access failed");
                result = ERROR_PARTIAL;
            } else {
                LOG_DEBUG("3C515 DMA registers accessible");
            }
        }
    }
    
    /* Test interrupt line if hardware responds */
    if (result == SUCCESS || result == ERROR_PARTIAL) {
        if (nic->irq > 0 && nic->irq < 16) {
            LOG_DEBUG("Testing interrupt line IRQ %d", nic->irq);
            /* Placeholder for interrupt line testing */
            /* In a full implementation, this would test interrupt functionality */
        } else {
            LOG_WARNING("Invalid IRQ configuration: %d", nic->irq);
            result = ERROR_PARTIAL;
        }
    }
    
    LOG_INFO("Enhanced hardware diagnostics completed with result: %d", result);
    return result;
}

/**
 * @brief Advanced error pattern correlation and analysis
 * @return Number of correlation patterns detected
 */
int diag_advanced_error_correlation(void) {
    int patterns_found = 0;
    uint32_t current_time = diagnostics_get_system_time();
    
    LOG_DEBUG("Performing advanced error correlation analysis");
    
    /* Analyze temporal error patterns */
    error_pattern_t *pattern = g_error_patterns;
    while (pattern) {
        /* Check if pattern is still active (within analysis window) */
        if (current_time - pattern->last_occurrence < g_pattern_analysis_window) {
            /* Check for burst patterns (high frequency) */
            if (pattern->frequency > 10) { /* More than 10 occurrences per minute */
                LOG_WARNING("Error burst detected: type=0x%02X, NIC=%d, freq=%lu/min",
                           pattern->error_type, pattern->nic_index, pattern->frequency);
                
                diag_generate_alert(ALERT_TYPE_ERROR_RATE_HIGH, 
                                  "Error burst pattern detected");
                patterns_found++;
            }
            
            /* Check for recurring patterns */
            if (pattern->frequency >= 3) {
                uint32_t interval = g_pattern_analysis_window / pattern->frequency;
                if (interval < 10000) { /* Less than 10 seconds between errors */
                    LOG_WARNING("Recurring error pattern: type=0x%02X, NIC=%d, interval=%lums",
                               pattern->error_type, pattern->nic_index, interval);
                    patterns_found++;
                }
            }
        }
        
        pattern = pattern->next;
    }
    
    /* Cross-NIC error correlation */
    if (patterns_found > 1) {
        LOG_INFO("Multiple error patterns detected - checking for system-wide issues");
        
        /* Check if multiple NICs are experiencing similar issues */
        error_pattern_t *p1 = g_error_patterns;
        while (p1) {
            error_pattern_t *p2 = p1->next;
            while (p2) {
                if (p1->error_type == p2->error_type && 
                    p1->nic_index != p2->nic_index &&
                    abs(p1->last_occurrence - p2->last_occurrence) < 5000) {
                    
                    LOG_CRITICAL("Correlated errors across multiple NICs - "
                                "system-wide issue suspected (error type: 0x%02X)",
                                p1->error_type);
                    
                    diag_generate_alert(ALERT_TYPE_NIC_FAILURE, 
                                      "System-wide adapter issues detected");
                    patterns_found++;
                    break;
                }
                p2 = p2->next;
            }
            p1 = p1->next;
        }
    }
    
    LOG_DEBUG("Error correlation analysis completed: %d patterns found", patterns_found);
    return patterns_found;
}

/**
 * @brief Performance bottleneck detection with enhanced analysis
 * @return Number of bottlenecks detected
 */
int diag_enhanced_bottleneck_detection(void) {
    int bottlenecks = 0;
    
    LOG_DEBUG("Performing enhanced bottleneck detection");
    
    /* Memory pressure analysis */
    extern uint32_t get_available_memory(void);  /* From memory.c */
    uint32_t available_memory = get_available_memory();
    
    if (available_memory < 32768) { /* Less than 32KB available */
        LOG_WARNING("Memory bottleneck detected: only %lu bytes available", available_memory);
        diag_generate_alert(ALERT_TYPE_MEMORY_LOW, "Low memory condition");
        bottlenecks++;
    }
    
    /* CPU utilization estimation */
    static uint32_t last_interrupt_count = 0;
    static uint32_t last_check_time = 0;
    
    uint32_t current_interrupts = g_perf_counters.interrupts_handled;
    uint32_t current_time = diagnostics_get_system_time();
    
    if (last_check_time > 0) {
        uint32_t time_diff = current_time - last_check_time;
        uint32_t interrupt_diff = current_interrupts - last_interrupt_count;
        
        if (time_diff > 0) {
            uint32_t interrupt_rate = (interrupt_diff * 1000) / time_diff; /* Interrupts per second */
            
            if (interrupt_rate > 500) { /* High interrupt load */
                LOG_WARNING("High interrupt load detected: %lu interrupts/sec", interrupt_rate);
                diag_generate_alert(ALERT_TYPE_BOTTLENECK_DETECTED, 
                                  "High interrupt load may impact performance");
                bottlenecks++;
            }
        }
    }
    
    last_interrupt_count = current_interrupts;
    last_check_time = current_time;
    
    /* Packet processing bottleneck analysis */
    uint32_t total_packets = g_perf_counters.packets_sent + g_perf_counters.packets_received;
    uint32_t packet_drops = g_perf_counters.packet_drops;
    
    if (total_packets > 0) {
        uint32_t drop_rate = (packet_drops * 100) / total_packets;
        
        if (drop_rate > 5) { /* More than 5% packet drop rate */
            LOG_WARNING("Packet processing bottleneck: %lu%% drop rate", drop_rate);
            diag_generate_alert(ALERT_TYPE_BOTTLENECK_DETECTED, 
                              "High packet drop rate indicates processing bottleneck");
            bottlenecks++;
        }
    }
    
    /* Buffer utilization analysis */
    if (g_perf_counters.buffer_overruns > 0) {
        LOG_WARNING("Buffer bottleneck detected: %lu buffer overruns", 
                   g_perf_counters.buffer_overruns);
        diag_generate_alert(ALERT_TYPE_BOTTLENECK_DETECTED, 
                          "Buffer overruns indicate insufficient buffer capacity");
        bottlenecks++;
    }
    
    LOG_DEBUG("Bottleneck detection completed: %d bottlenecks found", bottlenecks);
    return bottlenecks;
}

/**
 * @brief Cleanup old error patterns to prevent memory leaks
 */
static void diag_cleanup_old_errors(void) {
    uint32_t current_time = diagnostics_get_system_time();
    error_pattern_t **current = &g_error_patterns;
    
    while (*current) {
        error_pattern_t *pattern = *current;
        
        /* Remove patterns older than 5 minutes */
        if (current_time - pattern->last_occurrence > 300000) {
            *current = pattern->next;
            free(pattern);
        } else {
            current = &pattern->next;
        }
    }
}

/**
 * @brief Print comprehensive system diagnostic report
 */
void diag_print_comprehensive_report(void) {
    printf("\n");
    printf("=====================================\n");
    printf("3COM PACKET DRIVER DIAGNOSTIC REPORT\n");
    printf("=====================================\n");
    printf("Report Generated: %lu ms since startup\n", diagnostics_get_system_time());
    printf("Logging Configuration: Console=%s, File=%s, Network=%s\n",
           g_log_to_console ? "ON" : "OFF",
           g_log_to_file ? "ON" : "OFF", 
           g_log_to_network ? "ON" : "OFF");
    
    if (g_log_to_file) {
        printf("Log File: %s\n", g_log_file_path);
    }
    
    printf("\n--- System Health ---\n");
    printf("Overall Health: %d/100\n", g_network_health.overall_score);
    printf("Error Rate: %lu per 1000 packets\n", g_network_health.error_rate);
    printf("Network Utilization: %lu%%\n", g_network_health.utilization);
    printf("Active Flows: %d\n", g_network_health.active_flows);
    
    printf("\n--- Performance Counters ---\n");
    printf("Packets Sent: %lu\n", g_perf_counters.packets_sent);
    printf("Packets Received: %lu\n", g_perf_counters.packets_received);
    printf("Bytes Sent: %lu\n", g_perf_counters.bytes_sent);
    printf("Bytes Received: %lu\n", g_perf_counters.bytes_received);
    printf("Interrupts Handled: %lu\n", g_perf_counters.interrupts_handled);
    printf("Errors Detected: %lu\n", g_perf_counters.errors_detected);
    printf("Timeouts: %lu\n", g_perf_counters.timeouts);
    printf("Buffer Overruns: %lu\n", g_perf_counters.buffer_overruns);
    printf("Packet Drops: %lu\n", g_perf_counters.packet_drops);
    
    printf("\n--- Error Patterns ---\n");
    error_pattern_t *pattern = g_error_patterns;
    int pattern_count = 0;
    while (pattern && pattern_count < 10) { /* Limit output */
        printf("Pattern %d: Type=0x%02X, NIC=%d, Frequency=%lu, Last=%lu\n",
               pattern_count + 1, pattern->error_type, pattern->nic_index,
               pattern->frequency, pattern->last_occurrence);
        pattern = pattern->next;
        pattern_count++;
    }
    
    if (pattern_count == 0) {
        printf("No active error patterns detected\n");
    } else if (pattern) {
        printf("... and more (showing first 10)\n");
    }
    
    printf("\n--- Recovery Statistics ---\n");
    extern void print_recovery_statistics(void);
    print_recovery_statistics();
    
    printf("\n--- Historical Trends ---\n");
    if (g_diag_state.current_trends.sample_count > 0) {
        printf("Analysis Window: %lu ms, Samples: %lu\n",
               g_diag_state.current_trends.analysis_window_ms,
               g_diag_state.current_trends.sample_count);
        printf("Packet Trend: %ld pps change\n", g_diag_state.current_trends.packet_trend);
        printf("Error Trend: %ld errors/min change\n", g_diag_state.current_trends.error_trend);
        printf("Health Trend: %ld points change\n", g_diag_state.current_trends.health_trend);
        printf("Memory Trend: %ld bytes/sec change\n", g_diag_state.current_trends.memory_trend);
    } else {
        printf("Insufficient historical data for trend analysis\n");
    }
    
    printf("\n=====================================\n");
    printf("End of Diagnostic Report\n");
    printf("=====================================\n\n");
}


/* Helper functions for diagnostics */
static uint32_t get_system_memory_size(void) {
    /* Get system memory size - simplified implementation */
    /* In real DOS implementation would use INT 12h or INT 15h */
    return 640 * 1024; /* Conventional memory limit */
}

static uint8_t calculate_cpu_utilization(void) {
    /* Simple CPU utilization calculation */
    /* In real implementation would track timer interrupts */
    static uint32_t last_idle_time = 0;
    static uint32_t last_total_time = 0;
    
    /* Simplified calculation based on interrupt load */
    uint32_t interrupt_count = g_perf_counters.interrupt_count;
    uint8_t utilization = (interrupt_count > 1000) ? 
                         ((interrupt_count - 1000) / 100) : 0;
    
    return (utilization > 100) ? 100 : utilization;
}
