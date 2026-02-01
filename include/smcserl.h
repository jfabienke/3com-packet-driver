/**
 * @file smc_serialization.h
 * @brief Safe self-modifying code serialization declarations
 *
 * 3Com Packet Driver - SMC Serialization Module
 *
 * This header defines the interface for safe self-modifying code operations
 * with proper CPU serialization for patching safety operations into hot paths.
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef SMC_SERIALIZATION_H
#define SMC_SERIALIZATION_H

#include <stdint.h>
#include <stdbool.h>

/* Configuration constants */
#define MAX_PATCH_SITES     16      /* Maximum number of patch sites */
#define MAX_PATCH_SIZE      8       /* Maximum size of a single patch */
#define MAX_DESCRIPTION_LEN 64      /* Maximum length of patch description */

/* Patch site tracking structure */
typedef struct {
    void *address;                          /* Address to patch */
    uint8_t size;                           /* Size of patch in bytes */
    bool patched;                           /* Whether site is currently patched */
    uint8_t original_bytes[MAX_PATCH_SIZE]; /* Original bytes for rollback */
    char description[MAX_DESCRIPTION_LEN];  /* Human-readable description */
} smc_patch_site_t;

/* Single patch definition */
typedef struct {
    uint8_t site_index;                     /* Index of patch site */
    uint8_t patch_bytes[MAX_PATCH_SIZE];    /* Bytes to patch in */
} smc_patch_t;

/* Patch set for atomic application */
typedef struct {
    uint8_t num_patches;                    /* Number of patches in set */
    smc_patch_t patches[MAX_PATCH_SITES];   /* Array of patches */
} smc_patch_set_t;

/* Patch site information for queries */
typedef struct {
    void *address;                          /* Address of patch site */
    uint8_t size;                           /* Size of patch */
    bool patched;                           /* Current patch status */
    char description[MAX_DESCRIPTION_LEN];  /* Description */
} smc_patch_site_info_t;

/* Function declarations */

/* Initialization and management */
bool smc_serialization_init(void);
bool smc_is_initialized(void);

/* Patch site registration */
bool smc_register_patch_site(void *address, uint8_t size, const char *description);
uint8_t smc_get_num_patch_sites(void);
bool smc_get_patch_site_info(uint8_t site_index, smc_patch_site_info_t *info);

/* Single patch operations */
bool smc_apply_patch(uint8_t site_index, const uint8_t *patch_bytes);
bool smc_rollback_patch(uint8_t site_index);

/* Atomic patch set operations */
bool smc_apply_patch_set(const smc_patch_set_t *patch_set);

/* Debugging and status */
void smc_print_status(void);

/* Common patch byte sequences */
#define SMC_PATCH_NOP3          {0x90, 0x90, 0x90}                 /* 3-byte NOP */
#define SMC_PATCH_CALL_REL      {0xE8, 0x00, 0x00}                 /* Near call with 16-bit offset */
#define SMC_PATCH_JMP_REL       {0xE9, 0x00, 0x00}                 /* Near jump with 16-bit offset */

/* Helper macros for common operations */
#define SMC_REGISTER_SITE(addr, desc) \
    smc_register_patch_site((void*)(addr), 3, (desc))

#define SMC_PATCH_TO_CALL(site_idx, target_func) do { \
    uint8_t patch_bytes[3] = {0xE8, 0x00, 0x00}; \
    uint16_t offset = (uint16_t)((uintptr_t)(target_func) - \
                      (uintptr_t)patch_sites[(site_idx)].address - 3); \
    patch_bytes[1] = (uint8_t)(offset & 0xFF); \
    patch_bytes[2] = (uint8_t)((offset >> 8) & 0xFF); \
    smc_apply_patch((site_idx), patch_bytes); \
} while(0)

/* Error codes for SMC operations */
typedef enum {
    SMC_SUCCESS,                /* 0: Success */
    SMC_ERROR_NOT_INITIALIZED,  /* 1: Not initialized */
    SMC_ERROR_INVALID_SITE,     /* 2: Invalid site */
    SMC_ERROR_INVALID_PARAMS,   /* 3: Invalid parameters */
    SMC_ERROR_PATCH_FAILED,     /* 4: Patch failed */
    SMC_ERROR_ALREADY_PATCHED,  /* 5: Already patched */
    SMC_ERROR_NOT_PATCHED,      /* 6: Not patched */
    SMC_ERROR_VERIFICATION_FAILED /* 7: Verification failed */
} smc_error_t;

/* Patch type identifiers for safety operations */
/* Note: Values start at 1 to distinguish from uninitialized (0) */
typedef enum {
    SMC_PATCH_TYPE_RESERVED,            /* 0: Reserved/uninitialized */
    SMC_PATCH_TYPE_VDS_LOCK,            /* 1: VDS buffer locking */
    SMC_PATCH_TYPE_VDS_UNLOCK,          /* 2: VDS buffer unlocking */
    SMC_PATCH_TYPE_CACHE_FLUSH,         /* 3: Cache flush operation */
    SMC_PATCH_TYPE_BOUNCE_BUFFER,       /* 4: Bounce buffer operation */
    SMC_PATCH_TYPE_CHECK_64KB,          /* 5: 64KB boundary check */
    SMC_PATCH_TYPE_SAFE_INT,            /* 6: Safe interrupt handling */
    SMC_PATCH_TYPE_NOP                  /* 7: No operation (remove patch) */
} smc_patch_type_t;

#endif /* SMC_SERIALIZATION_H */
