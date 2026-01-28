/**
 * @file dmamap_rt.c
 * @brief DMA Mapping Layer - Runtime Functions (ROOT Segment)
 *
 * Created: 2026-01-28 08:00:00 UTC
 *
 * This file contains runtime DMA mapping functions that are called during
 * active packet TX/RX operations, including interrupt handlers. These
 * functions must remain memory-resident (ROOT segment) for performance
 * and reliability during high-frequency network operations.
 *
 * Functions included:
 * - dma_map_* functions for TX/RX buffer mapping
 * - dma_unmap_* functions for buffer release
 * - dma_mapping_get_* accessor functions
 * - dma_mapping_sync_* cache synchronization functions
 * - Helper functions called during interrupt handling
 *
 * Split from dmamap.c for memory segmentation optimization.
 * Init/cleanup functions are in dmamap_init.c (OVERLAY segment).
 */

#include "dmamap.h"
#include "dmabnd.h"
#include "cacheche.h"
#include "vds.h"
#include "vds_mapping.h"
#include "pltprob.h"
#include "diag.h"      /* For LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR macros */
#include "portabl.h"   /* C89 compatibility: bool, uint32_t, etc. */
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

/* External function declarations - GPT-5 device constraint validation */
extern bool dma_validate_buffer_constraints(const char *device_name,
                                           void *buffer, uint32_t size,
                                           uint32_t physical_addr);

/* Internal DMA mapping structure - enhanced for VDS */
struct dma_mapping {
    void *original_buffer;          /* Original user buffer */
    void *mapped_address;           /* Address to use for DMA (bounce or original) */
    uint32_t phys_addr;             /* Physical address for DMA */
    size_t length;                  /* Buffer length */
    dma_sync_direction_t direction; /* DMA direction */
    uint32_t flags;                 /* Mapping flags */
    bool uses_bounce;               /* Using bounce buffer */
    bool is_coherent;               /* Cache coherent mapping */
    bool uses_vds;                  /* Using VDS mapping */
    vds_mapping_t vds_mapping;      /* VDS mapping information */
    dma_check_result_t dma_check;   /* DMA safety check results */
    uint32_t magic;                 /* Magic number for validation */
};

#define DMA_MAPPING_MAGIC 0x444D4150  /* "DMAP" */

/*==============================================================================
 * External declarations for global state (defined in dmamap_init.c)
 *==============================================================================*/
extern dma_mapping_stats_t g_stats;
extern bool g_fast_path_enabled;
extern uint32_t g_cache_hits;
extern uint32_t g_cache_attempts;

/*==============================================================================
 * Internal helper functions
 *==============================================================================*/

static bool validate_mapping(const dma_mapping_t *mapping) {
    return mapping && mapping->magic == DMA_MAPPING_MAGIC;
}

static dma_mapping_t* create_mapping(void *buffer, size_t len, dma_sync_direction_t direction, uint32_t flags) {
    dma_mapping_t *mapping;

    if (!buffer || len == 0) {
        g_stats.mapping_errors++;
        return NULL;
    }

    mapping = malloc(sizeof(dma_mapping_t));
    if (!mapping) {
        g_stats.mapping_errors++;
        return NULL;
    }

    memset(mapping, 0, sizeof(dma_mapping_t));
    mapping->magic = DMA_MAPPING_MAGIC;
    mapping->original_buffer = buffer;
    mapping->length = len;
    mapping->direction = direction;
    mapping->flags = flags;
    mapping->is_coherent = (flags & DMA_MAP_COHERENT) != 0;

    return mapping;
}

static int setup_dma_mapping(dma_mapping_t *mapping) {
    void *dma_buffer;
    bool force_bounce;
    uint16_t cacheline_size;
    bool needs_alignment_bounce;
    bool force_vds;
    uint16_t vds_flags;
    void *bounce_buf;
    dma_check_result_t bounce_check;

    /* C89: Initialize variables after declarations */
    force_bounce = (mapping->flags & DMA_MAP_FORCE_BOUNCE) != 0;
    cacheline_size = get_cache_line_size();

    /* GPT-5 Critical: Check cacheline alignment safety */
    needs_alignment_bounce = needs_bounce_for_alignment(mapping->original_buffer,
                                                        mapping->length, cacheline_size);
    if (needs_alignment_bounce) {
        LOG_DEBUG("DMA mapping: Cacheline alignment requires bounce buffer");
    }

    /* Check DMA safety with framework constraints */
    /* GPT-5 CRITICAL: Use DMA safety framework for per-NIC constraint checking */

    if (!dma_check_buffer_safety(mapping->original_buffer, mapping->length, &mapping->dma_check)) {
        LOG_ERROR("DMA safety check failed for buffer %p len=%lu",
                  mapping->original_buffer, (unsigned long)mapping->length);
        g_stats.mapping_errors++;
        return DMA_MAP_ERROR_BOUNDARY;
    }

    /* Additional per-NIC constraint validation if device name is known */
    /* This will be called with actual device name from hardware layer */

    /* Determine if bounce buffer needed - GPT-5 Enhanced */
    mapping->uses_bounce = force_bounce ||
                          mapping->dma_check.crosses_64k ||
                          mapping->dma_check.crosses_16m ||
                          mapping->dma_check.needs_bounce ||
                          needs_alignment_bounce;  /* GPT-5 Critical addition */

    if (mapping->uses_bounce) {
        /* Get bounce buffer based on direction */
        if (mapping->direction == DMA_SYNC_TX ||
            (mapping->flags & DMA_MAP_READ)) {
            dma_buffer = dma_get_tx_bounce_buffer(mapping->length);
        } else {
            dma_buffer = dma_get_rx_bounce_buffer(mapping->length);
        }

        if (!dma_buffer) {
            LOG_ERROR("Failed to allocate bounce buffer len=%lu", (unsigned long)mapping->length);
            g_stats.mapping_errors++;
            return DMA_MAP_ERROR_NO_BOUNCE;
        }

        mapping->mapped_address = dma_buffer;
        g_stats.bounce_mappings++;

        /* For TX, copy data to bounce buffer */
        if (mapping->direction == DMA_SYNC_TX || (mapping->flags & DMA_MAP_READ)) {
            memcpy(dma_buffer, mapping->original_buffer, mapping->length);
        }
    } else {
        /* Direct mapping */
        mapping->mapped_address = mapping->original_buffer;
        g_stats.direct_mappings++;
    }

    /* Calculate physical address - use VDS if available and needed */
    force_vds = (mapping->flags & DMA_MAP_VDS_ZEROCOPY) != 0;
    if ((platform_get_dma_policy() == DMA_POLICY_COMMONBUF || force_vds) && !mapping->uses_bounce) {
        /* Try VDS lock for direct mapping */
        vds_flags = (mapping->direction == DMA_SYNC_TX) ? VDS_TX_FLAGS : VDS_RX_FLAGS;

        if (vds_lock_region_mapped(mapping->mapped_address, mapping->length, vds_flags, &mapping->vds_mapping)) {
            mapping->phys_addr = mapping->vds_mapping.physical_addr;
            mapping->uses_vds = true;
            LOG_DEBUG("DMA: VDS lock successful - virt=%p phys=%08lX",
                     mapping->mapped_address, (unsigned long)mapping->phys_addr);

            /* GPT-5 CRITICAL: Comprehensive DMA constraint validation */
            /* Check 1: ISA DMA 16MB limit (24-bit addressing) */
            if (mapping->phys_addr >= 0x1000000UL) {
                LOG_WARNING("VDS address exceeds 16MB ISA limit: %08lX, using bounce",
                           (unsigned long)mapping->phys_addr);
                vds_unlock_region_mapped(&mapping->vds_mapping);
                mapping->uses_vds = false;
                mapping->uses_bounce = true;
                g_stats.bounce_mappings++;
            }
            /* Check 2: 64KB boundary crossing (ISA DMA limitation) */
            else if (((mapping->phys_addr & 0xFFFFUL) + mapping->length) > 0x10000UL) {
                LOG_WARNING("VDS buffer crosses 64KB boundary: addr=%08lX len=%lu, using bounce",
                           (unsigned long)mapping->phys_addr, (unsigned long)mapping->length);
                vds_unlock_region_mapped(&mapping->vds_mapping);
                mapping->uses_vds = false;
                mapping->uses_bounce = true;
                g_stats.bounce_mappings++;
            }
            /* Check 3: Contiguity requirement for 3C515 (no scatter-gather) */
            else if (!mapping->vds_mapping.is_contiguous) {
                LOG_WARNING("VDS returned non-contiguous mapping, 3C515 requires contiguous, using bounce");
                vds_unlock_region_mapped(&mapping->vds_mapping);
                mapping->uses_vds = false;
                mapping->uses_bounce = true;
                g_stats.bounce_mappings++;
            }
            /* Check 4: Basic ISA compatibility check */
            else if (!vds_is_isa_compatible(mapping->phys_addr, mapping->length)) {
                LOG_ERROR("VDS returned non-ISA compatible address: %08lX",
                         (unsigned long)mapping->phys_addr);
                vds_unlock_region_mapped(&mapping->vds_mapping);
                mapping->uses_vds = false;
                return DMA_MAP_ERROR_BOUNDARY;
            }
        } else {
            LOG_WARNING("VDS lock failed - falling back to bounce buffer");
            /* Force bounce buffer allocation */
            mapping->uses_bounce = true;
            g_stats.bounce_mappings++;
        }
    }

    /* GPT-5 FIX: Allocate bounce buffer if VDS constraints failed */
    if (mapping->uses_bounce && mapping->mapped_address == mapping->original_buffer) {
        /* VDS failed constraints - need to allocate bounce buffer now */
        if (mapping->direction == DMA_SYNC_TX || (mapping->flags & DMA_MAP_READ)) {
            bounce_buf = dma_get_tx_bounce_buffer(mapping->length);
        } else {
            bounce_buf = dma_get_rx_bounce_buffer(mapping->length);
        }

        if (!bounce_buf) {
            LOG_ERROR("Failed to allocate bounce buffer after VDS constraint failure");
            return DMA_MAP_ERROR_NO_BOUNCE;
        }

        mapping->mapped_address = bounce_buf;

        /* For TX, copy data to bounce buffer */
        if (mapping->direction == DMA_SYNC_TX || (mapping->flags & DMA_MAP_READ)) {
            memcpy(bounce_buf, mapping->original_buffer, mapping->length);
        }
    }

    if (!mapping->uses_vds) {
        /* Use traditional physical address calculation */
        mapping->phys_addr = mapping->dma_check.phys_addr;
        if (mapping->uses_bounce) {
            /* Recalculate for bounce buffer */
            if (!dma_check_buffer_safety(mapping->mapped_address, mapping->length, &bounce_check)) {
                LOG_ERROR("Bounce buffer safety check failed");
                return DMA_MAP_ERROR_BOUNDARY;
            }
            mapping->phys_addr = bounce_check.phys_addr;
        }
    }

    /* Perform cache sync for device if needed - GPT-5 Enhanced Safety */
    if (!mapping->is_coherent && !(mapping->flags & DMA_MAP_NO_CACHE_SYNC)) {
        /* Use safe aligned cache flush to prevent partial cacheline corruption */
        if (mapping->direction == DMA_SYNC_TX) {
            cache_flush_aligned_safe(mapping->mapped_address, mapping->length);
        } else {
            /* For RX, invalidate cache lines safely */
            cache_flush_aligned_safe(mapping->mapped_address, mapping->length);
        }
        g_stats.cache_syncs++;
    }

    /* Update statistics */
    g_stats.total_mappings++;
    g_stats.active_mappings++;

    if (mapping->direction == DMA_SYNC_TX || (mapping->flags & DMA_MAP_READ)) {
        g_stats.tx_mappings++;
    } else {
        g_stats.rx_mappings++;
    }

    return DMA_MAP_SUCCESS;
}

/*==============================================================================
 * TX (CPU -> Device) mapping functions
 *==============================================================================*/

dma_mapping_t* dma_map_tx(void *buffer, size_t len) {
    return dma_map_tx_flags(buffer, len, 0);
}

dma_mapping_t* dma_map_tx_flags(void *buffer, size_t len, uint32_t flags) {
    dma_mapping_t *mapping;
    int result;

    mapping = create_mapping(buffer, len, DMA_SYNC_TX, flags | DMA_MAP_READ);
    if (!mapping) {
        return NULL;
    }

    result = setup_dma_mapping(mapping);
    if (result != DMA_MAP_SUCCESS) {
        free(mapping);
        return NULL;
    }

    return mapping;
}

void dma_unmap_tx(dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        LOG_ERROR("Invalid TX mapping for unmap");
        return;
    }

    /* No need to copy back for TX */

    /* Release bounce buffer if used */
    if (mapping->uses_bounce) {
        dma_release_tx_bounce_buffer(mapping->mapped_address);
    }

    /* CRITICAL: Unlock VDS mapping if used (deferred from ISR) */
    if (mapping->uses_vds && mapping->vds_mapping.needs_unlock) {
        if (!vds_unlock_region_mapped(&mapping->vds_mapping)) {
            LOG_WARNING("VDS unlock failed for TX mapping %p", mapping);
        }
        LOG_DEBUG("VDS TX mapping unlocked");
    }

    /* GPT-5 Critical: Unlock pages if they were locked */
    if (mapping->dma_check.pages_locked) {
        unlock_pages_for_dma(mapping->dma_check.lock_handle);
    }

    /* Update statistics */
    if (g_stats.active_mappings > 0) {
        g_stats.active_mappings--;
    }

    mapping->magic = 0;
    free(mapping);
}

/*==============================================================================
 * RX (Device -> CPU) mapping functions
 *==============================================================================*/

dma_mapping_t* dma_map_rx(void *buffer, size_t len) {
    return dma_map_rx_flags(buffer, len, 0);
}

dma_mapping_t* dma_map_rx_flags(void *buffer, size_t len, uint32_t flags) {
    dma_mapping_t *mapping;
    int result;

    mapping = create_mapping(buffer, len, DMA_SYNC_RX, flags | DMA_MAP_WRITE);
    if (!mapping) {
        return NULL;
    }

    result = setup_dma_mapping(mapping);
    if (result != DMA_MAP_SUCCESS) {
        free(mapping);
        return NULL;
    }

    return mapping;
}

void dma_unmap_rx(dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        LOG_ERROR("Invalid RX mapping for unmap");
        return;
    }

    /* Sync cache for CPU access - GPT-5 Enhanced Safety */
    if (!mapping->is_coherent && !(mapping->flags & DMA_MAP_NO_CACHE_SYNC)) {
        /* Invalidate cache lines safely before CPU reads */
        cache_flush_aligned_safe(mapping->mapped_address, mapping->length);
        g_stats.cache_syncs++;
    }

    /* Copy data back if using bounce buffer */
    if (mapping->uses_bounce) {
        memcpy(mapping->original_buffer, mapping->mapped_address, mapping->length);
        dma_release_rx_bounce_buffer(mapping->mapped_address);
    }

    /* CRITICAL: Unlock VDS mapping if used (deferred from ISR) */
    if (mapping->uses_vds && mapping->vds_mapping.needs_unlock) {
        if (!vds_unlock_region_mapped(&mapping->vds_mapping)) {
            LOG_WARNING("VDS unlock failed for RX mapping %p", mapping);
        }
        LOG_DEBUG("VDS RX mapping unlocked");
    }

    /* GPT-5 Critical: Unlock pages if they were locked */
    if (mapping->dma_check.pages_locked) {
        unlock_pages_for_dma(mapping->dma_check.lock_handle);
    }

    /* Update statistics */
    if (g_stats.active_mappings > 0) {
        g_stats.active_mappings--;
    }

    mapping->magic = 0;
    free(mapping);
}

/*==============================================================================
 * Generic mapping functions
 *==============================================================================*/

dma_mapping_t* dma_map_buffer(void *buffer, size_t len, dma_sync_direction_t direction) {
    return dma_map_buffer_flags(buffer, len, direction, 0);
}

dma_mapping_t* dma_map_buffer_flags(void *buffer, size_t len, dma_sync_direction_t direction, uint32_t flags) {
    if (direction == DMA_SYNC_TX) {
        return dma_map_tx_flags(buffer, len, flags);
    } else {
        return dma_map_rx_flags(buffer, len, flags);
    }
}

void dma_unmap_buffer(dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return;
    }

    if (mapping->direction == DMA_SYNC_TX) {
        dma_unmap_tx(mapping);
    } else {
        dma_unmap_rx(mapping);
    }
}

/* Static wrapper for internal use - alias to dma_unmap_buffer */
static void dma_unmap(dma_mapping_t *mapping) {
    dma_unmap_buffer(mapping);
}

/*==============================================================================
 * Mapping information access functions
 *==============================================================================*/

void* dma_mapping_get_address(const dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return NULL;
    }
    return mapping->mapped_address;
}

uint32_t dma_mapping_get_phys_addr(const dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return 0;
    }
    return mapping->phys_addr;
}

size_t dma_mapping_get_length(const dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return 0;
    }
    return mapping->length;
}

int dma_mapping_uses_bounce(const dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return 0;
    }
    return mapping->uses_bounce ? 1 : 0;
}

int dma_mapping_is_coherent(const dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return 0;
    }
    return mapping->is_coherent ? 1 : 0;
}

int dma_mapping_uses_vds(const dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return 0;
    }
    return mapping->uses_vds ? 1 : 0;
}

/*==============================================================================
 * Synchronization functions
 *==============================================================================*/

int dma_mapping_sync_for_device(dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return DMA_MAP_ERROR_NOT_MAPPED;
    }

    if (mapping->is_coherent || (mapping->flags & DMA_MAP_NO_CACHE_SYNC)) {
        return DMA_MAP_SUCCESS;
    }

    /* Call direction-specific cache operation */
    cache_sync_for_device(mapping->mapped_address, mapping->length);
    g_stats.cache_syncs++;

    return DMA_MAP_SUCCESS;
}

int dma_mapping_sync_for_cpu(dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return DMA_MAP_ERROR_NOT_MAPPED;
    }

    if (mapping->is_coherent || (mapping->flags & DMA_MAP_NO_CACHE_SYNC)) {
        return DMA_MAP_SUCCESS;
    }

    /* Call direction-specific cache operation */
    cache_sync_for_cpu(mapping->mapped_address, mapping->length);
    g_stats.cache_syncs++;

    return DMA_MAP_SUCCESS;
}

/**
 * @brief Explicit sync for device access without unmapping
 * GPT-5 Enhancement: Better API expressiveness for streaming mappings
 *
 * @param buffer Buffer to synchronize
 * @param len Buffer length
 * @param direction DMA direction
 * @return 0 on success, negative on error
 */
int dma_sync_for_device_explicit(void *buffer, size_t len, dma_sync_direction_t direction) {
    if (!buffer || len == 0) {
        return DMA_MAP_ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("DMA explicit sync for device: buffer=%p len=%lu dir=%s",
              buffer, (unsigned long)len, (direction == DMA_SYNC_TX) ? "TX" : "RX");

    /* Use the enhanced aligned cache flush for safety */
    cache_flush_aligned_safe(buffer, len);

    /* Check if we should trigger coalesced flush */
    cache_flush_if_needed();

    return DMA_MAP_SUCCESS;
}

/**
 * @brief Explicit sync for CPU access without unmapping
 * GPT-5 Enhancement: Streaming mapping support
 *
 * @param buffer Buffer to synchronize
 * @param len Buffer length
 * @param direction DMA direction
 * @return 0 on success, negative on error
 */
int dma_sync_for_cpu_explicit(void *buffer, size_t len, dma_sync_direction_t direction) {
    if (!buffer || len == 0) {
        return DMA_MAP_ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("DMA explicit sync for CPU: buffer=%p len=%lu dir=%s",
              buffer, (unsigned long)len, (direction == DMA_SYNC_TX) ? "TX" : "RX");

    /* For RX (device->CPU), we need cache invalidation */
    if (direction == DMA_SYNC_RX) {
        cache_flush_aligned_safe(buffer, len);
        cache_flush_if_needed();
    }

    /* For TX, no additional sync needed after device access */

    return DMA_MAP_SUCCESS;
}

/*==============================================================================
 * Device constraint mapping function
 *==============================================================================*/

/**
 * @brief Map DMA buffer with per-NIC device constraints
 *
 * GPT-5 CRITICAL: This function integrates with dma_safety.c to ensure
 * all DMA operations respect device-specific constraints
 *
 * @param buffer Buffer to map
 * @param length Buffer length
 * @param direction DMA direction
 * @param device_name NIC device name (e.g., "3C509B", "3C515TX")
 * @return Mapped DMA descriptor or NULL on failure
 */
dma_mapping_t* dma_map_with_device_constraints(void *buffer, size_t length,
                                              dma_sync_direction_t direction,
                                              const char *device_name) {
    dma_mapping_t *mapping;
    uint32_t phys_addr;

    /* Map normally first */
    mapping = dma_map_buffer_flags(buffer, length, direction, 0);
    if (!mapping) {
        LOG_ERROR("Failed to map buffer for DMA");
        return NULL;
    }

    /* Check with actual physical address if device constraints specified */
    if (device_name) {
        phys_addr = mapping->phys_addr;
        if (!dma_validate_buffer_constraints(device_name, buffer, length, phys_addr)) {
            LOG_DEBUG("Buffer at phys %08lX violates %s constraints, remapping with bounce",
                     (unsigned long)phys_addr, device_name);

            /* Remap with bounce buffer forced */
            dma_unmap(mapping);
            mapping = dma_map_buffer_flags(buffer, length, direction, DMA_MAP_FORCE_BOUNCE);

            /* GPT-5 FIX: Check if remapping succeeded */
            if (!mapping) {
                LOG_ERROR("Failed to remap buffer with bounce for %s constraints", device_name);
                return NULL;
            }

            /* GPT-5 A+ Enhancement: Re-validate bounce buffer meets constraints */
            phys_addr = mapping->phys_addr;
            if (!dma_validate_buffer_constraints(device_name,
                                                dma_mapping_get_address(mapping),
                                                length, phys_addr)) {
                LOG_ERROR("Bounce buffer at phys %08lX still violates %s constraints",
                         (unsigned long)phys_addr, device_name);
                dma_unmap(mapping);
                return NULL;
            }

            LOG_DEBUG("Remapped with bounce buffer at phys %08lX",
                     (unsigned long)mapping->phys_addr);
        }
    }

    return mapping;
}

/*==============================================================================
 * Integration with existing systems (scatter-gather)
 *==============================================================================*/

dma_mapping_t* dma_map_from_sg_descriptor(dma_sg_descriptor_t *sg_desc, dma_sync_direction_t direction) {
    if (!sg_desc) {
        return NULL;
    }

    /* Convert scatter-gather descriptor to buffer mapping */
    return dma_map_buffer_flags(sg_desc->original_buffer, sg_desc->total_length, direction, 0);
}

int dma_mapping_to_sg_list(const dma_mapping_t *mapping, dma_sg_descriptor_t **sg_desc) {
    dma_sg_descriptor_t *desc;

    if (!validate_mapping(mapping) || !sg_desc) {
        return DMA_MAP_ERROR_INVALID_PARAM;
    }

    desc = malloc(sizeof(dma_sg_descriptor_t));
    if (!desc) {
        return DMA_MAP_ERROR_NO_MEMORY;
    }

    /* Initialize the descriptor with a single segment */
    desc->original_buffer = mapping->mapped_address;
    desc->total_length = (uint32_t)mapping->length;
    desc->segment_count = 1;
    desc->uses_bounce = mapping->uses_bounce;
    desc->segments[0].phys_addr = mapping->phys_addr;
    desc->segments[0].length = (uint16_t)mapping->length;
    desc->segments[0].is_bounce = mapping->uses_bounce;
    desc->segments[0].bounce_ptr = mapping->uses_bounce ? mapping->mapped_address : NULL;

    *sg_desc = desc;
    return DMA_MAP_SUCCESS;
}

/*==============================================================================
 * Advanced runtime features
 *==============================================================================*/

int dma_mapping_set_cache_policy(dma_mapping_t *mapping, int coherent) {
    if (!validate_mapping(mapping)) {
        return DMA_MAP_ERROR_NOT_MAPPED;
    }

    mapping->is_coherent = coherent ? true : false;
    if (coherent) {
        mapping->flags |= DMA_MAP_COHERENT;
    } else {
        mapping->flags &= ~DMA_MAP_COHERENT;
    }

    return DMA_MAP_SUCCESS;
}

int dma_mapping_prefault(dma_mapping_t *mapping) {
    uint8_t *addr;
    size_t i;
    volatile uint8_t dummy;

    if (!validate_mapping(mapping)) {
        return DMA_MAP_ERROR_NOT_MAPPED;
    }

    /* Touch every page to ensure it's mapped */
    addr = (uint8_t*)mapping->mapped_address;
    for (i = 0; i < mapping->length; i += 4096) {
        dummy = addr[i];
        (void)dummy;
    }

    /* Touch the last byte */
    if (mapping->length > 0) {
        dummy = addr[mapping->length - 1];
        (void)dummy;
    }

    return DMA_MAP_SUCCESS;
}

int dma_mapping_pin_pages(dma_mapping_t *mapping) {
    /* In DOS, pages are always "pinned" - this is a no-op */
    if (!validate_mapping(mapping)) {
        return DMA_MAP_ERROR_NOT_MAPPED;
    }

    return DMA_MAP_SUCCESS;
}

int dma_mapping_validate(const dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return 0;
    }

    /* Additional validation checks */
    if (!mapping->mapped_address || mapping->length == 0) {
        return 0;
    }

    if (mapping->uses_bounce && mapping->mapped_address == mapping->original_buffer) {
        return 0;
    }

    return 1;
}

/*==============================================================================
 * Performance optimization functions
 *==============================================================================*/

int dma_mapping_is_fast_path_enabled(void) {
    return g_fast_path_enabled ? 1 : 0;
}

uint32_t dma_mapping_get_cache_hit_rate(void) {
    if (g_cache_attempts == 0) {
        return 0;
    }
    return (g_cache_hits * 100) / g_cache_attempts;
}
