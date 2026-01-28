/**
 * @file init_context.h
 * @brief Shared context structure passed between init stages
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This structure lives in the ROOT segment and persists across
 * overlay swaps. Each stage reads its inputs and writes its outputs here.
 *
 * Last Updated: 2026-01-26 14:30:00 UTC
 */

#ifndef _INIT_CONTEXT_H_
#define _INIT_CONTEXT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "cpudet.h"
#include "config.h"
#include "hardware.h"
#include "platform_probe.h"

/* ============================================================================
 * Stage Identifiers
 * ============================================================================ */

/* Stage bitmasks for stages_complete field */
#define STAGE_0_ENTRY_VALIDATION    (1 << 0)
#define STAGE_1_CPU_DETECT          (1 << 1)
#define STAGE_2_PLATFORM_PROBE      (1 << 2)
#define STAGE_3_LOGGING_INIT        (1 << 3)
#define STAGE_4_CONFIG_PARSE        (1 << 4)
#define STAGE_5_CHIPSET_DETECT      (1 << 5)
#define STAGE_6_VDS_DMA_REFINE      (1 << 6)
#define STAGE_7_MEMORY_INIT         (1 << 7)
#define STAGE_8_PACKET_OPS_INIT     (1 << 8)
#define STAGE_9_HARDWARE_DETECT     (1 << 9)
#define STAGE_10_DMA_BUFFER_INIT    (1 << 10)
#define STAGE_11_TSR_RELOCATE       (1 << 11)
#define STAGE_12_API_INSTALL        (1 << 12)
#define STAGE_13_IRQ_ENABLE         (1 << 13)
#define STAGE_14_API_ACTIVATE       (1 << 14)

#define STAGES_ALL_COMPLETE         0x7FFF  /* All 15 stages (0-14) */

/* Maximum number of NICs to track */
#ifndef INIT_MAX_NICS
#define INIT_MAX_NICS               4
#endif

/* ============================================================================
 * Chipset Detection Result
 * ============================================================================ */

/**
 * @brief Chipset detection result structure
 *
 * Contains information about the detected chipset that affects
 * DMA policy and hardware compatibility decisions.
 */
typedef struct {
    uint16_t vendor_id;             /* Chipset vendor ID */
    uint16_t device_id;             /* Chipset device ID */
    uint8_t  revision;              /* Chipset revision */
    uint8_t  chipset_type;          /* Chipset category/type */
    char     name[32];              /* Human-readable chipset name */
    uint16_t flags;                 /* Chipset capability flags */
    uint8_t  isa_bridge_type;       /* ISA bridge type */
    uint8_t  pci_revision;          /* PCI bus revision */
    uint8_t  has_usb;               /* USB controller present */
    uint8_t  has_ide;               /* IDE controller present */
    uint8_t  reserved[2];           /* Padding for alignment */
} chipset_detection_result_t;

/* Chipset type flags */
#define CHIPSET_FLAG_DMA_SAFE       0x0001  /* Safe for bus-master DMA */
#define CHIPSET_FLAG_ISA_DMA        0x0002  /* Has ISA DMA support */
#define CHIPSET_FLAG_PCI_PRESENT    0x0004  /* PCI bus present */
#define CHIPSET_FLAG_VLB_PRESENT    0x0008  /* VL-Bus present */
#define CHIPSET_FLAG_EISA_PRESENT   0x0010  /* EISA bus present */
#define CHIPSET_FLAG_MCA_PRESENT    0x0020  /* MCA bus present */
#define CHIPSET_FLAG_CACHE_WB       0x0040  /* Write-back cache */
#define CHIPSET_FLAG_BROKEN_ISA     0x0080  /* Known ISA timing issues */

/* ============================================================================
 * Init Context Structure
 * ============================================================================ */

/**
 * @brief Main initialization context structure (~2.5 KB)
 *
 * This structure is allocated in the ROOT segment and persists
 * throughout all overlay stage transitions. Each stage:
 * 1. Reads its required inputs from this structure
 * 2. Performs its initialization work
 * 3. Writes its outputs back to this structure
 * 4. Sets the appropriate bit in stages_complete
 *
 * After all stages complete, this structure contains all information
 * needed for TSR runtime operation.
 */
typedef struct {
    /* ========== Structure Header ========== */
    uint16_t magic;                 /* Magic number for validation (0x3C3C) */
    uint16_t version;               /* Structure version (1) */
    uint16_t size;                  /* Size of this structure */
    uint16_t reserved_header;       /* Reserved for future use */

    /* ========== Stage 1 Output: CPU Detection (~100 bytes via pointer) ========== */
    /* Note: Full cpu_info_t is large, so we store essential fields only */
    cpu_type_t cpu_type;            /* Detected CPU type */
    cpu_vendor_t cpu_vendor;        /* CPU vendor */
    uint32_t cpu_features;          /* CPU feature flags */
    uint8_t cpu_family;             /* CPU family (from CPUID) */
    uint8_t cpu_model;              /* CPU model (from CPUID) */
    uint8_t cpu_stepping;           /* CPU stepping (from CPUID) */
    uint8_t addr_bits;              /* Address bits (20/24/32) */
    uint16_t cpu_mhz;               /* CPU speed in MHz */
    uint8_t has_cpuid;              /* CPUID instruction available */
    uint8_t has_32bit;              /* 32-bit operations available */
    uint8_t in_v86_mode;            /* Running in V86 mode */
    uint8_t in_ring0;               /* Running at ring 0 */
    uint8_t opt_level;              /* Optimization level (CPU_OPT_*) */
    uint8_t reserved_cpu[3];        /* Padding */

    /* ========== Stage 2 Output: Platform Probe (~32 bytes) ========== */
    platform_probe_result_t platform; /* Full platform probe result */

    /* ========== Stage 4 Output: Configuration (~256 bytes via pointer) ========== */
    /* We store a pointer to the full config_t since it's large */
    config_t far *config_ptr;       /* Pointer to full config structure */

    /* Essential config values copied locally for fast access */
    uint16_t io1_base;              /* First NIC I/O base */
    uint16_t io2_base;              /* Second NIC I/O base */
    uint8_t irq1;                   /* First NIC IRQ */
    uint8_t irq2;                   /* Second NIC IRQ */
    uint8_t busmaster_mode;         /* Bus mastering mode */
    uint8_t pci_mode;               /* PCI support mode */
    uint8_t debug_level;            /* Debug verbosity level */
    uint8_t use_xms;                /* Use XMS if available */

    /* ========== Stage 5 Output: Chipset Detection (~64 bytes) ========== */
    chipset_detection_result_t chipset; /* Chipset detection result */
    bus_type_t bus_type;            /* Primary bus type detected */
    uint8_t reserved_chipset[2];    /* Padding */

    /* ========== Stage 6 Output: DMA Policy (4 bytes) ========== */
    dma_policy_t final_dma_policy;  /* Final DMA policy decision */
    uint8_t vds_available;          /* VDS services available */
    uint8_t vds_required;           /* VDS required for DMA */
    uint8_t bounce_buffers_needed;  /* Bounce buffers required */
    uint8_t reserved_dma;           /* Padding */

    /* ========== Stage 9 Output: Detected NICs (~2 KB) ========== */
    /* Note: nic_info_t is large, store essential info only */
    struct {
        uint8_t type;               /* NIC type (NIC_TYPE_*) */
        uint8_t status;             /* NIC status flags */
        uint16_t io_base;           /* I/O base address */
        uint8_t irq;                /* IRQ number */
        uint8_t mac[ETH_ALEN];      /* MAC address */
        uint16_t capabilities;      /* Hardware capabilities */
        uint8_t index;              /* NIC index in hardware array */
        uint8_t reserved[3];        /* Padding */
    } nics[INIT_MAX_NICS];          /* Array of detected NICs (~24 bytes each) */
    uint8_t num_nics;               /* Number of detected NICs */
    uint8_t active_nics;            /* Number of active (initialized) NICs */
    uint16_t reserved_nics;         /* Padding */

    /* ========== Stage 11 Output: Memory Layout (8 bytes) ========== */
    uint32_t resident_paragraphs;   /* Size of resident code in paragraphs */
    void far *resident_end;         /* Far pointer to end of resident section */

    /* ========== XMS State ========== */
    uint8_t xms_available;          /* XMS memory available */
    uint8_t xms_version_major;      /* XMS version major */
    uint8_t xms_version_minor;      /* XMS version minor */
    uint8_t reserved_xms;           /* Padding */
    uint32_t xms_free_kb;           /* Free XMS in KB */

    /* ========== Completion State ========== */
    uint16_t stages_complete;       /* Bitmask of completed stages */
    int16_t error_code;             /* Error code if init failed (0 = success) */
    uint16_t error_stage;           /* Stage where error occurred */
    char error_msg[64];             /* Error message string */

    /* ========== Runtime Flags ========== */
    uint8_t fully_initialized;      /* All stages completed successfully */
    uint8_t tsr_installed;          /* TSR is installed */
    uint8_t api_active;             /* Packet driver API is active */
    uint8_t irqs_enabled;           /* Interrupts are enabled */

    /* ========== Reserved for Future Use ========== */
    uint8_t reserved_future[32];    /* Reserved for expansion */

} init_context_t;  /* Total: ~2.5 KB */

/* Magic number for init context validation */
#define INIT_CONTEXT_MAGIC          0x3C3C
#define INIT_CONTEXT_VERSION        1

/* ============================================================================
 * Global Instance
 * ============================================================================ */

/**
 * @brief Global init context instance (in ROOT segment)
 *
 * This global variable is allocated in the ROOT (resident) segment
 * and persists throughout all overlay stage transitions.
 */
extern init_context_t g_init_ctx;

/* ============================================================================
 * Stage Entry Points
 * ============================================================================ */

/**
 * Stage entry points - each function lives in its overlay section.
 * The overlay manager automatically loads the appropriate section
 * when these functions are called.
 *
 * All stage functions:
 * - Take a far pointer to the init context
 * - Return 0 on success, negative error code on failure
 * - Use the 'far' keyword for proper overlay segment handling
 */

/* INIT_EARLY overlay (Stages 0-4) */
int far stage_entry_validation(init_context_t far *ctx);
int far stage_cpu_detect(init_context_t far *ctx);
int far stage_platform_probe(init_context_t far *ctx);
int far stage_logging_init(init_context_t far *ctx);
int far stage_config_parse(init_context_t far *ctx, int argc, char far * far *argv);

/* INIT_DETECT overlay (Stages 5-9) */
int far stage_chipset_detect(init_context_t far *ctx);
int far stage_vds_dma_refine(init_context_t far *ctx);
int far stage_memory_init(init_context_t far *ctx);
int far stage_packet_ops_init(init_context_t far *ctx);
int far stage_hardware_detect(init_context_t far *ctx);

/* INIT_FINAL overlay (Stages 10-14) */
int far stage_dma_buffer_init(init_context_t far *ctx);
int far stage_tsr_relocate(init_context_t far *ctx);
int far stage_api_install(init_context_t far *ctx);
int far stage_irq_enable(init_context_t far *ctx);
int far stage_api_activate(init_context_t far *ctx);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Initialize the init context structure
 * @param ctx Pointer to init context to initialize
 */
void init_context_init(init_context_t *ctx);

/**
 * @brief Validate init context structure
 * @param ctx Pointer to init context to validate
 * @return 1 if valid, 0 if invalid
 */
int init_context_validate(const init_context_t *ctx);

/**
 * @brief Set error state in init context
 * @param ctx Pointer to init context
 * @param stage Stage number where error occurred
 * @param error_code Error code
 * @param msg Error message (may be NULL)
 */
void init_context_set_error(init_context_t *ctx, uint16_t stage,
                           int16_t error_code, const char *msg);

/**
 * @brief Check if a specific stage is complete
 * @param ctx Pointer to init context
 * @param stage_mask Stage bitmask to check
 * @return 1 if stage is complete, 0 otherwise
 */
int init_context_stage_complete(const init_context_t *ctx, uint16_t stage_mask);

/**
 * @brief Get string description of current state
 * @param ctx Pointer to init context
 * @return Human-readable status string
 */
const char *init_context_status_string(const init_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _INIT_CONTEXT_H_ */
