/**
 * @file xms_mgr.c
 * @brief External XMS Buffer Manager (GPT-5 Stage 3B: Sidecar Model)
 * 
 * This is the external sidecar utility that provides comprehensive XMS buffer
 * pool management and migration control for the packet driver. It communicates
 * with the resident driver via the extension API to optimize memory usage.
 * 
 * GPT-5 Architecture: Zero resident footprint for complex XMS logic
 */

#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <string.h>

/* Extension API constants (match driver) */
#define EXT_GET_VERSION         0x80
#define EXT_XMS_CONTROL         0x86

#define EXT_FEATURE_XMS_BUFFERS 0x0004

/* XMS control subfunctions */
#define XMS_QUERY_STATUS        0
#define XMS_MIGRATE_BUFFERS     1
#define XMS_GET_STATS           2
#define XMS_CONTROL_MIGRATION   3

/* XMS migration control codes */
#define XMS_ENABLE_MIGRATION    0
#define XMS_DISABLE_MIGRATION   1
#define XMS_SET_THRESHOLD       2

/* Buffer pool types */
#define POOL_TYPE_RX            0
#define POOL_TYPE_TX            1
#define POOL_TYPE_ALL           2

/* XMS status flags */
#define XMS_FLAG_AUTO_ENABLED   0x0001
#define XMS_FLAG_MIGRATION_ACTIVE 0x0002
#define XMS_FLAG_EMERGENCY_MODE 0x0004
#define XMS_FLAG_DRIVER_VALIDATED 0x0008
#define XMS_FLAG_MEMORY_CRITICAL 0x0010

/* Packet driver interrupt (configurable) */
int packet_int = 0x60;

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
 * Check if driver supports XMS buffer management
 */
int check_driver_support(void)
{
    unsigned int ax, bx, cx, dx;
    
    printf("Checking for 3Com Packet Driver XMS buffer support...\\n");
    
    /* Call EXT_GET_VERSION */
    ax = bx = cx = dx = 0;
    if (call_extension_api(EXT_GET_VERSION, 0, &ax, &bx, &cx, &dx)) {
        printf("ERROR: Driver does not support extensions\\n");
        return 0;
    }
    
    printf("Extension API found: Version %d.%d, Signature 0x%04X\\n", 
           (bx >> 8) & 0xFF, bx & 0xFF, ax);
    printf("Feature bitmap: 0x%04X\\n", cx);
    
    /* Check for XMS buffer support */
    if (!(cx & EXT_FEATURE_XMS_BUFFERS)) {
        printf("ERROR: XMS buffer management not supported by this driver\\n");
        return 0;
    }
    
    printf("XMS buffer management feature: SUPPORTED\\n");
    return 1;
}

/**
 * Query XMS status from driver
 */
int query_xms_status(int *xms_available, int *total_kb, int *free_kb, int *flags)
{
    unsigned int ax, bx, cx, dx;
    
    ax = bx = cx = dx = 0;
    if (call_extension_api(EXT_XMS_CONTROL, XMS_QUERY_STATUS, &ax, &bx, &cx, &dx)) {
        return 0;  /* Failed */
    }
    
    *xms_available = ax;
    *total_kb = bx;
    *free_kb = cx;
    *flags = dx;
    return 1;  /* Success */
}

/**
 * Get XMS migration statistics
 */
int get_xms_statistics(int *total_migrations, int *successful, int *failed, int *kb_migrated)
{
    unsigned int ax, bx, cx, dx;
    
    ax = bx = cx = dx = 0;
    if (call_extension_api(EXT_XMS_CONTROL, XMS_GET_STATS, &ax, &bx, &cx, &dx)) {
        return 0;  /* Failed */
    }
    
    *total_migrations = ax;
    *successful = bx;
    *failed = cx;
    *kb_migrated = dx;  /* Low word only */
    return 1;  /* Success */
}

/**
 * Control XMS migration settings
 */
int control_xms_migration(int control_code, int parameter)
{
    unsigned int ax, bx, cx, dx;
    
    ax = 0;
    bx = control_code;
    cx = parameter;
    dx = 0;
    
    if (call_extension_api(EXT_XMS_CONTROL, XMS_CONTROL_MIGRATION, &ax, &bx, &cx, &dx)) {
        return 0;  /* Failed */
    }
    
    return 1;  /* Success */
}

/**
 * Migrate buffer pools to XMS
 */
int migrate_buffers(int pool_type, int *pools_migrated, int *kb_migrated)
{
    unsigned int ax, bx, cx, dx;
    
    ax = 0;
    bx = pool_type;
    cx = dx = 0;
    
    if (call_extension_api(EXT_XMS_CONTROL, XMS_MIGRATE_BUFFERS, &ax, &bx, &cx, &dx)) {
        return 0;  /* Failed */
    }
    
    *pools_migrated = bx;
    *kb_migrated = cx;
    return 1;  /* Success */
}

/**
 * Format XMS status flags for display
 */
void format_xms_flags(int flags, char *buffer, size_t size)
{
    buffer[0] = '\\0';
    
    if (flags & XMS_FLAG_AUTO_ENABLED) {
        strcat(buffer, "AUTO ");
    }
    if (flags & XMS_FLAG_MIGRATION_ACTIVE) {
        strcat(buffer, "MIGRATING ");
    }
    if (flags & XMS_FLAG_EMERGENCY_MODE) {
        strcat(buffer, "EMERGENCY ");
    }
    if (flags & XMS_FLAG_DRIVER_VALIDATED) {
        strcat(buffer, "VALIDATED ");
    }
    if (flags & XMS_FLAG_MEMORY_CRITICAL) {
        strcat(buffer, "CRITICAL ");
    }
    
    if (strlen(buffer) == 0) {
        strcpy(buffer, "NONE");
    }
}

/**
 * Display XMS system status
 */
void display_xms_status(void)
{
    int xms_available, total_kb, free_kb, flags;
    char flag_str[64];
    
    printf("\\n=== XMS Buffer Management Status ===\\n");
    
    if (!query_xms_status(&xms_available, &total_kb, &free_kb, &flags)) {
        printf("ERROR: Failed to query XMS status\\n");
        return;
    }
    
    printf("XMS Driver Status:    %s\\n", xms_available ? "AVAILABLE" : "NOT AVAILABLE");
    if (xms_available) {
        printf("Total XMS Memory:     %d KB\\n", total_kb);
        printf("Free XMS Memory:      %d KB\\n", free_kb);
        printf("Used XMS Memory:      %d KB\\n", total_kb - free_kb);
        
        if (total_kb > 0) {
            int used_percent = ((total_kb - free_kb) * 100) / total_kb;
            printf("XMS Utilization:      %d%%\\n", used_percent);
        }
    }
    
    format_xms_flags(flags, flag_str, sizeof(flag_str));
    printf("Migration Flags:      %s\\n", flag_str);
    
    /* Migration settings */
    printf("\\n=== Migration Settings ===\\n");
    printf("Auto Migration:       %s\\n", 
           (flags & XMS_FLAG_AUTO_ENABLED) ? "ENABLED" : "DISABLED");
    printf("Migration Status:     %s\\n",
           (flags & XMS_FLAG_MIGRATION_ACTIVE) ? "IN PROGRESS" : "IDLE");
    printf("Memory Status:        %s\\n",
           (flags & XMS_FLAG_MEMORY_CRITICAL) ? "CRITICAL" : "NORMAL");
}

/**
 * Display XMS migration statistics
 */
void display_xms_statistics(void)
{
    int total_migrations, successful, failed, kb_migrated;
    
    printf("\\n=== XMS Migration Statistics ===\\n");
    
    if (!get_xms_statistics(&total_migrations, &successful, &failed, &kb_migrated)) {
        printf("ERROR: Failed to get migration statistics\\n");
        return;
    }
    
    printf("Total Migrations:     %d\\n", total_migrations);
    printf("Successful:           %d\\n", successful);
    printf("Failed:               %d\\n", failed);
    if (total_migrations > 0) {
        int success_rate = (successful * 100) / total_migrations;
        printf("Success Rate:         %d%%\\n", success_rate);
    }
    printf("Data Migrated:        %d KB\\n", kb_migrated);
    
    if (total_migrations == 0) {
        printf("\\nNo migrations have been performed yet.\\n");
    }
}

/**
 * Perform interactive migration
 */
void perform_migration(const char *pool_type_str)
{
    int pool_type;
    int pools_migrated, kb_migrated;
    const char *type_name;
    
    /* Parse pool type */
    if (strcmp(pool_type_str, "rx") == 0) {
        pool_type = POOL_TYPE_RX;
        type_name = "RX";
    } else if (strcmp(pool_type_str, "tx") == 0) {
        pool_type = POOL_TYPE_TX;
        type_name = "TX";
    } else if (strcmp(pool_type_str, "all") == 0) {
        pool_type = POOL_TYPE_ALL;
        type_name = "ALL";
    } else {
        printf("ERROR: Invalid pool type '%s'. Use: rx, tx, or all\\n", pool_type_str);
        return;
    }
    
    printf("\\n=== Migrating %s Buffer Pools to XMS ===\\n", type_name);
    printf("This will move buffer pools from conventional to extended memory.\\n");
    printf("Migration may briefly impact network performance.\\n\\n");
    
    if (migrate_buffers(pool_type, &pools_migrated, &kb_migrated)) {
        printf("Migration completed successfully!\\n");
        printf("Pools migrated: %d\\n", pools_migrated);
        printf("Memory migrated: %d KB\\n", kb_migrated);
        printf("Conventional memory freed: %d KB\\n", kb_migrated);
    } else {
        printf("ERROR: Migration failed\\n");
        printf("Possible causes:\\n");
        printf("1. Insufficient XMS memory available\\n");
        printf("2. XMS driver not properly installed\\n");
        printf("3. Buffer pools currently in use\\n");
        printf("4. Migration already in progress\\n");
    }
}

/**
 * Set migration threshold
 */
void set_migration_threshold(int threshold_kb)
{
    printf("\\nSetting migration threshold to %d KB...\\n", threshold_kb);
    
    if (control_xms_migration(XMS_SET_THRESHOLD, threshold_kb)) {
        printf("Migration threshold updated successfully\\n");
        printf("Automatic migration will trigger when conventional memory < %d KB\\n", threshold_kb);
    } else {
        printf("ERROR: Failed to set migration threshold\\n");
        printf("Threshold must be between 64 KB and 2048 KB\\n");
    }
}

/**
 * Print usage information
 */
void print_usage(const char *program_name)
{
    printf("Usage: %s [options] [command] [parameters]\\n", program_name);
    printf("\\nCommands:\\n");
    printf("  status                    Show XMS system status\\n");
    printf("  stats                     Show migration statistics\\n");
    printf("  migrate <type>            Migrate buffer pools to XMS\\n");
    printf("    types: rx, tx, all\\n");
    printf("  enable                    Enable automatic migration\\n");
    printf("  disable                   Disable automatic migration\\n");
    printf("  threshold <kb>            Set migration threshold (KB)\\n");
    printf("\\nOptions:\\n");
    printf("  --int=XX                  Use interrupt XX (hex)\\n");
    printf("  --help                    Show this help\\n");
    printf("\\nExamples:\\n");
    printf("  %s status                 # Show current XMS status\\n", program_name);
    printf("  %s migrate all            # Migrate all buffer pools\\n", program_name);
    printf("  %s threshold 256          # Set threshold to 256KB\\n", program_name);
    printf("  %s enable                 # Enable automatic migration\\n", program_name);
}

/**
 * Main program
 */
int main(int argc, char *argv[])
{
    int i;
    const char *command = "status";
    
    printf("3Com Packet Driver XMS Buffer Manager v1.0\\n");
    printf("GPT-5 Stage 3B: External Sidecar Architecture\\n\\n");
    
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
        printf("3. XMS buffer management feature is enabled\\n");
        printf("4. Correct interrupt vector (default 0x60)\\n");
        return 1;
    }
    
    /* Execute command */
    if (strcmp(command, "status") == 0) {
        display_xms_status();
        
    } else if (strcmp(command, "stats") == 0) {
        display_xms_statistics();
        
    } else if (strcmp(command, "migrate") == 0) {
        if (i + 1 >= argc) {
            printf("ERROR: 'migrate' command requires pool type\\n");
            printf("Usage: %s migrate <rx|tx|all>\\n", argv[0]);
            return 1;
        }
        perform_migration(argv[i + 1]);
        
    } else if (strcmp(command, "enable") == 0) {
        printf("\\nEnabling automatic XMS migration...\\n");
        if (control_xms_migration(XMS_ENABLE_MIGRATION, 0)) {
            printf("Automatic migration enabled\\n");
            printf("Migration will occur when conventional memory is low\\n");
        } else {
            printf("ERROR: Failed to enable automatic migration\\n");
        }
        
    } else if (strcmp(command, "disable") == 0) {
        printf("\\nDisabling automatic XMS migration...\\n");
        if (control_xms_migration(XMS_DISABLE_MIGRATION, 0)) {
            printf("Automatic migration disabled\\n");
            printf("Manual migration is still available\\n");
        } else {
            printf("ERROR: Failed to disable automatic migration\\n");
        }
        
    } else if (strcmp(command, "threshold") == 0) {
        if (i + 1 >= argc) {
            printf("ERROR: 'threshold' command requires value in KB\\n");
            printf("Usage: %s threshold <kb>\\n", argv[0]);
            return 1;
        }
        int threshold = atoi(argv[i + 1]);
        if (threshold < 64 || threshold > 2048) {
            printf("ERROR: Threshold must be between 64 and 2048 KB\\n");
            return 1;
        }
        set_migration_threshold(threshold);
        
    } else {
        printf("ERROR: Unknown command '%s'\\n", command);
        print_usage(argv[0]);
        return 1;
    }
    
    return 0;
}