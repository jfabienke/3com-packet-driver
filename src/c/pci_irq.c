/**
 * @file pci_irq.c
 * @brief PCI IRQ routing validation and fallback implementation
 * 
 * Handles IRQ line validation, manual override, polled mode fallback,
 * and safe ISR chaining for shared interrupts.
 */

#include <dos.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "pci_bios.h"
#include "pci_irq.h"
#include "logging.h"
#include "common.h"
#include "config.h"

/* PIC (8259A) registers */
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

/* PIC commands */
#define PIC_EOI         0x20    /* End of Interrupt */
#define PIC_READ_IRR    0x0A    /* Read Interrupt Request Register */
#define PIC_READ_ISR    0x0B    /* Read In-Service Register */

/* Valid ISA IRQs for PCI devices */
static const uint8_t valid_irqs[] = {3, 5, 7, 9, 10, 11, 12, 15};
#define NUM_VALID_IRQS (sizeof(valid_irqs) / sizeof(valid_irqs[0]))

/* IRQ chaining structure for shared interrupts */
typedef struct irq_chain {
    void (__interrupt __far *old_handler)();
    void (__interrupt __far *new_handler)();
    uint8_t irq;
    bool shared;
    bool in_use;
} irq_chain_t;

/* IRQ chain table */
static irq_chain_t irq_chains[16] = {0};

/* Polling mode structure */
typedef struct {
    bool enabled;
    uint16_t interval_ms;
    uint32_t last_poll;
    void (*poll_handler)(void);
} poll_mode_t;

static poll_mode_t poll_mode = {0};

/**
 * @brief Validate PCI IRQ line value
 * 
 * Checks if IRQ line register contains a valid IRQ number.
 * 0 or 0xFF indicate no interrupt assigned.
 * 
 * @param irq IRQ line value from PCI config
 * @return true if valid, false if invalid or unassigned
 */
bool pci_validate_irq(uint8_t irq) {
    int i;
    
    /* Check for unassigned values */
    if (irq == 0 || irq == 0xFF) {
        LOG_DEBUG("IRQ line unassigned (0x%02X)", irq);
        return false;
    }
    
    /* Check if IRQ is in valid range */
    if (irq > 15) {
        LOG_WARNING("IRQ %d out of range (>15)", irq);
        return false;
    }
    
    /* Check if IRQ is valid for ISA/PCI */
    for (i = 0; i < NUM_VALID_IRQS; i++) {
        if (irq == valid_irqs[i]) {
            LOG_DEBUG("IRQ %d is valid", irq);
            return true;
        }
    }
    
    /* IRQ 2 is cascade, 0/1 are system */
    if (irq <= 2) {
        LOG_WARNING("IRQ %d is reserved for system use", irq);
        return false;
    }
    
    /* IRQs 4, 6, 8, 13, 14 typically used by standard devices */
    LOG_WARNING("IRQ %d may conflict with standard devices", irq);
    return true;  /* Allow but warn */
}

/**
 * @brief Override PCI IRQ assignment
 * 
 * Manually assigns IRQ to PCI device. Used when BIOS didn't program IRQ
 * but hardware is wired for specific IRQ.
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param irq IRQ to assign (3-15)
 * @return true on success, false on error
 */
bool pci_override_irq(uint8_t bus, uint8_t device, uint8_t function, uint8_t irq) {
    uint8_t current_irq;
    
    /* Validate requested IRQ */
    if (!pci_validate_irq(irq)) {
        LOG_ERROR("Cannot override with invalid IRQ %d", irq);
        return false;
    }
    
    /* Read current IRQ */
    current_irq = pci_get_irq(bus, device, function);
    
    if (current_irq == irq) {
        LOG_DEBUG("IRQ already set to %d", irq);
        return true;
    }
    
    LOG_WARNING("Overriding IRQ from %d to %d for %02X:%02X.%X",
               current_irq, irq, bus, device, function);
    
    /* Write new IRQ to Interrupt Line register */
    if (!pci_write_config_byte(bus, device, function, PCI_INTERRUPT_LINE, irq)) {
        LOG_ERROR("Failed to write IRQ override");
        return false;
    }
    
    /* Verify write */
    current_irq = pci_get_irq(bus, device, function);
    if (current_irq != irq) {
        LOG_ERROR("IRQ override failed - read back %d instead of %d", current_irq, irq);
        return false;
    }
    
    LOG_INFO("Successfully overrode IRQ to %d", irq);
    return true;
}

/**
 * @brief Setup polling mode for interrupt-less operation
 * 
 * Configures polled mode when no IRQ is available or as fallback.
 * 
 * @param interval_ms Polling interval in milliseconds
 * @param handler Polling handler function
 * @return true on success, false on error
 */
bool pci_setup_polling(uint16_t interval_ms, void (*handler)(void)) {
    if (!handler) {
        LOG_ERROR("Invalid polling handler");
        return false;
    }
    
    if (interval_ms < 10) {
        LOG_WARNING("Polling interval %dms too aggressive, using 10ms", interval_ms);
        interval_ms = 10;
    }
    
    if (interval_ms > 1000) {
        LOG_WARNING("Polling interval %dms may cause poor responsiveness", interval_ms);
    }
    
    poll_mode.enabled = true;
    poll_mode.interval_ms = interval_ms;
    poll_mode.poll_handler = handler;
    poll_mode.last_poll = 0;
    
    LOG_INFO("Polling mode enabled with %dms interval", interval_ms);
    
    return true;
}

/**
 * @brief Disable polling mode
 */
void pci_disable_polling(void) {
    if (poll_mode.enabled) {
        poll_mode.enabled = false;
        LOG_INFO("Polling mode disabled");
    }
}

/**
 * @brief Poll handler - call periodically from main loop
 * 
 * @return true if poll was executed, false if not time yet
 */
bool pci_poll_handler(void) {
    uint32_t current_time;
    
    if (!poll_mode.enabled || !poll_mode.poll_handler) {
        return false;
    }
    
    /* Get current time (implementation-specific) */
    current_time = get_system_ticks();
    
    if ((current_time - poll_mode.last_poll) >= poll_mode.interval_ms) {
        poll_mode.last_poll = current_time;
        poll_mode.poll_handler();
        return true;
    }
    
    return false;
}

/**
 * @brief Safe ISR practices for DOS PCI interrupt handling
 * 
 * Critical requirements for reliable interrupt handling:
 * 1. Check if our device caused the interrupt FIRST
 * 2. Acknowledge device interrupt source before PIC EOI
 * 3. NO DOS/BIOS calls inside ISR (no printf, malloc, etc.)
 * 4. Minimal processing - defer to main loop
 * 5. Chain carefully for shared interrupts
 */

/* ISR-safe logging buffer */
#define ISR_LOG_ENTRIES 16
static struct {
    uint16_t count;
    uint16_t errors;
    uint16_t spurious;
    uint16_t shared_calls;
} isr_stats = {0};

/**
 * @brief Template for safe 3Com NIC ISR
 * 
 * This is a template showing proper ISR structure for 3Com NICs.
 * Actual implementation must be device-specific.
 */
static void __interrupt __far safe_3com_isr_template(void) {
    uint16_t int_status;
    uint16_t iobase;  /* Would be set during ISR installation */
    bool our_interrupt = false;
    
    /* CRITICAL: Read interrupt status FIRST to check if ours */
    int_status = inw(iobase + 0x0E);  /* IntStatus register */
    
    /* Check if any of our interrupt sources are active */
    if (int_status & 0x01FF) {  /* Any interrupt bits set */
        our_interrupt = true;
        isr_stats.count++;
        
        /* Acknowledge interrupt sources in NIC BEFORE PIC EOI */
        outw(iobase + 0x0E, int_status & 0x01FF);  /* Clear our bits */
        
        /* Minimal processing - set flags for main loop */
        if (int_status & 0x0004) {  /* TX complete */
            /* Set flag for TX completion processing */
        }
        if (int_status & 0x0010) {  /* RX complete */
            /* Set flag for RX processing */
        }
        if (int_status & 0x0008) {  /* RX early */
            /* Set flag for RX early processing */
        }
        
        /* Send EOI to PIC only AFTER acknowledging device */
        pci_send_eoi(/* IRQ number would be stored */);
    } else {
        /* Not our interrupt - this is normal on shared lines */
        isr_stats.spurious++;
        our_interrupt = false;
    }
    
    /* Chain to old handler if interrupt wasn't ours or if sharing */
    if (!our_interrupt || /* sharing flag would be checked */) {
        isr_stats.shared_calls++;
        /* Call old handler - would be stored during installation */
        /* ((void (__interrupt __far *)())old_handler)(); */
    }
}

/**
 * @brief Get ISR statistics (safe to call from main loop)
 */
void pci_get_isr_stats(uint16_t *interrupts, uint16_t *errors, 
                      uint16_t *spurious, uint16_t *shared) {
    if (interrupts) *interrupts = isr_stats.count;
    if (errors) *errors = isr_stats.errors;
    if (spurious) *spurious = isr_stats.spurious;
    if (shared) *shared = isr_stats.shared_calls;
}

/**
 * @brief Install ISR with chaining support for shared IRQs
 * 
 * Safely installs interrupt handler with support for IRQ sharing.
 * 
 * @param irq IRQ number (0-15)
 * @param handler New interrupt handler
 * @param shared true if IRQ should be shared
 * @return true on success, false on error
 */
bool pci_install_isr(uint8_t irq, void (__interrupt __far *handler)(), bool shared) {
    void (__interrupt __far *old_handler)();
    
    if (irq > 15 || !handler) {
        LOG_ERROR("Invalid IRQ %d or handler", irq);
        return false;
    }
    
    if (irq_chains[irq].in_use) {
        if (!shared || !irq_chains[irq].shared) {
            LOG_ERROR("IRQ %d already in use and not shareable", irq);
            return false;
        }
        LOG_WARNING("Sharing IRQ %d with existing handler", irq);
    }
    
    /* Save old handler */
    old_handler = _dos_getvect(0x08 + irq);
    
    /* Store chain information */
    irq_chains[irq].old_handler = old_handler;
    irq_chains[irq].new_handler = handler;
    irq_chains[irq].irq = irq;
    irq_chains[irq].shared = shared;
    irq_chains[irq].in_use = true;
    
    /* Install new handler */
    _disable();
    _dos_setvect(0x08 + irq, handler);
    _enable();
    
    /* Unmask IRQ in PIC */
    if (irq < 8) {
        outp(PIC1_DATA, inp(PIC1_DATA) & ~(1 << irq));
    } else {
        outp(PIC2_DATA, inp(PIC2_DATA) & ~(1 << (irq - 8)));
        /* Also unmask cascade IRQ2 */
        outp(PIC1_DATA, inp(PIC1_DATA) & ~(1 << 2));
    }
    
    LOG_INFO("Installed ISR for IRQ %d (shared=%d)", irq, shared);
    
    return true;
}

/**
 * @brief Uninstall ISR and restore original handler
 * 
 * @param irq IRQ number (0-15)
 * @return true on success, false on error
 */
bool pci_uninstall_isr(uint8_t irq) {
    if (irq > 15) {
        LOG_ERROR("Invalid IRQ %d", irq);
        return false;
    }
    
    if (!irq_chains[irq].in_use) {
        LOG_WARNING("No ISR installed for IRQ %d", irq);
        return true;
    }
    
    /* Mask IRQ in PIC */
    if (irq < 8) {
        outp(PIC1_DATA, inp(PIC1_DATA) | (1 << irq));
    } else {
        outp(PIC2_DATA, inp(PIC2_DATA) | (1 << (irq - 8)));
    }
    
    /* Restore old handler */
    _disable();
    _dos_setvect(0x08 + irq, irq_chains[irq].old_handler);
    _enable();
    
    /* Clear chain information */
    memset(&irq_chains[irq], 0, sizeof(irq_chain_t));
    
    LOG_INFO("Uninstalled ISR for IRQ %d", irq);
    
    return true;
}

/**
 * @brief Send End-Of-Interrupt to PIC
 * 
 * Must be called at end of interrupt handler.
 * 
 * @param irq IRQ number (0-15)
 */
void pci_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        /* Send EOI to slave PIC */
        outp(PIC2_COMMAND, PIC_EOI);
    }
    /* Always send EOI to master PIC */
    outp(PIC1_COMMAND, PIC_EOI);
}

/**
 * @brief Check if IRQ is pending in PIC
 * 
 * @param irq IRQ number (0-15)
 * @return true if IRQ is pending, false otherwise
 */
bool pci_is_irq_pending(uint8_t irq) {
    uint8_t irr;
    
    if (irq > 15) {
        return false;
    }
    
    if (irq < 8) {
        /* Read IRR from master PIC */
        outp(PIC1_COMMAND, PIC_READ_IRR);
        irr = inp(PIC1_COMMAND);
        return (irr & (1 << irq)) ? true : false;
    } else {
        /* Read IRR from slave PIC */
        outp(PIC2_COMMAND, PIC_READ_IRR);
        irr = inp(PIC2_COMMAND);
        return (irr & (1 << (irq - 8))) ? true : false;
    }
}

/**
 * @brief Setup PCI device IRQ with validation and fallbacks
 * 
 * Comprehensive IRQ setup with validation, override, and polling fallback.
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param config Driver configuration
 * @param handler Interrupt handler
 * @param poll_handler Polling handler (for fallback)
 * @return Assigned IRQ (0-15), 0xFF for polling mode, or 0xFE on error
 */
uint8_t pci_setup_device_irq(uint8_t bus, uint8_t device, uint8_t function,
                             const config_t *config,
                             void (__interrupt __far *handler)(),
                             void (*poll_handler)(void)) {
    uint8_t irq;
    uint8_t override_irq = 0;
    bool use_polling = false;
    
    LOG_INFO("Setting up IRQ for PCI device %02X:%02X.%X", bus, device, function);
    
    /* Read IRQ from PCI config */
    irq = pci_get_irq(bus, device, function);
    LOG_DEBUG("PCI IRQ Line register = 0x%02X", irq);
    
    /* Check for manual IRQ override in config */
    if (config && config->irq1 != 0) {
        override_irq = config->irq1;
        LOG_INFO("Manual IRQ override requested: IRQ %d", override_irq);
    }
    
    /* Validate IRQ */
    if (!pci_validate_irq(irq)) {
        if (override_irq && pci_validate_irq(override_irq)) {
            /* Use override IRQ */
            LOG_WARNING("Invalid IRQ %d, using override IRQ %d", irq, override_irq);
            if (!pci_override_irq(bus, device, function, override_irq)) {
                LOG_ERROR("Failed to override IRQ");
                use_polling = true;
            } else {
                irq = override_irq;
            }
        } else {
            /* No valid IRQ - use polling */
            LOG_WARNING("No valid IRQ available - using polling mode");
            use_polling = true;
        }
    }
    
    /* Setup IRQ or polling */
    if (use_polling) {
        if (poll_handler) {
            uint16_t poll_interval = (config && config->poll_interval) ? 
                                    config->poll_interval : 20;  /* Default 20ms */
            if (pci_setup_polling(poll_interval, poll_handler)) {
                LOG_INFO("Using polling mode with %dms interval", poll_interval);
                return 0xFF;  /* Special value for polling mode */
            }
        }
        LOG_ERROR("Polling mode requested but no handler provided");
        return 0xFE;  /* Error */
    }
    
    /* Install ISR */
    if (handler) {
        bool shared = (config && config->shared_irq) ? true : false;
        if (!pci_install_isr(irq, handler, shared)) {
            LOG_ERROR("Failed to install ISR for IRQ %d", irq);
            /* Fall back to polling if available */
            if (poll_handler) {
                uint16_t poll_interval = (config && config->poll_interval) ? 
                                        config->poll_interval : 20;
                if (pci_setup_polling(poll_interval, poll_handler)) {
                    LOG_WARNING("Falling back to polling mode");
                    return 0xFF;
                }
            }
            return 0xFE;  /* Error */
        }
        LOG_INFO("IRQ %d configured successfully", irq);
        return irq;
    }
    
    LOG_ERROR("No interrupt handler provided");
    return 0xFE;  /* Error */
}