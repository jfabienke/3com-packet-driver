/**
 * @file pcimux_rt.c
 * @brief INT 2Fh PCI multiplex API - Runtime segment (ROOT)
 *
 * Contains the multiplex interrupt handler, status query functions,
 * and runtime state management. This code stays resident and may be
 * called during packet processing.
 *
 * Split from pcimux.c on 2026-01-28 11:06:57
 *
 * Multiplex ID: 0xB1 (chosen to match PCI BIOS AH value)
 * Functions:
 *   AX=B100h: Installation check / Get version
 *   AX=B101h: Enable shim
 *   AX=B102h: Disable shim
 *   AX=B103h: Get statistics
 *   AX=B1FFh: Uninstall (if safe)
 */

#include <dos.h>

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
#define MPLEX_VERSION           0x0100  /* Version 1.00 */

/*
 * Global multiplex state - must remain in ROOT segment for ISR access
 */
struct mplex_state_t {
    void (__interrupt __far *old_int2f)();
    bool installed;
    bool shim_enabled;
    uint32_t mplex_calls;
};

/* Multiplex state - global, used by both rt and init modules */
struct mplex_state_t mplex_state = {
    NULL,   /* old_int2f */
    false,  /* installed */
    true,   /* shim_enabled - enabled by default */
    0       /* mplex_calls */
};

/* External shim control functions */
extern bool pci_shim_is_enabled(void);
extern void pci_shim_set_enabled(bool enabled);
extern bool pci_shim_can_uninstall(void);
extern bool pci_shim_do_uninstall(void);

/**
 * @brief INT 2Fh multiplex handler
 *
 * Handles runtime control of the PCI BIOS shim via INT 2Fh multiplex.
 * This handler stays resident and chains to the original handler for
 * non-matching multiplex IDs.
 */
void __interrupt __far multiplex_handler(
    unsigned bp, unsigned di, unsigned si, unsigned ds,
    unsigned es, unsigned dx, unsigned cx, unsigned bx,
    unsigned ax, unsigned ip, unsigned cs, unsigned flags) {

    uint8_t ah_val = (ax >> 8) & 0xFF;
    uint8_t al_val = ax & 0xFF;

    /* Check if this is our multiplex ID */
    if (ah_val != MPLEX_ID) {
        /* Not ours, chain to next handler */
        _chain_intr(mplex_state.old_int2f);
        return;
    }

    mplex_state.mplex_calls++;

    /* Handle our functions */
    switch (al_val) {
        case MPLEX_INSTALL_CHECK:
            /* Installation check - return signature and version */
            ax = 0x00FF;  /* AL=FFh means installed */
            bx = MPLEX_SIGNATURE;  /* BX='PC' */
            cx = MPLEX_VERSION;    /* CX=version */
            dx = mplex_state.shim_enabled ? 0x0001 : 0x0000;  /* DX=status */
            si = 0x3C0D;  /* SI='3C' (3Com) */
            di = 0x5043;  /* DI='PC' (PCI) */
            break;

        case MPLEX_ENABLE_SHIM:
            /* Enable the shim */
            pci_shim_set_enabled(true);
            mplex_state.shim_enabled = true;
            ax = 0x0000;  /* Success */
            dx = 0x0001;  /* Enabled */
            LOG_INFO("PCI shim enabled via multiplex");
            break;

        case MPLEX_DISABLE_SHIM:
            /* Disable the shim */
            pci_shim_set_enabled(false);
            mplex_state.shim_enabled = false;
            ax = 0x0000;  /* Success */
            dx = 0x0000;  /* Disabled */
            LOG_INFO("PCI shim disabled via multiplex");
            break;

        case MPLEX_GET_STATS:
            /* Get statistics */
            {
                uint32_t total_calls, fallback_calls;
                /* Stack vars are near pointers - explicit cast for clarity */
                pci_shim_get_stats((uint32_t *)&total_calls,
                                   (uint32_t *)&fallback_calls);

                /* Return stats in registers */
                ax = 0x0000;  /* Success */
                /* Total calls in CX:BX */
                bx = (uint16_t)(total_calls & 0xFFFF);
                cx = (uint16_t)(total_calls >> 16);
                /* Fallback calls in DI:SI */
                si = (uint16_t)(fallback_calls & 0xFFFF);
                di = (uint16_t)(fallback_calls >> 16);
                /* Status in DX */
                dx = mplex_state.shim_enabled ? 0x0001 : 0x0000;
            }
            break;

        case MPLEX_UNINSTALL:
            /* Attempt to uninstall */
            if (!pci_shim_can_uninstall()) {
                ax = 0x0001;  /* Error - cannot uninstall */
                dx = 0xFFFF;  /* Reason: vectors hooked by others */
                LOG_WARNING("Cannot uninstall - vectors hooked");
            } else {
                /* Uninstall shim first */
                if (pci_shim_do_uninstall()) {
                    /* Then uninstall multiplex handler */
                    _dos_setvect(0x2F, mplex_state.old_int2f);
                    mplex_state.installed = false;
                    ax = 0x0000;  /* Success */
                    dx = 0x0000;  /* Uninstalled */
                    LOG_INFO("PCI shim and multiplex uninstalled");
                } else {
                    ax = 0x0001;  /* Error */
                    dx = 0xFFFE;  /* Reason: shim uninstall failed */
                }
            }
            break;

        default:
            /* Unknown function */
            ax = 0x0001;  /* Error - function not supported */
            break;
    }
}

/**
 * @brief Check if shim is enabled via multiplex
 *
 * @return true if the shim is currently enabled
 */
bool multiplex_is_shim_enabled(void) {
    return mplex_state.shim_enabled;
}

/**
 * @brief Set shim enabled state via multiplex
 *
 * @param enabled true to enable, false to disable
 */
void multiplex_set_shim_enabled(bool enabled) {
    mplex_state.shim_enabled = enabled;
    pci_shim_set_enabled(enabled);
}

/**
 * @brief Get multiplex statistics
 *
 * @param calls Pointer to receive total multiplex call count (may be NULL)
 */
void multiplex_get_stats(uint32_t *calls) {
    if (calls) {
        *calls = mplex_state.mplex_calls;
    }
}
