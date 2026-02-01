/**
 * @file jit_image.h
 * @brief JIT TSR image header definition for two-stage loader
 *
 * Defines the header structure placed at offset 0 of the JIT-built
 * TSR image. The Stage 1 loader reads this header to install interrupt
 * vectors and locate key entry points within the flat TSR image.
 *
 * Last Updated: 2026-02-01 18:20:35 CET
 */

#ifndef JIT_IMAGE_H
#define JIT_IMAGE_H

#include <stdint.h>

/* Image magic: "JITS" in little-endian (0x5354494A) */
#define JIT_IMAGE_MAGIC     0x5354494AUL

/* Current image format version */
#define JIT_IMAGE_VERSION   1

/**
 * JIT TSR image header - placed at offset 0 of the built image.
 *
 * The JIT engine populates this after building the image. The Stage 1
 * loader reads it to install vectors and allocate the DOS memory block.
 */
typedef struct {
    uint32_t magic;             /* "JITS" (0x5354494A) */
    uint16_t version;           /* Image format version */
    uint16_t image_size;        /* Total image size in bytes */
    uint16_t pktapi_offset;     /* INT 60h handler entry point offset */
    uint16_t idle_offset;       /* INT 28h handler entry point offset */
    uint16_t irq_offset;        /* Hardware IRQ handler entry point offset */
    uint16_t data_offset;       /* BSS/data section start offset */
    uint16_t data_size;         /* BSS/data section size in bytes */
    uint16_t stack_offset;      /* Private stack base offset */
    uint16_t stack_size;        /* Private stack size in bytes */
    uint16_t uninstall_offset;  /* Uninstall handler entry point offset */
    uint8_t  irq_number;        /* Hardware IRQ number (for vector install) */
    uint8_t  int_number;        /* Software INT number (default 0x60) */
    uint8_t  reserved[8];       /* Reserved for future use (pad to 32) */
} jit_image_header_t;

/* Structure size: 32 bytes (4+2+2+2+2+2+2+2+2+2+2+1+1+8) */

#endif /* JIT_IMAGE_H */
