/**
 * @file helper_mock_hardware.c
 * @brief Hardware mocking implementation for testing network card drivers
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 */

#include "../../include/hardware_mock.h"
#include "../../include/logging.h"
#include <string.h>
#include <stdlib.h>

/* Global mock framework instance */
mock_framework_t g_mock_framework = {0};

/* Default MAC addresses for testing */
static const uint8_t default_mac_3c509b[6] = {0x00, 0x60, 0x8C, 0x12, 0x34, 0x56};
static const uint8_t default_mac_3c515[6] = {0x00, 0x60, 0x8C, 0x78, 0x9A, 0xBC};

/* Default EEPROM contents for 3C509B */
static const uint16_t default_eeprom_3c509b[16] = {
    0x6000, 0x8C12, 0x3456,  /* MAC address words 0-2 */
    0x0000, 0x0000, 0x0000,  /* Reserved */
    0x6D50, 0x0000,          /* Product ID and version */
    0x0000, 0x0000, 0x0000,  /* Configuration */
    0x0000, 0x0000, 0x0000,  /* More configuration */
    0x0000, 0x0000           /* Checksum and padding */
};

/* Default EEPROM contents for 3C515 */
static const uint16_t default_eeprom_3c515[16] = {
    0x6000, 0x8C78, 0x9ABC,  /* MAC address words 0-2 */
    0x0000, 0x0000, 0x0000,  /* Reserved */
    0x5051, 0x0000,          /* Product ID and version */
    0x0000, 0x0000, 0x0000,  /* Configuration */
    0x0000, 0x0000, 0x0000,  /* More configuration */
    0x0000, 0x0000           /* Checksum and padding */
};

/* Forward declarations */
static mock_device_t* find_device_by_port(uint16_t port);
static void log_io_operation(mock_io_operation_t op, uint16_t port, uint32_t value, uint8_t device_id);
static uint16_t simulate_register_read(mock_device_t *device, uint16_t reg);
static void simulate_register_write(mock_device_t *device, uint16_t reg, uint16_t value);
static void simulate_command_execution(mock_device_t *device, uint16_t command);

/* Framework initialization and cleanup */
int mock_framework_init(void) {
    memset(&g_mock_framework, 0, sizeof(g_mock_framework));
    g_mock_framework.logging_enabled = true;
    g_mock_framework.strict_mode = false;
    
    LOG_INFO("Hardware mock framework initialized");
    return SUCCESS;
}

void mock_framework_cleanup(void) {
    for (int i = 0; i < g_mock_framework.device_count; i++) {
        mock_device_destroy(i);
    }
    memset(&g_mock_framework, 0, sizeof(g_mock_framework));
    
    LOG_INFO("Hardware mock framework cleaned up");
}

void mock_framework_reset(void) {
    mock_framework_cleanup();
    mock_framework_init();
}

/* Device management */
int mock_device_create(mock_device_type_t type, uint16_t io_base, uint8_t irq) {
    if (g_mock_framework.device_count >= MAX_MOCK_DEVICES) {
        LOG_ERROR("Maximum number of mock devices reached");
        return ERROR_NO_MEMORY;
    }
    
    uint8_t device_id = g_mock_framework.device_count++;
    mock_device_t *device = &g_mock_framework.devices[device_id];
    
    memset(device, 0, sizeof(mock_device_t));
    
    device->type = type;
    device->io_base = io_base;
    device->irq = irq;
    device->enabled = false;
    device->link_up = true;
    device->link_speed = (type == MOCK_DEVICE_3C515) ? 100 : 10;
    device->full_duplex = false;
    device->promiscuous = false;
    
    /* Initialize register state */
    device->registers.current_window = 0;
    device->registers.cmd_busy = false;
    
    /* Set default MAC address and initialize EEPROM */
    switch (type) {
        case MOCK_DEVICE_3C509B:
            memcpy(device->mac_address, default_mac_3c509b, 6);
            mock_eeprom_init(device_id, default_eeprom_3c509b, 16);
            break;
            
        case MOCK_DEVICE_3C515:
            memcpy(device->mac_address, default_mac_3c515, 6);
            mock_eeprom_init(device_id, default_eeprom_3c515, 16);
            break;
            
        default:
            LOG_ERROR("Unknown device type: %d", type);
            return ERROR_INVALID_PARAM;
    }
    
    LOG_INFO("Created mock device %d: type=%d, io_base=0x%X, irq=%d", 
             device_id, type, io_base, irq);
    
    return device_id;
}

int mock_device_destroy(uint8_t device_id) {
    if (device_id >= g_mock_framework.device_count) {
        return ERROR_INVALID_PARAM;
    }
    
    mock_device_t *device = &g_mock_framework.devices[device_id];
    memset(device, 0, sizeof(mock_device_t));
    
    LOG_DEBUG("Destroyed mock device %d", device_id);
    return SUCCESS;
}

mock_device_t* mock_device_get(uint8_t device_id) {
    if (device_id >= g_mock_framework.device_count) {
        return NULL;
    }
    return &g_mock_framework.devices[device_id];
}

mock_device_t* mock_device_find_by_io(uint16_t io_base) {
    for (int i = 0; i < g_mock_framework.device_count; i++) {
        if (g_mock_framework.devices[i].io_base == io_base) {
            return &g_mock_framework.devices[i];
        }
    }
    return NULL;
}

/* Device configuration */
int mock_device_set_mac_address(uint8_t device_id, const uint8_t *mac) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device || !mac) {
        return ERROR_INVALID_PARAM;
    }
    
    memcpy(device->mac_address, mac, 6);
    
    /* Update EEPROM with new MAC address */
    device->eeprom.data[0] = (mac[1] << 8) | mac[0];
    device->eeprom.data[1] = (mac[3] << 8) | mac[2];
    device->eeprom.data[2] = (mac[5] << 8) | mac[4];
    
    LOG_DEBUG("Set MAC address for device %d: %02X:%02X:%02X:%02X:%02X:%02X",
              device_id, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return SUCCESS;
}

int mock_device_set_link_status(uint8_t device_id, bool link_up, uint16_t speed) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device) {
        return ERROR_INVALID_PARAM;
    }
    
    bool was_up = device->link_up;
    device->link_up = link_up;
    device->link_speed = speed;
    
    /* Generate link change interrupt if status changed */
    if (was_up != link_up) {
        mock_interrupt_generate(device_id, MOCK_INTR_LINK_CHANGE);
    }
    
    LOG_DEBUG("Set link status for device %d: %s, %d Mbps", 
              device_id, link_up ? "UP" : "DOWN", speed);
    
    return SUCCESS;
}

int mock_device_set_promiscuous(uint8_t device_id, bool enable) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device) {
        return ERROR_INVALID_PARAM;
    }
    
    device->promiscuous = enable;
    
    LOG_DEBUG("Set promiscuous mode for device %d: %s", 
              device_id, enable ? "enabled" : "disabled");
    
    return SUCCESS;
}

int mock_device_enable(uint8_t device_id, bool enable) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device) {
        return ERROR_INVALID_PARAM;
    }
    
    device->enabled = enable;
    
    LOG_DEBUG("Device %d %s", device_id, enable ? "enabled" : "disabled");
    
    return SUCCESS;
}

/* EEPROM simulation */
int mock_eeprom_init(uint8_t device_id, const uint16_t *initial_data, size_t size) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device || !initial_data || size > MAX_EEPROM_SIZE) {
        return ERROR_INVALID_PARAM;
    }
    
    memset(&device->eeprom, 0, sizeof(mock_eeprom_t));
    memcpy(device->eeprom.data, initial_data, size * sizeof(uint16_t));
    device->eeprom.read_delay_us = (device->type == MOCK_DEVICE_3C509B) ? 
                                   _3C509B_EEPROM_READ_DELAY : _3C515_TX_EEPROM_READ_DELAY;
    
    LOG_DEBUG("Initialized EEPROM for device %d with %zu words", device_id, size);
    
    return SUCCESS;
}

uint16_t mock_eeprom_read(uint8_t device_id, uint8_t address) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device || address >= MAX_EEPROM_SIZE) {
        return 0xFFFF;
    }
    
    device->eeprom.last_address = address;
    
    /* Simulate read delay */
    if (device->eeprom.read_delay_us > 0) {
        /* In real implementation, this would be udelay() */
        g_mock_framework.global_timestamp += device->eeprom.read_delay_us;
    }
    
    uint16_t value = device->eeprom.data[address];
    
    LOG_TRACE("EEPROM read device %d, addr 0x%02X -> 0x%04X", device_id, address, value);
    
    return value;
}

int mock_eeprom_write(uint8_t device_id, uint8_t address, uint16_t data) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device || address >= MAX_EEPROM_SIZE) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!device->eeprom.write_enabled) {
        LOG_WARNING("EEPROM write to device %d blocked - write not enabled", device_id);
        return ERROR_ACCESS_DENIED;
    }
    
    device->eeprom.data[address] = data;
    device->eeprom.last_address = address;
    
    LOG_TRACE("EEPROM write device %d, addr 0x%02X <- 0x%04X", device_id, address, data);
    
    return SUCCESS;
}

/* Packet injection and simulation */
int mock_packet_inject_rx(uint8_t device_id, const uint8_t *packet, size_t length) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device || !packet || length == 0 || length > sizeof(mock_packet_t().data)) {
        return ERROR_INVALID_PARAM;
    }
    
    uint16_t next_tail = (device->rx_queue_tail + 1) % MAX_MOCK_PACKETS;
    if (next_tail == device->rx_queue_head) {
        LOG_WARNING("RX queue full for device %d", device_id);
        return ERROR_BUSY;
    }
    
    mock_packet_t *pkt = &device->rx_queue[device->rx_queue_tail];
    memcpy(pkt->data, packet, length);
    pkt->length = length;
    pkt->timestamp = g_mock_framework.global_timestamp++;
    pkt->status = 0;
    pkt->valid = true;
    
    device->rx_queue_tail = next_tail;
    device->rx_packets++;
    device->rx_bytes += length;
    
    /* Generate RX complete interrupt */
    mock_interrupt_generate(device_id, MOCK_INTR_RX_COMPLETE);
    
    LOG_TRACE("Injected RX packet to device %d: %zu bytes", device_id, length);
    
    return SUCCESS;
}

int mock_packet_extract_tx(uint8_t device_id, uint8_t *packet, size_t *length) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device || !packet || !length) {
        return ERROR_INVALID_PARAM;
    }
    
    if (device->tx_queue_head == device->tx_queue_tail) {
        *length = 0;
        return ERROR_NO_DATA;
    }
    
    mock_packet_t *pkt = &device->tx_queue[device->tx_queue_head];
    if (!pkt->valid) {
        device->tx_queue_head = (device->tx_queue_head + 1) % MAX_MOCK_PACKETS;
        return ERROR_IO;
    }
    
    if (*length < pkt->length) {
        *length = pkt->length;
        return ERROR_NO_MEMORY;
    }
    
    memcpy(packet, pkt->data, pkt->length);
    *length = pkt->length;
    
    pkt->valid = false;
    device->tx_queue_head = (device->tx_queue_head + 1) % MAX_MOCK_PACKETS;
    
    LOG_TRACE("Extracted TX packet from device %d: %zu bytes", device_id, *length);
    
    return SUCCESS;
}

int mock_packet_queue_count_rx(uint8_t device_id) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device) {
        return -1;
    }
    
    if (device->rx_queue_tail >= device->rx_queue_head) {
        return device->rx_queue_tail - device->rx_queue_head;
    } else {
        return MAX_MOCK_PACKETS - device->rx_queue_head + device->rx_queue_tail;
    }
}

int mock_packet_queue_count_tx(uint8_t device_id) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device) {
        return -1;
    }
    
    if (device->tx_queue_tail >= device->tx_queue_head) {
        return device->tx_queue_tail - device->tx_queue_head;
    } else {
        return MAX_MOCK_PACKETS - device->tx_queue_head + device->tx_queue_tail;
    }
}

void mock_packet_queue_clear(uint8_t device_id) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device) {
        return;
    }
    
    device->rx_queue_head = device->rx_queue_tail = 0;
    device->tx_queue_head = device->tx_queue_tail = 0;
    
    memset(device->rx_queue, 0, sizeof(device->rx_queue));
    memset(device->tx_queue, 0, sizeof(device->tx_queue));
    
    LOG_DEBUG("Cleared packet queues for device %d", device_id);
}

/* Error injection */
int mock_error_inject(uint8_t device_id, mock_error_type_t error, uint32_t trigger_count) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device) {
        return ERROR_INVALID_PARAM;
    }
    
    device->injected_error = error;
    device->error_trigger_count = trigger_count;
    device->operation_count = 0;
    
    LOG_DEBUG("Injected error %d for device %d, trigger at operation %d", 
              error, device_id, trigger_count);
    
    return SUCCESS;
}

void mock_error_clear(uint8_t device_id) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device) {
        return;
    }
    
    device->injected_error = MOCK_ERROR_NONE;
    device->error_trigger_count = 0;
    device->operation_count = 0;
    
    LOG_DEBUG("Cleared error injection for device %d", device_id);
}

/* Interrupt simulation */
int mock_interrupt_generate(uint8_t device_id, mock_interrupt_type_t type) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Update status register based on interrupt type */
    switch (type) {
        case MOCK_INTR_TX_COMPLETE:
            if (device->type == MOCK_DEVICE_3C509B) {
                device->registers.status_reg |= _3C509B_STATUS_TX_COMPLETE;
            } else {
                device->registers.status_reg |= _3C515_TX_STATUS_TX_COMPLETE;
            }
            break;
            
        case MOCK_INTR_RX_COMPLETE:
            if (device->type == MOCK_DEVICE_3C509B) {
                device->registers.status_reg |= _3C509B_STATUS_RX_COMPLETE;
            } else {
                device->registers.status_reg |= _3C515_TX_STATUS_RX_COMPLETE;
            }
            break;
            
        case MOCK_INTR_ADAPTER_FAILURE:
            if (device->type == MOCK_DEVICE_3C509B) {
                device->registers.status_reg |= _3C509B_STATUS_ADAPTER_FAILURE;
            } else {
                device->registers.status_reg |= _3C515_TX_STATUS_ADAPTER_FAILURE;
            }
            break;
            
        case MOCK_INTR_DMA_COMPLETE:
            if (device->type == MOCK_DEVICE_3C515) {
                device->registers.status_reg |= _3C515_TX_STATUS_DMA_DONE;
            }
            break;
            
        default:
            break;
    }
    
    device->registers.status_reg |= (device->type == MOCK_DEVICE_3C509B) ? 
                                   _3C509B_STATUS_INT_LATCH : _3C515_TX_STATUS_INT_LATCH;
    device->interrupts_generated++;
    
    LOG_TRACE("Generated interrupt %d for device %d", type, device_id);
    
    return SUCCESS;
}

bool mock_interrupt_pending(uint8_t device_id) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device) {
        return false;
    }
    
    uint16_t int_latch = (device->type == MOCK_DEVICE_3C509B) ? 
                        _3C509B_STATUS_INT_LATCH : _3C515_TX_STATUS_INT_LATCH;
    
    return (device->registers.status_reg & int_latch) != 0;
}

void mock_interrupt_clear(uint8_t device_id) {
    mock_device_t *device = mock_device_get(device_id);
    if (!device) {
        return;
    }
    
    device->registers.status_reg = 0;
    
    LOG_TRACE("Cleared interrupts for device %d", device_id);
}

/* I/O operation mocking */
static mock_device_t* find_device_by_port(uint16_t port) {
    for (int i = 0; i < g_mock_framework.device_count; i++) {
        mock_device_t *device = &g_mock_framework.devices[i];
        uint16_t extent = (device->type == MOCK_DEVICE_3C515) ? 
                         _3C515_TX_IO_EXTENT : _3C509B_IO_EXTENT;
        
        if (port >= device->io_base && port < (device->io_base + extent)) {
            return device;
        }
    }
    return NULL;
}

static void log_io_operation(mock_io_operation_t op, uint16_t port, uint32_t value, uint8_t device_id) {
    if (!g_mock_framework.logging_enabled) {
        return;
    }
    
    mock_io_log_entry_t *entry = &g_mock_framework.io_log[g_mock_framework.io_log_head];
    entry->operation = op;
    entry->port = port;
    entry->value = value;
    entry->timestamp = g_mock_framework.global_timestamp++;
    entry->device_id = device_id;
    
    g_mock_framework.io_log_head = (g_mock_framework.io_log_head + 1) % 1024;
}

uint8_t mock_inb(uint16_t port) {
    mock_device_t *device = find_device_by_port(port);
    if (!device) {
        if (g_mock_framework.strict_mode) {
            LOG_ERROR("I/O read from unmapped port 0x%04X", port);
        }
        return 0xFF;
    }
    
    uint16_t reg = port - device->io_base;
    uint16_t value = simulate_register_read(device, reg);
    uint8_t result = (uint8_t)(value & 0xFF);
    
    log_io_operation(MOCK_IO_READ_BYTE, port, result, device - g_mock_framework.devices);
    
    return result;
}

uint16_t mock_inw(uint16_t port) {
    mock_device_t *device = find_device_by_port(port);
    if (!device) {
        if (g_mock_framework.strict_mode) {
            LOG_ERROR("I/O read from unmapped port 0x%04X", port);
        }
        return 0xFFFF;
    }
    
    uint16_t reg = port - device->io_base;
    uint16_t result = simulate_register_read(device, reg);
    
    log_io_operation(MOCK_IO_READ_WORD, port, result, device - g_mock_framework.devices);
    
    return result;
}

uint32_t mock_inl(uint16_t port) {
    mock_device_t *device = find_device_by_port(port);
    if (!device) {
        if (g_mock_framework.strict_mode) {
            LOG_ERROR("I/O read from unmapped port 0x%04X", port);
        }
        return 0xFFFFFFFF;
    }
    
    uint16_t reg = port - device->io_base;
    uint32_t result = simulate_register_read(device, reg) | 
                     (simulate_register_read(device, reg + 2) << 16);
    
    log_io_operation(MOCK_IO_READ_DWORD, port, result, device - g_mock_framework.devices);
    
    return result;
}

void mock_outb(uint16_t port, uint8_t value) {
    mock_device_t *device = find_device_by_port(port);
    if (!device) {
        if (g_mock_framework.strict_mode) {
            LOG_ERROR("I/O write to unmapped port 0x%04X", port);
        }
        return;
    }
    
    uint16_t reg = port - device->io_base;
    simulate_register_write(device, reg, value);
    
    log_io_operation(MOCK_IO_WRITE_BYTE, port, value, device - g_mock_framework.devices);
}

void mock_outw(uint16_t port, uint16_t value) {
    mock_device_t *device = find_device_by_port(port);
    if (!device) {
        if (g_mock_framework.strict_mode) {
            LOG_ERROR("I/O write to unmapped port 0x%04X", port);
        }
        return;
    }
    
    uint16_t reg = port - device->io_base;
    
    /* Handle command register specially */
    if (reg == _3C509B_COMMAND_REG || reg == _3C515_TX_COMMAND_REG) {
        simulate_command_execution(device, value);
    } else {
        simulate_register_write(device, reg, value);
    }
    
    log_io_operation(MOCK_IO_WRITE_WORD, port, value, device - g_mock_framework.devices);
}

void mock_outl(uint16_t port, uint32_t value) {
    mock_device_t *device = find_device_by_port(port);
    if (!device) {
        if (g_mock_framework.strict_mode) {
            LOG_ERROR("I/O write to unmapped port 0x%04X", port);
        }
        return;
    }
    
    uint16_t reg = port - device->io_base;
    simulate_register_write(device, reg, value & 0xFFFF);
    simulate_register_write(device, reg + 2, (value >> 16) & 0xFFFF);
    
    log_io_operation(MOCK_IO_WRITE_DWORD, port, value, device - g_mock_framework.devices);
}

/* Register simulation helpers */
static uint16_t simulate_register_read(mock_device_t *device, uint16_t reg) {
    device->operation_count++;
    
    /* Check for error injection */
    if (device->injected_error != MOCK_ERROR_NONE && 
        device->operation_count >= device->error_trigger_count) {
        
        switch (device->injected_error) {
            case MOCK_ERROR_ADAPTER_FAILURE:
                mock_interrupt_generate(device - g_mock_framework.devices, MOCK_INTR_ADAPTER_FAILURE);
                break;
            default:
                break;
        }
    }
    
    /* Handle status register specially */
    if (reg == _3C509B_STATUS_REG || reg == _3C515_TX_STATUS_REG) {
        return device->registers.status_reg;
    }
    
    /* Handle window-specific registers */
    switch (device->registers.current_window) {
        case 0: /* EEPROM access */
            if (device->type == MOCK_DEVICE_3C509B && reg == _3C509B_EEPROM_DATA) {
                return mock_eeprom_read(device - g_mock_framework.devices, device->eeprom.last_address);
            }
            break;
            
        case 1: /* Normal operation */
            if (reg == (device->type == MOCK_DEVICE_3C509B ? _3C509B_RX_STATUS : _3C515_TX_RX_STATUS)) {
                /* Simulate RX status */
                if (mock_packet_queue_count_rx(device - g_mock_framework.devices) > 0) {
                    mock_packet_t *pkt = &device->rx_queue[device->rx_queue_head];
                    return pkt->length | (pkt->status << 11);
                }
                return 0x8000; /* No packet available */
            }
            break;
            
        default:
            break;
    }
    
    /* Return default register value */
    return device->registers.registers[reg % 32];
}

static void simulate_register_write(mock_device_t *device, uint16_t reg, uint16_t value) {
    device->operation_count++;
    
    /* Handle EEPROM command register */
    if (device->registers.current_window == 0) {
        if ((device->type == MOCK_DEVICE_3C509B && reg == _3C509B_EEPROM_CMD) ||
            (device->type == MOCK_DEVICE_3C515 && reg == _3C515_TX_W0_EEPROM_CMD)) {
            
            uint8_t command = (value >> 6) & 0x03;
            uint8_t address = value & 0x3F;
            
            if (command == 2) { /* Read command */
                device->eeprom.last_address = address;
            }
        }
    }
    
    /* Store register value */
    device->registers.registers[reg % 32] = value;
}

static void simulate_command_execution(mock_device_t *device, uint16_t command) {
    device->registers.command_reg = command;
    device->registers.cmd_busy = true;
    
    uint16_t cmd = command >> 11;
    uint16_t param = command & 0x7FF;
    
    switch (cmd) {
        case 0: /* Total reset */
            memset(&device->registers, 0, sizeof(device->registers));
            mock_packet_queue_clear(device - g_mock_framework.devices);
            break;
            
        case 1: /* Select window */
            device->registers.current_window = param & 0x07;
            break;
            
        case 4: /* RX enable */
            device->enabled = true;
            break;
            
        case 3: /* RX disable */
            device->enabled = false;
            break;
            
        case 9: /* TX enable */
            device->enabled = true;
            break;
            
        case 10: /* TX disable */
            device->enabled = false;
            break;
            
        case 13: /* Acknowledge interrupt */
            device->registers.status_reg &= ~(param & 0xFF);
            break;
            
        case 16: /* Set RX filter */
            device->promiscuous = (param & 0x08) != 0;
            break;
            
        default:
            LOG_TRACE("Unhandled command %d for device", cmd);
            break;
    }
    
    /* Clear busy flag after a short delay */
    device->registers.cmd_busy = false;
}

/* Statistics */
int mock_get_statistics(mock_statistics_t *stats) {
    if (!stats) {
        return ERROR_INVALID_PARAM;
    }
    
    memset(stats, 0, sizeof(mock_statistics_t));
    
    for (int i = 0; i < g_mock_framework.device_count; i++) {
        mock_device_t *device = &g_mock_framework.devices[i];
        stats->packets_injected += device->rx_packets;
        stats->packets_extracted += device->tx_packets;
        stats->interrupts_generated += device->interrupts_generated;
    }
    
    return SUCCESS;
}

/* I/O logging */
void mock_io_log_enable(bool enable) {
    g_mock_framework.logging_enabled = enable;
}

bool mock_io_log_is_enabled(void) {
    return g_mock_framework.logging_enabled;
}

void mock_io_log_clear(void) {
    g_mock_framework.io_log_head = 0;
    memset(g_mock_framework.io_log, 0, sizeof(g_mock_framework.io_log));
}