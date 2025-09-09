#include <stdint.h>
#include <dos.h>
#include "el3_unified.h"
#include "hardware.h"

#define MAX_IRQ_DEVICES 4
#define PORT_STATUS 0x0E
#define PORT_CMD    0x0E

struct irq_sharing {
    uint8_t irq;
    uint8_t device_count;
    struct el3_device *devices[MAX_IRQ_DEVICES];
    void (__interrupt __far *old_handler)();
};

static struct irq_sharing irq_table[16];
static uint8_t irq_initialized = 0;

static void send_eoi(uint8_t irq)
{
    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

static void __interrupt __far el3_shared_irq_handler(void)
{
    uint8_t irq;
    uint8_t i, j;
    uint8_t handled = 0;
    uint16_t status;
    
    _asm {
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
    }
    
    for (irq = 0; irq < 16; irq++) {
        if (irq_table[irq].device_count == 0)
            continue;
        
        for (i = 0; i < irq_table[irq].device_count; i++) {
            struct el3_device *dev = irq_table[irq].devices[i];
            
            status = inw(dev->iobase + PORT_STATUS);
            
            if (status & 0x01) {
                if (dev->caps_runtime & EL3_CAP_DMA) {
                    while (el3_receive_dma(dev, NULL, NULL) == 0) {
                        ;
                    }
                } else {
                    while (el3_receive_pio(dev, NULL, NULL) == 0) {
                        ;
                    }
                }
                
                outw(0x0001, dev->iobase + PORT_CMD);
                handled = 1;
            }
            
            if (status & 0x08) {
                outw(0x0008, dev->iobase + PORT_CMD);
                handled = 1;
            }
            
            if (status & 0xFE) {
                outw(status & 0xFE, dev->iobase + PORT_CMD);
                handled = 1;
            }
        }
        
        if (handled) {
            send_eoi(irq);
            break;
        }
    }
    
    if (!handled) {
        for (irq = 0; irq < 16; irq++) {
            if (irq_table[irq].old_handler) {
                _chain_intr(irq_table[irq].old_handler);
                break;
            }
        }
    }
    
    _asm {
        pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
    }
}

int el3_register_irq(struct el3_device *dev)
{
    uint8_t irq = dev->irq;
    uint8_t i;
    
    if (irq >= 16)
        return -1;
    
    if (!irq_initialized) {
        for (i = 0; i < 16; i++) {
            irq_table[i].irq = i;
            irq_table[i].device_count = 0;
            irq_table[i].old_handler = NULL;
        }
        irq_initialized = 1;
    }
    
    if (irq_table[irq].device_count >= MAX_IRQ_DEVICES)
        return -1;
    
    for (i = 0; i < irq_table[irq].device_count; i++) {
        if (irq_table[irq].devices[i] == dev) {
            return 0;
        }
    }
    
    if (irq_table[irq].device_count == 0) {
        irq_table[irq].old_handler = _dos_getvect(0x08 + irq);
        _dos_setvect(0x08 + irq, el3_shared_irq_handler);
        
        if (irq >= 8) {
            uint8_t mask = inb(0xA1);
            outb(0xA1, mask & ~(1 << (irq - 8)));
        } else {
            uint8_t mask = inb(0x21);
            outb(0x21, mask & ~(1 << irq));
        }
    }
    
    irq_table[irq].devices[irq_table[irq].device_count++] = dev;
    
    outw(0x78FF, dev->iobase + PORT_CMD);
    
    return 0;
}

int el3_unregister_irq(struct el3_device *dev)
{
    uint8_t irq = dev->irq;
    uint8_t i, j;
    
    if (irq >= 16 || !irq_initialized)
        return -1;
    
    for (i = 0; i < irq_table[irq].device_count; i++) {
        if (irq_table[irq].devices[i] == dev) {
            for (j = i; j < irq_table[irq].device_count - 1; j++) {
                irq_table[irq].devices[j] = irq_table[irq].devices[j + 1];
            }
            irq_table[irq].device_count--;
            
            outw(0x7800, dev->iobase + PORT_CMD);
            
            if (irq_table[irq].device_count == 0) {
                if (irq >= 8) {
                    uint8_t mask = inb(0xA1);
                    outb(0xA1, mask | (1 << (irq - 8)));
                } else {
                    uint8_t mask = inb(0x21);
                    outb(0x21, mask | (1 << irq));
                }
                
                _dos_setvect(0x08 + irq, irq_table[irq].old_handler);
                irq_table[irq].old_handler = NULL;
            }
            
            return 0;
        }
    }
    
    return -1;
}