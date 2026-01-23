#ifndef PLATFORM_PROBE_H
#define PLATFORM_PROBE_H

#include <stdint.h>
#include <stdbool.h>

/* Platform Detection and DMA Policy Module
 * 
 * Simplified detection strategy based on GPT-5 recommendations:
 * - VDS presence is the primary policy gate
 * - Skip V86 mode detection (unreliable and unnecessary)
 * - Optional virtualizer detection when VDS absent
 * - Conservative DMA policy enforcement
 */

/* DMA Policy Types */
typedef enum {
    DMA_POLICY_DIRECT = 0,      /* Real mode - direct physical access allowed */
    DMA_POLICY_COMMONBUF,       /* V86 + VDS - use VDS services for DMA */
    DMA_POLICY_FORBID           /* V86 without VDS - no DMA allowed */
} dma_policy_t;

/* Platform Probe Results */
typedef struct {
    /* Core Detection */
    bool vds_available;         /* VDS services present */
    uint16_t dos_version;       /* DOS version (major.minor) */
    
    /* Optional Virtualizer Detection (when VDS absent) */
    bool vcpi_present;          /* VCPI services detected */
    bool windows_enhanced;      /* Windows Enhanced mode */
    bool emm386_detected;       /* EMM386/similar memory manager */
    bool qemm_detected;         /* QEMM memory manager */
    
    /* Policy Decision */
    dma_policy_t recommended_policy;
    
    /* Capability Flags */
    bool safe_for_busmaster;    /* Bus-master DMA is safe */
    bool requires_vds;          /* DMA requires VDS services */
    bool pio_fallback_ok;       /* Can fall back to PIO */
    
    /* Environment Description */
    char environment_desc[64];  /* Human-readable environment description */
} platform_probe_result_t;

/* Global Platform State */
extern platform_probe_result_t g_platform;
extern dma_policy_t g_dma_policy;

/* Core Platform Detection Functions */

/**
 * @brief Perform comprehensive platform detection
 * @return Platform detection results
 */
platform_probe_result_t platform_detect(void);

/**
 * @brief Initialize platform detection and set global policy
 * @return 0 on success, negative on error
 */
int platform_init(void);

/**
 * @brief Get current DMA policy
 * @return Current DMA policy
 */
dma_policy_t platform_get_dma_policy(void);

/**
 * @brief Check if bus-master DMA is allowed under current policy
 * @return true if bus-master DMA allowed, false otherwise
 */
bool platform_allow_busmaster_dma(void);

/**
 * @brief Check if PIO fallback is available for given NIC type
 * @param nic_type NIC type identifier
 * @return true if PIO fallback available, false otherwise
 */
bool platform_has_pio_fallback(int nic_type);

/* Specific Detection Functions */

/**
 * @brief Detect VDS (Virtual DMA Services) availability
 * @return true if VDS present, false otherwise
 */
bool detect_vds_services(void);

/**
 * @brief Detect VCPI (Virtual Control Program Interface) presence
 * @return true if VCPI present, false otherwise
 */
bool detect_vcpi_services(void);

/**
 * @brief Detect Windows Enhanced mode
 * @return true if Windows Enhanced mode active, false otherwise
 */
bool detect_windows_enhanced_mode(void);

/**
 * @brief Detect EMM386 or similar memory manager
 * @return true if EMM386 detected, false otherwise
 */
bool detect_emm386_manager(void);

/**
 * @brief Detect QEMM memory manager
 * @return true if QEMM detected, false otherwise
 */
bool detect_qemm_manager(void);

/**
 * @brief Get DOS version
 * @return DOS version in format (major << 8) | minor
 */
uint16_t get_dos_version(void);

/* Policy Helper Functions */

/**
 * @brief Get human-readable policy description
 * @param policy DMA policy
 * @return Policy description string
 */
const char *platform_get_policy_desc(dma_policy_t policy);

/**
 * @brief Get platform environment description
 * @param result Platform probe result
 * @return Environment description string
 */
const char *platform_get_environment_desc(const platform_probe_result_t *result);

/**
 * @brief Validate DMA policy for specific NIC type
 * @param nic_type NIC type identifier
 * @param policy Proposed DMA policy
 * @return true if policy is safe for NIC type, false otherwise
 */
bool platform_validate_policy_for_nic(int nic_type, dma_policy_t policy);

/* NIC Type Constants for Policy Validation */
#define NIC_TYPE_3C509B     1   /* 3Com 3C509B (PIO only) */
#define NIC_TYPE_3C515_TX   2   /* 3Com 3C515-TX (bus-master) */

/* Platform Capability Flags */
#define PLATFORM_CAP_REAL_MODE      0x0001  /* Real mode environment */
#define PLATFORM_CAP_V86_MODE       0x0002  /* Virtual 8086 mode */
#define PLATFORM_CAP_PROTECTED_MODE 0x0004  /* Protected mode */
#define PLATFORM_CAP_VDS_SERVICES   0x0008  /* VDS services available */
#define PLATFORM_CAP_VCPI_SERVICES  0x0010  /* VCPI services available */
#define PLATFORM_CAP_DPMI_SERVICES  0x0020  /* DPMI services available */

/* Error Codes */
#define PLATFORM_SUCCESS            0
#define PLATFORM_ERROR_DETECTION   -1
#define PLATFORM_ERROR_UNSAFE      -2
#define PLATFORM_ERROR_NO_VDS      -3

#endif /* PLATFORM_PROBE_H */