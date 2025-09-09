/**
 * @file health_diag.c
 * @brief External Health Diagnostics Utility (DIAGTOOL)
 * 
 * This is the external sidecar utility that provides comprehensive health
 * diagnostics and monitoring for the packet driver. It communicates with
 * the resident driver via the extension API to collect and analyze data.
 * 
 * Uses the new atomic snapshot API (AH=81h-83h) with ES:DI destination buffers
 * for safe, consistent data access without pointer races.
 * 
 * Key features:
 * - Real-time health monitoring
 * - Error counter tracking
 * - Performance metrics display
 * - Interrupt mitigation statistics
 * - DMA validation status
 */

#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <time.h>
#include <string.h>

/* Extension API constants (match driver) */
#define EXT_GET_VERSION         0x80
#define EXT_GET_SAFETY          0x81
#define EXT_GET_PATCH_STATS     0x82
#define EXT_GET_NIC_INFO        0x83
#define EXT_GET_TX_OPTIMIZATION 0x84
#define EXT_GET_RX_OPTIMIZATION 0x85
#define EXT_GET_ERROR_COUNTERS  0x86
#define EXT_GET_PERF_METRICS    0x87
#define EXT_GET_MITIGATION      0x88
#define EXT_GET_DMA_STATS       0x89

#define EXT_FEATURE_DIAGNOSTICS 0x0001
#define EXT_FEATURE_SAFETY      0x0002
#define EXT_FEATURE_MITIGATION  0x0004
#define EXT_FEATURE_DMA_POLICY  0x0008

/* Health diagnostic subfunctions */
#define HEALTH_QUERY_STATUS     0
#define HEALTH_GET_COUNTERS     1
#define HEALTH_GET_METRICS      2
#define HEALTH_RESET_STATS      3

/* Error counter categories (matches driver layout) */
#define ERROR_TX                0
#define ERROR_RX                1
#define ERROR_DMA               2
#define ERROR_MEMORY            3
#define ERROR_HARDWARE          4
#define ERROR_API               5
#define ERROR_BUFFER            6
#define ERROR_TIMEOUT           7
#define ERROR_CATEGORIES        8

/* Performance metric indices (matches driver layout) */
#define METRIC_TX_RATE_BASE     0    /* [0-3] TX rates per NIC */
#define METRIC_RX_RATE_BASE     4    /* [4-7] RX rates per NIC */
#define METRIC_BUFFER_BASE      8    /* [8-11] Buffer utilization per NIC */
#define METRIC_CPU_UTIL         12   /* CPU utilization estimate */
#define METRIC_MEMORY_PRESSURE  13   /* Memory pressure indicator */
#define METRIC_ISR_FREQUENCY    14   /* ISR frequency */
#define METRIC_API_FREQUENCY    15   /* API call frequency */
#define METRIC_COUNT            16

/* Health status thresholds */
#define THRESHOLD_HIGH_ERROR_RATE   100   /* Errors per minute */
#define THRESHOLD_HIGH_CPU_UTIL     80    /* CPU utilization % */
#define THRESHOLD_HIGH_MEMORY       90    /* Memory pressure % */
#define THRESHOLD_LOW_THROUGHPUT    10    /* Packets per second */

/* Packet driver interrupt (configurable) */
int packet_int = 0x60;

/**
 * Call driver extension API with atomic snapshot
 */
int call_extension_api_snapshot(int function, void far *buffer, int buffer_size)
{
    union REGS regs;
    struct SREGS sregs;
    
    /* Set up ES:DI to point to destination buffer */
    segread(&sregs);
    sregs.es = FP_SEG(buffer);
    regs.x.di = FP_OFF(buffer);
    
    /* Call function */
    regs.h.ah = function;
    regs.h.al = 0;  /* Subfunction not used in atomic API */
    regs.x.cx = buffer_size;
    
    int86x(packet_int, &regs, &regs, &sregs);
    
    /* Return 0 on success (CF=0), 1 on error (CF=1) */
    return regs.x.cflag ? 1 : 0;
}

/**
 * Call driver extension API (legacy for discovery)
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
 * Check if driver supports health diagnostics
 */
int check_driver_support(void)
{
    unsigned int ax, bx, cx, dx;
    
    printf("Checking for 3Com Packet Driver health diagnostics support...\\n");
    
    /* Call EXT_GET_VERSION */
    ax = bx = cx = dx = 0;
    if (call_extension_api(EXT_GET_VERSION, 0, &ax, &bx, &cx, &dx)) {
        printf("ERROR: Driver does not support extensions\\n");
        return 0;
    }
    
    printf("Extension API found: Version %d.%d, Signature 0x%04X\\n", 
           (bx >> 8) & 0xFF, bx & 0xFF, ax);
    printf("Feature bitmap: 0x%04X\\n", cx);
    
    /* Check for health diagnostics support */
    if (!(cx & EXT_FEATURE_DIAGNOSTICS)) {
        printf("ERROR: Health diagnostics not supported by this driver\\n");
        return 0;
    }
    
    printf("Health diagnostics feature: SUPPORTED\\n");
    return 1;
}

/**
 * Get error counter names
 */
const char* get_error_name(int category)
{
    static const char* error_names[ERROR_CATEGORIES] = {
        "TX Errors", "RX Errors", "DMA Errors", "Memory Errors",
        "Hardware Errors", "API Errors", "Buffer Errors", "Timeout Errors"
    };
    
    if (category < 0 || category >= ERROR_CATEGORIES) {
        return "Unknown";
    }
    return error_names[category];
}

/**
 * Get metric names
 */
const char* get_metric_name(int index)
{
    static char name_buffer[32];
    
    if (index >= METRIC_TX_RATE_BASE && index < METRIC_TX_RATE_BASE + 4) {
        sprintf(name_buffer, "NIC %d TX Rate", index - METRIC_TX_RATE_BASE);
    } else if (index >= METRIC_RX_RATE_BASE && index < METRIC_RX_RATE_BASE + 4) {
        sprintf(name_buffer, "NIC %d RX Rate", index - METRIC_RX_RATE_BASE);
    } else if (index >= METRIC_BUFFER_BASE && index < METRIC_BUFFER_BASE + 4) {
        sprintf(name_buffer, "NIC %d Buffer Use", index - METRIC_BUFFER_BASE);
    } else {
        switch (index) {
            case METRIC_CPU_UTIL: strcpy(name_buffer, "CPU Utilization"); break;
            case METRIC_MEMORY_PRESSURE: strcpy(name_buffer, "Memory Pressure"); break;
            case METRIC_ISR_FREQUENCY: strcpy(name_buffer, "ISR Frequency"); break;
            case METRIC_API_FREQUENCY: strcpy(name_buffer, "API Frequency"); break;
            default: strcpy(name_buffer, "Unknown Metric"); break;
        }
    }
    
    return name_buffer;
}

/**
 * Display health status summary
 */
int display_health_status(void)
{
    struct {
        unsigned int safety_flags;
        unsigned int stack_free;
        unsigned int patch_count;
        unsigned int health_code;
    } far safety_snapshot;
    
    printf("\\n=== Health Status Summary ===\\n");
    
    /* Get safety state snapshot */
    if (call_extension_api_snapshot(EXT_GET_SAFETY, &safety_snapshot, sizeof(safety_snapshot))) {
        printf("ERROR: Failed to query health status\\n");
        return 0;
    }
    
    printf("Safety Flags: 0x%04X\\n", safety_snapshot.safety_flags);
    printf("Stack Free: %u bytes\\n", safety_snapshot.stack_free);
    printf("Patches Applied: %u\\n", safety_snapshot.patch_count);
    printf("Health Code: 0x%04X %s\\n", safety_snapshot.health_code,
           safety_snapshot.health_code == 0x0A11 ? "(ALL GOOD)" : "(ISSUES DETECTED)");
    
    return 1;
}

/**
 * Display error counters
 */
int display_error_counters(void)
{
    struct {
        unsigned long error_counts[ERROR_CATEGORIES];
    } far error_snapshot;
    int i;
    unsigned long total_errors = 0;
    
    printf("\\n=== Error Counters ===\\n");
    
    /* Get error counter snapshot */
    if (call_extension_api_snapshot(EXT_GET_ERROR_COUNTERS, &error_snapshot, sizeof(error_snapshot))) {
        printf("ERROR: Failed to get error counters\\n");
        return 0;
    }
    
    printf("Error Category                Count\\n");
    printf("--------------------         --------\\n");
    
    for (i = 0; i < ERROR_CATEGORIES; i++) {
        unsigned long count = error_snapshot.error_counts[i];
        printf("%-20s         %8lu\\n", get_error_name(i), count);
        total_errors += count;
    }
    
    printf("                             --------\\n");
    printf("Total Errors:                %8lu\\n", total_errors);
    
    return 1;
}

/**
 * Display performance metrics
 */
int display_performance_metrics(void)
{
    struct {
        unsigned int metrics[METRIC_COUNT];
    } far perf_snapshot;
    int i;
    
    printf("\\n=== Performance Metrics ===\\n");
    
    /* Get performance metrics snapshot */
    if (call_extension_api_snapshot(EXT_GET_PERF_METRICS, &perf_snapshot, sizeof(perf_snapshot))) {
        printf("ERROR: Failed to get performance metrics\\n");
        return 0;
    }
    
    printf("Metric                       Value    Unit\\n");
    printf("------------------          ------   ------\\n");
    
    /* Display per-NIC TX rates */
    for (i = 0; i < 4; i++) {
        if (perf_snapshot.metrics[METRIC_TX_RATE_BASE + i] > 0) {
            printf("%-18s      %6u   pkt/s\\n", 
                   get_metric_name(METRIC_TX_RATE_BASE + i),
                   perf_snapshot.metrics[METRIC_TX_RATE_BASE + i]);
        }
    }
    
    /* Display per-NIC RX rates */
    for (i = 0; i < 4; i++) {
        if (perf_snapshot.metrics[METRIC_RX_RATE_BASE + i] > 0) {
            printf("%-18s      %6u   pkt/s\\n", 
                   get_metric_name(METRIC_RX_RATE_BASE + i),
                   perf_snapshot.metrics[METRIC_RX_RATE_BASE + i]);
        }
    }
    
    /* Display per-NIC buffer utilization */
    for (i = 0; i < 4; i++) {
        if (perf_snapshot.metrics[METRIC_BUFFER_BASE + i] > 0) {
            printf("%-18s      %6u   %%\\n", 
                   get_metric_name(METRIC_BUFFER_BASE + i),
                   perf_snapshot.metrics[METRIC_BUFFER_BASE + i]);
        }
    }
    
    /* Display system metrics */
    printf("%-18s      %6u   %%\\n", get_metric_name(METRIC_CPU_UTIL), 
           perf_snapshot.metrics[METRIC_CPU_UTIL]);
    printf("%-18s      %6u   %%\\n", get_metric_name(METRIC_MEMORY_PRESSURE), 
           perf_snapshot.metrics[METRIC_MEMORY_PRESSURE]);
    printf("%-18s      %6u   Hz\\n", get_metric_name(METRIC_ISR_FREQUENCY), 
           perf_snapshot.metrics[METRIC_ISR_FREQUENCY]);
    printf("%-18s      %6u   Hz\\n", get_metric_name(METRIC_API_FREQUENCY), 
           perf_snapshot.metrics[METRIC_API_FREQUENCY]);
    
    return 1;
}

/**
 * Analyze health and provide recommendations
 */
void analyze_health(void)
{
    unsigned int ax, bx, cx, dx;
    unsigned int far *counters;
    unsigned int far *metrics;
    int warnings = 0;
    
    printf("\\n=== Health Analysis ===\\n");
    
    /* Get counters */
    ax = bx = cx = dx = 0;
    if (call_extension_api(EXT_GET_DIAGNOSTICS, HEALTH_GET_COUNTERS, &ax, &bx, &cx, &dx)) {
        printf("Unable to analyze - counter data unavailable\\n");
        return;
    }
    counters = (unsigned int far *)MK_FP(bx, dx);
    
    /* Get metrics */
    ax = bx = cx = dx = 0;
    if (call_extension_api(EXT_GET_DIAGNOSTICS, HEALTH_GET_METRICS, &ax, &bx, &cx, &dx)) {
        printf("Unable to analyze - metric data unavailable\\n");
        return;
    }
    metrics = (unsigned int far *)MK_FP(bx, dx);
    
    /* Analyze error rates */
    unsigned long tx_errors = ((unsigned long)counters[ERROR_TX*2+1] << 16) | counters[ERROR_TX*2];
    unsigned long rx_errors = ((unsigned long)counters[ERROR_RX*2+1] << 16) | counters[ERROR_RX*2];
    
    if (tx_errors > THRESHOLD_HIGH_ERROR_RATE || rx_errors > THRESHOLD_HIGH_ERROR_RATE) {
        printf("WARNING: High error rate detected\\n");
        printf("  Recommendation: Check network cables and hardware\\n");
        warnings++;
    }
    
    /* Analyze CPU utilization */
    if (metrics[METRIC_CPU_UTIL] > THRESHOLD_HIGH_CPU_UTIL) {
        printf("WARNING: High CPU utilization (%u%%)\\n", metrics[METRIC_CPU_UTIL]);
        printf("  Recommendation: Consider reducing network load or optimizing applications\\n");
        warnings++;
    }
    
    /* Analyze memory pressure */
    if (metrics[METRIC_MEMORY_PRESSURE] > THRESHOLD_HIGH_MEMORY) {
        printf("WARNING: High memory pressure (%u%%)\\n", metrics[METRIC_MEMORY_PRESSURE]);
        printf("  Recommendation: Check for memory leaks or increase available memory\\n");
        warnings++;
    }
    
    /* Analyze throughput */
    int low_throughput_nics = 0;
    int i;
    for (i = 0; i < 4; i++) {
        if (metrics[METRIC_TX_RATE_BASE + i] > 0 && 
            metrics[METRIC_TX_RATE_BASE + i] < THRESHOLD_LOW_THROUGHPUT) {
            low_throughput_nics++;
        }
    }
    
    if (low_throughput_nics > 0) {
        printf("INFO: %d NIC(s) with low throughput detected\\n", low_throughput_nics);
        printf("  This may be normal if network traffic is light\\n");
    }
    
    if (warnings == 0) {
        printf("System health: GOOD\\n");
        printf("No performance issues detected.\\n");
    } else {
        printf("System health: %d WARNING(S)\\n", warnings);
        printf("Review recommendations above.\\n");
    }
}

/**
 * Display interrupt mitigation statistics
 */
int display_mitigation_stats(void)
{
    struct {
        unsigned int enabled;
        unsigned int work_limit;
        unsigned long total_interrupts;
        unsigned long batched_packets;
        unsigned long interrupts_saved;
        unsigned int max_batch_size;
    } far mitigation_snapshot;
    
    printf("\\n=== Interrupt Mitigation Statistics ===\\n");
    
    /* Get mitigation snapshot */
    if (call_extension_api_snapshot(EXT_GET_MITIGATION, &mitigation_snapshot, sizeof(mitigation_snapshot))) {
        printf("ERROR: Failed to get mitigation statistics\\n");
        return 0;
    }
    
    printf("Status: %s\\n", mitigation_snapshot.enabled ? "ENABLED" : "DISABLED");
    printf("Work Limit: %u packets/interrupt\\n", mitigation_snapshot.work_limit);
    printf("Total Interrupts: %lu\\n", mitigation_snapshot.total_interrupts);
    printf("Batched Packets: %lu\\n", mitigation_snapshot.batched_packets);
    printf("Interrupts Saved: %lu\\n", mitigation_snapshot.interrupts_saved);
    printf("Max Batch Size: %u\\n", mitigation_snapshot.max_batch_size);
    
    if (mitigation_snapshot.total_interrupts > 0) {
        float avg_batch = (float)mitigation_snapshot.batched_packets / mitigation_snapshot.total_interrupts;
        float reduction = (float)mitigation_snapshot.interrupts_saved * 100.0f / 
                         (mitigation_snapshot.total_interrupts + mitigation_snapshot.interrupts_saved);
        printf("Average Batch Size: %.2f packets\\n", avg_batch);
        printf("Interrupt Reduction: %.1f%%\\n", reduction);
    }
    
    return 1;
}

/**
 * Display DMA policy and statistics
 */
int display_dma_stats(void)
{
    struct {
        unsigned char runtime_enable;
        unsigned char validation_passed;
        unsigned char last_known_safe;
        unsigned char cache_tier;
        unsigned long dma_transfers;
        unsigned long bounce_buffers;
        unsigned long boundary_violations;
    } far dma_snapshot;
    
    printf("\\n=== DMA Policy & Statistics ===\\n");
    
    /* Get DMA snapshot */
    if (call_extension_api_snapshot(EXT_GET_DMA_STATS, &dma_snapshot, sizeof(dma_snapshot))) {
        printf("ERROR: Failed to get DMA statistics\\n");
        return 0;
    }
    
    printf("DMA Policy:\\n");
    printf("  Runtime Enable: %s\\n", dma_snapshot.runtime_enable ? "YES" : "NO");
    printf("  Validation Passed: %s\\n", dma_snapshot.validation_passed ? "YES" : "NO");
    printf("  Last Known Safe: %s\\n", dma_snapshot.last_known_safe ? "YES" : "NO");
    printf("  Cache Tier: %u\\n", dma_snapshot.cache_tier);
    
    printf("\\nDMA Statistics:\\n");
    printf("  DMA Transfers: %lu\\n", dma_snapshot.dma_transfers);
    printf("  Bounce Buffers Used: %lu\\n", dma_snapshot.bounce_buffers);
    printf("  64KB Boundary Violations: %lu\\n", dma_snapshot.boundary_violations);
    
    if (dma_snapshot.dma_transfers > 0) {
        float bounce_rate = (float)dma_snapshot.bounce_buffers * 100.0f / dma_snapshot.dma_transfers;
        printf("  Bounce Buffer Rate: %.1f%%\\n", bounce_rate);
    }
    
    return 1;
}

/**
 * Reset diagnostic statistics
 */
int reset_statistics(void)
{
    unsigned int ax, bx, cx, dx;
    
    printf("\\nResetting diagnostic statistics...\\n");
    
    /* Call reset function */
    ax = bx = cx = dx = 0;
    if (call_extension_api(EXT_CONFIG_DIAGNOSTICS, HEALTH_RESET_STATS, &ax, &bx, &cx, &dx)) {
        printf("ERROR: Failed to reset statistics\\n");
        return 0;
    }
    
    printf("Statistics reset successfully\\n");
    return 1;
}

/**
 * Main program
 */
int main(int argc, char *argv[])
{
    int show_counters = 1;
    int show_metrics = 1;
    int show_analysis = 1;
    int reset_stats = 0;
    int i;
    
    printf("3Com Packet Driver Health Diagnostics Utility v1.0\\n");
    printf("GPT-5 Stage 2: External Sidecar Architecture\\n\\n");
    
    /* Parse command line arguments */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--counters-only") == 0) {
            show_metrics = 0;
            show_analysis = 0;
        } else if (strcmp(argv[i], "--metrics-only") == 0) {
            show_counters = 0;
            show_analysis = 0;
        } else if (strcmp(argv[i], "--analysis-only") == 0) {
            show_counters = 0;
            show_metrics = 0;
        } else if (strcmp(argv[i], "--reset") == 0) {
            reset_stats = 1;
        } else if (strncmp(argv[i], "--int=", 6) == 0) {
            packet_int = strtol(argv[i] + 6, NULL, 16);
            printf("Using packet driver interrupt: 0x%02X\\n", packet_int);
        } else {
            printf("Usage: %s [options]\\n", argv[0]);
            printf("Options:\\n");
            printf("  --counters-only    Show only error counters\\n");
            printf("  --metrics-only     Show only performance metrics\\n");
            printf("  --analysis-only    Show only health analysis\\n");
            printf("  --reset            Reset all statistics\\n");
            printf("  --int=XX           Use interrupt XX (hex)\\n");
            return 1;
        }
    }
    
    /* Verify driver support */
    if (!check_driver_support()) {
        printf("\\nDriver support check failed. Please ensure:\\n");
        printf("1. 3Com packet driver is loaded\\n"); 
        printf("2. Driver supports extension API\\n");
        printf("3. Health diagnostics feature is enabled\\n");
        printf("4. Correct interrupt vector (default 0x60)\\n");
        return 1;
    }
    
    /* Display health status */
    if (!display_health_status()) {
        return 1;
    }
    
    /* Reset statistics if requested */
    if (reset_stats) {
        if (!reset_statistics()) {
            return 1;
        }
        printf("\\nUse this utility again to view fresh statistics.\\n");
        return 0;
    }
    
    /* Display requested information */
    if (show_counters) {
        if (!display_error_counters()) {
            return 1;
        }
    }
    
    if (show_metrics) {
        if (!display_performance_metrics()) {
            return 1;
        }
    }
    
    /* Always show mitigation and DMA stats if available */
    display_mitigation_stats();
    display_dma_stats();
    /* Show PCMCIA/CardBus status */
    {
        extern int display_pcmcia_snapshot(void);
        display_pcmcia_snapshot();
    }
    
    if (show_analysis) {
        analyze_health();
    }
    
    return 0;
}
