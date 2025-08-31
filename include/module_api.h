/**
 * @file module_api.h
 * @brief Core Module API Specification for 3Com Packet Driver
 * 
 * Phase 3A: Dynamic Module Loading - Stream 1 Critical Path
 * 
 * This header defines the standard interface that ALL modules must implement.
 * Released Day 1 to unblock parallel development across all streams.
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef MODULE_API_H
#define MODULE_API_H

#include <stdint.h>
#include <stdbool.h>

/* Module system version */
#define MODULE_API_VERSION_MAJOR 1
#define MODULE_API_VERSION_MINOR 0
#define MODULE_API_VERSION ((MODULE_API_VERSION_MAJOR << 8) | MODULE_API_VERSION_MINOR)

/* Module magic number */
#define MODULE_MAGIC 0x4D44  /* 'MD' */

/* Module size limits */
#define MAX_MODULE_SIZE (64 * 1024)  /* 64KB maximum per module */
#define MIN_MODULE_SIZE 512          /* 512 bytes minimum */

/* Module name constraints */
#define MODULE_NAME_LENGTH 12
#define MODULE_DESC_LENGTH 32
#define MODULE_AUTHOR_LENGTH 16

/* Forward declarations */
typedef struct core_services core_services_t;
typedef struct module_config module_config_t;
typedef struct hardware_info hardware_info_t;
typedef struct nic_ops nic_ops_t;

/* ============================================================================
 * Module Type Definitions
 * ============================================================================ */

/**
 * @brief Module class identification
 */
typedef enum {
    MODULE_CLASS_HARDWARE = 0x0001,  /**< Hardware driver module */
    MODULE_CLASS_FEATURE  = 0x0002,  /**< Optional feature module */
    MODULE_CLASS_PROTOCOL = 0x0004   /**< Future: Protocol stack modules */
} module_class_t;

/**
 * @brief NIC family identifiers for hardware modules
 */
typedef enum {
    FAMILY_UNKNOWN    = 0x0000,  /**< Unknown or not applicable */
    FAMILY_ETHERLINK3 = 0x0509,  /**< EtherLink III family (3C509/3C509B/3C509C) */
    FAMILY_CORKSCREW  = 0x0515,  /**< Corkscrew family (3C515-TX) */
    FAMILY_VORTEX     = 0x0590,  /**< Future: Vortex family (3C590/3C595) */
    FAMILY_BOOMERANG  = 0x0900,  /**< Future: Boomerang family (3C900 series) */
    FAMILY_HURRICANE  = 0x0905   /**< Future: Hurricane family (3C905 series) */
} nic_family_t;

/**
 * @brief Feature capability flags
 */
typedef enum {
    FEATURE_ROUTING      = 0x0001,  /**< Multi-NIC routing */
    FEATURE_FLOW_CONTROL = 0x0002,  /**< 802.3x flow control */
    FEATURE_STATISTICS   = 0x0004,  /**< Advanced statistics */
    FEATURE_PROMISCUOUS  = 0x0008,  /**< Promiscuous mode */
    FEATURE_DIAGNOSTICS  = 0x0010,  /**< Diagnostic capabilities */
    FEATURE_MULTICAST    = 0x0020,  /**< Multicast support */
    FEATURE_WAKE_ON_LAN  = 0x0040,  /**< Wake-on-LAN support */
    FEATURE_INIT_ONLY    = 0x8000   /**< Init-only module (discarded after use) */
} feature_flags_t;

/* ============================================================================
 * Module Header Structure
 * ============================================================================ */

/**
 * @brief Standard module header (must be first in every module)
 * 
 * This structure MUST be the first data in every module file.
 * The core loader uses this to validate and load modules.
 */
typedef struct {
    /* Module identification */
    uint16_t magic;           /**< Module magic number (MODULE_MAGIC) */
    uint16_t version;         /**< Module version (BCD format: 0x0100 = v1.0) */
    uint16_t header_size;     /**< Size of this header structure */
    uint16_t module_size;     /**< Module size in paragraphs (16-byte units) */
    
    /* Module classification */
    uint16_t module_class;    /**< Module class (module_class_t) */
    uint16_t family_id;       /**< NIC family ID (for hardware modules) */
    uint16_t feature_flags;   /**< Feature capability flags */
    uint16_t api_version;     /**< Required API version */
    
    /* Entry points (offsets from module base) */
    uint16_t init_offset;     /**< Offset to initialization function */
    uint16_t vtable_offset;   /**< Offset to function table (hardware modules) */
    uint16_t cleanup_offset;  /**< Offset to cleanup function */
    uint16_t info_offset;     /**< Offset to module info structure */
    
    /* Dependencies and requirements */
    uint16_t deps_count;      /**< Number of module dependencies */
    uint16_t deps_offset;     /**< Offset to dependency list */
    uint16_t min_dos_version; /**< Minimum DOS version (0x0300 = DOS 3.0) */
    uint16_t min_cpu_family;  /**< Minimum CPU family (2=286, 3=386, etc.) */
    
    /* Metadata */
    char     name[MODULE_NAME_LENGTH];        /**< Module name (null-terminated) */
    char     description[MODULE_DESC_LENGTH]; /**< Description (null-terminated) */
    char     author[MODULE_AUTHOR_LENGTH];    /**< Author/organization */
    uint32_t build_timestamp;                 /**< Build timestamp (UNIX time) */
    
    /* Integrity and validation */
    uint16_t checksum;        /**< Module integrity checksum */
    
    /* Reserved for future expansion */
    uint16_t reserved[6];     /**< Reserved fields (must be zero) */
} module_header_t;

/* Ensure header is exactly 128 bytes for alignment */
_Static_assert(sizeof(module_header_t) == 128, "Module header must be 128 bytes");

/* ============================================================================
 * Module Dependency System
 * ============================================================================ */

/**
 * @brief Module dependency specification
 */
typedef struct {
    char     module_name[MODULE_NAME_LENGTH]; /**< Required module name */
    uint16_t min_version;                     /**< Minimum required version */
    uint16_t flags;                           /**< Dependency flags */
} module_dependency_t;

/**
 * @brief Dependency flags
 */
typedef enum {
    DEP_REQUIRED = 0x0001,  /**< Hard dependency (module fails without it) */
    DEP_OPTIONAL = 0x0002,  /**< Soft dependency (module adapts if missing) */
    DEP_CONFLICT = 0x0004   /**< Conflicting module (cannot coexist) */
} dependency_flags_t;

/* ============================================================================
 * Hardware Module Interface
 * ============================================================================ */

/**
 * @brief Hardware information structure
 */
typedef struct {
    uint16_t vendor_id;       /**< PCI vendor ID */
    uint16_t device_id;       /**< PCI device ID */
    uint16_t subsystem_id;    /**< PCI subsystem ID */
    uint16_t io_base;         /**< I/O base address */
    uint8_t  irq;             /**< IRQ number */
    uint8_t  bus_type;        /**< Bus type (ISA/PCI) */
    uint32_t memory_base;     /**< Memory base address (if applicable) */
    char     device_name[32]; /**< Human-readable device name */
} hardware_info_t;

/**
 * @brief NIC operation statistics
 */
typedef struct {
    uint32_t tx_packets;      /**< Transmitted packets */
    uint32_t rx_packets;      /**< Received packets */
    uint32_t tx_bytes;        /**< Transmitted bytes */
    uint32_t rx_bytes;        /**< Received bytes */
    uint32_t tx_errors;       /**< Transmission errors */
    uint32_t rx_errors;       /**< Reception errors */
    uint32_t collisions;      /**< Collision count */
    uint32_t dropped;         /**< Dropped packets */
} nic_stats_t;

/**
 * @brief NIC operational modes
 */
typedef enum {
    NIC_MODE_NORMAL      = 0x00,  /**< Normal operation */
    NIC_MODE_PROMISCUOUS = 0x01,  /**< Promiscuous mode */
    NIC_MODE_MULTICAST   = 0x02,  /**< Multicast mode */
    NIC_MODE_BROADCAST   = 0x04,  /**< Broadcast mode */
    NIC_MODE_LOOPBACK    = 0x08   /**< Loopback mode */
} nic_mode_t;

/**
 * @brief Link status information
 */
typedef struct {
    bool     link_up;         /**< Link is up */
    uint16_t speed_mbps;      /**< Link speed in Mbps */
    bool     full_duplex;     /**< Full duplex mode */
    bool     auto_negotiated; /**< Auto-negotiation used */
} link_status_t;

/**
 * @brief Packet buffer structure
 */
typedef struct {
    uint8_t* data;            /**< Packet data pointer */
    uint16_t length;          /**< Packet length */
    uint16_t buffer_size;     /**< Total buffer size */
    uint16_t type;            /**< Ethernet frame type */
    uint8_t  nic_id;          /**< Source/destination NIC ID */
    uint8_t  flags;           /**< Packet flags */
} packet_t;

/**
 * @brief Hardware module operations vtable
 * 
 * Hardware modules must implement this interface.
 * Function pointers may be NULL if operation is not supported.
 */
typedef struct {
    /* Core hardware operations */
    bool (*detect_hardware)(hardware_info_t* hw_info);
    bool (*initialize)(uint8_t nic_id, const hardware_info_t* hw_info);
    bool (*shutdown)(uint8_t nic_id);
    
    /* Packet operations */
    bool (*send_packet)(uint8_t nic_id, const packet_t* packet);
    packet_t* (*receive_packet)(uint8_t nic_id);
    
    /* Status and configuration */
    bool (*get_stats)(uint8_t nic_id, nic_stats_t* stats);
    bool (*reset_stats)(uint8_t nic_id);
    bool (*set_mode)(uint8_t nic_id, nic_mode_t mode);
    bool (*get_link_status)(uint8_t nic_id, link_status_t* status);
    
    /* Optional advanced operations */
    bool (*set_promiscuous)(uint8_t nic_id, bool enable);
    bool (*set_multicast)(uint8_t nic_id, const uint8_t* addr_list, uint16_t count);
    bool (*power_management)(uint8_t nic_id, bool sleep_mode);
    
    /* Diagnostics and testing */
    bool (*self_test)(uint8_t nic_id);
    bool (*loopback_test)(uint8_t nic_id);
    const char* (*get_driver_info)(void);
} nic_ops_t;

/* ============================================================================
 * Feature Module Interface
 * ============================================================================ */

/**
 * @brief Feature module configuration
 */
typedef struct {
    char     config_name[32]; /**< Configuration parameter name */
    uint32_t config_value;    /**< Configuration value */
    char     config_string[64]; /**< String configuration (if applicable) */
} feature_config_t;

/**
 * @brief API registration for feature modules
 */
typedef struct {
    const char* api_name;     /**< API function name */
    void*       api_function; /**< Function pointer */
} api_registration_t;

/* ============================================================================
 * Module Initialization Functions
 * ============================================================================ */

/**
 * @brief Hardware module initialization function signature
 * 
 * @param nic_id NIC identifier assigned by core
 * @param core Core services interface
 * @param hw_info Hardware information
 * @return Pointer to NIC operations vtable, NULL on failure
 */
typedef nic_ops_t* (*hardware_init_fn)(uint8_t nic_id, 
                                       core_services_t* core,
                                       const hardware_info_t* hw_info);

/**
 * @brief Feature module initialization function signature
 * 
 * @param core Core services interface
 * @param config Module configuration
 * @return true on success, false on failure
 */
typedef bool (*feature_init_fn)(core_services_t* core,
                                const module_config_t* config);

/**
 * @brief Module cleanup function signature
 */
typedef void (*module_cleanup_fn)(void);

/* ============================================================================
 * Module Information Structure
 * ============================================================================ */

/**
 * @brief Extended module information
 */
typedef struct {
    uint32_t memory_usage;        /**< Estimated memory usage in bytes */
    uint32_t initialization_time; /**< Initialization time in milliseconds */
    uint16_t supported_features;  /**< Supported feature flags */
    uint16_t hardware_requirements; /**< Hardware requirement flags */
    char     version_string[16];  /**< Human-readable version */
    char     build_info[32];      /**< Build information */
} module_info_t;

/* ============================================================================
 * Error Codes and Status
 * ============================================================================ */

/**
 * @brief Module operation result codes
 */
typedef enum {
    MODULE_SUCCESS           = 0,   /**< Operation successful */
    MODULE_ERROR_INVALID     = -1,  /**< Invalid parameter */
    MODULE_ERROR_NOT_FOUND   = -2,  /**< Module/resource not found */
    MODULE_ERROR_MEMORY      = -3,  /**< Memory allocation failure */
    MODULE_ERROR_HARDWARE    = -4,  /**< Hardware error */
    MODULE_ERROR_TIMEOUT     = -5,  /**< Operation timeout */
    MODULE_ERROR_BUSY        = -6,  /**< Resource busy */
    MODULE_ERROR_UNSUPPORTED = -7,  /**< Operation not supported */
    MODULE_ERROR_INIT        = -8,  /**< Initialization failure */
    MODULE_ERROR_DEPENDENCY  = -9,  /**< Dependency not met */
    MODULE_ERROR_VERSION     = -10, /**< Version incompatibility */
    MODULE_ERROR_CHECKSUM    = -11, /**< Checksum verification failed */
    MODULE_ERROR_CORRUPT     = -12  /**< Module file corrupted */
} module_result_t;

/* ============================================================================
 * Module Helper Macros
 * ============================================================================ */

/**
 * @brief Declare a module header with standard initialization
 */
#define DECLARE_MODULE_HEADER(name, class, family, features, init_fn) \
    const module_header_t module_header = { \
        .magic = MODULE_MAGIC, \
        .version = 0x0100, \
        .header_size = sizeof(module_header_t), \
        .module_size = 0, /* Filled by linker */ \
        .module_class = (class), \
        .family_id = (family), \
        .feature_flags = (features), \
        .api_version = MODULE_API_VERSION, \
        .init_offset = (uint16_t)(init_fn), \
        .vtable_offset = 0, /* Set by module if applicable */ \
        .cleanup_offset = 0, /* Set by module if applicable */ \
        .info_offset = 0, /* Set by module if applicable */ \
        .deps_count = 0, \
        .deps_offset = 0, \
        .min_dos_version = 0x0200, /* DOS 2.0+ */ \
        .min_cpu_family = 2, /* 286+ */ \
        .name = (name), \
        .description = "", /* Set by module */ \
        .author = "", /* Set by module */ \
        .build_timestamp = 0, /* Filled by build system */ \
        .checksum = 0, /* Calculated by build system */ \
        .reserved = {0} \
    }

/**
 * @brief Get module header from loaded module
 */
#define GET_MODULE_HEADER(module_ptr) ((const module_header_t*)(module_ptr))

/**
 * @brief Get function pointer from module
 */
#define GET_MODULE_FUNCTION(module_ptr, offset) \
    ((void*)((uint8_t*)(module_ptr) + (offset)))

/**
 * @brief Validate module header magic and version
 */
#define VALIDATE_MODULE_HEADER(header) \
    ((header)->magic == MODULE_MAGIC && \
     (header)->api_version <= MODULE_API_VERSION && \
     (header)->header_size == sizeof(module_header_t))

/* ============================================================================
 * Version and Compatibility
 * ============================================================================ */

/**
 * @brief Check API version compatibility
 * 
 * @param module_version Module's required API version
 * @return true if compatible, false otherwise
 */
static inline bool check_api_compatibility(uint16_t module_version) {
    uint8_t module_major = (module_version >> 8) & 0xFF;
    uint8_t core_major = (MODULE_API_VERSION >> 8) & 0xFF;
    
    /* Major version must match, minor version can be lower */
    return (module_major == core_major) && (module_version <= MODULE_API_VERSION);
}

/**
 * @brief Get human-readable version string
 * 
 * @param version Version in BCD format
 * @param buffer Buffer to store version string
 * @param size Buffer size
 */
static inline void format_version_string(uint16_t version, char* buffer, size_t size) {
    uint8_t major = (version >> 8) & 0xFF;
    uint8_t minor = version & 0xFF;
    snprintf(buffer, size, "%d.%d", major, minor);
}

#endif /* MODULE_API_H */