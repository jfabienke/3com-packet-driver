/**
 * @file buffer_autoconfig.c
 * @brief Intelligent buffer auto-configuration implementation
 *
 * Automatically determines optimal buffer sizes based on:
 * - Hardware capabilities
 * - Link speed negotiation
 * - Bus master DMA test results
 * - Available memory
 * - CPU generation
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "bufcfg.h"
#include "hardware.h"
#include "memory.h"
#include "busmaster.h"
#include "cpudet.h"
#include "logging.h"
#include "xmsdet.h"

/* Forward declarations */
static void configure_for_3c509b(buffer_config_t* config);
static void configure_for_3c515_10mbps(buffer_config_t* config);
static void configure_for_3c515_100mbps(buffer_config_t* config);
static void apply_memory_constraints(buffer_config_t* config);
static void apply_cpu_optimizations(buffer_config_t* config);

/**
 * Auto-configure optimal buffer settings
 * @return Configured buffer settings
 */
buffer_config_t auto_configure_buffers(void) {
    buffer_config_t config;
    memset(&config, 0, sizeof(config));
    
    /* Step 1: Detect hardware */
    config.nic_type = detect_nic_type();
    if (config.nic_type == NIC_UNKNOWN) {
        log_error("No supported NIC detected");
        apply_minimal_config(&config);
        return config;
    }
    
    /* Step 2: Get link speed (auto-negotiate if needed) */
    config.link_speed = get_link_speed();
    log_info("Link speed: %d Mbps", config.link_speed);
    
    /* Step 3: Detect CPU class */
    config.cpu_class = detect_cpu_type();
    log_info("CPU detected: 80%d86", config.cpu_class);
    
    /* Step 4: Test bus mastering if applicable */
    if (config.nic_type == NIC_3C515_TX && config.cpu_class >= CPU_80286) {
        config.bus_master_ok = test_bus_master_dma_quick();
        if (!config.bus_master_ok) {
            log_warning("Bus master DMA test failed, using PIO mode");
        }
    }
    
    /* Step 5: Check available memory */
    config.conventional_free = get_free_conventional_memory() / 1024;
    config.umb_free = get_free_umb_memory() / 1024;
    config.xms_free = get_free_xms_memory() / 1024;
    
    log_info("Memory available: Conv=%dKB, UMB=%dKB, XMS=%ldKB",
             config.conventional_free, config.umb_free, config.xms_free);
    
    /* Step 6: Check XMS availability for memory optimization */
    if (xms_is_available()) {
        xms_info_t xms_info;
        if (xms_get_info(&xms_info) == XMS_SUCCESS && xms_info.free_kb >= 128) {
            config.use_xms = 1;
            config.xms_buffers = (xms_info.free_kb >= 256) ? 32 : 16;
            config.staging_buffers = 12;  /* 12 staging buffers in conventional */
            config.xms_threshold = 200;   /* RX_COPYBREAK threshold */
            log_info("XMS optimization enabled: %d XMS buffers, %d staging buffers",
                     config.xms_buffers, config.staging_buffers);
        }
    }
    
    /* Step 7: Apply configuration based on scenario */
    if (config.nic_type == NIC_3C509B) {
        configure_for_3c509b(&config);
    } else if (config.link_speed == 10) {
        configure_for_3c515_10mbps(&config);
    } else {
        configure_for_3c515_100mbps(&config);
    }
    
    /* Step 7: Apply constraints */
    apply_memory_constraints(&config);
    apply_cpu_optimizations(&config);
    
    /* Step 8: Calculate totals and estimates */
    config.total_buffer_memory = calculate_buffer_memory(
        config.buffer_size, config.tx_ring_count, config.rx_ring_count);
    config.expected_throughput = estimate_throughput(&config);
    config.cpu_utilization = estimate_cpu_usage(&config);
    
    return config;
}

/**
 * Configure for 3C509B (10 Mbps, PIO only)
 */
static void configure_for_3c509b(buffer_config_t* config) {
    config->buffer_size = BUFSIZE_512;
    config->tx_ring_count = 8;
    config->rx_ring_count = 8;
    config->transfer_mode = XFER_MODE_PIO;
    
    log_info("3C509B: 512B×16 buffers (8KB) for 10Mbps PIO");
}

/**
 * Configure for 3C515-TX at 10 Mbps
 */
static void configure_for_3c515_10mbps(buffer_config_t* config) {
    /* PIO is actually better at 10 Mbps due to BM overhead */
    config->buffer_size = BUFSIZE_512;
    config->tx_ring_count = 8;
    config->rx_ring_count = 8;
    config->transfer_mode = XFER_MODE_PIO;
    
    log_info("3C515 @ 10Mbps: Using PIO mode with 512B buffers");
}

/**
 * Configure for 3C515-TX at 100 Mbps
 */
static void configure_for_3c515_100mbps(buffer_config_t* config) {
    uint16_t available_memory = config->conventional_free;
    
    /* Include UMB if available */
    if (config->umb_free > 0) {
        available_memory += config->umb_free;
        config->use_umb = 1;
    }
    
    if (config->bus_master_ok) {
        /* Bus master mode - choose buffer size based on memory */
        
        /* If XMS is enabled, we can use smaller conventional memory footprint */
        if (config->use_xms) {
            config->buffer_size = BUFSIZE_1536;  /* Full MTU for staging */
            config->tx_ring_count = 16;          /* TX still needs conventional */
            config->rx_ring_count = 0;           /* RX uses staging + XMS */
            log_info("100Mbps BM+XMS: 1536B staging, %d XMS buffers (18KB conventional)",
                     config->xms_buffers);
        } else if (available_memory >= 48) {
            /* Optimal configuration without XMS */
            config->buffer_size = BUFSIZE_1024;
            config->tx_ring_count = 16;
            config->rx_ring_count = 32;
            log_info("100Mbps BM: Optimal 1024B×48 (48KB)");
        } else if (available_memory >= 32) {
            /* Good configuration */
            config->buffer_size = BUFSIZE_1024;
            config->tx_ring_count = 16;
            config->rx_ring_count = 16;
            log_info("100Mbps BM: Good 1024B×32 (32KB)");
        } else if (available_memory >= 16) {
            /* Acceptable configuration */
            config->buffer_size = BUFSIZE_512;
            config->tx_ring_count = 16;
            config->rx_ring_count = 16;
            log_info("100Mbps BM: Acceptable 512B×32 (16KB)");
        } else {
            /* Minimal with bus master */
            config->buffer_size = BUFSIZE_256;
            config->tx_ring_count = 16;
            config->rx_ring_count = 32;
            log_info("100Mbps BM: Minimal 256B×48 (12KB)");
        }
        config->transfer_mode = XFER_MODE_BUS_MASTER;
    } else {
        /* PIO fallback - CPU will be bottleneck anyway */
        config->buffer_size = BUFSIZE_256;
        config->tx_ring_count = 8;
        config->rx_ring_count = 16;
        config->transfer_mode = XFER_MODE_PIO;
        log_info("100Mbps PIO: 256B×24 (6KB) - CPU limited");
    }
}

/**
 * Apply memory constraints to configuration
 */
static void apply_memory_constraints(buffer_config_t* config) {
    uint16_t required = calculate_buffer_memory(
        config->buffer_size, config->tx_ring_count, config->rx_ring_count);
    
    /* Check if we need to reduce */
    while (required > config->conventional_free + config->umb_free) {
        if (config->buffer_size > BUFSIZE_256) {
            /* Try smaller buffers first */
            config->buffer_size /= 2;
        } else if (config->rx_ring_count > MIN_RX_RING) {
            /* Reduce RX ring */
            config->rx_ring_count = config->rx_ring_count * 3 / 4;
        } else if (config->tx_ring_count > MIN_TX_RING) {
            /* Reduce TX ring */
            config->tx_ring_count = config->tx_ring_count * 3 / 4;
        } else {
            /* Already at minimum */
            break;
        }
        
        required = calculate_buffer_memory(
            config->buffer_size, config->tx_ring_count, config->rx_ring_count);
    }
    
    if (required > config->conventional_free + config->umb_free) {
        log_warning("Insufficient memory for buffers, using absolute minimum");
        apply_minimal_config(config);
    }
}

/**
 * Apply CPU-specific optimizations
 */
static void apply_cpu_optimizations(buffer_config_t* config) {
    /* 286 can't handle bus mastering well */
    if (config->cpu_class == CPU_80286 && config->transfer_mode == XFER_MODE_BUS_MASTER) {
        if (config->link_speed == 100) {
            /* Keep BM for 100Mbps but reduce buffers */
            if (config->buffer_size > BUFSIZE_512) {
                config->buffer_size = BUFSIZE_512;
            }
        }
    }
    
    /* 386 benefits from 32-bit operations */
    if (config->cpu_class >= CPU_80386 && config->buffer_size == BUFSIZE_256) {
        /* Consider upgrading to 512 if memory allows */
        uint16_t new_required = calculate_buffer_memory(
            BUFSIZE_512, config->tx_ring_count, config->rx_ring_count);
        if (new_required <= config->conventional_free + config->umb_free) {
            config->buffer_size = BUFSIZE_512;
        }
    }
}

/**
 * Apply command-line overrides to configuration
 */
void apply_buffer_overrides(buffer_config_t* config, const buffer_override_t* override) {
    if (override->force_minimal) {
        apply_minimal_config(config);
        return;
    }
    
    if (override->force_optimal) {
        apply_optimal_config(config);
        return;
    }
    
    if (override->buffer_size > 0) {
        config->buffer_size = override->buffer_size;
    }
    
    if (override->tx_ring_count > 0) {
        config->tx_ring_count = override->tx_ring_count;
    }
    
    if (override->rx_ring_count > 0) {
        config->rx_ring_count = override->rx_ring_count;
    }
    
    if (override->force_pio) {
        config->transfer_mode = XFER_MODE_PIO;
    }
    
    /* Recalculate totals */
    config->total_buffer_memory = calculate_buffer_memory(
        config->buffer_size, config->tx_ring_count, config->rx_ring_count);
}

/**
 * Validate buffer configuration
 */
int validate_buffer_config(const buffer_config_t* config) {
    /* Check buffer size */
    if (config->buffer_size != BUFSIZE_256 &&
        config->buffer_size != BUFSIZE_512 &&
        config->buffer_size != BUFSIZE_1024 &&
        config->buffer_size != BUFSIZE_1536) {
        return 0;
    }
    
    /* Check ring sizes */
    if (config->tx_ring_count < MIN_TX_RING || config->tx_ring_count > MAX_TX_RING) {
        return 0;
    }
    
    if (config->rx_ring_count < MIN_RX_RING || config->rx_ring_count > MAX_RX_RING) {
        return 0;
    }
    
    /* Check DMA alignment for larger buffers */
    if (config->transfer_mode == XFER_MODE_BUS_MASTER && config->buffer_size > BUFSIZE_512) {
        if (!check_dma_alignment(config->buffer_size, 
                                config->tx_ring_count + config->rx_ring_count)) {
            log_warning("Buffer configuration may cross 64KB DMA boundary");
        }
    }
    
    return 1;
}

/**
 * Display buffer configuration
 */
void display_buffer_config(const buffer_config_t* config) {
    const char* nic_name = (config->nic_type == NIC_3C509B) ? "3C509B" : "3C515-TX";
    const char* mode = (config->transfer_mode == XFER_MODE_PIO) ? "PIO" : "Bus Master";
    
    printf("\n");
    printf("Buffer Configuration:\n");
    printf("  NIC: %s, Link: %d Mbps\n", nic_name, config->link_speed);
    printf("  Mode: %s\n", mode);
    printf("  Buffer Size: %d bytes\n", config->buffer_size);
    printf("  TX Ring: %d buffers\n", config->tx_ring_count);
    printf("  RX Ring: %d buffers\n", config->rx_ring_count);
    printf("  Total Memory: %d KB\n", config->total_buffer_memory);
    
    if (config->use_umb) {
        printf("  Location: Upper Memory Block\n");
    } else if (config->use_xms) {
        printf("  Location: Extended Memory (XMS)\n");
    } else {
        printf("  Location: Conventional Memory\n");
    }
    
    printf("  Expected Performance: ~%d%% line rate\n", config->expected_throughput);
    printf("  Expected CPU Usage: ~%d%%\n", config->cpu_utilization);
    printf("\n");
}

/**
 * Calculate total buffer memory requirement
 */
uint16_t calculate_buffer_memory(uint16_t buffer_size, uint8_t tx_count, uint8_t rx_count) {
    uint32_t total = (uint32_t)buffer_size * (tx_count + rx_count);
    return (uint16_t)((total + 1023) / 1024);  /* Round up to KB */
}

/**
 * Check if buffers will cross 64KB DMA boundary
 */
int check_dma_alignment(uint16_t buffer_size, uint16_t count) {
    uint32_t total_size = (uint32_t)buffer_size * count;
    
    /* If total size > 64KB, definitely crosses */
    if (total_size > 65536L) {
        return 0;
    }
    
    /* 256 and 512 byte buffers align naturally */
    if (buffer_size <= BUFSIZE_512) {
        return 1;
    }
    
    /* For larger buffers, need careful alignment */
    /* This is simplified - actual allocation will handle details */
    return (65536L % buffer_size) == 0;
}

/**
 * Apply minimal configuration (3KB)
 */
void apply_minimal_config(buffer_config_t* config) {
    config->buffer_size = BUFSIZE_256;
    config->tx_ring_count = MIN_TX_RING;
    config->rx_ring_count = MIN_RX_RING;
    config->transfer_mode = XFER_MODE_PIO;
    config->total_buffer_memory = 3;
    log_info("Minimal configuration: 256B×12 (3KB)");
}

/**
 * Apply standard configuration (auto-detected)
 */
void apply_standard_config(buffer_config_t* config) {
    /* This is handled by auto_configure_buffers() */
    *config = auto_configure_buffers();
}

/**
 * Apply optimal configuration (maximum performance)
 */
void apply_optimal_config(buffer_config_t* config) {
    config->buffer_size = BUFSIZE_1536;
    config->tx_ring_count = MAX_TX_RING;
    config->rx_ring_count = MAX_RX_RING;
    config->transfer_mode = XFER_MODE_BUS_MASTER;
    config->total_buffer_memory = 96;
    log_info("Optimal configuration: 1536B×64 (96KB)");
}

/**
 * Estimate throughput percentage
 */
uint8_t estimate_throughput(const buffer_config_t* config) {
    /* Base estimates */
    uint8_t throughput = 0;
    
    if (config->link_speed == 10) {
        /* 10 Mbps - usually CPU limited */
        if (config->buffer_size >= BUFSIZE_512) {
            throughput = 95;
        } else {
            throughput = 85;
        }
    } else {
        /* 100 Mbps - depends heavily on mode and buffers */
        if (config->transfer_mode == XFER_MODE_BUS_MASTER) {
            if (config->buffer_size >= BUFSIZE_1024) {
                throughput = 90;
            } else if (config->buffer_size >= BUFSIZE_512) {
                throughput = 70;
            } else {
                throughput = 50;
            }
            
            /* Adjust for ring size */
            if (config->rx_ring_count < 16) {
                throughput -= 10;
            }
        } else {
            /* PIO at 100 Mbps - CPU bottleneck */
            throughput = 35;
        }
    }
    
    /* CPU adjustments */
    if (config->cpu_class == CPU_80286) {
        throughput = throughput * 8 / 10;  /* 20% reduction */
    } else if (config->cpu_class >= CPU_PENTIUM) {
        throughput = throughput * 11 / 10; /* 10% bonus */
    }
    
    /* Cap at 95% */
    if (throughput > 95) throughput = 95;
    
    return throughput;
}

/**
 * Estimate CPU usage percentage
 */
uint8_t estimate_cpu_usage(const buffer_config_t* config) {
    uint8_t cpu_usage = 0;
    
    if (config->link_speed == 10) {
        /* 10 Mbps */
        if (config->transfer_mode == XFER_MODE_PIO) {
            cpu_usage = 15;  /* PIO at 10 Mbps */
        } else {
            cpu_usage = 10;  /* BM at 10 Mbps (overhead) */
        }
    } else {
        /* 100 Mbps */
        if (config->transfer_mode == XFER_MODE_PIO) {
            cpu_usage = 85;  /* PIO at 100 Mbps (saturated) */
        } else {
            cpu_usage = 30;  /* BM at 100 Mbps */
            
            /* Buffer size affects CPU usage */
            if (config->buffer_size == BUFSIZE_256) {
                cpu_usage += 15;  /* Fragmentation overhead */
            }
        }
    }
    
    /* CPU generation adjustments */
    switch (config->cpu_class) {
        case CPU_80286:
            cpu_usage = cpu_usage * 15 / 10;  /* 50% more */
            break;
        case CPU_80386:
            cpu_usage = cpu_usage * 12 / 10;  /* 20% more */
            break;
        case CPU_80486:
            /* Baseline */
            break;
        case CPU_PENTIUM:
        case CPU_PENTIUM4:
            cpu_usage = cpu_usage * 6 / 10;   /* 40% less */
            break;
    }
    
    /* Cap at 100% */
    if (cpu_usage > 100) cpu_usage = 100;
    
    return cpu_usage;
}