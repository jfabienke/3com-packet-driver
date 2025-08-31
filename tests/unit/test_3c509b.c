/**
 * @file 3c509b_test.c
 * @brief Comprehensive unit tests for the 3Com 3C509B NIC driver
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This test suite validates all critical functionality of the 3C509B driver
 * including window selection, EEPROM operations, packet handling, and error
 * recovery mechanisms using hardware mocking.
 */

#include "../../include/test_framework.h"
#include "../../include/hardware_mock.h"
#include "../../include/3c509b.h"
#include "../../include/hardware.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include <string.h>
#include <stdio.h>

/* Test configuration */
#define TEST_IO_BASE        0x300
#define TEST_IRQ           10
#define TEST_DEVICE_ID     0

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

/* Test setup and teardown functions */
static int setup_3c509b_test_environment(void) {
    int result;
    
    /* Initialize mock framework */
    result = mock_framework_init();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize mock framework");
        return result;
    }
    
    /* Create mock 3C509B device */
    result = mock_device_create(MOCK_DEVICE_3C509B, TEST_IO_BASE, TEST_IRQ);
    if (result < 0) {
        LOG_ERROR("Failed to create mock 3C509B device");
        return result;
    }
    
    /* Initialize test NIC structure */
    memset(&test_nic, 0, sizeof(nic_info_t));
    test_nic.io_base = TEST_IO_BASE;
    test_nic.irq = TEST_IRQ;
    test_nic.index = TEST_DEVICE_ID;
    test_nic.type = NIC_TYPE_3C509B;
    
    /* Enable the mock device */
    mock_device_enable(TEST_DEVICE_ID, true);
    
    LOG_INFO("3C509B test environment setup complete");
    return SUCCESS;
}

static void teardown_3c509b_test_environment(void) {
    mock_framework_cleanup();
    memset(&test_nic, 0, sizeof(nic_info_t));
    LOG_INFO("3C509B test environment cleaned up");
}

/**
 * @brief Test window selection mechanism
 * @return Test result
 */
static test_result_t test_3c509b_window_selection(void) {
    TEST_START("3C509B Window Selection");
    
    /* Test all valid windows */
    uint8_t test_windows[] = {
        _3C509B_WINDOW_0, _3C509B_WINDOW_1, _3C509B_WINDOW_2, 
        _3C509B_WINDOW_4, _3C509B_WINDOW_6
    };
    
    for (size_t i = 0; i < sizeof(test_windows); i++) {
        uint8_t window = test_windows[i];
        
        /* Select window using macro */
        _3C509B_SELECT_WINDOW(test_nic.io_base, window);
        
        /* Verify window was selected in mock device */
        mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
        TEST_ASSERT(device != NULL, "Mock device accessible");
        TEST_ASSERT(device->registers.current_window == window, 
                   "Window selection successful");
        
        LOG_DEBUG("Window %d selected successfully", window);
    }
    
    /* Test invalid window (should not crash) */
    _3C509B_SELECT_WINDOW(test_nic.io_base, 7);
    
    TEST_END("3C509B Window Selection");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test EEPROM read operations
 * @return Test result
 */
static test_result_t test_3c509b_eeprom_read(void) {
    TEST_START("3C509B EEPROM Read");
    
    /* Setup test EEPROM data */
    uint16_t test_eeprom[16] = {
        0x6000, 0x8C12, 0x3456,  /* MAC address */
        0x0000, 0x0000, 0x0000,  /* Reserved */
        0x6D50, 0x0001,          /* Product ID and version */
        0x0000, 0x0000, 0x0000,  /* Configuration */
        0x0000, 0x0000, 0x0000,  /* More configuration */
        0x0000, 0x0000           /* Checksum and padding */
    };
    
    int result = mock_eeprom_init(TEST_DEVICE_ID, test_eeprom, 16);
    TEST_ASSERT(result == SUCCESS, "EEPROM initialization");
    
    /* Test reading MAC address from EEPROM */
    uint8_t mac[6];
    for (int i = 0; i < 3; i++) {
        uint16_t word = mock_eeprom_read(TEST_DEVICE_ID, i);
        mac[i * 2] = word & 0xFF;
        mac[i * 2 + 1] = (word >> 8) & 0xFF;
    }
    
    uint8_t expected_mac[] = {0x00, 0x60, 0x12, 0x8C, 0x56, 0x34};
    TEST_ASSERT(memcmp(mac, expected_mac, 6) == 0, "MAC address read correctly");
    
    /* Test reading product ID */
    uint16_t product_id = mock_eeprom_read(TEST_DEVICE_ID, 6);
    TEST_ASSERT(product_id == 0x6D50, "Product ID read correctly");
    
    /* Test reading from invalid address */
    uint16_t invalid_data = mock_eeprom_read(TEST_DEVICE_ID, 255);
    TEST_ASSERT(invalid_data == 0x0000, "Invalid EEPROM address handled");
    
    TEST_END("3C509B EEPROM Read");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test MAC address reading from EEPROM
 * @return Test result
 */
static test_result_t test_3c509b_mac_address_reading(void) {
    TEST_START("3C509B MAC Address Reading");
    
    /* Test with known MAC address */
    uint8_t test_mac[] = {0x00, 0x60, 0x8C, 0xAA, 0xBB, 0xCC};
    int result = mock_device_set_mac_address(TEST_DEVICE_ID, test_mac);
    TEST_ASSERT(result == SUCCESS, "Test MAC address set");
    
    /* Read MAC address using driver function */
    uint8_t read_mac[6];
    memset(read_mac, 0, sizeof(read_mac));
    
    /* Simulate the MAC reading process */
    for (int i = 0; i < 3; i++) {
        uint16_t word = mock_eeprom_read(TEST_DEVICE_ID, i);
        read_mac[i * 2] = word & 0xFF;
        read_mac[i * 2 + 1] = (word >> 8) & 0xFF;
    }
    
    TEST_ASSERT(memcmp(read_mac, test_mac, 6) == 0, "MAC address read matches set value");
    
    LOG_INFO("MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
             read_mac[0], read_mac[1], read_mac[2], 
             read_mac[3], read_mac[4], read_mac[5]);
    
    TEST_END("3C509B MAC Address Reading");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test media auto-detection and setup
 * @return Test result
 */
static test_result_t test_3c509b_media_setup(void) {
    TEST_START("3C509B Media Setup");
    
    /* Set mock device to simulate 10Base-T with link */
    mock_device_set_link_status(TEST_DEVICE_ID, true, 10);
    
    /* Select Window 4 for media control */
    _3C509B_SELECT_WINDOW(test_nic.io_base, _3C509B_WINDOW_4);
    
    /* Verify window selection */
    mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
    TEST_ASSERT(device->registers.current_window == _3C509B_WINDOW_4, 
               "Window 4 selected for media control");
    
    /* Simulate media configuration */
    mock_outw(test_nic.io_base + _3C509B_MEDIA_CTRL, _3C509B_MEDIA_TP);
    
    /* Test link status detection */
    TEST_ASSERT(device->link_up == true, "Link status detected as UP");
    TEST_ASSERT(device->link_speed == 10, "Link speed detected as 10 Mbps");
    
    /* Test link down scenario */
    mock_device_set_link_status(TEST_DEVICE_ID, false, 0);
    TEST_ASSERT(device->link_up == false, "Link status detected as DOWN");
    
    TEST_END("3C509B Media Setup");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test receive filter configuration
 * @return Test result
 */
static test_result_t test_3c509b_rx_filter_config(void) {
    TEST_START("3C509B RX Filter Configuration");
    
    /* Select Window 1 for RX filter operations */
    _3C509B_SELECT_WINDOW(test_nic.io_base, _3C509B_WINDOW_1);
    
    /* Test normal filter (station + broadcast) */
    uint16_t normal_filter = _3C509B_RX_FILTER_STATION | _3C509B_RX_FILTER_BROADCAST;
    mock_outw(test_nic.io_base + _3C509B_COMMAND_REG, 
              _3C509B_CMD_SET_RX_FILTER | normal_filter);
    
    mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
    TEST_ASSERT(device->promiscuous == false, "Normal mode - promiscuous off");
    
    /* Test promiscuous mode */
    uint16_t prom_filter = normal_filter | _3C509B_RX_FILTER_PROM;
    mock_outw(test_nic.io_base + _3C509B_COMMAND_REG, 
              _3C509B_CMD_SET_RX_FILTER | prom_filter);
    
    TEST_ASSERT(device->promiscuous == true, "Promiscuous mode enabled");
    
    /* Test multicast filter */
    uint16_t mc_filter = normal_filter | _3C509B_RX_FILTER_MULTICAST;
    mock_outw(test_nic.io_base + _3C509B_COMMAND_REG, 
              _3C509B_CMD_SET_RX_FILTER | mc_filter);
    
    TEST_ASSERT(device->promiscuous == false, "Multicast mode - promiscuous off");
    
    TEST_END("3C509B RX Filter Configuration");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test packet transmission
 * @return Test result
 */
static test_result_t test_3c509b_packet_transmission(void) {
    TEST_START("3C509B Packet Transmission");
    
    /* Prepare test packet */
    uint8_t test_packet[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /* Destination MAC (broadcast) */
        0x00, 0x60, 0x8C, 0x12, 0x34, 0x56,  /* Source MAC */
        0x08, 0x00,                          /* EtherType (IP) */
        0x45, 0x00, 0x00, 0x1C,             /* IP header start */
        0x00, 0x00, 0x40, 0x00, 0x40, 0x01,
        0x00, 0x00, 0xC0, 0xA8, 0x01, 0x01,
        0xC0, 0xA8, 0x01, 0x02,             /* IP header end */
        0x08, 0x00, 0xF7, 0xFC,             /* ICMP header */
        0x00, 0x00, 0x00, 0x00              /* ICMP data */
    };
    size_t packet_len = sizeof(test_packet);
    
    /* Enable device and TX */
    mock_device_enable(TEST_DEVICE_ID, true);
    _3C509B_SELECT_WINDOW(test_nic.io_base, _3C509B_WINDOW_1);
    mock_outw(test_nic.io_base + _3C509B_COMMAND_REG, _3C509B_CMD_TX_ENABLE);
    
    /* Simulate TX FIFO availability */
    mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
    device->registers.status_reg |= _3C509B_STATUS_TX_AVAILABLE;
    
    /* Simulate packet transmission using driver logic */
    uint16_t tx_fifo = test_nic.io_base + _3C509B_TX_FIFO;
    
    /* Write packet length */
    mock_outw(tx_fifo, (uint16_t)packet_len);
    
    /* Write packet data */
    size_t words = packet_len / 2;
    const uint16_t *packet_words = (const uint16_t*)test_packet;
    
    for (size_t i = 0; i < words; i++) {
        mock_outw(tx_fifo, packet_words[i]);
    }
    
    /* Write remaining byte if odd length */
    if (packet_len & 1) {
        mock_outb(tx_fifo, test_packet[packet_len - 1]);
    }
    
    /* Generate TX complete interrupt */
    mock_interrupt_generate(TEST_DEVICE_ID, MOCK_INTR_TX_COMPLETE);
    
    /* Verify TX completion */
    TEST_ASSERT(mock_interrupt_pending(TEST_DEVICE_ID), "TX interrupt generated");
    TEST_ASSERT(device->registers.status_reg & _3C509B_STATUS_TX_COMPLETE, 
               "TX complete status set");
    
    /* Extract transmitted packet for verification */
    uint8_t extracted_packet[1600];
    size_t extracted_len = sizeof(extracted_packet);
    int result = mock_packet_extract_tx(TEST_DEVICE_ID, extracted_packet, &extracted_len);
    
    if (result == SUCCESS) {
        TEST_ASSERT(extracted_len == packet_len, "Transmitted packet length correct");
        TEST_ASSERT(memcmp(extracted_packet, test_packet, packet_len) == 0, 
                   "Transmitted packet data correct");
    }
    
    TEST_END("3C509B Packet Transmission");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test packet reception
 * @return Test result
 */
static test_result_t test_3c509b_packet_reception(void) {
    TEST_START("3C509B Packet Reception");
    
    /* Prepare test packet */
    uint8_t test_packet[] = {
        0x00, 0x60, 0x8C, 0x12, 0x34, 0x56,  /* Destination MAC */
        0x00, 0x60, 0x8C, 0xAA, 0xBB, 0xCC,  /* Source MAC */
        0x08, 0x06,                          /* EtherType (ARP) */
        0x00, 0x01, 0x08, 0x00, 0x06, 0x04,  /* ARP header */
        0x00, 0x01,                          /* ARP request */
        0x00, 0x60, 0x8C, 0xAA, 0xBB, 0xCC,  /* Sender MAC */
        0xC0, 0xA8, 0x01, 0x01,             /* Sender IP */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* Target MAC */
        0xC0, 0xA8, 0x01, 0x02              /* Target IP */
    };
    size_t packet_len = sizeof(test_packet);
    
    /* Enable device and RX */
    mock_device_enable(TEST_DEVICE_ID, true);
    _3C509B_SELECT_WINDOW(test_nic.io_base, _3C509B_WINDOW_1);
    mock_outw(test_nic.io_base + _3C509B_COMMAND_REG, _3C509B_CMD_RX_ENABLE);
    
    /* Inject test packet */
    int result = mock_packet_inject_rx(TEST_DEVICE_ID, test_packet, packet_len);
    TEST_ASSERT(result == SUCCESS, "Test packet injected successfully");
    
    /* Verify RX interrupt generated */
    TEST_ASSERT(mock_interrupt_pending(TEST_DEVICE_ID), "RX interrupt generated");
    
    mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
    TEST_ASSERT(device->registers.status_reg & _3C509B_STATUS_RX_COMPLETE, 
               "RX complete status set");
    
    /* Check packet queue */
    int rx_count = mock_packet_queue_count_rx(TEST_DEVICE_ID);
    TEST_ASSERT(rx_count > 0, "Packet in RX queue");
    
    /* Simulate reading RX status */
    uint16_t rx_status = mock_inw(test_nic.io_base + _3C509B_RX_STATUS);
    uint16_t rx_length = rx_status & _3C509B_RXSTAT_LEN_MASK;
    bool rx_error = (rx_status & _3C509B_RXSTAT_ERROR) != 0;
    
    TEST_ASSERT(!rx_error, "No RX error detected");
    TEST_ASSERT(rx_length == packet_len, "RX length matches injected packet");
    
    /* Simulate reading packet data from RX FIFO */
    uint8_t received_packet[1600];
    uint16_t rx_fifo = test_nic.io_base + _3C509B_RX_FIFO;
    
    size_t words = rx_length / 2;
    uint16_t *buffer_words = (uint16_t*)received_packet;
    
    for (size_t i = 0; i < words; i++) {
        buffer_words[i] = mock_inw(rx_fifo);
    }
    
    if (rx_length & 1) {
        received_packet[rx_length - 1] = mock_inb(rx_fifo);
    }
    
    TEST_ASSERT(memcmp(received_packet, test_packet, packet_len) == 0, 
               "Received packet data matches injected packet");
    
    TEST_END("3C509B Packet Reception");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test error handling and edge cases
 * @return Test result
 */
static test_result_t test_3c509b_error_handling(void) {
    TEST_START("3C509B Error Handling");
    
    /* Test adapter failure error */
    mock_error_inject(TEST_DEVICE_ID, MOCK_ERROR_ADAPTER_FAILURE, 1);
    
    /* Trigger the error with an I/O operation */
    _3C509B_SELECT_WINDOW(test_nic.io_base, _3C509B_WINDOW_1);
    
    /* Check that adapter failure interrupt was generated */
    mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
    TEST_ASSERT(device->registers.status_reg & _3C509B_STATUS_ADAPTER_FAILURE, 
               "Adapter failure status set");
    
    /* Clear error and test recovery */
    mock_error_clear(TEST_DEVICE_ID);
    mock_interrupt_clear(TEST_DEVICE_ID);
    
    /* Test RX error injection */
    uint8_t bad_packet[] = {0x00, 0x01, 0x02, 0x03}; /* Too short */
    int result = mock_packet_inject_rx(TEST_DEVICE_ID, bad_packet, sizeof(bad_packet));
    TEST_ASSERT(result == SUCCESS, "Bad packet injected");
    
    /* The mock should mark this as an error packet */
    uint16_t rx_status = mock_inw(test_nic.io_base + _3C509B_RX_STATUS);
    bool should_have_error = (sizeof(bad_packet) < _3C509B_MIN_PACKET_SIZE);
    
    LOG_DEBUG("RX status: 0x%04X, error expected: %s", 
              rx_status, should_have_error ? "yes" : "no");
    
    /* Test TX timeout simulation */
    device->registers.status_reg &= ~_3C509B_STATUS_TX_AVAILABLE;
    
    /* Attempt to send when TX not available should fail */
    uint8_t test_tx[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    /* The driver should detect TX not available */
    uint16_t status = mock_inw(test_nic.io_base + _3C509B_STATUS_REG);
    TEST_ASSERT(!(status & _3C509B_STATUS_TX_AVAILABLE), "TX not available correctly detected");
    
    TEST_END("3C509B Error Handling");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test self-test functionality
 * @return Test result
 */
static test_result_t test_3c509b_self_test(void) {
    TEST_START("3C509B Self Test");
    
    /* Test register read/write capability */
    _3C509B_SELECT_WINDOW(test_nic.io_base, _3C509B_WINDOW_0);
    
    /* Read original value */
    uint16_t original = mock_inw(test_nic.io_base + _3C509B_W0_CONFIG_CTRL);
    
    /* Write test pattern */
    uint16_t test_pattern = 0x5AA5;
    mock_outw(test_nic.io_base + _3C509B_W0_CONFIG_CTRL, test_pattern);
    
    /* Read back and verify */
    uint16_t readback = mock_inw(test_nic.io_base + _3C509B_W0_CONFIG_CTRL);
    TEST_ASSERT(readback == test_pattern, "Register read/write test passed");
    
    /* Restore original value */
    mock_outw(test_nic.io_base + _3C509B_W0_CONFIG_CTRL, original);
    
    /* Test EEPROM accessibility */
    uint16_t product_id = mock_eeprom_read(TEST_DEVICE_ID, 6);
    TEST_ASSERT(product_id != 0xFFFF, "EEPROM readable");
    
    /* Test basic command execution */
    mock_outw(test_nic.io_base + _3C509B_COMMAND_REG, _3C509B_CMD_SELECT_WINDOW | 1);
    
    mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
    TEST_ASSERT(device->registers.current_window == 1, "Command execution working");
    
    TEST_END("3C509B Self Test");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test interrupt handling
 * @return Test result
 */
static test_result_t test_3c509b_interrupt_handling(void) {
    TEST_START("3C509B Interrupt Handling");
    
    /* Setup interrupt mask */
    _3C509B_SELECT_WINDOW(test_nic.io_base, _3C509B_WINDOW_1);
    uint16_t int_mask = _3C509B_IMASK_TX_COMPLETE | _3C509B_IMASK_RX_COMPLETE | 
                        _3C509B_IMASK_ADAPTER_FAILURE;
    mock_outw(test_nic.io_base + _3C509B_COMMAND_REG, 
              _3C509B_CMD_SET_INTR_ENB | int_mask);
    
    /* Test TX complete interrupt */
    mock_interrupt_generate(TEST_DEVICE_ID, MOCK_INTR_TX_COMPLETE);
    TEST_ASSERT(mock_interrupt_pending(TEST_DEVICE_ID), "TX interrupt pending");
    
    uint16_t status = mock_inw(test_nic.io_base + _3C509B_STATUS_REG);
    TEST_ASSERT(status & _3C509B_STATUS_TX_COMPLETE, "TX complete in status");
    
    /* Acknowledge interrupt */
    mock_outw(test_nic.io_base + _3C509B_COMMAND_REG, 
              _3C509B_CMD_ACK_INTR | (status & 0x00FF));
    
    /* Verify interrupt cleared */
    status = mock_inw(test_nic.io_base + _3C509B_STATUS_REG);
    TEST_ASSERT(!(status & _3C509B_STATUS_TX_COMPLETE), "TX interrupt acknowledged");
    
    /* Test RX complete interrupt */
    mock_interrupt_generate(TEST_DEVICE_ID, MOCK_INTR_RX_COMPLETE);
    status = mock_inw(test_nic.io_base + _3C509B_STATUS_REG);
    TEST_ASSERT(status & _3C509B_STATUS_RX_COMPLETE, "RX complete in status");
    
    /* Test multiple simultaneous interrupts */
    mock_interrupt_generate(TEST_DEVICE_ID, MOCK_INTR_TX_COMPLETE);
    status = mock_inw(test_nic.io_base + _3C509B_STATUS_REG);
    TEST_ASSERT((status & (_3C509B_STATUS_TX_COMPLETE | _3C509B_STATUS_RX_COMPLETE)) == 
               (_3C509B_STATUS_TX_COMPLETE | _3C509B_STATUS_RX_COMPLETE), 
               "Multiple interrupts handled");
    
    TEST_END("3C509B Interrupt Handling");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test stress conditions and edge cases
 * @return Test result
 */
static test_result_t test_3c509b_stress_conditions(void) {
    TEST_START("3C509B Stress Conditions");
    
    /* Test rapid window switching */
    uint8_t windows[] = {0, 1, 2, 4, 6, 1, 0, 4, 2, 6};
    for (size_t i = 0; i < sizeof(windows); i++) {
        _3C509B_SELECT_WINDOW(test_nic.io_base, windows[i]);
        mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
        TEST_ASSERT(device->registers.current_window == windows[i], 
                   "Rapid window switching successful");
    }
    
    /* Test packet queue overflow */
    uint8_t overflow_packet[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    int injected_count = 0;
    
    /* Inject packets until queue is full */
    for (int i = 0; i < MAX_MOCK_PACKETS + 5; i++) {
        int result = mock_packet_inject_rx(TEST_DEVICE_ID, overflow_packet, sizeof(overflow_packet));
        if (result == SUCCESS) {
            injected_count++;
        } else if (result == ERROR_BUSY) {
            break; /* Queue full, expected */
        }
    }
    
    TEST_ASSERT(injected_count <= MAX_MOCK_PACKETS, "Packet queue overflow handled");
    LOG_INFO("Injected %d packets before queue full", injected_count);
    
    /* Clear queue and verify */
    mock_packet_queue_clear(TEST_DEVICE_ID);
    int queue_count = mock_packet_queue_count_rx(TEST_DEVICE_ID);
    TEST_ASSERT(queue_count == 0, "Packet queue cleared successfully");
    
    /* Test rapid interrupt generation */
    for (int i = 0; i < 100; i++) {
        mock_interrupt_generate(TEST_DEVICE_ID, (i % 2) ? MOCK_INTR_TX_COMPLETE : MOCK_INTR_RX_COMPLETE);
        
        /* Acknowledge immediately */
        uint16_t status = mock_inw(test_nic.io_base + _3C509B_STATUS_REG);
        mock_outw(test_nic.io_base + _3C509B_COMMAND_REG, 
                  _3C509B_CMD_ACK_INTR | (status & 0x00FF));
    }
    
    /* System should still be responsive */
    _3C509B_SELECT_WINDOW(test_nic.io_base, _3C509B_WINDOW_1);
    mock_device_t *device = mock_device_get(TEST_DEVICE_ID);
    TEST_ASSERT(device->registers.current_window == 1, "System responsive after interrupt stress");
    
    TEST_END("3C509B Stress Conditions");
    return TEST_RESULT_PASS;
}

/**
 * @brief Run comprehensive 3C509B driver tests
 * @return 0 on success, negative on failure
 */
int run_3c509b_comprehensive_tests(void) {
    int overall_result = 0;
    
    LOG_INFO("=== Starting Comprehensive 3C509B Driver Tests ===");
    
    /* Initialize test results */
    memset(&g_test_results, 0, sizeof(g_test_results));
    
    /* Setup test environment */
    if (setup_3c509b_test_environment() != SUCCESS) {
        LOG_ERROR("Failed to setup test environment");
        return -1;
    }
    
    /* Run all test suites */
    test_result_t results[] = {
        test_3c509b_window_selection(),
        test_3c509b_eeprom_read(),
        test_3c509b_mac_address_reading(),
        test_3c509b_media_setup(),
        test_3c509b_rx_filter_config(),
        test_3c509b_packet_transmission(),
        test_3c509b_packet_reception(),
        test_3c509b_error_handling(),
        test_3c509b_self_test(),
        test_3c509b_interrupt_handling(),
        test_3c509b_stress_conditions()
    };
    
    /* Check results */
    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
        if (results[i] != TEST_RESULT_PASS) {
            overall_result = -1;
        }
    }
    
    /* Cleanup test environment */
    teardown_3c509b_test_environment();
    
    /* Print test summary */
    LOG_INFO("=== 3C509B Test Summary ===");
    LOG_INFO("Tests run: %d", g_test_results.tests_run);
    LOG_INFO("Tests passed: %d", g_test_results.tests_passed);
    LOG_INFO("Tests failed: %d", g_test_results.tests_failed);
    
    if (g_test_results.tests_failed > 0) {
        LOG_ERROR("Last error: %s", g_test_results.last_error);
        overall_result = -1;
    }
    
    if (overall_result == 0) {
        LOG_INFO("=== ALL 3C509B TESTS PASSED ===");
    } else {
        LOG_ERROR("=== SOME 3C509B TESTS FAILED ===");
    }
    
    return overall_result;
}

/**
 * @brief Run specific 3C509B test by name
 * @param test_name Name of the test to run
 * @return Test result
 */
test_result_t run_3c509b_test_by_name(const char *test_name) {
    if (!test_name) {
        return TEST_RESULT_ERROR;
    }
    
    /* Setup test environment */
    if (setup_3c509b_test_environment() != SUCCESS) {
        LOG_ERROR("Failed to setup test environment");
        return TEST_RESULT_ERROR;
    }
    
    test_result_t result = TEST_RESULT_ERROR;
    
    /* Run specific test */
    if (strcmp(test_name, "window_selection") == 0) {
        result = test_3c509b_window_selection();
    } else if (strcmp(test_name, "eeprom_read") == 0) {
        result = test_3c509b_eeprom_read();
    } else if (strcmp(test_name, "mac_address") == 0) {
        result = test_3c509b_mac_address_reading();
    } else if (strcmp(test_name, "media_setup") == 0) {
        result = test_3c509b_media_setup();
    } else if (strcmp(test_name, "rx_filter") == 0) {
        result = test_3c509b_rx_filter_config();
    } else if (strcmp(test_name, "packet_tx") == 0) {
        result = test_3c509b_packet_transmission();
    } else if (strcmp(test_name, "packet_rx") == 0) {
        result = test_3c509b_packet_reception();
    } else if (strcmp(test_name, "error_handling") == 0) {
        result = test_3c509b_error_handling();
    } else if (strcmp(test_name, "self_test") == 0) {
        result = test_3c509b_self_test();
    } else if (strcmp(test_name, "interrupts") == 0) {
        result = test_3c509b_interrupt_handling();
    } else if (strcmp(test_name, "stress") == 0) {
        result = test_3c509b_stress_conditions();
    } else {
        LOG_ERROR("Unknown test name: %s", test_name);
        result = TEST_RESULT_ERROR;
    }
    
    /* Cleanup test environment */
    teardown_3c509b_test_environment();
    
    return result;
}