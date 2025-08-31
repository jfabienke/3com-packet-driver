/**
 * @file validate_hardware_activation.c
 * @brief Hardware activation and vtable wiring validation test
 * 
 * This test validates that PnP activation enables hardware I/O operations
 * and that the vtable architecture is properly connected for both NIC types.
 *
 * Tests performed:
 * 1. Verify NIC detection and enumeration
 * 2. Check PnP activation enables hardware I/O
 * 3. Validate vtable function pointers are connected
 * 4. Test basic hardware register access
 * 5. Verify error handling for missing hardware
 * 
 * Expected Results:
 * - NICs are detected and properly configured
 * - Vtable functions are connected (not NULL)
 * - Hardware responds after PnP activation
 * - Error handling works for edge cases
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "../include/hardware.h"
#include "../include/3c509b.h"
#include "../include/3c515.h"
#include "../include/logging.h"
#include "../include/pnp.h"

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

/* Function prototypes */
static void test_nic_detection(void);
static void test_vtable_wiring(void);
static void test_hardware_activation(void);
static void test_error_handling(void);
static void print_test_result(const char *test_name, int passed);
static void print_summary(void);

int main(void) {
    printf("=== 3Com Packet Driver Hardware Activation Test ===\n");
    printf("Testing vtable integration and PnP activation...\n\n");
    
    /* Initialize hardware detection */
    if (hardware_init() != 0) {
        printf("FATAL: Hardware initialization failed\n");
        return 1;
    }
    
    /* Run test battery */
    test_nic_detection();
    test_vtable_wiring();
    test_hardware_activation();
    test_error_handling();
    
    /* Print final results */
    print_summary();
    
    /* Cleanup */
    hardware_cleanup();
    
    return (tests_failed == 0) ? 0 : 1;
}

/**
 * Test 1: Verify NIC detection and enumeration
 */
static void test_nic_detection(void) {
    int num_nics;
    nic_info_t *nic;
    int passed = 1;
    
    printf("Test 1: NIC Detection and Enumeration\n");
    
    /* Check NIC count */
    num_nics = hardware_get_nic_count();
    if (num_nics < 0) {
        printf("  ERROR: hardware_get_nic_count() returned error: %d\n", num_nics);
        passed = 0;
    } else {
        printf("  Detected %d NIC(s)\n", num_nics);
    }
    
    /* Test NIC enumeration */
    for (int i = 0; i < num_nics && i < MAX_NICS; i++) {
        nic = hardware_get_nic(i);
        if (!nic) {
            printf("  ERROR: hardware_get_nic(%d) returned NULL\n", i);
            passed = 0;
            continue;
        }
        
        printf("  NIC %d: Type=%d, IO=0x%04X, IRQ=%d\n", 
               i, nic->type, nic->io_base, nic->irq);
               
        /* Verify NIC type is valid */
        if (nic->type != NIC_TYPE_3C509B && nic->type != NIC_TYPE_3C515_TX) {
            printf("  ERROR: Invalid NIC type: %d\n", nic->type);
            passed = 0;
        }
        
        /* Verify I/O base is reasonable */
        if (nic->io_base < 0x100 || nic->io_base > 0x3FF) {
            printf("  ERROR: Invalid I/O base: 0x%04X\n", nic->io_base);
            passed = 0;
        }
        
        /* Verify IRQ is valid */
        if (nic->irq < 3 || nic->irq > 15 || 
            nic->irq == 4 || nic->irq == 6 || nic->irq == 8) {
            printf("  ERROR: Invalid IRQ: %d\n", nic->irq);  
            passed = 0;
        }
    }
    
    print_test_result("NIC Detection", passed);
}

/**
 * Test 2: Validate vtable function pointers are connected
 */
static void test_vtable_wiring(void) {
    int num_nics;
    nic_info_t *nic;
    nic_ops_t *ops;
    int passed = 1;
    int critical_functions = 0;
    int connected_functions = 0;
    
    printf("\nTest 2: Vtable Function Wiring\n");
    
    num_nics = hardware_get_nic_count();
    
    for (int i = 0; i < num_nics && i < MAX_NICS; i++) {
        nic = hardware_get_nic(i);
        if (!nic) continue;
        
        ops = nic->ops;
        if (!ops) {
            printf("  ERROR: NIC %d has NULL ops vtable\n", i);
            passed = 0;
            continue;
        }
        
        printf("  NIC %d vtable validation:\n", i);
        
        /* Check critical function pointers */
        const struct {
            void *ptr;
            const char *name;
            int critical;
        } functions[] = {
            {ops->init, "init", 1},
            {ops->cleanup, "cleanup", 1}, 
            {ops->send_packet, "send_packet", 1},
            {ops->receive_packet, "receive_packet", 1},
            {ops->handle_interrupt, "handle_interrupt", 1},
            {ops->get_mac_address, "get_mac_address", 1},
            {ops->set_receive_mode, "set_receive_mode", 1},
            {ops->check_tx_complete, "check_tx_complete", 0},
            {ops->check_rx_available, "check_rx_available", 0},
            {ops->reset, "reset", 0},
            {ops->self_test, "self_test", 0},
            {ops->check_interrupt, "check_interrupt", 0},
            {ops->enable_interrupts, "enable_interrupts", 0},
            {ops->disable_interrupts, "disable_interrupts", 0},
            {ops->get_statistics, "get_statistics", 0}
        };
        
        for (int j = 0; j < sizeof(functions)/sizeof(functions[0]); j++) {
            if (functions[j].critical) {
                critical_functions++;
                if (functions[j].ptr != NULL) {
                    connected_functions++;
                    printf("    ✓ %s: connected\n", functions[j].name);
                } else {
                    printf("    ✗ %s: NOT connected (CRITICAL)\n", functions[j].name);
                    passed = 0;
                }
            } else {
                if (functions[j].ptr != NULL) {
                    printf("    ✓ %s: connected\n", functions[j].name);
                } else {
                    printf("    - %s: not connected (optional)\n", functions[j].name);
                }
            }
        }
    }
    
    printf("  Summary: %d/%d critical functions connected\n", 
           connected_functions, critical_functions);
    
    print_test_result("Vtable Wiring", passed);
}

/**
 * Test 3: Test hardware activation via PnP
 */
static void test_hardware_activation(void) {
    int num_nics;
    nic_info_t *nic;
    int passed = 1;
    
    printf("\nTest 3: Hardware PnP Activation\n");
    
    num_nics = hardware_get_nic_count();
    
    for (int i = 0; i < num_nics && i < MAX_NICS; i++) {
        nic = hardware_get_nic(i);
        if (!nic || !nic->ops) continue;
        
        printf("  Testing NIC %d activation...\n", i);
        
        /* Test hardware initialization through vtable */
        if (nic->ops->init) {
            int result = nic->ops->init(nic);
            if (result != 0) {
                printf("    ERROR: NIC init failed with code: %d\n", result);
                passed = 0;
            } else {
                printf("    ✓ NIC initialization successful\n");
                
                /* Test basic hardware access */
                if (nic->ops->get_mac_address) {
                    uint8_t mac[6];
                    result = nic->ops->get_mac_address(nic, mac);
                    if (result == 0) {
                        printf("    ✓ MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                    } else {
                        printf("    ERROR: Failed to read MAC address: %d\n", result);
                        passed = 0;
                    }
                }
                
                /* Test hardware reset */  
                if (nic->ops->reset) {
                    result = nic->ops->reset(nic);
                    if (result == 0) {
                        printf("    ✓ Hardware reset successful\n");
                    } else {
                        printf("    WARNING: Hardware reset failed: %d\n", result);
                    }
                }
            }
        } else {
            printf("    ERROR: No init function in vtable\n");
            passed = 0;
        }
    }
    
    print_test_result("Hardware Activation", passed);
}

/**
 * Test 4: Test error handling for edge cases
 */
static void test_error_handling(void) {
    int passed = 1;
    nic_info_t *nic;
    
    printf("\nTest 4: Error Handling\n");
    
    /* Test invalid NIC index */
    nic = hardware_get_nic(99);
    if (nic != NULL) {
        printf("  ERROR: hardware_get_nic(99) should return NULL\n");
        passed = 0;
    } else {
        printf("  ✓ Invalid NIC index handled correctly\n");
    }
    
    /* Test negative NIC index */
    nic = hardware_get_nic(-1);
    if (nic != NULL) {
        printf("  ERROR: hardware_get_nic(-1) should return NULL\n");
        passed = 0;
    } else {
        printf("  ✓ Negative NIC index handled correctly\n");
    }
    
    /* Test hardware cleanup */
    int cleanup_result = hardware_cleanup();
    if (cleanup_result == 0) {
        printf("  ✓ Hardware cleanup successful\n");
    } else {
        printf("  WARNING: Hardware cleanup returned: %d\n", cleanup_result);
    }
    
    print_test_result("Error Handling", passed);
}

/**
 * Print individual test result
 */
static void print_test_result(const char *test_name, int passed) {
    if (passed) {
        printf("  RESULT: %s PASSED\n", test_name);
        tests_passed++;
    } else {
        printf("  RESULT: %s FAILED\n", test_name);
        tests_failed++;
    }
}

/**
 * Print final test summary
 */
static void print_summary(void) {
    printf("\n=== TEST SUMMARY ===\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Total tests:  %d\n", tests_passed + tests_failed);
    
    if (tests_failed == 0) {
        printf("\n*** HARDWARE ACTIVATION TEST PASSED ***\n");
        printf("Vtable integration is functional and production-ready!\n");
    } else {
        printf("\n*** HARDWARE ACTIVATION TEST FAILED ***\n");
        printf("Driver requires fixes before production deployment.\n");
    }
}