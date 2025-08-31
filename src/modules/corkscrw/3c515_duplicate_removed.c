/**
 * @file 3c515.c
 * @brief 3Com 3C515 ISA Bus-Master Hardware Driver
 * 
 * Agent Team B (07-08): Week 1 Implementation
 * 
 * This module implements the low-level hardware programming for the 3Com 3C515
 * "Corkscrew" and "Tornado" Fast Ethernet ISA cards with bus mastering DMA.
 * 
 * Key Features:
 * - ISA bus-master DMA with descriptor rings
 * - Window-based register access
 * - Hardware auto-negotiation support
 * - Interrupt-driven packet processing
 * - DMA boundary safety checks
 * 
 * Hardware Specifications:
 * - 3C515-TX: 100 Mbps Fast Ethernet
 * - ISA bus with bus mastering capability
 * - Window-based register architecture
 * - DMA descriptor ring buffers
 * - Auto-negotiation and link detection
 * 
 * This file is part of the CORKSCRW.MOD module.
 * Copyright (c) 2025 3Com/Phase3A Team B
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Hardware Constants */
#define VENDOR_3COM             0x10B7
#define DEVICE_3C515            0x5150
#define DEVICE_3C515_TORNADO    0x5057

/* I/O Port Configuration */
#define ISA_IO_PORTS_MIN        0x200
#define ISA_IO_PORTS_MAX        0x3F0
#define ISA_IO_PORT_STEP        0x10
#define IO_REGION_SIZE          0x10

/* Register Offsets (relative to I/O base) */
#define REG_COMMAND             0x0E
#define REG_STATUS              0x0E
#define REG_INT_STATUS          0x0E
#define REG_FIFO_DIAG           0x04
#define REG_TIMER               0x0A
#define REG_TX_STATUS           0x0B

/* Window-Specific Registers */
/* Window 0: Setup/Configuration */
#define WIN0_EEPROM_DATA        0x0C
#define WIN0_EEPROM_CMD         0x0A
#define WIN0_CONFIG_CTRL        0x04
#define WIN0_MEDIA_OPTIONS      0x08

/* Window 1: Operating Registers */
#define WIN1_TX_FIFO            0x00
#define WIN1_RX_FIFO            0x00
#define WIN1_RX_STATUS          0x08
#define WIN1_TX_STATUS          0x0B
#define WIN1_TX_FREE            0x0C

/* Window 2: Station Address */
#define WIN2_STATION_ADDR_0     0x00
#define WIN2_STATION_ADDR_1     0x02
#define WIN2_STATION_ADDR_2     0x04
#define WIN2_STATION_MASK_0     0x06
#define WIN2_STATION_MASK_1     0x08
#define WIN2_STATION_MASK_2     0x0A

/* Window 3: FIFO Management */
#define WIN3_TX_FREE            0x0C
#define WIN3_TX_STATUS          0x0B
#define WIN3_RX_BYTES           0x0A
#define WIN3_RX_STATUS          0x08

/* Window 4: Diagnostics */
#define WIN4_MEDIA_STATUS       0x0A
#define WIN4_BAD_SSD            0x0C
#define WIN4_UPPER_BYTES_OK     0x0D

/* Window 5: Read Results/Statistics */
#define WIN5_TX_START_THRESH    0x00
#define WIN5_TX_AVAIL_THRESH    0x02
#define WIN5_RX_EARLY_THRESH    0x06
#define WIN5_RX_FILTER          0x08
#define WIN5_INT_MASK           0x0A
#define WIN5_READ_ZERO_MASK     0x0C

/* Window 6: Statistics */
#define WIN6_CARRIER_LOST       0x00
#define WIN6_SQE_ERRORS         0x01
#define WIN6_MULTIPLE_COLLS     0x02
#define WIN6_SINGLE_COLLS       0x03
#define WIN6_LATE_COLLS         0x04
#define WIN6_RX_OVERRUNS        0x05
#define WIN6_FRAMES_OK          0x06
#define WIN6_FRAMES_DEFERRED    0x08
#define WIN6_BYTES_OK           0x0A
#define WIN6_BYTES_RX_OK        0x0C

/* Window 7: Bus Master Control */
#define WIN7_MASTER_ADDR        0x00
#define WIN7_MASTER_LEN         0x06
#define WIN7_MASTER_STATUS      0x0C
#define WIN7_FRAG_ADDR          0x00
#define WIN7_FRAG_LEN           0x04
#define WIN7_UP_LIST_PTR        0x38
#define WIN7_UP_PKT_STATUS      0x30
#define WIN7_DN_LIST_PTR        0x24
#define WIN7_DN_POLL            0x2D
#define WIN7_DMA_CTRL           0x20

/* Commands */
#define CMD_GLOBAL_RESET        0x0000
#define CMD_SELECT_WINDOW       0x0800
#define CMD_TX_ENABLE           0x4800
#define CMD_TX_DISABLE          0x5000
#define CMD_RX_ENABLE           0x2000
#define CMD_RX_DISABLE          0x1800
#define CMD_RX_DISCARD          0x4000
#define CMD_TX_RESET            0x5800
#define CMD_RX_RESET            0x2800
#define CMD_UP_STALL            0x3000
#define CMD_UP_UNSTALL          0x3001
#define CMD_DN_STALL            0x3002
#define CMD_DN_UNSTALL          0x3003
#define CMD_SET_RX_FILTER       0x8000
#define CMD_SET_RX_THRESH       0xB800
#define CMD_SET_TX_THRESH       0x9800
#define CMD_SET_TX_START        0x9800
#define CMD_STATS_ENABLE        0xA800
#define CMD_STATS_DISABLE       0xB000
#define CMD_STOP_COAX           0xB400
#define CMD_START_COAX          0xB800
#define CMD_REQ_INT             0xC000
#define CMD_ACK_INT             0x6800

/* Window Numbers */
#define WINDOW_SETUP            0
#define WINDOW_OPERATING        1
#define WINDOW_STATION_ADDR     2
#define WINDOW_FIFO             3
#define WINDOW_DIAGNOSTICS      4
#define WINDOW_RESULTS          5
#define WINDOW_STATISTICS       6
#define WINDOW_BUS_MASTER       7

/* Status Register Bits */
#define STATUS_INT_LATCH        0x0001
#define STATUS_ADAPTER_FAIL     0x0002
#define STATUS_TX_COMPLETE      0x0004
#define STATUS_TX_AVAIL         0x0008
#define STATUS_RX_COMPLETE      0x0010
#define STATUS_RX_EARLY         0x0020
#define STATUS_INT_REQ          0x0040
#define STATUS_UPDATE_STATS     0x0080
#define STATUS_CMD_IN_PROGRESS  0x1000

/* RX Filter Bits */
#define RX_FILTER_INDIVIDUAL    0x0001
#define RX_FILTER_MULTICAST     0x0002
#define RX_FILTER_BROADCAST     0x0004
#define RX_FILTER_PROMISCUOUS   0x0008

/* Media Types */
#define MEDIA_10BASE_T          0x00
#define MEDIA_AUI               0x01
#define MEDIA_10BASE_2          0x03
#define MEDIA_100BASE_TX        0x06
#define MEDIA_100BASE_FX        0x07
#define MEDIA_MII               0x08
#define MEDIA_AUTO_SELECT       0x0F

/* DMA Control Bits */
#define DMA_CTRL_UP_COMPLETE    0x00000001
#define DMA_CTRL_DN_COMPLETE    0x00000002
#define DMA_CTRL_UP_POLL        0x00000004
#define DMA_CTRL_ARM_COUNTDOWN  0x00000008
#define DMA_CTRL_COUNTDOWN_SPEED 0x00000010
#define DMA_CTRL_COUNTDOWN_MODE 0x00000020
#define DMA_CTRL_DEFEAT_MWI     0x00000040
#define DMA_CTRL_DEFEAT_MRL     0x00000080
#define DMA_CTRL_UP_OVERFLOW    0x00000100
#define DMA_CTRL_TARGET_ABORT   0x40000000
#define DMA_CTRL_MASTER_ABORT   0x80000000

/* Hardware Context */
typedef struct {
    uint16_t io_base;           /* I/O base address */
    uint8_t irq;                /* IRQ number */
    uint16_t device_id;         /* Device ID (3C515 variant) */
    uint8_t current_window;     /* Currently selected window */
    uint8_t mac_addr[6];        /* Station address */
    uint8_t media_type;         /* Current media type */
    bool link_active;           /* Link status */
    bool bus_master_enabled;    /* Bus mastering status */
    uint32_t capabilities;      /* Hardware capabilities */
} hw_3c515_t;

/* Global hardware context */
static hw_3c515_t g_hw_ctx;

/* Forward Declarations */
static void hw_select_window(uint8_t window);
static void hw_outb(uint8_t reg, uint8_t value);
static void hw_outw(uint8_t reg, uint16_t value);
static void hw_outl(uint8_t reg, uint32_t value);
static uint8_t hw_inb(uint8_t reg);
static uint16_t hw_inw(uint8_t reg);
static uint32_t hw_inl(uint8_t reg);
static bool hw_wait_command_complete(uint32_t timeout_ms);
static int hw_eeprom_read(uint8_t offset);
static void hw_reset_adapter(void);
static int hw_detect_media(void);
static int hw_configure_media(uint8_t media_type);
static bool hw_check_link_status(void);

/**
 * ============================================================================
 * HARDWARE DETECTION AND INITIALIZATION
 * ============================================================================
 */

/**
 * @brief Detect 3C515 hardware on ISA bus
 * 
 * @param io_base Pointer to store detected I/O base address
 * @param irq Pointer to store detected IRQ
 * @return 0 if found, -1 if not found
 */
int hw_3c515_detect(uint16_t *io_base, uint8_t *irq)
{
    if (!io_base || !irq) {
        return -1;
    }
    
    /* Scan ISA I/O address space for 3C515 */
    for (uint16_t addr = ISA_IO_PORTS_MIN; addr <= ISA_IO_PORTS_MAX; addr += ISA_IO_PORT_STEP) {
        g_hw_ctx.io_base = addr;
        
        /* Try to reset the adapter */
        hw_outw(REG_COMMAND, CMD_GLOBAL_RESET);
        
        /* Wait for reset to complete */
        if (!hw_wait_command_complete(1000)) {
            continue;  /* Timeout, try next address */
        }
        
        /* Select setup window and read device ID */
        hw_select_window(WINDOW_SETUP);
        
        /* Read manufacturer and device ID from EEPROM */
        int vendor_id = hw_eeprom_read(0x00);
        int device_id = hw_eeprom_read(0x01);
        
        if (vendor_id == VENDOR_3COM && 
            (device_id == DEVICE_3C515 || device_id == DEVICE_3C515_TORNADO)) {
            
            /* Found 3C515! */
            *io_base = addr;
            g_hw_ctx.device_id = (uint16_t)device_id;
            
            /* Detect IRQ - typically from EEPROM or configuration */
            *irq = 11;  /* Default IRQ, would read from EEPROM */
            g_hw_ctx.irq = *irq;
            
            return 0;  /* Success */
        }
    }
    
    return -1;  /* Not found */
}

/**
 * @brief Initialize 3C515 hardware
 * 
 * @param io_base I/O base address
 * @param irq IRQ number
 * @return 0 on success, negative on error
 */
int hw_3c515_init(uint16_t io_base, uint8_t irq)
{
    /* Initialize hardware context */
    memset(&g_hw_ctx, 0, sizeof(hw_3c515_t));
    g_hw_ctx.io_base = io_base;
    g_hw_ctx.irq = irq;
    g_hw_ctx.current_window = 0xFF;  /* Force window selection */
    
    /* Reset the adapter */
    hw_reset_adapter();
    
    /* Read station address from EEPROM */
    for (int i = 0; i < 3; i++) {
        int word = hw_eeprom_read(0x0A + i);
        if (word < 0) {
            return -1;  /* EEPROM read failed */
        }
        g_hw_ctx.mac_addr[i * 2] = (uint8_t)(word & 0xFF);
        g_hw_ctx.mac_addr[i * 2 + 1] = (uint8_t)(word >> 8);
    }
    
    /* Program station address */
    hw_select_window(WINDOW_STATION_ADDR);
    for (int i = 0; i < 6; i += 2) {
        uint16_t addr_word = g_hw_ctx.mac_addr[i] | 
                            (g_hw_ctx.mac_addr[i + 1] << 8);
        hw_outw(WIN2_STATION_ADDR_0 + i, addr_word);
    }
    
    /* Detect and configure media */
    int media = hw_detect_media();
    if (media < 0) {
        return -1;
    }
    
    if (hw_configure_media((uint8_t)media) < 0) {
        return -1;
    }
    
    /* Enable bus mastering */
    hw_select_window(WINDOW_BUS_MASTER);
    g_hw_ctx.bus_master_enabled = true;
    
    /* Set up interrupt mask */
    hw_select_window(WINDOW_RESULTS);
    hw_outw(WIN5_INT_MASK, STATUS_TX_COMPLETE | STATUS_RX_COMPLETE | 
                          STATUS_UPDATE_STATS | STATUS_TX_AVAIL);
    
    /* Enable statistics collection */
    hw_outw(REG_COMMAND, CMD_STATS_ENABLE);
    
    return 0;
}

/**
 * @brief Configure 3C515 for bus-master DMA operation
 * 
 * @param tx_ring_phys Physical address of TX descriptor ring
 * @param rx_ring_phys Physical address of RX descriptor ring
 * @return 0 on success, negative on error
 */
int hw_3c515_setup_dma(uint32_t tx_ring_phys, uint32_t rx_ring_phys)
{
    if (!g_hw_ctx.bus_master_enabled) {
        return -1;
    }
    
    /* Select bus master window */
    hw_select_window(WINDOW_BUS_MASTER);
    
    /* Program download (TX) list pointer */
    hw_outl(WIN7_DN_LIST_PTR, tx_ring_phys);
    
    /* Program upload (RX) list pointer */
    hw_outl(WIN7_UP_LIST_PTR, rx_ring_phys);
    
    /* Configure DMA control register */
    uint32_t dma_ctrl = DMA_CTRL_UP_COMPLETE | DMA_CTRL_DN_COMPLETE;
    hw_outl(WIN7_DMA_CTRL, dma_ctrl);
    
    return 0;
}

/**
 * @brief Enable 3C515 transmit and receive
 * 
 * @return 0 on success, negative on error
 */
int hw_3c515_enable(void)
{
    /* Unstall upload and download engines */
    hw_outw(REG_COMMAND, CMD_UP_UNSTALL);
    hw_outw(REG_COMMAND, CMD_DN_UNSTALL);
    
    /* Enable receiver */
    hw_outw(REG_COMMAND, CMD_RX_ENABLE);
    
    /* Enable transmitter */
    hw_outw(REG_COMMAND, CMD_TX_ENABLE);
    
    /* Set RX filter to accept directed, broadcast, and multicast */
    hw_outw(REG_COMMAND, CMD_SET_RX_FILTER | 
            (RX_FILTER_INDIVIDUAL | RX_FILTER_BROADCAST | RX_FILTER_MULTICAST));
    
    g_hw_ctx.link_active = hw_check_link_status();
    
    return 0;
}

/**
 * @brief Disable 3C515 transmit and receive
 */
void hw_3c515_disable(void)
{
    /* Disable transmitter */
    hw_outw(REG_COMMAND, CMD_TX_DISABLE);
    
    /* Disable receiver */
    hw_outw(REG_COMMAND, CMD_RX_DISABLE);
    
    /* Stall DMA engines */
    hw_outw(REG_COMMAND, CMD_UP_STALL);
    hw_outw(REG_COMMAND, CMD_DN_STALL);
    
    g_hw_ctx.link_active = false;
}

/**
 * @brief Start DMA transmission
 * 
 * @return 0 on success, negative on error
 */
int hw_3c515_start_tx(void)
{
    if (!g_hw_ctx.bus_master_enabled) {
        return -1;
    }
    
    /* Select bus master window */
    hw_select_window(WINDOW_BUS_MASTER);
    
    /* Trigger download poll */
    hw_outb(WIN7_DN_POLL, 1);
    
    return 0;
}

/**
 * @brief Check for received packets
 * 
 * @return true if packets available, false otherwise
 */
bool hw_3c515_rx_available(void)
{
    hw_select_window(WINDOW_BUS_MASTER);
    uint32_t status = hw_inl(WIN7_UP_PKT_STATUS);
    return (status & 0x8000) != 0;  /* Packet complete bit */
}

/**
 * @brief Get interrupt status
 * 
 * @return Interrupt status register value
 */
uint16_t hw_3c515_get_int_status(void)
{
    return hw_inw(REG_INT_STATUS);
}

/**
 * @brief Acknowledge interrupts
 * 
 * @param int_mask Interrupt mask to acknowledge
 */
void hw_3c515_ack_int(uint16_t int_mask)
{
    hw_outw(REG_COMMAND, CMD_ACK_INT | (int_mask & 0x7FF));
}

/**
 * @brief Read station address
 * 
 * @param mac_addr Buffer to store MAC address (6 bytes)
 */
void hw_3c515_get_mac_addr(uint8_t *mac_addr)
{
    if (mac_addr) {
        memcpy(mac_addr, g_hw_ctx.mac_addr, 6);
    }
}

/**
 * @brief Check link status
 * 
 * @return true if link is active, false otherwise
 */
bool hw_3c515_link_active(void)
{
    return hw_check_link_status();
}

/**
 * ============================================================================
 * LOW-LEVEL HARDWARE ACCESS
 * ============================================================================
 */

/**
 * @brief Select register window
 * 
 * @param window Window number (0-7)
 */
static void hw_select_window(uint8_t window)
{
    if (g_hw_ctx.current_window != window) {
        hw_outw(REG_COMMAND, CMD_SELECT_WINDOW | (window & 0x07));
        g_hw_ctx.current_window = window;
    }
}

/**
 * @brief Write byte to hardware register (stub for Week 1)
 */
static void hw_outb(uint8_t reg, uint8_t value)
{
    /* Stub implementation for Week 1 */
    /* In real implementation: outb(g_hw_ctx.io_base + reg, value); */
}

/**
 * @brief Write word to hardware register (stub for Week 1)
 */
static void hw_outw(uint8_t reg, uint16_t value)
{
    /* Stub implementation for Week 1 */
    /* In real implementation: outw(g_hw_ctx.io_base + reg, value); */
}

/**
 * @brief Write dword to hardware register (stub for Week 1)
 */
static void hw_outl(uint8_t reg, uint32_t value)
{
    /* Stub implementation for Week 1 */
    /* In real implementation: outl(g_hw_ctx.io_base + reg, value); */
}

/**
 * @brief Read byte from hardware register (stub for Week 1)
 */
static uint8_t hw_inb(uint8_t reg)
{
    /* Stub implementation for Week 1 */
    /* In real implementation: return inb(g_hw_ctx.io_base + reg); */
    return 0;
}

/**
 * @brief Read word from hardware register (stub for Week 1)
 */
static uint16_t hw_inw(uint8_t reg)
{
    /* Stub implementation for Week 1 */
    /* In real implementation: return inw(g_hw_ctx.io_base + reg); */
    return 0;
}

/**
 * @brief Read dword from hardware register (stub for Week 1)
 */
static uint32_t hw_inl(uint8_t reg)
{
    /* Stub implementation for Week 1 */
    /* In real implementation: return inl(g_hw_ctx.io_base + reg); */
    return 0;
}

/**
 * ============================================================================
 * UTILITY FUNCTIONS (Stubs for Week 1)
 * ============================================================================
 */

static bool hw_wait_command_complete(uint32_t timeout_ms)
{
    /* Stub - wait for command completion */
    return true;
}

static int hw_eeprom_read(uint8_t offset)
{
    /* Stub - read from EEPROM */
    /* Default MAC address for testing */
    static const uint16_t default_mac[] = {0x5000, 0x0010, 0xA400};
    if (offset >= 0x0A && offset <= 0x0C) {
        return default_mac[offset - 0x0A];
    }
    return (offset == 0x00) ? VENDOR_3COM : 
           (offset == 0x01) ? DEVICE_3C515 : 0;
}

static void hw_reset_adapter(void)
{
    /* Stub - reset adapter */
    hw_outw(REG_COMMAND, CMD_GLOBAL_RESET);
}

static int hw_detect_media(void)
{
    /* Stub - return 100BASE-TX */
    return MEDIA_100BASE_TX;
}

static int hw_configure_media(uint8_t media_type)
{
    /* Stub - configure media */
    g_hw_ctx.media_type = media_type;
    return 0;
}

static bool hw_check_link_status(void)
{
    /* Stub - assume link is always up */
    return true;
}