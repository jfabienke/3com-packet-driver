/**
 * @file init_main.c
 * @brief Main initialization orchestrator (ROOT segment)
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * Calls each stage function in order. The overlay manager automatically
 * loads the appropriate overlay section when each stage_* function is called.
 *
 * This file lives in the ROOT segment and is never overlaid.
 *
 * Last Updated: 2026-02-01 12:05:57 CET
 * Phase 7: Added JIT copy-down build stage after hardware detection
 */

#include "dos_io.h"
#include <string.h>
#include <dos.h>
#include "common.h"
#include "init_context.h"
#include "logging.h"
#include "mod_select.h"
#include "jit_build.h"

/* ============================================================================
 * Global Init Context (ROOT Segment)
 * ============================================================================ */

/**
 * Global init context - resides in ROOT segment, persists across overlays.
 * This is the communication channel between all initialization stages.
 */
init_context_t g_init_ctx;

/* ============================================================================
 * Private State
 * ============================================================================ */

/* Track whether init has been run */
static int g_init_started = 0;
static int g_init_completed = 0;

/* PSP segment (set during entry) */
static uint16_t g_psp_segment = 0;

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Initialize the init context structure
 * @param ctx Pointer to init context to initialize
 */
void init_context_init(init_context_t *ctx) {
    if (!ctx) return;

    /* Zero the entire structure */
    _fmemset(ctx, 0, sizeof(init_context_t));

    /* Set magic and version */
    ctx->magic = INIT_CONTEXT_MAGIC;
    ctx->version = INIT_CONTEXT_VERSION;
    ctx->size = sizeof(init_context_t);
}

/**
 * @brief Validate init context structure
 * @param ctx Pointer to init context to validate
 * @return 1 if valid, 0 if invalid
 */
int init_context_validate(const init_context_t *ctx) {
    if (!ctx) return 0;
    if (ctx->magic != INIT_CONTEXT_MAGIC) return 0;
    if (ctx->version != INIT_CONTEXT_VERSION) return 0;
    if (ctx->size != sizeof(init_context_t)) return 0;
    return 1;
}

/**
 * @brief Set error state in init context
 * @param ctx Pointer to init context
 * @param stage Stage number where error occurred
 * @param error_code Error code
 * @param msg Error message (may be NULL)
 */
void init_context_set_error(init_context_t *ctx, uint16_t stage,
                           int16_t error_code, const char *msg) {
    if (!ctx) return;

    ctx->error_code = error_code;
    ctx->error_stage = stage;

    if (msg) {
        strncpy(ctx->error_msg, msg, sizeof(ctx->error_msg) - 1);
        ctx->error_msg[sizeof(ctx->error_msg) - 1] = '\0';
    } else {
        ctx->error_msg[0] = '\0';
    }
}

/**
 * @brief Check if a specific stage is complete
 * @param ctx Pointer to init context
 * @param stage_mask Stage bitmask to check
 * @return 1 if stage is complete, 0 otherwise
 */
int init_context_stage_complete(const init_context_t *ctx, uint16_t stage_mask) {
    if (!ctx) return 0;
    return (ctx->stages_complete & stage_mask) == stage_mask;
}

/**
 * @brief Get string description of current state
 * @param ctx Pointer to init context
 * @return Human-readable status string
 */
const char *init_context_status_string(const init_context_t *ctx) {
    static char status_buf[64];

    if (!ctx) {
        return "Invalid context";
    }

    if (ctx->error_code != 0) {
        snprintf(status_buf, sizeof(status_buf),
                "Error at stage %d: code %d", ctx->error_stage, ctx->error_code);
        return status_buf;
    }

    if (ctx->fully_initialized) {
        return "Fully initialized";
    }

    /* Count completed stages */
    {
        int count = 0;
        uint16_t mask = ctx->stages_complete;
        while (mask) {
            count += (mask & 1);
            mask >>= 1;
        }
        snprintf(status_buf, sizeof(status_buf),
                "In progress: %d/15 stages complete", count);
    }

    return status_buf;
}

/* ============================================================================
 * Main Initialization Orchestrator
 * ============================================================================ */

/**
 * @brief Run all initialization stages
 * @param argc Argument count from command line
 * @param argv Argument vector from command line
 * @return 0 on success, negative error code on failure
 *
 * This function orchestrates the entire initialization sequence by calling
 * each stage function in order. The overlay manager automatically loads
 * the appropriate overlay section when each stage_* function is called.
 *
 * Stage Groups:
 * - INIT_EARLY (Stages 0-4): Entry validation, CPU, platform, logging, config
 * - INIT_DETECT (Stages 5-9): Chipset, VDS/DMA, memory, packet ops, hardware
 * - INIT_FINAL (Stages 10-14): DMA buffers, TSR, API, IRQ, activation
 */
int run_init_stages(int argc, char far * far *argv) {
    int result;

    /* Prevent double initialization */
    if (g_init_started) {
        LOG_ERROR("Initialization already in progress");
        return ERROR_BUSY;
    }
    g_init_started = 1;

    /* Initialize context structure */
    init_context_init(&g_init_ctx);

    LOG_INFO("Starting 3Com Packet Driver initialization");
    LOG_INFO("Overlay-based multi-stage loader active");

    /* ========== INIT_EARLY Overlay (Stages 0-4) ========== */
    /* This overlay section loads automatically when we call the first function */

    /* Stage 0: Entry Validation */
    LOG_DEBUG("Stage 0: Entry validation");
    result = stage_entry_validation(&g_init_ctx);
    if (result != 0) {
        init_context_set_error(&g_init_ctx, 0, result, "Entry validation failed");
        LOG_ERROR("Stage 0 failed: %d", result);
        return result;
    }
    g_init_ctx.stages_complete |= STAGE_0_ENTRY_VALIDATION;

    /* Stage 1: CPU Detection */
    LOG_DEBUG("Stage 1: CPU detection");
    result = stage_cpu_detect(&g_init_ctx);
    if (result != 0) {
        init_context_set_error(&g_init_ctx, 1, result, "CPU detection failed");
        LOG_ERROR("Stage 1 failed: %d", result);
        return result;
    }
    g_init_ctx.stages_complete |= STAGE_1_CPU_DETECT;

    /* Stage 2: Platform Probe */
    LOG_DEBUG("Stage 2: Platform probe");
    result = stage_platform_probe(&g_init_ctx);
    if (result != 0) {
        init_context_set_error(&g_init_ctx, 2, result, "Platform probe failed");
        LOG_ERROR("Stage 2 failed: %d", result);
        return result;
    }
    g_init_ctx.stages_complete |= STAGE_2_PLATFORM_PROBE;

    /* Stage 3: Logging Initialization */
    LOG_DEBUG("Stage 3: Logging init");
    result = stage_logging_init(&g_init_ctx);
    if (result != 0) {
        init_context_set_error(&g_init_ctx, 3, result, "Logging init failed");
        LOG_ERROR("Stage 3 failed: %d", result);
        return result;
    }
    g_init_ctx.stages_complete |= STAGE_3_LOGGING_INIT;

    /* Stage 4: Configuration Parsing */
    LOG_DEBUG("Stage 4: Config parse");
    result = stage_config_parse(&g_init_ctx, argc, argv);
    if (result != 0) {
        init_context_set_error(&g_init_ctx, 4, result, "Config parse failed");
        LOG_ERROR("Stage 4 failed: %d", result);
        return result;
    }
    g_init_ctx.stages_complete |= STAGE_4_CONFIG_PARSE;

    LOG_INFO("INIT_EARLY complete (stages 0-4)");

    /* ========== INIT_DETECT Overlay (Stages 5-9) ========== */
    /* Overlay manager automatically swaps to this section */

    /* Stage 5: Chipset Detection */
    LOG_DEBUG("Stage 5: Chipset detect");
    result = stage_chipset_detect(&g_init_ctx);
    if (result != 0) {
        init_context_set_error(&g_init_ctx, 5, result, "Chipset detection failed");
        LOG_ERROR("Stage 5 failed: %d", result);
        return result;
    }
    g_init_ctx.stages_complete |= STAGE_5_CHIPSET_DETECT;

    /* Stage 6: VDS/DMA Policy Refinement */
    LOG_DEBUG("Stage 6: VDS/DMA refine");
    result = stage_vds_dma_refine(&g_init_ctx);
    if (result != 0) {
        init_context_set_error(&g_init_ctx, 6, result, "VDS/DMA refine failed");
        LOG_ERROR("Stage 6 failed: %d", result);
        return result;
    }
    g_init_ctx.stages_complete |= STAGE_6_VDS_DMA_REFINE;

    /* Stage 7: Memory Initialization */
    LOG_DEBUG("Stage 7: Memory init");
    result = stage_memory_init(&g_init_ctx);
    if (result != 0) {
        init_context_set_error(&g_init_ctx, 7, result, "Memory init failed");
        LOG_ERROR("Stage 7 failed: %d", result);
        return result;
    }
    g_init_ctx.stages_complete |= STAGE_7_MEMORY_INIT;

    /* Stage 8: Packet Operations Initialization */
    LOG_DEBUG("Stage 8: Packet ops init");
    result = stage_packet_ops_init(&g_init_ctx);
    if (result != 0) {
        init_context_set_error(&g_init_ctx, 8, result, "Packet ops init failed");
        LOG_ERROR("Stage 8 failed: %d", result);
        return result;
    }
    g_init_ctx.stages_complete |= STAGE_8_PACKET_OPS_INIT;

    /* Stage 9: Hardware Detection */
    LOG_DEBUG("Stage 9: Hardware detect");
    result = stage_hardware_detect(&g_init_ctx);
    if (result != 0) {
        init_context_set_error(&g_init_ctx, 9, result, "Hardware detection failed");
        LOG_ERROR("Stage 9 failed: %d", result);
        return result;
    }
    g_init_ctx.stages_complete |= STAGE_9_HARDWARE_DETECT;

    LOG_INFO("INIT_DETECT complete (stages 5-9)");

    /* ========== JIT Copy-Down Build ========== */
    /* After hardware detection, select modules and build JIT TSR image.
     * The JIT engine: (1) selects modules, (2) copies hot sections
     * contiguously, (3) applies SMC patches, (4) resolves relocations,
     * (5) serializes the prefetch queue. The resulting image is the
     * pure-ASM TSR that stage_tsr_relocate will install. */
    {
        int jit_result;
        jit_layout_t jit_layout;
        jit_hw_values_t jit_hw;

        /* Step 1: Select modules based on detected hardware */
        LOG_DEBUG("JIT: Selecting modules based on detected hardware");
        jit_result = select_all_modules(&g_init_ctx);
        if (jit_result != 0) {
            init_context_set_error(&g_init_ctx, 9, jit_result,
                                   "JIT module selection failed");
            LOG_ERROR("JIT module selection failed: %d", jit_result);
            return jit_result;
        }
        LOG_INFO("JIT: Selected %d modules, %lu bytes hot",
                 get_module_selection()->count,
                 get_module_selection()->total_hot_size);

        /* Step 2: Build TSR image (copy-down hot sections) */
        LOG_DEBUG("JIT: Building TSR image");
        jit_result = jit_build_image(&jit_layout);
        if (jit_result != 0) {
            init_context_set_error(&g_init_ctx, 9, jit_result,
                                   "JIT image build failed");
            LOG_ERROR("JIT image build failed: %d", jit_result);
            return jit_result;
        }
        LOG_INFO("JIT: Built %u byte image from %u modules",
                 jit_layout.image_size, jit_layout.entry_count);

        /* Step 3: Populate hardware values from init context */
        memset(&jit_hw, 0, sizeof(jit_hw));
        jit_hw.io_base    = g_init_ctx.io1_base;
        jit_hw.irq_number = g_init_ctx.irq1;
        jit_hw.dma_channel = 0xFF;  /* Default: no DMA channel */
        jit_hw.nic_type   = (g_init_ctx.num_nics > 0)
                            ? g_init_ctx.nics[0].type : 0;
        jit_hw.cpu_type   = (uint16_t)g_init_ctx.cpu_type;
        jit_hw.flags      = 0;

        /* Copy MAC address from first detected NIC */
        if (g_init_ctx.num_nics > 0) {
            memcpy(jit_hw.mac_addr, g_init_ctx.nics[0].mac, 6);
        }

        /* Set DMA channel if ISA DMA is available and selected */
        if (is_module_selected(MOD_DMA_ISA)) {
            jit_hw.dma_channel = 1;  /* Standard ISA DMA channel */
        }

        /* Set runtime flags from chipset/DMA detection */
        if (g_init_ctx.chipset.flags & CHIPSET_FLAG_DMA_SAFE) {
            jit_hw.flags |= 0x0001;  /* Bus-master capable */
        }
        if (g_init_ctx.vds_available) {
            jit_hw.flags |= 0x0002;  /* VDS available */
        }
        if (g_init_ctx.chipset.flags & CHIPSET_FLAG_CACHE_WB) {
            jit_hw.flags |= 0x0004;  /* Write-back cache */
        }

        /* Cache line size: 32 for 486+, 0 for older */
        if (g_init_ctx.cpu_type >= CPU_DET_80486) {
            jit_hw.cache_line_size = 32;
        }
        if (g_init_ctx.cpu_features & CPU_FEATURE_CLFLUSH) {
            jit_hw.cache_line_size = 64;  /* Pentium+ typically 64 */
        }

        /* Step 4: Apply SMC patches (bake hardware values) */
        LOG_DEBUG("JIT: Applying SMC patches (IO=0x%04X IRQ=%u CPU=%u)",
                  jit_hw.io_base, jit_hw.irq_number, jit_hw.cpu_type);
        jit_result = jit_apply_patches(&jit_layout, &jit_hw);
        if (jit_result < 0) {
            init_context_set_error(&g_init_ctx, 9, jit_result,
                                   "JIT SMC patching failed");
            LOG_ERROR("JIT SMC patching failed: %d", jit_result);
            return jit_result;
        }
        LOG_INFO("JIT: Applied %d SMC patches", jit_result);

        /* Step 5: Apply inter-module relocations */
        LOG_DEBUG("JIT: Applying relocations");
        jit_result = jit_apply_relocations(&jit_layout);
        if (jit_result < 0) {
            init_context_set_error(&g_init_ctx, 9, jit_result,
                                   "JIT relocation failed");
            LOG_ERROR("JIT relocation failed: %d", jit_result);
            return jit_result;
        }
        LOG_INFO("JIT: Applied %d relocations", jit_result);

        /* Step 6: Serialize prefetch queue */
        jit_serialize_prefetch(&jit_layout);
        LOG_DEBUG("JIT: Prefetch serialized");

        /* Store image info in init context for stage_tsr_relocate */
        g_init_ctx.resident_end = (void far *)
            (jit_layout.image_base + jit_layout.image_size);
        g_init_ctx.resident_paragraphs =
            ((uint32_t)jit_layout.image_size + 15) >> 4;

        LOG_INFO("JIT: TSR image ready at %Fp, %u bytes (%lu paragraphs)",
                 jit_layout.image_base, jit_layout.image_size,
                 g_init_ctx.resident_paragraphs);
    }

    /* ========== INIT_FINAL Overlay (Stages 10-14) ========== */
    /* Overlay manager automatically swaps to this section */

    /* Stage 10: DMA Buffer Initialization */
    LOG_DEBUG("Stage 10: DMA buffer init");
    result = stage_dma_buffer_init(&g_init_ctx);
    if (result != 0) {
        init_context_set_error(&g_init_ctx, 10, result, "DMA buffer init failed");
        LOG_ERROR("Stage 10 failed: %d", result);
        return result;
    }
    g_init_ctx.stages_complete |= STAGE_10_DMA_BUFFER_INIT;

    /* Stage 11: TSR Relocation */
    LOG_DEBUG("Stage 11: TSR relocate");
    result = stage_tsr_relocate(&g_init_ctx);
    if (result != 0) {
        init_context_set_error(&g_init_ctx, 11, result, "TSR relocation failed");
        LOG_ERROR("Stage 11 failed: %d", result);
        return result;
    }
    g_init_ctx.stages_complete |= STAGE_11_TSR_RELOCATE;

    /* Stage 12: API Installation */
    LOG_DEBUG("Stage 12: API install");
    result = stage_api_install(&g_init_ctx);
    if (result != 0) {
        init_context_set_error(&g_init_ctx, 12, result, "API install failed");
        LOG_ERROR("Stage 12 failed: %d", result);
        return result;
    }
    g_init_ctx.stages_complete |= STAGE_12_API_INSTALL;

    /* Stage 13: IRQ Enable */
    LOG_DEBUG("Stage 13: IRQ enable");
    result = stage_irq_enable(&g_init_ctx);
    if (result != 0) {
        init_context_set_error(&g_init_ctx, 13, result, "IRQ enable failed");
        LOG_ERROR("Stage 13 failed: %d", result);
        return result;
    }
    g_init_ctx.stages_complete |= STAGE_13_IRQ_ENABLE;

    /* Stage 14: API Activation */
    LOG_DEBUG("Stage 14: API activate");
    result = stage_api_activate(&g_init_ctx);
    if (result != 0) {
        init_context_set_error(&g_init_ctx, 14, result, "API activation failed");
        LOG_ERROR("Stage 14 failed: %d", result);
        return result;
    }
    g_init_ctx.stages_complete |= STAGE_14_API_ACTIVATE;

    LOG_INFO("INIT_FINAL complete (stages 10-14)");

    /* Mark initialization as complete */
    g_init_ctx.fully_initialized = 1;
    g_init_completed = 1;

    LOG_INFO("All initialization stages completed successfully");
    LOG_INFO("Resident code: %lu paragraphs (%lu KB)",
             g_init_ctx.resident_paragraphs,
             (g_init_ctx.resident_paragraphs * 16) / 1024);

    return 0;
}

/**
 * @brief Free overlay area after init completes
 *
 * Shrinks program memory to ROOT segment only via INT 21h/4Ah.
 * This releases the overlay area back to DOS, reducing TSR footprint.
 *
 * MUST be called AFTER all initialization is complete and BEFORE
 * returning to DOS as a TSR.
 */
void free_overlay_area(void) {
    union REGS regs;
    struct SREGS sregs;

    /* Only proceed if initialization completed successfully */
    if (!g_init_completed) {
        LOG_WARNING("free_overlay_area: init not completed, skipping");
        return;
    }

    /* Get current PSP if not already known */
    if (g_psp_segment == 0) {
        regs.h.ah = 0x62;  /* Get PSP */
        intdos(&regs, &regs);
        g_psp_segment = regs.x.bx;
    }

    LOG_DEBUG("Freeing overlay area: PSP=0x%04X, resident=%lu paras",
              g_psp_segment, g_init_ctx.resident_paragraphs);

    /* Resize memory block to resident_paragraphs */
    /* INT 21h/4Ah: Resize Memory Block */
    /* ES = segment of block, BX = new size in paragraphs */
    regs.h.ah = 0x4A;
    regs.x.bx = (unsigned int)g_init_ctx.resident_paragraphs;
    sregs.es = g_psp_segment;
    intdosx(&regs, &regs, &sregs);

    if (regs.x.cflag) {
        LOG_ERROR("Memory resize failed: error %d, max available %d paras",
                  regs.x.ax, regs.x.bx);
    } else {
        LOG_INFO("Overlay area freed - TSR resident size: %lu KB",
                 (g_init_ctx.resident_paragraphs * 16) / 1024);
    }
}

/**
 * @brief Set the PSP segment for memory operations
 * @param psp_segment PSP segment value
 *
 * Called from main entry point to provide PSP segment.
 */
void set_psp_segment(uint16_t psp_segment) {
    g_psp_segment = psp_segment;
}

/**
 * @brief Get initialization status
 * @return 1 if fully initialized, 0 otherwise
 */
int is_init_complete(void) {
    return g_init_completed;
}

/**
 * @brief Get pointer to init context
 * @return Pointer to global init context
 */
init_context_t *get_init_context(void) {
    return &g_init_ctx;
}

/* ============================================================================
 * Stage Stub Implementations
 * ============================================================================
 * These are weak stub implementations that will be overridden by the actual
 * stage implementations in the overlay sections. They exist to allow the
 * code to compile and link even if some stages are not yet implemented.
 */

#pragma aux stage_entry_validation "*" parm caller []
#pragma aux stage_cpu_detect "*" parm caller []
#pragma aux stage_platform_probe "*" parm caller []
#pragma aux stage_logging_init "*" parm caller []
#pragma aux stage_config_parse "*" parm caller []
#pragma aux stage_chipset_detect "*" parm caller []
#pragma aux stage_vds_dma_refine "*" parm caller []
#pragma aux stage_memory_init "*" parm caller []
#pragma aux stage_packet_ops_init "*" parm caller []
#pragma aux stage_hardware_detect "*" parm caller []
#pragma aux stage_dma_buffer_init "*" parm caller []
#pragma aux stage_tsr_relocate "*" parm caller []
#pragma aux stage_api_install "*" parm caller []
#pragma aux stage_irq_enable "*" parm caller []
#pragma aux stage_api_activate "*" parm caller []
