/**
 * @file nic_display.c
 * @brief Network Interface Display Functions using Quarterdeck-Style Console
 *
 * 3Com Packet Driver - Professional Network Interface Display
 *
 * This module provides the high-level network interface display functions
 * that create the classic Quarterdeck-style professional interface for
 * DOS network administrators and power users.
 */

#include "../include/console.h"
#include "../include/common.h"
#include "../include/3c509b.h"
#include "../include/3c515.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// Network activity history for graphing
#define ACTIVITY_HISTORY_SIZE 60
static uint32_t g_activity_history[ACTIVITY_HISTORY_SIZE];
static int g_activity_pos = 0;

// Performance counters
typedef struct {
    uint32_t total_packets;
    uint32_t total_bytes;
    uint32_t packets_per_sec;
    uint32_t bytes_per_sec;
    time_t last_update;
} perf_counters_t;

static perf_counters_t g_perf_counters = {0};

// Function prototypes
static void update_activity_history(uint32_t packets);
static void update_performance_counters(nic_info_t *nics, int nic_count);
static const char *get_nic_type_string(nic_info_t *nic);
static const char *get_media_type_string(nic_info_t *nic);
static ansi_color_t get_status_color(nic_info_t *nic);

/**
 * Display the main driver loading banner
 */
void display_driver_banner(const char *version) {
    if (!console_colors_enabled()) {
        printf("3Com EtherLink Packet Driver v%s\n", version);
        printf("Enhanced DOS Network Support Suite\n");
        printf("================================================================================\n");
        return;
    }
    
    draw_quarterdeck_header("3Com EtherLink Packet Driver", version);
    
    // Subtitle
    goto_xy(1, 4);
    set_color(g_palette.info, g_palette.normal_bg);
    const char *subtitle = center_text("Enhanced DOS Network Support Suite", CONSOLE_WIDTH());
    printf("%s\n", subtitle);
    
    reset_colors();
}

/**
 * Display hardware detection progress
 */
void display_detection_progress(void) {
    if (!console_colors_enabled()) {
        printf("Scanning for network hardware...\n");
        return;
    }
    
    goto_xy(1, 6);
    draw_box(1, 6, CONSOLE_WIDTH(), 6, "Hardware Detection", false);
    
    goto_xy(3, 8);
    set_color(g_palette.info, g_palette.normal_bg);
    printf("Scanning ISA bus...");
    
    goto_xy(CONSOLE_WIDTH() - 15, 8);
    display_status_indicator("SCANNING", g_palette.status_warn);
    
    reset_colors();
}

/**
 * Display detected NIC with status
 */
void display_detected_nic(nic_info_t *nic, int nic_index, bool success) {
    if (!console_colors_enabled()) {
        if (success) {
            printf("Found: %s at I/O 0x%X, IRQ %d\n", 
                   get_nic_type_string(nic), nic->io_base, nic->irq);
        } else {
            printf("Failed to initialize NIC at I/O 0x%X\n", nic->io_base);
        }
        return;
    }
    
    int line = 8 + nic_index;
    goto_xy(3, line);
    
    if (success) {
        set_color(g_palette.info, g_palette.normal_bg);
        printf("Found: %s at I/O 0x%X, IRQ %d", 
               get_nic_type_string(nic), nic->io_base, nic->irq);
        
        goto_xy(CONSOLE_WIDTH() - 15, line);
        display_status_indicator("DETECTED", g_palette.status_ok);
    } else {
        set_color(g_palette.status_err, g_palette.normal_bg);
        printf("Failed: NIC at I/O 0x%X", nic->io_base);
        
        goto_xy(CONSOLE_WIDTH() - 15, line);
        display_status_indicator("ERROR", g_palette.status_err);
    }
    
    reset_colors();
}

/**
 * Display driver configuration progress
 */
void display_configuration_progress(void) {
    if (!console_colors_enabled()) {
        printf("\nConfiguring packet driver...\n");
        return;
    }
    
    goto_xy(1, 13);
    draw_box(1, 13, CONSOLE_WIDTH(), 8, "Driver Configuration", false);
    
    // Configuration steps
    const char *steps[] = {
        "Loading packet driver API...",
        "Setting up interrupt handlers...",
        "Allocating packet buffers...",
        "Initializing network interfaces...",
        "Enabling network operations..."
    };
    
    for (int i = 0; i < 5; i++) {
        goto_xy(3, 15 + i);
        set_color(g_palette.info, g_palette.normal_bg);
        printf("%-50s", steps[i]);
        
        goto_xy(CONSOLE_WIDTH() - 12, 15 + i);
        STATUS_OK();
        
        // Small delay for visual effect
        delay_ms(200);
    }
    
    reset_colors();
}

/**
 * Display network interface status summary
 */
void display_nic_status_summary(nic_info_t *nics, int nic_count) {
    if (!console_colors_enabled()) {
        printf("\nNetwork Interfaces:\n");
        for (int i = 0; i < nic_count; i++) {
            printf("NIC #%d: %s  MAC: %s  Link: %s  Speed: %d Mbps\n",
                   i + 1, get_nic_type_string(&nics[i]),
                   format_mac_address(nics[i].mac),
                   nics[i].link_up ? "UP" : "DOWN",
                   nics[i].speed);
        }
        return;
    }
    
    goto_xy(1, 22);
    draw_box(1, 22, CONSOLE_WIDTH(), nic_count + 2, "Network Status", false);
    
    for (int i = 0; i < nic_count; i++) {
        goto_xy(3, 24 + i);
        
        // NIC number and type
        set_color(g_palette.info, g_palette.normal_bg);
        printf("NIC #%d: %-12s", i + 1, get_nic_type_string(&nics[i]));
        
        // MAC address
        printf("MAC: ");
        set_color(g_palette.data, g_palette.normal_bg);
        printf("%-17s", format_mac_address(nics[i].mac));
        
        // Link status
        set_color(g_palette.info, g_palette.normal_bg);
        printf("Link: ");
        if (nics[i].link_up) {
            set_color(g_palette.status_ok, g_palette.normal_bg);
            printf("%-4s", "UP");
        } else {
            set_color(g_palette.status_err, g_palette.normal_bg);
            printf("%-4s", "DOWN");
        }
        
        // Speed
        set_color(g_palette.info, g_palette.normal_bg);
        printf("Speed: ");
        set_color(g_palette.data, g_palette.normal_bg);
        printf("%d Mbps", nics[i].speed);
        
        // Status indicator
        goto_xy(CONSOLE_WIDTH() - 12, 24 + i);
        display_status_indicator("ACTIVE", get_status_color(&nics[i]));
    }
    
    reset_colors();
}

/**
 * Display full-screen network monitor
 */
void display_network_monitor(nic_info_t *nics, int nic_count) {
    update_performance_counters(nics, nic_count);
    
    clear_screen();
    draw_quarterdeck_header("3Com Packet Driver", "Network Monitor");
    
    // Active interfaces section
    goto_xy(1, 5);
    draw_box(1, 5, CONSOLE_WIDTH(), 8 + (nic_count * 3), "Active Network Interfaces", false);
    
    int current_line = 7;
    for (int i = 0; i < nic_count; i++) {
        // NIC header box
        goto_xy(3, current_line);
        set_color(g_palette.frame, g_palette.normal_bg);
        printf("%s %s ", g_box_chars.top_left, get_nic_type_string(&nics[i]));
        for (int j = strlen(get_nic_type_string(&nics[i])) + 5; j < CONSOLE_WIDTH() - 15; j++) {
            printf("%s", g_box_chars.horizontal);
        }
        
        goto_xy(CONSOLE_WIDTH() - 12, current_line);
        display_status_indicator("ACTIVE", get_status_color(&nics[i]));
        set_color(g_palette.frame, g_palette.normal_bg);
        printf(" %s", g_box_chars.top_right);
        
        // NIC details line 1
        goto_xy(3, current_line + 1);
        printf("%s ", g_box_chars.vertical);
        print_status("I/O", "", g_palette.info);
        printf("0x%03X  ", nics[i].io_base);
        print_status("IRQ", "", g_palette.info);
        printf("%-2d  ", nics[i].irq);
        print_status("Link", "", g_palette.info);
        if (nics[i].link_up) {
            set_color(g_palette.status_ok, g_palette.normal_bg);
            printf("%-4s", "UP");
        } else {
            set_color(g_palette.status_err, g_palette.normal_bg);
            printf("%-4s", "DOWN");
        }
        print_status("  Speed", "", g_palette.info);
        printf("%d Mbps", nics[i].speed);
        
        goto_xy(CONSOLE_WIDTH() - 2, current_line + 1);
        set_color(g_palette.frame, g_palette.normal_bg);
        printf("%s", g_box_chars.vertical);
        
        // NIC details line 2
        goto_xy(3, current_line + 2);
        printf("%s ", g_box_chars.vertical);
        print_status("MAC", "", g_palette.info);
        printf("%-17s  ", format_mac_address(nics[i].mac));
        print_status("TX", "", g_palette.info);
        printf("%-8lu  ", nics[i].tx_packets);
        print_status("RX", "", g_palette.info);
        printf("%-8lu", nics[i].rx_packets);
        
        if (nics[i].tx_errors || nics[i].rx_errors) {
            print_status("  Errors", "", g_palette.info);
            set_color(g_palette.status_err, g_palette.normal_bg);
            printf("%lu", nics[i].tx_errors + nics[i].rx_errors);
        }
        
        goto_xy(CONSOLE_WIDTH() - 2, current_line + 2);
        set_color(g_palette.frame, g_palette.normal_bg);
        printf("%s", g_box_chars.vertical);
        
        // Bottom border
        goto_xy(3, current_line + 3);
        printf("%s", g_box_chars.bottom_left);
        for (int j = 0; j < CONSOLE_WIDTH() - 6; j++) {
            printf("%s", g_box_chars.horizontal);
        }
        printf("%s", g_box_chars.bottom_right);
        
        current_line += 4;
    }
    
    // Network activity graph
    current_line += 1;
    goto_xy(1, current_line);
    draw_box(1, current_line, CONSOLE_WIDTH(), 6, "Network Activity", false);
    
    goto_xy(3, current_line + 2);
    set_color(g_palette.info, g_palette.normal_bg);
    printf("Total: ");
    display_network_activity_graph(g_activity_history, ACTIVITY_HISTORY_SIZE, 50);
    printf(" %s", format_packets_per_sec(g_perf_counters.packets_per_sec));
    
    // Individual NIC activity
    for (int i = 0; i < nic_count && i < 2; i++) {  // Show max 2 NICs in graph
        goto_xy(3, current_line + 3 + i);
        printf("%-6s ", get_nic_type_string(&nics[i]));
        
        // Simple activity indicator (would need per-NIC history in real implementation)
        uint32_t nic_history[50];
        for (int j = 0; j < 50; j++) {
            nic_history[j] = (nics[i].tx_packets + nics[i].rx_packets) / 50;
        }
        display_network_activity_graph(nic_history, 50, 40);
        printf(" %lu pkt/s", (nics[i].tx_packets + nics[i].rx_packets) / 60);
    }
    
    // Command footer
    draw_quarterdeck_footer(" F1-Help  F2-Config  F3-Stats  F4-Test  ESC-Exit");
    
    reset_colors();
}

/**
 * Display error/diagnostic messages with timestamp
 */
void display_diagnostic_message(const char *level, const char *message) {
    static int message_line = 0;
    const int max_messages = 10;
    
    if (!console_colors_enabled()) {
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        printf("%02d:%02d:%02d [%s] %s\n", 
               tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec, level, message);
        return;
    }
    
    // Position in message area (would need proper window management)
    goto_xy(1, 15 + (message_line % max_messages));
    
    // Timestamp
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    set_color(g_palette.frame, g_palette.normal_bg);
    printf("%02d:%02d:%02d ", tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
    
    // Level indicator with color
    ansi_color_t level_color = g_palette.info;
    if (strcmp(level, "ERROR") == 0) {
        level_color = g_palette.status_err;
    } else if (strcmp(level, "WARNING") == 0) {
        level_color = g_palette.status_warn;
    } else if (strcmp(level, "SUCCESS") == 0) {
        level_color = g_palette.status_ok;
    }
    
    set_color(level_color, g_palette.normal_bg);
    printf("[%-7s] ", level);
    
    // Message
    set_color(g_palette.normal_fg, g_palette.normal_bg);
    printf("%-60s", message);
    
    message_line++;
    reset_colors();
}

/**
 * Display TSR loaded confirmation
 */
void display_tsr_loaded(uint16_t segment, uint8_t interrupt, uint16_t size_kb) {
    if (!console_colors_enabled()) {
        printf("\nTSR loaded at %04X:0000, Int %02Xh, %dKB resident\n", 
               segment, interrupt, size_kb);
        printf("Press any key to continue...\n");
        return;
    }
    
    goto_xy(1, CONSOLE_HEIGHT() - 3);
    
    set_color(g_palette.info, g_palette.normal_bg);
    printf("Driver resident at segment ");
    set_color(g_palette.data, g_palette.normal_bg);
    printf("0x%04X", segment);
    
    set_color(g_palette.info, g_palette.normal_bg);
    printf(" using ");
    set_color(g_palette.data, g_palette.normal_bg);
    printf("%dKB", size_kb);
    
    set_color(g_palette.info, g_palette.normal_bg);
    printf(" (TSR Mode)\n");
    
    set_color(g_palette.frame, g_palette.normal_bg);
    printf("Press any key to continue...");
    
    reset_colors();
}

// === Internal Helper Functions ===

/**
 * Update network activity history for graphing
 */
static void update_activity_history(uint32_t packets) {
    g_activity_history[g_activity_pos] = packets;
    g_activity_pos = (g_activity_pos + 1) % ACTIVITY_HISTORY_SIZE;
}

/**
 * Update performance counters
 */
static void update_performance_counters(nic_info_t *nics, int nic_count) {
    time_t now = time(NULL);
    
    uint32_t total_packets = 0;
    uint32_t total_bytes = 0;
    
    for (int i = 0; i < nic_count; i++) {
        total_packets += nics[i].tx_packets + nics[i].rx_packets;
        total_bytes += nics[i].tx_bytes + nics[i].rx_bytes;
    }
    
    if (g_perf_counters.last_update > 0) {
        time_t elapsed = now - g_perf_counters.last_update;
        if (elapsed > 0) {
            g_perf_counters.packets_per_sec = 
                (total_packets - g_perf_counters.total_packets) / elapsed;
            g_perf_counters.bytes_per_sec = 
                (total_bytes - g_perf_counters.total_bytes) / elapsed;
        }
    }
    
    g_perf_counters.total_packets = total_packets;
    g_perf_counters.total_bytes = total_bytes;
    g_perf_counters.last_update = now;
    
    update_activity_history(g_perf_counters.packets_per_sec);
}

/**
 * Get NIC type string for display
 */
static const char *get_nic_type_string(nic_info_t *nic) {
    // This would be enhanced to read from NIC identification
    if (nic->io_base < 0x300) {
        return "3C515-TX";
    } else {
        return "3C509B-TP";
    }
}

/**
 * Get media type string for display  
 */
static const char *get_media_type_string(nic_info_t *nic) {
    if (nic->speed >= 100) {
        return "100Base-TX";
    } else {
        return "10Base-T";
    }
}

/**
 * Get appropriate status color for NIC
 */
static ansi_color_t get_status_color(nic_info_t *nic) {
    if (nic->status & NIC_STATUS_ERROR) {
        return g_palette.status_err;
    } else if (nic->link_up && (nic->status & NIC_STATUS_ACTIVE)) {
        return g_palette.status_ok;
    } else {
        return g_palette.status_warn;
    }
}