/**
 * @file driver_version.c
 * @brief Versioned Driver Interface Implementation
 * 
 * Implementation of version checking and compatibility validation
 * for driver operations structures.
 */

#include "../include/driver_version.h"
#include "../include/common.h"
#include "../include/logging.h"
#include "../include/hardware.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief Check driver version compatibility
 */
driver_compatibility_t driver_check_compatibility(
    const versioned_driver_ops_t *driver_ops,
    uint32_t required_version,
    uint32_t required_features
) {
    uint32_t driver_version;
    uint32_t driver_major, driver_minor;
    uint32_t required_major, required_minor;
    
    if (!driver_ops) {
        LOG_ERROR("Driver Version: NULL driver operations structure");
        return DRIVER_COMPAT_INCOMPATIBLE;
    }
    
    driver_version = driver_ops->interface_version;
    
    /* Extract version components */
    driver_major = DRIVER_VERSION_MAJOR(driver_version);
    driver_minor = DRIVER_VERSION_MINOR(driver_version);
    required_major = DRIVER_VERSION_MAJOR(required_version);
    required_minor = DRIVER_VERSION_MINOR(required_version);
    
    LOG_DEBUG("Driver Version: Checking compatibility - Driver %d.%d vs Required %d.%d",
              driver_major, driver_minor, required_major, required_minor);
    
    /* Major version compatibility check */
    if (driver_major < required_major) {
        LOG_ERROR("Driver Version: Driver major version %d < required %d", 
                  driver_major, required_major);
        return DRIVER_COMPAT_VERSION_TOO_OLD;
    }
    
    if (driver_major > required_major) {
        LOG_WARNING("Driver Version: Driver major version %d > required %d - may be incompatible", 
                    driver_major, required_major);
        return DRIVER_COMPAT_MAJOR_DIFF;
    }
    
    /* Minor version compatibility check (within same major) */
    if (driver_minor < required_minor) {
        LOG_WARNING("Driver Version: Driver minor version %d < required %d", 
                    driver_minor, required_minor);
        return DRIVER_COMPAT_MINOR_DIFF;
    }
    
    /* Check feature requirements */
    uint32_t missing_features = required_features & ~(driver_ops->features_supported);
    if (missing_features != 0) {
        LOG_ERROR("Driver Version: Missing required features: 0x%08lX", missing_features);
        return DRIVER_COMPAT_MISSING_FEATURES;
    }
    
    /* Check driver's version constraints */
    if (driver_ops->min_required_version != 0 && 
        required_version < driver_ops->min_required_version) {
        LOG_ERROR("Driver Version: Required version %08lX < driver minimum %08lX", 
                  required_version, driver_ops->min_required_version);
        return DRIVER_COMPAT_VERSION_TOO_OLD;
    }
    
    if (driver_ops->max_supported_version != 0 && 
        required_version > driver_ops->max_supported_version) {
        LOG_ERROR("Driver Version: Required version %08lX > driver maximum %08lX", 
                  required_version, driver_ops->max_supported_version);
        return DRIVER_COMPAT_VERSION_TOO_NEW;
    }
    
    LOG_INFO("Driver Version: Compatibility check passed - %s %d.%d", 
             driver_ops->driver_name, driver_major, driver_minor);
    
    return DRIVER_COMPAT_COMPATIBLE;
}

/**
 * @brief Validate driver operations structure
 */
int driver_validate_ops(const versioned_driver_ops_t *driver_ops) {
    if (!driver_ops) {
        LOG_ERROR("Driver Version: NULL driver operations structure");
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Check version is reasonable */
    if (driver_ops->interface_version == 0) {
        LOG_ERROR("Driver Version: Invalid interface version 0");
        return ERROR_INVALID_PARAMETER;
    }
    
    uint32_t major = DRIVER_VERSION_MAJOR(driver_ops->interface_version);
    if (major == 0 || major > 99) {
        LOG_ERROR("Driver Version: Invalid major version %d", major);
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Check required function pointers exist */
    if (!driver_ops->init_v1) {
        LOG_ERROR("Driver Version: Missing init_v1 function");
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!driver_ops->cleanup_v1) {
        LOG_ERROR("Driver Version: Missing cleanup_v1 function");
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!driver_ops->send_packet_v1) {
        LOG_ERROR("Driver Version: Missing send_packet_v1 function");
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!driver_ops->receive_packet_v1) {
        LOG_ERROR("Driver Version: Missing receive_packet_v1 function");
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!driver_ops->handle_interrupt_v1) {
        LOG_ERROR("Driver Version: Missing handle_interrupt_v1 function");
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Check driver name is reasonable */
    if (strnlen(driver_ops->driver_name, 16) == 0) {
        LOG_WARNING("Driver Version: Empty driver name");
    }
    
    /* Validate feature flags consistency */
    if ((driver_ops->features_required & ~driver_ops->features_supported) != 0) {
        LOG_ERROR("Driver Version: Driver requires features it doesn't support");
        return ERROR_INVALID_PARAMETER;
    }
    
    LOG_DEBUG("Driver Version: Validation passed - %s v%08lX", 
              driver_ops->driver_name, driver_ops->interface_version);
    
    return SUCCESS;
}

/**
 * @brief Get version compatibility string
 */
const char* driver_compatibility_string(driver_compatibility_t compatibility) {
    switch (compatibility) {
        case DRIVER_COMPAT_COMPATIBLE:
            return "Compatible";
        case DRIVER_COMPAT_MINOR_DIFF:
            return "Minor differences (should work)";
        case DRIVER_COMPAT_MAJOR_DIFF:
            return "Major differences (may work)";
        case DRIVER_COMPAT_INCOMPATIBLE:
            return "Incompatible";
        case DRIVER_COMPAT_VERSION_TOO_OLD:
            return "Driver version too old";
        case DRIVER_COMPAT_VERSION_TOO_NEW:
            return "Driver version too new";
        case DRIVER_COMPAT_MISSING_FEATURES:
            return "Missing required features";
        default:
            return "Unknown compatibility status";
    }
}

/**
 * @brief Format version number as string
 */
char* driver_format_version(uint32_t version, char *buffer) {
    uint32_t major, minor, patch;
    
    if (!buffer) {
        return NULL;
    }
    
    major = DRIVER_VERSION_MAJOR(version);
    minor = DRIVER_VERSION_MINOR(version);
    patch = DRIVER_VERSION_PATCH(version);
    
    sprintf(buffer, "%lu.%lu.%lu", major, minor, patch);
    
    return buffer;
}

/**
 * @brief Create versioned driver ops from legacy nic_ops
 */
int driver_create_versioned_ops(
    const void *legacy_ops,  /* nic_ops_t* */
    const char *driver_name,
    const char *vendor_name,
    versioned_driver_ops_t *versioned_ops
) {
    const nic_ops_t *nic_ops = (const nic_ops_t *)legacy_ops;
    
    if (!nic_ops || !versioned_ops) {
        return ERROR_INVALID_PARAMETER;
    }
    
    LOG_DEBUG("Driver Version: Creating versioned ops wrapper for %s", 
              driver_name ? driver_name : "unknown");
    
    /* Initialize versioned structure */
    memset(versioned_ops, 0, sizeof(versioned_driver_ops_t));
    
    /* Set version information */
    versioned_ops->interface_version = CURRENT_DRIVER_VERSION;
    versioned_ops->implementation_version = MAKE_DRIVER_VERSION(1, 0, 0); /* Legacy */
    
    /* Copy names */
    if (driver_name) {
        strncpy(versioned_ops->driver_name, driver_name, 15);
        versioned_ops->driver_name[15] = '\0';
    } else {
        strcpy(versioned_ops->driver_name, "legacy");
    }
    
    if (vendor_name) {
        strncpy(versioned_ops->vendor_name, vendor_name, 15);
        versioned_ops->vendor_name[15] = '\0';
    } else {
        strcpy(versioned_ops->vendor_name, "unknown");
    }
    
    /* Set compatibility constraints */
    versioned_ops->min_required_version = MAKE_DRIVER_VERSION(1, 0, 0);
    versioned_ops->max_supported_version = MAKE_DRIVER_VERSION(1, 9, 99);
    
    /* Set basic features */
    versioned_ops->features_supported = DRIVER_FEATURE_BASIC | DRIVER_FEATURE_STATISTICS;
    versioned_ops->features_required = DRIVER_FEATURE_BASIC;
    
    /* Map legacy functions to versioned interface */
    if (nic_ops->init) {
        versioned_ops->init_v1 = (int (*)(void*, void*))nic_ops->init;
    }
    
    if (nic_ops->cleanup) {
        versioned_ops->cleanup_v1 = (int (*)(void*))nic_ops->cleanup;
    }
    
    if (nic_ops->send_packet) {
        versioned_ops->send_packet_v1 = (int (*)(void*, const void far*, uint16_t))nic_ops->send_packet;
    }
    
    if (nic_ops->receive_packet) {
        versioned_ops->receive_packet_v1 = (int (*)(void*, void far*, uint16_t, uint16_t*))nic_ops->receive_packet;
    }
    
    if (nic_ops->handle_interrupt) {
        versioned_ops->handle_interrupt_v1 = (void far (*)(void*))nic_ops->handle_interrupt;
    }
    
    if (nic_ops->get_statistics) {
        versioned_ops->get_statistics_v1 = (int (*)(void*, void far*))nic_ops->get_statistics;
    }
    
    /* Validate the result */
    int result = driver_validate_ops(versioned_ops);
    if (result != SUCCESS) {
        LOG_ERROR("Driver Version: Failed to create valid versioned ops: %d", result);
        return result;
    }
    
    LOG_INFO("Driver Version: Created versioned wrapper for %s by %s", 
             versioned_ops->driver_name, versioned_ops->vendor_name);
    
    return SUCCESS;
}