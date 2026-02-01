/**
 * @file jit_build.h
 * @brief JIT copy-down engine shared types and function prototypes
 *
 * Shared between jit_build.c, jit_patch.c, and jit_reloc.c.
 * All three files live in the OVERLAY section and are discarded after init.
 *
 * 3Com Packet Driver - JIT Copy-Down Engine
 *
 * Last Updated: 2026-02-01 12:05:57 CET
 */

#ifndef JIT_BUILD_H
#define JIT_BUILD_H

#include "modhdr.h"
#include "mod_select.h"
#include <stdint.h>

/* ============================================================================
 * Types
 * ============================================================================ */

/* Layout entry for one module in the TSR image */
typedef struct {
    module_id_t id;
    uint16_t    src_offset;     /* Offset in source module (hot_start) */
    uint16_t    src_size;       /* Size of hot section */
    uint16_t    dst_offset;     /* Offset in TSR image */
} jit_layout_entry_t;

/* Complete TSR image layout */
typedef struct {
    uint8_t far    *image_base;     /* Far pointer to TSR image buffer */
    uint16_t        image_size;     /* Total image size */
    uint16_t        entry_count;    /* Number of modules in layout */
    jit_layout_entry_t entries[16]; /* Module layout entries */
} jit_layout_t;

/* Hardware values to bake into the TSR via SMC */
typedef struct {
    uint16_t    io_base;        /* NIC I/O base address */
    uint8_t     irq_number;     /* IRQ number */
    uint8_t     dma_channel;    /* DMA channel (0xFF=none) */
    uint8_t     mac_addr[6];    /* MAC address */
    uint16_t    nic_type;       /* NIC type identifier */
    uint16_t    cpu_type;       /* CPU type identifier */
    uint16_t    flags;          /* Runtime flags */
    uint8_t     cache_line_size;/* Cache line size */
} jit_hw_values_t;

/* ============================================================================
 * Function Prototypes
 * ============================================================================ */

/* jit_build.c */
int jit_build_image(jit_layout_t *layout);
jit_layout_entry_t *jit_get_layout_entry(jit_layout_t *layout, module_id_t id);
void jit_serialize_prefetch(jit_layout_t *layout);

/* jit_patch.c */
int jit_apply_patches(jit_layout_t *layout, const jit_hw_values_t *hw);

/* jit_reloc.c */
int jit_apply_relocations(jit_layout_t *layout);

#endif /* JIT_BUILD_H */
