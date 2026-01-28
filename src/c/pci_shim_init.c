/**
 * @file pci_shim_init.c
 * @brief PCI BIOS shim layer - Initialization segment (OVERLAY)
 *
 * Contains PCI initialization, BIOS quirk detection, and one-time setup code.
 * This code can be discarded after initialization to save memory.
 *
 * Split from pci_shim.c on 2026-01-28 09:26:47
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

/* 32-bit I/O operations for PCI configuration */
#include "pci_io.h"

/* Alias inportd/outportd from pci_io module to local names for clarity */
#define inportd_asm  inportd
#define outportd_asm outportd

/* Broken function flags */
#define BROKEN_FIND_DEVICE      0x0004  /* B102 Find Device */
#define BROKEN_FIND_CLASS       0x0008  /* B103 Find Class */
#define BROKEN_READ_BYTE        0x0100  /* B108 Read Config Byte */
#define BROKEN_READ_WORD        0x0200  /* B109 Read Config Word */
#define BROKEN_READ_DWORD       0x0400  /* B10A Read Config Dword */
#define BROKEN_WRITE_BYTE       0x0800  /* B10B Write Config Byte */
#define BROKEN_WRITE_WORD       0x1000  /* B10C Write Config Word */
#define BROKEN_WRITE_DWORD      0x2000  /* B10D Write Config Dword */

/* Known broken BIOS database entry */
typedef struct {
    const char *vendor_string;  /* BIOS vendor ID string */
    const char *version_string; /* BIOS version string */
    uint16_t broken_functions;  /* Bitmask of broken subfunctions */
    const char *description;    /* Human-readable description */
} broken_bios_entry_t;

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

/* External reference to shim state defined in pci_shim_rt.c */
extern struct pci_shim_state {
    void (__interrupt __far *original_int1a)();
    bool installed;
    uint8_t mechanism;          /* 0=BIOS, 1=Mech#1, 2=Mech#2 */
    uint16_t broken_functions;  /* Detected broken functions */
    uint32_t shim_calls;        /* Statistics */
    uint32_t fallback_calls;    /* Statistics */
} shim_state;

/* External reference to ISR handler in pci_shim_rt.c */
extern void __interrupt __far pci_shim_handler(
    unsigned bp, unsigned di, unsigned si, unsigned ds,
    unsigned es, unsigned dx, unsigned cx, unsigned bx,
    unsigned ax, unsigned ip, unsigned cs, unsigned flags);

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
