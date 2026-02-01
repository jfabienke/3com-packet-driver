/**
 * @file jit_reloc.c
 * @brief JIT copy-down engine - Near call/jump relocation (OVERLAY)
 *
 * After copy-down moves modules to new contiguous offsets, inter-module
 * CALL/JMP instructions must be relocated to reflect the new layout.
 * This file processes PATCH_TYPE_RELOC_NEAR entries from each module's
 * patch table. This file lives in the OVERLAY section and is discarded
 * after init.
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
 * jit_resolve_near
 * ============================================================================ */

/**
 * Resolve a single near CALL/JMP relocation.
 *
 * Calculates the new 16-bit relative offset for a near CALL or JMP
 * instruction that references another module's hot section.
 *
 * For x86 near CALL/JMP, the operand is: target_addr - (patch_addr + 2)
 * where patch_addr is the address of the 16-bit operand itself.
 *
 * @param layout            The TSR image layout
 * @param patch_image_offset Absolute offset in TSR image of the CALL/JMP operand
 * @param target_module     Target module ID
 * @param target_hot_offset Offset within the target module's hot section
 *
 * Returns 0 on success, negative on error.
 */
int jit_resolve_near(jit_layout_t *layout,
                     uint16_t patch_image_offset,
                     module_id_t target_module,
                     uint16_t target_hot_offset)
{
    jit_layout_entry_t *target_entry;
    uint16_t target_addr;
    uint16_t patch_addr;
    int16_t  rel_offset;
    uint8_t far *operand;

    if (layout == NULL || layout->image_base == NULL) {
        LOG_ERROR("jit_resolve_near: NULL layout or image");
        return -1;
    }

    /* Bounds check on patch location (need 2 bytes for the operand) */
    if (patch_image_offset + 2 > layout->image_size) {
        LOG_ERROR("jit_resolve_near: Patch at 0x%04X exceeds image size 0x%04X",
                  patch_image_offset, layout->image_size);
        return -2;
    }

    /* Find target module's layout entry */
    target_entry = jit_get_layout_entry(layout, target_module);
    if (target_entry == NULL) {
        LOG_ERROR("jit_resolve_near: Target module %d not in layout",
                  target_module);
        return -3;
    }

    /* Validate target offset is within the target module's hot section */
    if (target_hot_offset >= target_entry->src_size) {
        LOG_ERROR("jit_resolve_near: Target offset 0x%04X exceeds module "
                  "hot size 0x%04X", target_hot_offset, target_entry->src_size);
        return -4;
    }

    /* Calculate absolute addresses within the TSR image */
    target_addr = target_entry->dst_offset + target_hot_offset;
    patch_addr  = patch_image_offset;

    /*
     * x86 near CALL/JMP encoding:
     *   E8 xx xx   (CALL rel16)
     *   E9 xx xx   (JMP  rel16)
     *
     * The operand (xx xx) = target - (operand_address + 2)
     * The +2 accounts for the 2-byte operand itself.
     */
    rel_offset = (int16_t)(target_addr - (patch_addr + 2));

    /* Write the 16-bit relative offset (little-endian) */
    operand = layout->image_base + patch_image_offset;
    operand[0] = (uint8_t)(rel_offset & 0xFF);
    operand[1] = (uint8_t)((rel_offset >> 8) & 0xFF);

    LOG_DEBUG("jit_resolve_near: Reloc at 0x%04X -> module %d+0x%04X "
              "(abs 0x%04X, rel %d)",
              patch_image_offset, target_module, target_hot_offset,
              target_addr, rel_offset);

    return 0;
}

/* ============================================================================
 * jit_apply_relocations
 * ============================================================================ */

/**
 * Apply all near CALL/JMP relocations after copy-down.
 *
 * Walks every selected module's patch table looking for PATCH_TYPE_RELOC_NEAR
 * entries. For each one:
 *   1. Find the source module's new base in the layout
 *   2. Find the target module's new base in the layout
 *   3. Calculate and write the new relative offset
 *
 * RELOC_NEAR patch convention:
 *   patch_entry_t.patch_offset  = offset of the CALL/JMP operand within
 *                                  the module's hot section
 *   patch_entry_t.cpu_8086[0-1] = target module ID (uint16_t LE)
 *   patch_entry_t.cpu_8086[2-3] = target offset within hot section (uint16_t LE)
 *
 * Returns count of relocations applied, negative on error.
 */
int jit_apply_relocations(jit_layout_t *layout)
{
    mod_selection_t *sel;
    const mod_registry_entry_t *reg;
    const module_header_t far *hdr;
    const patch_entry_t far *patch_table;
    const patch_entry_t far *pe;
    jit_layout_entry_t *src_entry;
    int total_relocs;
    uint16_t i;
    uint16_t j;
    int rc;

    if (layout == NULL) {
        LOG_ERROR("jit_apply_relocations: NULL layout");
        return -1;
    }

    if (layout->image_base == NULL) {
        LOG_ERROR("jit_apply_relocations: NULL image base");
        return -2;
    }

    sel = get_module_selection();
    if (sel == NULL || sel->count == 0) {
        LOG_ERROR("jit_apply_relocations: No modules selected");
        return -3;
    }

    LOG_DEBUG("jit_apply_relocations: Processing %u modules", sel->count);

    total_relocs = 0;

    for (i = 0; i < sel->count; i++) {
        reg = mod_registry_get(sel->selected[i]);
        if (reg == NULL) {
            LOG_ERROR("jit_apply_relocations: Unknown module ID %d",
                      sel->selected[i]);
            return -4;
        }

        hdr = (const module_header_t far *)reg->header_ptr;
        if (hdr == NULL) {
            LOG_ERROR("jit_apply_relocations: NULL header for module %s",
                      reg->name);
            return -5;
        }

        /* Skip modules with no patches */
        if (hdr->patch_count == 0) {
            continue;
        }

        /* Find source module's layout entry */
        src_entry = jit_get_layout_entry(layout, sel->selected[i]);
        if (src_entry == NULL) {
            LOG_ERROR("jit_apply_relocations: Module %s not in layout",
                      reg->name);
            return -6;
        }

        /* Get pointer to patch table */
        patch_table = (const patch_entry_t far *)
                      ((uint8_t far *)hdr + hdr->patch_table_offset);

        /* Scan for RELOC_NEAR entries */
        for (j = 0; j < hdr->patch_count; j++) {
            pe = &patch_table[j];

            if (pe->patch_type != PATCH_TYPE_RELOC_NEAR) {
                continue;
            }

            /* Extract target module and offset from the patch entry */
            {
                uint16_t target_mod_id;
                uint16_t target_off;
                uint16_t patch_abs;

                /* Target info encoded in cpu_8086 field (convention) */
                target_mod_id = (uint16_t)pe->cpu_8086[0] |
                                ((uint16_t)pe->cpu_8086[1] << 8);
                target_off    = (uint16_t)pe->cpu_8086[2] |
                                ((uint16_t)pe->cpu_8086[3] << 8);

                /* Calculate absolute image offset for the operand */
                patch_abs = src_entry->dst_offset + pe->patch_offset;

                LOG_DEBUG("jit_apply_relocations: Module %s patch %u: "
                          "RELOC_NEAR at img+0x%04X -> mod %u+0x%04X",
                          reg->name, j, patch_abs, target_mod_id, target_off);

                rc = jit_resolve_near(layout,
                                      patch_abs,
                                      (module_id_t)target_mod_id,
                                      target_off);
                if (rc < 0) {
                    LOG_ERROR("jit_apply_relocations: Failed to resolve "
                              "reloc in module %s patch %u (rc=%d)",
                              reg->name, j, rc);
                    return -7;
                }

                total_relocs++;
            }
        }
    }

    LOG_DEBUG("jit_apply_relocations: Applied %d relocations", total_relocs);
    return total_relocs;
}
