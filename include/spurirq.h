/**
 * @file spurious_irq.h
 * @brief Spurious IRQ 7 and IRQ 15 detection and handling
 * 
 * Production-quality spurious interrupt handling addressing GPT-5's
 * suggestion for robust interrupt management.
 */

#ifndef SPURIOUS_IRQ_H
#define SPURIOUS_IRQ_H

#include <stdint.h>
#include <stdbool.h>

/* Spurious IRQ statistics */
struct spurious_irq_stats {
    uint32_t spurious_irq7_count;   /* Spurious IRQ 7 interrupts */
    uint32_t spurious_irq15_count;  /* Spurious IRQ 15 interrupts */
    uint32_t total_irq7_count;      /* Total IRQ 7 interrupts */
    uint32_t total_irq15_count;     /* Total IRQ 15 interrupts */
};

/* Function prototypes */

/**
 * Install spurious IRQ handlers
 * Hooks IRQ 7 and IRQ 15 to detect and handle spurious interrupts
 */
void install_spurious_irq_handlers(void);

/**
 * Restore original IRQ handlers
 * Call during driver cleanup
 */
void restore_spurious_irq_handlers(void);

/**
 * Check if an interrupt is spurious
 * Returns true if the interrupt is spurious (no corresponding bit in ISR)
 */
bool is_spurious_interrupt(uint8_t irq);

/**
 * Handle spurious interrupt from specific NIC
 * Performs proper EOI handling based on IRQ number and spurious status
 */
void handle_nic_spurious_interrupt(uint8_t device_id, uint16_t irq);

/**
 * Get spurious IRQ statistics
 */
void get_spurious_irq_stats(struct spurious_irq_stats *stats);

/**
 * PIC utility functions
 */
uint8_t read_pic_isr(bool slave);      /* Read In-Service Register */
uint8_t read_pic_irr(bool slave);      /* Read Interrupt Request Register */

/* Convenience macros */

/* Check for spurious IRQ 7 (master PIC) */
#define IS_SPURIOUS_IRQ7() is_spurious_interrupt(7)

/* Check for spurious IRQ 15 (slave PIC) */
#define IS_SPURIOUS_IRQ15() is_spurious_interrupt(15)

/* Read master PIC registers */
#define READ_MASTER_ISR() read_pic_isr(false)
#define READ_MASTER_IRR() read_pic_irr(false)

/* Read slave PIC registers */
#define READ_SLAVE_ISR() read_pic_isr(true)
#define READ_SLAVE_IRR() read_pic_irr(true)

/**
 * Integration with enhanced ISR
 * 
 * Enhanced ISR should check for spurious interrupts before processing:
 * 
 * ```asm
 * _enhanced_isr_with_spurious_check:
 *     ; ... save registers ...
 *     
 *     ; Check if spurious
 *     push    [device_irq]
 *     call    _is_spurious_interrupt
 *     add     sp, 2
 *     test    ax, ax
 *     jnz     .spurious_detected
 *     
 *     ; Normal interrupt processing
 *     jmp     .process_interrupt
 *     
 * .spurious_detected:
 *     ; Handle spurious interrupt
 *     push    [device_irq]  
 *     push    [device_id]
 *     call    _handle_nic_spurious_interrupt
 *     add     sp, 4
 *     jmp     .isr_exit
 * ```
 */

#endif /* SPURIOUS_IRQ_H */