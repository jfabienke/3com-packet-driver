/**
 * @file vds_mapping.h
 * @brief VDS (Virtual DMA Services) mapping interface
 *
 * GPT-5 Critical: Proper VDS integration for V86/Windows compatibility
 */

#ifndef VDS_MAPPING_H
#define VDS_MAPPING_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VDS scatter-gather entry */
typedef struct {
    uint32_t physical_addr;     /* Physical address of segment */
    uint32_t length;           /* Length of segment */
    bool is_contiguous;        /* True if entire buffer is contiguous */
} vds_sg_entry_t;

/* VDS buffer descriptor */
typedef struct {
    void* virtual_addr;        /* Virtual address */
    uint32_t physical_addr;    /* Physical address (if contiguous) */
    uint32_t size;            /* Buffer size */
    uint16_t buffer_id;       /* VDS buffer ID/handle */
    bool is_vds_allocated;    /* Allocated by VDS (vs mapped) */
} vds_buffer_t;

/* VDS mapping handle */
typedef uint16_t vds_lock_handle_t;

/* Initialization */
bool vds_init(void);
bool is_vds_available(void);
bool is_v86_mode(void);
uint16_t get_vds_version(void);

/* Buffer mapping/unmapping */
vds_lock_handle_t vds_map_buffer(void* virtual_addr, uint32_t size,
                                 vds_sg_entry_t* sg_list, uint16_t max_entries);
bool vds_unmap_buffer(vds_lock_handle_t lock_handle);

/* DMA buffer allocation */
vds_buffer_t* vds_request_dma_buffer(uint32_t size, uint16_t alignment);
bool vds_release_dma_buffer(vds_buffer_t* buffer);

/* Safe physical address resolution */
bool vds_get_safe_physical_address(void* virtual_addr, uint32_t size,
                                  uint32_t* phys_addr);

/* Constraint checking with VDS awareness */
bool vds_check_dma_constraints(void* virtual_addr, uint32_t size,
                              bool require_64kb_safe, bool require_16mb_limit);

#ifdef __cplusplus
}
#endif

#endif /* VDS_MAPPING_H */