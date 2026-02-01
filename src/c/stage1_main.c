/**
 * @file stage1_main.c
 * @brief Stage 1 loader entry point for two-stage TSR architecture
 *
 * 3CPDINIT.EXE - Normal DOS program that:
 *   1. Runs all 15 init stages (hardware detection, NIC setup, etc.)
 *   2. Builds extended JIT image (core + selected modules)
 *   3. Allocates DOS memory block for TSR
 *   4. Copies image to block and installs interrupt vectors
 *   5. Exits normally (DOS frees all Stage 1 memory)
 *
 * The resident TSR is a pure-ASM flat image with zero CRT dependency.
 *
 * Last Updated: 2026-02-01 18:20:35 CET
 */

#include "dos_io.h"
#include "init_context.h"
#include "mod_select.h"
#include "jit_build.h"
#include "jit_image.h"
#include "stage1.h"
#include "logging.h"
#include <string.h>
#include <dos.h>

/* Banner */
static const char banner[] =
    "3Com EtherLink III Packet Driver Loader v2.0\r\n"
    "Copyright (c) 2026 - Two-Stage TSR Architecture\r\n";

/* ============================================================================
 * Forward declarations (from init_main.c)
 * ============================================================================ */
extern int run_init_stages(int argc, char far * far *argv);
extern init_context_t g_init_ctx;

/* ============================================================================
 * Stage 1 Main
 * ============================================================================ */

int main(int argc, char *argv[])
{
    int result;
    jit_layout_t jit_layout;
    jit_hw_values_t jit_hw;
    jit_image_header_t far *img_hdr;
    uint16_t paras;
    uint16_t tsr_seg;

    /* Print banner */
    dos_printf("%s\r\n", banner);

    /* ======================================================================
     * Step 1: Run all 15 init stages
     * This performs CPU detection, hardware probing, NIC initialization,
     * module selection, and JIT image building.
     * ====================================================================== */
    dos_printf("Initializing...\r\n");

    result = run_init_stages(argc, (char far * far *)argv);
    if (result != 0) {
        dos_printf("ERROR: Initialization failed (stage %d, code %d)\r\n",
                   g_init_ctx.error_stage, g_init_ctx.error_code);
        if (g_init_ctx.error_msg[0] != '\0') {
            dos_printf("  %s\r\n", g_init_ctx.error_msg);
        }
        return 1;
    }

    dos_printf("Hardware detected: ");
    if (g_init_ctx.num_nics > 0) {
        switch (g_init_ctx.nics[0].type) {
        case 1: dos_printf("3C509B (ISA)"); break;
        case 2: dos_printf("3C515 (ISA)"); break;
        case 3: dos_printf("3C590/595 (PCI/PIO)"); break;
        case 4: dos_printf("3C900/905 (PCI/DMA)"); break;
        case 5: dos_printf("3C905B (PCI/DMA+csum)"); break;
        case 6: dos_printf("3C905C (PCI/DMA+SG)"); break;
        default: dos_printf("Unknown NIC type %d", g_init_ctx.nics[0].type);
        }
        dos_printf(" at IO=%04Xh IRQ=%d\r\n",
                   g_init_ctx.io1_base, g_init_ctx.irq1);
    }

    /* ======================================================================
     * Step 2: The JIT image was already built by run_init_stages().
     * Retrieve the layout from the init context.
     * ====================================================================== */

    /*
     * The JIT image is already built in run_init_stages() (init_main.c).
     * The image_base and image_size are stored in g_init_ctx.
     * We need to access the jit_layout that was built - for now we
     * rebuild it since the layout is local to run_init_stages().
     */

    /* Re-run JIT build to get the layout handle.
     * This is safe because module selection is already done and the
     * build is deterministic. */
    memset(&jit_layout, 0, sizeof(jit_layout));
    result = jit_build_image(&jit_layout);
    if (result != 0) {
        dos_printf("ERROR: JIT image build failed: %d\r\n", result);
        return 1;
    }

    /* Populate hardware values for patching */
    memset(&jit_hw, 0, sizeof(jit_hw));
    jit_hw.io_base     = g_init_ctx.io1_base;
    jit_hw.irq_number  = g_init_ctx.irq1;
    jit_hw.dma_channel = 0xFF;
    jit_hw.nic_type    = (g_init_ctx.num_nics > 0)
                         ? g_init_ctx.nics[0].type : 0;
    jit_hw.cpu_type    = (uint16_t)g_init_ctx.cpu_type;
    jit_hw.flags       = 0;

    if (g_init_ctx.num_nics > 0) {
        memcpy(jit_hw.mac_addr, g_init_ctx.nics[0].mac, 6);
    }
    if (is_module_selected(MOD_DMA_ISA)) {
        jit_hw.dma_channel = 1;
    }
    if (g_init_ctx.chipset.flags & CHIPSET_FLAG_DMA_SAFE) {
        jit_hw.flags |= 0x0001;
    }
    if (g_init_ctx.vds_available) {
        jit_hw.flags |= 0x0002;
    }
    if (g_init_ctx.cpu_type >= CPU_DET_80486) {
        jit_hw.cache_line_size = 32;
    }
    if (g_init_ctx.cpu_features & CPU_FEATURE_CLFLUSH) {
        jit_hw.cache_line_size = 64;
    }

    /* Apply SMC patches */
    result = jit_apply_patches(&jit_layout, &jit_hw);
    if (result < 0) {
        dos_printf("ERROR: SMC patching failed: %d\r\n", result);
        return 1;
    }

    /* Apply inter-module relocations */
    result = jit_apply_relocations(&jit_layout);
    if (result < 0) {
        dos_printf("ERROR: Relocation failed: %d\r\n", result);
        return 1;
    }

    /* Serialize prefetch queue */
    jit_serialize_prefetch(&jit_layout);

    dos_printf("JIT image: %u bytes, %u modules\r\n",
               jit_layout.image_size, jit_layout.entry_count);

    /* ======================================================================
     * Step 3: Allocate DOS memory block for TSR
     * ====================================================================== */
    paras = (jit_layout.image_size + 15) >> 4;
    tsr_seg = dos_alloc_block(paras);
    if (tsr_seg == 0) {
        dos_printf("ERROR: Cannot allocate %u paragraphs of DOS memory\r\n",
                   paras);
        return 1;
    }

    /* ======================================================================
     * Step 4: Copy image to DOS block
     * ====================================================================== */
    _fmemcpy(MK_FP(tsr_seg, 0), jit_layout.image_base,
             jit_layout.image_size);

    /* ======================================================================
     * Step 5: Install interrupt vectors
     * ====================================================================== */
    img_hdr = (jit_image_header_t far *)MK_FP(tsr_seg, 0);

    /* Validate image header */
    if (img_hdr->magic != JIT_IMAGE_MAGIC) {
        dos_printf("ERROR: Bad image magic (expected %08lX, got %08lX)\r\n",
                   JIT_IMAGE_MAGIC, img_hdr->magic);
        dos_free_block(tsr_seg);
        return 1;
    }

    install_vectors(tsr_seg, img_hdr);

    /* ======================================================================
     * Step 6: Report success and exit
     * ====================================================================== */
    dos_printf("\r\n");
    dos_printf("3Com Packet Driver loaded successfully.\r\n");
    dos_printf("  Resident TSR: %u bytes (%u KB) at segment %04Xh\r\n",
               jit_layout.image_size,
               (jit_layout.image_size + 1023) / 1024,
               tsr_seg);
    dos_printf("  INT %02Xh handler installed\r\n",
               img_hdr->int_number);
    if (g_init_ctx.num_nics > 0) {
        dos_printf("  MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   jit_hw.mac_addr[0], jit_hw.mac_addr[1],
                   jit_hw.mac_addr[2], jit_hw.mac_addr[3],
                   jit_hw.mac_addr[4], jit_hw.mac_addr[5]);
    }

    /* Free the JIT build buffer (not the TSR block!) */
    /* jit_layout.image_base was allocated by jit_build_image() */

    /*
     * Return 0 = normal DOS exit via INT 21h/4Ch.
     * DOS frees ALL memory belonging to this process (Stage 1).
     * The TSR lives in the separately allocated DOS block at tsr_seg.
     */
    return 0;
}
