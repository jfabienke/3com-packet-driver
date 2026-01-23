/**
 * @file driver_version.h
 * @brief Versioned Driver Interface Definitions
 * 
 * Provides version checking and compatibility validation between
 * modules and driver implementations. This ensures ABI stability
 * and prevents incompatible driver/module combinations.
 * 
 * ARCHITECTURE: Semantic versioning with compatibility matrix
 * - MAJOR: Breaking changes, no backward compatibility
 * - MINOR: New features, backward compatible
 * - PATCH: Bug fixes, fully compatible
 */

#ifndef DRIVER_VERSION_H
#define DRIVER_VERSION_H

#include <stdint.h>

/* Current driver interface version */
#define DRIVER_INTERFACE_VERSION_MAJOR      1
#define DRIVER_INTERFACE_VERSION_MINOR      2
#define DRIVER_INTERFACE_VERSION_PATCH      0

/* Pack version into single 32-bit value: MAJOR.MINOR.PATCH.RESERVED */
#define MAKE_DRIVER_VERSION(major, minor, patch) \
    (((uint32_t)(major) << 24) | ((uint32_t)(minor) << 16) | ((uint32_t)(patch) << 8))

#define CURRENT_DRIVER_VERSION \
    MAKE_DRIVER_VERSION(DRIVER_INTERFACE_VERSION_MAJOR, \
                       DRIVER_INTERFACE_VERSION_MINOR, \
                       DRIVER_INTERFACE_VERSION_PATCH)

/* Extract version components */
#define DRIVER_VERSION_MAJOR(version)   (((version) >> 24) & 0xFF)
#define DRIVER_VERSION_MINOR(version)   (((version) >> 16) & 0xFF) 
#define DRIVER_VERSION_PATCH(version)   (((version) >> 8) & 0xFF)

/**
 * @brief Versioned driver operations structure
 * 
 * This extends the basic nic_ops_t with version information
 * and compatibility checking.
 */
typedef struct {
    /* Version Information */
    uint32_t interface_version;         /* Driver interface version */
    uint32_t implementation_version;    /* Driver implementation version */
    char driver_name[16];               /* Driver name (null-terminated) */
    char vendor_name[16];               /* Vendor name (null-terminated) */
    
    /* Compatibility Information */
    uint32_t min_required_version;      /* Minimum compatible version */
    uint32_t max_supported_version;     /* Maximum compatible version */
    uint32_t features_supported;        /* Feature flags */
    uint32_t features_required;         /* Required feature flags */
    
    /* Function Pointers - versioned interface */
    int (*init_v1)(void *context, void *config);
    int (*cleanup_v1)(void *context);
    int (*send_packet_v1)(void *context, const void far *data, uint16_t length);
    int (*receive_packet_v1)(void *context, void far *buffer, uint16_t buffer_size, uint16_t *received);
    void far (*handle_interrupt_v1)(void *context);
    int (*get_statistics_v1)(void *context, void far *stats);
    
    /* Version 1.2+ extensions */
    int (*validate_config_v12)(void *context, void *config);
    int (*get_capabilities_v12)(void *context, uint32_t *capabilities);
    int (*set_power_state_v12)(void *context, uint8_t power_state);
    
    /* Future extension placeholder */
    void *reserved[8];                  /* Reserved for future versions */
    
} versioned_driver_ops_t;

/* Driver Feature Flags */
#define DRIVER_FEATURE_BASIC            BIT(0)  /* Basic packet I/O */
#define DRIVER_FEATURE_DMA              BIT(1)  /* DMA support */
#define DRIVER_FEATURE_BUS_MASTER       BIT(2)  /* Bus mastering */
#define DRIVER_FEATURE_CHECKSUM_OFFLOAD BIT(3)  /* Hardware checksum */
#define DRIVER_FEATURE_POWER_MGMT       BIT(4)  /* Power management */
#define DRIVER_FEATURE_WAKE_ON_LAN      BIT(5)  /* Wake-on-LAN */
#define DRIVER_FEATURE_VLAN             BIT(6)  /* VLAN support */
#define DRIVER_FEATURE_STATISTICS       BIT(7)  /* Statistics collection */

/* Compatibility check results */
typedef enum {
    DRIVER_COMPAT_COMPATIBLE = 0,       /* Fully compatible */
    DRIVER_COMPAT_MINOR_DIFF = 1,       /* Minor differences, should work */
    DRIVER_COMPAT_MAJOR_DIFF = 2,       /* Major differences, may work */
    DRIVER_COMPAT_INCOMPATIBLE = -1,    /* Incompatible, will not work */
    DRIVER_COMPAT_VERSION_TOO_OLD = -2, /* Driver too old */
    DRIVER_COMPAT_VERSION_TOO_NEW = -3, /* Driver too new */
    DRIVER_COMPAT_MISSING_FEATURES = -4 /* Required features missing */
} driver_compatibility_t;

/* Version checking functions */

/**
 * @brief Check driver version compatibility
 * 
 * @param driver_ops Driver operations structure
 * @param required_version Minimum required version
 * @param required_features Required feature flags
 * @return Compatibility result
 */
driver_compatibility_t driver_check_compatibility(
    const versioned_driver_ops_t *driver_ops,
    uint32_t required_version,
    uint32_t required_features
);

/**
 * @brief Validate driver operations structure
 * 
 * Performs basic validation of driver operations structure
 * to ensure it's properly initialized.
 * 
 * @param driver_ops Driver operations structure
 * @return SUCCESS if valid, negative error code otherwise
 */
int driver_validate_ops(const versioned_driver_ops_t *driver_ops);

/**
 * @brief Get version compatibility string
 * 
 * Returns human-readable description of compatibility status.
 * 
 * @param compatibility Compatibility result
 * @return Static string describing compatibility
 */
const char* driver_compatibility_string(driver_compatibility_t compatibility);

/**
 * @brief Format version number as string
 * 
 * @param version Version number
 * @param buffer Buffer to store formatted string (minimum 16 bytes)
 * @return Formatted version string
 */
char* driver_format_version(uint32_t version, char *buffer);

/**
 * @brief Create versioned driver ops from legacy nic_ops
 * 
 * Wraps legacy nic_ops_t structure in versioned interface
 * for backward compatibility.
 * 
 * @param legacy_ops Legacy nic operations
 * @param driver_name Driver name
 * @param vendor_name Vendor name
 * @param versioned_ops Output versioned operations structure
 * @return SUCCESS on success, negative error code on failure
 */
int driver_create_versioned_ops(
    const void *legacy_ops,  /* nic_ops_t* */
    const char *driver_name,
    const char *vendor_name,
    versioned_driver_ops_t *versioned_ops
);

/* Convenience macros */

/**
 * @brief Check if driver supports specific feature
 */
#define DRIVER_SUPPORTS_FEATURE(ops, feature) \
    (((ops)->features_supported & (feature)) != 0)

/**
 * @brief Check if driver requires specific feature
 */
#define DRIVER_REQUIRES_FEATURE(ops, feature) \
    (((ops)->features_required & (feature)) != 0)

/**
 * @brief Check if driver version is at least minimum version
 */
#define DRIVER_VERSION_AT_LEAST(ops, major, minor, patch) \
    ((ops)->interface_version >= MAKE_DRIVER_VERSION(major, minor, patch))

/**
 * @brief Standard driver validation
 */
#define VALIDATE_DRIVER_OPS(ops) do { \
    int _result = driver_validate_ops(ops); \
    if (_result != SUCCESS) return _result; \
} while(0)

#endif /* DRIVER_VERSION_H */