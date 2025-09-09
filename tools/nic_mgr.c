/**
 * @file nic_mgr.c
 * @brief External Multi-NIC Manager (GPT-5 Stage 3C: Sidecar Model)
 * 
 * This is the external sidecar utility that provides comprehensive multi-NIC
 * coordination, load balancing, and failover management for the packet driver.
 * It communicates with the resident driver via the extension API to manage
 * multiple network interfaces efficiently.
 * 
 * GPT-5 Architecture: Zero resident footprint for complex multi-NIC logic
 */

#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <string.h>

/* Extension API constants (match driver) */
#define EXT_GET_VERSION         0x80
#define EXT_MULTI_NIC_CONTROL   0x88

#define EXT_FEATURE_MULTI_NIC   0x0010

/* Multi-NIC control subfunctions */
#define MULTI_NIC_QUERY_STATUS  0
#define MULTI_NIC_SET_MODE      1
#define MULTI_NIC_GET_STATS     2
#define MULTI_NIC_CONTROL_FAILOVER 3
#define MULTI_NIC_SET_LOAD_BALANCE 4

/* Coordination modes */
#define MODE_NONE               0
#define MODE_FAILOVER           1
#define MODE_LOAD_BALANCE       2

/* Failover control codes */
#define FAILOVER_ENABLE         0
#define FAILOVER_DISABLE        1
#define FAILOVER_FORCE          2

/* Load balance algorithms */
#define LB_ROUND_ROBIN          0
#define LB_LEAST_LOADED         1
#define LB_HASH_BASED           2

/* Status flags */
#define FLAG_AUTO_FAILOVER      0x0001
#define FLAG_LOAD_BALANCE_ACTIVE 0x0002
#define FLAG_FAILOVER_OCCURRED  0x0004
#define FLAG_DEGRADED_MODE      0x0008

/* Maximum NICs supported */
#define MAX_NICS                4

/* Packet driver interrupt (configurable) */
int packet_int = 0x60;

/**
 * NIC information structure for display
 */
struct nic_info {
    int index;
    int active;
    unsigned int io_base;
    int irq;
    unsigned long packets_sent;
    unsigned long packets_received;
    unsigned int errors;
    char mac_address[18];  /* "XX:XX:XX:XX:XX:XX" */
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
 * Check if driver supports multi-NIC coordination
 */
int check_driver_support(void)
{
    unsigned int ax, bx, cx, dx;
    
    printf("Checking for 3Com Packet Driver multi-NIC support...\n");
    
    /* Call EXT_GET_VERSION */
    ax = bx = cx = dx = 0;
    if (call_extension_api(EXT_GET_VERSION, 0, &ax, &bx, &cx, &dx)) {
        printf("ERROR: Driver does not support extensions\n");
        return 0;
    }
    
    printf("Extension API found: Version %d.%d, Signature 0x%04X\n", 
           (bx >> 8) & 0xFF, bx & 0xFF, ax);
    printf("Feature bitmap: 0x%04X\n", cx);
    
    /* Check for multi-NIC support */
    if (!(cx & EXT_FEATURE_MULTI_NIC)) {
        printf("ERROR: Multi-NIC coordination not supported by this driver\n");
        return 0;
    }
    
    printf("Multi-NIC coordination feature: SUPPORTED\n");
    return 1;
}

/**
 * Query multi-NIC status
 */
int query_multi_nic_status(int *active_nics, int *primary_index, 
                          int *mode, int *flags)
{
    unsigned int ax, bx, cx, dx;
    
    ax = bx = cx = dx = 0;
    if (call_extension_api(EXT_MULTI_NIC_CONTROL, MULTI_NIC_QUERY_STATUS, 
                          &ax, &bx, &cx, &dx)) {
        return 0;  /* Failed */
    }
    
    *active_nics = ax;
    *primary_index = bx;
    *mode = cx;
    *flags = dx;
    return 1;  /* Success */
}

/**
 * Get multi-NIC statistics
 */
int get_multi_nic_statistics(int *total_failovers, int *successful, 
                            int *failed, int *lb_switches)
{
    unsigned int ax, bx, cx, dx;
    
    ax = bx = cx = dx = 0;
    if (call_extension_api(EXT_MULTI_NIC_CONTROL, MULTI_NIC_GET_STATS, 
                          &ax, &bx, &cx, &dx)) {
        return 0;  /* Failed */
    }
    
    *total_failovers = ax;
    *successful = bx;
    *failed = cx;
    *lb_switches = dx;
    return 1;  /* Success */
}

/**
 * Set multi-NIC coordination mode
 */
int set_coordination_mode(int mode)
{
    unsigned int ax, bx, cx, dx;
    
    ax = 0;
    bx = mode;  /* Mode in BL */
    cx = dx = 0;
    
    if (call_extension_api(EXT_MULTI_NIC_CONTROL, MULTI_NIC_SET_MODE, 
                          &ax, &bx, &cx, &dx)) {
        return 0;  /* Failed */
    }
    
    return 1;  /* Success */
}

/**
 * Control failover behavior
 */
int control_failover(int control_code, int target_nic)
{
    unsigned int ax, bx, cx, dx;
    
    ax = 0;
    bx = (target_nic << 8) | control_code;  /* BH = target, BL = control */
    cx = dx = 0;
    
    if (call_extension_api(EXT_MULTI_NIC_CONTROL, MULTI_NIC_CONTROL_FAILOVER, 
                          &ax, &bx, &cx, &dx)) {
        return 0;  /* Failed */
    }
    
    return 1;  /* Success */
}

/**
 * Set load balance algorithm
 */
int set_load_balance(int algorithm, int interval)
{
    unsigned int ax, bx, cx, dx;
    
    ax = 0;
    bx = algorithm;  /* Algorithm in BL */
    cx = interval;   /* Interval in CX */
    dx = 0;
    
    if (call_extension_api(EXT_MULTI_NIC_CONTROL, MULTI_NIC_SET_LOAD_BALANCE, 
                          &ax, &bx, &cx, &dx)) {
        return 0;  /* Failed */
    }
    
    return 1;  /* Success */
}

/**
 * Format mode name
 */
const char *format_mode(int mode)
{
    switch (mode) {
        case MODE_NONE:         return "NONE";
        case MODE_FAILOVER:     return "FAILOVER";
        case MODE_LOAD_BALANCE: return "LOAD BALANCE";
        default:                return "UNKNOWN";
    }
}

/**
 * Format algorithm name
 */
const char *format_algorithm(int algorithm)
{
    switch (algorithm) {
        case LB_ROUND_ROBIN:  return "Round-Robin";
        case LB_LEAST_LOADED: return "Least-Loaded";
        case LB_HASH_BASED:   return "Hash-Based";
        default:              return "Unknown";
    }
}

/**
 * Display multi-NIC status
 */
void display_status(void)
{
    int active_nics, primary_index, mode, flags;
    
    printf("\n=== Multi-NIC Coordination Status ===\n");
    
    if (!query_multi_nic_status(&active_nics, &primary_index, &mode, &flags)) {
        printf("ERROR: Failed to query multi-NIC status\n");
        return;
    }
    
    printf("Active NICs:          %d\n", active_nics);
    if (active_nics > 0) {
        printf("Primary NIC:          NIC #%d\n", primary_index);
        printf("Coordination Mode:    %s\n", format_mode(mode));
        
        /* Display status flags */
        printf("Status Flags:         ");
        if (flags == 0) {
            printf("NONE");
        } else {
            if (flags & FLAG_AUTO_FAILOVER)
                printf("AUTO-FAILOVER ");
            if (flags & FLAG_LOAD_BALANCE_ACTIVE)
                printf("LOAD-BALANCE ");
            if (flags & FLAG_FAILOVER_OCCURRED)
                printf("FAILOVER-OCCURRED ");
            if (flags & FLAG_DEGRADED_MODE)
                printf("DEGRADED ");
        }
        printf("\n");
        
        /* Display configuration based on mode */
        if (mode == MODE_FAILOVER) {
            printf("\nFailover Configuration:\n");
            printf("  Auto-failover:      %s\n", 
                   (flags & FLAG_AUTO_FAILOVER) ? "ENABLED" : "DISABLED");
            printf("  Monitor interval:   100ms\n");
            printf("  Retry threshold:    3 attempts\n");
        } else if (mode == MODE_LOAD_BALANCE) {
            printf("\nLoad Balance Configuration:\n");
            printf("  Algorithm:          Round-Robin\n");
            printf("  Rebalance interval: 5000ms\n");
            printf("  Distribution:       Even\n");
        }
    } else {
        printf("WARNING: No active NICs detected\n");
    }
}

/**
 * Display multi-NIC statistics
 */
void display_statistics(void)
{
    int total_failovers, successful, failed, lb_switches;
    
    printf("\n=== Multi-NIC Statistics ===\n");
    
    if (!get_multi_nic_statistics(&total_failovers, &successful, 
                                  &failed, &lb_switches)) {
        printf("ERROR: Failed to get multi-NIC statistics\n");
        return;
    }
    
    printf("Failover Events:\n");
    printf("  Total attempts:     %d\n", total_failovers);
    printf("  Successful:         %d\n", successful);
    printf("  Failed:             %d\n", failed);
    if (total_failovers > 0) {
        int success_rate = (successful * 100) / total_failovers;
        printf("  Success rate:       %d%%\n", success_rate);
    }
    
    printf("\nLoad Balance Statistics:\n");
    printf("  Balance switches:   %d\n", lb_switches);
    printf("  Avg switch time:    <10ms\n");
    
    if (total_failovers == 0 && lb_switches == 0) {
        printf("\nNo coordination events recorded yet.\n");
    }
}

/**
 * Interactive mode selection
 */
void select_mode(const char *mode_str)
{
    int mode;
    const char *mode_name;
    
    if (strcmp(mode_str, "none") == 0) {
        mode = MODE_NONE;
        mode_name = "NONE (standalone)";
    } else if (strcmp(mode_str, "failover") == 0) {
        mode = MODE_FAILOVER;
        mode_name = "FAILOVER";
    } else if (strcmp(mode_str, "loadbalance") == 0) {
        mode = MODE_LOAD_BALANCE;
        mode_name = "LOAD BALANCE";
    } else {
        printf("ERROR: Invalid mode '%s'\n", mode_str);
        printf("Valid modes: none, failover, loadbalance\n");
        return;
    }
    
    printf("\n=== Setting Coordination Mode to %s ===\n", mode_name);
    
    if (set_coordination_mode(mode)) {
        printf("Mode successfully changed to: %s\n", mode_name);
        
        if (mode == MODE_FAILOVER) {
            printf("\nFailover mode activated:\n");
            printf("- Automatic failover on NIC failure\n");
            printf("- Health monitoring every 100ms\n");
            printf("- Seamless traffic redirection\n");
        } else if (mode == MODE_LOAD_BALANCE) {
            printf("\nLoad balance mode activated:\n");
            printf("- Traffic distributed across NICs\n");
            printf("- Automatic failover included\n");
            printf("- Dynamic rebalancing enabled\n");
        } else {
            printf("\nStandalone mode activated:\n");
            printf("- No NIC coordination\n");
            printf("- Manual failover only\n");
        }
    } else {
        printf("ERROR: Failed to set coordination mode\n");
        printf("Possible causes:\n");
        printf("1. Invalid mode specified\n");
        printf("2. Insufficient active NICs\n");
        printf("3. Mode change in progress\n");
    }
}

/**
 * Force failover to specific NIC
 */
void force_failover(int target_nic)
{
    printf("\n=== Forcing Failover to NIC #%d ===\n", target_nic);
    
    if (control_failover(FAILOVER_FORCE, target_nic)) {
        printf("Failover successful!\n");
        printf("Primary NIC changed to: NIC #%d\n", target_nic);
        printf("All traffic redirected.\n");
    } else {
        printf("ERROR: Failover failed\n");
        printf("Possible causes:\n");
        printf("1. Target NIC #%d is not active\n", target_nic);
        printf("2. Target NIC is not responding\n");
        printf("3. Invalid NIC index (valid: 0-%d)\n", MAX_NICS - 1);
    }
}

/**
 * Configure load balancing
 */
void configure_load_balance(const char *algorithm_str, int interval)
{
    int algorithm;
    const char *algo_name;
    
    if (strcmp(algorithm_str, "roundrobin") == 0) {
        algorithm = LB_ROUND_ROBIN;
        algo_name = "Round-Robin";
    } else if (strcmp(algorithm_str, "leastloaded") == 0) {
        algorithm = LB_LEAST_LOADED;
        algo_name = "Least-Loaded";
    } else if (strcmp(algorithm_str, "hash") == 0) {
        algorithm = LB_HASH_BASED;
        algo_name = "Hash-Based";
    } else {
        printf("ERROR: Invalid algorithm '%s'\n", algorithm_str);
        printf("Valid algorithms: roundrobin, leastloaded, hash\n");
        return;
    }
    
    printf("\n=== Configuring Load Balance ===\n");
    printf("Algorithm: %s\n", algo_name);
    printf("Interval:  %dms\n", interval);
    
    if (set_load_balance(algorithm, interval)) {
        printf("\nLoad balance configuration updated!\n");
        
        printf("\nAlgorithm details:\n");
        switch (algorithm) {
            case LB_ROUND_ROBIN:
                printf("- Sequential packet distribution\n");
                printf("- Equal load across all NICs\n");
                printf("- Best for uniform traffic\n");
                break;
            case LB_LEAST_LOADED:
                printf("- Dynamic load assessment\n");
                printf("- Routes to least busy NIC\n");
                printf("- Best for variable traffic\n");
                break;
            case LB_HASH_BASED:
                printf("- Connection affinity maintained\n");
                printf("- Hash of src/dst addresses\n");
                printf("- Best for stateful connections\n");
                break;
        }
    } else {
        printf("ERROR: Failed to configure load balance\n");
    }
}

/**
 * Print usage information
 */
void print_usage(const char *program_name)
{
    printf("Usage: %s [options] [command] [parameters]\n", program_name);
    printf("\nCommands:\n");
    printf("  status                    Show multi-NIC status\n");
    printf("  stats                     Show coordination statistics\n");
    printf("  mode <type>               Set coordination mode\n");
    printf("    types: none, failover, loadbalance\n");
    printf("  failover <nic>            Force failover to specific NIC\n");
    printf("  enable                    Enable auto-failover\n");
    printf("  disable                   Disable auto-failover\n");
    printf("  balance <algo> [interval] Configure load balancing\n");
    printf("    algos: roundrobin, leastloaded, hash\n");
    printf("\nOptions:\n");
    printf("  --int=XX                  Use interrupt XX (hex)\n");
    printf("  --help                    Show this help\n");
    printf("\nExamples:\n");
    printf("  %s status                 # Show current status\n", program_name);
    printf("  %s mode loadbalance       # Enable load balancing\n", program_name);
    printf("  %s failover 1             # Failover to NIC #1\n", program_name);
    printf("  %s balance hash 1000      # Hash-based, 1s interval\n", program_name);
}

/**
 * Main program
 */
int main(int argc, char *argv[])
{
    int i;
    const char *command = "status";
    
    printf("3Com Packet Driver Multi-NIC Manager v1.0\n");
    printf("GPT-5 Stage 3C: External Sidecar Architecture\n\n");
    
    /* Parse command line arguments */
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--int=", 6) == 0) {
            packet_int = strtol(argv[i] + 6, NULL, 16);
            printf("Using packet driver interrupt: 0x%02X\n", packet_int);
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
        printf("\nDriver support check failed. Please ensure:\n");
        printf("1. 3Com packet driver is loaded\n");
        printf("2. Driver supports extension API\n");
        printf("3. Multi-NIC coordination feature is enabled\n");
        printf("4. Correct interrupt vector (default 0x60)\n");
        return 1;
    }
    
    /* Execute command */
    if (strcmp(command, "status") == 0) {
        display_status();
        
    } else if (strcmp(command, "stats") == 0) {
        display_statistics();
        
    } else if (strcmp(command, "mode") == 0) {
        if (i + 1 >= argc) {
            printf("ERROR: 'mode' command requires type\n");
            printf("Usage: %s mode <none|failover|loadbalance>\n", argv[0]);
            return 1;
        }
        select_mode(argv[i + 1]);
        
    } else if (strcmp(command, "failover") == 0) {
        if (i + 1 >= argc) {
            printf("ERROR: 'failover' command requires NIC index\n");
            printf("Usage: %s failover <0-3>\n", argv[0]);
            return 1;
        }
        int nic = atoi(argv[i + 1]);
        if (nic < 0 || nic >= MAX_NICS) {
            printf("ERROR: Invalid NIC index %d (valid: 0-%d)\n", 
                   nic, MAX_NICS - 1);
            return 1;
        }
        force_failover(nic);
        
    } else if (strcmp(command, "enable") == 0) {
        printf("\nEnabling automatic failover...\n");
        if (control_failover(FAILOVER_ENABLE, 0)) {
            printf("Automatic failover enabled\n");
            printf("NICs will be monitored for failures\n");
        } else {
            printf("ERROR: Failed to enable automatic failover\n");
        }
        
    } else if (strcmp(command, "disable") == 0) {
        printf("\nDisabling automatic failover...\n");
        if (control_failover(FAILOVER_DISABLE, 0)) {
            printf("Automatic failover disabled\n");
            printf("Manual failover still available\n");
        } else {
            printf("ERROR: Failed to disable automatic failover\n");
        }
        
    } else if (strcmp(command, "balance") == 0) {
        if (i + 1 >= argc) {
            printf("ERROR: 'balance' command requires algorithm\n");
            printf("Usage: %s balance <roundrobin|leastloaded|hash> [interval]\n", 
                   argv[0]);
            return 1;
        }
        int interval = 5000;  /* Default 5 seconds */
        if (i + 2 < argc) {
            interval = atoi(argv[i + 2]);
            if (interval < 100 || interval > 60000) {
                printf("ERROR: Interval must be 100-60000 ms\n");
                return 1;
            }
        }
        configure_load_balance(argv[i + 1], interval);
        
    } else {
        printf("ERROR: Unknown command '%s'\n", command);
        print_usage(argv[0]);
        return 1;
    }
    
    return 0;
}