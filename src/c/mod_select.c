/**
 * @file mod_select.c
 * @brief Module selection logic for JIT copy-down TSR builder (OVERLAY)
 *
 * Selects which ASM modules to include in the JIT-built TSR image
 * based on detected CPU, NIC, DMA, and cache capabilities.
 *
 * This file lives in an overlay section and is discarded after init.
 *
 * Last Updated: 2026-02-01 18:20:35 CET
 * Phase 8: Extended with 21 core ASM module entries for two-stage loader
 */

#include "mod_select.h"
#include "modhdr.h"
#include "cpudet.h"
#include "logging.h"
#include <string.h>

/* ============================================================================
 * Module Registry
 * ============================================================================ */

/* External module header symbols (defined in ASM modules) */
extern module_header_t far mod_isr_header;
extern module_header_t far mod_irq_header;
extern module_header_t far mod_pktbuf_header;
extern module_header_t far mod_data_header;
extern module_header_t far mod_3c509b_header;
extern module_header_t far mod_3c515_header;
extern module_header_t far mod_vortex_header;
extern module_header_t far mod_boom_header;
extern module_header_t far mod_cyclone_header;
extern module_header_t far mod_tornado_header;
extern module_header_t far mod_pio_header;
extern module_header_t far mod_dma_isa_header;
extern module_header_t far mod_dma_busmaster_header;
extern module_header_t far mod_dma_descring_header;
extern module_header_t far mod_dma_bounce_header;
extern module_header_t far mod_cache_none_header;
extern module_header_t far mod_cache_wbinvd_header;
extern module_header_t far mod_cache_clflush_header;
extern module_header_t far mod_cache_snoop_header;
extern module_header_t far mod_copy_8086_header;
extern module_header_t far mod_copy_286_header;
extern module_header_t far mod_copy_386_header;
extern module_header_t far mod_copy_pent_header;

/* Core ASM module headers (Phase 8: two-stage loader) */
extern module_header_t far mod_pktapi_header;
extern module_header_t far mod_nicirq_header;
extern module_header_t far mod_hwsmc_header;
extern module_header_t far mod_pcmisr_header;
extern module_header_t far mod_flowrt_header;
extern module_header_t far mod_dirpio_header;
extern module_header_t far mod_pktops_header;
extern module_header_t far mod_pktcopy_header;
extern module_header_t far mod_tsrcom_header;
extern module_header_t far mod_tsrwrap_header;
extern module_header_t far mod_pci_io_header;
extern module_header_t far mod_pciisr_header;
extern module_header_t far mod_linkasm_header;
extern module_header_t far mod_hwpkt_header;
extern module_header_t far mod_hwcfg_header;
extern module_header_t far mod_hwcoord_header;
extern module_header_t far mod_hwinit_header;
extern module_header_t far mod_hweep_header;
extern module_header_t far mod_hwdma_header;
extern module_header_t far mod_cacheops_header;
extern module_header_t far mod_tsr_crt_header;

/* Static registry of all available modules */
static mod_registry_entry_t g_registry[MOD_COUNT] = {
    /* Core modules */
    { MOD_ISR,            "mod_isr",            0,                    0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_IRQ,            "mod_irq",            0,                    0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_PKTBUF,         "mod_pktbuf",         0,                    0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_DATA,           "mod_data",           0,                    0, MOD_NIC_ANY,       NULL, 0 },
    /* NIC modules */
    { MOD_3C509B,         "mod_3c509b_rt",      0,                    0, MOD_NIC_3C509B,    NULL, 0 },
    { MOD_3C515,          "mod_3c515_rt",       MOD_CAP_PCI_BUS,      0, MOD_NIC_3C515,     NULL, 0 },
    { MOD_VORTEX,         "mod_vortex_rt",      MOD_CAP_PCI_BUS,      2, MOD_NIC_VORTEX,    NULL, 0 },
    { MOD_BOOMERANG,      "mod_boom_rt",        MOD_CAP_PCI_BUS|MOD_CAP_BUSMASTER_DMA|MOD_CAP_DESC_RING, 3, MOD_NIC_BOOMERANG, NULL, 0 },
    { MOD_CYCLONE,        "mod_cyclone_rt",     MOD_CAP_PCI_BUS|MOD_CAP_BUSMASTER_DMA|MOD_CAP_DESC_RING, 3, MOD_NIC_CYCLONE,   NULL, 0 },
    { MOD_TORNADO,        "mod_tornado_rt",     MOD_CAP_PCI_BUS|MOD_CAP_BUSMASTER_DMA|MOD_CAP_DESC_RING, 3, MOD_NIC_TORNADO,   NULL, 0 },
    /* DMA modules */
    { MOD_PIO,            "mod_pio",            0,                    0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_DMA_ISA,        "mod_dma_isa",        MOD_CAP_ISA_DMA,      0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_DMA_BUSMASTER,  "mod_dma_busmaster",  MOD_CAP_BUSMASTER_DMA,2, MOD_NIC_ANY,       NULL, 0 },
    { MOD_DMA_DESCRING,   "mod_dma_descring",   MOD_CAP_BUSMASTER_DMA|MOD_CAP_DESC_RING, 3, MOD_NIC_ANY, NULL, 0 },
    { MOD_DMA_BOUNCE,     "mod_dma_bounce",     MOD_CAP_BOUNCE_BUF,   0, MOD_NIC_ANY,       NULL, 0 },
    /* Cache modules */
    { MOD_CACHE_NONE,     "mod_cache_none",     0,                    0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CACHE_WBINVD,   "mod_cache_wbinvd",   MOD_CAP_WBINVD,       3, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CACHE_CLFLUSH,  "mod_cache_clflush",  MOD_CAP_CLFLUSH,      4, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CACHE_SNOOP,    "mod_cache_snoop",    MOD_CAP_SNOOP|MOD_CAP_PCI_BUS, 3, MOD_NIC_ANY, NULL, 0 },
    /* Copy modules */
    { MOD_COPY_8086,      "mod_copy_8086",      0,                    0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_COPY_286,       "mod_copy_286",       0,                    1, MOD_NIC_ANY,       NULL, 0 },
    { MOD_COPY_386,       "mod_copy_386",       0,                    2, MOD_NIC_ANY,       NULL, 0 },
    { MOD_COPY_PENT,      "mod_copy_pent",      0,                    4, MOD_NIC_ANY,       NULL, 0 },
    /* Core ASM modules (Phase 8: always selected for two-stage loader) */
    { MOD_CORE_PKTAPI,    "core_pktapi",        MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_NICIRQ,    "core_nicirq",        MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_HWSMC,     "core_hwsmc",         MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_PCMISR,    "core_pcmisr",        MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_FLOWRT,    "core_flowrt",        MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_DIRPIO,    "core_dirpio",        MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_PKTOPS,    "core_pktops",        MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_PKTCOPY,   "core_pktcopy",       MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_TSRCOM,    "core_tsrcom",        MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_TSRWRAP,   "core_tsrwrap",       MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_PCI_IO,    "core_pci_io",        MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_PCIISR,    "core_pciisr",        MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_LINKASM,   "core_linkasm",       MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_HWPKT,     "core_hwpkt",         MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_HWCFG,     "core_hwcfg",         MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_HWCOORD,   "core_hwcoord",       MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_HWINIT,    "core_hwinit",        MOD_CAP_CORE,         2, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_HWEEP,     "core_hweep",         MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_HWDMA,     "core_hwdma",         MOD_CAP_CORE,         2, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_CACHEOPS,  "core_cacheops",      MOD_CAP_CORE,         2, MOD_NIC_ANY,       NULL, 0 },
    { MOD_CORE_TSR_CRT,   "core_tsr_crt",       MOD_CAP_CORE,         0, MOD_NIC_ANY,       NULL, 0 },
};

/* Current selection state */
static mod_selection_t g_selection;
static int g_registry_initialized = 0;

/* ============================================================================
 * Registry Management
 * ============================================================================ */

int mod_registry_init(void) {
    /* Link registry entries to actual module headers in the EXE */
    g_registry[MOD_ISR].header_ptr           = (void far *)&mod_isr_header;
    g_registry[MOD_IRQ].header_ptr           = (void far *)&mod_irq_header;
    g_registry[MOD_PKTBUF].header_ptr        = (void far *)&mod_pktbuf_header;
    g_registry[MOD_DATA].header_ptr          = (void far *)&mod_data_header;
    g_registry[MOD_3C509B].header_ptr        = (void far *)&mod_3c509b_header;
    g_registry[MOD_3C515].header_ptr         = (void far *)&mod_3c515_header;
    g_registry[MOD_VORTEX].header_ptr        = (void far *)&mod_vortex_header;
    g_registry[MOD_BOOMERANG].header_ptr     = (void far *)&mod_boom_header;
    g_registry[MOD_CYCLONE].header_ptr       = (void far *)&mod_cyclone_header;
    g_registry[MOD_TORNADO].header_ptr       = (void far *)&mod_tornado_header;
    g_registry[MOD_PIO].header_ptr           = (void far *)&mod_pio_header;
    g_registry[MOD_DMA_ISA].header_ptr       = (void far *)&mod_dma_isa_header;
    g_registry[MOD_DMA_BUSMASTER].header_ptr = (void far *)&mod_dma_busmaster_header;
    g_registry[MOD_DMA_DESCRING].header_ptr  = (void far *)&mod_dma_descring_header;
    g_registry[MOD_DMA_BOUNCE].header_ptr    = (void far *)&mod_dma_bounce_header;
    g_registry[MOD_CACHE_NONE].header_ptr    = (void far *)&mod_cache_none_header;
    g_registry[MOD_CACHE_WBINVD].header_ptr  = (void far *)&mod_cache_wbinvd_header;
    g_registry[MOD_CACHE_CLFLUSH].header_ptr = (void far *)&mod_cache_clflush_header;
    g_registry[MOD_CACHE_SNOOP].header_ptr   = (void far *)&mod_cache_snoop_header;
    g_registry[MOD_COPY_8086].header_ptr     = (void far *)&mod_copy_8086_header;
    g_registry[MOD_COPY_286].header_ptr      = (void far *)&mod_copy_286_header;
    g_registry[MOD_COPY_386].header_ptr      = (void far *)&mod_copy_386_header;
    g_registry[MOD_COPY_PENT].header_ptr     = (void far *)&mod_copy_pent_header;

    /* Core ASM module headers */
    g_registry[MOD_CORE_PKTAPI].header_ptr   = (void far *)&mod_pktapi_header;
    g_registry[MOD_CORE_NICIRQ].header_ptr   = (void far *)&mod_nicirq_header;
    g_registry[MOD_CORE_HWSMC].header_ptr    = (void far *)&mod_hwsmc_header;
    g_registry[MOD_CORE_PCMISR].header_ptr   = (void far *)&mod_pcmisr_header;
    g_registry[MOD_CORE_FLOWRT].header_ptr   = (void far *)&mod_flowrt_header;
    g_registry[MOD_CORE_DIRPIO].header_ptr   = (void far *)&mod_dirpio_header;
    g_registry[MOD_CORE_PKTOPS].header_ptr   = (void far *)&mod_pktops_header;
    g_registry[MOD_CORE_PKTCOPY].header_ptr  = (void far *)&mod_pktcopy_header;
    g_registry[MOD_CORE_TSRCOM].header_ptr   = (void far *)&mod_tsrcom_header;
    g_registry[MOD_CORE_TSRWRAP].header_ptr  = (void far *)&mod_tsrwrap_header;
    g_registry[MOD_CORE_PCI_IO].header_ptr   = (void far *)&mod_pci_io_header;
    g_registry[MOD_CORE_PCIISR].header_ptr   = (void far *)&mod_pciisr_header;
    g_registry[MOD_CORE_LINKASM].header_ptr  = (void far *)&mod_linkasm_header;
    g_registry[MOD_CORE_HWPKT].header_ptr    = (void far *)&mod_hwpkt_header;
    g_registry[MOD_CORE_HWCFG].header_ptr    = (void far *)&mod_hwcfg_header;
    g_registry[MOD_CORE_HWCOORD].header_ptr  = (void far *)&mod_hwcoord_header;
    g_registry[MOD_CORE_HWINIT].header_ptr   = (void far *)&mod_hwinit_header;
    g_registry[MOD_CORE_HWEEP].header_ptr    = (void far *)&mod_hweep_header;
    g_registry[MOD_CORE_HWDMA].header_ptr    = (void far *)&mod_hwdma_header;
    g_registry[MOD_CORE_CACHEOPS].header_ptr = (void far *)&mod_cacheops_header;
    g_registry[MOD_CORE_TSR_CRT].header_ptr  = (void far *)&mod_tsr_crt_header;

    /* Read hot_size from each module header */
    {
        int i;
        for (i = 0; i < MOD_COUNT; i++) {
            module_header_t far *hdr = (module_header_t far *)g_registry[i].header_ptr;
            if (hdr) {
                g_registry[i].hot_size = hdr->hot_end - hdr->hot_start;
            }
        }
    }

    memset(&g_selection, 0, sizeof(g_selection));
    g_registry_initialized = 1;
    return 0;
}

/* ============================================================================
 * Selection Functions
 * ============================================================================ */

int select_module(module_id_t id) {
    int i;
    if (id >= MOD_COUNT) return -1;
    if (g_selection.count >= MOD_SELECT_MAX) return -2;

    /* Check for duplicates */
    for (i = 0; i < g_selection.count; i++) {
        if (g_selection.selected[i] == id) return 0; /* already selected */
    }

    g_selection.selected[g_selection.count++] = id;
    g_selection.total_hot_size += g_registry[id].hot_size;
    g_selection.cap_flags_met |= g_registry[id].cap_flags;

    LOG_DEBUG("Selected module: %s (%u bytes hot)",
              g_registry[id].name, g_registry[id].hot_size);
    return 0;
}

int is_module_selected(module_id_t id) {
    int i;
    for (i = 0; i < g_selection.count; i++) {
        if (g_selection.selected[i] == id) return 1;
    }
    return 0;
}

mod_selection_t *get_module_selection(void) {
    return &g_selection;
}

const mod_registry_entry_t *mod_registry_get(module_id_t id) {
    if (id >= MOD_COUNT) return NULL;
    return &g_registry[id];
}

/* ============================================================================
 * Hardware-Based Selection Logic
 * ============================================================================ */

int select_core_modules(void) {
    int i;

    /* Original JIT core modules */
    select_module(MOD_ISR);
    select_module(MOD_IRQ);
    select_module(MOD_PKTBUF);
    select_module(MOD_DATA);

    /* Core ASM modules (always selected for two-stage loader) */
    for (i = MOD_CORE_FIRST; i <= MOD_CORE_LAST; i++) {
        select_module((module_id_t)i);
    }

    return 0;
}

int select_nic_module(const init_context_t *ctx) {
    if (!ctx || ctx->num_nics == 0) return -1;

    switch (ctx->nics[0].type) {
        case MOD_NIC_3C509B:
            return select_module(MOD_3C509B);
        case MOD_NIC_3C515:
            return select_module(MOD_3C515);
        case MOD_NIC_VORTEX:
            return select_module(MOD_VORTEX);
        case MOD_NIC_BOOMERANG:
            return select_module(MOD_BOOMERANG);
        case MOD_NIC_CYCLONE:
            return select_module(MOD_CYCLONE);
        case MOD_NIC_TORNADO:
            return select_module(MOD_TORNADO);
        default:
            LOG_ERROR("Unknown NIC type: %d", ctx->nics[0].type);
            return -1;
    }
}

int select_dma_module(const init_context_t *ctx) {
    if (!ctx) return -1;

    /* PCI NIC generations with descriptor rings */
    if (is_module_selected(MOD_BOOMERANG) ||
        is_module_selected(MOD_CYCLONE) ||
        is_module_selected(MOD_TORNADO)) {
        select_module(MOD_DMA_DESCRING);
        if (ctx->bounce_buffers_needed)
            select_module(MOD_DMA_BOUNCE);
        return 0;
    }

    /* 3C515 with bus-master capability */
    if (is_module_selected(MOD_3C515) &&
        ctx->busmaster_mode &&
        (ctx->chipset.flags & CHIPSET_FLAG_DMA_SAFE)) {
        select_module(MOD_DMA_BUSMASTER);
        if (ctx->bounce_buffers_needed)
            select_module(MOD_DMA_BOUNCE);
        if (ctx->chipset.flags & CHIPSET_FLAG_PCI_PRESENT)
            select_module(MOD_CACHE_SNOOP);
        return 0;
    }

    /* ISA DMA available */
    if (ctx->chipset.flags & CHIPSET_FLAG_ISA_DMA) {
        select_module(MOD_DMA_ISA);
        if (ctx->bounce_buffers_needed)
            select_module(MOD_DMA_BOUNCE);
        return 0;
    }

    /* Fallback: programmed I/O */
    select_module(MOD_PIO);
    return 0;
}

int select_cache_module(const init_context_t *ctx) {
    if (!ctx) return -1;

    if (ctx->cpu_features & CPU_FEATURE_CLFLUSH) {
        return select_module(MOD_CACHE_CLFLUSH);
    } else if (ctx->cpu_type >= CPU_DET_80486) {
        return select_module(MOD_CACHE_WBINVD);
    } else {
        return select_module(MOD_CACHE_NONE);
    }
}

int select_copy_module(const init_context_t *ctx) {
    if (!ctx) return -1;

    if (ctx->cpu_type >= CPU_DET_CPUID_CAPABLE) {
        return select_module(MOD_COPY_PENT);
    } else if (ctx->cpu_type >= CPU_DET_80386) {
        return select_module(MOD_COPY_386);
    } else if (ctx->cpu_type >= CPU_DET_80286) {
        return select_module(MOD_COPY_286);
    } else {
        return select_module(MOD_COPY_8086);
    }
}

int select_all_modules(const init_context_t *ctx) {
    int result;

    if (!ctx) return -1;

    /* Initialize registry if needed */
    if (!g_registry_initialized) {
        result = mod_registry_init();
        if (result != 0) return result;
    }

    /* Always include core modules */
    select_core_modules();

    /* Select based on detected hardware */
    result = select_nic_module(ctx);
    if (result != 0) return result;

    result = select_dma_module(ctx);
    if (result != 0) return result;

    result = select_cache_module(ctx);
    if (result != 0) return result;

    result = select_copy_module(ctx);
    if (result != 0) return result;

    LOG_INFO("Module selection: %d modules, %lu bytes total hot",
             g_selection.count, g_selection.total_hot_size);

    return validate_module_selection(&g_selection, ctx);
}

int validate_module_selection(const mod_selection_t *sel,
                              const init_context_t *ctx) {
    int i;
    int has_nic = 0, has_dma = 0, has_cache = 0, has_copy = 0;

    if (!sel || !ctx) return -1;

    for (i = 0; i < sel->count; i++) {
        module_id_t id = sel->selected[i];

        /* Check CPU requirements */
        if (g_registry[id].cpu_req > ctx->cpu_type) {
            LOG_ERROR("Module %s requires CPU >= %d, have %d",
                      g_registry[id].name, g_registry[id].cpu_req,
                      ctx->cpu_type);
            return -1;
        }

        /* Track categories */
        if (id >= MOD_3C509B && id <= MOD_TORNADO) has_nic = 1;
        if (id >= MOD_PIO && id <= MOD_DMA_BOUNCE) has_dma = 1;
        if (id >= MOD_CACHE_NONE && id <= MOD_CACHE_SNOOP) has_cache = 1;
        if (id >= MOD_COPY_8086 && id <= MOD_COPY_PENT) has_copy = 1;
    }

    if (!has_nic) { LOG_ERROR("No NIC module selected"); return -1; }
    if (!has_dma) { LOG_ERROR("No DMA module selected"); return -1; }
    if (!has_cache) { LOG_ERROR("No cache module selected"); return -1; }
    if (!has_copy) { LOG_ERROR("No copy module selected"); return -1; }

    return 0;
}
