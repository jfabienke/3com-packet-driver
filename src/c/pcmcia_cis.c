/**
 * @file pcmcia_cis.c
 * @brief Minimal CIS parser (3Com-specific tuples)
 */

#include <string.h>
#include "../include/pcmcia_cis.h"

/* CIS tuple types */
#define CISTPL_NULL        0x00
#define CISTPL_MANFID      0x20
#define CISTPL_FUNCID      0x21
#define CISTPL_VERS_1      0x15
#define CISTPL_CONFIG      0x1A
#define CISTPL_CFTABLE     0x1B
#define CISTPL_END         0xFF

/* Minimal parse to find I/O and IRQ hints in CFTABLE. Fallbacks if not found. */
int pcmcia_cis_parse_3com(const uint8_t *cis, uint16_t length,
                          uint16_t *io_base, uint8_t *irq)
{
    uint16_t off = 0;
    if (!cis || length < 4) return -1;
    /* Defaults */
    uint16_t found_io = 0; uint8_t found_irq = 0;
    while (off + 2 <= length) {
        uint8_t type = cis[off++];
        if (type == CISTPL_NULL) continue;
        if (type == CISTPL_END) break;
        if (off >= length) break;
        uint8_t len = cis[off++];
        if (off + len > length) break;
        const uint8_t *data = &cis[off];
        switch (type) {
            case CISTPL_CFTABLE:
                /* Heuristic: look for simple I/O base (16-bit) followed by size and IRQ */
                if (len >= 4) {
                    /* Not accurate without full decode; attempt to read little-endian IO base */
                    uint16_t io = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
                    if (io >= 0x200 && io < 0x400) found_io = io;
                    /* IRQ hint often encoded as a mask or number; take a plausible byte */
                    uint8_t q = data[len-1] & 0x0F;
                    if (q >= 3 && q <= 15) found_irq = q;
                }
                break;
            default:
                break;
        }
        off += len;
    }
    if (found_io == 0) found_io = 0x300;
    if (found_irq == 0) found_irq = 10;
    if (io_base) *io_base = found_io;
    if (irq) *irq = found_irq;
    return 0;
}

