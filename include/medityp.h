/**
 * @file media_types.h
 * @brief Comprehensive media type definitions and documentation for 3Com 3c509 family
 *
 * This header provides complete media type support for all variants of the 3Com 3c509
 * family, including ISA and Fast Ethernet models. It extends the basic definitions in
 * nic_defs.h with detailed capability descriptions and usage guidelines.
 *
 * SUPPORTED 3COM MODELS:
 * - 3c509B      : ISA 10Mbps with multiple media options
 * - 3c509-TP    : ISA 10Mbps 10BaseT only
 * - 3c509-BNC   : ISA 10Mbps 10Base2 only  
 * - 3c509-Combo : ISA 10Mbps auto-select media
 * - 3c515       : ISA 10/100Mbps Fast Ethernet
 *
 * MEDIA TYPES SUPPORTED:
 * - 10BaseT (RJ45) : Twisted pair, supports full duplex, link detection
 * - 10Base2 (BNC)  : Thin coaxial, half duplex only, no link detection
 * - AUI (DB15)     : External transceiver, SQE heartbeat support
 * - 10BaseFL       : Fiber optic (rare), full duplex capable
 * - 100BaseTX      : Fast Ethernet twisted pair (3c515 only)
 * - 100BaseFX      : Fast Ethernet fiber (3c515 only)
 *
 * @note This file is part of Phase 0A implementation for comprehensive 3c509
 *       family support in the 3Com packet driver project.
 */

#ifndef _MEDIA_TYPES_H_
#define _MEDIA_TYPES_H_

#include "nic_defs.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Media Type Capability Matrix ---

/**
 * @brief Media capability matrix for different 3c509 family models
 * 
 * This matrix defines which media types are supported by each NIC model variant.
 * Use these constants to determine valid media configurations at runtime.
 */

/* 3c509B Combo Card - Most flexible, supports multiple media with auto-selection */
#define MEDIA_MATRIX_3C509B_COMBO { \
    {MEDIA_TYPE_10BASE_T,   MEDIA_CAP_10BASE_T | MEDIA_CAP_LINK_DETECT | MEDIA_CAP_FULL_DUPLEX}, \
    {MEDIA_TYPE_10BASE_2,   MEDIA_CAP_10BASE_2}, \
    {MEDIA_TYPE_AUI,        MEDIA_CAP_AUI}, \
    {MEDIA_TYPE_COMBO,      MEDIA_CAP_AUTO_SELECT}, \
    {MEDIA_TYPE_UNKNOWN,    0} /* Terminator */ \
}

/* 3c509-TP - 10BaseT only variant */
#define MEDIA_MATRIX_3C509_TP { \
    {MEDIA_TYPE_10BASE_T,   MEDIA_CAP_10BASE_T | MEDIA_CAP_LINK_DETECT}, \
    {MEDIA_TYPE_UNKNOWN,    0} /* Terminator */ \
}

/* 3c509-BNC - 10Base2 only variant */
#define MEDIA_MATRIX_3C509_BNC { \
    {MEDIA_TYPE_10BASE_2,   MEDIA_CAP_10BASE_2}, \
    {MEDIA_TYPE_UNKNOWN,    0} /* Terminator */ \
}

/* 3c515 Fast Ethernet - Multi-speed with advanced features */
#define MEDIA_MATRIX_3C515_TX { \
    {MEDIA_TYPE_10BASE_T,   MEDIA_CAP_10BASE_T | MEDIA_CAP_LINK_DETECT | MEDIA_CAP_FULL_DUPLEX}, \
    {MEDIA_TYPE_100BASE_TX, MEDIA_CAP_100BASE_TX | MEDIA_CAP_LINK_DETECT | MEDIA_CAP_FULL_DUPLEX}, \
    {MEDIA_TYPE_MII,        MEDIA_CAP_MII | MEDIA_CAP_AUTO_SELECT}, \
    {MEDIA_TYPE_AUTO_DETECT, MEDIA_CAP_AUTO_SELECT}, \
    {MEDIA_TYPE_UNKNOWN,    0} /* Terminator */ \
}

// --- Media Type Characteristics ---

/**
 * @brief Detailed characteristics for each media type
 * 
 * These structures provide comprehensive information about media type capabilities,
 * limitations, and configuration requirements.
 */

typedef struct {
    media_type_t type;              /* Media type identifier */
    const char* name;               /* Human-readable name */
    const char* description;        /* Detailed description */
    uint16_t max_speed_mbps;        /* Maximum speed in Mbps */
    uint8_t supports_full_duplex;   /* Full duplex capability (0=no, 1=yes) */
    uint8_t supports_link_detect;   /* Link detection capability */
    uint8_t requires_termination;   /* Requires bus termination */
    uint16_t max_cable_length_m;    /* Maximum cable length in meters */
} media_characteristics_t;

/**
 * @brief Media characteristics database
 * 
 * Complete characteristics for all supported media types.
 */
static const media_characteristics_t MEDIA_CHARACTERISTICS[] = {
    {
        .type = MEDIA_TYPE_10BASE_T,
        .name = MEDIA_STR_10BASE_T,
        .description = "10Mbps twisted pair (RJ45)",
        .max_speed_mbps = 10,
        .supports_full_duplex = 1,
        .supports_link_detect = 1,
        .requires_termination = 0,
        .max_cable_length_m = 100
    },
    {
        .type = MEDIA_TYPE_10BASE_2,
        .name = MEDIA_STR_10BASE_2,  
        .description = "10Mbps thin coaxial (BNC)",
        .max_speed_mbps = 10,
        .supports_full_duplex = 0,
        .supports_link_detect = 0,
        .requires_termination = 1,
        .max_cable_length_m = 185
    },
    {
        .type = MEDIA_TYPE_AUI,
        .name = MEDIA_STR_AUI,
        .description = "Attachment Unit Interface (DB15)",
        .max_speed_mbps = 10,
        .supports_full_duplex = 1,
        .supports_link_detect = 0,
        .requires_termination = 0,
        .max_cable_length_m = 50
    },
    {
        .type = MEDIA_TYPE_10BASE_FL,
        .name = MEDIA_STR_10BASE_FL,
        .description = "10Mbps fiber optic link",
        .max_speed_mbps = 10,
        .supports_full_duplex = 1,
        .supports_link_detect = 1,
        .requires_termination = 0,
        .max_cable_length_m = 2000
    },
    {
        .type = MEDIA_TYPE_100BASE_TX,
        .name = MEDIA_STR_100BASE_TX,
        .description = "100Mbps twisted pair (RJ45)",
        .max_speed_mbps = 100,
        .supports_full_duplex = 1,
        .supports_link_detect = 1,
        .requires_termination = 0,
        .max_cable_length_m = 100
    },
    {
        .type = MEDIA_TYPE_100BASE_FX,
        .name = MEDIA_STR_100BASE_FX,
        .description = "100Mbps fiber optic",
        .max_speed_mbps = 100,
        .supports_full_duplex = 1,
        .supports_link_detect = 1,
        .requires_termination = 0,
        .max_cable_length_m = 2000
    },
    {
        .type = MEDIA_TYPE_UNKNOWN,
        .name = MEDIA_STR_UNKNOWN,
        .description = "Unknown or undetected media",
        .max_speed_mbps = 0,
        .supports_full_duplex = 0,
        .supports_link_detect = 0,
        .requires_termination = 0,
        .max_cable_length_m = 0
    }
};

// --- Media Type Utility Functions ---

/**
 * @brief Get media characteristics for a specific media type
 * @param media Media type to query
 * @return Pointer to characteristics structure, or NULL if not found
 */
const media_characteristics_t* get_media_characteristics(media_type_t media);

/**
 * @brief Determine optimal media type for a given NIC and environment
 * @param nic_type Type of NIC hardware
 * @param available_media Bitmask of physically available media connections
 * @param prefer_speed Prefer higher speed when multiple options available
 * @return Recommended media type
 */
media_type_t suggest_optimal_media(nic_type_t nic_type, uint16_t available_media, int prefer_speed);

/**
 * @brief Validate media configuration against NIC capabilities
 * @param nic_type Type of NIC hardware
 * @param requested_media Requested media configuration
 * @param error_msg Buffer for error message (optional, can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return 1 if valid, 0 if invalid
 */
int validate_media_selection(nic_type_t nic_type, media_type_t requested_media, 
                           char* error_msg, size_t error_msg_size);

/**
 * @brief Generate human-readable media capability summary
 * @param caps Media capability flags
 * @param buffer Output buffer for summary text
 * @param buffer_size Size of output buffer
 * @return Number of characters written to buffer
 */
int format_media_capabilities(uint16_t caps, char* buffer, size_t buffer_size);

// --- Backward Compatibility Helpers ---

/**
 * @brief Convert legacy xcvr_type_t to modern media_type_t
 * @param xcvr Legacy transceiver type
 * @return Equivalent media type
 */
static inline media_type_t xcvr_to_media_type(xcvr_type_t xcvr) {
    switch (xcvr) {
        case XCVR_TYPE_AUI:      return MEDIA_TYPE_AUI;
        case XCVR_TYPE_10BASE_T: return MEDIA_TYPE_10BASE_T;
        case XCVR_TYPE_BNC:      return MEDIA_TYPE_10BASE_2;
        default:                 return MEDIA_TYPE_UNKNOWN;
    }
}

/**
 * @brief Convert modern media_type_t to legacy xcvr_type_t
 * @param media Modern media type
 * @return Equivalent transceiver type (limited to legacy types)
 */
static inline xcvr_type_t media_type_to_xcvr(media_type_t media) {
    switch (media) {
        case MEDIA_TYPE_AUI:      return XCVR_TYPE_AUI;
        case MEDIA_TYPE_10BASE_T: return XCVR_TYPE_10BASE_T;
        case MEDIA_TYPE_10BASE_2: return XCVR_TYPE_BNC;
        default:                  return XCVR_TYPE_10BASE_T; /* Safe default */
    }
}

// --- Configuration Examples ---

/**
 * @brief Example configurations for different deployment scenarios
 * 
 * These examples demonstrate proper media type configuration for common
 * network deployment scenarios.
 */

/* Office environment with structured cabling */
#define MEDIA_CONFIG_OFFICE { \
    .media = MEDIA_TYPE_10BASE_T, \
    .media_caps = MEDIA_CAP_10BASE_T | MEDIA_CAP_LINK_DETECT | MEDIA_CAP_FULL_DUPLEX, \
    .force_full_duplex = 0 /* Auto-negotiate */ \
}

/* Legacy coaxial network */
#define MEDIA_CONFIG_LEGACY_COAX { \
    .media = MEDIA_TYPE_10BASE_2, \
    .media_caps = MEDIA_CAP_10BASE_2, \
    .force_full_duplex = 1 /* Force half-duplex */ \
}

/* External transceiver setup */
#define MEDIA_CONFIG_EXTERNAL_XCVR { \
    .media = MEDIA_TYPE_AUI, \
    .media_caps = MEDIA_CAP_AUI, \
    .force_full_duplex = 0 /* Depends on external transceiver */ \
}

/* Fast Ethernet high-performance */
#define MEDIA_CONFIG_FAST_ETHERNET { \
    .media = MEDIA_TYPE_100BASE_TX, \
    .media_caps = MEDIA_CAP_100BASE_TX | MEDIA_CAP_FULL_DUPLEX | MEDIA_CAP_LINK_DETECT, \
    .force_full_duplex = 2 /* Force full-duplex */ \
}

#ifdef __cplusplus
}
#endif

#endif /* _MEDIA_TYPES_H_ */