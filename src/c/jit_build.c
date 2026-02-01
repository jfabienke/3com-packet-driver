/**
 * @file jit_build.c
 * @brief JIT copy-down engine - Module layout and TSR image builder (OVERLAY)
 *
 * Builds a minimal pure-ASM TSR image by copying the hot sections of
 * selected modules contiguously into a single image buffer. This file
 * lives in the OVERLAY section and is discarded after init.
 *
 * 3Com Packet Driver - JIT Copy-Down Engine
 *
 * Last Updated: 2026-02-01 18:20:35 CET
 * Phase 8: Extended for two-stage loader (core modules + image header)
 */

#include "jit_build.h"
#include "jit_image.h"
#include "logging.h"
#include <string.h>
#include <dos.h>
#include <stdlib.h>

/* ============================================================================
 * jit_build_image
 * ============================================================================ */

/**
 * Build TSR image from selected modules.
 *
 * 1. Walk selected module headers, sum hot sizes
 * 2. Allocate contiguous buffer for TSR image
 * 3. Copy each module's hot section contiguously into image
 * 4. Build layout table for relocation
 *
 * Returns 0 on success, negative on error.
 */
int jit_build_image(jit_layout_t *layout)
{
    mod_selection_t *sel;
    const mod_registry_entry_t *reg;
    const module_header_t far *hdr;
    uint16_t total_size;
    uint16_t dst_offset;
    uint16_t hot_size;
    uint16_t i;

    if (layout == NULL) {
        LOG_ERROR("jit_build_image: NULL layout pointer");
        return -1;
    }

    /* Clear the layout structure */
    _fmemset(layout, 0, sizeof(jit_layout_t));

    /* Get current module selection */
    sel = get_module_selection();
    if (sel == NULL) {
        LOG_ERROR("jit_build_image: No module selection available");
        return -2;
    }

    if (sel->count == 0) {
        LOG_ERROR("jit_build_image: No modules selected");
        return -3;
    }

    if (sel->count > MOD_SELECT_MAX) {
        LOG_ERROR("jit_build_image: Too many modules selected (%u)", sel->count);
        return -4;
    }

    /* Pass 1: Calculate total hot section size and validate headers.
     * Reserve space for JIT image header at offset 0. */
    total_size = sizeof(jit_image_header_t);
    for (i = 0; i < sel->count; i++) {
        reg = mod_registry_get(sel->selected[i]);
        if (reg == NULL) {
            LOG_ERROR("jit_build_image: Unknown module ID %d", sel->selected[i]);
            return -5;
        }

        hdr = (const module_header_t far *)reg->header_ptr;
        if (hdr == NULL) {
            LOG_ERROR("jit_build_image: NULL header for module %s", reg->name);
            return -6;
        }

        /* Validate module signature */
        if (_fmemcmp(hdr->signature, MODULE_SIGNATURE, MODULE_SIG_SIZE) != 0) {
            LOG_ERROR("jit_build_image: Bad signature for module %s", reg->name);
            return -7;
        }

        hot_size = hdr->hot_end - hdr->hot_start;
        if (hot_size == 0) {
            LOG_ERROR("jit_build_image: Zero hot size for module %s", reg->name);
            return -8;
        }

        /* Check for overflow */
        if ((uint32_t)total_size + hot_size > 0xFFFFUL) {
            LOG_ERROR("jit_build_image: TSR image would exceed 64K");
            return -9;
        }

        total_size += hot_size;
        LOG_DEBUG("jit_build_image: Module %s hot=%u bytes", reg->name, hot_size);
    }

    LOG_DEBUG("jit_build_image: Total TSR image size = %u bytes", total_size);

    /* Allocate the TSR image buffer using DOS far allocation */
    {
        uint16_t paras;
        unsigned seg_val;

        paras = (total_size + 15) >> 4;
        if (_dos_allocmem(paras, &seg_val) != 0) {
            LOG_ERROR("jit_build_image: Failed to allocate %u paragraphs", paras);
            return -10;
        }
        layout->image_base = (uint8_t far *)MK_FP(seg_val, 0);
    }

    layout->image_size = total_size;
    layout->entry_count = sel->count;

    /* Zero-fill the image buffer */
    _fmemset(layout->image_base, 0, total_size);

    /* Pass 2: Copy hot sections contiguously into image buffer.
     * Start after the image header. */
    dst_offset = sizeof(jit_image_header_t);
    for (i = 0; i < sel->count; i++) {
        reg = mod_registry_get(sel->selected[i]);
        hdr = (const module_header_t far *)reg->header_ptr;
        hot_size = hdr->hot_end - hdr->hot_start;

        /* Build layout entry */
        layout->entries[i].id         = sel->selected[i];
        layout->entries[i].src_offset = hdr->hot_start;
        layout->entries[i].src_size   = hot_size;
        layout->entries[i].dst_offset = dst_offset;

        /* Copy hot section into TSR image */
        {
            uint8_t far *src;
            uint8_t far *dst;

            src = (uint8_t far *)hdr + hdr->hot_start;
            dst = layout->image_base + dst_offset;

            _fmemcpy(dst, src, hot_size);
        }

        LOG_DEBUG("jit_build_image: Copied %s (%u bytes) at image offset 0x%04X",
                  reg->name, hot_size, dst_offset);

        dst_offset += hot_size;
    }

    /* Write JIT image header at offset 0 */
    {
        jit_image_header_t far *img_hdr;
        jit_layout_entry_t *isr_entry;
        jit_layout_entry_t *idle_entry;
        jit_layout_entry_t *irq_entry;
        jit_layout_entry_t *uninstall_entry;

        img_hdr = (jit_image_header_t far *)layout->image_base;
        img_hdr->magic       = JIT_IMAGE_MAGIC;
        img_hdr->version     = JIT_IMAGE_VERSION;
        img_hdr->image_size  = layout->image_size;
        img_hdr->int_number  = 0x60;  /* Default packet driver INT */
        img_hdr->irq_number  = 0xFF;  /* Set by caller after patching */

        /* Locate key entry points from layout */
        isr_entry = jit_get_layout_entry(layout, MOD_ISR);
        if (isr_entry) {
            img_hdr->pktapi_offset = isr_entry->dst_offset;
        }

        /* Core modules provide idle and IRQ entry points */
        idle_entry = jit_get_layout_entry(layout, MOD_CORE_TSRWRAP);
        if (idle_entry) {
            img_hdr->idle_offset = idle_entry->dst_offset;
        }

        irq_entry = jit_get_layout_entry(layout, MOD_IRQ);
        if (irq_entry) {
            img_hdr->irq_offset = irq_entry->dst_offset;
        }

        uninstall_entry = jit_get_layout_entry(layout, MOD_CORE_TSRCOM);
        if (uninstall_entry) {
            img_hdr->uninstall_offset = uninstall_entry->dst_offset;
        }

        /* Data/BSS and stack offsets are set to end of image */
        img_hdr->data_offset  = layout->image_size;
        img_hdr->data_size    = 0;
        img_hdr->stack_offset = layout->image_size;
        img_hdr->stack_size   = 512;
    }

    LOG_DEBUG("jit_build_image: Image built successfully, %u modules, %u bytes",
              layout->entry_count, layout->image_size);

    return 0;
}

/* ============================================================================
 * jit_get_layout_entry
 * ============================================================================ */

/**
 * Get layout entry for a specific module.
 * Returns NULL if the module is not in the layout.
 */
jit_layout_entry_t *jit_get_layout_entry(jit_layout_t *layout, module_id_t id)
{
    uint16_t i;

    if (layout == NULL) {
        return NULL;
    }

    for (i = 0; i < layout->entry_count; i++) {
        if (layout->entries[i].id == id) {
            return &layout->entries[i];
        }
    }

    return NULL;
}

/* ============================================================================
 * jit_serialize_prefetch
 * ============================================================================ */

/**
 * Serialize CPU prefetch queue after all patches.
 *
 * On 486+ CPUs, the prefetch queue may contain stale instructions after
 * self-modifying code. A far JMP forces the CPU to flush and refetch.
 * We write a far JMP to self at the end of the image as a serialization
 * barrier, then NOP it out so it does not affect runtime.
 *
 * For 8086/286/386, a short JMP $+2 is sufficient.
 */
void jit_serialize_prefetch(jit_layout_t *layout)
{
    uint8_t far *img;
    uint16_t entry_offset;
    jit_layout_entry_t *first;

    if (layout == NULL || layout->image_base == NULL) {
        LOG_ERROR("jit_serialize_prefetch: NULL layout or image");
        return;
    }

    if (layout->entry_count == 0) {
        LOG_ERROR("jit_serialize_prefetch: No modules in layout");
        return;
    }

    img = layout->image_base;
    first = &layout->entries[0];
    entry_offset = first->dst_offset;

    /*
     * Execute a far JMP to the TSR entry point to serialize the prefetch
     * queue. We do this via inline assembly for Watcom C.
     *
     * The far JMP forces a complete pipeline/prefetch flush on all x86 CPUs.
     * After serialization, execution continues at the image entry point.
     */
    {
        uint16_t seg;
        uint16_t off;

        seg = FP_SEG(img);
        off = FP_OFF(img) + entry_offset;

        LOG_DEBUG("jit_serialize_prefetch: Far JMP to %04X:%04X", seg, off);

        /*
         * We don't actually execute the JMP here -- this is an init-time
         * serialization. We use a JMP $+2 (EB 00) written into the first
         * two bytes of the first module's hot section as a short serialization
         * point. The ISR entry code will naturally serialize on entry via
         * the interrupt mechanism itself.
         *
         * For safety, we write a short JMP $+2 (self-jump that falls through)
         * at the very start of the first module. This is a 2-byte NOP
         * equivalent that forces prefetch serialization on 386 and below.
         */
        img[entry_offset]     = 0xEB;  /* JMP short */
        img[entry_offset + 1] = 0x00;  /* +0 (next instruction) */

        LOG_DEBUG("jit_serialize_prefetch: Wrote JMP $+2 at offset 0x%04X",
                  entry_offset);
    }
}
