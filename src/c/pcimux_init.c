/**
 * @file pcimux_init.c
 * @brief INT 2Fh PCI multiplex API - Initialization segment (OVERLAY)
 *
 * Contains multiplex handler installation, uninstallation, and the
 * command-line control utility interface. This code can be discarded
 * after initialization to save memory.
 *
 * Split from pcimux.c on 2026-01-28 11:06:57
 *
 * Multiplex ID: 0xB1 (chosen to match PCI BIOS AH value)
 */

#include <dos.h>
#include "dos_io.h"
#include <string.h>

/* C89 compatibility - include portability header for types */
#include "portabl.h"

/* Try standard headers for non-Watcom compilers */
#ifndef __WATCOMC__
#include <stdint.h>
#include <stdbool.h>
#endif

#include "pci_shim.h"
#include "logging.h"

/* Multiplex function codes */
#define MPLEX_ID                0xB1    /* Our multiplex ID */
#define MPLEX_INSTALL_CHECK     0x00    /* Installation check */
#define MPLEX_ENABLE_SHIM       0x01    /* Enable shim */
#define MPLEX_DISABLE_SHIM      0x02    /* Disable shim */
#define MPLEX_GET_STATS         0x03    /* Get statistics */
#define MPLEX_UNINSTALL         0xFF    /* Attempt uninstall */

/* Signature for installation check */
#define MPLEX_SIGNATURE         0x5043  /* 'PC' */

/* External reference to multiplex state defined in pcimux_rt.c */
extern struct mplex_state_t {
    void (__interrupt __far *old_int2f)();
    bool installed;
    bool shim_enabled;
    uint32_t mplex_calls;
} mplex_state;

/* External reference to ISR handler in pcimux_rt.c */
extern void __interrupt __far multiplex_handler(
    unsigned bp, unsigned di, unsigned si, unsigned ds,
    unsigned es, unsigned dx, unsigned cx, unsigned bx,
    unsigned ax, unsigned ip, unsigned cs, unsigned flags);

/**
 * @brief Install INT 2Fh multiplex handler
 *
 * Installs our multiplex handler on INT 2Fh after verifying that
 * our multiplex ID is not already in use.
 *
 * @return true if installation succeeded, false otherwise
 */
bool multiplex_install(void) {
    union REGS regs;
    struct SREGS sregs;

    if (mplex_state.installed) {
        LOG_WARNING("Multiplex handler already installed");
        return true;
    }

    /* Check if our multiplex ID is already in use */
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));

    regs.x.ax = (MPLEX_ID << 8) | MPLEX_INSTALL_CHECK;
    int86x(0x2F, &regs, &regs, &sregs);

    if (regs.h.al == 0xFF) {
        /* Already installed by someone else */
        LOG_ERROR("Multiplex ID 0x%02X already in use", MPLEX_ID);

        /* Check if it's another instance of our driver */
        if (regs.x.bx == MPLEX_SIGNATURE) {
            LOG_INFO("Another instance of PCI shim detected");
            return false;
        }

        /* Try alternate multiplex IDs */
        LOG_INFO("Trying alternate multiplex ID...");
        /* Could implement scanning for free ID here */
        return false;
    }

    /* Hook INT 2Fh */
    mplex_state.old_int2f = _dos_getvect(0x2F);
    _dos_setvect(0x2F, multiplex_handler);

    mplex_state.installed = true;
    LOG_INFO("Multiplex handler installed on INT 2Fh, ID=0x%02X", MPLEX_ID);

    return true;
}

/**
 * @brief Uninstall INT 2Fh multiplex handler
 *
 * Removes our multiplex handler from INT 2Fh, restoring the original
 * handler. Only succeeds if we are still the active handler.
 *
 * @return true if uninstallation succeeded, false otherwise
 */
bool multiplex_uninstall(void) {
    void (__interrupt __far *current_int2f)();

    if (!mplex_state.installed) {
        return false;
    }

    /* Check if we're still the active handler */
    current_int2f = _dos_getvect(0x2F);
    if (current_int2f != multiplex_handler) {
        LOG_ERROR("Cannot uninstall - INT 2Fh hooked by another program");
        return false;
    }

    /* Restore original handler */
    _dos_setvect(0x2F, mplex_state.old_int2f);

    LOG_INFO("Multiplex handler uninstalled (handled %lu calls)",
             mplex_state.mplex_calls);

    mplex_state.installed = false;
    return true;
}

/**
 * @brief Command-line utility interface
 *
 * This function can be called from a separate utility program to
 * control the resident PCI shim via INT 2Fh.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int multiplex_control(int argc, char *argv[]) {
    union REGS regs;
    struct SREGS sregs;

    if (argc < 2) {
        printf("PCI Shim Control Utility\n");
        printf("Usage: pcishim [command]\n");
        printf("Commands:\n");
        printf("  status  - Show shim status\n");
        printf("  enable  - Enable PCI BIOS shim\n");
        printf("  disable - Disable PCI BIOS shim\n");
        printf("  stats   - Show statistics\n");
        printf("  remove  - Uninstall shim (if safe)\n");
        return 1;
    }

    /* Check if shim is installed */
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));

    regs.x.ax = (MPLEX_ID << 8) | MPLEX_INSTALL_CHECK;
    int86x(0x2F, &regs, &regs, &sregs);

    if (regs.h.al != 0xFF || regs.x.bx != MPLEX_SIGNATURE) {
        printf("PCI shim not installed\n");
        return 2;
    }

    /* Execute command */
    if (stricmp(argv[1], "status") == 0) {
        printf("PCI BIOS Shim Status:\n");
        printf("  Version: %d.%02d\n", regs.h.ch, regs.h.cl);
        printf("  Status: %s\n", regs.x.dx ? "Enabled" : "Disabled");

    } else if (stricmp(argv[1], "enable") == 0) {
        regs.x.ax = (MPLEX_ID << 8) | MPLEX_ENABLE_SHIM;
        int86x(0x2F, &regs, &regs, &sregs);
        printf("PCI shim %s\n", regs.x.ax == 0 ? "enabled" : "error");

    } else if (stricmp(argv[1], "disable") == 0) {
        regs.x.ax = (MPLEX_ID << 8) | MPLEX_DISABLE_SHIM;
        int86x(0x2F, &regs, &regs, &sregs);
        printf("PCI shim %s\n", regs.x.ax == 0 ? "disabled" : "error");

    } else if (stricmp(argv[1], "stats") == 0) {
        regs.x.ax = (MPLEX_ID << 8) | MPLEX_GET_STATS;
        int86x(0x2F, &regs, &regs, &sregs);

        if (regs.x.ax == 0) {
            uint32_t total = ((uint32_t)regs.x.cx << 16) | regs.x.bx;
            uint32_t fallback = ((uint32_t)regs.x.di << 16) | regs.x.si;

            printf("PCI BIOS Shim Statistics:\n");
            printf("  Total calls: %lu\n", total);
            printf("  Fallback calls: %lu\n", fallback);
            if (total > 0) {
                printf("  Fallback rate: %lu.%lu%%\n",
                       (fallback * 100) / total,
                       ((fallback * 1000) / total) % 10);
            }
        }

    } else if (stricmp(argv[1], "remove") == 0) {
        regs.x.ax = (MPLEX_ID << 8) | MPLEX_UNINSTALL;
        int86x(0x2F, &regs, &regs, &sregs);

        if (regs.x.ax == 0) {
            printf("PCI shim uninstalled successfully\n");
        } else {
            printf("Cannot uninstall: ");
            if (regs.x.dx == 0xFFFF) {
                printf("vectors hooked by other programs\n");
            } else {
                printf("error code 0x%04X\n", regs.x.dx);
            }
        }

    } else {
        printf("Unknown command: %s\n", argv[1]);
        return 1;
    }

    return 0;
}
