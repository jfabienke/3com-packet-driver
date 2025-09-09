/**
 * @file pcmcia_cis.h
 * @brief Minimal CIS parser prototypes (3Com-focused)
 */

#ifndef _PCMCIA_CIS_H_
#define _PCMCIA_CIS_H_

#include <stdint.h>

/* Try to parse CIS from a buffer to extract I/O base and IRQ.
 * Returns 0 on success, negative on error. */
int pcmcia_cis_parse_3com(const uint8_t *cis, uint16_t length,
                          uint16_t *io_base, uint8_t *irq);

#endif /* _PCMCIA_CIS_H_ */

