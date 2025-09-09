/**
 * @file irq_bind.c
 * @brief Bind NIC IRQ/IO to ASM handler and install vector
 */

#include "../include/hardware.h"

/* ASM helpers */
extern void nic_irq_set_binding(unsigned short io_base, unsigned char irq, unsigned char nic_index);
extern void irq_handler_init(void);
extern void irq_handler_uninstall(void);

void nic_irq_bind_and_install(const nic_info_t *nic) {
    if (!nic) return;
    /* Program ASM-side globals */
    nic_irq_set_binding(nic->io_base, nic->irq, nic->index);
    /* Install INT vector; PIC unmasking is handled later in enable_driver_interrupts() */
    irq_handler_init();
}

void nic_irq_uninstall(void) {
    /* Restore previous vector saved during install */
    irq_handler_uninstall();
}
