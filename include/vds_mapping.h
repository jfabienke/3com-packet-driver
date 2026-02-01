/**
 * @file vds_mapping.h
 * @brief VDS (Virtual DMA Services) mapping structures
 *
 * Provides vds_mapping_t structure for managing VDS DMA mappings
 * with tracking of physical addresses, lock state, and contiguity.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 * Last Updated: 2026-01-24 14:45:00 CET
 */

#ifndef _VDS_MAPPING_H_
#define _VDS_MAPPING_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "portabl.h"   /* C89 compatibility: bool, uint32_t, etc. */
#include "vds.h"

/**
 * @brief VDS mapping tracking structure
 *
 * Tracks a VDS DMA mapping including the underlying DDS,
 * physical/virtual addresses, and lock state.
 */
typedef struct vds_mapping {
    VDS_DDS dds;                /* Underlying VDS DMA descriptor */
    uint32_t physical_addr;     /* Physical address of mapping */
    void *virtual_addr;         /* Virtual address (far pointer in real mode) */
    uint32_t size;              /* Size of mapped region in bytes */
    uint8_t is_locked;          /* Non-zero if region is locked for DMA */
    uint8_t is_contiguous;      /* Non-zero if region is physically contiguous */
    uint8_t needs_unlock;       /* Non-zero if unlock is needed on release */
    uint8_t flags;              /* Additional flags from VDS */
} vds_mapping_t;

/* VDS direction flags for lock operations */
#define VDS_TX_FLAGS    0x01    /* TX direction (CPU to device) */
#define VDS_RX_FLAGS    0x02    /* RX direction (device to CPU) */

/* VDS helper function prototypes */
bool vds_lock_region_mapped(void *addr, uint32_t size, uint16_t flags, vds_mapping_t *mapping);
bool vds_unlock_region_mapped(vds_mapping_t *mapping);
bool vds_is_isa_compatible(uint32_t physical_addr, uint32_t size);

/**
 * @brief Initialize a VDS mapping structure
 * @param mapping Pointer to mapping structure to initialize
 */
static void vds_mapping_init(vds_mapping_t *mapping) {
    if (mapping) {
        mapping->dds.size = 0;
        mapping->dds.offset = 0;
        mapping->dds.segment = 0;
        mapping->dds.buffer_id = 0;
        mapping->dds.physical = 0;
        mapping->dds.flags = 0;
        mapping->physical_addr = 0;
        mapping->virtual_addr = 0;
        mapping->size = 0;
        mapping->is_locked = 0;
        mapping->is_contiguous = 0;
        mapping->needs_unlock = 0;
        mapping->flags = 0;
    }
}

/**
 * @brief Check if a VDS mapping is valid and locked
 * @param mapping Pointer to mapping structure
 * @return Non-zero if mapping is valid and locked
 */
static int vds_mapping_is_valid(const vds_mapping_t *mapping) {
    return mapping && mapping->is_locked && mapping->size > 0;
}

#ifdef __cplusplus
}
#endif

#endif /* _VDS_MAPPING_H_ */
