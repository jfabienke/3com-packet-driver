/**
 * @file config_mgr.c
 * @brief External Runtime Configuration Manager (GPT-5 Stage 3A: Sidecar Model)
 * 
 * This is the external sidecar utility that provides comprehensive runtime
 * configuration management for the packet driver. It communicates with
 * the resident driver via the extension API to dynamically adjust parameters.
 * 
 * GPT-5 Architecture: Zero resident footprint for configuration logic
 */

#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <string.h>

/* Extension API constants (match driver) */
#define EXT_GET_VERSION         0x80
#define EXT_RUNTIME_GET_PARAM   0x83
#define EXT_RUNTIME_SET_PARAM   0x84
#define EXT_COMMIT_CONFIG       0x85

#define EXT_FEATURE_RUNTIME_CFG 0x0002

/* Configuration parameter categories */
#define PARAM_CATEGORY_NETWORK  0
#define PARAM_CATEGORY_MEMORY   1
#define PARAM_CATEGORY_DIAG     2
#define PARAM_CATEGORY_HARDWARE 3

/* Parameter indices (must match driver layout) */
#define PARAM_TX_TIMEOUT        0
#define PARAM_RX_POLL_INTERVAL  1
#define PARAM_BUFFER_THRESHOLD  2
#define PARAM_RETRY_COUNT       3
#define PARAM_DMA_BURST_SIZE    4
#define PARAM_IRQ_COALESCING    5
#define PARAM_FLOW_CONTROL      6
#define PARAM_DUPLEX_MODE       7

#define PARAM_BUFFER_POOL_SIZE  8
#define PARAM_XMS_THRESHOLD     9
#define PARAM_COPY_BREAK_SIZE   10
#define PARAM_MEMORY_PRESSURE   11
#define PARAM_GC_INTERVAL       12
#define PARAM_ALLOC_STRATEGY    13

#define PARAM_LOG_LEVEL         16
#define PARAM_HEALTH_INTERVAL   17
#define PARAM_STATS_RESET       18
#define PARAM_ERROR_THRESHOLD   19
#define PARAM_PERF_MONITORING   20
#define PARAM_DEBUG_OUTPUT      21

#define PARAM_NIC_SPEED         24
#define PARAM_NIC_DUPLEX        25
#define PARAM_BUS_MASTER_ENABLE 26
#define PARAM_PIO_THRESHOLD     27
#define PARAM_IRQ_MASK_TIME     28
#define PARAM_CABLE_TEST_ENABLE 29

#define MAX_PARAMETERS          32

/* Packet driver interrupt (configurable) */
int packet_int = 0x60;

/* Parameter metadata for validation and display */
struct param_info {
    char name[32];
    char unit[8];
    int min_value;
    int max_value;
    int is_boolean;
    char description[64];
};

static struct param_info param_table[MAX_PARAMETERS] = {
    /* Network parameters [0-7] */
    {"tx_timeout_ms", "ms", 100, 30000, 0, "TX timeout in milliseconds"},
    {"rx_poll_interval", "ticks", 10, 1000, 0, "RX polling interval"},
    {"buffer_threshold", "%", 25, 95, 0, "Buffer utilization threshold"},
    {"retry_count", "", 1, 10, 0, "Packet retry attempts"},
    {"dma_burst_size", "bytes", 64, 8192, 0, "DMA burst transfer size"},
    {"irq_coalescing", "", 0, 1, 1, "IRQ coalescing enabled"},
    {"flow_control", "", 0, 1, 1, "Flow control enabled"},
    {"duplex_mode", "", 0, 2, 0, "Duplex mode (0=half,1=full,2=auto)"},
    
    /* Memory parameters [8-15] */
    {"buffer_pool_size", "", 8, 128, 0, "Number of buffers in pool"},
    {"xms_threshold", "KB", 128, 2048, 0, "XMS migration threshold"},
    {"copy_break_size", "bytes", 32, 512, 0, "Copy break threshold"},
    {"memory_pressure", "%", 50, 99, 0, "Memory pressure limit"},
    {"gc_interval", "ticks", 100, 5000, 0, "Garbage collection interval"},
    {"alloc_strategy", "", 0, 2, 0, "Allocation strategy"},
    {"reserved_mem1", "", 0, 255, 0, "Reserved"},
    {"reserved_mem2", "", 0, 255, 0, "Reserved"},
    
    /* Diagnostic parameters [16-23] */
    {"log_level", "", 0, 4, 0, "Log level (0=off,1=error,2=warn,3=info,4=debug)"},
    {"health_interval", "ticks", 50, 1000, 0, "Health check interval"},
    {"stats_reset", "", 0, 1, 1, "Reset stats after reading"},
    {"error_threshold", "/min", 10, 1000, 0, "Error rate threshold"},
    {"perf_monitoring", "", 0, 1, 1, "Performance monitoring enabled"},
    {"debug_output", "", 0, 1, 1, "Debug output to console"},
    {"reserved_diag1", "", 0, 255, 0, "Reserved"},
    {"reserved_diag2", "", 0, 255, 0, "Reserved"},
    
    /* Hardware parameters [24-31] */
    {"nic_speed", "", 0, 2, 0, "NIC speed (0=auto,1=10M,2=100M)"},
    {"nic_duplex", "", 0, 2, 0, "NIC duplex (0=auto,1=half,2=full)"},
    {"bus_master_enable", "", 0, 1, 1, "Bus mastering enabled"},
    {"pio_threshold", "bytes", 16, 256, 0, "PIO vs DMA threshold"},
    {"irq_mask_time", "μs", 1, 100, 0, "IRQ mask time"},
    {"cable_test_enable", "", 0, 1, 1, "Cable testing enabled"},
    {"reserved_hw1", "", 0, 255, 0, "Reserved"},
    {"reserved_hw2", "", 0, 255, 0, "Reserved"}
};

/**
 * Call driver extension API
 */
int call_extension_api(int function, int subfunction, 
                      unsigned int *ax, unsigned int *bx, 
                      unsigned int *cx, unsigned int *dx)
{
    union REGS regs;
    int carry_flag;
    
    regs.h.ah = function;
    regs.h.al = subfunction;
    regs.x.bx = *bx;
    regs.x.cx = *cx;
    regs.x.dx = *dx;
    
    int86(packet_int, &regs, &regs);
    
    *ax = regs.x.ax;
    *bx = regs.x.bx;
    *cx = regs.x.cx;
    *dx = regs.x.dx;
    
    /* Return 0 on success (CF=0), 1 on error (CF=1) */
    carry_flag = regs.x.cflag;
    return carry_flag ? 1 : 0;
}

/**
 * Check if driver supports runtime configuration
 */
int check_driver_support(void)
{
    unsigned int ax, bx, cx, dx;
    
    printf("Checking for 3Com Packet Driver runtime configuration support...\\n");
    
    /* Call EXT_GET_VERSION */
    ax = bx = cx = dx = 0;
    if (call_extension_api(EXT_GET_VERSION, 0, &ax, &bx, &cx, &dx)) {
        printf("ERROR: Driver does not support extensions\\n");
        return 0;
    }
    
    printf("Extension API found: Version %d.%d, Signature 0x%04X\\n", 
           (bx >> 8) & 0xFF, bx & 0xFF, ax);
    printf("Feature bitmap: 0x%04X\\n", cx);
    
    /* Check for runtime configuration support */
    if (!(cx & EXT_FEATURE_RUNTIME_CFG)) {
        printf("ERROR: Runtime configuration not supported by this driver\\n");
        return 0;
    }
    
    printf("Runtime configuration feature: SUPPORTED\\n");
    return 1;
}

/**
 * Get parameter value from driver
 */
int get_parameter(int param_index, int *value, int *flags)
{
    unsigned int ax, bx, cx, dx;
    
    ax = bx = cx = dx = 0;
    if (call_extension_api(EXT_RUNTIME_GET_PARAM, param_index, &ax, &bx, &cx, &dx)) {
        return 0;  /* Failed */
    }
    
    *value = bx;
    *flags = dx;
    return 1;  /* Success */
}

/**
 * Set parameter value in driver
 */
int set_parameter(int param_index, int value)
{
    unsigned int ax, bx, cx, dx;
    
    ax = 0;
    bx = value;
    cx = dx = 0;
    
    if (call_extension_api(EXT_RUNTIME_SET_PARAM, param_index, &ax, &bx, &cx, &dx)) {
        return 0;  /* Failed */
    }
    
    return 1;  /* Success */
}

/**
 * Commit configuration changes
 */
int commit_configuration(int commit_type)
{
    unsigned int ax, bx, cx, dx;
    
    ax = bx = cx = dx = 0;
    if (call_extension_api(EXT_COMMIT_CONFIG, commit_type, &ax, &bx, &cx, &dx)) {
        return 0;  /* Failed */
    }
    
    return 1;  /* Success */
}

/**
 * Validate parameter value against constraints
 */
int validate_parameter(int param_index, int value)
{
    if (param_index < 0 || param_index >= MAX_PARAMETERS) {
        return 0;
    }
    
    struct param_info *param = &param_table[param_index];
    
    if (value < param->min_value || value > param->max_value) {
        printf("ERROR: Parameter %s value %d out of range [%d-%d]\\n",
               param->name, value, param->min_value, param->max_value);
        return 0;
    }
    
    return 1;
}

/**
 * Display current configuration
 */
void display_configuration(void)
{
    int i, value, flags;
    
    printf("\\n=== Current Runtime Configuration ===\\n");
    printf("Parameter                    Current   Unit     Range        Description\\n");
    printf("---------------------------  --------  -------  -----------  ------------------------\\n");
    
    for (i = 0; i < MAX_PARAMETERS; i++) {
        if (strlen(param_table[i].name) == 0) continue;
        
        if (get_parameter(i, &value, &flags)) {
            printf("%-27s  %8d  %-7s  %4d-%-4d   %s\\n",
                   param_table[i].name, value, param_table[i].unit,
                   param_table[i].min_value, param_table[i].max_value,
                   param_table[i].description);
        } else {
            printf("%-27s  %8s  %-7s  %4d-%-4d   %s\\n",
                   param_table[i].name, "ERROR", param_table[i].unit,
                   param_table[i].min_value, param_table[i].max_value,
                   param_table[i].description);
        }
    }
    
    printf("\\nConfiguration Status: ");
    if (flags & 0x0001) {
        printf("MODIFIED (use 'commit' to apply)\\n");
    } else {
        printf("CLEAN\\n");
    }
}

/**
 * Display configuration by category
 */
void display_category(int category)
{
    int start, end;
    
    switch (category) {
        case PARAM_CATEGORY_NETWORK:
            printf("\\n=== Network Parameters ===\\n");
            start = 0; end = 7;
            break;
        case PARAM_CATEGORY_MEMORY:
            printf("\\n=== Memory Management Parameters ===\\n");
            start = 8; end = 15;
            break;
        case PARAM_CATEGORY_DIAG:
            printf("\\n=== Diagnostic Parameters ===\\n");
            start = 16; end = 23;
            break;
        case PARAM_CATEGORY_HARDWARE:
            printf("\\n=== Hardware Parameters ===\\n");
            start = 24; end = 31;
            break;
        default:
            printf("ERROR: Invalid category\\n");
            return;
    }
    
    printf("Parameter                    Current   Range        Description\\n");
    printf("---------------------------  --------  -----------  ------------------------\\n");
    
    for (int i = start; i <= end; i++) {
        int value, flags;
        if (strlen(param_table[i].name) == 0) continue;
        
        if (get_parameter(i, &value, &flags)) {
            printf("%-27s  %8d  %4d-%-4d   %s\\n",
                   param_table[i].name, value,
                   param_table[i].min_value, param_table[i].max_value,
                   param_table[i].description);
        }
    }
}

/**
 * Find parameter by name
 */
int find_parameter(const char *name)
{
    for (int i = 0; i < MAX_PARAMETERS; i++) {
        if (strcmp(param_table[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * Set parameter by name and value
 */
int set_parameter_by_name(const char *name, const char *value_str)
{
    int param_index = find_parameter(name);
    if (param_index < 0) {
        printf("ERROR: Unknown parameter '%s'\\n", name);
        return 0;
    }
    
    int value = atoi(value_str);
    if (!validate_parameter(param_index, value)) {
        return 0;
    }
    
    if (!set_parameter(param_index, value)) {
        printf("ERROR: Failed to set parameter %s\\n", name);
        return 0;
    }
    
    printf("Parameter %s set to %d\\n", name, value);
    return 1;
}

/**
 * Print usage information
 */
void print_usage(const char *program_name)
{
    printf("Usage: %s [options] [command] [parameters]\\n", program_name);
    printf("\\nCommands:\\n");
    printf("  show [category]           Show current configuration\\n");
    printf("    categories: network, memory, diag, hardware\\n");
    printf("  set <parameter> <value>   Set parameter value\\n");
    printf("  get <parameter>           Get parameter value\\n");
    printf("  commit [type]             Commit configuration changes\\n");
    printf("    types: apply(default), save, rollback\\n");
    printf("  reset                     Reset to default values\\n");
    printf("\\nOptions:\\n");
    printf("  --int=XX                  Use interrupt XX (hex)\\n");
    printf("  --help                    Show this help\\n");
    printf("\\nExamples:\\n");
    printf("  %s show                   # Show all parameters\\n", program_name);
    printf("  %s show network           # Show network parameters\\n", program_name);
    printf("  %s set tx_timeout_ms 10000 # Set TX timeout to 10 seconds\\n", program_name);
    printf("  %s commit                 # Apply pending changes\\n", program_name);
}

/**
 * Main program
 */
int main(int argc, char *argv[])
{
    int i;
    const char *command = "show";
    
    printf("3Com Packet Driver Runtime Configuration Manager v1.0\\n");
    printf("GPT-5 Stage 3A: External Sidecar Architecture\\n\\n");
    
    /* Parse command line arguments */
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--int=", 6) == 0) {
            packet_int = strtol(argv[i] + 6, NULL, 16);
            printf("Using packet driver interrupt: 0x%02X\\n", packet_int);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            command = argv[i];
            break;
        }
    }
    
    /* Verify driver support */
    if (!check_driver_support()) {
        printf("\\nDriver support check failed. Please ensure:\\n");
        printf("1. 3Com packet driver is loaded\\n"); 
        printf("2. Driver supports extension API\\n");
        printf("3. Runtime configuration feature is enabled\\n");
        printf("4. Correct interrupt vector (default 0x60)\\n");
        return 1;
    }
    
    /* Execute command */
    if (strcmp(command, "show") == 0) {
        if (i + 1 < argc) {
            const char *category = argv[i + 1];
            if (strcmp(category, "network") == 0) {
                display_category(PARAM_CATEGORY_NETWORK);
            } else if (strcmp(category, "memory") == 0) {
                display_category(PARAM_CATEGORY_MEMORY);
            } else if (strcmp(category, "diag") == 0) {
                display_category(PARAM_CATEGORY_DIAG);
            } else if (strcmp(category, "hardware") == 0) {
                display_category(PARAM_CATEGORY_HARDWARE);
            } else {
                printf("ERROR: Unknown category '%s'\\n", category);
                return 1;
            }
        } else {
            display_configuration();
        }
    } else if (strcmp(command, "set") == 0) {
        if (i + 2 >= argc) {
            printf("ERROR: 'set' command requires parameter name and value\\n");
            print_usage(argv[0]);
            return 1;
        }
        if (!set_parameter_by_name(argv[i + 1], argv[i + 2])) {
            return 1;
        }
        printf("Use 'commit' to apply the change.\\n");
    } else if (strcmp(command, "get") == 0) {
        if (i + 1 >= argc) {
            printf("ERROR: 'get' command requires parameter name\\n");
            return 1;
        }
        int param_index = find_parameter(argv[i + 1]);
        if (param_index < 0) {
            printf("ERROR: Unknown parameter '%s'\\n", argv[i + 1]);
            return 1;
        }
        int value, flags;
        if (get_parameter(param_index, &value, &flags)) {
            printf("%s = %d %s\\n", argv[i + 1], value, param_table[param_index].unit);
        } else {
            printf("ERROR: Failed to get parameter %s\\n", argv[i + 1]);
            return 1;
        }
    } else if (strcmp(command, "commit") == 0) {
        int commit_type = 0;  /* Default: apply */
        if (i + 1 < argc) {
            if (strcmp(argv[i + 1], "save") == 0) {
                commit_type = 1;
            } else if (strcmp(argv[i + 1], "rollback") == 0) {
                commit_type = 2;
            }
        }
        
        if (commit_configuration(commit_type)) {
            switch (commit_type) {
                case 0: printf("Configuration changes applied successfully\\n"); break;
                case 1: printf("Configuration saved successfully\\n"); break;
                case 2: printf("Configuration rolled back successfully\\n"); break;
            }
        } else {
            printf("ERROR: Failed to commit configuration\\n");
            return 1;
        }
    } else {
        printf("ERROR: Unknown command '%s'\\n", command);
        print_usage(argv[0]);
        return 1;
    }
    
    return 0;
}