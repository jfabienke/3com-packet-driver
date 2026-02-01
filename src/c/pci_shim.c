/**
 * @file pci_shim.c
 * @brief PCI BIOS shim layer for handling broken/buggy BIOS implementations
 * 
 * Provides a transparent INT 1Ah hook that intercepts PCI BIOS calls and
 * selectively overrides broken functions while chaining to the original BIOS
 * for working functions. Includes Mechanism #2 fallback for real-mode safety.
 * 
 * Based on the layered shimming approach for maximum compatibility.
 */

#include <dos.h>
#include <conio.h>
#include <string.h>

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

/* Known broken BIOS database entry */
typedef struct {
    const char *vendor_string;  /* BIOS vendor ID string */
    const char *version_string; /* BIOS version string */
    uint16_t broken_functions;  /* Bitmask of broken subfunctions */
    const char *description;    /* Human-readable description */
} broken_bios_entry_t;

/* Broken function flags */
#define BROKEN_FIND_DEVICE      0x0004  /* B102 Find Device */
#define BROKEN_FIND_CLASS       0x0008  /* B103 Find Class */
#define BROKEN_READ_BYTE        0x0100  /* B108 Read Config Byte */
#define BROKEN_READ_WORD        0x0200  /* B109 Read Config Word */
#define BROKEN_READ_DWORD       0x0400  /* B10A Read Config Dword */
#define BROKEN_WRITE_BYTE       0x0800  /* B10B Write Config Byte */
#define BROKEN_WRITE_WORD       0x1000  /* B10C Write Config Word */
#define BROKEN_WRITE_DWORD      0x2000  /* B10D Write Config Dword */

/*
 * Init-only BIOS database. With -zc compiler flag, the string
 * literals go to code segment automatically.
 */

/* Known broken BIOS database */
static const broken_bios_entry_t broken_bios_db[] = {
    /* Award BIOSes */
    {"Award", "4.51PG", BROKEN_READ_WORD | BROKEN_WRITE_WORD, 
     "Award 4.51PG - Word access broken"},
    {"Award", "4.50G", BROKEN_FIND_DEVICE, 
     "Award 4.50G - Find device returns wrong bus"},
    
    /* Phoenix BIOSes */
    {"Phoenix", "4.0 Release 6.0", BROKEN_FIND_DEVICE | BROKEN_FIND_CLASS,
     "Phoenix 4.0 R6.0 - Device enumeration issues"},
    
    /* AMI BIOSes */
    {"AMI", "1.00.12.DT0", 0xFFFF,
     "AMI 1.00.12.DT0 - All functions unreliable, use mechanisms"},
    
    /* Generic patterns */
    {"Award", "4.5", BROKEN_READ_WORD | BROKEN_WRITE_WORD,
     "Award 4.5x series - Word access issues"},
    
    {NULL, NULL, 0, NULL}  /* Terminator */
};

/* Shim state */
static struct {
    void (__interrupt __far *original_int1a)();
    bool installed;
    uint8_t mechanism;          /* 0=BIOS, 1=Mech#1, 2=Mech#2 */
    uint16_t broken_functions;  /* Detected broken functions */
    uint32_t shim_calls;        /* Statistics */
    uint32_t fallback_calls;    /* Statistics */
} shim_state = {0};

/* Forward declarations */
static bool detect_broken_bios(void);
static uint8_t detect_pci_mechanism(void);
static bool mech2_read_config(uint8_t bus, uint8_t dev, uint8_t func, 
                              uint8_t offset, uint32_t *value, uint8_t size);
static bool mech2_write_config(uint8_t bus, uint8_t dev, uint8_t func,
                               uint8_t offset, uint32_t value, uint8_t size);

/**
 * @brief Behavioral test for broken BIOS functions
 */
static bool test_bios_behavior(void) {
    uint16_t vendor_id, device_id;
    uint32_t vendor_device;
    uint8_t bus = 0, dev = 0, func = 0;
    bool found_device = false;
    bool has_issues = false;
    /* C89: All declarations must be at block start */
    uint8_t vid_low, vid_high;
    uint16_t vid_word;
    
    /* Find a PCI device to test with */
    for (dev = 0; dev < 32 && !found_device; dev++) {
        vendor_id = pci_read_config_word(bus, dev, func, PCI_VENDOR_ID);
        if (vendor_id != 0xFFFF && vendor_id != 0x0000) {
            found_device = true;
            device_id = pci_read_config_word(bus, dev, func, PCI_DEVICE_ID);
            LOG_DEBUG("Testing with device %02X:%02X.%X (VID:DID %04X:%04X)",
                     bus, dev, func, vendor_id, device_id);
            break;
        }
    }
    
    if (!found_device) {
        LOG_DEBUG("No PCI device found for behavioral testing");
        return false;
    }
    
    /* Test 1: Compare byte vs word reads */
    vid_low = pci_read_config_byte(bus, dev, func, PCI_VENDOR_ID);
    vid_high = pci_read_config_byte(bus, dev, func, PCI_VENDOR_ID + 1);
    vid_word = pci_read_config_word(bus, dev, func, PCI_VENDOR_ID);
    
    if (vid_word != ((vid_high << 8) | vid_low)) {
        LOG_WARNING("BIOS word read inconsistent with byte reads");
        shim_state.broken_functions |= BROKEN_READ_WORD;
        has_issues = true;
    }
    
    /* Test 2: Compare word vs dword reads */
    vendor_device = pci_read_config_dword(bus, dev, func, PCI_VENDOR_ID);
    if (vendor_device != ((uint32_t)device_id << 16 | vendor_id)) {
        LOG_WARNING("BIOS dword read inconsistent with word reads");
        shim_state.broken_functions |= BROKEN_READ_DWORD;
        has_issues = true;
    }
    
    /* Test 3: Verify write capability (use scratch register if available) */
    /* Skip write test for safety - could use latency timer as test */
    
    return has_issues;
}

/**
 * @brief Check if BIOS vendor/version matches known broken entries
 */
static bool detect_broken_bios(void) {
    char far *bios_area;
    char vendor_buf[64] = {0};
    char version_buf[64] = {0};
    int i, j, offset;
    bool found_vendor = false;
    bool behavioral_issues;
    
    /* Scan BIOS area for vendor strings (more robust than fixed offsets) */
    for (offset = 0xE000; offset < 0xF000 && !found_vendor; offset += 16) {
        bios_area = MK_FP(0xF000, offset);
        
        /* Look for common vendor strings */
        /* Use _fmemcmp for far pointer comparison to avoid W112 truncation */
        if (_fmemcmp(bios_area, "Award", 5) == 0 ||
            _fmemcmp(bios_area, "Phoenix", 7) == 0 ||
            _fmemcmp(bios_area, "AMI", 3) == 0) {
            
            /* Found vendor string, copy it */
            for (i = 0; i < 63 && bios_area[i] >= 0x20 && bios_area[i] < 0x7F; i++) {
                vendor_buf[i] = bios_area[i];
            }
            found_vendor = true;
            LOG_DEBUG("Found BIOS vendor string at F000:%04X: %s", offset, vendor_buf);
        }
    }
    
    /* Check BIOS date */
    bios_area = MK_FP(0xF000, 0xFFF5);
    if (bios_area[2] == '/' && bios_area[5] == '/') {
        /* Looks like a date MM/DD/YY */
        /* C89: Declaration moved to block start */
        int year;
        year = (bios_area[6] - '0') * 10 + (bios_area[7] - '0');
        if (year < 96) {  /* Pre-1996 */
            LOG_WARNING("Pre-1996 BIOS detected (19%02d), enabling compatibility mode", year);
            shim_state.broken_functions = BROKEN_READ_WORD | BROKEN_WRITE_WORD;
        }
    }
    
    /* Check against known broken BIOS database */
    if (found_vendor) {
        for (i = 0; broken_bios_db[i].vendor_string != NULL; i++) {
            if (strstr(vendor_buf, broken_bios_db[i].vendor_string) != NULL) {
                shim_state.broken_functions |= broken_bios_db[i].broken_functions;
                LOG_WARNING("Known problematic BIOS: %s", broken_bios_db[i].description);
            }
        }
    }
    
    /* Perform behavioral testing */
    behavioral_issues = test_bios_behavior();
    if (behavioral_issues) {
        LOG_WARNING("BIOS behavioral issues detected, shim enabled for affected functions");
    }
    
    return (shim_state.broken_functions != 0);
}

/* 32-bit I/O operations for PCI configuration.
 * Use the external assembly implementation from pci_io.asm which properly
 * handles 32-bit I/O in 16-bit real mode using .386 instructions.
 * The assembly module returns 32-bit values in DX:AX per Watcom calling convention.
 */
#include "pci_io.h"

/* Alias inportd/outportd from pci_io module to local names for clarity */
#define inportd_asm  inportd
#define outportd_asm outportd

/**
 * @brief Detect available PCI configuration mechanisms
 */
static uint8_t detect_pci_mechanism(void) {
    uint32_t save_cf8;
    uint8_t save_cfa, save_cf8_byte;
    uint8_t mechanism = 0;
    uint8_t bios_mechs;
    union REGS regs;
    struct SREGS sregs;

    /* First check BIOS for supported mechanisms */
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    regs.h.ah = 0xB1;
    regs.h.al = 0x01;  /* Installation check */
    int86x(0x1A, &regs, &regs, &sregs);

#ifdef __WATCOMC__
    /* Watcom 16-bit mode: union REGS only has 16-bit dx, not 32-bit edx.
     * Check low 16-bits of EDX for 'CI' (0x4350) - 'PCI ' = 0x20494350 */
    if (regs.x.cflag == 0 && regs.x.dx == 0x4350) {
        bios_mechs = regs.h.al;
        LOG_DEBUG("BIOS reports mechanisms: 0x%02X", bios_mechs);

        /* Bit 0 = Mechanism #1, Bit 1 = Mechanism #2 */
        if (bios_mechs & 0x01) mechanism |= 0x01;
        if (bios_mechs & 0x02) mechanism |= 0x02;
    }
#else
    /* Non-Watcom: use full 32-bit edx field */
    if (regs.x.cflag == 0 && regs.x.edx == 0x20494350) {
        bios_mechs = regs.h.al;
        LOG_DEBUG("BIOS reports mechanisms: 0x%02X", bios_mechs);

        /* Bit 0 = Mechanism #1, Bit 1 = Mechanism #2 */
        if (bios_mechs & 0x01) mechanism |= 0x01;
        if (bios_mechs & 0x02) mechanism |= 0x02;
    }
#endif
    
    /* Independently verify Mechanism #1 (preferred) */
    save_cf8 = inportd_asm(0xCF8);
    outportd_asm(0xCF8, 0x80000000);  /* Enable bit pattern */
    if ((inportd_asm(0xCF8) & 0x80000000) != 0) {
        mechanism |= 0x01;
        LOG_DEBUG("Mechanism #1 verified by probe");
    }
    outportd_asm(0xCF8, save_cf8);  /* Restore */
    
    /* Only test Mechanism #2 if #1 not available */
    if (!(mechanism & 0x01)) {
        save_cf8_byte = inportb(0xCF8);
        save_cfa = inportb(0xCFA);
        
        /* Try to enable Mechanism #2 */
        outportb(0xCF8, 0x00);  /* Disable first */
        outportb(0xCFA, 0x00);  /* Clear forward */
        outportb(0xCF8, 0x01);  /* Enable bit for bus 0 */
        
        if ((inportb(0xCF8) & 0x01) != 0) {
            mechanism |= 0x02;
            LOG_DEBUG("Mechanism #2 detected (obsolete)");
        }
        
        /* Restore */
        outportb(0xCF8, save_cf8_byte);
        outportb(0xCFA, save_cfa);
    }
    
    return mechanism;
}

/**
 * @brief Read PCI configuration using Mechanism #1 (preferred)
 */
static bool mech1_read_config(uint8_t bus, uint8_t dev, uint8_t func,
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
static bool mech2_read_config(uint8_t bus, uint8_t dev, uint8_t func,
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
static bool mech1_write_config(uint8_t bus, uint8_t dev, uint8_t func,
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
static bool mech2_write_config(uint8_t bus, uint8_t dev, uint8_t func,
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
 * @brief Install PCI BIOS shim
 */
bool pci_shim_install(void) {
    uint8_t mechanisms;
    
    if (shim_state.installed) {
        LOG_WARNING("PCI shim already installed");
        return true;
    }
    
    /* Detect if we have a broken BIOS */
    if (detect_broken_bios()) {
        LOG_INFO("Installing PCI BIOS shim for broken BIOS");
    } else {
        LOG_INFO("BIOS appears functional, shim in monitoring mode");
        shim_state.broken_functions = 0;  /* Monitor only */
    }
    
    /* Detect available mechanisms - prefer #1 */
    mechanisms = detect_pci_mechanism();
    if (mechanisms & 0x01) {
        shim_state.mechanism = 1;  /* Mechanism #1 (universal, preferred) */
        LOG_INFO("Using PCI Mechanism #1 for fallback (32-bit I/O)");
    } else if (mechanisms & 0x02) {
        shim_state.mechanism = 2;  /* Mechanism #2 (obsolete, limited) */
        LOG_WARNING("Using obsolete PCI Mechanism #2 (limited to 16 devices)");
    } else {
        LOG_WARNING("No PCI mechanisms detected, shim will monitor only");
        shim_state.mechanism = 0;
    }
    
    /* Hook INT 1Ah */
    shim_state.original_int1a = _dos_getvect(0x1A);
    _dos_setvect(0x1A, pci_shim_handler);
    
    shim_state.installed = true;
    LOG_INFO("PCI BIOS shim installed successfully");
    
    return true;
}

/**
 * @brief Uninstall PCI BIOS shim
 */
bool pci_shim_uninstall(void) {
    if (!shim_state.installed) {
        return false;
    }
    
    /* Restore original INT 1Ah */
    _dos_setvect(0x1A, shim_state.original_int1a);
    
    LOG_INFO("PCI shim stats: %lu calls, %lu fallbacks",
             shim_state.shim_calls, shim_state.fallback_calls);
    
    shim_state.installed = false;
    return true;
}

/**
 * @brief Get shim statistics for diagnostics
 */
void pci_shim_get_stats(uint32_t *total_calls, uint32_t *fallback_calls) {
    if (total_calls) *total_calls = shim_state.shim_calls;
    if (fallback_calls) *fallback_calls = shim_state.fallback_calls;
}
