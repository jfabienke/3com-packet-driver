# Module Development Guide - Vtable Pattern

**Updated**: 2025-08-19  
**Architecture**: Vtable Pattern with Module Interface

## Overview

This guide provides comprehensive instructions for developing modules for the 3Com Packet Driver using the **standardized vtable architecture**. The vtable-based system enables polymorphic hardware dispatch while supporting both current implementations and Phase 5 modular loading.

## Module Architecture

### Module Types

**Hardware Modules**: Family-based drivers that support complete NIC families
- **ETHRLINK3.MOD**: All 3C509 variants (3C509, 3C509B, 3C509C, etc.)
- **CORKSCREW.MOD**: All 3C515 variants (3C515-TX, etc.)
- **Future**: VORTEX.MOD, BOOMERANG.MOD, etc.

**Feature Modules**: Optional functionality that extends driver capabilities
- **ROUTING.MOD**: Multi-NIC routing capabilities
- **FLOWCTRL.MOD**: 802.3x flow control
- **STATS.MOD**: Advanced statistics collection
- **DIAG.MOD**: Diagnostic utilities (init-only)
- **PROMISC.MOD**: Promiscuous mode support

### Vtable-Based Architecture

All hardware modules must implement the standardized `nic_ops_t` vtable interface:

```c
// From include/hardware.h
typedef struct nic_ops {
    /* Core lifecycle */
    int (*init)(struct nic_info *nic);
    int (*cleanup)(struct nic_info *nic);
    int (*reset)(struct nic_info *nic);
    int (*self_test)(struct nic_info *nic);
    
    /* Packet operations - CRITICAL PATH */
    int (*send_packet)(struct nic_info *nic, const uint8_t *packet, size_t len);
    int (*receive_packet)(struct nic_info *nic, uint8_t *buffer, size_t *len);
    int (*check_tx_complete)(struct nic_info *nic);
    int (*check_rx_available)(struct nic_info *nic);
    
    /* Interrupt handling */
    void (*handle_interrupt)(struct nic_info *nic);
    int (*check_interrupt)(struct nic_info *nic);
    int (*enable_interrupts)(struct nic_info *nic);
    int (*disable_interrupts)(struct nic_info *nic);
    
    /* Configuration */
    int (*set_receive_mode)(struct nic_info *nic, uint16_t mode);
    // ... additional operations
} nic_ops_t;
```

### Unified .MOD Extension with Vtable Export

All modules use the `.MOD` extension with **vtable_offset** pointing to their `nic_ops_t` implementation. The core loader uses this for polymorphic dispatch.

## Module Header Specification

### Header Structure

Every module must begin with a standardized header:

```c
#include "module_types.h"

typedef struct {
    // Module identification
    uint16_t magic;           // 'MD' (0x4D44) - Module identifier
    uint16_t version;         // Module version (BCD format: 0x0100 = v1.0)
    uint16_t header_size;     // Header size in bytes (sizeof(module_header_t))
    uint16_t module_size;     // Module size in paragraphs (16-byte units)
    
    // Module classification
    uint16_t module_class;    // MODULE_CLASS_HARDWARE or MODULE_CLASS_FEATURE
    uint16_t family_id;       // For hardware: FAMILY_ETHERLINK3, FAMILY_CORKSCREW
    uint16_t feature_flags;   // Capability flags (see feature_flags_t)
    
    // Entry points
    uint16_t init_offset;     // Offset to initialization function
    uint16_t vtable_offset;   // Offset to nic_ops_t vtable (CRITICAL for hardware modules)
    uint16_t cleanup_offset;  // Offset to cleanup function (optional)
    
    // Dependencies and requirements
    uint16_t deps_count;      // Number of required dependencies
    uint16_t deps_offset;     // Offset to dependency list
    uint16_t min_dos_version; // Minimum DOS version (0x0300 = DOS 3.0)
    uint16_t min_cpu_family;  // Minimum CPU (2=286, 3=386, 4=486, etc.)
    
    // Metadata
    char     name[12];        // Module name (e.g., "ETHRLINK3")
    char     description[32]; // Human-readable description
    char     author[16];      // Module author/organization
    uint16_t checksum;        // Module integrity verification
    
    // Reserved for future expansion
    uint16_t reserved[8];
} module_header_t;
```

### Module Constants

```c
// Module magic number
#define MODULE_MAGIC 0x4D44  // 'MD'

// Module classes
typedef enum {
    MODULE_CLASS_HARDWARE = 0x0001,  // Hardware driver module
    MODULE_CLASS_FEATURE  = 0x0002,  // Optional feature module
    MODULE_CLASS_PROTOCOL = 0x0004   // Future: Protocol stack modules
} module_class_t;

// NIC family identifiers
typedef enum {
    FAMILY_ETHERLINK3 = 0x0509,  // All 3C509 variants
    FAMILY_CORKSCREW  = 0x0515,  // All 3C515 variants
    FAMILY_VORTEX     = 0x0590,  // Future: 3C590/3C595
    FAMILY_BOOMERANG  = 0x0900,  // Future: 3C900 series
    FAMILY_HURRICANE  = 0x0905   // Future: 3C905 series
} nic_family_t;

// Feature capability flags
typedef enum {
    FEATURE_ROUTING      = 0x0001,  // Supports routing
    FEATURE_FLOW_CONTROL = 0x0002,  // Supports flow control
    FEATURE_STATISTICS   = 0x0004,  // Provides statistics
    FEATURE_PROMISCUOUS  = 0x0008,  // Supports promiscuous mode
    FEATURE_DIAGNOSTICS  = 0x0010,  // Diagnostic capabilities
    FEATURE_INIT_ONLY    = 0x8000   // Init-only module (discarded)
} feature_flags_t;
```

## Hardware Module Development

### Hardware Module Template

```c
// hardware_module_template.c - Template for hardware modules
#include "module_api.h"
#include "nic_ops.h"

// Module header (must be first in file)
const module_header_t module_header = {
    .magic = MODULE_MAGIC,
    .version = 0x0100,           // Version 1.0
    .header_size = sizeof(module_header_t),
    .module_size = 0,            // Filled by linker
    .module_class = MODULE_CLASS_HARDWARE,
    .family_id = FAMILY_ETHERLINK3,  // Change for your family
    .feature_flags = 0,
    .init_offset = (uint16_t)module_init,
    .vtable_offset = (uint16_t)&hardware_vtable,
    .cleanup_offset = (uint16_t)module_cleanup,
    .deps_count = 0,
    .deps_offset = 0,
    .min_dos_version = 0x0200,   // DOS 2.0+
    .min_cpu_family = 2,         // 286+
    .name = "ETHRLINK3",
    .description = "EtherLink III Family Driver",
    .author = "3Com Driver Team",
    .checksum = 0                // Calculated by build system
};

// Hardware operations vtable
static const nic_ops_t hardware_vtable = {
    .detect_hardware = etherlink3_detect,
    .initialize = etherlink3_initialize,
    .send_packet = etherlink3_send,
    .receive_packet = etherlink3_receive,
    .get_stats = etherlink3_get_stats,
    .set_mode = etherlink3_set_mode,
    .cleanup = etherlink3_cleanup
};

// Module initialization function
nic_ops_t* module_init(uint8_t nic_id, core_services_t* core, hardware_info_t* hw_info) {
    // Initialize family-specific code
    if (!etherlink3_family_init(nic_id, core, hw_info)) {
        return NULL;
    }
    
    // Return vtable for core to use
    return (nic_ops_t*)&hardware_vtable;
}

// Module cleanup function (optional)
void module_cleanup(void) {
    etherlink3_family_cleanup();
}

// Family detection function
bool etherlink3_detect(hardware_info_t* hw_info) {
    // Detect any 3C509 family member
    if (hw_info->vendor_id == 0x10B7) {
        uint16_t device_id = hw_info->device_id;
        
        // Check for 3C509 family (0x50xx series)
        if ((device_id & 0xFF00) == 0x5000) {
            switch (device_id & 0x00FF) {
                case 0x90:  // 3C509
                case 0x91:  // 3C509B
                case 0x92:  // 3C509C
                    // Add variant-specific detection
                    return detect_specific_variant(device_id, hw_info);
                default:
                    return false;
            }
        }
    }
    return false;
}

// Family initialization
bool etherlink3_family_init(uint8_t nic_id, core_services_t* core, hardware_info_t* hw_info) {
    // Initialize common family components
    if (!init_eeprom_interface(hw_info)) {
        return false;
    }
    
    if (!init_media_detection(hw_info)) {
        return false;
    }
    
    // Initialize variant-specific features
    switch (hw_info->device_id & 0x00FF) {
        case 0x90:  // 3C509
            return init_3c509_specific(nic_id, core, hw_info);
        case 0x91:  // 3C509B
            return init_3c509b_specific(nic_id, core, hw_info);
        case 0x92:  // 3C509C
            return init_3c509c_specific(nic_id, core, hw_info);
        default:
            return false;
    }
}
```

### Hardware Module API

Hardware modules must implement the `nic_ops_t` interface:

```c
typedef struct {
    // Core hardware operations
    bool (*detect_hardware)(hardware_info_t* hw_info);
    bool (*initialize)(uint8_t nic_id, nic_config_t* config);
    bool (*send_packet)(uint8_t nic_id, packet_t* packet);
    packet_t* (*receive_packet)(uint8_t nic_id);
    
    // Management operations
    bool (*get_stats)(uint8_t nic_id, nic_stats_t* stats);
    bool (*set_mode)(uint8_t nic_id, nic_mode_t mode);
    void (*cleanup)(uint8_t nic_id);
    
    // Optional operations (can be NULL)
    bool (*set_promiscuous)(uint8_t nic_id, bool enable);
    bool (*set_multicast)(uint8_t nic_id, uint8_t* addr_list, uint16_t count);
    bool (*get_link_status)(uint8_t nic_id, link_status_t* status);
} nic_ops_t;
```

### Family Coverage Guidelines

When developing hardware modules, ensure complete family coverage:

1. **Identify All Variants**: Research all devices in the family
2. **Common Code Extraction**: Identify shared functionality
3. **Variant-Specific Handling**: Handle differences between models
4. **Future Compatibility**: Design for unknown future variants

Example family coverage for ETHRLINK3.MOD:
```c
// Complete 3C509 family support matrix
static const family_variant_t etherlink3_variants[] = {
    { 0x5090, "3C509",     MEDIA_TP | MEDIA_COAX | MEDIA_AUI },
    { 0x5091, "3C509B",    MEDIA_TP | MEDIA_COAX | MEDIA_AUI },
    { 0x5092, "3C509C",    MEDIA_TP | MEDIA_COAX | MEDIA_AUI },
    { 0x5093, "3C509-TP",  MEDIA_TP },
    { 0x5094, "3C509-TPC", MEDIA_TP | MEDIA_COAX },
    { 0x5095, "3C509-TPO", MEDIA_TP },
    // Add new variants here as discovered
    { 0x0000, NULL,        0 }  // Terminator
};
```

## Feature Module Development

### Feature Module Template

```c
// feature_module_template.c - Template for feature modules
#include "module_api.h"

// Module header
const module_header_t module_header = {
    .magic = MODULE_MAGIC,
    .version = 0x0100,
    .header_size = sizeof(module_header_t),
    .module_size = 0,            // Filled by linker
    .module_class = MODULE_CLASS_FEATURE,
    .family_id = 0,              // Not applicable for features
    .feature_flags = FEATURE_ROUTING,  // Change for your feature
    .init_offset = (uint16_t)feature_init,
    .vtable_offset = 0,          // Features don't have vtables
    .cleanup_offset = (uint16_t)feature_cleanup,
    .deps_count = 0,
    .deps_offset = 0,
    .min_dos_version = 0x0200,
    .min_cpu_family = 2,
    .name = "ROUTING",
    .description = "Multi-NIC Routing Engine",
    .author = "3Com Driver Team",
    .checksum = 0
};

// Feature initialization function
bool feature_init(core_services_t* core, module_config_t* config) {
    // Initialize feature-specific functionality
    if (!routing_engine_init(core, config)) {
        return false;
    }
    
    // Register feature APIs with core
    if (!register_routing_apis(core)) {
        routing_engine_cleanup();
        return false;
    }
    
    return true;
}

// Feature cleanup function
void feature_cleanup(void) {
    routing_engine_cleanup();
}
```

### Feature Module API Registration

Feature modules register their APIs with the core loader:

```c
// API registration for routing module
bool register_routing_apis(core_services_t* core) {
    api_registration_t routing_apis[] = {
        { "add_route",    FUNC_PTR(add_static_route) },
        { "del_route",    FUNC_PTR(delete_static_route) },
        { "route_packet", FUNC_PTR(route_packet_to_nic) },
        { "get_routes",   FUNC_PTR(get_routing_table) },
        { NULL, NULL }  // Terminator
    };
    
    return core->register_feature_apis("ROUTING", routing_apis);
}
```

## Build System Integration

### Module Makefile Template

```makefile
# Module makefile template
MODULE_NAME = ETHRLINK3
MODULE_TYPE = hardware

# Source files for this module
SOURCES = etherlink3_main.c \
          etherlink3_eeprom.c \
          etherlink3_media.c \
          etherlink3_3c509.c \
          etherlink3_3c509b.c \
          etherlink3_3c509c.c

# Assembly files
ASM_SOURCES = etherlink3_pio.asm

# Object files
OBJECTS = $(SOURCES:.c=.obj) $(ASM_SOURCES:.asm=.obj)

# Module-specific flags
MODULE_CFLAGS = -DMODULE_NAME=\"$(MODULE_NAME)\" \
                -DMODULE_TYPE_$(shell echo $(MODULE_TYPE) | tr a-z A-Z)

# Build rules
$(MODULE_NAME).MOD: $(OBJECTS) module.ld
	$(LINKER) -T module.ld -o $@ $(OBJECTS)
	$(MODULE_VERIFY) $@
	$(MODULE_CHECKSUM) $@

%.obj: %.c
	$(CC) $(CFLAGS) $(MODULE_CFLAGS) -c $< -o $@

%.obj: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

clean:
	del *.obj
	del $(MODULE_NAME).MOD

.PHONY: clean
```

### Module Linker Script

```ld
/* module.ld - Linker script for modules */
MEMORY {
    MODULE : ORIGIN = 0x0000, LENGTH = 64K
}

SECTIONS {
    .header : {
        *(.header)
        KEEP(*(.header))
    } > MODULE
    
    .text : {
        *(.text)
        *(.text.*)
    } > MODULE
    
    .data : {
        *(.data)
        *(.data.*)
    } > MODULE
    
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } > MODULE
    
    .bss : {
        *(.bss)
        *(.bss.*)
    } > MODULE
    
    /* Calculate module size */
    __module_end = .;
    __module_size = (__module_end - 0x0000) / 16;  /* In paragraphs */
}
```

## Module Testing

### Test Framework Integration

```c
// Module test template
#include "module_test.h"

// Test suite for hardware module
test_suite_t etherlink3_tests = {
    .module_name = "ETHRLINK3",
    .tests = {
        { "Module Loading",     test_module_load },
        { "Hardware Detection", test_hardware_detection },
        { "Family Coverage",    test_family_coverage },
        { "Initialization",     test_initialization },
        { "Data Transfer",      test_data_transfer },
        { "Error Handling",     test_error_handling },
        { "Module Unloading",   test_module_unload },
        { NULL, NULL }
    }
};

bool test_module_load(void) {
    // Test module loading and header validation
    module_header_t* header = load_test_module("ETHRLINK3.MOD");
    
    TEST_ASSERT(header != NULL, "Module should load successfully");
    TEST_ASSERT(header->magic == MODULE_MAGIC, "Magic number should be correct");
    TEST_ASSERT(header->module_class == MODULE_CLASS_HARDWARE, "Should be hardware module");
    TEST_ASSERT(header->family_id == FAMILY_ETHERLINK3, "Should be EtherLink III family");
    
    return true;
}

bool test_family_coverage(void) {
    // Test that module covers all known family variants
    hardware_info_t test_devices[] = {
        { 0x10B7, 0x5090, "3C509" },    // Original 3C509
        { 0x10B7, 0x5091, "3C509B" },   // Enhanced 3C509B
        { 0x10B7, 0x5092, "3C509C" },   // Latest 3C509C
        // Add more variants...
    };
    
    for (size_t i = 0; i < ARRAY_SIZE(test_devices); i++) {
        bool detected = etherlink3_detect(&test_devices[i]);
        TEST_ASSERT(detected, "Should detect %s", test_devices[i].name);
    }
    
    return true;
}
```

### Module Validation Tools

```bash
#!/bin/bash
# validate_module.sh - Module validation script

MODULE_FILE="$1"

echo "Validating module: $MODULE_FILE"

# Check file exists
if [ ! -f "$MODULE_FILE" ]; then
    echo "ERROR: Module file not found"
    exit 1
fi

# Check file extension
if [[ "$MODULE_FILE" != *.MOD ]]; then
    echo "ERROR: Module must have .MOD extension"
    exit 1
fi

# Validate module header
./tools/check_module_header "$MODULE_FILE"
if [ $? -ne 0 ]; then
    echo "ERROR: Invalid module header"
    exit 1
fi

# Verify checksum
./tools/verify_checksum "$MODULE_FILE"
if [ $? -ne 0 ]; then
    echo "ERROR: Checksum verification failed"
    exit 1
fi

# Check module size
SIZE=$(stat -c%s "$MODULE_FILE")
MAX_SIZE=$((64 * 1024))  # 64KB maximum
if [ $SIZE -gt $MAX_SIZE ]; then
    echo "ERROR: Module too large ($SIZE bytes, max $MAX_SIZE)"
    exit 1
fi

echo "Module validation successful"
exit 0
```

## Core Services API

Modules have access to core services through the `core_services_t` interface:

```c
typedef struct {
    // Memory management
    void* (*allocate_memory)(size_t size, memory_type_t type);
    void (*free_memory)(void* ptr);
    
    // Buffer management
    packet_buffer_t* (*get_tx_buffer)(size_t size);
    packet_buffer_t* (*get_rx_buffer)(size_t size);
    void (*return_buffer)(packet_buffer_t* buffer);
    
    // Cache management (Phase 4 integration)
    void (*cache_flush_for_dma)(void* buffer, size_t size);
    void (*cache_invalidate_after_dma)(void* buffer, size_t size);
    
    // Hardware access
    uint8_t (*inb)(uint16_t port);
    uint16_t (*inw)(uint16_t port);
    uint32_t (*inl)(uint16_t port);
    void (*outb)(uint16_t port, uint8_t value);
    void (*outw)(uint16_t port, uint16_t value);
    void (*outl)(uint16_t port, uint32_t value);
    
    // Interrupt management
    bool (*install_interrupt)(uint8_t irq, interrupt_handler_t handler);
    void (*remove_interrupt)(uint8_t irq);
    
    // Logging and diagnostics
    void (*log_message)(log_level_t level, const char* format, ...);
    void (*record_error)(error_type_t type, const char* description);
    
    // Feature registration (for feature modules)
    bool (*register_feature_apis)(const char* feature_name, api_registration_t* apis);
    void (*unregister_feature_apis)(const char* feature_name);
} core_services_t;
```

## Best Practices

### Performance Guidelines

1. **Minimize Memory Usage**: Every byte counts in DOS
2. **Optimize Critical Paths**: Packet send/receive should be as fast as possible
3. **Use CPU-Specific Features**: Take advantage of available CPU capabilities
4. **Cache-Friendly Code**: Consider cache coherency in DMA operations

### Compatibility Guidelines

1. **DOS Version Support**: Support DOS 2.0+ unless specific features require newer versions
2. **CPU Compatibility**: Gracefully handle older CPUs (286+ recommended)
3. **Memory Manager Compatibility**: Work with EMM386, QEMM386, etc.
4. **Hardware Variations**: Handle subtle hardware differences within families

### Code Style Guidelines

1. **Consistent Naming**: Follow existing naming conventions
2. **Error Handling**: Always check return values and handle errors gracefully
3. **Documentation**: Comment complex algorithms and hardware interactions
4. **Testing**: Provide comprehensive test coverage

## Module Distribution

### Packaging Guidelines

1. **Module Integrity**: All modules must pass checksum verification
2. **Documentation**: Include module-specific documentation
3. **Version Compatibility**: Clearly specify core version requirements
4. **Testing**: Provide test reports for supported hardware

### Installation Integration

```c
// Module installation helper
bool install_module(const char* module_path, const char* install_dir) {
    char dest_path[256];
    
    // Validate module before installation
    if (!validate_module_file(module_path)) {
        return false;
    }
    
    // Copy to installation directory
    sprintf(dest_path, "%s\\%s", install_dir, get_filename(module_path));
    if (!copy_file(module_path, dest_path)) {
        return false;
    }
    
    // Update module registry
    if (!register_installed_module(dest_path)) {
        delete_file(dest_path);
        return false;
    }
    
    return true;
}
```

## Conclusion

The modular architecture enables unprecedented extensibility for DOS network drivers. By following these guidelines, developers can create hardware modules that support entire NIC families and feature modules that provide sophisticated networking capabilities.

The unified .MOD extension and standardized APIs ensure that modules integrate seamlessly with the core loader while maintaining the flexibility to implement family-specific optimizations and unique features.

This modular approach represents a revolutionary advancement in DOS driver architecture, enabling memory-efficient, extensible, and maintainable network drivers that can adapt to diverse hardware configurations and user requirements.