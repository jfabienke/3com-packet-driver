/**
 * @file stage1.h
 * @brief Two-stage loader handoff structures and DOS memory helpers
 *
 * Defines the interface between the Stage 1 loader (3cpdinit.exe) and
 * the resident TSR image. Stage 1 runs all init stages, builds the JIT
 * image, allocates a DOS memory block, copies the image, installs
 * vectors, and exits normally (freeing all Stage 1 memory).
 *
 * Last Updated: 2026-02-01 18:20:35 CET
 */

#ifndef STAGE1_H
#define STAGE1_H

#include <stdint.h>
#include <dos.h>
#include "jit_image.h"

/* ============================================================================
 * DOS Memory Block Allocation (INT 21h/48h)
 * ============================================================================ */

/**
 * Allocate a DOS conventional memory block.
 *
 * @param paragraphs  Number of 16-byte paragraphs to allocate
 * @return Segment of allocated block, or 0 on failure
 */
static uint16_t dos_alloc_block(uint16_t paragraphs)
{
    union REGS regs;

    regs.h.ah = 0x48;
    regs.w.bx = paragraphs;
    intdos(&regs, &regs);

    if (regs.w.cflag & 1) {
        return 0;  /* Allocation failed */
    }
    return regs.w.ax;  /* Segment of allocated block */
}

/**
 * Free a DOS conventional memory block.
 *
 * @param segment  Segment of block to free (from dos_alloc_block)
 * @return 0 on success, -1 on failure
 */
static int dos_free_block(uint16_t segment)
{
    union REGS regs;
    struct SREGS sregs;

    regs.h.ah = 0x49;
    segread(&sregs);
    sregs.es = segment;
    intdosx(&regs, &regs, &sregs);

    if (regs.w.cflag & 1) {
        return -1;
    }
    return 0;
}

/* ============================================================================
 * Interrupt Vector Management
 * ============================================================================ */

/**
 * Saved original interrupt vectors for uninstall.
 * Stored at a known offset in the TSR image data section.
 */
typedef struct {
    uint32_t orig_int60;        /* Original INT 60h vector (seg:off) */
    uint32_t orig_int28;        /* Original INT 28h vector (seg:off) */
    uint32_t orig_irq;          /* Original hardware IRQ vector */
    uint16_t tsr_segment;       /* Segment of our DOS memory block */
    uint16_t tsr_paragraphs;    /* Size in paragraphs (for free) */
} tsr_install_info_t;

/**
 * Install interrupt vectors pointing into the TSR image.
 *
 * @param tsr_seg  Segment of TSR memory block
 * @param hdr      Pointer to JIT image header in the TSR block
 */
static void install_vectors(uint16_t tsr_seg, jit_image_header_t far *hdr)
{
    union REGS regs;
    struct SREGS sregs;

    /* Save and set INT 60h (packet driver API) */
    /* Get current vector */
    regs.h.ah = 0x35;
    regs.h.al = hdr->int_number;
    intdosx(&regs, &regs, &sregs);
    /* sregs.es:regs.w.bx = old vector - store in TSR data area later */

    /* Set new vector */
    regs.h.ah = 0x25;
    regs.h.al = hdr->int_number;
    regs.w.dx = hdr->pktapi_offset;
    segread(&sregs);
    sregs.ds = tsr_seg;
    intdosx(&regs, &regs, &sregs);

    /* Save and set INT 28h (DOS idle) if we have an idle handler */
    if (hdr->idle_offset != 0) {
        regs.h.ah = 0x25;
        regs.h.al = 0x28;
        regs.w.dx = hdr->idle_offset;
        segread(&sregs);
        sregs.ds = tsr_seg;
        intdosx(&regs, &regs, &sregs);
    }

    /* Set hardware IRQ vector */
    if (hdr->irq_offset != 0 && hdr->irq_number != 0xFF) {
        uint8_t irq_vec;
        if (hdr->irq_number < 8) {
            irq_vec = 0x08 + hdr->irq_number;
        } else {
            irq_vec = 0x70 + (hdr->irq_number - 8);
        }

        regs.h.ah = 0x25;
        regs.h.al = irq_vec;
        regs.w.dx = hdr->irq_offset;
        segread(&sregs);
        sregs.ds = tsr_seg;
        intdosx(&regs, &regs, &sregs);
    }
}

#endif /* STAGE1_H */
