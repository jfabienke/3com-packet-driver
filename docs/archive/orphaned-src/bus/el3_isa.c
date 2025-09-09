/**
 * @file el3_isa.c
 * @brief ISA Bus Prober for 3Com EtherLink III
 *
 * ISA-specific device detection and attachment for 3C509B and 3C515-TX.
 * Handles ISA Plug and Play isolation and I/O port probing.
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "../core/el3_core.h"
#include "../../include/logging.h"

/* ISA PnP registers */
#define PNP_ADDRESS         0x0279  /* PnP address port */
#define PNP_WRITE_DATA      0x0A79  /* PnP write data port */
#define PNP_READ_DATA       0x0203  /* Default read data port */

/* PnP commands */
#define PNP_RESET           0x02    /* Reset all cards */
#define PNP_SET_RD_PORT     0x00    /* Set read data port */
#define PNP_SERIAL_ISOLATION 0x01   /* Enter isolation state */
#define PNP_CONFIG_CONTROL  0x04    /* Configuration control */
#define PNP_WAKE            0x03    /* Wake CSN */
#define PNP_SET_CSN         0x06    /* Set card select number */
#define PNP_ACTIVATE        0x30    /* Activate logical device */
#define PNP_IO_BASE_HIGH    0x60    /* I/O base address high */
#define PNP_IO_BASE_LOW     0x61    /* I/O base address low */
#define PNP_IRQ_SELECT      0x70    /* IRQ number */

/* 3Com PnP IDs */
#define EISA_ID_3C509B      0x90506D50  /* TCM5090 */
#define EISA_ID_3C515       0x90515D50  /* TCM5150 */
#define EISA_ID_3C509B_TP   0x90509050  /* TCM5090 TP */
#define EISA_ID_3C509B_BNC  0x90509150  /* TCM5090 BNC */
#define EISA_ID_3C509B_COMBO 0x90509250 /* TCM5090 Combo */

/* ID port range for 3C509B */
#define ID_PORT_BASE        0x0100
#define ID_PORT_RANGE       0x10
#define ID_PORT_PATTERN     0xFF

/* I/O port probe ranges */
#define IO_PORT_MIN         0x0200
#define IO_PORT_MAX         0x03E0
#define IO_PORT_STEP        0x0010

/* 3C509B specific registers */
#define EL3_ID_PORT         0x0110  /* Default ID port */
#define EL3_CONFIG_CTRL     0x04    /* Configuration control */
#define EL3_RESOURCE_CFG    0x08    /* Resource configuration */

/* 3C515-TX specific */
#define CORKSCREW_TOTAL_SIZE 0x20   /* I/O region size */

/* Forward declarations */
static int el3_isa_pnp_isolate(void);
static int el3_isa_probe_3c509b(void);
static int el3_isa_probe_3c515(void);
static int el3_isa_probe_io_ports(uint16_t base);
static bool el3_isa_check_busmaster(void);
static struct el3_dev *el3_isa_alloc_device(void);
static int el3_isa_configure_3c509b(struct el3_dev *dev, uint16_t io_base, uint8_t irq);
static int el3_isa_configure_3c515(struct el3_dev *dev, uint16_t io_base, uint8_t irq);

/* Global state for ISA bus probing */
static uint8_t g_pnp_read_port = 0x03;  /* Read port address >> 2 */

/**
 * @brief Main ISA bus probe function
 *
 * Searches for 3C509B and 3C515-TX cards using PnP and I/O probing.
 *
 * @return Number of devices found
 */
int el3_isa_probe(void)
{
    int count = 0;
    int ret;
    
    LOG_INFO("EL3-ISA: Starting ISA bus probe");
    
    /* Try ISA PnP isolation first */
    ret = el3_isa_pnp_isolate();
    if (ret > 0) {
        count += ret;
        LOG_INFO("EL3-ISA: Found %d device(s) via PnP", ret);
    }
    
    /* Try legacy 3C509B detection */
    ret = el3_isa_probe_3c509b();
    if (ret > 0) {
        count += ret;
        LOG_INFO("EL3-ISA: Found %d 3C509B via legacy probe", ret);
    }
    
    /* Try 3C515-TX detection */
    ret = el3_isa_probe_3c515();
    if (ret > 0) {
        count += ret;
        LOG_INFO("EL3-ISA: Found %d 3C515-TX", ret);
    }
    
    LOG_INFO("EL3-ISA: Probe complete, found %d total device(s)", count);
    return count;
}

/**
 * @brief ISA PnP isolation sequence
 *
 * Performs the ISA PnP isolation protocol to find PnP cards.
 */
static int el3_isa_pnp_isolate(void)
{
    uint32_t eisa_id;
    uint8_t checksum;
    uint16_t io_base;
    uint8_t irq;
    int count = 0;
    int csn = 1;
    int bit, i;
    
    LOG_DEBUG("EL3-ISA: Starting PnP isolation");
    
    /* Reset all PnP cards */
    outportb(PNP_ADDRESS, PNP_RESET);
    outportb(PNP_WRITE_DATA, PNP_RESET);
    delay_ms(2);
    
    /* Set read data port */
    outportb(PNP_ADDRESS, PNP_SET_RD_PORT);
    outportb(PNP_WRITE_DATA, g_pnp_read_port);
    
    /* Enter isolation state */
    outportb(PNP_ADDRESS, PNP_SERIAL_ISOLATION);
    
    /* Isolation loop - try to find up to 4 cards */
    while (csn <= 4) {
        eisa_id = 0;
        checksum = 0x6A;  /* Initial checksum */
        
        /* Read 72 bits of isolation data */
        for (bit = 0; bit < 72; bit++) {
            uint8_t data;
            
            /* Read bit pair */
            data = inportb((g_pnp_read_port << 2) | 0x03);
            data = (data << 8) | inportb((g_pnp_read_port << 2) | 0x03);
            
            /* Timeout if no response */
            if (data == 0x0000) {
                if (bit == 0) {
                    /* No more cards */
                    goto done;
                }
                break;
            }
            
            /* Extract bit from isolation pair */
            if (data == 0x55AA) {
                /* Bit is 0 */
                if (bit < 64) {
                    eisa_id >>= 1;
                } else {
                    checksum >>= 1;
                }
            } else if (data == 0xAA55) {
                /* Bit is 1 */
                if (bit < 64) {
                    eisa_id = (eisa_id >> 1) | 0x80000000;
                } else {
                    checksum = (checksum >> 1) | 0x80;
                }
            } else {
                /* Collision or error */
                break;
            }
        }
        
        /* Check if we got a valid ID */
        if (bit == 72) {
            struct el3_dev *dev = NULL;
            
            LOG_DEBUG("EL3-ISA: Found PnP device, EISA ID: 0x%08lX", eisa_id);
            
            /* Configure the card */
            outportb(PNP_ADDRESS, PNP_CONFIG_CONTROL);
            outportb(PNP_WRITE_DATA, 0x02);  /* Configure */
            
            /* Assign CSN */
            outportb(PNP_ADDRESS, PNP_SET_CSN);
            outportb(PNP_WRITE_DATA, csn);
            
            /* Wake the card */
            outportb(PNP_ADDRESS, PNP_WAKE);
            outportb(PNP_WRITE_DATA, csn);
            
            /* Read I/O base */
            outportb(PNP_ADDRESS, PNP_IO_BASE_HIGH);
            io_base = inportb((g_pnp_read_port << 2) | 0x03) << 8;
            outportb(PNP_ADDRESS, PNP_IO_BASE_LOW);
            io_base |= inportb((g_pnp_read_port << 2) | 0x03);
            
            /* Read IRQ */
            outportb(PNP_ADDRESS, PNP_IRQ_SELECT);
            irq = inportb((g_pnp_read_port << 2) | 0x03) & 0x0F;
            
            /* Activate the card */
            outportb(PNP_ADDRESS, PNP_ACTIVATE);
            outportb(PNP_WRITE_DATA, 0x01);
            
            /* Check EISA ID and configure accordingly */
            if ((eisa_id & 0xFFFFFF00) == (EISA_ID_3C509B & 0xFFFFFF00)) {
                /* 3C509B variant */
                dev = el3_isa_alloc_device();
                if (dev) {
                    if (el3_isa_configure_3c509b(dev, io_base, irq) == 0) {
                        count++;
                    } else {
                        free(dev);
                    }
                }
            } else if ((eisa_id & 0xFFFFFF00) == (EISA_ID_3C515 & 0xFFFFFF00)) {
                /* 3C515-TX */
                dev = el3_isa_alloc_device();
                if (dev) {
                    if (el3_isa_configure_3c515(dev, io_base, irq) == 0) {
                        count++;
                    } else {
                        free(dev);
                    }
                }
            }
            
            csn++;
        }
        
        /* Return to wait state */
        outportb(PNP_ADDRESS, PNP_CONFIG_CONTROL);
        outportb(PNP_WRITE_DATA, 0x00);
    }
    
done:
    /* Exit isolation */
    outportb(PNP_ADDRESS, PNP_CONFIG_CONTROL);
    outportb(PNP_WRITE_DATA, 0x00);
    
    return count;
}

/**
 * @brief Legacy 3C509B probe
 *
 * Uses the 3C509B-specific ID port mechanism.
 */
static int el3_isa_probe_3c509b(void)
{
    uint16_t id_port;
    uint16_t vendor_id;
    uint16_t io_base;
    uint8_t irq;
    int count = 0;
    
    /* Try different ID ports */
    for (id_port = ID_PORT_BASE; id_port < ID_PORT_BASE + ID_PORT_RANGE; id_port++) {
        /* Send ID sequence */
        outportb(id_port, 0x00);
        outportb(id_port, 0x00);
        outportb(id_port, ID_PORT_PATTERN);
        
        /* Read vendor ID */
        vendor_id = inportw(id_port);
        
        if (vendor_id == 0x6D50) {  /* 3Com vendor ID */
            struct el3_dev *dev;
            
            /* Read resource configuration */
            outportb(id_port, 0xC0 | EL3_RESOURCE_CFG);
            io_base = (inportw(id_port) & 0x1F) << 4;
            
            if (io_base < IO_PORT_MIN || io_base > IO_PORT_MAX) {
                continue;
            }
            
            /* Get IRQ from configuration */
            irq = (inportw(id_port) >> 12) & 0x0F;
            
            LOG_DEBUG("EL3-ISA: Found 3C509B at I/O 0x%04X IRQ %d", io_base, irq);
            
            /* Allocate and configure device */
            dev = el3_isa_alloc_device();
            if (dev) {
                if (el3_isa_configure_3c509b(dev, io_base, irq) == 0) {
                    count++;
                } else {
                    free(dev);
                }
            }
            
            /* Only one 3C509B can be at each ID port */
            break;
        }
    }
    
    return count;
}

/**
 * @brief 3C515-TX probe
 *
 * Probes for 3C515-TX Fast EtherLink ISA cards.
 */
static int el3_isa_probe_3c515(void)
{
    uint16_t io_bases[] = {0x300, 0x280, 0x320, 0x340, 0x360, 0x380, 0};
    int i;
    int count = 0;
    
    for (i = 0; io_bases[i] != 0; i++) {
        uint16_t io_base = io_bases[i];
        uint16_t sig;
        
        /* Check for 3C515 signature */
        sig = inportw(io_base + 0x00);
        if (sig == 0x5157) {  /* "QW" in little-endian */
            struct el3_dev *dev;
            uint8_t irq;
            
            /* Read IRQ from configuration */
            irq = (inportw(io_base + 0x08) >> 12) & 0x0F;
            
            LOG_INFO("EL3-ISA: Found 3C515-TX at I/O 0x%04X IRQ %d", io_base, irq);
            
            /* Allocate and configure device */
            dev = el3_isa_alloc_device();
            if (dev) {
                if (el3_isa_configure_3c515(dev, io_base, irq) == 0) {
                    count++;
                } else {
                    free(dev);
                }
            }
        }
    }
    
    return count;
}

/**
 * @brief Probe specific I/O port for 3Com card
 */
static int el3_isa_probe_io_ports(uint16_t base)
{
    uint16_t sig;
    
    /* Try to read signature */
    sig = inportw(base);
    
    /* Check for 3Com patterns */
    if ((sig & 0xFF00) == 0x5000 ||  /* 3C5xx series */
        (sig & 0xFF00) == 0x9000) {  /* 3C9xx series */
        return 1;
    }
    
    return 0;
}

/**
 * @brief Check if ISA bus master DMA is available
 */
static bool el3_isa_check_busmaster(void)
{
    /* Check for DMA controller presence */
    /* This would normally check chipset capabilities */
    /* For now, assume DMA is available on 386+ systems */
    
    /* Simple CPU check - if 386 or better, likely has DMA */
    uint16_t cpu_type;
    
    __asm {
        pushf
        pop ax
        mov cx, ax
        or ax, 0x7000  ; Try to set NT, IOPL flags
        push ax
        popf
        pushf
        pop ax
        push cx
        popf
        and ax, 0x7000
        jz is_286_or_less
        mov cpu_type, 386
        jmp done
is_286_or_less:
        mov cpu_type, 286
done:
    }
    
    return (cpu_type >= 386);
}

/**
 * @brief Allocate device structure
 */
static struct el3_dev *el3_isa_alloc_device(void)
{
    struct el3_dev *dev;
    
    dev = (struct el3_dev *)calloc(1, sizeof(struct el3_dev));
    if (!dev) {
        LOG_ERROR("EL3-ISA: Failed to allocate device structure");
        return NULL;
    }
    
    dev->io_mapped = true;  /* ISA is always I/O mapped */
    
    return dev;
}

/**
 * @brief Configure 3C509B device
 */
static int el3_isa_configure_3c509b(struct el3_dev *dev, uint16_t io_base, uint8_t irq)
{
    strcpy(dev->name, "3C509B EtherLink III");
    dev->vendor_id = 0x10B7;  /* 3Com */
    dev->device_id = 0x5090;  /* 3C509B */
    dev->generation = EL3_GEN_3C509B;
    dev->io_base = io_base;
    dev->irq = irq;
    
    /* Initialize the device */
    return el3_init(dev);
}

/**
 * @brief Configure 3C515-TX device
 */
static int el3_isa_configure_3c515(struct el3_dev *dev, uint16_t io_base, uint8_t irq)
{
    strcpy(dev->name, "3C515-TX Fast EtherLink");
    dev->vendor_id = 0x10B7;  /* 3Com */
    dev->device_id = 0x5150;  /* 3C515 */
    dev->generation = EL3_GEN_3C515;
    dev->io_base = io_base;
    dev->irq = irq;
    
    /* Check for bus master capability */
    if (el3_isa_check_busmaster()) {
        LOG_INFO("EL3-ISA: ISA bus master DMA available for 3C515-TX");
        /* Capability will be set by el3_detect_capabilities */
    }
    
    /* Initialize the device */
    return el3_init(dev);
}

/**
 * @brief Utility: Delay in microseconds
 */
void delay_us(unsigned int us)
{
    /* Simple delay loop - adjust for CPU speed */
    while (us--) {
        __asm {
            nop
            nop
            nop
            nop
        }
    }
}

/**
 * @brief Utility: Delay in milliseconds
 */
void delay_ms(unsigned int ms)
{
    while (ms--) {
        delay_us(1000);
    }
}