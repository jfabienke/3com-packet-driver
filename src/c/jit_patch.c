/**
 * @file jit_patch.c
 * @brief JIT copy-down engine - SMC patching engine (OVERLAY)
 *
 * Applies self-modifying code patches to the built TSR image, baking in
 * hardware-specific values (I/O base, IRQ, MAC address, etc.) and
 * selecting CPU-optimal code variants. This file lives in the OVERLAY
 * section and is discarded after init.
 *
 * 3Com Packet Driver - JIT Copy-Down Engine
 *
 * Last Updated: 2026-02-01 12:05:57 CET
 */

#include "jit_build.h"
#include "logging.h"
#include <string.h>
#include <dos.h>

/* ============================================================================
 * Internal helpers
 * ============================================================================ */

/**
 * Select the appropriate CPU variant from a patch entry.
 * Returns pointer to the variant code (5 bytes max) or NULL on error.
 */
static const uint8_t *select_cpu_variant(const patch_entry_t far *entry,
                                          uint16_t cpu_type)
{
    /*
     * cpu_type values: 0=8086, 2=286, 3=386, 4=486, 5=Pentium+
     * Fallback: use highest variant that doesn't exceed detected CPU.
     */
    if (cpu_type >= 5) {
        return (const uint8_t *)entry->cpu_pentium;
    } else if (cpu_type >= 4) {
        return (const uint8_t *)entry->cpu_486;
    } else if (cpu_type >= 3) {
        return (const uint8_t *)entry->cpu_386;
    } else if (cpu_type >= 2) {
        return (const uint8_t *)entry->cpu_286;
    } else {
        return (const uint8_t *)entry->cpu_8086;
    }
}

/* ============================================================================
 * jit_apply_one_patch
 * ============================================================================ */

/**
 * Apply a single patch entry to the TSR image.
 *
 * @param image             Far pointer to TSR image buffer
 * @param image_size        Total image size in bytes
 * @param entry             Far pointer to patch table entry
 * @param hw                Hardware values for patching
 * @param module_base_offset Offset of this module's hot section in image
 *
 * Returns 0 on success, negative on error.
 */
int jit_apply_one_patch(uint8_t far *image, uint16_t image_size,
                        const patch_entry_t far *entry,
                        const jit_hw_values_t *hw,
                        uint16_t module_base_offset)
{
    uint16_t patch_abs;     /* Absolute offset in TSR image */
    uint8_t far *target;    /* Pointer to patch location */
    const uint8_t *variant;

    if (image == NULL || entry == NULL || hw == NULL) {
        LOG_ERROR("jit_apply_one_patch: NULL argument");
        return -1;
    }

    /* Calculate absolute image offset for this patch */
    patch_abs = module_base_offset + entry->patch_offset;

    /* Bounds check */
    if (patch_abs + entry->patch_size > image_size) {
        LOG_ERROR("jit_apply_one_patch: Patch at 0x%04X size %u exceeds image "
                  "(0x%04X)", patch_abs, entry->patch_size, image_size);
        return -2;
    }

    target = image + patch_abs;

    switch (entry->patch_type) {

    case PATCH_TYPE_IO:
        /* Replace placeholder with actual I/O base address (16-bit) */
        if (entry->patch_size < 2) {
            LOG_ERROR("jit_apply_one_patch: IO patch size too small (%u)",
                      entry->patch_size);
            return -3;
        }
        target[0] = (uint8_t)(hw->io_base & 0xFF);
        target[1] = (uint8_t)(hw->io_base >> 8);
        LOG_DEBUG("jit_apply_one_patch: IO patch at 0x%04X = 0x%04X",
                  patch_abs, hw->io_base);
        break;

    case PATCH_TYPE_IMM8:
        /* Replace with 8-bit immediate value */
        if (entry->patch_size < 1) {
            LOG_ERROR("jit_apply_one_patch: IMM8 patch size too small");
            return -3;
        }
        /*
         * Determine which 8-bit value to use based on the default content.
         * Convention: cpu_8086[0] holds a tag byte indicating which hw field:
         *   0x01 = irq_number, 0x02 = dma_channel, 0x03 = cache_line_size
         */
        {
            uint8_t tag;
            tag = entry->cpu_8086[0];
            switch (tag) {
            case 0x01:
                target[0] = hw->irq_number;
                LOG_DEBUG("jit_apply_one_patch: IMM8 IRQ=%u at 0x%04X",
                          hw->irq_number, patch_abs);
                break;
            case 0x02:
                target[0] = hw->dma_channel;
                LOG_DEBUG("jit_apply_one_patch: IMM8 DMA=%u at 0x%04X",
                          hw->dma_channel, patch_abs);
                break;
            case 0x03:
                target[0] = hw->cache_line_size;
                LOG_DEBUG("jit_apply_one_patch: IMM8 CacheLine=%u at 0x%04X",
                          hw->cache_line_size, patch_abs);
                break;
            default:
                LOG_ERROR("jit_apply_one_patch: Unknown IMM8 tag 0x%02X",
                          tag);
                return -4;
            }
        }
        break;

    case PATCH_TYPE_IMM16:
        /* Replace with 16-bit immediate value */
        if (entry->patch_size < 2) {
            LOG_ERROR("jit_apply_one_patch: IMM16 patch size too small");
            return -3;
        }
        /*
         * Convention: cpu_8086[0] holds a tag byte:
         *   0x01 = io_base, 0x02 = nic_type, 0x03 = cpu_type, 0x04 = flags
         */
        {
            uint8_t tag;
            uint16_t val;

            tag = entry->cpu_8086[0];
            switch (tag) {
            case 0x01: val = hw->io_base;   break;
            case 0x02: val = hw->nic_type;  break;
            case 0x03: val = hw->cpu_type;  break;
            case 0x04: val = hw->flags;     break;
            default:
                LOG_ERROR("jit_apply_one_patch: Unknown IMM16 tag 0x%02X",
                          tag);
                return -4;
            }
            target[0] = (uint8_t)(val & 0xFF);
            target[1] = (uint8_t)(val >> 8);
            LOG_DEBUG("jit_apply_one_patch: IMM16 tag=0x%02X val=0x%04X "
                      "at 0x%04X", tag, val, patch_abs);
        }
        break;

    case PATCH_TYPE_COPY:
        /* Copy N bytes from hw_values (e.g., MAC address) */
        /*
         * Convention: cpu_8086[0] = source field tag, cpu_8086[1] = byte offset
         *   Tag 0x01 = mac_addr (6 bytes)
         */
        {
            uint8_t tag;
            tag = entry->cpu_8086[0];
            if (tag == 0x01) {
                /* MAC address copy */
                if (entry->patch_size < 6) {
                    LOG_ERROR("jit_apply_one_patch: COPY MAC patch size "
                              "too small (%u)", entry->patch_size);
                    return -3;
                }
                _fmemcpy(target, hw->mac_addr, 6);
                LOG_DEBUG("jit_apply_one_patch: COPY MAC at 0x%04X",
                          patch_abs);
            } else {
                LOG_ERROR("jit_apply_one_patch: Unknown COPY tag 0x%02X",
                          tag);
                return -4;
            }
        }
        break;

    case PATCH_TYPE_BRANCH:
        /* Select CPU-appropriate code variant */
        variant = select_cpu_variant(entry, hw->cpu_type);
        if (variant == NULL) {
            LOG_ERROR("jit_apply_one_patch: No variant for CPU %u",
                      hw->cpu_type);
            return -5;
        }
        _fmemcpy(target, (const void far *)variant, entry->patch_size);
        LOG_DEBUG("jit_apply_one_patch: BRANCH variant for CPU %u at 0x%04X "
                  "(%u bytes)", hw->cpu_type, patch_abs, entry->patch_size);
        break;

    case PATCH_TYPE_NOP:
        /* Fill patch area with NOPs to disable code path */
        _fmemset(target, 0x90, entry->patch_size);
        LOG_DEBUG("jit_apply_one_patch: NOP fill %u bytes at 0x%04X",
                  entry->patch_size, patch_abs);
        break;

    case PATCH_TYPE_RELOC_NEAR:
        /* Handled by jit_reloc.c -- skip here */
        LOG_DEBUG("jit_apply_one_patch: Skipping RELOC_NEAR at 0x%04X "
                  "(handled by relocator)", patch_abs);
        break;

    default:
        LOG_ERROR("jit_apply_one_patch: Unknown patch type 0x%02X at 0x%04X",
                  entry->patch_type, patch_abs);
        return -6;
    }

    return 0;
}

/* ============================================================================
 * jit_apply_patches
 * ============================================================================ */

/**
 * Apply all SMC patches to the built TSR image.
 *
 * For each selected module, walks its patch table and applies patches
 * based on type and hardware values.
 *
 * Returns count of patches applied, negative on error.
 */
int jit_apply_patches(jit_layout_t *layout, const jit_hw_values_t *hw)
{
    mod_selection_t *sel;
    const mod_registry_entry_t *reg;
    const module_header_t far *hdr;
    const patch_entry_t far *patch_table;
    jit_layout_entry_t *le;
    int total_patches;
    uint16_t i;
    uint16_t j;
    int rc;

    if (layout == NULL || hw == NULL) {
        LOG_ERROR("jit_apply_patches: NULL argument");
        return -1;
    }

    if (layout->image_base == NULL) {
        LOG_ERROR("jit_apply_patches: NULL image base");
        return -2;
    }

    sel = get_module_selection();
    if (sel == NULL || sel->count == 0) {
        LOG_ERROR("jit_apply_patches: No modules selected");
        return -3;
    }

    LOG_DEBUG("jit_apply_patches: Applying patches for %u modules, "
              "IO=0x%04X IRQ=%u", sel->count, hw->io_base, hw->irq_number);

    total_patches = 0;

    for (i = 0; i < sel->count; i++) {
        reg = mod_registry_get(sel->selected[i]);
        if (reg == NULL) {
            LOG_ERROR("jit_apply_patches: Unknown module ID %d",
                      sel->selected[i]);
            return -4;
        }

        hdr = (const module_header_t far *)reg->header_ptr;
        if (hdr == NULL) {
            LOG_ERROR("jit_apply_patches: NULL header for module %s",
                      reg->name);
            return -5;
        }

        /* Find this module's layout entry */
        le = jit_get_layout_entry(layout, sel->selected[i]);
        if (le == NULL) {
            LOG_ERROR("jit_apply_patches: Module %s not in layout", reg->name);
            return -6;
        }

        /* Skip modules with no patches */
        if (hdr->patch_count == 0) {
            LOG_DEBUG("jit_apply_patches: Module %s has no patches", reg->name);
            continue;
        }

        /* Get pointer to patch table */
        patch_table = (const patch_entry_t far *)
                      ((uint8_t far *)hdr + hdr->patch_table_offset);

        LOG_DEBUG("jit_apply_patches: Module %s: %u patches",
                  reg->name, hdr->patch_count);

        /* Apply each patch in this module */
        for (j = 0; j < hdr->patch_count; j++) {
            rc = jit_apply_one_patch(
                layout->image_base,
                layout->image_size,
                &patch_table[j],
                hw,
                le->dst_offset
            );

            if (rc < 0) {
                LOG_ERROR("jit_apply_patches: Patch %u in module %s failed "
                          "(rc=%d)", j, reg->name, rc);
                return -7;
            }

            total_patches++;
        }
    }

    LOG_DEBUG("jit_apply_patches: Applied %d patches total", total_patches);
    return total_patches;
}
