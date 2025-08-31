/**
 * @file tsr_manager.c
 * @brief TSR relocation and interrupt management functions
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file implements TSR (Terminate and Stay Resident) relocation
 * and precise interrupt control functions for the boot sequence.
 */

#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "../../include/main.h"
#include "../../include/hardware.h"
#include "../../include/logging.h"
#include "../../include/config.h"

/* TSR relocation constants */
#define TSR_SIGNATURE_OFFSET    0x100  /* Offset to check for signature */
#define TSR_MAX_SIZE           0x1000  /* Maximum TSR size (4KB) */
#define TSR_PREFERRED_SEGMENT  0x9000  /* Preferred high memory segment */

/* Interrupt control flags */
static struct {
    uint16_t saved_interrupt_mask;
    uint8_t saved_elcr1;           /* Saved ELCR1 state (IRQ 0-7) */
    uint8_t saved_elcr2;           /* Saved ELCR2 state (IRQ 8-15) */
    int elcr_saved;                /* ELCR values saved */
    int elcr_present;              /* ELCR detected */
    int interrupts_enabled;
    uint16_t driver_irq;
    uint8_t irq_type;              /* Edge or level triggered */
} g_interrupt_state = {0};

/* ELCR (Edge/Level Control Register) ports */
#define ELCR_PORT1      0x4D0      /* IRQ 0-7 */
#define ELCR_PORT2      0x4D1      /* IRQ 8-15 */

/* IRQ trigger types */
#define IRQ_EDGE        0
#define IRQ_LEVEL       1

/* System-critical IRQs that should not be modified */
#define IRQ_SYSTEM_TIMER    0
#define IRQ_KEYBOARD        1
#define IRQ_CASCADE         2
#define IRQ_RTC             8

/**
 * @brief Relocate TSR to optimal memory location (Phase 11)
 * 
 * Attempts to move the TSR code to high memory to free up
 * conventional memory for applications.
 * 
 * @return 0 on success, negative on error
 */
int tsr_relocate(void) {
    uint16_t current_segment;
    uint16_t target_segment;
    uint16_t tsr_size;
    void far *source;
    void far *dest;
    
    log_info("Attempting TSR relocation");
    
    /* Get current code segment */
    _asm {
        mov ax, cs
        mov current_segment, ax
    }
    
    log_info("  Current segment: 0x%04X", current_segment);
    
    /* Check if already in high memory */
    if (current_segment >= 0x8000) {
        log_info("  Already in high memory, no relocation needed");
        return 0;
    }
    
    /* Calculate TSR size */
    tsr_size = TSR_MAX_SIZE;  /* Use conservative estimate */
    
    /* Try to allocate memory in upper memory area */
    target_segment = TSR_PREFERRED_SEGMENT;
    
    /* Check if target memory is available */
    _asm {
        push es
        mov ax, target_segment
        mov es, ax
        mov ax, word ptr es:[0]
        pop es
    }
    
    /* For now, skip actual relocation (complex operation) */
    log_info("  TSR relocation deferred (requires complex memory management)");
    
    /* In production, this would:
     * 1. Allocate target memory block
     * 2. Copy TSR code and data to new location
     * 3. Update interrupt vectors
     * 4. Update segment registers
     * 5. Free original memory
     */
    
    return -1;  /* Indicate relocation was not performed */
}

/**
 * @brief Detect ELCR presence
 * 
 * @return true if ELCR is present
 */
static int detect_elcr(void) {
    uint8_t test_val1, test_val2, orig_val;
    
    /* ELCR only exists on EISA/PCI systems */
    /* Try to detect by reading and verifying consistency */
    
    _asm {
        cli
        /* Read original value */
        mov dx, ELCR_PORT1
        in al, dx
        mov orig_val, al
        
        /* Write test pattern */
        mov al, 0x55
        out dx, al
        jmp short $+2      ; I/O delay
        
        /* Read back */
        in al, dx
        mov test_val1, al
        
        /* Write inverted pattern */
        mov al, 0xAA
        out dx, al
        jmp short $+2      ; I/O delay
        
        /* Read back */
        in al, dx
        mov test_val2, al
        
        /* Restore original */
        mov al, orig_val
        out dx, al
        sti
    }
    
    /* ELCR present if we can read back what we wrote */
    /* But bits 0,1,2 are usually read-only */
    if ((test_val1 & 0xF8) == 0x50 && (test_val2 & 0xF8) == 0xA8) {
        return 1;  /* ELCR detected */
    }
    
    return 0;  /* No ELCR or not writable */
}

/**
 * @brief Save ELCR state for restoration
 */
static void save_elcr_state(void) {
    if (g_interrupt_state.elcr_saved) {
        return;
    }
    
    _asm {
        cli
        /* Save ELCR1 */
        mov dx, ELCR_PORT1
        in al, dx
        mov g_interrupt_state.saved_elcr1, al
        
        /* Save ELCR2 */
        mov dx, ELCR_PORT2
        in al, dx
        mov g_interrupt_state.saved_elcr2, al
        sti
    }
    
    g_interrupt_state.elcr_saved = 1;
}

/**
 * @brief Restore ELCR state
 */
static void restore_elcr_state(void) {
    if (!g_interrupt_state.elcr_saved || !g_interrupt_state.elcr_present) {
        return;
    }
    
    _asm {
        cli
        /* Restore ELCR1 */
        mov dx, ELCR_PORT1
        mov al, g_interrupt_state.saved_elcr1
        out dx, al
        
        /* Restore ELCR2 */
        mov dx, ELCR_PORT2
        mov al, g_interrupt_state.saved_elcr2
        out dx, al
        sti
    }
    
    log_info("ELCR restored to original state");
}

/**
 * @brief Program ELCR for correct interrupt trigger mode
 * 
 * GPT-5: Enhanced with detection, save/restore, and critical IRQ protection
 * 
 * @param irq IRQ number
 * @param trigger_type IRQ_EDGE or IRQ_LEVEL
 * @param force Force change even if already set
 */
static void program_elcr(uint8_t irq, uint8_t trigger_type, int force) {
    uint8_t elcr_val, orig_val;
    uint16_t elcr_port;
    uint8_t irq_bit;
    
    /* Don't modify system-critical IRQs */
    if (irq == IRQ_SYSTEM_TIMER || irq == IRQ_KEYBOARD || 
        irq == IRQ_CASCADE || irq == IRQ_RTC) {
        log_warning("  Refusing to modify system IRQ %d", irq);
        return;
    }
    
    /* Check if ELCR is present */
    if (!g_interrupt_state.elcr_present) {
        log_info("  ELCR not present - skipping programming");
        return;
    }
    
    /* Determine ELCR port and bit */
    if (irq < 8) {
        elcr_port = ELCR_PORT1;
        irq_bit = irq;
    } else {
        elcr_port = ELCR_PORT2;
        irq_bit = irq - 8;
    }
    
    _asm cli;  /* Disable interrupts */
    
    /* Read current ELCR value */
    _asm {
        mov dx, elcr_port
        in al, dx
        mov elcr_val, al
        mov orig_val, al
    }
    
    /* Check if already set correctly */
    if (!force) {
        int current_type = (elcr_val & (1 << irq_bit)) ? IRQ_LEVEL : IRQ_EDGE;
        if (current_type == trigger_type) {
            log_info("  IRQ%d already %s-triggered", 
                    irq, trigger_type == IRQ_LEVEL ? "level" : "edge");
            _asm sti;
            return;
        }
    }
    
    /* Set trigger type */
    if (trigger_type == IRQ_LEVEL) {
        elcr_val |= (1 << irq_bit);   /* Set bit for level */
    } else {
        elcr_val &= ~(1 << irq_bit);  /* Clear bit for edge */
    }
    
    /* Write ELCR value */
    _asm {
        mov dx, elcr_port
        mov al, elcr_val
        out dx, al
    }
    
    _asm sti;  /* Re-enable interrupts */
    
    log_info("  ELCR programmed: IRQ%d %s-triggered (was 0x%02X, now 0x%02X)",
             irq, trigger_type == IRQ_LEVEL ? "level" : "edge",
             orig_val, elcr_val);
}

/**
 * @brief Enable driver interrupts with precise control (Phase 12)
 * 
 * Enables only the necessary hardware interrupts at the precise
 * point in the boot sequence. This avoids interrupt storms and
 * ensures proper initialization order.
 * GPT-5: Added ELCR programming and IRQ2/9 aliasing
 * 
 * @return 0 on success, negative on error
 */
int enable_driver_interrupts(void) {
    nic_info_t *nic;
    uint8_t irq_mask;
    uint8_t actual_irq;
    int result;
    driver_state_t *state;
    
    log_info("Enabling driver interrupts (precise control)");
    
    /* Get primary NIC */
    nic = hardware_get_primary_nic();
    if (!nic) {
        log_error("  No NIC available for interrupt configuration");
        return -1;
    }
    
    /* GPT-5: Handle IRQ2/IRQ9 aliasing on AT systems */
    actual_irq = nic->irq;
    if (actual_irq == 2) {
        log_info("  IRQ2 aliased to IRQ9 on AT system");
        actual_irq = 9;
    }
    
    /* GPT-5: Clear any pending interrupts on the NIC first */
    log_info("  Clearing pending NIC interrupts");
    result = hardware_clear_interrupts(nic);
    if (result < 0) {
        log_warning("  Failed to clear NIC interrupts");
    }
    
    /* Save current interrupt mask */
    _asm {
        cli                     ; Disable interrupts
        in al, 0x21            ; Read master PIC mask
        mov byte ptr irq_mask, al
    }
    g_interrupt_state.saved_interrupt_mask = irq_mask;
    
    /* Store driver IRQ */
    g_interrupt_state.driver_irq = actual_irq;
    
    /* GPT-5: Detect ELCR and save state before any changes */
    g_interrupt_state.elcr_present = detect_elcr();
    if (g_interrupt_state.elcr_present) {
        log_info("  ELCR detected - saving current state");
        save_elcr_state();
    }
    
    /* GPT-5: Determine and program ELCR based on bus type */
    state = get_driver_state();
    if (state->bus_type == BUS_PCI || state->bus_type == BUS_EISA) {
        /* PCI and EISA typically use level-triggered */
        g_interrupt_state.irq_type = IRQ_LEVEL;
        program_elcr(actual_irq, IRQ_LEVEL, 0);  /* Don't force if already set */
    } else {
        /* ISA and MCA use edge-triggered */
        g_interrupt_state.irq_type = IRQ_EDGE;
        program_elcr(actual_irq, IRQ_EDGE, 0);  /* Don't force if already set */
    }
    
    log_info("  Current IRQ mask: 0x%02X", irq_mask);
    log_info("  Enabling IRQ %d for NIC", actual_irq);
    
    /* Enable only the specific IRQ for our NIC */
    if (actual_irq < 8) {
        /* Master PIC (IRQ 0-7) */
        irq_mask &= ~(1 << actual_irq);
        _asm {
            mov al, byte ptr irq_mask
            out 0x21, al       ; Update master PIC mask
        }
    } else {
        /* Slave PIC (IRQ 8-15) */
        uint8_t slave_mask;
        _asm {
            in al, 0xA1        ; Read slave PIC mask
            mov byte ptr slave_mask, al
        }
        
        slave_mask &= ~(1 << (actual_irq - 8));
        
        _asm {
            mov al, byte ptr slave_mask
            out 0xA1, al       ; Update slave PIC mask
        }
        
        /* Also ensure cascade IRQ (IRQ 2) is enabled */
        irq_mask &= ~(1 << 2);
        _asm {
            mov al, byte ptr irq_mask
            out 0x21, al
        }
    }
    
    /* Enable NIC interrupts at the hardware level */
    result = hardware_enable_interrupts(nic);
    if (result < 0) {
        log_error("  Failed to enable NIC interrupts");
        return -1;
    }
    
    /* Re-enable global interrupts */
    _asm {
        sti                    ; Enable interrupts
    }
    
    g_interrupt_state.interrupts_enabled = 1;
    
    log_info("  Interrupts enabled successfully");
    log_info("  IRQ %d unmasked and active", actual_irq);
    
    return 0;
}

/**
 * @brief Disable driver interrupts
 * 
 * Disables driver interrupts and restores original interrupt mask.
 * Used during cleanup or error recovery.
 * GPT-5: Now also restores ELCR state
 * 
 * @return 0 on success, negative on error
 */
int disable_driver_interrupts(void) {
    nic_info_t *nic;
    uint8_t mask1, mask2;
    
    if (!g_interrupt_state.interrupts_enabled) {
        return 0;
    }
    
    log_info("Disabling driver interrupts");
    
    /* Get primary NIC */
    nic = hardware_get_primary_nic();
    if (nic) {
        /* Disable NIC interrupts at hardware level */
        hardware_disable_interrupts(nic);
    }
    
    /* Restore ELCR if it was modified */
    if (g_interrupt_state.elcr_present && g_interrupt_state.elcr_saved) {
        restore_elcr_state();
    }
    
    /* Restore original interrupt masks (both PICs) */
    mask1 = g_interrupt_state.saved_interrupt_mask & 0xFF;
    mask2 = (g_interrupt_state.saved_interrupt_mask >> 8) & 0xFF;
    
    _asm {
        cli
        mov al, mask1
        out 0x21, al       ; Restore master PIC mask
        mov al, mask2
        out 0xA1, al       ; Restore slave PIC mask
        sti
    }
    
    g_interrupt_state.interrupts_enabled = 0;
    
    log_info("  Interrupts disabled, masks and ELCR restored");
    
    return 0;
}

/**
 * @brief Check if driver interrupts are enabled
 * 
 * @return 1 if enabled, 0 if disabled
 */
int are_interrupts_enabled(void) {
    return g_interrupt_state.interrupts_enabled;
}

/**
 * @brief Get driver IRQ number
 * 
 * @return IRQ number, or -1 if not configured
 */
int get_driver_irq(void) {
    return g_interrupt_state.driver_irq ? g_interrupt_state.driver_irq : -1;
}