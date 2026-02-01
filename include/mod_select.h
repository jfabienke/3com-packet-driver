/**
 * @file mod_select.h
 * @brief Module selection API for JIT copy-down TSR builder
 *
 * Provides the interface for selecting which ASM modules to include
 * in the JIT-built TSR image based on detected hardware capabilities.
 *
 * Last Updated: 2026-02-01 11:02:20 CET
 */

#ifndef MOD_SELECT_H
#define MOD_SELECT_H

#include "modhdr.h"
#include "init_context.h"

/* Maximum modules that can be selected for a single TSR build */
#define MOD_SELECT_MAX      48

/* Module registry entry - links module ID to its compiled object */
typedef struct {
    module_id_t     id;             /* Module identifier */
    const char     *name;           /* Human-readable name */
    uint16_t        cap_flags;      /* Capability requirements */
    uint8_t         cpu_req;        /* Minimum CPU (0=8086, 2=286, 3=386...) */
    uint8_t         nic_type;       /* NIC type (0=any) */
    void far       *header_ptr;     /* Far pointer to module header in EXE */
    uint16_t        hot_size;       /* Size of hot section in bytes */
} mod_registry_entry_t;

/* Module selection context - tracks which modules are selected */
typedef struct {
    uint8_t             count;                      /* Number of selected modules */
    module_id_t         selected[MOD_SELECT_MAX];   /* Selected module IDs */
    uint32_t            total_hot_size;             /* Total hot section bytes */
    uint16_t            cap_flags_met;              /* Combined capability flags */
} mod_selection_t;

/* Initialize the module registry (called once at init) */
int mod_registry_init(void);

/* Select a module by ID */
int select_module(module_id_t id);

/* Check if a module is selected */
int is_module_selected(module_id_t id);

/* Get the current selection context */
mod_selection_t *get_module_selection(void);

/* Get registry entry for a module */
const mod_registry_entry_t *mod_registry_get(module_id_t id);

/* Hardware-based module selection functions (called from init) */
int select_nic_module(const init_context_t *ctx);
int select_dma_module(const init_context_t *ctx);
int select_cache_module(const init_context_t *ctx);
int select_copy_module(const init_context_t *ctx);
int select_core_modules(void);

/* Master selection function - calls all of the above */
int select_all_modules(const init_context_t *ctx);

/* Validate that selected modules are compatible */
int validate_module_selection(const mod_selection_t *sel,
                              const init_context_t *ctx);

#endif /* MOD_SELECT_H */
