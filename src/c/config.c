/**
 * @file config.c
 * @brief Configuration parameter processing
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "../include/config.h"
#include "../include/static_routing.h"
#include "../include/logging.h"
#include "../include/cpu_detect.h"
#include "../include/busmaster_test.h"
#include "../include/production.h"
#include "../include/buffer_config.h"

/* All functions in this file go to cold section (discarded after init) */
#pragma code_seg("COLD_TEXT", "CODE")

/* Default configuration values */
static const config_t default_config = {
    0,              /* debug_level */
    1,              /* use_xms */
    1,              /* enable_routing */
    0,              /* enable_static_routing */
    4,              /* buffer_count */
    1514,           /* buffer_size */
    0x60,           /* interrupt_vector */
    0x300,          /* io_base (legacy) */
    5,              /* irq (legacy) */
    1,              /* enable_stats */
    0,              /* promiscuous_mode */
    1,              /* enable_logging */
    0,              /* test_mode */
    
    CONFIG_DEFAULT_IO1_BASE,    /* io1_base */
    CONFIG_DEFAULT_IO2_BASE,    /* io2_base */
    CONFIG_DEFAULT_IRQ1,        /* irq1 */
    CONFIG_DEFAULT_IRQ2,        /* irq2 */
    CONFIG_DEFAULT_SPEED,       /* speed */
    CONFIG_DEFAULT_BUSMASTER,   /* busmaster */
    PCI_ENABLED,                /* pci (enabled if available) */
    CONFIG_DEFAULT_LOG_ENABLED, /* log_enabled */
    {{0}},                      /* routes (initialized to zero) */
    0,                          /* route_count */
    
    /* Buffer auto-configuration overrides (0 = auto) */
    0,                          /* override_buffer_size */
    0,                          /* override_tx_ring_count */
    0,                          /* override_rx_ring_count */
    0,                          /* force_pio_mode */
    0,                          /* force_minimal_buffers */
    0                           /* force_optimal_buffers */
};

/* Configuration parameter names and handlers */
typedef struct {
    const char *name;
    int (*handler)(config_t *config, const char *value);
    const char *description;
} config_param_t;

/* Forward declarations for parameter handlers */
static int handle_debug_level(config_t *config, const char *value);
static int handle_use_xms(config_t *config, const char *value);
static int handle_enable_routing(config_t *config, const char *value);
static int handle_enable_static_routing(config_t *config, const char *value);
static int handle_buffer_count(config_t *config, const char *value);
static int handle_buffer_size(config_t *config, const char *value);
static int handle_interrupt_vector(config_t *config, const char *value);
static int handle_io_base(config_t *config, const char *value);
static int handle_irq(config_t *config, const char *value);
static int handle_enable_stats(config_t *config, const char *value);
static int handle_promiscuous_mode(config_t *config, const char *value);
static int handle_enable_logging(config_t *config, const char *value);
static int handle_test_mode(config_t *config, const char *value);

/* 3Com packet driver specific handlers */
static int handle_io1_base(config_t *config, const char *value);
static int handle_io2_base(config_t *config, const char *value);
static int handle_irq1(config_t *config, const char *value);
static int handle_irq2(config_t *config, const char *value);
static int handle_speed(config_t *config, const char *value);
static int handle_busmaster(config_t *config, const char *value);
static int handle_pci(config_t *config, const char *value);
static int handle_log(config_t *config, const char *value);
static int handle_route(config_t *config, const char *value);

/* Buffer configuration override handlers */
static int handle_buffer_size_override(config_t *config, const char *value);
static int handle_tx_ring_count(config_t *config, const char *value);
static int handle_rx_ring_count(config_t *config, const char *value);
static int handle_force_pio(config_t *config, const char *value);
static int handle_minimal_buffers(config_t *config, const char *value);
static int handle_optimal_buffers(config_t *config, const char *value);
static int handle_buffer_config(config_t *config, const char *value);

/* Helper functions */
static char* normalize_parameter_name(const char* param);
static int parse_hex_value(const char* value, unsigned int* result);
static int parse_network_address(const char* addr_str, uint32_t* network, uint32_t* netmask);
static int parse_ip_address(const char* addr_str, uint32_t* ip_addr);
static int stricmp(const char* s1, const char* s2);

/* Configuration parameters table */
static const config_param_t config_params[] = {
    /* Legacy parameters */
    {"DEBUG", handle_debug_level, "Debug level (0-3)"},
    {"XMS", handle_use_xms, "Use XMS memory (0/1)"},
    {"ROUTING", handle_enable_routing, "Enable routing (0/1)"},
    {"STATIC_ROUTING", handle_enable_static_routing, "Enable static routing (0/1)"},
    {"BUFFERS", handle_buffer_count, "Number of buffers (1-16)"},
    {"BUFSIZE", handle_buffer_size, "Buffer size in bytes"},
    {"INTVEC", handle_interrupt_vector, "Interrupt vector (hex)"},
    {"IOBASE", handle_io_base, "I/O base address (hex)"},
    {"IRQ", handle_irq, "IRQ number (2-15)"},
    {"STATS", handle_enable_stats, "Enable statistics (0/1)"},
    {"PROMISC", handle_promiscuous_mode, "Promiscuous mode (0/1)"},
    {"LOGGING", handle_enable_logging, "Enable logging (0/1)"},
    {"TEST", handle_test_mode, "Test mode (0/1)"},
    
    /* 3Com packet driver specific parameters */
    {"IO1", handle_io1_base, "First NIC I/O base address (0x200-0x3F0)"},
    {"IO2", handle_io2_base, "Second NIC I/O base address (0x200-0x3F0)"},
    {"IRQ1", handle_irq1, "First NIC IRQ (3,5,7,9,10,11,12,15)"},
    {"IRQ2", handle_irq2, "Second NIC IRQ (3,5,7,9,10,11,12,15)"},
    {"SPEED", handle_speed, "Network speed (10, 100, AUTO)"},
    {"BUSMASTER", handle_busmaster, "Bus mastering (ON, OFF, AUTO)"},
    {"PCI", handle_pci, "PCI support (ON, OFF, REQUIRED)"},
    {"LOG", handle_log, "Diagnostic logging (ON, OFF)"},
    {"ROUTE", handle_route, "Static route (network/mask,nic)"},
    
    /* Buffer configuration overrides */
    {"TXRING", handle_tx_ring_count, "TX ring size (4-32)"},
    {"RXRING", handle_rx_ring_count, "RX ring size (8-32)"},
    {"PIO", handle_force_pio, "Force PIO mode (no bus master)"},
    {"MINIMAL", handle_minimal_buffers, "Minimal 3KB buffer config"},
    {"OPTIMAL", handle_optimal_buffers, "Maximum performance config"},
    {"BUFFERS", handle_buffer_config, "Buffer config (size,tx,rx)"}
};

#define NUM_CONFIG_PARAMS (sizeof(config_params) / sizeof(config_params[0]))

/**
 * @brief Parse configuration parameters from CONFIG.SYS line
 * @param params Parameter string from CONFIG.SYS
 * @param config Configuration structure to populate
 * @return 0 on success, negative on error
 */
int config_parse_params(const char *params, config_t *config) {
    char *param_copy, *token, *value;
    char *saveptr = NULL;
    int i, found, result = 0;
    
    if (!config) {
        log_error("config_parse_params: NULL config parameter");
        return CONFIG_ERR_INVALID_PARAM;
    }
    
    /* Initialize with defaults */
    *config = default_config;
    
    if (!params || strlen(params) == 0) {
        log_info("No configuration parameters, using defaults");
        return 0;
    }
    
    log_info("Parsing configuration: %s", params);
    
    /* Make a copy of the parameter string for parsing */
    param_copy = malloc(strlen(params) + 1);
    if (!param_copy) {
        log_error("Failed to allocate memory for parameter parsing");
        return CONFIG_ERR_MEMORY;
    }
    strcpy(param_copy, params);
    
    /* Parse each parameter - support both /PARAM=VALUE and PARAM=VALUE formats */
    token = strtok(param_copy, " \t");
    while (token != NULL) {
        char* param_name;
        
        /* Skip leading slash if present */
        if (token[0] == '/') {
            param_name = token + 1;
        } else {
            param_name = token;
        }
        
        /* Find '=' separator */
        value = strchr(param_name, '=');
        if (value) {
            *value = '\0';
            value++;
        } else {
            value = "1"; /* Default value for flags */
        }
        
        /* Normalize parameter name (uppercase, handle aliases) */
        char* normalized_name = normalize_parameter_name(param_name);
        if (!normalized_name) {
            log_error("Failed to allocate memory for parameter name");
            free(param_copy);
            return CONFIG_ERR_MEMORY;
        }
        
        /* Find matching parameter */
        found = 0;
        for (i = 0; i < NUM_CONFIG_PARAMS; i++) {
            if (strcmp(normalized_name, config_params[i].name) == 0) {
                result = config_params[i].handler(config, value);
                if (result < 0) {
                    log_error("Error processing parameter %s=%s: %d", 
                             normalized_name, value, result);
                    free(normalized_name);
                    free(param_copy);
                    return result;
                }
                found = 1;
                break;
            }
        }
        
        if (!found) {
            log_warning("Unknown configuration parameter: %s", normalized_name);
        }
        
        free(normalized_name);
        token = strtok(NULL, " \t");
    }
    
    free(param_copy);
    
    /* Validate configuration */
    result = config_validate(config);
    if (result < 0) {
        log_error("Configuration validation failed: %d", result);
        return result;
    }
    
    /* Perform cross-parameter validation */
    result = config_validate_cross_parameters(config);
    if (result < 0) {
        log_error("Cross-parameter validation failed: %d", result);
        return result;
    }
    
    log_info("Configuration parsed successfully");
    return 0;
}

/**
 * @brief Validate configuration parameters
 * @param config Configuration to validate
 * @return 0 on success, negative on error
 */
int config_validate(const config_t *config) {
    if (!config) {
        return CONFIG_ERR_INVALID_PARAM;
    }
    
    log_debug("Validating configuration");
    
    /* Validate debug level */
    if (config->debug_level > 3) {
        log_error("Invalid debug level: %d (max 3)", config->debug_level);
        return CONFIG_ERR_INVALID_VALUE;
    }
    
    /* Validate buffer configuration */
    if (config->buffer_count < 1 || config->buffer_count > 16) {
        log_error("Invalid buffer count: %d (range 1-16)", config->buffer_count);
        return CONFIG_ERR_INVALID_VALUE;
    }
    
    if (config->buffer_size < 64 || config->buffer_size > 65536) {
        log_error("Invalid buffer size: %d (range 64-65536)", config->buffer_size);
        return CONFIG_ERR_INVALID_VALUE;
    }
    
    /* Validate legacy IRQ */
    if (config->irq < 2 || config->irq > 15) {
        log_error("Invalid legacy IRQ: %d (range 2-15)", config->irq);
        return CONFIG_ERR_INVALID_VALUE;
    }
    
    /* Validate legacy I/O base address */
    if (config->io_base < 0x200 || config->io_base > 0x3FF) {
        log_warning("Unusual legacy I/O base address: 0x%04X", config->io_base);
    }
    
    /* Validate 3Com packet driver specific parameters */
    
    /* Validate I/O addresses */
    if (!config_is_valid_io_address(config->io1_base)) {
        log_error("Invalid IO1 base address: 0x%04X (range 0x%04X-0x%04X)", 
                 config->io1_base, CONFIG_MIN_IO_BASE, CONFIG_MAX_IO_BASE);
        return CONFIG_ERR_INVALID_IO_RANGE;
    }
    
    if (!config_is_valid_io_address(config->io2_base)) {
        log_error("Invalid IO2 base address: 0x%04X (range 0x%04X-0x%04X)", 
                 config->io2_base, CONFIG_MIN_IO_BASE, CONFIG_MAX_IO_BASE);
        return CONFIG_ERR_INVALID_IO_RANGE;
    }
    
    /* Check I/O address conflicts */
    if (!config_check_io_conflict(config->io1_base, config->io2_base)) {
        log_error("I/O address conflict: IO1=0x%04X and IO2=0x%04X overlap", 
                 config->io1_base, config->io2_base);
        return CONFIG_ERR_IO_CONFLICT;
    }
    
    /* Validate IRQ numbers */
    if (!config_is_valid_irq_number(config->irq1)) {
        log_error("Invalid IRQ1: %d (valid: 3,5,7,9,10,11,12,15)", config->irq1);
        return CONFIG_ERR_INVALID_IRQ_RANGE;
    }
    
    if (!config_is_valid_irq_number(config->irq2)) {
        log_error("Invalid IRQ2: %d (valid: 3,5,7,9,10,11,12,15)", config->irq2);
        return CONFIG_ERR_INVALID_IRQ_RANGE;
    }
    
    /* Check IRQ conflicts */
    if (!config_check_irq_conflict(config->irq1, config->irq2)) {
        log_error("IRQ conflict: IRQ1=%d and IRQ2=%d are the same", 
                 config->irq1, config->irq2);
        return CONFIG_ERR_IRQ_CONFLICT;
    }
    
    /* Validate network speed */
    if (config->speed != SPEED_AUTO && config->speed != SPEED_10 && config->speed != SPEED_100) {
        log_error("Invalid network speed: %d (valid: 10, 100, AUTO)", config->speed);
        return CONFIG_ERR_INVALID_SPEED;
    }
    
    /* Validate route count */
    if (config->route_count > MAX_ROUTES) {
        log_error("Too many routes: %d (max %d)", config->route_count, MAX_ROUTES);
        return CONFIG_ERR_TOO_MANY_ROUTES;
    }
    
    log_debug("Configuration validation passed");
    return 0;
}

/**
 * @brief Get default configuration
 * @param config Configuration structure to populate
 * @return 0 on success, negative on error
 */
int config_get_defaults(config_t *config) {
    if (!config) {
        return CONFIG_ERR_INVALID_PARAM;
    }
    
    *config = default_config;
    log_debug("Loaded default configuration");
    
    return 0;
}

/**
 * @brief Print configuration to log
 * @param config Configuration to print
 * @param level Log level to use
 */
void config_print(const config_t *config, int level) {
    if (!config) {
        return;
    }
    
    log_at_level(level, "Configuration:");
    log_at_level(level, "  Debug Level: %d", config->debug_level);
    log_at_level(level, "  Use XMS: %d", config->use_xms);
    log_at_level(level, "  Enable Routing: %d", config->enable_routing);
    log_at_level(level, "  Enable Static Routing: %d", config->enable_static_routing);
    log_at_level(level, "  Buffer Count: %d", config->buffer_count);
    log_at_level(level, "  Buffer Size: %d", config->buffer_size);
    log_at_level(level, "  Interrupt Vector: 0x%02X", config->interrupt_vector);
    log_at_level(level, "  I/O Base (legacy): 0x%04X", config->io_base);
    log_at_level(level, "  IRQ (legacy): %d", config->irq);
    log_at_level(level, "  Enable Stats: %d", config->enable_stats);
    log_at_level(level, "  Promiscuous Mode: %d", config->promiscuous_mode);
    log_at_level(level, "  Enable Logging: %d", config->enable_logging);
    log_at_level(level, "  Test Mode: %d", config->test_mode);
    
    /* 3Com packet driver specific settings */
    log_at_level(level, "  IO1 Base: 0x%04X", config->io1_base);
    log_at_level(level, "  IO2 Base: 0x%04X", config->io2_base);
    log_at_level(level, "  IRQ1: %d", config->irq1);
    log_at_level(level, "  IRQ2: %d", config->irq2);
    
    const char* speed_str;
    switch (config->speed) {
        case SPEED_AUTO: speed_str = "AUTO"; break;
        case SPEED_10: speed_str = "10 Mbps"; break;
        case SPEED_100: speed_str = "100 Mbps"; break;
        default: speed_str = "Unknown"; break;
    }
    log_at_level(level, "  Network Speed: %s", speed_str);
    
    const char* busmaster_str;
    switch (config->busmaster) {
        case BUSMASTER_OFF: busmaster_str = "OFF"; break;
        case BUSMASTER_ON: busmaster_str = "ON"; break;
        case BUSMASTER_AUTO: busmaster_str = "AUTO"; break;
        default: busmaster_str = "Unknown"; break;
    }
    log_at_level(level, "  Bus Mastering: %s", busmaster_str);
    log_at_level(level, "  Logging: %s", config->log_enabled ? "ON" : "OFF");
    
    if (config->route_count > 0) {
        int i;
        log_at_level(level, "  Static Routes (%d):", config->route_count);
        for (i = 0; i < config->route_count; i++) {
            const route_entry_t* route = &config->routes[i];
            if (route->active) {
                log_at_level(level, "    %d.%d.%d.%d/%d -> NIC %d",
                           (route->network >> 24) & 0xFF,
                           (route->network >> 16) & 0xFF,
                           (route->network >> 8) & 0xFF,
                           route->network & 0xFF,
                           __builtin_popcount(route->netmask),
                           route->nic_id);
            }
        }
    }
}

/* Legacy parameter handler implementations */

static int handle_debug_level(config_t *config, const char *value) {
    int level = atoi(value);
    if (level < 0 || level > 3) {
        return CONFIG_ERR_INVALID_VALUE;
    }
    config->debug_level = level;
    return 0;
}

static int handle_use_xms(config_t *config, const char *value) {
    config->use_xms = (atoi(value) != 0);
    return 0;
}

static int handle_enable_routing(config_t *config, const char *value) {
    config->enable_routing = (atoi(value) != 0);
    return 0;
}

static int handle_enable_static_routing(config_t *config, const char *value) {
    config->enable_static_routing = (atoi(value) != 0);
    return 0;
}

static int handle_buffer_count(config_t *config, const char *value) {
    int count = atoi(value);
    if (count < 1 || count > 16) {
        return CONFIG_ERR_INVALID_VALUE;
    }
    config->buffer_count = count;
    return 0;
}

static int handle_buffer_size(config_t *config, const char *value) {
    int size = atoi(value);
    if (size < 64 || size > 65536) {
        return CONFIG_ERR_INVALID_VALUE;
    }
    config->buffer_size = size;
    return 0;
}

static int handle_interrupt_vector(config_t *config, const char *value) {
    unsigned int vector;
    if (sscanf(value, "%x", &vector) != 1 || vector > 0xFF) {
        return CONFIG_ERR_INVALID_VALUE;
    }
    config->interrupt_vector = (uint8_t)vector;
    return 0;
}

static int handle_io_base(config_t *config, const char *value) {
    unsigned int base;
    if (sscanf(value, "%x", &base) != 1 || base > 0xFFFF) {
        return CONFIG_ERR_INVALID_VALUE;
    }
    config->io_base = (uint16_t)base;
    return 0;
}

static int handle_irq(config_t *config, const char *value) {
    int irq = atoi(value);
    if (irq < 2 || irq > 15) {
        return CONFIG_ERR_INVALID_VALUE;
    }
    config->irq = irq;
    return 0;
}

static int handle_enable_stats(config_t *config, const char *value) {
    config->enable_stats = (atoi(value) != 0);
    return 0;
}

static int handle_promiscuous_mode(config_t *config, const char *value) {
    config->promiscuous_mode = (atoi(value) != 0);
    return 0;
}

static int handle_enable_logging(config_t *config, const char *value) {
    config->enable_logging = (atoi(value) != 0);
    return 0;
}

static int handle_test_mode(config_t *config, const char *value) {
    config->test_mode = (atoi(value) != 0);
    return 0;
}

/* 3Com packet driver specific parameter handlers */

static int handle_io1_base(config_t *config, const char *value) {
    unsigned int base;
    if (parse_hex_value(value, &base) != 0 || !config_is_valid_io_address((uint16_t)base)) {
        return CONFIG_ERR_INVALID_VALUE;
    }
    config->io1_base = (uint16_t)base;
    config->io_base = (uint16_t)base; /* Update legacy field too */
    return 0;
}

static int handle_io2_base(config_t *config, const char *value) {
    unsigned int base;
    if (parse_hex_value(value, &base) != 0 || !config_is_valid_io_address((uint16_t)base)) {
        return CONFIG_ERR_INVALID_VALUE;
    }
    config->io2_base = (uint16_t)base;
    return 0;
}

static int handle_irq1(config_t *config, const char *value) {
    int irq = atoi(value);
    if (!config_is_valid_irq_number((uint8_t)irq)) {
        return CONFIG_ERR_INVALID_VALUE;
    }
    config->irq1 = (uint8_t)irq;
    config->irq = (uint8_t)irq; /* Update legacy field too */
    return 0;
}

static int handle_irq2(config_t *config, const char *value) {
    int irq = atoi(value);
    if (!config_is_valid_irq_number((uint8_t)irq)) {
        return CONFIG_ERR_INVALID_VALUE;
    }
    config->irq2 = (uint8_t)irq;
    return 0;
}

static int handle_speed(config_t *config, const char *value) {
    if (stricmp(value, "AUTO") == 0) {
        config->speed = SPEED_AUTO;
    } else if (stricmp(value, "10") == 0) {
        config->speed = SPEED_10;
    } else if (stricmp(value, "100") == 0) {
        config->speed = SPEED_100;
    } else {
        return CONFIG_ERR_INVALID_VALUE;
    }
    return 0;
}

static int handle_busmaster(config_t *config, const char *value) {
    if (stricmp(value, "ON") == 0) {
        config->busmaster = BUSMASTER_ON;
    } else if (stricmp(value, "OFF") == 0) {
        config->busmaster = BUSMASTER_OFF;
    } else if (stricmp(value, "AUTO") == 0) {
        config->busmaster = BUSMASTER_AUTO;
    } else {
        return CONFIG_ERR_INVALID_VALUE;
    }
    return 0;
}

static int handle_pci(config_t *config, const char *value) {
    if (stricmp(value, "ON") == 0 || stricmp(value, "ENABLED") == 0) {
        config->pci = PCI_ENABLED;
    } else if (stricmp(value, "OFF") == 0 || stricmp(value, "DISABLED") == 0) {
        config->pci = PCI_DISABLED;
    } else if (stricmp(value, "REQUIRED") == 0) {
        config->pci = PCI_REQUIRED;
    } else {
        return CONFIG_ERR_INVALID_VALUE;
    }
    return 0;
}

static int handle_log(config_t *config, const char *value) {
    if (stricmp(value, "ON") == 0) {
        config->log_enabled = true;
        config->enable_logging = 1; /* Update legacy field */
    } else if (stricmp(value, "OFF") == 0) {
        config->log_enabled = false;
        config->enable_logging = 0; /* Update legacy field */
    } else {
        return CONFIG_ERR_INVALID_VALUE;
    }
    return 0;
}

static int handle_route(config_t *config, const char *value) {
    if (config->route_count >= MAX_ROUTES) {
        return CONFIG_ERR_TOO_MANY_ROUTES;
    }
    
    route_entry_t* route = &config->routes[config->route_count];
    int result = config_parse_route_entry(value, route);
    if (result == 0) {
        config->route_count++;
        config->enable_static_routing = 1; /* Enable static routing */
    }
    return result;
}

/* Buffer configuration override handlers */

static int handle_buffer_size_override(config_t *config, const char *value) {
    int size = atoi(value);
    if (size != 256 && size != 512 && size != 1024 && size != 1536) {
        log_error("Buffer size must be 256, 512, 1024, or 1536");
        return CONFIG_ERR_INVALID_VALUE;
    }
    config->override_buffer_size = (uint16_t)size;
    /* Note: Setting BUFSIZE now refers to override, not legacy buffer_size */
    return 0;
}

static int handle_tx_ring_count(config_t *config, const char *value) {
    int count = atoi(value);
    if (count < 4 || count > 32) {
        log_error("TX ring count must be between 4 and 32");
        return CONFIG_ERR_INVALID_VALUE;
    }
    config->override_tx_ring_count = (uint8_t)count;
    return 0;
}

static int handle_rx_ring_count(config_t *config, const char *value) {
    int count = atoi(value);
    if (count < 8 || count > 32) {
        log_error("RX ring count must be between 8 and 32");
        return CONFIG_ERR_INVALID_VALUE;
    }
    config->override_rx_ring_count = (uint8_t)count;
    return 0;
}

static int handle_force_pio(config_t *config, const char *value) {
    /* PIO can be specified without value or with ON/OFF */
    if (!value || stricmp(value, "ON") == 0 || strcmp(value, "1") == 0) {
        config->force_pio_mode = 1;
        log_info("Forcing PIO mode (bus master disabled)");
    } else if (stricmp(value, "OFF") == 0 || strcmp(value, "0") == 0) {
        config->force_pio_mode = 0;
    } else {
        return CONFIG_ERR_INVALID_VALUE;
    }
    return 0;
}

static int handle_minimal_buffers(config_t *config, const char *value) {
    /* MINIMAL can be specified without value or with ON/OFF */
    if (!value || stricmp(value, "ON") == 0 || strcmp(value, "1") == 0) {
        config->force_minimal_buffers = 1;
        log_info("Forcing minimal 3KB buffer configuration");
    } else if (stricmp(value, "OFF") == 0 || strcmp(value, "0") == 0) {
        config->force_minimal_buffers = 0;
    } else {
        return CONFIG_ERR_INVALID_VALUE;
    }
    return 0;
}

static int handle_optimal_buffers(config_t *config, const char *value) {
    /* OPTIMAL can be specified without value or with ON/OFF */
    if (!value || stricmp(value, "ON") == 0 || strcmp(value, "1") == 0) {
        config->force_optimal_buffers = 1;
        log_info("Forcing optimal buffer configuration");
    } else if (stricmp(value, "OFF") == 0 || strcmp(value, "0") == 0) {
        config->force_optimal_buffers = 0;
    } else {
        return CONFIG_ERR_INVALID_VALUE;
    }
    return 0;
}

static int handle_buffer_config(config_t *config, const char *value) {
    /* Parse format: size,tx,rx */
    int size, tx, rx;
    
    if (sscanf(value, "%d,%d,%d", &size, &tx, &rx) != 3) {
        log_error("BUFFERS format: size,tx,rx (e.g., 1024,16,16)");
        return CONFIG_ERR_INVALID_VALUE;
    }
    
    /* Validate size */
    if (size != 256 && size != 512 && size != 1024 && size != 1536) {
        log_error("Buffer size must be 256, 512, 1024, or 1536");
        return CONFIG_ERR_INVALID_VALUE;
    }
    
    /* Validate TX count */
    if (tx < 4 || tx > 32) {
        log_error("TX ring count must be between 4 and 32");
        return CONFIG_ERR_INVALID_VALUE;
    }
    
    /* Validate RX count */
    if (rx < 8 || rx > 32) {
        log_error("RX ring count must be between 8 and 32");
        return CONFIG_ERR_INVALID_VALUE;
    }
    
    config->override_buffer_size = (uint16_t)size;
    config->override_tx_ring_count = (uint8_t)tx;
    config->override_rx_ring_count = (uint8_t)rx;
    
    log_info("Buffer config override: %dB x %d TX, %d RX", size, tx, rx);
    return 0;
}

/* Helper functions */

static char* normalize_parameter_name(const char* param) {
    char* normalized = malloc(strlen(param) + 1);
    if (!normalized) {
        return NULL;
    }
    
    /* Convert to uppercase */
    int i;
    for (i = 0; param[i]; i++) {
        normalized[i] = toupper(param[i]);
    }
    normalized[i] = '\0';
    
    return normalized;
}

static int parse_hex_value(const char* value, unsigned int* result) {
    char* endptr;
    
    /* Handle 0x prefix */
    if (strncmp(value, "0x", 2) == 0 || strncmp(value, "0X", 2) == 0) {
        *result = strtoul(value, &endptr, 16);
    } else {
        /* Try hex first, then decimal */
        *result = strtoul(value, &endptr, 16);
        if (*endptr != '\0') {
            *result = strtoul(value, &endptr, 10);
        }
    }
    
    return (*endptr == '\0') ? 0 : -1;
}

static int parse_network_address(const char* addr_str, uint32_t* network, uint32_t* netmask) {
    char* addr_copy = malloc(strlen(addr_str) + 1);
    if (!addr_copy) {
        return CONFIG_ERR_MEMORY;
    }
    strcpy(addr_copy, addr_str);
    
    /* Split network/mask */
    char* mask_str = strchr(addr_copy, '/');
    if (!mask_str) {
        free(addr_copy);
        return CONFIG_ERR_ROUTE_SYNTAX;
    }
    *mask_str = '\0';
    mask_str++;
    
    /* Parse network address (simplified - would need proper IP parsing in real implementation) */
    unsigned int a, b, c, d;
    if (sscanf(addr_copy, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 ||
        a > 255 || b > 255 || c > 255 || d > 255) {
        free(addr_copy);
        return CONFIG_ERR_ROUTE_SYNTAX;
    }
    *network = (a << 24) | (b << 16) | (c << 8) | d;
    
    /* Parse netmask (CIDR notation) */
    int cidr = atoi(mask_str);
    if (cidr < 0 || cidr > 32) {
        free(addr_copy);
        return CONFIG_ERR_ROUTE_SYNTAX;
    }
    
    if (cidr == 0) {
        *netmask = 0;
    } else {
        *netmask = ~((1UL << (32 - cidr)) - 1);
    }
    
    free(addr_copy);
    return 0;
}

static int parse_ip_address(const char* addr_str, uint32_t* ip_addr) {
    if (!addr_str || !ip_addr) {
        return CONFIG_ERR_INVALID_PARAM;
    }
    
    /* Parse IP address in dotted decimal notation */
    unsigned int a, b, c, d;
    if (sscanf(addr_str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 ||
        a > 255 || b > 255 || c > 255 || d > 255) {
        return CONFIG_ERR_ROUTE_SYNTAX;
    }
    
    *ip_addr = (a << 24) | (b << 16) | (c << 8) | d;
    return 0;
}

/* Configuration validation helper functions */

bool config_is_valid_io_address(uint16_t io_base) {
    return (io_base >= CONFIG_MIN_IO_BASE && io_base <= CONFIG_MAX_IO_BASE &&
            (io_base & 0x1F) == 0); /* Must be aligned to 32-byte boundary */
}

bool config_is_valid_irq_number(uint8_t irq) {
    return (CONFIG_VALID_IRQS & (1 << irq)) != 0;
}

bool config_check_io_conflict(uint16_t io1, uint16_t io2) {
    if (io1 == io2) {
        return false; /* Same address is a conflict */
    }
    
    /* Check if ranges overlap (each NIC uses 32 bytes) */
    uint16_t io1_end = io1 + CONFIG_IO_RANGE_SIZE - 1;
    uint16_t io2_end = io2 + CONFIG_IO_RANGE_SIZE - 1;
    
    return !(io1 <= io2_end && io2 <= io1_end);
}

bool config_check_irq_conflict(uint8_t irq1, uint8_t irq2) {
    return irq1 != irq2;
}

bool config_cpu_supports_busmaster(void) {
    /* Updated to allow 286+ systems to attempt bus mastering with testing */
    if (g_cpu_info.type >= CPU_TYPE_80286) {
        return true;
    }
    return false;
}

int config_parse_route_entry(const char* route_str, route_entry_t* route) {
    if (!route_str || !route) {
        return CONFIG_ERR_INVALID_PARAM;
    }
    
    /* Parse format: network/mask,nic[,gateway] */
    char* route_copy = malloc(strlen(route_str) + 1);
    if (!route_copy) {
        return CONFIG_ERR_MEMORY;
    }
    strcpy(route_copy, route_str);
    
    /* Find first comma (before NIC) */
    char* nic_str = strrchr(route_copy, ',');
    if (!nic_str) {
        free(route_copy);
        return CONFIG_ERR_ROUTE_SYNTAX;
    }
    *nic_str = '\0';
    nic_str++;
    
    /* Check for gateway (third parameter) */
    char* gateway_str = strchr(nic_str, ',');
    if (gateway_str) {
        *gateway_str = '\0';
        gateway_str++;
    }
    
    /* Parse network address */
    int result = parse_network_address(route_copy, &route->network, &route->netmask);
    if (result != 0) {
        free(route_copy);
        return result;
    }
    
    /* Parse NIC ID */
    int nic_id = atoi(nic_str);
    if (nic_id < 0 || nic_id >= MAX_NICS) {
        free(route_copy);
        return CONFIG_ERR_ROUTE_SYNTAX;
    }
    route->nic_id = (uint8_t)nic_id;
    route->active = true;
    
    /* Add route to static routing system */
    if (static_routing_is_enabled()) {
        ip_addr_t dest_network, netmask, gateway;
        
        /* Convert from uint32_t to ip_addr_t */
        ip_addr_from_uint32(&dest_network, route->network);
        ip_addr_from_uint32(&netmask, route->netmask);
        
        if (gateway_str) {
            /* Parse gateway address */
            uint32_t gw_addr;
            if (parse_ip_address(gateway_str, &gw_addr) == 0) {
                ip_addr_from_uint32(&gateway, gw_addr);
                result = static_route_add(&dest_network, &netmask, &gateway, nic_id, 1);
            } else {
                free(route_copy);
                return CONFIG_ERR_ROUTE_SYNTAX;
            }
        } else {
            /* Direct route (no gateway) */
            ip_addr_set(&gateway, 0, 0, 0, 0);
            result = static_route_add(&dest_network, &netmask, NULL, nic_id, 1);
        }
        
        if (result != SUCCESS) {
            log_warning("Failed to add static route: %d", result);
        } else {
            log_info("Added static route: %d.%d.%d.%d/%d.%d.%d.%d via NIC %d",
                    dest_network.addr[0], dest_network.addr[1], dest_network.addr[2], dest_network.addr[3],
                    netmask.addr[0], netmask.addr[1], netmask.addr[2], netmask.addr[3],
                    nic_id);
        }
    }
    
    free(route_copy);
    return 0;
}

int config_validate_cross_parameters(const config_t* config) {
    if (!config) {
        return CONFIG_ERR_INVALID_PARAM;
    }
    
    /* Validate busmaster setting against CPU capability */
    if (config->busmaster == BUSMASTER_ON && !config_cpu_supports_busmaster()) {
        log_error("Bus mastering requires 386+ CPU, but %s detected", 
                 cpu_type_to_string(g_cpu_info.type));
        return CONFIG_ERR_CPU_REQUIRED;
    }
    
    /* Additional cross-parameter validations can be added here */
    
    return 0;
}

/**
 * @brief Perform automated bus mastering capability testing when BUSMASTER=AUTO
 * 
 * This function is called when the configuration specifies BUSMASTER=AUTO.
 * It runs the comprehensive 45-second testing framework to determine if
 * bus mastering should be enabled based on system compatibility.
 * 
 * @param config Configuration structure to update
 * @param ctx NIC context for testing
 * @param quick_mode Use quick 10-second test instead of full 45-second test
 * @return 0 on success, negative on error
 */
int config_perform_busmaster_auto_test(config_t *config, nic_context_t *ctx, bool quick_mode) {
    if (!config || !ctx) {
        log_error("config_perform_busmaster_auto_test: NULL parameters");
        return CONFIG_ERR_INVALID_PARAM;
    }
    
    /* Only perform testing for 3C515-TX NICs */
    if (ctx->nic_info.type != NIC_TYPE_3C515_TX) {
        log_info("Bus mastering not supported on %s - using programmed I/O", 
                (ctx->nic_info.type == NIC_TYPE_3C509B) ? "3C509B" : "Unknown NIC");
        config->busmaster = BUSMASTER_OFF;
        return 0;
    }
    
    /* Check CPU compatibility - now supports 286+ systems */
    if (!config_cpu_supports_busmaster()) {
        log_info("CPU does not support bus mastering - using programmed I/O");
        config->busmaster = BUSMASTER_OFF;
        return 0;
    }
    
    /* CPU-specific information */
    bool is_286_system = cpu_requires_conservative_testing();
    uint16_t cpu_threshold = get_cpu_appropriate_confidence_threshold();
    
    log_info("=== CPU-Aware Bus Mastering Configuration ===");
    log_info("Detected: %s CPU", 
             (g_cpu_info.type == CPU_TYPE_80286) ? "80286" :
             (g_cpu_info.type == CPU_TYPE_80386) ? "80386" :
             (g_cpu_info.type == CPU_TYPE_80486) ? "80486" :
             (g_cpu_info.type >= CPU_TYPE_PENTIUM) ? "Pentium+" : "Unknown");
    
    /* Step 1: Try to load cached results */
    busmaster_test_cache_t cached_results;
    cache_validation_info_t validation;
    busmaster_test_results_t test_results;
    
    if (load_busmaster_test_cache(ctx, &cached_results) == 0) {
        /* Cache found - validate it */
        if (validate_busmaster_test_cache(ctx, &cached_results, &validation) == 0) {
            /* Cache is valid - use cached results */
            log_info("Using cached bus mastering test results");
            cache_to_test_results(&cached_results, &test_results);
            
            /* Apply configuration and exit */
            int apply_result = apply_busmaster_configuration(ctx, &test_results, config);
            if (apply_result == 0) {
                log_info("Bus mastering configured from cache: %s", 
                         (config->busmaster == BUSMASTER_ON) ? "ENABLED" : "DISABLED");
            }
            return apply_result;
        } else {
            /* Cache invalid - explain why and proceed with testing */
            log_info("Cached results invalid: %s", validation.invalidation_reason);
        }
    } else {
        log_info("No cached test results found");
    }
    
    /* Step 2: Perform CPU-appropriate testing */
    if (is_286_system) {
        /* 286 System: Conservative approach */
        log_info("80286 system detected - conservative testing required for bus mastering");
        log_info("Quick test (10s) will run first, exhaustive test (45s) required for bus mastering");
        
        /* Always start with quick test */
        if (perform_cpu_aware_testing(ctx, config, &test_results, true) != 0) {
            return fallback_to_programmed_io(ctx, config, "Quick test failed");
        }
        
        /* If quick test passed but confidence too low for 286, prompt for exhaustive test */
        if (test_results.confidence_score < cpu_threshold) {
            if (!quick_mode && prompt_user_for_exhaustive_test()) {
                log_info("Running exhaustive 45-second test for 80286 bus mastering validation...");
                if (perform_cpu_aware_testing(ctx, config, &test_results, false) != 0) {
                    return fallback_to_programmed_io(ctx, config, "Exhaustive test failed");
                }
            } else {
                log_info("80286 system requires exhaustive test for bus mastering - using PIO mode");
                config->busmaster = BUSMASTER_OFF;
                save_busmaster_test_cache(ctx, &test_results); /* Cache the PIO decision */
                return 0;
            }
        }
        
    } else {
        /* 386+ System: Streamlined approach */
        log_info("80386+ system detected - quick test sufficient for bus mastering");
        
        /* Quick test is sufficient for 386+ systems */
        busmaster_test_mode_t test_mode = quick_mode ? BM_TEST_MODE_QUICK : BM_TEST_MODE_FULL;
        log_info("Running %s test (user preference)...", 
                 quick_mode ? "quick 10-second" : "comprehensive 45-second");
                 
        if (perform_cpu_aware_testing(ctx, config, &test_results, quick_mode) != 0) {
            return fallback_to_programmed_io(ctx, config, "Test failed");
        }
    }
    
    /* Step 3: Apply configuration based on results */
    int apply_result = apply_busmaster_configuration(ctx, &test_results, config);
    if (apply_result != 0) {
        log_error("Failed to apply bus mastering configuration");
        return apply_result;
    }
    
    /* Step 4: Cache the results for future boots */
    if (save_busmaster_test_cache(ctx, &test_results) == 0) {
        log_info("Test results cached - subsequent boots will be faster");
    }
    
    log_info("=== Bus Mastering Auto-Configuration Complete ===");
    log_info("Final Configuration: %s (Confidence: %s, Score: %u/%u)", 
             (config->busmaster == BUSMASTER_ON) ? "Bus Mastering ENABLED" : "Programmed I/O MODE",
             (test_results.confidence_level == BM_CONFIDENCE_HIGH) ? "HIGH" :
             (test_results.confidence_level == BM_CONFIDENCE_MEDIUM) ? "MEDIUM" :
             (test_results.confidence_level == BM_CONFIDENCE_LOW) ? "LOW" : "FAILED",
             test_results.confidence_score, BM_SCORE_TOTAL_MAX);
    
    return 0;
}

/**
 * @brief Perform CPU-aware testing with proper initialization and cleanup
 */
static int perform_cpu_aware_testing(nic_context_t *ctx, config_t *config, 
                                   busmaster_test_results_t *results, bool quick_mode) {
    /* Initialize the testing framework */
    if (busmaster_test_init(ctx) != 0) {
        log_error("Failed to initialize bus mastering test framework");
        return -1;
    }
    
    /* Perform the test */
    busmaster_test_mode_t test_mode = quick_mode ? BM_TEST_MODE_QUICK : BM_TEST_MODE_FULL;
    int test_result = perform_automated_busmaster_test(ctx, test_mode, results);
    
    /* Log detailed test results */
    log_info("Bus mastering test completed:");
    log_info("  Total Score: %u/%u (%.1f%%)", 
             results->confidence_score, BM_SCORE_TOTAL_MAX,
             (results->confidence_score * 100.0) / BM_SCORE_TOTAL_MAX);
    log_info("  Confidence Level: %s",
             (results->confidence_level == BM_CONFIDENCE_HIGH) ? "HIGH" :
             (results->confidence_level == BM_CONFIDENCE_MEDIUM) ? "MEDIUM" :
             (results->confidence_level == BM_CONFIDENCE_LOW) ? "LOW" : "FAILED");
    log_info("  Individual Scores: DMA=%u/70, Memory=%u/80, Timing=%u/100", 
             results->dma_controller_score, results->memory_coherency_score, 
             results->timing_constraints_score);
    log_info("  Pattern Tests: Data=%u/85, Burst=%u/82, Recovery=%u/85", 
             results->data_integrity_score, results->burst_transfer_score, 
             results->error_recovery_score);
    if (!quick_mode) {
        log_info("  Stability Test: %u/50", results->stability_score);
    }
    
    /* Cleanup test framework */
    busmaster_test_cleanup(ctx);
    
    return test_result;
}

/**
 * @brief Prompt user for exhaustive test on 286 systems
 */
static bool prompt_user_for_exhaustive_test(void) {
    /* For DOS, we'll use a simple prompt */
    printf("\n80286 system requires 45-second exhaustive test for bus mastering.\n");
    printf("[E]xhaustive test (recommended) or [S]kip (use PIO): ");
    
    char response = getchar();
    getchar(); /* consume newline */
    
    return (response == 'E' || response == 'e');
}

/**
 * @brief Apply bus mastering configuration based on test results
 */
int apply_busmaster_configuration(nic_context_t *ctx, 
                                const busmaster_test_results_t *results,
                                config_t *config) {
    if (!ctx || !results || !config) {
        log_error("apply_busmaster_configuration: NULL parameters");
        return CONFIG_ERR_INVALID_PARAM;
    }
    
    /* Apply configuration based on confidence level */
    switch (results->confidence_level) {
        case BM_CONFIDENCE_HIGH:
            config->busmaster = BUSMASTER_ON;
            log_info("HIGH confidence - Bus mastering ENABLED");
            log_info("System shows excellent compatibility for bus mastering");
            break;
            
        case BM_CONFIDENCE_MEDIUM:
            config->busmaster = BUSMASTER_ON;
            log_info("MEDIUM confidence - Bus mastering ENABLED with monitoring");
            log_warning("Monitor system for stability issues");
            break;
            
        case BM_CONFIDENCE_LOW:
            config->busmaster = BUSMASTER_OFF;
            log_warning("LOW confidence - Bus mastering DISABLED");
            log_warning("System compatibility questionable - using programmed I/O for safety");
            return fallback_to_programmed_io(ctx, config, "Low confidence score");
            
        case BM_CONFIDENCE_FAILED:
        default:
            config->busmaster = BUSMASTER_OFF;
            log_error("Test FAILED - Bus mastering DISABLED");
            log_error("System not compatible with bus mastering - using programmed I/O");
            return fallback_to_programmed_io(ctx, config, results->failure_reason);
    }
    
    return 0;
}

/**
 * @brief Generate detailed test report
 */
int generate_busmaster_test_report(const busmaster_test_results_t *results,
                                 char *buffer, size_t buffer_size) {
    if (!results || !buffer || buffer_size == 0) {
        return CONFIG_ERR_INVALID_PARAM;
    }
    
    int written = snprintf(buffer, buffer_size,
        "Bus Mastering Capability Test Report\n"
        "====================================\n"
        "Overall Score: %u/%u points (%.1f%%)\n"
        "Confidence Level: %s\n"
        "Test Duration: %lu ms\n"
        "\n"
        "Individual Test Scores:\n"
        "  DMA Controller Presence: %u/70 pts\n"
        "  Memory Coherency: %u/80 pts\n"
        "  Timing Constraints: %u/100 pts\n"
        "  Data Integrity Patterns: %u/85 pts\n"
        "  Burst Transfer Capability: %u/82 pts\n"
        "  Error Recovery Mechanisms: %u/85 pts\n"
        "  Long Duration Stability: %u/50 pts\n"
        "\n"
        "System Compatibility:\n"
        "  CPU Supports Bus Mastering: %s\n"
        "  Chipset Compatible: %s\n"
        "  DMA Controller Present: %s\n"
        "  Memory Coherent: %s\n"
        "\n"
        "Performance Metrics:\n"
        "  Transfers Completed: %lu\n"
        "  Bytes Transferred: %lu\n"
        "  Error Count: %u\n"
        "  Success Rate: %.1f%%\n"
        "\n"
        "Recommendations:\n"
        "%s\n"
        "\n"
        "Safe for Production: %s\n"
        "Requires Fallback: %s\n",
        results->confidence_score, BM_SCORE_TOTAL_MAX,
        (results->confidence_score * 100.0) / BM_SCORE_TOTAL_MAX,
        (results->confidence_level == BM_CONFIDENCE_HIGH) ? "HIGH" :
        (results->confidence_level == BM_CONFIDENCE_MEDIUM) ? "MEDIUM" :
        (results->confidence_level == BM_CONFIDENCE_LOW) ? "LOW" : "FAILED",
        results->test_duration_ms,
        results->dma_controller_score,
        results->memory_coherency_score,
        results->timing_constraints_score,
        results->data_integrity_score,
        results->burst_transfer_score,
        results->error_recovery_score,
        results->stability_score,
        results->cpu_supports_busmaster ? "YES" : "NO",
        results->chipset_compatible ? "YES" : "NO",
        results->dma_controller_present ? "YES" : "NO",
        results->memory_coherent ? "YES" : "NO",
        results->transfers_completed,
        results->bytes_transferred,
        results->error_count,
        (results->transfers_completed > 0) ? 
            ((results->transfers_completed * 100.0) / (results->transfers_completed + results->error_count)) : 0.0,
        results->recommendations,
        results->safe_for_production ? "YES" : "NO",
        results->requires_fallback ? "YES" : "NO"
    );
    
    return (written > 0 && written < (int)buffer_size) ? 0 : CONFIG_ERR_MEMORY;
}

/* Case-insensitive string comparison (DOS-compatible) */
static int stricmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        char c1 = toupper(*s1);
        char c2 = toupper(*s2);
        if (c1 != c2) {
            return c1 - c2;
        }
        s1++;
        s2++;
    }
    return toupper(*s1) - toupper(*s2);
}

/* Restore default code segment */
#pragma code_seg()

