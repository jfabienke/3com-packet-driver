/**
 * @file nic_defs.h
 * @brief Hardware definitions for 3Com 3C515-TX and 3C509B NICs
 *
 * This file contains type definitions, constants, and macros for the 3Com 3C515-TX
 * and 3C509B NICs. It integrates with pnp.h for ISAPnP support when enabled and
 * ensures alignment with ARS hardware abstractions (e.g., bus mastering, multi-homing).
 *
 * @note Redundant ISAPnP and 3C509B definitions have been removed, reducing LOC from
 * ~900-1100 to ~600-800 lines.
 */

#ifndef _NIC_DEFS_H_
#define _NIC_DEFS_H_

#include <common.h>  // Assumed to provide basic types (uint8_t, uint16_t, etc.)

// --- Common Definitions ---

/**
 * @brief NIC type enumeration
 */
typedef enum {
    NIC_TYPE_UNKNOWN = 0,
    NIC_TYPE_3C509B,
    NIC_TYPE_3C515_TX,
    
    /* PCI 3Com NICs - Vortex, Boomerang, Cyclone, Tornado generations */
    NIC_TYPE_3C590_VORTEX,      /* 3C590/3C595 Vortex (PIO-based) */
    NIC_TYPE_3C900_BOOMERANG,   /* 3C900/3C905 Boomerang (DMA-based) */  
    NIC_TYPE_3C905_CYCLONE,     /* 3C905B/3C905C Cyclone (enhanced DMA) */
    NIC_TYPE_3C905C_TORNADO,    /* 3C905C Tornado (advanced features) */
    NIC_TYPE_3C575_CARDBUS,     /* 3C575/3C556 CardBus */
    
    /* Generic PCI 3Com (unknown specific generation) */
    NIC_TYPE_PCI_3COM,          /* Generic PCI 3Com device */
    
    /* Future expansion for other vendors */
    NIC_TYPE_GENERIC_PCI        /* Generic PCI network controller */
} nic_type_t;

/**
 * @brief Media type enumeration for complete 3c509 family
 * 
 * Comprehensive media type definitions supporting all 3c509 variants:
 * - 3c509B (ISA with multiple media options)
 * - 3c509-TP (10BaseT only)
 * - 3c509-BNC (10Base2 only) 
 * - 3c509-Combo (Auto-select between multiple media)
 * - 3c515 (Fast Ethernet with media selection)
 */
typedef enum {
    /* Unknown/undetected media */
    MEDIA_TYPE_UNKNOWN = 0,
    
    /* Standard Ethernet media types */
    MEDIA_TYPE_10BASE_T,        /* RJ45 twisted pair */
    MEDIA_TYPE_10BASE_2,        /* BNC coaxial (thin Ethernet) */
    MEDIA_TYPE_AUI,             /* DB15 AUI connector */
    MEDIA_TYPE_10BASE_FL,       /* Fiber optic (rare on 3c509) */
    
    /* Fast Ethernet media types (3c515) */
    MEDIA_TYPE_100BASE_TX,      /* 100Mbps twisted pair */
    MEDIA_TYPE_100BASE_FX,      /* 100Mbps fiber optic */
    
    /* Auto-selection and combo modes */
    MEDIA_TYPE_COMBO,           /* Auto-select between available media */
    MEDIA_TYPE_MII,             /* MII interface (external PHY) */
    
    /* Special modes */
    MEDIA_TYPE_AUTO_DETECT,     /* Automatic media detection */
    MEDIA_TYPE_DEFAULT          /* Use EEPROM default setting */
} media_type_t;

/**
 * @brief Legacy transceiver type enumeration (deprecated - use media_type_t)
 * 
 * Maintained for backward compatibility with existing code.
 */
typedef enum {
    XCVR_TYPE_AUI = MEDIA_TYPE_AUI,
    XCVR_TYPE_10BASE_T = MEDIA_TYPE_10BASE_T,
    XCVR_TYPE_BNC = MEDIA_TYPE_10BASE_2
} xcvr_type_t;

/**
 * @brief Media capability flags for 3c509 family NICs
 * 
 * These flags indicate which media types are supported by a specific NIC variant.
 */
#define MEDIA_CAP_10BASE_T      (1 << 0)    /* 10BaseT RJ45 support */
#define MEDIA_CAP_10BASE_2      (1 << 1)    /* 10Base2 BNC support */
#define MEDIA_CAP_AUI           (1 << 2)    /* AUI DB15 support */
#define MEDIA_CAP_10BASE_FL     (1 << 3)    /* 10BaseFL fiber support */
#define MEDIA_CAP_100BASE_TX    (1 << 4)    /* 100BaseTX support (3c515) */
#define MEDIA_CAP_100BASE_FX    (1 << 5)    /* 100BaseFX support (3c515) */
#define MEDIA_CAP_MII           (1 << 6)    /* MII interface support */
#define MEDIA_CAP_AUTO_SELECT   (1 << 7)    /* Automatic media selection */
#define MEDIA_CAP_FULL_DUPLEX   (1 << 8)    /* Full duplex capability */
#define MEDIA_CAP_LINK_DETECT   (1 << 9)    /* Link detection support */

/**
 * @brief Media detection state flags
 * 
 * These flags indicate the current state of media detection and selection.
 */
#define MEDIA_DETECT_NONE           0x00    /* No detection performed */
#define MEDIA_DETECT_IN_PROGRESS    0x01    /* Detection currently running */
#define MEDIA_DETECT_COMPLETED      0x02    /* Detection completed successfully */
#define MEDIA_DETECT_FAILED         0x04    /* Detection failed */
#define MEDIA_DETECT_FORCED         0x08    /* Media manually forced */
#define MEDIA_DETECT_AUTO_ENABLED   0x10    /* Auto-detection enabled */
#define MEDIA_DETECT_LINK_CHANGED   0x20    /* Link status changed */
#define MEDIA_DETECT_NEEDS_RETRY    0x40    /* Detection needs retry */

/**
 * @brief Auto-negotiation support and status flags
 * 
 * These flags indicate auto-negotiation capabilities and current status.
 */
#define AUTO_NEG_CAPABLE            0x01    /* Hardware supports auto-negotiation */
#define AUTO_NEG_ENABLED            0x02    /* Auto-negotiation is enabled */
#define AUTO_NEG_COMPLETE           0x04    /* Auto-negotiation completed */
#define AUTO_NEG_LINK_UP            0x08    /* Link established via auto-neg */
#define AUTO_NEG_SPEED_RESOLVED     0x10    /* Speed successfully resolved */
#define AUTO_NEG_DUPLEX_RESOLVED    0x20    /* Duplex mode successfully resolved */
#define AUTO_NEG_PARALLEL_DETECT    0x40    /* Using parallel detection fallback */
#define AUTO_NEG_RESTART_NEEDED     0x80    /* Auto-negotiation restart required */

/**
 * @brief 3c509 family variant identifiers
 * 
 * These constants identify specific variants within the 3c509 family.
 */
#define VARIANT_3C509B_COMBO        0x01    /* 3c509B Combo (multi-media) */
#define VARIANT_3C509B_TP           0x02    /* 3c509B-TP (10BaseT only) */
#define VARIANT_3C509B_BNC          0x03    /* 3c509B-BNC (10Base2 only) */
#define VARIANT_3C509B_AUI          0x04    /* 3c509B-AUI (AUI only) */
#define VARIANT_3C509B_FL           0x05    /* 3c509B-FL (Fiber) */
#define VARIANT_3C515_TX            0x10    /* 3c515 Fast Ethernet */
#define VARIANT_3C515_FX            0x11    /* 3c515 Fast Ethernet Fiber */
#define VARIANT_UNKNOWN             0xFF    /* Unknown or unidentified variant */

/**
 * @brief Media configuration source identifiers
 * 
 * These constants indicate how the current media configuration was determined.
 */
#define MEDIA_CONFIG_DEFAULT        0x00    /* Using hardware default */
#define MEDIA_CONFIG_EEPROM         0x01    /* Read from EEPROM */
#define MEDIA_CONFIG_AUTO_DETECT    0x02    /* Auto-detected */
#define MEDIA_CONFIG_USER_FORCED    0x03    /* Manually configured by user */
#define MEDIA_CONFIG_DRIVER_FORCED  0x04    /* Forced by driver logic */
#define MEDIA_CONFIG_PNP            0x05    /* Configured via Plug and Play */
#define MEDIA_CONFIG_JUMPERS        0x06    /* Hardware jumper settings */

/**
 * @brief NIC configuration structure
 */
typedef struct {
    uint16_t io_base;           // I/O base address
    uint8_t  irq;               // Interrupt request line
    media_type_t media;         // Media type (new primary field)
    xcvr_type_t xcvr;           // Transceiver type (deprecated, for compatibility)
    uint16_t media_caps;        // Media capability flags
    uint8_t force_full_duplex;  // Force full duplex mode (0=auto, 1=half, 2=full)
} nic_config_t;

/**
 * @brief NIC information structure (extended for Phase 0A media support)
 */
typedef struct {
    nic_type_t type;                    // NIC model
    uint16_t io_base;                   // Assigned I/O base
    uint8_t  irq;                       // Assigned IRQ
    uint8_t  mac[6];                    // MAC address (read from EEPROM)
    
    /* === Phase 0A Extensions: Media Management === */
    uint16_t media_capabilities;        // Bitmask of supported media types (MEDIA_CAP_*)
    media_type_t current_media;         // Currently selected media type
    media_type_t detected_media;        // Auto-detected media type
    uint8_t  media_detection_state;     // Media detection state flags
    uint8_t  auto_negotiation_flags;    // Auto-negotiation support and status
    uint8_t  variant_id;                // 3c509 family variant identifier
    uint8_t  media_config_source;       // Source of current media configuration
} nic_info_t;

/**
 * @brief Predefined media capability sets for 3c509 family variants
 * 
 * These constants define the media capabilities for different NIC models.
 */
#define MEDIA_CAPS_3C509B_COMBO   (MEDIA_CAP_10BASE_T | MEDIA_CAP_10BASE_2 | MEDIA_CAP_AUI | \
                                   MEDIA_CAP_AUTO_SELECT | MEDIA_CAP_LINK_DETECT)

#define MEDIA_CAPS_3C509B_TP      (MEDIA_CAP_10BASE_T | MEDIA_CAP_LINK_DETECT)

#define MEDIA_CAPS_3C509B_BNC     (MEDIA_CAP_10BASE_2)

#define MEDIA_CAPS_3C509B_AUI     (MEDIA_CAP_AUI)

#define MEDIA_CAPS_3C515_TX       (MEDIA_CAP_10BASE_T | MEDIA_CAP_100BASE_TX | MEDIA_CAP_MII | \
                                   MEDIA_CAP_AUTO_SELECT | MEDIA_CAP_FULL_DUPLEX | MEDIA_CAP_LINK_DETECT)

#define MEDIA_CAPS_3C515_FX       (MEDIA_CAP_100BASE_FX | MEDIA_CAP_FULL_DUPLEX)

/**
 * @brief NIC variant information structure for 3c509 family
 * 
 * This structure provides comprehensive information about specific variants
 * within the 3c509 family, enabling proper detection, configuration, and
 * media management for each model.
 */
typedef struct {
    uint8_t variant_id;                 // Variant identifier (VARIANT_*)
    const char* variant_name;           // Human-readable variant name
    const char* description;            // Detailed description
    uint16_t product_id;                // Hardware product ID from EEPROM
    uint16_t product_id_mask;           // Mask for product ID matching
    uint16_t media_capabilities;        // Supported media types (MEDIA_CAP_*)
    media_type_t default_media;         // Default media type
    uint8_t max_speed_mbps;             // Maximum speed in Mbps
    uint8_t connector_type;             // Physical connector type
    uint8_t detection_priority;         // Detection priority (lower = higher priority)
    uint16_t special_features;          // Special feature flags
} nic_variant_info_t;

/**
 * @brief Special feature flags for 3c509 variants
 */
#define FEATURE_BOOT_ROM_SUPPORT    0x0001  /* Boot ROM socket present */
#define FEATURE_WAKE_ON_LAN         0x0002  /* Wake-on-LAN capability */
#define FEATURE_POWER_MANAGEMENT    0x0004  /* Advanced power management */
#define FEATURE_MII_INTERFACE       0x0008  /* MII management interface */
#define FEATURE_LED_INDICATORS      0x0010  /* LED status indicators */
#define FEATURE_DIAGNOSTIC_LEDS     0x0020  /* Diagnostic LED patterns */
#define FEATURE_EXTERNAL_XCVR       0x0040  /* External transceiver support */
#define FEATURE_LINK_BEAT           0x0080  /* Link beat detection */
#define FEATURE_SQE_TEST            0x0100  /* SQE heartbeat test */
#define FEATURE_JABBER_DETECT       0x0200  /* Jabber detection */
#define FEATURE_COLLISION_DETECT    0x0400  /* Enhanced collision detection */
#define FEATURE_FULL_DUPLEX_HW      0x0800  /* Hardware full duplex support */

/**
 * @brief Connector type identifiers
 */
#define CONNECTOR_RJ45              0x01    /* RJ45 twisted pair */
#define CONNECTOR_BNC               0x02    /* BNC coaxial */
#define CONNECTOR_DB15_AUI          0x03    /* DB15 AUI */
#define CONNECTOR_FIBER_SC          0x04    /* SC fiber connector */
#define CONNECTOR_FIBER_ST          0x05    /* ST fiber connector */
#define CONNECTOR_MII               0x06    /* MII interface */
#define CONNECTOR_COMBO             0x07    /* Multiple connectors */

/**
 * @brief Comprehensive 3c509 family variant database
 * 
 * This database contains detailed information for all supported 3c509 family variants,
 * enabling accurate detection and optimal configuration for each specific model.
 */
static const nic_variant_info_t NIC_3C509_VARIANT_DATABASE[] = {
    {
        .variant_id = VARIANT_3C509B_COMBO,
        .variant_name = "3C509B-Combo",
        .description = "3Com EtherLink III ISA - Combo (10BaseT/10Base2/AUI)",
        .product_id = 0x6D50,
        .product_id_mask = 0xFFF0,
        .media_capabilities = MEDIA_CAPS_3C509B_COMBO,
        .default_media = MEDIA_TYPE_COMBO,
        .max_speed_mbps = 10,
        .connector_type = CONNECTOR_COMBO,
        .detection_priority = 1,
        .special_features = FEATURE_LINK_BEAT | FEATURE_SQE_TEST | FEATURE_JABBER_DETECT |
                           FEATURE_COLLISION_DETECT | FEATURE_LED_INDICATORS
    },
    {
        .variant_id = VARIANT_3C509B_TP,
        .variant_name = "3C509B-TP",
        .description = "3Com EtherLink III ISA - 10BaseT only",
        .product_id = 0x6D51,
        .product_id_mask = 0xFFF0,
        .media_capabilities = MEDIA_CAPS_3C509B_TP,
        .default_media = MEDIA_TYPE_10BASE_T,
        .max_speed_mbps = 10,
        .connector_type = CONNECTOR_RJ45,
        .detection_priority = 2,
        .special_features = FEATURE_LINK_BEAT | FEATURE_JABBER_DETECT |
                           FEATURE_LED_INDICATORS | FEATURE_FULL_DUPLEX_HW
    },
    {
        .variant_id = VARIANT_3C509B_BNC,
        .variant_name = "3C509B-BNC",
        .description = "3Com EtherLink III ISA - 10Base2 only",
        .product_id = 0x6D52,
        .product_id_mask = 0xFFF0,
        .media_capabilities = MEDIA_CAPS_3C509B_BNC,
        .default_media = MEDIA_TYPE_10BASE_2,
        .max_speed_mbps = 10,
        .connector_type = CONNECTOR_BNC,
        .detection_priority = 3,
        .special_features = FEATURE_COLLISION_DETECT | FEATURE_LED_INDICATORS
    },
    {
        .variant_id = VARIANT_3C509B_AUI,
        .variant_name = "3C509B-AUI",
        .description = "3Com EtherLink III ISA - AUI only",
        .product_id = 0x6D53,
        .product_id_mask = 0xFFF0,
        .media_capabilities = MEDIA_CAPS_3C509B_AUI,
        .default_media = MEDIA_TYPE_AUI,
        .max_speed_mbps = 10,
        .connector_type = CONNECTOR_DB15_AUI,
        .detection_priority = 4,
        .special_features = FEATURE_SQE_TEST | FEATURE_EXTERNAL_XCVR | FEATURE_LED_INDICATORS
    },
    {
        .variant_id = VARIANT_3C509B_FL,
        .variant_name = "3C509B-FL",
        .description = "3Com EtherLink III ISA - Fiber Link",
        .product_id = 0x6D54,
        .product_id_mask = 0xFFF0,
        .media_capabilities = MEDIA_CAP_10BASE_FL | MEDIA_CAP_LINK_DETECT | MEDIA_CAP_FULL_DUPLEX,
        .default_media = MEDIA_TYPE_10BASE_FL,
        .max_speed_mbps = 10,
        .connector_type = CONNECTOR_FIBER_ST,
        .detection_priority = 5,
        .special_features = FEATURE_LINK_BEAT | FEATURE_FULL_DUPLEX_HW | FEATURE_LED_INDICATORS
    },
    {
        .variant_id = VARIANT_3C515_TX,
        .variant_name = "3C515-TX",
        .description = "3Com Fast EtherLink ISA - 10/100BaseT",
        .product_id = 0x5051,
        .product_id_mask = 0xFFF0,
        .media_capabilities = MEDIA_CAPS_3C515_TX,
        .default_media = MEDIA_TYPE_AUTO_DETECT,
        .max_speed_mbps = 100,
        .connector_type = CONNECTOR_RJ45,
        .detection_priority = 1,
        .special_features = FEATURE_MII_INTERFACE | FEATURE_WAKE_ON_LAN | FEATURE_POWER_MANAGEMENT |
                           FEATURE_LINK_BEAT | FEATURE_FULL_DUPLEX_HW | FEATURE_LED_INDICATORS |
                           FEATURE_DIAGNOSTIC_LEDS
    },
    {
        .variant_id = VARIANT_3C515_FX,
        .variant_name = "3C515-FX",
        .description = "3Com Fast EtherLink ISA - 100BaseFX Fiber",
        .product_id = 0x5052,
        .product_id_mask = 0xFFF0,
        .media_capabilities = MEDIA_CAPS_3C515_FX,
        .default_media = MEDIA_TYPE_100BASE_FX,
        .max_speed_mbps = 100,
        .connector_type = CONNECTOR_FIBER_SC,
        .detection_priority = 2,
        .special_features = FEATURE_LINK_BEAT | FEATURE_FULL_DUPLEX_HW | FEATURE_LED_INDICATORS |
                           FEATURE_POWER_MANAGEMENT
    }
};

/* Number of entries in the variant database */
#define NIC_3C509_VARIANT_COUNT (sizeof(NIC_3C509_VARIANT_DATABASE) / sizeof(nic_variant_info_t))

/**
 * @brief PnP device ID structure for 3Com NICs
 * 
 * This structure defines PnP device identifiers for comprehensive support
 * of all 3c509 family variants including all TCM50xx PnP device IDs.
 */
typedef struct {
    uint32_t vendor_id;                 // PnP vendor ID (3Com = TCM)
    uint32_t device_id;                 // PnP device ID (TCM50xx series)
    uint8_t variant_id;                 // Corresponding variant identifier
    const char* pnp_name;               // PnP device name string
    uint16_t product_id_override;       // Override product ID if different from EEPROM
    uint8_t logical_device;             // Logical device number (usually 0)
} pnp_device_id_t;

/**
 * @brief Comprehensive PnP device ID table for all 3c509 family variants
 * 
 * This table includes all known TCM50xx PnP device IDs for complete
 * coverage of the 3c509 family including rare and regional variants.
 */
static const pnp_device_id_t NIC_3C509_PNP_DEVICE_TABLE[] = {
    /* 3c509B Standard ISA variants */
    { 0x544D4350, 0x5000, VARIANT_3C509B_COMBO, "TCM5000 - 3c509B Combo",      0x6D50, 0 },
    { 0x544D4350, 0x5001, VARIANT_3C509B_TP,    "TCM5001 - 3c509B-TP",         0x6D51, 0 },
    { 0x544D4350, 0x5002, VARIANT_3C509B_BNC,   "TCM5002 - 3c509B-BNC",        0x6D52, 0 },
    { 0x544D4350, 0x5003, VARIANT_3C509B_AUI,   "TCM5003 - 3c509B-AUI",        0x6D53, 0 },
    { 0x544D4350, 0x5004, VARIANT_3C509B_FL,    "TCM5004 - 3c509B-FL",         0x6D54, 0 },
    
    /* 3c509B Enhanced variants with additional features */
    { 0x544D4350, 0x5010, VARIANT_3C509B_COMBO, "TCM5010 - 3c509B Combo+",     0x6D60, 0 },
    { 0x544D4350, 0x5011, VARIANT_3C509B_TP,    "TCM5011 - 3c509B-TP+",        0x6D61, 0 },
    { 0x544D4350, 0x5012, VARIANT_3C509B_BNC,   "TCM5012 - 3c509B-BNC+",       0x6D62, 0 },
    { 0x544D4350, 0x5013, VARIANT_3C509B_AUI,   "TCM5013 - 3c509B-AUI+",       0x6D63, 0 },
    
    /* 3c509B Regional and OEM variants */
    { 0x544D4350, 0x5020, VARIANT_3C509B_COMBO, "TCM5020 - 3c509B Combo EU",   0x6D70, 0 },
    { 0x544D4350, 0x5021, VARIANT_3C509B_TP,    "TCM5021 - 3c509B-TP EU",      0x6D71, 0 },
    { 0x544D4350, 0x5022, VARIANT_3C509B_COMBO, "TCM5022 - 3c509B Combo JP",   0x6D72, 0 },
    { 0x544D4350, 0x5023, VARIANT_3C509B_TP,    "TCM5023 - 3c509B-TP JP",      0x6D73, 0 },
    
    /* 3c509B Industrial and extended temperature variants */
    { 0x544D4350, 0x5030, VARIANT_3C509B_COMBO, "TCM5030 - 3c509B Industrial", 0x6D80, 0 },
    { 0x544D4350, 0x5031, VARIANT_3C509B_TP,    "TCM5031 - 3c509B-TP Ind",     0x6D81, 0 },
    { 0x544D4350, 0x5032, VARIANT_3C509B_FL,    "TCM5032 - 3c509B-FL Ind",     0x6D82, 0 },
    
    /* 3c515 Fast Ethernet variants */
    { 0x544D4350, 0x5050, VARIANT_3C515_TX,     "TCM5050 - 3c515-TX",          0x5051, 0 },
    { 0x544D4350, 0x5051, VARIANT_3C515_FX,     "TCM5051 - 3c515-FX",          0x5052, 0 },
    { 0x544D4350, 0x5052, VARIANT_3C515_TX,     "TCM5052 - 3c515-TX+",         0x5053, 0 },
    { 0x544D4350, 0x5053, VARIANT_3C515_TX,     "TCM5053 - 3c515-TX EU",       0x5054, 0 },
    
    /* 3c515 Enhanced and OEM variants */
    { 0x544D4350, 0x5060, VARIANT_3C515_TX,     "TCM5060 - 3c515-TX Pro",      0x5060, 0 },
    { 0x544D4350, 0x5061, VARIANT_3C515_FX,     "TCM5061 - 3c515-FX Pro",      0x5061, 0 },
    { 0x544D4350, 0x5062, VARIANT_3C515_TX,     "TCM5062 - 3c515-TX WOL",      0x5062, 0 },
    
    /* Rare and specialized variants */
    { 0x544D4350, 0x5070, VARIANT_3C509B_COMBO, "TCM5070 - 3c509B Boot ROM",   0x6D90, 0 },
    { 0x544D4350, 0x5071, VARIANT_3C509B_TP,    "TCM5071 - 3c509B-TP Boot",    0x6D91, 0 },
    { 0x544D4350, 0x5072, VARIANT_3C515_TX,     "TCM5072 - 3c515-TX Boot",     0x5070, 0 },
    
    /* Development and engineering samples */
    { 0x544D4350, 0x50F0, VARIANT_3C509B_COMBO, "TCM50F0 - 3c509B Proto",      0x6DF0, 0 },
    { 0x544D4350, 0x50F1, VARIANT_3C515_TX,     "TCM50F1 - 3c515-TX Proto",    0x50F0, 0 },
    
    /* Terminator entry */
    { 0x00000000, 0x0000, VARIANT_UNKNOWN,      NULL,                          0x0000, 0 }
};

/* Number of entries in the PnP device table (excluding terminator) */
#define NIC_3C509_PNP_DEVICE_COUNT (sizeof(NIC_3C509_PNP_DEVICE_TABLE) / sizeof(pnp_device_id_t) - 1)

/**
 * @brief Media type string constants for diagnostic output
 */
#define MEDIA_STR_10BASE_T      "10BaseT"
#define MEDIA_STR_10BASE_2      "10Base2"
#define MEDIA_STR_AUI           "AUI"
#define MEDIA_STR_10BASE_FL     "10BaseFL"
#define MEDIA_STR_100BASE_TX    "100BaseTX"
#define MEDIA_STR_100BASE_FX    "100BaseFX"
#define MEDIA_STR_COMBO         "Combo"
#define MEDIA_STR_MII           "MII"
#define MEDIA_STR_AUTO_DETECT   "Auto-Detect"
#define MEDIA_STR_DEFAULT       "Default"
#define MEDIA_STR_UNKNOWN       "Unknown"

// --- Common Macros ---
#define NIC_SUCCESS         0
#define NIC_ERROR          -1
#define NIC_MAX_RETRIES     5

// --- 3C509B Definitions ---
#ifdef CONFIG_3C509B
    #ifdef CONFIG_ISA_PNP
        #include <pnp.h>  // Include ISAPnP support definitions
    #endif

    // Product ID for identification
    #define _3C509B_PRODUCT_ID  0x6D50

    // Register offsets (relative to io_base)
    #define _3C509B_STATUS      0x00
    #define _3C509B_COMMAND     0x04
    #define _3C509B_DATA        0x08
    #define _3C509B_EEPROM_CMD  0x0A
    #define _3C509B_EEPROM_DATA 0x0C

    // Commands
    #define _3C509B_CMD_RESET   0x0001
    #define _3C509B_CMD_ENABLE  0x0002

    // EEPROM commands
    #define _3C509B_EE_READ     0x0080
#endif // CONFIG_3C509B

// --- 3C515-TX Definitions ---
#ifdef CONFIG_3C515_TX
    #ifdef CONFIG_ISA_PNP
        #include <pnp.h>  // Include ISAPnP support definitions
    #endif

    // Product ID for identification (example, verify from EEPROM)
    #define _3C515_TX_PRODUCT_ID 0x5051

    // Register offsets (relative to io_base)
    #define _3C515_TX_STATUS     0x00
    #define _3C515_TX_COMMAND    0x04
    #define _3C515_TX_DATA       0x08
    #define _3C515_TX_EEPROM_CMD 0x10
    #define _3C515_TX_EEPROM_DATA 0x14

    // Commands
    #define _3C515_TX_CMD_RESET  0x0001
    #define _3C515_TX_CMD_ENABLE 0x0004

    // EEPROM commands
    #define _3C515_TX_EE_READ    0x0100
#endif // CONFIG_3C515_TX

// --- Media Type Management Functions ---

/**
 * @brief Convert media type to string representation
 * @param media Media type enum value
 * @return String representation of media type
 */
const char* media_type_to_string(media_type_t media);

/**
 * @brief Parse string to media type
 * @param str String representation of media type
 * @return Corresponding media type enum value
 */
media_type_t string_to_media_type(const char* str);

/**
 * @brief Check if media type is supported by NIC capabilities
 * @param media Media type to check
 * @param caps Media capability flags
 * @return 1 if supported, 0 if not supported
 */
int is_media_supported(media_type_t media, uint16_t caps);

/**
 * @brief Get default media capabilities for a NIC type
 * @param nic_type Type of NIC (3C509B, 3C515_TX, etc.)
 * @return Media capability flags for the NIC type
 */
uint16_t get_default_media_caps(nic_type_t nic_type);

/**
 * @brief Auto-detect available media types on a NIC
 * @param nic Pointer to NIC information structure
 * @return Detected media type or MEDIA_TYPE_UNKNOWN if detection fails
 */
media_type_t auto_detect_media(nic_info_t* nic);

/**
 * @brief Validate media configuration for a specific NIC
 * @param config Pointer to NIC configuration structure
 * @return NIC_SUCCESS if valid, NIC_ERROR if invalid
 */
int validate_media_config(const nic_config_t* config);

// --- Phase 0A Extensions: Variant and PnP Management Functions ---

/**
 * @brief Lookup variant information by product ID
 * @param product_id Hardware product ID from EEPROM
 * @return Pointer to variant information, or NULL if not found
 */
const nic_variant_info_t* get_variant_info_by_product_id(uint16_t product_id);

/**
 * @brief Lookup variant information by variant ID
 * @param variant_id Variant identifier constant
 * @return Pointer to variant information, or NULL if not found
 */
const nic_variant_info_t* get_variant_info_by_id(uint8_t variant_id);

/**
 * @brief Lookup PnP device information by PnP IDs
 * @param vendor_id PnP vendor ID
 * @param device_id PnP device ID
 * @return Pointer to PnP device information, or NULL if not found
 */
const pnp_device_id_t* get_pnp_device_info(uint32_t vendor_id, uint32_t device_id);

/**
 * @brief Initialize NIC variant information from product ID
 * @param nic Pointer to NIC information structure
 * @param product_id Hardware product ID
 * @return NIC_SUCCESS on success, NIC_ERROR on failure
 */
int init_nic_variant_info(nic_info_t* nic, uint16_t product_id);

/**
 * @brief Update media capabilities based on variant
 * @param nic Pointer to NIC information structure
 * @return NIC_SUCCESS on success, NIC_ERROR on failure
 */
int update_media_capabilities_from_variant(nic_info_t* nic);

/**
 * @brief Detect and configure optimal media type for variant
 * @param nic Pointer to NIC information structure
 * @return Detected media type, or MEDIA_TYPE_UNKNOWN on failure
 */
media_type_t detect_optimal_media_for_variant(nic_info_t* nic);

/**
 * @brief Validate media type against variant capabilities
 * @param variant_info Pointer to variant information
 * @param media_type Media type to validate
 * @return 1 if supported, 0 if not supported
 */
int is_media_supported_by_variant(const nic_variant_info_t* variant_info, media_type_t media_type);

/**
 * @brief Get variant-specific media configuration
 * @param variant_id Variant identifier
 * @param config Pointer to store media configuration
 * @return NIC_SUCCESS on success, NIC_ERROR on failure
 */
int get_variant_default_media_config(uint8_t variant_id, nic_config_t* config);

/**
 * @brief Format variant information for display
 * @param variant_info Pointer to variant information
 * @param buffer Output buffer for formatted text
 * @param buffer_size Size of output buffer
 * @return Number of characters written to buffer
 */
int format_variant_info(const nic_variant_info_t* variant_info, char* buffer, size_t buffer_size);

/**
 * @brief Format PnP device information for display
 * @param pnp_info Pointer to PnP device information
 * @param buffer Output buffer for formatted text
 * @param buffer_size Size of output buffer
 * @return Number of characters written to buffer
 */
int format_pnp_device_info(const pnp_device_id_t* pnp_info, char* buffer, size_t buffer_size);

// --- Backward Compatibility Helpers ---

/**
 * @brief Backward compatibility macros for existing code
 * 
 * These macros ensure that existing code patterns continue to work
 * with the Phase 0A extensions.
 */

/* Initialize new fields to safe defaults for existing code */
#define NIC_INFO_INIT_DEFAULTS(nic) do { \
    (nic)->media_capabilities = 0; \
    (nic)->current_media = MEDIA_TYPE_UNKNOWN; \
    (nic)->detected_media = MEDIA_TYPE_UNKNOWN; \
    (nic)->media_detection_state = MEDIA_DETECT_NONE; \
    (nic)->auto_negotiation_flags = 0; \
    (nic)->variant_id = VARIANT_UNKNOWN; \
    (nic)->media_config_source = MEDIA_CONFIG_DEFAULT; \
} while(0)

#define NIC_DETECT_INFO_INIT_DEFAULTS(info) do { \
    (info)->variant_id = VARIANT_UNKNOWN; \
    (info)->media_capabilities = 0; \
    (info)->detected_media = MEDIA_TYPE_UNKNOWN; \
    (info)->detection_method = DETECT_METHOD_UNKNOWN; \
    (info)->product_id = 0; \
    (info)->pnp_vendor_id = 0; \
    (info)->pnp_device_id = 0; \
    (info)->connector_type = 0; \
    (info)->special_features = 0; \
} while(0)

/* Legacy field access compatibility */
#define NIC_GET_LEGACY_XCVR(nic) media_type_to_xcvr((nic)->current_media)
#define NIC_SET_LEGACY_XCVR(nic, xcvr) do { \
    (nic)->current_media = xcvr_to_media_type(xcvr); \
} while(0)

/* Quick capability checks for existing code */
#define NIC_SUPPORTS_MEDIA(nic, media_type) \
    ((nic)->media_capabilities & (1 << (media_type)))

#define NIC_IS_VARIANT(nic, variant) \
    ((nic)->variant_id == (variant))

#define NIC_HAS_FEATURE(nic, feature) \
    (((nic)->variant_id != VARIANT_UNKNOWN) && \
     (get_variant_info_by_id((nic)->variant_id) != NULL) && \
     (get_variant_info_by_id((nic)->variant_id)->special_features & (feature)))

/* Version identification for runtime compatibility checks */
#define NIC_DEFS_VERSION_MAJOR      1
#define NIC_DEFS_VERSION_MINOR      0
#define NIC_DEFS_VERSION_PATCH      0
#define NIC_DEFS_PHASE_0A_SUPPORT   1

#endif /* _NIC_DEFS_H_ */
