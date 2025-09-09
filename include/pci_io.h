/**
 * @file pci_io.h
 * @brief PCI I/O assembly function declarations
 * 
 * Header file for assembly-implemented I/O functions that provide
 * 32-bit port access for PCI configuration mechanisms.
 * 
 * These functions are implemented in assembly to ensure correct
 * 32-bit I/O operations in 16-bit real mode on 386+ processors.
 */

#ifndef _PCI_IO_H_
#define _PCI_IO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read 32-bit value from I/O port
 * 
 * Uses 386+ 32-bit IN instruction with operand size prefix.
 * Required for PCI Mechanism #1 configuration access.
 * 
 * @param port I/O port address
 * @return 32-bit value read from port
 */
extern uint32_t cdecl inportd(uint16_t port);

/**
 * @brief Write 32-bit value to I/O port
 * 
 * Uses 386+ 32-bit OUT instruction with operand size prefix.
 * Required for PCI Mechanism #1 configuration access.
 * 
 * @param port I/O port address
 * @param value 32-bit value to write
 */
extern void cdecl outportd(uint16_t port, uint32_t value);

/**
 * @brief Read 16-bit value from I/O port
 * 
 * Standard word I/O for Mechanism #2 and general use.
 * 
 * @param port I/O port address
 * @return 16-bit value read from port
 */
extern uint16_t cdecl inportw(uint16_t port);

/**
 * @brief Write 16-bit value to I/O port
 * 
 * Standard word I/O for Mechanism #2 and general use.
 * 
 * @param port I/O port address
 * @param value 16-bit value to write
 */
extern void cdecl outportw(uint16_t port, uint16_t value);

/**
 * @brief Read 8-bit value from I/O port
 * 
 * Standard byte I/O for Mechanism #2 and general use.
 * 
 * @param port I/O port address
 * @return 8-bit value read from port
 */
extern uint8_t cdecl inportb(uint16_t port);

/**
 * @brief Write 8-bit value to I/O port
 * 
 * Standard byte I/O for Mechanism #2 and general use.
 * 
 * @param port I/O port address
 * @param value 8-bit value to write
 */
extern void cdecl outportb(uint16_t port, uint8_t value);

/**
 * @brief Disable interrupts
 * 
 * Executes CLI instruction.
 */
extern void cdecl cli_safe(void);

/**
 * @brief Enable interrupts
 * 
 * Executes STI instruction.
 */
extern void cdecl sti_safe(void);

/**
 * @brief Save current flags register
 * 
 * Pushes flags and returns value.
 * 
 * @return Current flags register value
 */
extern uint16_t cdecl save_flags(void);

/**
 * @brief Restore flags register
 * 
 * Restores previously saved flags including interrupt flag.
 * 
 * @param flags Flags value to restore
 */
extern void cdecl restore_flags(uint16_t flags);

/* PCI BIOS shim ISR functions */

/**
 * @brief PCI BIOS shim interrupt service routine
 * 
 * Main ISR entry point for INT 1Ah interception.
 * Implemented in assembly for proper register handling.
 */
extern void interrupt far pci_shim_isr(void);

/**
 * @brief Set chain vector for original INT 1Ah
 * 
 * Configures the tail-chain address for the original BIOS handler.
 * 
 * @param segment Original handler segment
 * @param offset Original handler offset
 */
extern void cdecl set_chain_vector(uint16_t segment, uint16_t offset);

/**
 * @brief Get ECX high word
 * 
 * Returns the high 16 bits of ECX register.
 * Used for handling 32-bit PCI BIOS operations.
 * 
 * @return High 16 bits of ECX
 */
extern uint16_t cdecl get_ecx_high(void);

/**
 * @brief Set ECX high word
 * 
 * Sets the high 16 bits of ECX register.
 * Used for returning 32-bit values in PCI BIOS operations.
 * 
 * @param value Value to set in ECX high word
 */
extern void cdecl set_ecx_high(uint16_t value);

/* Register context structure for C handler */
typedef struct {
    uint16_t ax;            /* AX register (function in AL) */
    uint16_t bx;            /* BX register (bus/dev/func) */
    uint16_t cx_low;        /* CX low 16 bits */
    uint16_t cx_high;       /* CX high 16 bits (for ECX) */
    uint16_t dx_low;        /* DX low 16 bits */
    uint16_t dx_high;       /* DX high 16 bits (for EDX) */
    uint16_t di;            /* DI register (offset) */
    uint16_t si;            /* SI register */
} pci_regs_t;

#ifdef __cplusplus
}
#endif

#endif /* _PCI_IO_H_ */