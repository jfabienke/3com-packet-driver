/**
 * @file pci_shim_rt.c
 * @brief PCI BIOS shim layer - Runtime segment (ROOT)
 *
 * Contains PCI I/O wrapper functions, runtime register access, and ISR code
 * that may be called during packet processing. This code stays resident.
 *
 * Split from pci_shim.c on 2026-01-28 09:26:47
 *
 * Based on the layered shimming approach for maximum compatibility.
 */

#include <dos.h>
#include <conio.h>

/* C89 compatibility - include portability header for types */
#include "portabl.h"

/* Try standard headers for non-Watcom compilers */
#ifndef __WATCOMC__
#include <stdint.h>
#include <stdbool.h>
#endif

#include "pci_bios.h"
#include "diag.h"

/* PCI Configuration Mechanism #1 ports (primary, universal) */
#define PCI_MECH1_CONFIG_ADDR   0xCF8   /* Configuration address */
#define PCI_MECH1_CONFIG_DATA   0xCFC   /* Configuration data */
#define PCI_MECH1_ENABLE        0x80000000  /* Enable bit */

/* PCI Configuration Mechanism #2 ports (obsolete, fallback only) */
#define PCI_MECH2_ENABLE_REG    0xCF8   /* Enable/CSE register */
#define PCI_MECH2_FORWARD_REG   0xCFA   /* Forward register */
#define PCI_MECH2_CONFIG_BASE   0xC000  /* Configuration space base */

/* 32-bit I/O operations for PCI configuration.
 * Use the external assembly implementation from pci_io.asm which properly
 * handles 32-bit I/O in 16-bit real mode using .386 instructions.
 * The assembly module returns 32-bit values in DX:AX per Watcom calling convention.
 */
#include "pci_io.h"

/* Alias inportd/outportd from pci_io module to local names for clarity */
#define inportd_asm  inportd
#define outportd_asm outportd

/*
 * Global shim state - must remain in ROOT segment for ISR access
 */
struct pci_shim_state {
    void (__interrupt __far *original_int1a)();
    bool installed;
    uint8_t mechanism;          /* 0=BIOS, 1=Mech#1, 2=Mech#2 */
    uint16_t broken_functions;  /* Detected broken functions */
    uint32_t shim_calls;        /* Statistics */
    uint32_t fallback_calls;    /* Statistics */
};

/* Shim state - global, used by both rt and init modules */
struct pci_shim_state shim_state = {0};

/**
 * @brief Read PCI configuration using Mechanism #1 (preferred)
 */
bool mech1_read_config(uint8_t bus, uint8_t dev, uint8_t func,
                       uint8_t offset, uint32_t *value, uint8_t size) {
    uint32_t address;
    uint32_t data;

    /* Build configuration address */
    address = PCI_MECH1_ENABLE |
              ((uint32_t)bus << 16) |
              ((uint32_t)dev << 11) |
              ((uint32_t)func << 8) |
              (offset & 0xFC);  /* Dword aligned */

    /* Disable interrupts for atomic access */
    _disable();

    /* Write address */
    outportd_asm(PCI_MECH1_CONFIG_ADDR, address);

    /* Read data */
    data = inportd_asm(PCI_MECH1_CONFIG_DATA);

    /* Re-enable interrupts */
    _enable();

    /* Extract value based on size and offset */
    switch (size) {
        case 1:
            *value = (data >> ((offset & 3) * 8)) & 0xFF;
            break;
        case 2:
            if (offset & 1) return false;  /* Unaligned */
            *value = (data >> ((offset & 2) * 8)) & 0xFFFF;
            break;
        case 4:
            if (offset & 3) return false;  /* Unaligned */
            *value = data;
            break;
        default:
            return false;
    }

    return true;
}

/**
 * @brief Read PCI configuration using Mechanism #2 (obsolete fallback)
 */
bool mech2_read_config(uint8_t bus, uint8_t dev, uint8_t func,
                       uint8_t offset, uint32_t *value, uint8_t size) {
    uint16_t port;
    uint8_t enable;

    /* Mechanism #2 limitations */
    if (dev > 15) {
        LOG_DEBUG("Mech2: Device %d > 15, not supported", dev);
        return false;
    }

    /* Alignment check for word/dword */
    if ((size == 2 && (offset & 1)) || (size == 4 && (offset & 3))) {
        LOG_DEBUG("Mech2: Unaligned %d-byte read at offset 0x%02X", size, offset);
        return false;
    }

    /* Calculate port */
    port = PCI_MECH2_CONFIG_BASE | ((dev & 0x0F) << 8) | (offset & 0xFC);

    /* Disable interrupts for atomic access */
    _disable();

    /* Configure for access */
    outportb(PCI_MECH2_ENABLE_REG, 0x00);  /* Disable first */

    if (bus == 0) {
        /* Type 0 configuration cycle */
        enable = ((func & 0x07) << 1) | 0x01;  /* Function + enable bit */
    } else {
        /* Type 1 configuration cycle */
        outportb(PCI_MECH2_FORWARD_REG, bus);  /* Set bus */
        enable = ((func & 0x07) << 1) | 0x81;  /* Function + type1 + enable */
    }

    outportb(PCI_MECH2_ENABLE_REG, enable);

    /* Read value based on size */
    switch (size) {
        case 1:
            *value = inportb(port | (offset & 0x03));
            break;
        case 2:
            *value = inportw(port | (offset & 0x02));
            break;
        case 4:
            /* Mechanism #2 doesn't support 32-bit, read as two words */
            *value = inportw(port);
            *value |= ((uint32_t)inportw(port + 2)) << 16;
            break;
    }

    /* Disable access */
    outportb(PCI_MECH2_ENABLE_REG, 0x00);

    /* Re-enable interrupts */
    _enable();

    return true;
}

/**
 * @brief Write PCI configuration using Mechanism #1 (preferred)
 */
bool mech1_write_config(uint8_t bus, uint8_t dev, uint8_t func,
                        uint8_t offset, uint32_t value, uint8_t size) {
    uint32_t address;
    uint32_t data;
    uint8_t shift;

    /* Build configuration address */
    address = PCI_MECH1_ENABLE |
              ((uint32_t)bus << 16) |
              ((uint32_t)dev << 11) |
              ((uint32_t)func << 8) |
              (offset & 0xFC);

    /* Disable interrupts for atomic access */
    _disable();

    /* For byte/word writes, need read-modify-write */
    if (size < 4) {
        outportd_asm(PCI_MECH1_CONFIG_ADDR, address);
        data = inportd_asm(PCI_MECH1_CONFIG_DATA);

        switch (size) {
            case 1:
                shift = (offset & 3) * 8;
                data &= ~(0xFF << shift);
                data |= (value & 0xFF) << shift;
                break;
            case 2:
                if (offset & 1) {
                    _enable();
                    return false;  /* Unaligned */
                }
                shift = (offset & 2) * 8;
                data &= ~(0xFFFF << shift);
                data |= (value & 0xFFFF) << shift;
                break;
        }
    } else {
        if (offset & 3) {
            _enable();
            return false;  /* Unaligned */
        }
        data = value;
    }

    /* Write address and data */
    outportd_asm(PCI_MECH1_CONFIG_ADDR, address);
    outportd_asm(PCI_MECH1_CONFIG_DATA, data);

    /* Re-enable interrupts */
    _enable();

    return true;
}

/**
 * @brief Write PCI configuration using Mechanism #2 (obsolete fallback)
 */
bool mech2_write_config(uint8_t bus, uint8_t dev, uint8_t func,
                        uint8_t offset, uint32_t value, uint8_t size) {
    uint16_t port;
    uint8_t enable;

    /* Mechanism #2 limitations */
    if (dev > 15) {
        return false;
    }

    /* Alignment check */
    if ((size == 2 && (offset & 1)) || (size == 4 && (offset & 3))) {
        return false;
    }

    /* Calculate port */
    port = PCI_MECH2_CONFIG_BASE | ((dev & 0x0F) << 8) | (offset & 0xFC);

    /* Disable interrupts */
    _disable();

    /* Configure for access */
    outportb(PCI_MECH2_ENABLE_REG, 0x00);

    if (bus == 0) {
        enable = ((func & 0x07) << 1) | 0x01;  /* Type 0 */
    } else {
        outportb(PCI_MECH2_FORWARD_REG, bus);
        enable = ((func & 0x07) << 1) | 0x81;  /* Type 1 */
    }

    outportb(PCI_MECH2_ENABLE_REG, enable);

    /* Write value */
    switch (size) {
        case 1:
            outportb(port | (offset & 0x03), value & 0xFF);
            break;
        case 2:
            outportw(port | (offset & 0x02), value & 0xFFFF);
            break;
        case 4:
            /* Write as two words */
            outportw(port, value & 0xFFFF);
            outportw(port + 2, (value >> 16) & 0xFFFF);
            break;
    }

    /* Disable access */
    outportb(PCI_MECH2_ENABLE_REG, 0x00);

    /* Re-enable interrupts */
    _enable();

    return true;
}

/**
 * @brief PCI BIOS shim interrupt handler
 *
 * NOTE: This needs proper assembly wrapper for correct ISR behavior.
 * The C handler shown here is simplified.
 */
void __interrupt __far pci_shim_handler(
    unsigned bp, unsigned di, unsigned si, unsigned ds,
    unsigned es, unsigned dx, unsigned cx, unsigned bx,
    unsigned ax, unsigned ip, unsigned cs, unsigned flags) {

    uint8_t ah_val = (ax >> 8) & 0xFF;
    uint8_t al_val = ax & 0xFF;
    uint8_t bus, dev, func, offset;
    /* Use static to ensure near pointer address in data segment,
     * avoiding W112 far-to-near pointer truncation when passing to
     * mech1_read_config/mech2_read_config which expect near pointers */
    static uint32_t value;

    /* Only intercept PCI BIOS config read/write calls */
    if (ah_val != 0xB1 || al_val < 0x08 || al_val > 0x0D) {
        /* Not our concern - chain immediately */
        _chain_intr(shim_state.original_int1a);
        return;
    }

    shim_state.shim_calls++;

    /* Extract common parameters */
    bus = (bx >> 8) & 0xFF;
    dev = (bx >> 3) & 0x1F;
    func = bx & 0x07;
    offset = di & 0xFF;

    /* Check if this function is known broken */
    if (shim_state.broken_functions != 0) {
        /* C89: Declarations must be at start of block */
        uint16_t func_mask;
        bool success;

        func_mask = 1 << (al_val & 0x0F);
        success = false;

        if (shim_state.broken_functions & func_mask) {
            /* This function is broken, handle via our mechanism */
            shim_state.fallback_calls++;

            /* Choose mechanism based on what's available */
            if (shim_state.mechanism == 1) {
                /* Use Mechanism #1 */
                switch (al_val) {
                    case 0x08:  /* Read Config Byte */
                        success = mech1_read_config(bus, dev, func, offset, &value, 1);
                        if (success) cx = (cx & 0xFF00) | (value & 0xFF);
                        break;

                    case 0x09:  /* Read Config Word */
                        success = mech1_read_config(bus, dev, func, offset, &value, 2);
                        if (success) cx = value & 0xFFFF;
                        break;

                    case 0x0A:  /* Read Config Dword */
                        success = mech1_read_config(bus, dev, func, offset, &value, 4);
                        if (success) {
                            /* Return in ECX - needs assembly wrapper for full 32-bit */
                            cx = value & 0xFFFF;
                            dx = (value >> 16) & 0xFFFF;  /* High word in DX as workaround */
                        }
                        break;

                    case 0x0B:  /* Write Config Byte */
                        value = cx & 0xFF;
                        success = mech1_write_config(bus, dev, func, offset, value, 1);
                        break;

                    case 0x0C:  /* Write Config Word */
                        value = cx & 0xFFFF;
                        success = mech1_write_config(bus, dev, func, offset, value, 2);
                        break;

                    case 0x0D:  /* Write Config Dword */
                        /* Get full 32-bit value - needs assembly wrapper */
                        value = cx | ((uint32_t)dx << 16);
                        success = mech1_write_config(bus, dev, func, offset, value, 4);
                        break;
                }
            } else if (shim_state.mechanism == 2) {
                /* Use Mechanism #2 (limited) */
                switch (al_val) {
                    case 0x08:
                        success = mech2_read_config(bus, dev, func, offset, &value, 1);
                        if (success) cx = (cx & 0xFF00) | (value & 0xFF);
                        break;

                    case 0x09:
                        success = mech2_read_config(bus, dev, func, offset, &value, 2);
                        if (success) cx = value & 0xFFFF;
                        break;

                    case 0x0A:
                        success = mech2_read_config(bus, dev, func, offset, &value, 4);
                        if (success) {
                            cx = value & 0xFFFF;
                            dx = (value >> 16) & 0xFFFF;
                        }
                        break;

                    case 0x0B:
                        value = cx & 0xFF;
                        success = mech2_write_config(bus, dev, func, offset, value, 1);
                        break;

                    case 0x0C:
                        value = cx & 0xFFFF;
                        success = mech2_write_config(bus, dev, func, offset, value, 2);
                        break;

                    case 0x0D:
                        value = cx | ((uint32_t)dx << 16);
                        success = mech2_write_config(bus, dev, func, offset, value, 4);
                        break;
                }
            }

            if (success) {
                /* Set success status */
                ax = (ax & 0xFF00) | 0x00;  /* AH = SUCCESSFUL */
                flags &= ~0x01;  /* Clear carry flag */
                return;  /* Don't chain */
            } else {
                /* Set appropriate error */
                ax = (ax & 0xFF00) | 0x87;  /* AH = BAD_REGISTER_NUMBER */
                flags |= 0x01;  /* Set carry flag */
                return;  /* Don't chain */
            }
        }
    }

    /* Either not broken or mechanism failed, chain to BIOS */
    _chain_intr(shim_state.original_int1a);
}

/**
 * @brief Get shim statistics for diagnostics
 */
void pci_shim_get_stats(uint32_t *total_calls, uint32_t *fallback_calls) {
    if (total_calls) *total_calls = shim_state.shim_calls;
    if (fallback_calls) *fallback_calls = shim_state.fallback_calls;
}
