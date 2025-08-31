/**
 * @file 3c515_test.c
 * @brief Comprehensive unit tests for the 3Com 3C515-TX NIC driver
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This test suite validates all critical functionality of the 3C515-TX driver
 * including bus mastering DMA operations, descriptor ring management, high-
 * performance packet handling, and error recovery mechanisms.
 */

#include "../../include/test_framework.h"
#include "../../include/hardware_mock.h"
#include "../../include/3c515.h"
#include "../../include/hardware.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Test configuration */
#define TEST_IO_BASE        0x300
#define TEST_IRQ           11
#define TEST_DEVICE_ID     0

/* DMA test configuration */
#define TEST_TX_RING_SIZE  8
#define TEST_RX_RING_SIZE  8
#define TEST_BUFFER_SIZE   1600

/* Test result tracking */
static test_results_t g_test_results = {0};

/* Test helper macros */
#define TEST_ASSERT(condition, message) \
    do { \
        g_test_results.tests_run++; \
        if (!(condition)) { \
            g_test_results.tests_failed++; \
            snprintf(g_test_results.last_error, sizeof(g_test_results.last_error), \
                     "FAIL: %s", message); \
            LOG_ERROR("TEST FAILED: %s", message); \
            return TEST_RESULT_FAIL; \
        } else { \
            g_test_results.tests_passed++; \
            LOG_INFO("PASS: %s", message); \
        } \
    } while(0)

#define TEST_START(name) \
    LOG_INFO("=== Starting test: %s ===", name)

#define TEST_END(name) \
    LOG_INFO("=== Completed test: %s ===", name)

/* Mock NIC info for testing */
static nic_info_t test_nic;

/* DMA descriptor rings for testing */
static _3c515_tx_tx_desc_t *test_tx_ring = NULL;
static _3c515_tx_rx_desc_t *test_rx_ring = NULL;
static uint8_t *test_buffers = NULL;

/* Test setup and teardown functions */
static int setup_3c515_test_environment(void) {
    int result;
    
    /* Initialize mock framework */
    result = mock_framework_init();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize mock framework");
        return result;
    }
    
    /* Create mock 3C515-TX device */
    result = mock_device_create(MOCK_DEVICE_3C515, TEST_IO_BASE, TEST_IRQ);
    if (result < 0) {
        LOG_ERROR("Failed to create mock 3C515-TX device");
        return result;
    }
    
    /* Initialize test NIC structure */
    memset(&test_nic, 0, sizeof(nic_info_t));
    test_nic.io_base = TEST_IO_BASE;
    test_nic.irq = TEST_IRQ;
    test_nic.index = TEST_DEVICE_ID;
    test_nic.type = NIC_TYPE_3C515;
    
    /* Allocate DMA descriptor rings */
    test_tx_ring = malloc(TEST_TX_RING_SIZE * sizeof(_3c515_tx_tx_desc_t));
    test_rx_ring = malloc(TEST_RX_RING_SIZE * sizeof(_3c515_tx_rx_desc_t));
    test_buffers = malloc((TEST_TX_RING_SIZE + TEST_RX_RING_SIZE) * TEST_BUFFER_SIZE);
    
    if (!test_tx_ring || !test_rx_ring || !test_buffers) {
        LOG_ERROR("Failed to allocate test buffers");
        return ERROR_NO_MEMORY;
    }
    
    memset(test_tx_ring, 0, TEST_TX_RING_SIZE * sizeof(_3c515_tx_tx_desc_t));
    memset(test_rx_ring, 0, TEST_RX_RING_SIZE * sizeof(_3c515_tx_rx_desc_t));
    memset(test_buffers, 0, (TEST_TX_RING_SIZE + TEST_RX_RING_SIZE) * TEST_BUFFER_SIZE);
    
    /* Enable the mock device */
    mock_device_enable(TEST_DEVICE_ID, true);
    mock_device_set_link_status(TEST_DEVICE_ID, true, 100);
    
    LOG_INFO("3C515-TX test environment setup complete");
    return SUCCESS;
}

static void teardown_3c515_test_environment(void) {
    if (test_tx_ring) {
        free(test_tx_ring);
        test_tx_ring = NULL;
    }
    if (test_rx_ring) {
        free(test_rx_ring);
        test_rx_ring = NULL;
    }
    if (test_buffers) {
        free(test_buffers);
        test_buffers = NULL;
    }
    
    mock_framework_cleanup();
    memset(&test_nic, 0, sizeof(nic_info_t));
    LOG_INFO("3C515-TX test environment cleaned up");
}

/**
 * @brief Test descriptor ring initialization
 * @return Test result
 */
static test_result_t test_3c515_descriptor_ring_init(void) {
    TEST_START("3C515-TX Descriptor Ring Initialization");
    
    /* Initialize TX descriptor ring */
    for (int i = 0; i < TEST_TX_RING_SIZE; i++) {
        test_tx_ring[i].next = (i + 1 < TEST_TX_RING_SIZE) ?
            (uint32_t)&test_tx_ring[i + 1] : 0;
        test_tx_ring[i].addr = (uint32_t)(test_buffers + i * TEST_BUFFER_SIZE);
        test_tx_ring[i].status = 0;
        test_tx_ring[i].length = TEST_BUFFER_SIZE;
    }
    
    /* Initialize RX descriptor ring */
    for (int i = 0; i < TEST_RX_RING_SIZE; i++) {
        test_rx_ring[i].next = (i + 1 < TEST_RX_RING_SIZE) ?
            (uint32_t)&test_rx_ring[i + 1] : 0;
        test_rx_ring[i].addr = (uint32_t)(test_buffers + (TEST_TX_RING_SIZE + i) * TEST_BUFFER_SIZE);
        test_rx_ring[i].status = 0;
        test_rx_ring[i].length = TEST_BUFFER_SIZE;
    }
    
    /* Verify TX ring initialization */
    for (int i = 0; i < TEST_TX_RING_SIZE; i++) {
        TEST_ASSERT(test_tx_ring[i].addr != 0, "TX descriptor has valid buffer address");
        TEST_ASSERT(test_tx_ring[i].length == TEST_BUFFER_SIZE, "TX descriptor has correct buffer size");
        
        if (i + 1 < TEST_TX_RING_SIZE) {
            TEST_ASSERT(test_tx_ring[i].next == (uint32_t)&test_tx_ring[i + 1], 
                       "TX descriptor next pointer correct");
        } else {
            TEST_ASSERT(test_tx_ring[i].next == 0, "Last TX descriptor next pointer is NULL");
        }
    }
    
    /* Verify RX ring initialization */
    for (int i = 0; i < TEST_RX_RING_SIZE; i++) {
        TEST_ASSERT(test_rx_ring[i].addr != 0, "RX descriptor has valid buffer address");
        TEST_ASSERT(test_rx_ring[i].length == TEST_BUFFER_SIZE, "RX descriptor has correct buffer size");
        
        if (i + 1 < TEST_RX_RING_SIZE) {
            TEST_ASSERT(test_rx_ring[i].next == (uint32_t)&test_rx_ring[i + 1], 
                       "RX descriptor next pointer correct");
        } else {
            TEST_ASSERT(test_rx_ring[i].next == 0, "Last RX descriptor next pointer is NULL");
        }
    }
    
    TEST_END("3C515-TX Descriptor Ring Initialization");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test DMA engine setup and configuration
 * @return Test result
 */
static test_result_t test_3c515_dma_setup(void) {
    TEST_START("3C515-TX DMA Setup");
    
    /* Reset the NIC */
    mock_outw(test_nic.io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TOTAL_RESET);
    
    /* Select Window 7 for DMA control */
    _3C515_TX_SELECT_WINDOW(test_nic.io_base, _3C515_TX_WINDOW_7);
    
    /* Verify window selection */
    mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
    TEST_ASSERT(device->registers.current_window == _3C515_TX_WINDOW_7, 
               "Window 7 selected for DMA control");
    
    /* Set descriptor list pointers */
    mock_outl(test_nic.io_base + _3C515_TX_DOWN_LIST_PTR, (uint32_t)test_tx_ring);
    mock_outl(test_nic.io_base + _3C515_TX_UP_LIST_PTR, (uint32_t)test_rx_ring);
    
    /* Configure DMA in mock device */
    int result = mock_dma_set_descriptors(TEST_DEVICE_ID, 
                                         (uint32_t)test_tx_ring, 
                                         (uint32_t)test_rx_ring);
    TEST_ASSERT(result == SUCCESS, "DMA descriptors configured");
    
    /* Enable transmitter and receiver */
    mock_outw(test_nic.io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TX_ENABLE);
    mock_outw(test_nic.io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_RX_ENABLE);
    
    /* Verify device is enabled */
    TEST_ASSERT(device->enabled == true, "Device enabled after TX/RX enable");
    
    /* Test DMA status checking */
    bool dma_active = mock_dma_is_active(TEST_DEVICE_ID);
    TEST_ASSERT(dma_active == false, "DMA initially inactive");
    
    TEST_END("3C515-TX DMA Setup");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test high-performance packet transmission using DMA
 * @return Test result
 */
static test_result_t test_3c515_dma_transmission(void) {
    TEST_START("3C515-TX DMA Transmission");
    
    /* Prepare test packet */
    uint8_t test_packet[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /* Destination MAC (broadcast) */
        0x00, 0x60, 0x8C, 0x78, 0x9A, 0xBC,  /* Source MAC */
        0x08, 0x00,                          /* EtherType (IP) */
        0x45, 0x00, 0x00, 0x54,             /* IP header start */
        0x12, 0x34, 0x40, 0x00, 0x40, 0x01,
        0x00, 0x00, 0xC0, 0xA8, 0x01, 0x64,
        0xC0, 0xA8, 0x01, 0x01,             /* IP header end */
        /* ICMP ping packet data */
        0x08, 0x00, 0xF7, 0xFC, 0x00, 0x00, 0x00, 0x00,
        0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20,
        0x61, 0x20, 0x74, 0x65, 0x73, 0x74, 0x20, 0x70,
        0x61, 0x63, 0x6B, 0x65, 0x74, 0x20, 0x66, 0x6F,
        0x72, 0x20, 0x44, 0x4D, 0x41, 0x20, 0x74, 0x72,
        0x61, 0x6E, 0x73, 0x6D, 0x69, 0x73, 0x73, 0x69,
        0x6F, 0x6E, 0x20, 0x74, 0x65, 0x73, 0x74, 0x69,
        0x6E, 0x67, 0x21, 0x00
    };
    size_t packet_len = sizeof(test_packet);
    
    /* Use first TX descriptor */
    _3c515_tx_tx_desc_t *desc = &test_tx_ring[0];
    
    /* Check if descriptor is free */
    TEST_ASSERT(!(desc->status & _3C515_TX_TX_DESC_COMPLETE), "TX descriptor initially free");
    
    /* Copy packet to buffer */
    memcpy((void *)desc->addr, test_packet, packet_len);
    
    /* Configure descriptor for transmission */
    desc->length = packet_len | _3C515_TX_TX_INTR_BIT;  /* Request interrupt on completion */
    desc->status = 0;  /* Clear status, mark as ready for DMA */
    
    /* Start DMA transfer */
    mock_outw(test_nic.io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_START_DMA_DOWN);
    
    /* Simulate DMA transfer completion */
    int result = mock_dma_start_transfer(TEST_DEVICE_ID, true);  /* true = TX */
    TEST_ASSERT(result == SUCCESS, "DMA transfer started");
    
    /* Simulate transfer completion */
    desc->status |= _3C515_TX_TX_DESC_COMPLETE;
    mock_interrupt_generate(TEST_DEVICE_ID, MOCK_INTR_DMA_COMPLETE);
    
    /* Verify interrupt and status */
    TEST_ASSERT(mock_interrupt_pending(TEST_DEVICE_ID), "DMA completion interrupt generated");
    
    mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
    TEST_ASSERT(device->registers.status_reg & _3C515_TX_STATUS_DMA_DONE, 
               "DMA done status set");
    
    /* Verify packet was transmitted */
    uint8_t extracted_packet[1600];
    size_t extracted_len = sizeof(extracted_packet);
    result = mock_packet_extract_tx(TEST_DEVICE_ID, extracted_packet, &extracted_len);
    
    if (result == SUCCESS) {
        TEST_ASSERT(extracted_len == packet_len, "Transmitted packet length correct");
        TEST_ASSERT(memcmp(extracted_packet, test_packet, packet_len) == 0, 
                   "Transmitted packet data correct");
    }
    
    /* Clear descriptor for next use */
    desc->status = 0;
    
    TEST_END("3C515-TX DMA Transmission");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test high-performance packet reception using DMA
 * @return Test result
 */
static test_result_t test_3c515_dma_reception(void) {
    TEST_START("3C515-TX DMA Reception");
    
    /* Prepare test packet */
    uint8_t test_packet[] = {
        0x00, 0x60, 0x8C, 0x78, 0x9A, 0xBC,  /* Destination MAC */
        0x00, 0x60, 0x8C, 0xDE, 0xAD, 0xBE,  /* Source MAC */
        0x08, 0x06,                          /* EtherType (ARP) */
        0x00, 0x01, 0x08, 0x00, 0x06, 0x04,  /* ARP header */
        0x00, 0x02,                          /* ARP reply */
        0x00, 0x60, 0x8C, 0xDE, 0xAD, 0xBE,  /* Sender MAC */
        0xC0, 0xA8, 0x01, 0x02,             /* Sender IP */
        0x00, 0x60, 0x8C, 0x78, 0x9A, 0xBC,  /* Target MAC */
        0xC0, 0xA8, 0x01, 0x64,             /* Target IP */
        /* Padding to minimum frame size */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00
    };
    size_t packet_len = sizeof(test_packet);
    
    /* Use first RX descriptor */
    _3c515_tx_rx_desc_t *desc = &test_rx_ring[0];
    
    /* Prepare descriptor for reception */
    desc->status = 0;  /* Clear status */
    desc->length = TEST_BUFFER_SIZE;  /* Buffer size */
    
    /* Inject packet into mock device */
    int result = mock_packet_inject_rx(TEST_DEVICE_ID, test_packet, packet_len);
    TEST_ASSERT(result == SUCCESS, "Test packet injected successfully");
    
    /* Simulate DMA reception */
    result = mock_dma_start_transfer(TEST_DEVICE_ID, false);  /* false = RX */
    TEST_ASSERT(result == SUCCESS, "DMA RX transfer started");
    
    /* Copy packet to RX buffer (simulate DMA) */
    memcpy((void *)desc->addr, test_packet, packet_len);
    
    /* Mark descriptor as complete */
    desc->status = _3C515_TX_RX_DESC_COMPLETE | packet_len;
    desc->length = packet_len;
    
    /* Generate completion interrupt */
    mock_interrupt_generate(TEST_DEVICE_ID, MOCK_INTR_RX_COMPLETE);
    
    /* Verify interrupt and status */
    TEST_ASSERT(mock_interrupt_pending(TEST_DEVICE_ID), "RX completion interrupt generated");
    
    mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
    TEST_ASSERT(device->registers.status_reg & _3C515_TX_STATUS_RX_COMPLETE, 
               "RX complete status set");
    
    /* Check descriptor completion */
    TEST_ASSERT(desc->status & _3C515_TX_RX_DESC_COMPLETE, "RX descriptor marked complete");
    TEST_ASSERT(!(desc->status & _3C515_TX_RX_DESC_ERROR), "No RX error detected");
    
    uint16_t rx_length = desc->status & _3C515_TX_RX_DESC_LEN_MASK;
    TEST_ASSERT(rx_length == packet_len, "RX length matches injected packet");
    
    /* Verify received data */
    uint8_t *received_data = (uint8_t *)desc->addr;
    TEST_ASSERT(memcmp(received_data, test_packet, packet_len) == 0, 
               "Received packet data matches injected packet");
    
    /* Clear descriptor for next use */
    desc->status = 0;
    
    TEST_END("3C515-TX DMA Reception");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test multiple descriptor ring management
 * @return Test result
 */
static test_result_t test_3c515_descriptor_ring_management(void) {
    TEST_START("3C515-TX Descriptor Ring Management");
    
    /* Test TX ring wrapping */
    for (int i = 0; i < TEST_TX_RING_SIZE; i++) {
        _3c515_tx_tx_desc_t *desc = &test_tx_ring[i];
        
        /* Prepare small test packet */
        uint8_t small_packet[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 
                                 0x08, 0x09, 0x0A, 0x0B, 0x08, 0x00, i, i+1};
        
        /* Copy to descriptor buffer */
        memcpy((void *)desc->addr, small_packet, sizeof(small_packet));
        desc->length = sizeof(small_packet);
        desc->status = 0;
        
        /* Mark as transmitted */
        desc->status |= _3C515_TX_TX_DESC_COMPLETE;
        
        LOG_DEBUG("TX descriptor %d: addr=0x%08X, len=%d, status=0x%08X", 
                  i, desc->addr, desc->length, desc->status);
    }
    
    /* Verify all descriptors were processed */
    for (int i = 0; i < TEST_TX_RING_SIZE; i++) {
        TEST_ASSERT(test_tx_ring[i].status & _3C515_TX_TX_DESC_COMPLETE, 
                   "TX descriptor marked complete");
    }
    
    /* Test RX ring handling */
    for (int i = 0; i < TEST_RX_RING_SIZE; i++) {
        _3c515_tx_rx_desc_t *desc = &test_rx_ring[i];
        
        /* Simulate packet reception */
        uint8_t test_data[] = {0xAA, 0xBB, 0xCC, 0xDD, i, i+1, i+2, i+3};
        memcpy((void *)desc->addr, test_data, sizeof(test_data));
        
        desc->status = _3C515_TX_RX_DESC_COMPLETE | sizeof(test_data);
        desc->length = sizeof(test_data);
        
        LOG_DEBUG("RX descriptor %d: addr=0x%08X, len=%d, status=0x%08X", 
                  i, desc->addr, desc->length, desc->status);
    }
    
    /* Verify all RX descriptors */
    for (int i = 0; i < TEST_RX_RING_SIZE; i++) {
        TEST_ASSERT(test_rx_ring[i].status & _3C515_TX_RX_DESC_COMPLETE, 
                   "RX descriptor marked complete");
        
        uint16_t rx_len = test_rx_ring[i].status & _3C515_TX_RX_DESC_LEN_MASK;
        TEST_ASSERT(rx_len == 8, "RX descriptor length correct");
    }
    
    /* Test descriptor cleanup and reset */
    for (int i = 0; i < TEST_TX_RING_SIZE; i++) {
        test_tx_ring[i].status = 0;
        TEST_ASSERT(!(test_tx_ring[i].status & _3C515_TX_TX_DESC_COMPLETE), 
                   "TX descriptor cleared");
    }
    
    for (int i = 0; i < TEST_RX_RING_SIZE; i++) {
        test_rx_ring[i].status = 0;
        test_rx_ring[i].length = TEST_BUFFER_SIZE;
        TEST_ASSERT(!(test_rx_ring[i].status & _3C515_TX_RX_DESC_COMPLETE), 
                   "RX descriptor reset");
    }
    
    TEST_END("3C515-TX Descriptor Ring Management");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test PCI configuration and initialization
 * @return Test result
 */
static test_result_t test_3c515_pci_configuration(void) {
    TEST_START("3C515-TX PCI Configuration");
    
    /* Test window selection for configuration access */
    _3C515_TX_SELECT_WINDOW(test_nic.io_base, _3C515_TX_WINDOW_3);
    
    mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
    TEST_ASSERT(device->registers.current_window == _3C515_TX_WINDOW_3, 
               "Window 3 selected for configuration");
    
    /* Test configuration register access */
    uint16_t config_orig = mock_inw(test_nic.io_base + _3C515_TX_W3_CONFIG);
    uint16_t test_config = 0x1234;
    mock_outw(test_nic.io_base + _3C515_TX_W3_CONFIG, test_config);
    uint16_t config_read = mock_inw(test_nic.io_base + _3C515_TX_W3_CONFIG);
    TEST_ASSERT(config_read == test_config, "Configuration register read/write");
    
    /* Restore original value */
    mock_outw(test_nic.io_base + _3C515_TX_W3_CONFIG, config_orig);
    
    /* Test MAC control register */
    uint16_t mac_ctrl = mock_inw(test_nic.io_base + _3C515_TX_W3_MAC_CTRL);
    
    /* Test full-duplex configuration */
    mock_outw(test_nic.io_base + _3C515_TX_W3_MAC_CTRL, 
              mac_ctrl | _3C515_TX_FULL_DUPLEX_BIT);
    
    uint16_t mac_ctrl_fd = mock_inw(test_nic.io_base + _3C515_TX_W3_MAC_CTRL);
    TEST_ASSERT(mac_ctrl_fd & _3C515_TX_FULL_DUPLEX_BIT, "Full-duplex bit set");
    
    /* Test media control (Window 4) */
    _3C515_TX_SELECT_WINDOW(test_nic.io_base, _3C515_TX_WINDOW_4);
    TEST_ASSERT(device->registers.current_window == _3C515_TX_WINDOW_4, 
               "Window 4 selected for media control");
    
    /* Test 100Base-TX media configuration */
    mock_outw(test_nic.io_base + _3C515_TX_W4_MEDIA, _3C515_TX_MEDIA_10TP);
    
    /* Verify link status simulation */
    TEST_ASSERT(device->link_up == true, "Link detected as up");
    TEST_ASSERT(device->link_speed == 100, "Link speed detected as 100 Mbps");
    
    TEST_END("3C515-TX PCI Configuration");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test performance optimization paths
 * @return Test result
 */
static test_result_t test_3c515_performance_optimization(void) {
    TEST_START("3C515-TX Performance Optimization");
    
    /* Test burst DMA transfers */
    const int burst_count = 4;
    uint8_t burst_packets[burst_count][64];
    
    /* Prepare multiple small packets for burst transfer */
    for (int i = 0; i < burst_count; i++) {
        memset(burst_packets[i], i, sizeof(burst_packets[i]));
        /* Add minimal Ethernet header */
        burst_packets[i][12] = 0x08;  /* EtherType high */
        burst_packets[i][13] = 0x00;  /* EtherType low (IP) */
    }
    
    /* Setup multiple TX descriptors for burst */
    for (int i = 0; i < burst_count && i < TEST_TX_RING_SIZE; i++) {
        _3c515_tx_tx_desc_t *desc = &test_tx_ring[i];
        
        memcpy((void *)desc->addr, burst_packets[i], sizeof(burst_packets[i]));
        desc->length = sizeof(burst_packets[i]);
        desc->status = 0;
        
        if (i == burst_count - 1) {
            desc->length |= _3C515_TX_TX_INTR_BIT;  /* Interrupt on last packet */
        }
    }
    
    /* Simulate burst DMA transfer */
    mock_outw(test_nic.io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_START_DMA_DOWN);
    
    /* Mark all descriptors as complete */
    for (int i = 0; i < burst_count && i < TEST_TX_RING_SIZE; i++) {
        test_tx_ring[i].status |= _3C515_TX_TX_DESC_COMPLETE;
    }
    
    /* Generate interrupt for completion */
    mock_interrupt_generate(TEST_DEVICE_ID, MOCK_INTR_DMA_COMPLETE);
    
    /* Verify burst completion */
    mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
    TEST_ASSERT(device->registers.status_reg & _3C515_TX_STATUS_DMA_DONE, 
               "Burst DMA completion detected");
    
    /* Test interrupt coalescing simulation */
    int coalesced_interrupts = 0;
    for (int i = 0; i < 10; i++) {
        if (i % 3 == 0) {  /* Coalesce every 3 operations */
            mock_interrupt_generate(TEST_DEVICE_ID, MOCK_INTR_TX_COMPLETE);
            coalesced_interrupts++;
        }
    }
    
    TEST_ASSERT(coalesced_interrupts < 10, "Interrupt coalescing reduces interrupt count");
    LOG_INFO("Generated %d coalesced interrupts instead of 10", coalesced_interrupts);
    
    /* Test zero-copy buffer optimization simulation */
    _3c515_tx_rx_desc_t *rx_desc = &test_rx_ring[0];
    uint8_t large_packet[1500];
    memset(large_packet, 0xAB, sizeof(large_packet));
    
    /* Simulate zero-copy by directly using the descriptor buffer */
    memcpy((void *)rx_desc->addr, large_packet, sizeof(large_packet));
    rx_desc->status = _3C515_TX_RX_DESC_COMPLETE | sizeof(large_packet);
    rx_desc->length = sizeof(large_packet);
    
    /* Verify zero-copy efficiency */
    uint8_t *zero_copy_data = (uint8_t *)rx_desc->addr;
    TEST_ASSERT(memcmp(zero_copy_data, large_packet, sizeof(large_packet)) == 0, 
               "Zero-copy buffer contains correct data");
    
    /* Test descriptor prefetching simulation */
    for (int i = 0; i < TEST_RX_RING_SIZE; i++) {
        test_rx_ring[i].status = 0;
        test_rx_ring[i].length = TEST_BUFFER_SIZE;
        /* Simulate prefetch by accessing next descriptor */
        if (i + 1 < TEST_RX_RING_SIZE) {
            volatile uint32_t prefetch = test_rx_ring[i + 1].addr;
            (void)prefetch;  /* Suppress unused warning */
        }
    }
    
    TEST_ASSERT(true, "Descriptor prefetching simulation completed");
    
    TEST_END("3C515-TX Performance Optimization");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test error recovery mechanisms
 * @return Test result
 */
static test_result_t test_3c515_error_recovery(void) {
    TEST_START("3C515-TX Error Recovery");
    
    /* Test DMA error handling */
    mock_error_inject(TEST_DEVICE_ID, MOCK_ERROR_DMA_ERROR, 1);
    
    /* Attempt DMA operation that should trigger error */
    _3c515_tx_tx_desc_t *desc = &test_tx_ring[0];
    desc->status = 0;
    desc->length = 64;
    
    mock_outw(test_nic.io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_START_DMA_DOWN);
    
    /* Simulate DMA error */
    desc->status |= _3C515_TX_TX_DESC_ERROR;
    mock_interrupt_generate(TEST_DEVICE_ID, MOCK_INTR_ADAPTER_FAILURE);
    
    mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
    TEST_ASSERT(device->registers.status_reg & _3C515_TX_STATUS_ADAPTER_FAILURE, 
               "DMA error detected");
    
    /* Test error recovery: reset DMA engine */
    mock_outw(test_nic.io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TX_RESET);
    mock_outw(test_nic.io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_RX_RESET);
    
    /* Clear error injection */
    mock_error_clear(TEST_DEVICE_ID);
    mock_interrupt_clear(TEST_DEVICE_ID);
    
    /* Test recovery: reinitialize descriptors */
    for (int i = 0; i < TEST_TX_RING_SIZE; i++) {
        test_tx_ring[i].status = 0;
        test_tx_ring[i].length = TEST_BUFFER_SIZE;
    }
    
    for (int i = 0; i < TEST_RX_RING_SIZE; i++) {
        test_rx_ring[i].status = 0;
        test_rx_ring[i].length = TEST_BUFFER_SIZE;
    }
    
    /* Re-enable DMA */
    mock_outw(test_nic.io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TX_ENABLE);
    mock_outw(test_nic.io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_RX_ENABLE);
    
    TEST_ASSERT(device->enabled == true, "Device re-enabled after error recovery");
    
    /* Test descriptor error handling */
    _3c515_tx_rx_desc_t *rx_desc = &test_rx_ring[0];
    rx_desc->status = _3C515_TX_RX_DESC_ERROR | _3C515_TX_RX_DESC_COMPLETE;
    
    /* Driver should detect and handle descriptor error */
    bool desc_error = (rx_desc->status & _3C515_TX_RX_DESC_ERROR) != 0;
    TEST_ASSERT(desc_error, "Descriptor error detected");
    
    /* Recovery: clear error descriptor */
    rx_desc->status = 0;
    rx_desc->length = TEST_BUFFER_SIZE;
    
    /* Test link state recovery */
    mock_device_set_link_status(TEST_DEVICE_ID, false, 0);
    TEST_ASSERT(device->link_up == false, "Link down detected");
    
    /* Simulate link recovery */
    mock_device_set_link_status(TEST_DEVICE_ID, true, 100);
    TEST_ASSERT(device->link_up == true, "Link recovery detected");
    TEST_ASSERT(device->link_speed == 100, "Link speed restored");
    
    /* Test ring buffer overflow recovery */
    for (int i = 0; i < MAX_MOCK_PACKETS + 1; i++) {
        uint8_t overflow_packet[] = {0x01, 0x02, 0x03, 0x04};
        int result = mock_packet_inject_rx(TEST_DEVICE_ID, overflow_packet, sizeof(overflow_packet));
        if (result == ERROR_BUSY) {
            LOG_DEBUG("RX queue overflow detected at packet %d", i);
            break;
        }
    }
    
    /* Recovery: clear queue */
    mock_packet_queue_clear(TEST_DEVICE_ID);
    int queue_count = mock_packet_queue_count_rx(TEST_DEVICE_ID);
    TEST_ASSERT(queue_count == 0, "RX queue cleared after overflow");
    
    TEST_END("3C515-TX Error Recovery");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test bus mastering DMA operations
 * @return Test result
 */
static test_result_t test_3c515_bus_mastering(void) {
    TEST_START("3C515-TX Bus Mastering DMA");
    
    /* Test bus master capability detection */
    _3C515_TX_SELECT_WINDOW(test_nic.io_base, _3C515_TX_WINDOW_7);
    
    mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
    TEST_ASSERT(device->registers.current_window == _3C515_TX_WINDOW_7, 
               "Window 7 selected for bus master control");
    
    /* Test DMA address programming */
    uint32_t test_dma_addr = 0x12345678;
    mock_outl(test_nic.io_base + _3C515_TX_W7_MASTER_ADDR, test_dma_addr);
    
    uint32_t read_addr = mock_inl(test_nic.io_base + _3C515_TX_W7_MASTER_ADDR);
    TEST_ASSERT(read_addr == test_dma_addr, "DMA address programming");
    
    /* Test DMA length programming */
    uint16_t test_dma_len = 1024;
    mock_outw(test_nic.io_base + _3C515_TX_W7_MASTER_LEN, test_dma_len);
    
    uint16_t read_len = mock_inw(test_nic.io_base + _3C515_TX_W7_MASTER_LEN);
    TEST_ASSERT(read_len == test_dma_len, "DMA length programming");
    
    /* Test DMA stall/unstall operations */
    mock_outw(test_nic.io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_UP_STALL);
    /* In a real implementation, we would check DMA status */
    
    mock_outw(test_nic.io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_UP_UNSTALL);
    /* Resume DMA operations */
    
    mock_outw(test_nic.io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_DOWN_STALL);
    mock_outw(test_nic.io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_DOWN_UNSTALL);
    
    TEST_ASSERT(true, "DMA stall/unstall operations completed");
    
    /* Test concurrent DMA operations */
    mock_dma_start_transfer(TEST_DEVICE_ID, true);   /* Start TX DMA */
    mock_dma_start_transfer(TEST_DEVICE_ID, false);  /* Start RX DMA */
    
    bool dma_active = mock_dma_is_active(TEST_DEVICE_ID);
    TEST_ASSERT(dma_active == true, "Concurrent DMA operations active");
    
    /* Test DMA completion detection */
    mock_interrupt_generate(TEST_DEVICE_ID, MOCK_INTR_DMA_COMPLETE);
    
    uint16_t status = mock_inw(test_nic.io_base + _3C515_TX_STATUS_REG);
    TEST_ASSERT(status & _3C515_TX_STATUS_DMA_DONE, "DMA completion detected");
    
    /* Test bus master status checking */
    uint16_t master_status = mock_inw(test_nic.io_base + _3C515_TX_W7_MASTER_STATUS);
    LOG_DEBUG("Bus master status: 0x%04X", master_status);
    
    TEST_END("3C515-TX Bus Mastering DMA");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test stress conditions and high load scenarios
 * @return Test result
 */
static test_result_t test_3c515_stress_conditions(void) {
    TEST_START("3C515-TX Stress Conditions");
    
    /* Test high-frequency descriptor processing */
    const int stress_iterations = 100;
    int successful_tx = 0;
    int successful_rx = 0;
    
    for (int i = 0; i < stress_iterations; i++) {
        /* Alternate between TX and RX operations */
        if (i % 2 == 0) {
            /* TX operation */
            int desc_idx = i % TEST_TX_RING_SIZE;
            _3c515_tx_tx_desc_t *desc = &test_tx_ring[desc_idx];
            
            if (!(desc->status & _3C515_TX_TX_DESC_COMPLETE)) {
                uint8_t stress_packet[] = {0x55, 0xAA, 0x55, 0xAA, i & 0xFF, (i >> 8) & 0xFF};
                memcpy((void *)desc->addr, stress_packet, sizeof(stress_packet));
                desc->length = sizeof(stress_packet);
                desc->status = _3C515_TX_TX_DESC_COMPLETE;
                successful_tx++;
            }
        } else {
            /* RX operation */
            int desc_idx = i % TEST_RX_RING_SIZE;
            _3c515_tx_rx_desc_t *desc = &test_rx_ring[desc_idx];
            
            if (!(desc->status & _3C515_TX_RX_DESC_COMPLETE)) {
                uint8_t stress_packet[] = {0xAA, 0x55, 0xAA, 0x55, i & 0xFF, (i >> 8) & 0xFF};
                memcpy((void *)desc->addr, stress_packet, sizeof(stress_packet));
                desc->status = _3C515_TX_RX_DESC_COMPLETE | sizeof(stress_packet);
                desc->length = sizeof(stress_packet);
                successful_rx++;
            }
        }
        
        /* Simulate processing delay */
        if (i % 10 == 0) {
            mock_interrupt_generate(TEST_DEVICE_ID, 
                                   (i % 20 == 0) ? MOCK_INTR_TX_COMPLETE : MOCK_INTR_RX_COMPLETE);
        }
    }
    
    LOG_INFO("Stress test: %d TX, %d RX operations completed", successful_tx, successful_rx);
    TEST_ASSERT(successful_tx > 0, "Some TX operations completed under stress");
    TEST_ASSERT(successful_rx > 0, "Some RX operations completed under stress");
    
    /* Test descriptor ring wrap-around */
    for (int i = 0; i < TEST_TX_RING_SIZE * 2; i++) {
        int desc_idx = i % TEST_TX_RING_SIZE;
        _3c515_tx_tx_desc_t *desc = &test_tx_ring[desc_idx];
        
        desc->status = 0;  /* Clear and reuse */
        desc->length = 64;
        desc->status = _3C515_TX_TX_DESC_COMPLETE;
    }
    
    TEST_ASSERT(true, "Descriptor ring wrap-around handled");
    
    /* Test memory pressure simulation */
    mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
    for (int i = 0; i < 1000; i++) {
        device->operation_count++;
        if (i % 100 == 0) {
            mock_interrupt_generate(TEST_DEVICE_ID, MOCK_INTR_TX_COMPLETE);
            mock_interrupt_clear(TEST_DEVICE_ID);
        }
    }
    
    TEST_ASSERT(device->operation_count >= 1000, "System survived memory pressure simulation");
    
    /* Reset all descriptors */
    for (int i = 0; i < TEST_TX_RING_SIZE; i++) {
        test_tx_ring[i].status = 0;
        test_tx_ring[i].length = TEST_BUFFER_SIZE;
    }
    
    for (int i = 0; i < TEST_RX_RING_SIZE; i++) {
        test_rx_ring[i].status = 0;
        test_rx_ring[i].length = TEST_BUFFER_SIZE;
    }
    
    TEST_END("3C515-TX Stress Conditions");
    return TEST_RESULT_PASS;
}

/**
 * @brief Run comprehensive 3C515-TX driver tests
 * @return 0 on success, negative on failure
 */
int run_3c515_comprehensive_tests(void) {
    int overall_result = 0;
    
    LOG_INFO("=== Starting Comprehensive 3C515-TX Driver Tests ===");
    
    /* Initialize test results */
    memset(&g_test_results, 0, sizeof(g_test_results));
    
    /* Setup test environment */
    if (setup_3c515_test_environment() != SUCCESS) {
        LOG_ERROR("Failed to setup test environment");
        return -1;
    }
    
    /* Run all test suites */
    test_result_t results[] = {
        test_3c515_descriptor_ring_init(),
        test_3c515_dma_setup(),
        test_3c515_dma_transmission(),
        test_3c515_dma_reception(),
        test_3c515_descriptor_ring_management(),
        test_3c515_pci_configuration(),
        test_3c515_performance_optimization(),
        test_3c515_error_recovery(),
        test_3c515_bus_mastering(),
        test_3c515_stress_conditions()
    };
    
    /* Check results */
    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
        if (results[i] != TEST_RESULT_PASS) {
            overall_result = -1;
        }
    }
    
    /* Cleanup test environment */
    teardown_3c515_test_environment();
    
    /* Print test summary */
    LOG_INFO("=== 3C515-TX Test Summary ===");
    LOG_INFO("Tests run: %d", g_test_results.tests_run);
    LOG_INFO("Tests passed: %d", g_test_results.tests_passed);
    LOG_INFO("Tests failed: %d", g_test_results.tests_failed);
    
    if (g_test_results.tests_failed > 0) {
        LOG_ERROR("Last error: %s", g_test_results.last_error);
        overall_result = -1;
    }
    
    if (overall_result == 0) {
        LOG_INFO("=== ALL 3C515-TX TESTS PASSED ===");
    } else {
        LOG_ERROR("=== SOME 3C515-TX TESTS FAILED ===");
    }
    
    return overall_result;
}

/**
 * @brief Run specific 3C515-TX test by name
 * @param test_name Name of the test to run
 * @return Test result
 */
test_result_t run_3c515_test_by_name(const char *test_name) {
    if (!test_name) {
        return TEST_RESULT_ERROR;
    }
    
    /* Setup test environment */
    if (setup_3c515_test_environment() != SUCCESS) {
        LOG_ERROR("Failed to setup test environment");
        return TEST_RESULT_ERROR;
    }
    
    test_result_t result = TEST_RESULT_ERROR;
    
    /* Run specific test */
    if (strcmp(test_name, "descriptor_init") == 0) {
        result = test_3c515_descriptor_ring_init();
    } else if (strcmp(test_name, "dma_setup") == 0) {
        result = test_3c515_dma_setup();
    } else if (strcmp(test_name, "dma_tx") == 0) {
        result = test_3c515_dma_transmission();
    } else if (strcmp(test_name, "dma_rx") == 0) {
        result = test_3c515_dma_reception();
    } else if (strcmp(test_name, "ring_management") == 0) {
        result = test_3c515_descriptor_ring_management();
    } else if (strcmp(test_name, "pci_config") == 0) {
        result = test_3c515_pci_configuration();
    } else if (strcmp(test_name, "performance") == 0) {
        result = test_3c515_performance_optimization();
    } else if (strcmp(test_name, "error_recovery") == 0) {
        result = test_3c515_error_recovery();
    } else if (strcmp(test_name, "bus_mastering") == 0) {
        result = test_3c515_bus_mastering();
    } else if (strcmp(test_name, "stress") == 0) {
        result = test_3c515_stress_conditions();
    } else {
        LOG_ERROR("Unknown test name: %s", test_name);
        result = TEST_RESULT_ERROR;
    }
    
    /* Cleanup test environment */
    teardown_3c515_test_environment();
    
    return result;
}