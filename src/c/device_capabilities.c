/**
 * @file device_capabilities.c
 * @brief 3Com Device Capability Definitions
 *
 * GPT-5 Enhanced Design: Device capability descriptors drive all DMA decisions
 * instead of hard-coded strategies. This provides unified, device-aware
 * buffer allocation and DMA safety across all 3Com NIC generations.
 *
 * 3Com Packet Driver - Device Capability Framework
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/dma_safety.h"
#include <string.h>

/* ============================================================================
 * 3Com Device Capability Definitions
 * ============================================================================ */

/**
 * 3C509B EtherLink III - ISA, PIO only
 * - No bus mastering capability
 * - 16-bit ISA addressing
 * - No scatter-gather support
 * - Minimal DMA requirements (PIO transfers)
 */
const device_caps_t caps_3c509b = {
    .dma_addr_bits = 24,                    /* ISA 24-bit addressing */
    .max_sg_entries = 1,                    /* No scatter-gather */
    .sg_boundary = 65536,                   /* 64KB boundary (ISA requirement) */
    .alignment = 4,                         /* 4-byte alignment for ISA */
    .descriptor_alignment = 4,              /* Minimal descriptor alignment */
    .needs_vds = false,                     /* PIO doesn't need VDS */
    .rx_copybreak = 256,                    /* Small copybreak for PIO */
    .tx_copybreak = 256,                    /* Small copybreak for PIO */
    .cache_coherent = true,                 /* PIO is cache coherent */
    .supports_sg = false,                   /* No scatter-gather */
    .no_64k_cross = false,                  /* PIO doesn't have DMA constraints */
    .max_segment_size = 1536,               /* MTU-based for PIO */
    .device_name = "3C509B"
};

/**
 * 3C515-TX Fast EtherLink - ISA bus master, 24-bit DMA
 * - ISA bus mastering with 24-bit addressing limit
 * - Requires careful 16MB boundary management
 * - May need VDS in V86 environments
 * - Proven bus master testing integration
 */
const device_caps_t caps_3c515tx = {
    .dma_addr_bits = 24,                    /* ISA 24-bit limit (<16MB) */
    .max_sg_entries = 1,                    /* Single fragment per descriptor */
    .sg_boundary = 65536,                   /* 64KB boundary critical for ISA */
    .alignment = 16,                        /* 16-byte alignment for DMA */
    .descriptor_alignment = 16,             /* Descriptor ring alignment */
    .needs_vds = true,                      /* Often needs VDS for ISA bus mastering */
    .rx_copybreak = 512,                    /* Balanced for 10/100 operation */
    .tx_copybreak = 512,                    /* Balanced for 10/100 operation */
    .cache_coherent = false,                /* ISA bus mastering needs cache mgmt */
    .supports_sg = false,                   /* Single fragment DMA */
    /* GPT-5 Critical: ISA bus master cannot cross 64KB boundaries */
    .no_64k_cross = true,                   /* ISA DMA controller limitation */
    .max_segment_size = 65536,              /* Max 64KB per DMA segment */
    .device_name = "3C515-TX"
};

/**
 * 3C589 - PCMCIA version of 3C509B
 * - Similar to 3C509B but with PCMCIA constraints
 * - PIO only, no bus mastering
 * - May have different power management needs
 */
const device_caps_t caps_3c589 = {
    .dma_addr_bits = 24,                    /* PCMCIA addressing */
    .max_sg_entries = 1,                    /* No scatter-gather */
    .sg_boundary = 65536,                   /* 64KB boundary */
    .alignment = 4,                         /* 4-byte alignment */
    .descriptor_alignment = 4,              /* Minimal alignment */
    .needs_vds = false,                     /* PIO operation */
    .rx_copybreak = 256,                    /* Conservative for PCMCIA */
    .tx_copybreak = 256,                    /* Conservative for PCMCIA */
    .cache_coherent = true,                 /* PIO is coherent */
    .supports_sg = false,                   /* No scatter-gather */
    .no_64k_cross = false,                  /* PIO doesn't have DMA constraints */
    .max_segment_size = 1536,               /* MTU-based for PIO */
    .device_name = "3C589"
};

/**
 * 3C590 Vortex - Early PCI, 32-bit DMA
 * - PCI bus mastering with full 32-bit addressing
 * - Basic Vortex architecture
 * - No advanced features like hardware checksum
 */
const device_caps_t caps_3c590 = {
    .dma_addr_bits = 32,                    /* Full 32-bit PCI addressing */
    .max_sg_entries = 1,                    /* Single fragment per descriptor */
    .sg_boundary = 65536,                   /* Conservative 64KB boundary */
    .alignment = 16,                        /* PCI cache line alignment */
    .descriptor_alignment = 16,             /* Descriptor alignment */
    .needs_vds = false,                     /* PCI usually doesn't need VDS */
    .rx_copybreak = 736,                    /* GPT-5: Better than 512 for Vortex */
    .tx_copybreak = 736,                    /* GPT-5: Better than 512 for Vortex */
    .cache_coherent = false,                /* PCI bus mastering needs cache mgmt */
    .supports_sg = false,                   /* Basic single fragment */
    .no_64k_cross = false,                  /* PCI doesn't have 64KB constraint */
    .max_segment_size = 65536,              /* Conservative for early PCI */
    .device_name = "3C590"
};

/**
 * 3C595 Vortex - 100Mbps version
 * - Enhanced Vortex with 100Mbps capability
 * - Similar to 3C590 but optimized for higher throughput
 * - May benefit from larger buffers at 100Mbps
 */
const device_caps_t caps_3c595 = {
    .dma_addr_bits = 32,                    /* Full 32-bit PCI */
    .max_sg_entries = 1,                    /* Single fragment */
    .sg_boundary = 65536,                   /* 64KB boundary */
    .alignment = 16,                        /* PCI alignment */
    .descriptor_alignment = 16,             /* Descriptor alignment */
    .needs_vds = false,                     /* PCI operation */
    .rx_copybreak = 1024,                   /* Larger for 100Mbps */
    .tx_copybreak = 1024,                   /* Larger for 100Mbps */
    .cache_coherent = false,                /* Needs cache management */
    .supports_sg = false,                   /* Single fragment */
    .no_64k_cross = false,                  /* PCI doesn't have 64KB constraint */
    .max_segment_size = 65536,              /* Conservative for 100Mbps */
    .device_name = "3C595"
};

/**
 * 3C900 Boomerang TPO - PCI with enhanced DMA
 * - Boomerang architecture with scatter-gather support
 * - Multiple fragments per descriptor
 * - Enhanced performance capabilities
 */
const device_caps_t caps_3c900 = {
    .dma_addr_bits = 32,                    /* Full 32-bit PCI */
    .max_sg_entries = 4,                    /* Multiple fragments supported */
    .sg_boundary = 65536,                   /* 64KB boundary handling */
    .alignment = 16,                        /* Cache line alignment */
    .descriptor_alignment = 16,             /* Descriptor alignment */
    .needs_vds = false,                     /* PCI operation */
    .rx_copybreak = 1024,                   /* Optimized for Boomerang */
    .tx_copybreak = 1024,                   /* Optimized for Boomerang */
    .cache_coherent = false,                /* Cache management required */
    .supports_sg = true,                    /* Scatter-gather capable */
    .no_64k_cross = false,                  /* PCI doesn't have 64KB constraint */
    .max_segment_size = 131072,             /* Larger segments with S/G */
    .device_name = "3C900-TPO"
};

/**
 * 3C905 Boomerang TX/B - Enhanced Boomerang
 * - Full Boomerang feature set
 * - Scatter-gather DMA support
 * - Higher performance than 3C900
 */
const device_caps_t caps_3c905 = {
    .dma_addr_bits = 32,                    /* Full 32-bit PCI */
    .max_sg_entries = 8,                    /* Enhanced scatter-gather */
    .sg_boundary = 65536,                   /* 64KB boundary */
    .alignment = 16,                        /* Cache optimization */
    .descriptor_alignment = 16,             /* Descriptor alignment */
    .needs_vds = false,                     /* PCI operation */
    .rx_copybreak = 1536,                   /* GPT-5: Better than 1024 */
    .tx_copybreak = 1536,                   /* GPT-5: Better than 1024 */
    .cache_coherent = false,                /* Cache management needed */
    .supports_sg = true,                    /* Full scatter-gather */
    .no_64k_cross = false,                  /* PCI doesn't have 64KB constraint */
    .max_segment_size = 131072,             /* Larger segments with S/G */
    .device_name = "3C905"
};

/**
 * 3C905B Cyclone - Advanced features
 * - Cyclone architecture with hardware checksum offload
 * - Enhanced scatter-gather capabilities
 * - Advanced performance features
 */
const device_caps_t caps_3c905b = {
    .dma_addr_bits = 32,                    /* Full 32-bit PCI */
    .max_sg_entries = 8,                    /* Full scatter-gather */
    .sg_boundary = 65536,                   /* 64KB handling */
    .alignment = 16,                        /* Cache line aligned */
    .descriptor_alignment = 16,             /* Descriptor alignment */
    .needs_vds = false,                     /* PCI operation */
    .rx_copybreak = 1536,                   /* Optimized for Cyclone */
    .tx_copybreak = 1536,                   /* Optimized for Cyclone */
    .cache_coherent = false,                /* Cache management */
    .supports_sg = true,                    /* Advanced scatter-gather */
    .no_64k_cross = false,                  /* PCI doesn't have 64KB constraint */
    .max_segment_size = 131072,             /* Larger segments with S/G */
    .device_name = "3C905B"
};

/**
 * 3C905C Tornado - Most advanced features
 * - Tornado architecture with full feature set
 * - Hardware checksum offload
 * - Advanced flow control
 * - Maximum performance capabilities
 */
const device_caps_t caps_3c905c = {
    .dma_addr_bits = 32,                    /* Full 32-bit PCI */
    .max_sg_entries = 8,                    /* Maximum scatter-gather */
    .sg_boundary = 65536,                   /* 64KB boundary management */
    .alignment = 16,                        /* Optimal cache alignment */
    .descriptor_alignment = 16,             /* Descriptor alignment */
    .needs_vds = false,                     /* PCI operation */
    .rx_copybreak = 1536,                   /* Maximum performance */
    .tx_copybreak = 1536,                   /* Maximum performance */
    .cache_coherent = false,                /* Cache management required */
    .supports_sg = true,                    /* Full scatter-gather support */
    .no_64k_cross = false,                  /* PCI doesn't have 64KB constraint */
    .max_segment_size = 131072,             /* Larger segments with S/G */
    .device_name = "3C905C"
};

/* ============================================================================
 * Device Capability Lookup Functions
 * ============================================================================ */

/* Device capability lookup table */
static const struct {
    const char* name;
    const device_caps_t* caps;
} device_cap_table[] = {
    { "3C509B", &caps_3c509b },
    { "3C515-TX", &caps_3c515tx },
    { "3C515TX", &caps_3c515tx },      /* Alternative naming */
    { "3C589", &caps_3c589 },
    { "3C590", &caps_3c590 },
    { "3C595", &caps_3c595 },
    { "3C900-TPO", &caps_3c900 },
    { "3C900", &caps_3c900 },          /* Alternative naming */
    { "3C905", &caps_3c905 },
    { "3C905B", &caps_3c905b },
    { "3C905C", &caps_3c905c },
    { NULL, NULL }                      /* End marker */
};

/**
 * @brief Get device capabilities by name
 * 
 * @param device_name Device name (e.g., "3C515-TX", "3C905")
 * @return Pointer to device capabilities or NULL if not found
 */
const device_caps_t* dma_get_device_caps(const char* device_name)
{
    int i;
    
    if (!device_name) {
        return NULL;
    }
    
    /* Search capability table */
    for (i = 0; device_cap_table[i].name != NULL; i++) {
        if (strcmp(device_name, device_cap_table[i].name) == 0) {
            return device_cap_table[i].caps;
        }
    }
    
    /* Not found */
    return NULL;
}

/**
 * @brief Validate device capability descriptor
 *
 * GPT-5 Critical Enhancement: Validate device capability descriptors 
 * for structural and logical consistency to prevent configuration errors.
 *
 * @param caps Device capability descriptor to validate
 * @param device_name Device name for error reporting
 * @return true if capabilities are valid, false otherwise
 */
bool validate_device_caps(const device_caps_t* caps, const char* device_name)
{
    bool is_valid = true;
    
    if (!caps) {
        printf("VALIDATION ERROR: %s - NULL capability descriptor\n", device_name ? device_name : "Unknown");
        return false;
    }
    
    if (!device_name || strlen(device_name) == 0) {
        printf("VALIDATION ERROR: Missing device name for capability validation\n");
        is_valid = false;
    }
    
    /* Validate DMA addressing bits */
    if (caps->dma_addr_bits != 24 && caps->dma_addr_bits != 32) {
        printf("VALIDATION ERROR: %s - Invalid dma_addr_bits=%d (must be 24 or 32)\n", 
               device_name, caps->dma_addr_bits);
        is_valid = false;
    }
    
    /* Validate scatter-gather entries */
    if (caps->max_sg_entries == 0 || caps->max_sg_entries > 8) {
        printf("VALIDATION ERROR: %s - Invalid max_sg_entries=%d (must be 1-8)\n",
               device_name, caps->max_sg_entries);
        is_valid = false;
    }
    
    /* Validate sg_boundary */
    if (caps->sg_boundary == 0 || (caps->sg_boundary & (caps->sg_boundary - 1)) != 0) {
        printf("VALIDATION ERROR: %s - Invalid sg_boundary=%d (must be power of 2)\n",
               device_name, caps->sg_boundary);
        is_valid = false;
    }
    
    /* Validate alignment requirements */
    if (caps->alignment == 0 || caps->alignment > 128 || 
        (caps->alignment & (caps->alignment - 1)) != 0) {
        printf("VALIDATION ERROR: %s - Invalid alignment=%d (must be power of 2, 1-128)\n",
               device_name, caps->alignment);
        is_valid = false;
    }
    
    /* Validate descriptor alignment */
    if (caps->descriptor_alignment == 0 || 
        (caps->descriptor_alignment & (caps->descriptor_alignment - 1)) != 0) {
        printf("VALIDATION ERROR: %s - Invalid descriptor_alignment=%d (must be power of 2)\n",
               device_name, caps->descriptor_alignment);
        is_valid = false;
    }
    
    /* Validate copybreak values */
    if (caps->rx_copybreak > 2048 || caps->tx_copybreak > 2048) {
        printf("VALIDATION ERROR: %s - Copybreak values too large (rx=%d, tx=%d, max=2048)\n",
               device_name, caps->rx_copybreak, caps->tx_copybreak);
        is_valid = false;
    }
    
    /* GPT-5 Enhanced: Additional field validation */
    
    /* Validate no_64k_cross flag consistency */
    if (caps->no_64k_cross && caps->dma_addr_bits != 24) {
        printf("VALIDATION WARNING: %s - no_64k_cross set but not ISA device (dma_addr_bits=%d)\n",
               device_name, caps->dma_addr_bits);
    }
    
    /* Validate max_segment_size */
    if (caps->max_segment_size == 0 || caps->max_segment_size > 0x100000) {
        printf("VALIDATION ERROR: %s - Invalid max_segment_size=%lu (must be 1-1MB)\n",
               device_name, caps->max_segment_size);
        is_valid = false;
    }
    
    /* Logical consistency checks */
    
    /* ISA devices should have 24-bit addressing and need VDS */
    if (strstr(device_name, "3C509") != NULL || strstr(device_name, "3C515") != NULL || 
        strstr(device_name, "3C589") != NULL) {
        if (caps->dma_addr_bits != 24) {
            printf("VALIDATION WARNING: %s - ISA device with non-24-bit addressing\n", device_name);
        }
        
        /* 3C515 is ISA bus master and needs special handling */
        if (strstr(device_name, "3C515") != NULL) {
            if (!caps->needs_vds) {
                printf("VALIDATION WARNING: %s - 3C515 should typically need VDS for ISA bus mastering\n", device_name);
            }
            if (!caps->no_64k_cross) {
                printf("VALIDATION ERROR: %s - 3C515 must have no_64k_cross=true for ISA DMA\n", device_name);
                is_valid = false;
            }
        } else {
            /* 3C509B and 3C589 are PIO devices */
            if (caps->no_64k_cross) {
                printf("VALIDATION WARNING: %s - PIO device has no_64k_cross set\n", device_name);
            }
        }
    }
    
    /* PCI devices should have 32-bit addressing and not need VDS */
    if (strstr(device_name, "3C59") != NULL || strstr(device_name, "3C90") != NULL) {
        if (caps->dma_addr_bits != 32) {
            printf("VALIDATION WARNING: %s - PCI device with non-32-bit addressing\n", device_name);
        }
        if (caps->needs_vds) {
            printf("VALIDATION WARNING: %s - PCI device should not typically need VDS\n", device_name);
        }
    }
    
    /* Scatter-gather consistency */
    if (caps->supports_sg && caps->max_sg_entries <= 1) {
        printf("VALIDATION ERROR: %s - supports_sg=true but max_sg_entries=%d\n",
               device_name, caps->max_sg_entries);
        is_valid = false;
    }
    
    if (!caps->supports_sg && caps->max_sg_entries > 1) {
        printf("VALIDATION WARNING: %s - supports_sg=false but max_sg_entries=%d\n",
               device_name, caps->max_sg_entries);
    }
    
    /* Cache coherency consistency */
    if (caps->cache_coherent && caps->needs_vds) {
        printf("VALIDATION INFO: %s - Device claims cache coherent but needs VDS\n", device_name);
    }
    
    /* Device name consistency */
    if (!caps->device_name || strcmp(caps->device_name, device_name) != 0) {
        printf("VALIDATION ERROR: %s - Capability device_name mismatch: '%s'\n",
               device_name, caps->device_name ? caps->device_name : "(null)");
        is_valid = false;
    }
    
    if (is_valid) {
        printf("VALIDATION OK: %s - All capability checks passed\n", device_name);
    }
    
    return is_valid;
}

/**
 * @brief Validate all predefined device capabilities
 *
 * GPT-5 Enhancement: Comprehensive validation of all device descriptors
 * to catch configuration errors during initialization.
 *
 * @return true if all device capabilities are valid
 */
bool validate_all_device_caps(void)
{
    bool all_valid = true;
    int i;
    
    printf("=== Device Capability Validation ===\n");
    
    /* Validate all entries in the lookup table */
    for (i = 0; device_cap_table[i].name != NULL; i++) {
        if (!validate_device_caps(device_cap_table[i].caps, device_cap_table[i].name)) {
            all_valid = false;
        }
    }
    
    if (all_valid) {
        printf("Device Capability Validation: ALL CHECKS PASSED\n");
    } else {
        printf("Device Capability Validation: ERRORS DETECTED - Check configuration\n");
    }
    
    printf("====================================\n");
    return all_valid;
}

/**
 * @brief Register custom device capabilities
 * 
 * This function allows registering custom or modified device capabilities
 * at runtime. Currently returns error as we use static capabilities.
 * 
 * @param device_name Device name
 * @param caps Capability descriptor
 * @return 0 on success, negative on error
 */
int dma_register_device_caps(const char* device_name, const device_caps_t* caps)
{
    /* For now, we use static capability definitions */
    /* Future enhancement could support dynamic registration */
    (void)device_name;
    (void)caps;
    
    return -1;  /* Not implemented */
}