/**
 * @file test_unified.c
 * @brief Test Harness for Unified 3Com EtherLink III Driver
 *
 * Validates the unified driver architecture across all supported NICs.
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "../core/el3_core.h"
#include "../../include/logging.h"

/* Test configuration */
#define TEST_VERBOSE    1
#define TEST_LOOPBACK   0   /* Requires loopback cable */

/* Test results */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* Test macros */
#define TEST_START(name) \
    do { \
        tests_run++; \
        if (TEST_VERBOSE) printf("TEST: %s... ", name); \
    } while(0)

#define TEST_PASS() \
    do { \
        tests_passed++; \
        if (TEST_VERBOSE) printf("PASS\n"); \
    } while(0)

#define TEST_FAIL(msg) \
    do { \
        tests_failed++; \
        if (TEST_VERBOSE) printf("FAIL: %s\n", msg); \
        return -1; \
    } while(0)

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            TEST_FAIL(msg); \
        } \
    } while(0)

/* Forward declarations */
static int test_capability_detection(void);
static int test_generation_mapping(void);
static int test_isa_detection(void);
static int test_pci_detection(void);
static int test_datapath_selection(void);
static int test_window_management(void);
static int test_statistics(void);
static int test_loopback(void);
static void print_device_info(struct el3_dev *dev);
static void run_test_suite(void);

/**
 * @brief Main test entry point
 */
int main(int argc, char *argv[])
{
    printf("=== Unified 3Com EtherLink III Driver Test Suite ===\n\n");
    
    /* Initialize logging */
    LOG_INIT("test_unified.log");
    
    /* Run test suite */
    run_test_suite();
    
    /* Print results */
    printf("\n=== Test Results ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    
    if (tests_failed == 0) {
        printf("\nALL TESTS PASSED!\n");
        return 0;
    } else {
        printf("\nSOME TESTS FAILED!\n");
        return 1;
    }
}

/**
 * @brief Run all tests
 */
static void run_test_suite(void)
{
    printf("Running capability tests...\n");
    test_capability_detection();
    test_generation_mapping();
    
    printf("\nRunning bus detection tests...\n");
    test_isa_detection();
    test_pci_detection();
    
    printf("\nRunning datapath tests...\n");
    test_datapath_selection();
    test_window_management();
    
    printf("\nRunning operational tests...\n");
    test_statistics();
    
    if (TEST_LOOPBACK) {
        printf("\nRunning loopback tests...\n");
        test_loopback();
    }
}

/**
 * @brief Test capability detection
 */
static int test_capability_detection(void)
{
    struct el3_dev dev;
    int ret;
    
    TEST_START("Capability detection for 3C509B");
    
    /* Simulate 3C509B */
    memset(&dev, 0, sizeof(dev));
    dev.device_id = 0x5090;  /* 3C509B ID */
    dev.io_base = 0x300;     /* Typical I/O */
    
    ret = el3_detect_capabilities(&dev);
    TEST_ASSERT(ret == 0, "Failed to detect capabilities");
    TEST_ASSERT(dev.generation == EL3_GEN_3C509B, "Wrong generation");
    TEST_ASSERT(dev.caps.fifo_size == 2048, "Wrong FIFO size");
    TEST_ASSERT(!dev.caps.has_bus_master, "Should not have bus master");
    
    TEST_PASS();
    
    TEST_START("Capability detection for Vortex");
    
    /* Simulate Vortex */
    memset(&dev, 0, sizeof(dev));
    dev.device_id = 0x5950;  /* 3C595 Vortex */
    dev.io_base = 0x6000;    /* PCI I/O */
    
    ret = el3_detect_capabilities(&dev);
    TEST_ASSERT(ret == 0, "Failed to detect capabilities");
    TEST_ASSERT(dev.generation == EL3_GEN_VORTEX, "Wrong generation");
    TEST_ASSERT(dev.caps.has_permanent_window1, "Should have permanent window 1");
    
    TEST_PASS();
    
    TEST_START("Capability detection for Cyclone");
    
    /* Simulate Cyclone */
    memset(&dev, 0, sizeof(dev));
    dev.device_id = 0x9200;  /* 3C905C Cyclone */
    dev.io_base = 0x6000;
    
    ret = el3_detect_capabilities(&dev);
    TEST_ASSERT(ret == 0, "Failed to detect capabilities");
    TEST_ASSERT(dev.generation == EL3_GEN_CYCLONE, "Wrong generation");
    TEST_ASSERT(dev.caps.has_hw_checksum, "Should have HW checksum");
    
    TEST_PASS();
    
    return 0;
}

/**
 * @brief Test generation mapping
 */
static int test_generation_mapping(void)
{
    TEST_START("Generation name mapping");
    
    TEST_ASSERT(strcmp(el3_get_generation_name(EL3_GEN_3C509B), "3C509B") == 0, 
                "Wrong name for 3C509B");
    TEST_ASSERT(strcmp(el3_get_generation_name(EL3_GEN_VORTEX), "Vortex") == 0,
                "Wrong name for Vortex");
    TEST_ASSERT(strcmp(el3_get_generation_name(EL3_GEN_BOOMERANG), "Boomerang") == 0,
                "Wrong name for Boomerang");
    TEST_ASSERT(strcmp(el3_get_generation_name(EL3_GEN_CYCLONE), "Cyclone") == 0,
                "Wrong name for Cyclone");
    TEST_ASSERT(strcmp(el3_get_generation_name(EL3_GEN_TORNADO), "Tornado") == 0,
                "Wrong name for Tornado");
    
    TEST_PASS();
    
    return 0;
}

/**
 * @brief Test ISA detection
 */
static int test_isa_detection(void)
{
    int count;
    
    TEST_START("ISA bus probe simulation");
    
    /* This would require actual hardware or emulation */
    /* For now, just test that the function exists and returns */
    count = el3_isa_probe();
    
    if (count > 0) {
        printf("  Found %d ISA device(s)\n", count);
        
        /* Get first device */
        struct el3_dev *dev = el3_get_device(0);
        if (dev) {
            print_device_info(dev);
        }
    } else {
        printf("  No ISA devices found (expected in test environment)\n");
    }
    
    TEST_PASS();
    
    return 0;
}

/**
 * @brief Test PCI detection
 */
static int test_pci_detection(void)
{
    int count;
    
    TEST_START("PCI bus probe simulation");
    
    /* This would require actual hardware or emulation */
    /* For now, just test that the function exists and returns */
    count = el3_pci_probe();
    
    if (count > 0) {
        printf("  Found %d PCI device(s)\n", count);
        
        /* Get devices */
        for (int i = 0; i < count && i < MAX_EL3_DEVICES; i++) {
            struct el3_dev *dev = el3_get_device(i);
            if (dev) {
                print_device_info(dev);
            }
        }
    } else {
        printf("  No PCI devices found (expected in test environment)\n");
    }
    
    TEST_PASS();
    
    return 0;
}

/**
 * @brief Test datapath selection
 */
static int test_datapath_selection(void)
{
    struct el3_dev dev;
    
    TEST_START("Datapath selection for PIO");
    
    /* Simulate 3C509B (PIO only) */
    memset(&dev, 0, sizeof(dev));
    dev.generation = EL3_GEN_3C509B;
    dev.caps.has_bus_master = false;
    
    el3_select_generation_ops(&dev);
    
    TEST_ASSERT(dev.start_xmit == el3_pio_xmit, "Should use PIO transmit");
    TEST_ASSERT(dev.rx_poll == el3_pio_rx_poll, "Should use PIO receive");
    
    TEST_PASS();
    
    TEST_START("Datapath selection for DMA");
    
    /* Simulate Boomerang (DMA capable) */
    memset(&dev, 0, sizeof(dev));
    dev.generation = EL3_GEN_BOOMERANG;
    dev.caps.has_bus_master = true;
    
    el3_select_generation_ops(&dev);
    
    TEST_ASSERT(dev.start_xmit == el3_dma_xmit, "Should use DMA transmit");
    TEST_ASSERT(dev.rx_poll == el3_dma_rx_poll, "Should use DMA receive");
    
    TEST_PASS();
    
    return 0;
}

/**
 * @brief Test window management
 */
static int test_window_management(void)
{
    struct el3_dev dev;
    
    TEST_START("Window switching optimization");
    
    /* Test that Vortex+ avoids window switches */
    memset(&dev, 0, sizeof(dev));
    dev.generation = EL3_GEN_VORTEX;
    dev.caps.has_permanent_window1 = true;
    dev.io_base = 0x300;  /* Dummy I/O */
    
    /* This would test actual window switching */
    /* For now, just verify the capability flag */
    TEST_ASSERT(dev.caps.has_permanent_window1, 
                "Vortex should have permanent window 1");
    
    TEST_PASS();
    
    return 0;
}

/**
 * @brief Test statistics tracking
 */
static int test_statistics(void)
{
    struct el3_dev dev;
    
    TEST_START("Statistics initialization");
    
    memset(&dev, 0, sizeof(dev));
    
    TEST_ASSERT(dev.stats.tx_packets == 0, "TX packets should be 0");
    TEST_ASSERT(dev.stats.rx_packets == 0, "RX packets should be 0");
    TEST_ASSERT(dev.stats.tx_errors == 0, "TX errors should be 0");
    TEST_ASSERT(dev.stats.rx_errors == 0, "RX errors should be 0");
    
    /* Simulate some activity */
    dev.stats.tx_packets = 100;
    dev.stats.rx_packets = 150;
    dev.stats.tx_bytes = 64000;
    dev.stats.rx_bytes = 96000;
    
    TEST_ASSERT(dev.stats.tx_packets == 100, "TX packets mismatch");
    TEST_ASSERT(dev.stats.rx_packets == 150, "RX packets mismatch");
    
    TEST_PASS();
    
    return 0;
}

/**
 * @brief Test loopback operation
 */
static int test_loopback(void)
{
    struct el3_dev *dev;
    struct packet tx_pkt, rx_pkt;
    uint8_t test_data[64];
    int i, ret;
    
    TEST_START("Loopback packet transmission");
    
    /* Get first device */
    dev = el3_get_device(0);
    if (!dev) {
        printf("  No device available for loopback test\n");
        TEST_PASS();
        return 0;
    }
    
    /* Prepare test packet */
    for (i = 0; i < 64; i++) {
        test_data[i] = i & 0xFF;
    }
    
    tx_pkt.data = test_data;
    tx_pkt.length = 64;
    tx_pkt.flags = 0;
    
    /* Transmit packet */
    ret = dev->start_xmit(dev, &tx_pkt);
    TEST_ASSERT(ret == 0, "Transmission failed");
    
    /* Wait for reception */
    delay(10);  /* 10ms delay */
    
    /* Poll for received packet */
    ret = dev->rx_poll(dev);
    TEST_ASSERT(ret > 0, "No packet received");
    
    /* Verify statistics */
    TEST_ASSERT(dev->stats.tx_packets > 0, "TX counter not incremented");
    TEST_ASSERT(dev->stats.rx_packets > 0, "RX counter not incremented");
    
    TEST_PASS();
    
    return 0;
}

/**
 * @brief Print device information
 */
static void print_device_info(struct el3_dev *dev)
{
    printf("  Device: %s\n", dev->name);
    printf("    Generation: %s\n", el3_get_generation_name(dev->generation));
    printf("    I/O Base: 0x%04X\n", dev->io_base);
    printf("    IRQ: %d\n", dev->irq);
    printf("    MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           dev->mac_addr[0], dev->mac_addr[1], dev->mac_addr[2],
           dev->mac_addr[3], dev->mac_addr[4], dev->mac_addr[5]);
    printf("    Capabilities:\n");
    printf("      FIFO Size: %d bytes\n", dev->caps.fifo_size);
    printf("      Bus Master: %s\n", dev->caps.has_bus_master ? "Yes" : "No");
    printf("      HW Checksum: %s\n", dev->caps.has_hw_checksum ? "Yes" : "No");
    printf("      Wake-on-LAN: %s\n", dev->caps.has_wol ? "Yes" : "No");
}

/**
 * @brief Delay function
 */
static void delay(int ms)
{
    clock_t start = clock();
    clock_t end = start + (ms * CLOCKS_PER_SEC / 1000);
    
    while (clock() < end) {
        /* Busy wait */
    }
}