/**
 * @file pcmcia.h
 * @brief Minimal PCMCIA/CardBus hotplug manager interface (integrated)
 */

#ifndef _PCMCIA_H_
#define _PCMCIA_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize PCMCIA/CardBus manager (cold). Returns number of controller/sockets detected (may be 0). */
int pcmcia_init(void);

/* Cleanup manager state (cold). */
void pcmcia_cleanup(void);

/* Background polling hook (safe to call from idle bottom-half). */
void pcmcia_poll(void);

/* Capability queries. */
bool pcmcia_controller_present(void);   /* 16-bit PCMCIA/PCIC (ISA) */
bool cardbus_present(void);             /* 32-bit CardBus (PCI) */

#ifdef __cplusplus
}
#endif

#endif /* _PCMCIA_H_ */

