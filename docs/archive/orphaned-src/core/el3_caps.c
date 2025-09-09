/**
 * @file el3_caps.c
 * @brief 3Com EtherLink III Capability Detection
 *
 * Runtime capability detection for all 3Com EtherLink III variants.
 * Identifies generation, features, and hardware parameters.
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include <stdint.h>
#include <string.h>
#include "el3_core.h"
#include "el3_caps.h"
#include "../hal/el3_hal.h"
#include "../../include/logging.h"

/* EEPROM command definitions */
#define EEPROM_CMD_READ     0x80
#define EEPROM_BUSY         0x8000
#define EEPROM_SIZE         64      /* Words */

/* EEPROM offsets */
#define EEPROM_NODE_ADDR_0  0x00    /* MAC address words 0-2 */
#define EEPROM_NODE_ADDR_1  0x01
#define EEPROM_NODE_ADDR_2  0x02
#define EEPROM_PROD_ID      0x03    /* Product ID */
#define EEPROM_MFG_DATE     0x04    /* Manufacturing date */
#define EEPROM_MFG_ID       0x07    /* Manufacturer ID */
#define EEPROM_ADDR_CFG     0x08    /* Address configuration */
#define EEPROM_RESOURCE_CFG 0x09    /* Resource configuration */
#define EEPROM_SOFT_INFO    0x0A    /* Software information */
#define EEPROM_COMPAT       0x0E    /* Compatibility */
#define EEPROM_CAPABILITIES 0x10    /* Capabilities word */
#define EEPROM_INTERNAL_CFG 0x13    /* Internal configuration */

/* Product ID masks */
#define PROD_ID_MASK        0xFF00
#define PROD_ID_3C509B      0x5090
#define PROD_ID_3C515       0x5150
#define PROD_ID_3C590       0x5900
#define PROD_ID_3C595       0x5950
#define PROD_ID_3C900       0x9000
#define PROD_ID_3C905       0x9050
#define PROD_ID_3C905B      0x9055
#define PROD_ID_3C905C      0x9200

/* Capability bits from EEPROM */
#define CAP_FULL_DUPLEX     0x0001
#define CAP_LARGE_PACKETS   0x0002
#define CAP_SLAVE_DMA       0x0004
#define CAP_SECOND_DMA      0x0008
#define CAP_FULL_BUS_MASTER 0x0010
#define CAP_FRAG_BUS_MASTER 0x0020
#define CAP_CRC_PASS_THRU   0x0040
#define CAP_TX_DONE_INT     0x0080
#define CAP_NO_TX_LENGTH    0x0100
#define CAP_RX_REPEAT       0x0200
#define CAP_INDICATORS      0x0400
#define CAP_BROAD_RX        0x0800
#define CAP_RAM_SIZE_MASK   0x7000
#define CAP_RAM_SPEED       0x8000

/* Generation capability tables */
struct generation_caps {
    enum el3_generation generation;
    uint16_t base_caps;
    uint16_t fifo_size;
    bool has_permanent_window1;
    bool has_stats_window;
    bool has_flow_control;
    bool has_nway;
    bool has_hw_checksum;
    bool has_vlan_support;
    bool has_wake_on_lan;
};

static const struct generation_caps gen_caps[] = {
    /* 3C509B - Basic ISA EtherLink III */
    {
        .generation = EL3_GEN_3C509B,
        .base_caps = 0,
        .fifo_size = 2048,  /* 2KB FIFOs */
        .has_permanent_window1 = false,
        .has_stats_window = true,
        .has_flow_control = false,
        .has_nway = false,
        .has_hw_checksum = false,
        .has_vlan_support = false,
        .has_wake_on_lan = false
    },
    
    /* 3C515-TX - ISA Fast EtherLink with bus master */
    {
        .generation = EL3_GEN_3C515,
        .base_caps = CAP_FULL_BUS_MASTER,
        .fifo_size = 8192,  /* 8KB FIFOs */
        .has_permanent_window1 = true,
        .has_stats_window = true,
        .has_flow_control = false,
        .has_nway = false,
        .has_hw_checksum = false,
        .has_vlan_support = false,
        .has_wake_on_lan = false
    },
    
    /* Vortex - First generation PCI */
    {
        .generation = EL3_GEN_VORTEX,
        .base_caps = CAP_FULL_BUS_MASTER,
        .fifo_size = 8192,
        .has_permanent_window1 = true,
        .has_stats_window = true,
        .has_flow_control = false,
        .has_nway = false,
        .has_hw_checksum = false,
        .has_vlan_support = false,
        .has_wake_on_lan = false
    },
    
    /* Boomerang - Enhanced DMA */
    {
        .generation = EL3_GEN_BOOMERANG,
        .base_caps = CAP_FULL_BUS_MASTER | CAP_FULL_DUPLEX,
        .fifo_size = 8192,
        .has_permanent_window1 = true,
        .has_stats_window = true,
        .has_flow_control = true,
        .has_nway = false,
        .has_hw_checksum = false,
        .has_vlan_support = false,
        .has_wake_on_lan = false
    },
    
    /* Cyclone - Hardware offload */
    {
        .generation = EL3_GEN_CYCLONE,
        .base_caps = CAP_FULL_BUS_MASTER | CAP_FULL_DUPLEX,
        .fifo_size = 8192,
        .has_permanent_window1 = true,
        .has_stats_window = true,
        .has_flow_control = true,
        .has_nway = true,
        .has_hw_checksum = true,
        .has_vlan_support = true,
        .has_wake_on_lan = false
    },
    
    /* Tornado - Advanced features */
    {
        .generation = EL3_GEN_TORNADO,
        .base_caps = CAP_FULL_BUS_MASTER | CAP_FULL_DUPLEX,
        .fifo_size = 8192,
        .has_permanent_window1 = true,
        .has_stats_window = true,
        .has_flow_control = true,
        .has_nway = true,
        .has_hw_checksum = true,
        .has_vlan_support = true,
        .has_wake_on_lan = true
    }
};

/* Forward declarations */
static uint16_t el3_read_eeprom(struct el3_dev *dev, uint8_t offset);
static enum el3_generation el3_identify_generation(struct el3_dev *dev, uint16_t prod_id);
static void el3_apply_generation_caps(struct el3_dev *dev);
static void el3_detect_runtime_features(struct el3_dev *dev);

/**
 * @brief Detect device capabilities
 *
 * Reads EEPROM, identifies generation, and determines features.
 *
 * @param dev Device structure
 * @return 0 on success, negative error code on failure
 */
int el3_detect_capabilities(struct el3_dev *dev)
{
    uint16_t prod_id;
    uint16_t cap_word;
    uint16_t internal_cfg;
    
    if (!dev) {
        return -EINVAL;
    }
    
    LOG_DEBUG("EL3: Detecting capabilities for device at 0x%04X", dev->io_base);
    
    /* Select window 0 for EEPROM access */
    el3_select_window(dev, 0);
    
    /* Read product ID */
    prod_id = el3_read_eeprom(dev, EEPROM_PROD_ID);
    if (prod_id == 0xFFFF || prod_id == 0x0000) {
        LOG_ERROR("EL3: Invalid product ID 0x%04X", prod_id);
        return -ENODEV;
    }
    
    LOG_DEBUG("EL3: Product ID: 0x%04X", prod_id);
    
    /* Identify generation based on product ID */
    dev->generation = el3_identify_generation(dev, prod_id);
    if (dev->generation == EL3_GEN_UNKNOWN) {
        LOG_WARNING("EL3: Unknown product ID 0x%04X, using defaults", prod_id);
    }
    
    /* Read capability word from EEPROM */
    cap_word = el3_read_eeprom(dev, EEPROM_CAPABILITIES);
    LOG_DEBUG("EL3: EEPROM capabilities: 0x%04X", cap_word);
    
    /* Read internal configuration */
    internal_cfg = el3_read_eeprom(dev, EEPROM_INTERNAL_CFG);
    LOG_DEBUG("EL3: Internal config: 0x%04X", internal_cfg);
    
    /* Apply generation-specific capabilities */
    el3_apply_generation_caps(dev);
    
    /* Parse EEPROM capability bits */
    if (cap_word & CAP_FULL_BUS_MASTER) {
        dev->caps.has_bus_master = true;
    }
    if (cap_word & CAP_FULL_DUPLEX) {
        /* Full duplex capable */
    }
    if (cap_word & CAP_LARGE_PACKETS) {
        dev->caps.has_large_packets = true;
    }
    
    /* Detect runtime features */
    el3_detect_runtime_features(dev);
    
    /* Set interrupt mask based on capabilities */
    dev->caps.interrupt_mask = 0x01FB;  /* Standard interrupts */
    if (dev->caps.has_bus_master) {
        dev->caps.interrupt_mask |= 0x0200;  /* DMA interrupts */
    }
    
    LOG_INFO("EL3: Generation: %s, Bus Master: %s, HW Checksum: %s",
             el3_generation_name(dev->generation),
             dev->caps.has_bus_master ? "Yes" : "No",
             dev->caps.has_hw_checksum ? "Yes" : "No");
    
    return 0;
}

/**
 * @brief Read MAC address from EEPROM
 *
 * @param dev Device structure
 * @return 0 on success, negative error code on failure
 */
int el3_read_mac_address(struct el3_dev *dev)
{
    uint16_t word;
    int i;
    
    if (!dev) {
        return -EINVAL;
    }
    
    /* Select window 0 for EEPROM access */
    el3_select_window(dev, 0);
    
    /* Read MAC address from EEPROM (3 words) */
    for (i = 0; i < 3; i++) {
        word = el3_read_eeprom(dev, EEPROM_NODE_ADDR_0 + i);
        dev->mac_addr[i * 2] = (word >> 8) & 0xFF;
        dev->mac_addr[i * 2 + 1] = word & 0xFF;
    }
    
    /* Validate MAC address */
    if ((dev->mac_addr[0] & 0x01) ||  /* Multicast bit set */
        (dev->mac_addr[0] == 0x00 && dev->mac_addr[1] == 0x00 &&
         dev->mac_addr[2] == 0x00 && dev->mac_addr[3] == 0x00 &&
         dev->mac_addr[4] == 0x00 && dev->mac_addr[5] == 0x00)) {
        LOG_ERROR("EL3: Invalid MAC address");
        return -EINVAL;
    }
    
    return 0;
}

/**
 * @brief Read word from EEPROM
 *
 * @param dev Device structure
 * @param offset EEPROM word offset
 * @return EEPROM word value
 */
static uint16_t el3_read_eeprom(struct el3_dev *dev, uint8_t offset)
{
    uint16_t cmd;
    int timeout;
    
    /* Issue read command */
    cmd = EEPROM_CMD_READ | (offset & 0x3F);
    el3_write16(dev, 0x0A, cmd);  /* Window 0, offset 0x0A */
    
    /* Wait for EEPROM to be ready */
    timeout = 1000;
    while (timeout > 0) {
        if (!(el3_read16(dev, 0x0A) & EEPROM_BUSY)) {
            break;
        }
        delay_us(10);
        timeout--;
    }
    
    if (timeout == 0) {
        LOG_ERROR("EL3: EEPROM read timeout at offset %d", offset);
        return 0xFFFF;
    }
    
    /* Read data */
    return el3_read16(dev, 0x0C);  /* Window 0, offset 0x0C */
}

/**
 * @brief Identify device generation from product ID
 */
static enum el3_generation el3_identify_generation(struct el3_dev *dev, uint16_t prod_id)
{
    /* Check PCI device ID if available */
    if (dev->device_id != 0) {
        /* PCI device ID mapping */
        if ((dev->device_id & 0xFF00) == 0x5900) {
            return EL3_GEN_VORTEX;  /* 3C59x */
        } else if ((dev->device_id & 0xFF00) == 0x9000) {
            if (dev->device_id < 0x9050) {
                return EL3_GEN_BOOMERANG;  /* 3C900 */
            } else if (dev->device_id < 0x9200) {
                return EL3_GEN_BOOMERANG;  /* 3C905 */
            } else if (dev->device_id < 0x9300) {
                return EL3_GEN_CYCLONE;    /* 3C905B */
            } else {
                return EL3_GEN_TORNADO;    /* 3C905C */
            }
        }
    }
    
    /* Fall back to EEPROM product ID */
    switch (prod_id & PROD_ID_MASK) {
    case PROD_ID_3C509B:
        return EL3_GEN_3C509B;
    case PROD_ID_3C515:
        return EL3_GEN_3C515;
    case PROD_ID_3C590:
        return EL3_GEN_VORTEX;
    case PROD_ID_3C595:
        return EL3_GEN_VORTEX;
    case PROD_ID_3C900:
        return EL3_GEN_BOOMERANG;
    case PROD_ID_3C905:
        return EL3_GEN_BOOMERANG;
    case PROD_ID_3C905B:
        return EL3_GEN_CYCLONE;
    case PROD_ID_3C905C:
        return EL3_GEN_TORNADO;
    default:
        return EL3_GEN_UNKNOWN;
    }
}

/**
 * @brief Apply generation-specific capabilities
 */
static void el3_apply_generation_caps(struct el3_dev *dev)
{
    const struct generation_caps *caps = NULL;
    int i;
    
    /* Find matching generation */
    for (i = 0; i < sizeof(gen_caps)/sizeof(gen_caps[0]); i++) {
        if (gen_caps[i].generation == dev->generation) {
            caps = &gen_caps[i];
            break;
        }
    }
    
    if (!caps) {
        /* Default to 3C509B capabilities */
        caps = &gen_caps[0];
    }
    
    /* Apply capabilities */
    dev->caps.fifo_size = caps->fifo_size;
    dev->caps.has_permanent_window1 = caps->has_permanent_window1;
    dev->caps.has_stats_window = caps->has_stats_window;
    dev->caps.has_flow_control = caps->has_flow_control;
    dev->caps.has_nway = caps->has_nway;
    dev->caps.has_hw_checksum = caps->has_hw_checksum;
    dev->caps.has_vlan_support = caps->has_vlan_support;
    dev->caps.has_wake_on_lan = caps->has_wake_on_lan;
    
    /* Bus master capability from base_caps or ISA detection */
    if (caps->base_caps & CAP_FULL_BUS_MASTER) {
        dev->caps.has_bus_master = true;
    }
    
    /* Store raw capability flags */
    dev->caps.flags = caps->base_caps;
}

/**
 * @brief Detect runtime features not in EEPROM
 */
static void el3_detect_runtime_features(struct el3_dev *dev)
{
    /* ISA bus master detection for 3C515-TX */
    if (dev->generation == EL3_GEN_3C515) {
        /* Check if ISA DMA is available */
        /* This would normally check chipset capabilities */
        /* For now, assume DMA is available if 3C515 detected */
        dev->caps.has_bus_master = true;
    }
    
    /* PCI cards always have bus master capability */
    if (dev->generation >= EL3_GEN_VORTEX && dev->device_id != 0) {
        dev->caps.has_bus_master = true;
    }
    
    /* Disable advanced features if running on slow CPU */
    /* This could check CPU speed and disable features accordingly */
}