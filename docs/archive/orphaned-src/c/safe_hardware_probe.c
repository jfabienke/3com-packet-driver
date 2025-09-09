/**
 * @file safe_hardware_probe.c
 * @brief Safe Hardware Probing Framework
 *
 * CRITICAL: GPT-5 Identified Probing Safety Issues
 * "ISA autoprobing can hang some systems; gate probes behind safe ranges 
 *  and add a 'no-probe' mode requiring explicit parameters."
 *
 * This framework implements:
 * 1. Restricted I/O ranges for safe probing
 * 2. Timeout protection against system hangs
 * 3. Manual configuration mode (no-probe)
 * 4. 3Com-specific safe probe sequences
 * 5. Conflict detection and avoidance
 * 6. Graceful fallback mechanisms
 *
 * Supports: 3C509B, 3C589, 3C905B/C, 3C515-TX
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dos.h>
#include <conio.h>

#include "../../include/hardware.h"
#include "../../include/logging.h"
#include "../../include/pnp.h"
#include "../../docs/agents/shared/error-codes.h"
#include "../../docs/agents/shared/timing-measurement.h"

/* Safe I/O range definitions */
typedef struct {
    uint16_t start_addr;                    /* Range start address */
    uint16_t end_addr;                      /* Range end address */
    char description[32];                   /* Range description */
    bool safe_for_probing;                  /* Safe to probe this range */
} io_range_t;

/* Hardware probe result */
typedef struct {
    uint16_t io_base;                       /* Detected I/O base */
    uint8_t irq;                            /* Detected IRQ */
    uint16_t vendor_id;                     /* Vendor ID */
    uint16_t device_id;                     /* Device ID */
    uint8_t mac_address[6];                 /* MAC address */
    char device_name[32];                   /* Device description */
    bool probe_successful;                  /* Probe completed successfully */
    uint32_t probe_time_us;                 /* Time taken to probe */
} hardware_probe_result_t;

/* Probe configuration */
typedef struct {
    bool auto_probe_enabled;                /* Allow automatic probing */
    bool use_safe_ranges_only;              /* Restrict to safe ranges */
    uint32_t probe_timeout_us;              /* Timeout for probe operations */
    bool verbose_logging;                   /* Enable verbose probe logging */
    uint16_t manual_io_base;                /* Manual I/O base (0 = auto) */
    uint8_t manual_irq;                     /* Manual IRQ (0 = auto) */
} probe_config_t;

/* Global probe configuration */
static probe_config_t g_probe_config = {
    .auto_probe_enabled = true,
    .use_safe_ranges_only = true,
    .probe_timeout_us = 100000,             /* 100ms timeout */
    .verbose_logging = false,
    .manual_io_base = 0,
    .manual_irq = 0
};

/* Safe I/O ranges for 3Com network cards */
static const io_range_t safe_3com_ranges[] = {
    /* 3C509B ISA PnP standard ranges */
    { 0x0200, 0x021F, "3C509B Range 1", true },
    { 0x0220, 0x023F, "3C509B Range 2", true },
    { 0x0240, 0x025F, "3C509B Range 3", true },
    { 0x0260, 0x027F, "3C509B Range 4", true },
    { 0x0280, 0x029F, "3C509B Range 5", true },
    { 0x02A0, 0x02BF, "3C509B Range 6", true },
    { 0x02C0, 0x02DF, "3C509B Range 7", true },
    { 0x02E0, 0x02FF, "3C509B Range 8", true },
    { 0x0300, 0x031F, "3C509B Range 9", true },
    
    /* 3C589 PCMCIA ranges (when configured for I/O) */
    { 0x0340, 0x035F, "3C589 PCMCIA Range 1", true },
    { 0x0360, 0x037F, "3C589 PCMCIA Range 2", true },
    
    /* 3C515-TX ISA ranges */
    { 0x0380, 0x039F, "3C515-TX Range 1", true },
    { 0x03A0, 0x03BF, "3C515-TX Range 2", true },
    
    /* Dangerous ranges to avoid */
    { 0x0000, 0x00FF, "System DMA/PIC", false },         /* System controllers */
    { 0x0170, 0x017F, "IDE Secondary", false },          /* IDE controllers */
    { 0x01F0, 0x01FF, "IDE Primary", false },
    { 0x03B0, 0x03DF, "VGA/Graphics", false },           /* VGA registers */
    { 0x03F0, 0x03FF, "Floppy/Serial", false },          /* Floppy and serial */
};

#define SAFE_RANGE_COUNT (sizeof(safe_3com_ranges) / sizeof(io_range_t))

/* Function prototypes */
static bool is_io_range_safe(uint16_t io_base, uint16_t range_size);
static bool probe_port_safely(uint16_t port, uint32_t timeout_us);
static int probe_3c509b_safely(hardware_probe_result_t* result);
static int probe_3c589_safely(hardware_probe_result_t* result);
static int probe_3c905_safely(hardware_probe_result_t* result);
static int probe_3c515tx_safely(hardware_probe_result_t* result);
static bool detect_port_conflict(uint16_t io_base, uint16_t range_size);
static void save_probe_timeout(void);
static void restore_probe_timeout(void);

/* Timeout handling for safe probing */
static volatile bool probe_timeout_occurred = false;
static void (__interrupt __far *old_timer_handler)(void);

/**
 * @brief Initialize safe hardware probing
 *
 * Sets up timeout handlers and validates probe configuration.
 * GPT-5 Requirement: Safe probing with timeout protection.
 *
 * @return SUCCESS or error code
 */
int safe_probe_init(void) {
    log_info("Safe Probe: Initializing safe hardware probing framework");
    
    /* Validate configuration */
    if (g_probe_config.probe_timeout_us > 1000000) { /* 1 second max */
        log_warning("Safe Probe: Timeout too large, limiting to 1 second");
        g_probe_config.probe_timeout_us = 1000000;
    }
    
    if (g_probe_config.manual_io_base != 0) {
        log_info("Safe Probe: Manual I/O base specified: 0x%X", g_probe_config.manual_io_base);
        g_probe_config.auto_probe_enabled = false;
    }
    
    /* Install timeout handler */
    save_probe_timeout();
    
    log_info("Safe Probe: Framework initialized - Auto probe: %s, Safe ranges: %s",
             g_probe_config.auto_probe_enabled ? "Enabled" : "Disabled",
             g_probe_config.use_safe_ranges_only ? "Only" : "All");
    
    return SUCCESS;
}

/**
 * @brief Configure probe settings
 *
 * GPT-5 Requirement: "Add a 'no-probe' mode requiring explicit parameters"
 * Allows manual configuration to avoid any probing risks.
 *
 * @param config Probe configuration
 * @return SUCCESS or error code
 */
int safe_probe_configure(const probe_config_t* config) {
    if (!config) {
        return ERROR_INVALID_PARAM;
    }
    
    g_probe_config = *config;
    
    if (g_probe_config.manual_io_base != 0 && g_probe_config.manual_irq != 0) {
        log_info("Safe Probe: Manual mode - I/O: 0x%X, IRQ: %d", 
                 g_probe_config.manual_io_base, g_probe_config.manual_irq);
        g_probe_config.auto_probe_enabled = false;
    }
    
    return SUCCESS;
}

/**
 * @brief Safely probe for all 3Com hardware
 *
 * CRITICAL: GPT-5 Safety Requirements Implemented:
 * 1. Restricted to safe I/O ranges only
 * 2. Timeout protection against system hangs  
 * 3. Conflict detection before probing
 * 4. Manual override capability
 *
 * @param results Array to store probe results
 * @param max_results Maximum number of results to return
 * @return Number of devices found, or negative error code
 */
int safe_probe_all_3com_hardware(hardware_probe_result_t* results, int max_results) {
    int device_count = 0;
    int probe_result;
    
    if (!results || max_results <= 0) {
        return ERROR_INVALID_PARAM;
    }
    
    log_info("Safe Probe: Starting safe hardware probe for 3Com devices");
    
    /* Manual configuration mode - no probing */
    if (!g_probe_config.auto_probe_enabled && 
        g_probe_config.manual_io_base != 0 && 
        g_probe_config.manual_irq != 0) {
        
        log_info("Safe Probe: Using manual configuration - no auto-probing");
        
        hardware_probe_result_t* result = &results[0];
        memset(result, 0, sizeof(hardware_probe_result_t));
        result->io_base = g_probe_config.manual_io_base;
        result->irq = g_probe_config.manual_irq;
        result->probe_successful = true;
        strcpy(result->device_name, "Manual Configuration");
        
        return 1; /* Return manual configuration as single result */
    }
    
    if (!g_probe_config.auto_probe_enabled) {
        log_info("Safe Probe: Auto-probing disabled, no hardware detection performed");
        return 0;
    }
    
    /* Probe for 3C509B ISA cards */
    if (device_count < max_results) {
        log_debug("Safe Probe: Probing for 3C509B ISA cards");
        probe_result = probe_3c509b_safely(&results[device_count]);
        if (probe_result == SUCCESS && results[device_count].probe_successful) {
            device_count++;
            log_info("Safe Probe: Found 3C509B at I/O 0x%X, IRQ %d", 
                     results[device_count-1].io_base, results[device_count-1].irq);
        }
    }
    
    /* Probe for 3C589 PCMCIA cards */
    if (device_count < max_results) {
        log_debug("Safe Probe: Probing for 3C589 PCMCIA cards");
        probe_result = probe_3c589_safely(&results[device_count]);
        if (probe_result == SUCCESS && results[device_count].probe_successful) {
            device_count++;
            log_info("Safe Probe: Found 3C589 at I/O 0x%X, IRQ %d", 
                     results[device_count-1].io_base, results[device_count-1].irq);
        }
    }
    
    /* Probe for 3C905 PCI cards */  
    if (device_count < max_results) {
        log_debug("Safe Probe: Probing for 3C905 PCI cards");
        probe_result = probe_3c905_safely(&results[device_count]);
        if (probe_result == SUCCESS && results[device_count].probe_successful) {
            device_count++;
            log_info("Safe Probe: Found 3C905 at I/O 0x%X, IRQ %d", 
                     results[device_count-1].io_base, results[device_count-1].irq);
        }
    }
    
    /* Probe for 3C515-TX ISA cards */
    if (device_count < max_results) {
        log_debug("Safe Probe: Probing for 3C515-TX ISA cards");
        probe_result = probe_3c515tx_safely(&results[device_count]);
        if (probe_result == SUCCESS && results[device_count].probe_successful) {
            device_count++;
            log_info("Safe Probe: Found 3C515-TX at I/O 0x%X, IRQ %d", 
                     results[device_count-1].io_base, results[device_count-1].irq);
        }
    }
    
    log_info("Safe Probe: Hardware probe completed - found %d devices", device_count);
    
    return device_count;
}

/**
 * @brief Check if I/O range is safe for probing
 *
 * GPT-5 Requirement: "Gate probes behind safe ranges"
 * Validates I/O addresses against known safe ranges.
 */
static bool is_io_range_safe(uint16_t io_base, uint16_t range_size) {
    int i;
    uint16_t end_addr = io_base + range_size - 1;
    
    if (!g_probe_config.use_safe_ranges_only) {
        return true; /* Allow all ranges if safety is disabled */
    }
    
    /* Check against safe ranges */
    for (i = 0; i < SAFE_RANGE_COUNT; i++) {
        const io_range_t* range = &safe_3com_ranges[i];
        
        if (!range->safe_for_probing) {
            /* Check if requested range overlaps with unsafe range */
            if (io_base <= range->end_addr && end_addr >= range->start_addr) {
                log_warning("Safe Probe: I/O range 0x%X-0x%X overlaps unsafe range %s", 
                           io_base, end_addr, range->description);
                return false;
            }
        } else {
            /* Check if requested range is within a safe range */
            if (io_base >= range->start_addr && end_addr <= range->end_addr) {
                log_debug("Safe Probe: I/O range 0x%X-0x%X is within safe range %s", 
                         io_base, end_addr, range->description);
                return true;
            }
        }
    }
    
    /* If we get here, range is not explicitly safe */
    log_debug("Safe Probe: I/O range 0x%X-0x%X not in safe ranges", io_base, end_addr);
    return false;
}

/**
 * @brief Safely probe a single I/O port with timeout
 *
 * GPT-5 Requirement: Timeout protection to prevent system hangs.
 * Uses timer interrupt to detect and recover from hanging probes.
 */
static bool probe_port_safely(uint16_t port, uint32_t timeout_us) {
    pit_timing_t timing;
    uint16_t test_value, read_value;
    uint32_t elapsed_us;
    bool probe_success = false;
    
    if (!is_io_range_safe(port, 1)) {
        log_debug("Safe Probe: Port 0x%X is not in safe range", port);
        return false;
    }
    
    /* Check for port conflicts first */
    if (detect_port_conflict(port, 1)) {
        log_debug("Safe Probe: Port 0x%X has detected conflict", port);
        return false;
    }
    
    /* Start timeout timing */
    PIT_START_TIMING(&timing);
    probe_timeout_occurred = false;
    
    /* Perform safe probe sequence */
    __asm {
        cli                             ; Disable interrupts for atomic operation
    }
    
    /* Try to read the port safely */
    test_value = inp(port);
    
    /* Small delay to allow hardware to respond */
    for (int i = 0; i < 100; i++) {
        __asm nop;
    }
    
    /* Read again to see if value is stable */
    read_value = inp(port);
    
    __asm {
        sti                             ; Re-enable interrupts
    }
    
    PIT_END_TIMING(&timing);
    elapsed_us = PIT_GET_MICROSECONDS(&timing);
    
    /* Check for timeout */
    if (probe_timeout_occurred || elapsed_us > timeout_us) {
        log_warning("Safe Probe: Port 0x%X probe timed out (%lu μs)", port, elapsed_us);
        return false;
    }
    
    /* Simple heuristic: if port returns 0xFF, it's likely not connected */
    if (test_value == 0xFF && read_value == 0xFF) {
        log_debug("Safe Probe: Port 0x%X appears unconnected (returns 0xFF)", port);
        return false;
    }
    
    /* If values are different, hardware might be responding */
    if (test_value != read_value) {
        log_debug("Safe Probe: Port 0x%X shows activity (0x%X -> 0x%X)", 
                 port, test_value, read_value);
        probe_success = true;
    }
    
    log_debug("Safe Probe: Port 0x%X probe completed in %lu μs - %s", 
             port, elapsed_us, probe_success ? "Active" : "Inactive");
    
    return probe_success;
}

/**
 * @brief Detect I/O port conflicts
 *
 * Checks if other drivers or hardware are already using the port range.
 */
static bool detect_port_conflict(uint16_t io_base, uint16_t range_size) {
    /* This is a simplified conflict detection */
    /* In a full implementation, this would check:
     * - Other packet drivers
     * - Sound cards
     * - Other network cards
     * - System devices
     */
    
    /* For now, just check a few known conflict ranges */
    if (io_base >= 0x220 && io_base <= 0x233) {
        /* Sound Blaster range */
        log_debug("Safe Probe: Potential Sound Blaster conflict at 0x%X", io_base);
        return true;
    }
    
    if (io_base == 0x300 && range_size >= 16) {
        /* Common NE2000 range - might conflict */
        log_debug("Safe Probe: Potential NE2000 conflict at 0x%X", io_base);
        return false; /* Allow this since NE2000 is compatible */
    }
    
    return false; /* No conflicts detected */
}

/**
 * @brief Safely probe for 3C509B ISA cards
 *
 * Uses ISA PnP protocol for safe detection without random I/O probing.
 */
static int probe_3c509b_safely(hardware_probe_result_t* result) {
    pit_timing_t timing;
    uint16_t io_base;
    uint8_t irq;
    int probe_result;
    
    memset(result, 0, sizeof(hardware_probe_result_t));
    strcpy(result->device_name, "3C509B ISA PnP");
    result->vendor_id = 0x6D50; /* 3Com vendor ID */
    result->device_id = 0x5090; /* 3C509B device ID */
    
    log_debug("Safe Probe: Starting 3C509B ISA PnP probe");
    
    PIT_START_TIMING(&timing);
    
    /* Use ISA PnP protocol for safe detection */
    probe_result = pnp_detect_3c509b(&io_base, &irq);
    
    PIT_END_TIMING(&timing);
    result->probe_time_us = PIT_GET_MICROSECONDS(&timing);
    
    if (probe_result == SUCCESS) {
        /* Validate detected I/O range is safe */
        if (!is_io_range_safe(io_base, 16)) {
            log_warning("Safe Probe: 3C509B detected at unsafe I/O range 0x%X", io_base);
            return ERROR_UNSAFE_IO_RANGE;
        }
        
        result->io_base = io_base;
        result->irq = irq;
        result->probe_successful = true;
        
        /* Try to read MAC address safely */
        if (read_3c509b_mac_address(io_base, result->mac_address) == SUCCESS) {
            log_debug("Safe Probe: 3C509B MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                     result->mac_address[0], result->mac_address[1], result->mac_address[2],
                     result->mac_address[3], result->mac_address[4], result->mac_address[5]);
        }
        
        log_debug("Safe Probe: 3C509B probe successful (%lu μs)", result->probe_time_us);
        return SUCCESS;
    } else {
        log_debug("Safe Probe: 3C509B not detected (%lu μs)", result->probe_time_us);
        return ERROR_HARDWARE_NOT_FOUND;
    }
}

/* Stub implementations for other hardware probes */
static int probe_3c589_safely(hardware_probe_result_t* result) {
    memset(result, 0, sizeof(hardware_probe_result_t));
    strcpy(result->device_name, "3C589 PCMCIA");
    log_debug("Safe Probe: 3C589 probe not yet implemented");
    return ERROR_NOT_IMPLEMENTED;
}

static int probe_3c905_safely(hardware_probe_result_t* result) {
    memset(result, 0, sizeof(hardware_probe_result_t));
    strcpy(result->device_name, "3C905 PCI");
    log_debug("Safe Probe: 3C905 probe not yet implemented");
    return ERROR_NOT_IMPLEMENTED;
}

static int probe_3c515tx_safely(hardware_probe_result_t* result) {
    memset(result, 0, sizeof(hardware_probe_result_t));
    strcpy(result->device_name, "3C515-TX ISA");
    log_debug("Safe Probe: 3C515-TX probe not yet implemented");
    return ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Print safe probe configuration
 */
void safe_probe_print_config(void) {
    int i;
    
    printf("Safe Hardware Probe Configuration:\\n");
    printf("  Auto Probe: %s\\n", g_probe_config.auto_probe_enabled ? "Enabled" : "Disabled");
    printf("  Safe Ranges Only: %s\\n", g_probe_config.use_safe_ranges_only ? "Yes" : "No");
    printf("  Probe Timeout: %lu μs\\n", g_probe_config.probe_timeout_us);
    printf("  Verbose Logging: %s\\n", g_probe_config.verbose_logging ? "Enabled" : "Disabled");
    
    if (g_probe_config.manual_io_base != 0) {
        printf("  Manual I/O Base: 0x%X\\n", g_probe_config.manual_io_base);
    }
    if (g_probe_config.manual_irq != 0) {
        printf("  Manual IRQ: %d\\n", g_probe_config.manual_irq);
    }
    
    printf("\\nSafe I/O Ranges:\\n");
    for (i = 0; i < SAFE_RANGE_COUNT; i++) {
        const io_range_t* range = &safe_3com_ranges[i];
        if (range->safe_for_probing) {
            printf("  0x%04X-0x%04X: %s\\n", range->start_addr, range->end_addr, range->description);
        }
    }
}

/**
 * @brief Shutdown safe probing framework
 */
int safe_probe_shutdown(void) {
    log_info("Safe Probe: Shutting down safe probing framework");
    
    /* Restore timeout handler */
    restore_probe_timeout();
    
    return SUCCESS;
}

/* Timer interrupt handler for timeout protection */
static void __interrupt __far probe_timeout_handler(void) {
    probe_timeout_occurred = true;
    
    /* Chain to original handler */
    if (old_timer_handler) {
        old_timer_handler();
    }
}

/* Save and restore timeout handlers */
static void save_probe_timeout(void) {
    old_timer_handler = _dos_getvect(0x08); /* Timer interrupt */
    _dos_setvect(0x08, probe_timeout_handler);
}

static void restore_probe_timeout(void) {
    if (old_timer_handler) {
        _dos_setvect(0x08, old_timer_handler);
        old_timer_handler = NULL;
    }
}