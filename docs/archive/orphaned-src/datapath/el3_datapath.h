/**
 * @file el3_datapath.h
 * @brief Datapath Header for 3Com EtherLink III
 *
 * Common definitions for PIO and DMA datapaths.
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _EL3_DATAPATH_H_
#define _EL3_DATAPATH_H_

#include <stdint.h>

/* Packet structure */
struct packet {
    uint8_t *data;
    uint16_t length;
    uint16_t flags;
};

/* Datapath function prototypes (already declared in el3_core.h) */

/* TX clean function for DMA */
void el3_dma_tx_clean(struct el3_dev *dev);

/* Helper functions */
static inline void outportl(uint16_t port, uint32_t value);
static inline uint32_t inportl(uint16_t port);

#endif /* _EL3_DATAPATH_H_ */