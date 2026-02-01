/**
 * @file extension_api.h
 * @brief Vendor Extension API (AH=80h-9Fh) definitions
 *
 * Provides introspection and diagnostics without impacting ISR performance.
 * All handlers are constant-time reads of precomputed snapshots.
 */

#ifndef _EXTENSION_API_H_
#define _EXTENSION_API_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* API function codes */
#define EXT_VENDOR_DISCOVERY    0x80   /* Get vendor info and capabilities */
#define EXT_SAFETY_STATE       0x81   /* Get safety flags and kill switches */
#define EXT_PATCH_STATS        0x82   /* Get SMC patch statistics */
#define EXT_MEMORY_MAP         0x83   /* Get resident memory layout */
#define EXT_VERSION_INFO       0x84   /* Get version and build flags */
#define EXT_PERF_COUNTERS      0x85   /* Get performance counters (Stage 2) */
#define EXT_MAX_FUNCTION       0x84   /* Maximum implemented function */

/* Error codes (returned in AX with CF=1) */
#define EXT_ERR_SUCCESS        0x0000  /* Success */
#define EXT_ERR_BAD_FUNCTION   0xFFFF  /* Invalid function code */
#define EXT_ERR_NO_BUFFER      0xFFFE  /* Buffer required but not provided */
#define EXT_ERR_BUFFER_SMALL   0xFFFD  /* Buffer too small */
#define EXT_ERR_NOT_READY      0xFFFC  /* Feature not yet available */

/* Capability flags (AH=80h, returned in DX) */
#define EXT_CAP_BASIC          0x0001  /* Basic introspection */
#define EXT_CAP_SAFETY         0x0002  /* Safety state queries */
#define EXT_CAP_PATCHES        0x0004  /* SMC patch info */
#define EXT_CAP_MEMORY         0x0008  /* Memory layout info */
#define EXT_CAP_RUNTIME_CONFIG 0x0010  /* Runtime configuration (AH=94h-96h) */
#define EXT_CAP_CURRENT        0x001F  /* Current capabilities */

/* Safety state flags (AH=81h, returned in AX) */
#define SAFETY_PIO_FORCED      0x0001  /* PIO mode forced */
#define SAFETY_PATCHES_OK      0x0002  /* Patches verified */
#define SAFETY_BOUNDARY_CHECK  0x0004  /* DMA boundary checking */
#define SAFETY_CACHE_OPS       0x0008  /* Cache operations active */
#define SAFETY_STACK_GUARD     0x0010  /* ISR stack protected */
#define SAFETY_DMA_VALIDATED   0x0020  /* Bus master test passed */
#define SAFETY_KILL_SWITCH     0x8000  /* Kill switch activated */

/* Build flags (AH=84h, returned in BX) */
#define BUILD_PRODUCTION       0x8000  /* Production build */
#define BUILD_PIO_MODE        0x0001  /* PIO mode active */
#define BUILD_DMA_MODE        0x0002  /* DMA mode active */
#define BUILD_DEBUG           0x0004  /* Debug features */
#define BUILD_LOGGING         0x0008  /* Logging enabled */
#define BUILD_STATS           0x0010  /* Statistics enabled */

/* Health codes (AH=82h, returned in DX) */
#define HEALTH_ALL_GOOD       0x0A11  /* All systems operational */
#define HEALTH_DEGRADED       0x0BAD  /* Running with limitations */
#define HEALTH_CHECK_FAILED   0xDEAD  /* Critical check failed */

/* Ensure structures are byte-packed (no padding) for predictable layout */
#pragma pack(push, 1)

/**
 * @brief Snapshot structure for vendor discovery (8 bytes)
 * Precomputed at init, read-only at runtime
 */
typedef struct {
    uint16_t signature;         /* '3C' */
    uint16_t version;          /* BCD version (0x0100 = 1.00) */
    uint16_t max_function;     /* Highest supported AH code */
    uint16_t capabilities;     /* Capability flags */
} extension_info_t;

/**
 * @brief Snapshot structure for safety state (8 bytes)
 */
typedef struct {
    uint16_t flags;            /* Safety flags */
    uint16_t stack_free;       /* ISR stack bytes free */
    uint16_t patch_count;      /* Active patches */
    uint16_t reserved;         /* Reserved */
} safety_snapshot_t;

/**
 * @brief Snapshot structure for patch statistics (8 bytes)
 */
typedef struct {
    uint16_t patches_applied;  /* Total patches applied */
    uint16_t max_cli_ticks;    /* Maximum CLI duration */
    uint16_t modules_patched;  /* Number of modules */
    uint16_t health_code;      /* System health */
} patch_snapshot_t;

/**
 * @brief Snapshot structure for memory map (8 bytes)
 */
typedef struct {
    uint16_t hot_code_size;    /* Resident code bytes */
    uint16_t hot_data_size;    /* Resident data bytes */
    uint16_t stack_size;       /* ISR stack size */
    uint16_t total_resident;   /* Total resident bytes */
} memory_snapshot_t;

/**
 * @brief Snapshot structure for version info (8 bytes)
 */
typedef struct {
    uint16_t version_bcd;      /* Version in BCD format */
    uint16_t build_flags;      /* Build configuration */
    uint16_t nic_type;         /* NIC model present */
    uint16_t reserved;         /* Reserved */
} version_snapshot_t;

/**
 * @brief Unified snapshot table (40 bytes total)
 * Indexed by (AH - 0x80) * 8
 */
typedef struct {
    extension_info_t   discovery;  /* AH=80h */
    safety_snapshot_t  safety;     /* AH=81h */
    patch_snapshot_t   patches;    /* AH=82h */
    memory_snapshot_t  memory;     /* AH=83h */
    version_snapshot_t version;    /* AH=84h */
} extension_snapshots_t;

/* Restore default packing */
#pragma pack(pop)

/* Assembly interface */
extern extension_snapshots_t extension_snapshots;
extern void init_extension_snapshots(void);

/* C interface for initialization */
void update_safety_snapshot(void);
void update_patch_snapshot(void);
void update_memory_snapshot(void);
void set_resident_size(uint16_t paragraphs);

/* Test interface */
int test_extension_api(void);
int validate_register_preservation(void);
int validate_timing_bounds(void);

#ifdef __cplusplus
}
#endif

#endif /* _EXTENSION_API_H_ */