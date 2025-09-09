#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "hardware.h"
#include "pci_bios.h"
#include "el3_unified.h"
#include "el3_segments.h"

static struct el3_device devices[MAX_EL3_DEVICES];
static uint8_t device_count = 0;

static const struct el3_device_info el3_device_table[] = {
    {0x10B7, 0x5900, "3C590 Vortex",       EL3_GEN_VORTEX,    EL3_CAP_10BASE},
    {0x10B7, 0x5950, "3C595 Vortex",       EL3_GEN_VORTEX,    EL3_CAP_100BASE},
    {0x10B7, 0x5951, "3C595 Vortex",       EL3_GEN_VORTEX,    EL3_CAP_100BASE},
    {0x10B7, 0x5952, "3C595 Vortex",       EL3_GEN_VORTEX,    EL3_CAP_100BASE},
    {0x10B7, 0x9000, "3C900 Boomerang",    EL3_GEN_BOOMERANG, EL3_CAP_10BASE | EL3_CAP_DMA},
    {0x10B7, 0x9001, "3C900 Boomerang",    EL3_GEN_BOOMERANG, EL3_CAP_10BASE | EL3_CAP_DMA},
    {0x10B7, 0x9004, "3C900B Cyclone",     EL3_GEN_CYCLONE,   EL3_CAP_10BASE | EL3_CAP_DMA | EL3_CAP_PM},
    {0x10B7, 0x9005, "3C900B Cyclone",     EL3_GEN_CYCLONE,   EL3_CAP_10BASE | EL3_CAP_DMA | EL3_CAP_PM},
    {0x10B7, 0x9050, "3C905 Boomerang",    EL3_GEN_BOOMERANG, EL3_CAP_100BASE | EL3_CAP_DMA},
    {0x10B7, 0x9051, "3C905 Boomerang",    EL3_GEN_BOOMERANG, EL3_CAP_100BASE | EL3_CAP_DMA},
    {0x10B7, 0x9055, "3C905B Cyclone",     EL3_GEN_CYCLONE,   EL3_CAP_100BASE | EL3_CAP_DMA | EL3_CAP_PM},
    {0x10B7, 0x9058, "3C905B Cyclone",     EL3_GEN_CYCLONE,   EL3_CAP_100BASE | EL3_CAP_DMA | EL3_CAP_PM},
    {0x10B7, 0x905A, "3C905B Cyclone",     EL3_GEN_CYCLONE,   EL3_CAP_100BASE | EL3_CAP_DMA | EL3_CAP_PM},
    {0x10B7, 0x9200, "3C905C Tornado",     EL3_GEN_TORNADO,   EL3_CAP_100BASE | EL3_CAP_DMA | EL3_CAP_PM | EL3_CAP_WOL},
    {0x10B7, 0x9201, "3C905C Tornado",     EL3_GEN_TORNADO,   EL3_CAP_100BASE | EL3_CAP_DMA | EL3_CAP_PM | EL3_CAP_WOL},
    {0x10B7, 0x9202, "3C920 Tornado",      EL3_GEN_TORNADO,   EL3_CAP_100BASE | EL3_CAP_DMA | EL3_CAP_PM | EL3_CAP_WOL},
    {0x10B7, 0x9800, "3C980 Cyclone",      EL3_GEN_CYCLONE,   EL3_CAP_100BASE | EL3_CAP_DMA | EL3_CAP_PM},
    {0x10B7, 0x9805, "3C980C Tornado",     EL3_GEN_TORNADO,   EL3_CAP_100BASE | EL3_CAP_DMA | EL3_CAP_PM | EL3_CAP_WOL},
    {0, 0, NULL, 0, 0}
};

__discard
static bool el3_validate_generation(struct el3_device *dev)
{
    uint32_t asic_rev;
    uint16_t media_options;
    
    asic_rev = inl(dev->iobase + 0x7C);
    media_options = inw(dev->iobase + 0x08);
    
    if ((asic_rev >> 28) == 0) {
        return dev->generation == EL3_GEN_VORTEX;
    } else if ((asic_rev >> 28) <= 4) {
        return dev->generation == EL3_GEN_BOOMERANG;
    } else if ((asic_rev >> 28) <= 9) {
        return dev->generation == EL3_GEN_CYCLONE;
    } else {
        return dev->generation == EL3_GEN_TORNADO;
    }
}

__discard
static void el3_probe_capabilities(struct el3_device *dev)
{
    uint16_t config_ctrl;
    uint8_t cap_ptr;
    uint32_t cap_header;
    
    dev->caps_runtime = dev->caps_static;
    
    if (pci_read_config_word(dev->bus, dev->devfn, 0x04, &config_ctrl) == 0) {
        if (config_ctrl & 0x10) {
            cap_ptr = 0;
            if (pci_read_config_byte(dev->bus, dev->devfn, 0x34, &cap_ptr) == 0 && cap_ptr != 0) {
                while (cap_ptr != 0 && cap_ptr < 0xFC) {
                    if (pci_read_config_dword(dev->bus, dev->devfn, cap_ptr, &cap_header) != 0)
                        break;
                    
                    switch (cap_header & 0xFF) {
                        case 0x01:
                            dev->caps_runtime |= EL3_CAP_PM;
                            break;
                        case 0x05:
                            dev->caps_runtime |= EL3_CAP_MSI;
                            break;
                        case 0x10:
                            dev->caps_runtime |= EL3_CAP_PCIE;
                            break;
                    }
                    
                    cap_ptr = (cap_header >> 8) & 0xFF;
                }
            }
        }
    }
    
    if (dev->generation >= EL3_GEN_CYCLONE) {
        uint16_t pm_caps;
        if (inw(dev->iobase + 0x3C) & 0x20) {
            dev->caps_runtime |= EL3_CAP_WOL;
        }
    }
    
    if (dev->generation >= EL3_GEN_BOOMERANG) {
        dev->caps_runtime |= EL3_CAP_DMA;
    }
}

__discard
static int el3_init_device(struct el3_device *dev)
{
    uint16_t cmd;
    
    if (pci_read_config_word(dev->bus, dev->devfn, 0x04, &cmd) != 0)
        return -1;
    
    cmd |= 0x0007;
    if (pci_write_config_word(dev->bus, dev->devfn, 0x04, cmd) != 0)
        return -1;
    
    outw(0x0000, dev->iobase + 0x0E);
    outw(0x00FF, dev->iobase + 0x0E);
    
    outw(0x0800, dev->iobase + 0x0E);
    
    outw(0x2000, dev->iobase + 0x0E);
    
    if (dev->caps_runtime & EL3_CAP_DMA) {
        if (el3_init_dma(dev) != 0)
            return -1;
    }
    
    return 0;
}

__discard
int el3_unified_init(void)
{
    uint8_t last_bus;
    uint8_t bus, dev, func;
    uint16_t vendor, device;
    uint32_t iobase;
    uint8_t irq;
    int i;
    
    if (pci_get_last_bus(&last_bus) != 0) {
        last_bus = 0;
    }
    
    device_count = 0;
    
    for (bus = 0; bus <= last_bus && device_count < MAX_EL3_DEVICES; bus++) {
        for (dev = 0; dev < 32 && device_count < MAX_EL3_DEVICES; dev++) {
            for (func = 0; func < 8 && device_count < MAX_EL3_DEVICES; func++) {
                if (pci_read_config_word(bus, (dev << 3) | func, 0x00, &vendor) != 0)
                    continue;
                if (vendor == 0xFFFF || vendor == 0x0000)
                    continue;
                
                if (vendor != 0x10B7)
                    continue;
                
                if (pci_read_config_word(bus, (dev << 3) | func, 0x02, &device) != 0)
                    continue;
                
                for (i = 0; el3_device_table[i].vendor != 0; i++) {
                    if (el3_device_table[i].vendor == vendor && 
                        el3_device_table[i].device == device) {
                        
                        if (pci_read_config_dword(bus, (dev << 3) | func, 0x10, &iobase) != 0)
                            break;
                        iobase &= 0xFFFC;
                        
                        if (pci_read_config_byte(bus, (dev << 3) | func, 0x3C, &irq) != 0)
                            break;
                        
                        struct el3_device *el3_dev = &devices[device_count];
                        el3_dev->vendor = vendor;
                        el3_dev->device = device;
                        el3_dev->bus = bus;
                        el3_dev->devfn = (dev << 3) | func;
                        el3_dev->iobase = iobase;
                        el3_dev->irq = irq;
                        el3_dev->generation = el3_device_table[i].generation;
                        el3_dev->caps_static = el3_device_table[i].capabilities;
                        strcpy(el3_dev->name, el3_device_table[i].name);
                        
                        el3_probe_capabilities(el3_dev);
                        
                        if (!el3_validate_generation(el3_dev)) {
                            if (el3_dev->generation < EL3_GEN_TORNADO)
                                el3_dev->generation++;
                        }
                        
                        if (el3_init_device(el3_dev) == 0) {
                            device_count++;
                        }
                        break;
                    }
                }
                
                if (func == 0) {
                    uint8_t header_type;
                    if (pci_read_config_byte(bus, (dev << 3) | func, 0x0E, &header_type) != 0)
                        break;
                    if ((header_type & 0x80) == 0)
                        break;
                }
            }
        }
    }
    
    return device_count;
}

__resident
struct el3_device* el3_get_device(uint8_t index)
{
    if (index >= device_count)
        return NULL;
    return &devices[index];
}

__resident
uint8_t el3_get_device_count(void)
{
    return device_count;
}