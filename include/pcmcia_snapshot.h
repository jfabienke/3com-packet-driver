/**
 * @file pcmcia_snapshot.h
 * @brief Snapshot structs for PCMCIA/CardBus status (Extension API AH=98h)
 */

#ifndef _PCMCIA_SNAPSHOT_H_
#define _PCMCIA_SNAPSHOT_H_

#include <stdint.h>

/* One entry per socket/controller (packed, constant-time copy) */
typedef struct {
    uint8_t socket_id;        /* 0..N-1 */
    uint8_t present;          /* 1=controller present for PCMCIA socket; 0=absent */
    uint8_t card_present;     /* 1=card detected; 0=empty/unknown */
    uint8_t powered;          /* 1=socket powered; 0=off (not managed yet) */
    uint16_t io_base;         /* I/O base if mapped, else 0 */
    uint8_t irq;              /* IRQ if assigned, else 0 */
    uint8_t type;             /* 1=PCMCIA(ISA), 2=CardBus(PCI), 0=unknown */
} pcmcia_socket_info_t;

typedef struct {
    uint8_t socket_count;     /* number of entries that follow */
    uint8_t capabilities;     /* bit0=PCMCIA controller present, bit1=CardBus present */
    uint16_t reserved;        /* alignment */
    /* Followed by socket_count entries */
    /* pcmcia_socket_info_t entries[]; */
} pcmcia_snapshot_header_t;

/* C API used by Extension API handler */
int pcmcia_get_snapshot(void far *dst, uint16_t max_bytes);

/* Manager-provided helper to fill snapshot entries and capabilities */
int pcmcia_manager_fill_snapshot(pcmcia_socket_info_t *entries,
                                 uint16_t max_entries,
                                 uint8_t *capabilities,
                                 uint8_t *count_out);

#endif /* _PCMCIA_SNAPSHOT_H_ */
