/**
 * @file pci_multiplex.c
 * @brief INT 2Fh multiplex API for PCI BIOS shim runtime control
 * 
 * Provides a multiplex interface for enabling/disabling the PCI BIOS shim
 * at runtime, querying status, and retrieving statistics. Uses INT 2Fh
 * which is the standard DOS multiplex interrupt for TSR communication.
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
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
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

/* State structure */
static struct {
    void (__interrupt __far *old_int2f)();
    bool installed;
    bool shim_enabled;
    uint32_t mplex_calls;
} mplex_state = {
    .installed = false,
    .shim_enabled = true,  /* Enabled by default */
    .mplex_calls = 0
};

/* External shim control functions */
extern bool pci_shim_is_enabled(void);
extern void pci_shim_set_enabled(bool enabled);
extern bool pci_shim_can_uninstall(void);
extern bool pci_shim_do_uninstall(void);

/**
 * @brief INT 2Fh multiplex handler
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
                pci_shim_get_stats(&total_calls, &fallback_calls);
                
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
 * @brief Install INT 2Fh multiplex handler
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
 * @brief Check if shim is enabled via multiplex
 */
bool multiplex_is_shim_enabled(void) {
    return mplex_state.shim_enabled;
}

/**
 * @brief Set shim enabled state via multiplex
 */
void multiplex_set_shim_enabled(bool enabled) {
    mplex_state.shim_enabled = enabled;
    pci_shim_set_enabled(enabled);
}

/**
 * @brief Get multiplex statistics
 */
void multiplex_get_stats(uint32_t *calls) {
    if (calls) {
        *calls = mplex_state.mplex_calls;
    }
}

/**
 * @brief Command-line utility interface
 * 
 * This function can be called from a separate utility program to
 * control the resident PCI shim via INT 2Fh.
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
                printf("  Fallback rate: %.1f%%\n", 
                       (fallback * 100.0) / total);
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