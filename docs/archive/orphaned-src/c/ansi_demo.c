/**
 * @file ansi_demo.c
 * @brief ANSI Color Demo Program for 3Com Packet Driver
 *
 * Demonstrates the Quarterdeck-style ANSI color interface capabilities.
 * This program can be compiled separately to test ANSI functionality
 * before integrating into the main driver.
 *
 * Compile: tcc ansi_demo.c console.c -o ansi_demo.exe
 * Usage: ansi_demo.exe
 */

#include "../include/console.h"
#include "../include/nic_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <time.h>

// Mock NIC data for demonstration
static nic_info_t demo_nics[2] = {
    {
        .io_base = 0x300,
        .irq = 10,
        .mac = {0x00, 0x60, 0x97, 0x2B, 0xA4, 0xF1},
        .link_up = true,
        .speed = 10,
        .tx_packets = 15234,
        .rx_packets = 28451,
        .tx_bytes = 1524000,
        .rx_bytes = 2845100,
        .tx_errors = 0,
        .rx_errors = 0,
        .status = NIC_STATUS_ACTIVE
    },
    {
        .io_base = 0x240,
        .irq = 5,
        .mac = {0x00, 0x10, 0x5A, 0x44, 0xBC, 0x2D},
        .link_up = true,
        .speed = 100,
        .tx_packets = 92451,
        .rx_packets = 184223,
        .tx_bytes = 9245100,
        .rx_bytes = 18422300,
        .tx_errors = 2,
        .rx_errors = 1,
        .status = NIC_STATUS_ACTIVE
    }
};

// Function prototypes
static void demo_banner_display(void);
static void demo_detection_sequence(void);
static void demo_network_monitor(void);
static void demo_color_palette(void);
static void demo_box_drawing(void);
static void demo_diagnostic_messages(void);
static void wait_for_key(void);

/**
 * Main demo program
 */
int main(void) {
    int choice;
    
    // Initialize console system
    console_init();
    
    if (!console_ansi_detected()) {
        printf("ANSI.SYS not detected. Some features may not display correctly.\n");
        printf("For best results, load ANSI.SYS in CONFIG.SYS or use a compatible terminal.\n\n");
    }
    
    while (1) {
        clear_screen();
        
        // Main menu
        draw_quarterdeck_header("3Com Packet Driver ANSI Demo", "1.0");
        
        goto_xy(1, 6);
        draw_box(1, 6, CONSOLE_WIDTH(), 12, "Demo Menu", false);
        
        goto_xy(3, 8);
        printf("Select a demonstration:\n");
        printf("\n");
        printf("   [1] Driver Banner and Startup Sequence\n");
        printf("   [2] Hardware Detection Display\n");
        printf("   [3] Network Monitor Interface\n");
        printf("   [4] Color Palette Test\n");
        printf("   [5] Box Drawing Characters\n");
        printf("   [6] Diagnostic Messages\n");
        printf("   [0] Exit Demo\n");
        printf("\n");
        printf("   Choice: ");
        
        choice = getch();
        
        switch (choice) {
            case '1':
                demo_banner_display();
                break;
            case '2':
                demo_detection_sequence();
                break;
            case '3':
                demo_network_monitor();
                break;
            case '4':
                demo_color_palette();
                break;
            case '5':
                demo_box_drawing();
                break;
            case '6':
                demo_diagnostic_messages();
                break;
            case '0':
            case 27:  // ESC
                goto cleanup;
            default:
                continue;
        }
        
        wait_for_key();
    }
    
cleanup:
    console_cleanup();
    return 0;
}

/**
 * Demo: Driver banner and startup sequence
 */
static void demo_banner_display(void) {
    clear_screen();
    
    // Main banner
    display_driver_banner("1.0 Demo");
    
    delay_ms(1000);
    
    // Subtitle and copyright
    goto_xy(1, 5);
    set_color(g_palette.info, g_palette.normal_bg);
    printf("%s\n", center_text("Copyright (C) 2024 - Enhanced DOS Network Support", CONSOLE_WIDTH()));
    
    goto_xy(1, 6);
    set_color(g_palette.frame, g_palette.normal_bg);
    printf("%s\n", center_text("Supporting: 3c509, 3c509B, 3c515, 3c590, 3c595, 3c900", CONSOLE_WIDTH()));
    
    delay_ms(2000);
    
    // System information
    goto_xy(1, 9);
    draw_box(1, 9, CONSOLE_WIDTH(), 8, "System Information", false);
    
    goto_xy(3, 11);
    print_status("DOS Version", "6.22", g_palette.data);
    printf("    ");
    print_status("Memory", "640KB", g_palette.data);
    printf("    ");
    print_status("CPU", "i486DX", g_palette.data);
    
    goto_xy(3, 12);
    print_status("ANSI Support", console_ansi_detected() ? "YES" : "NO", 
                  console_ansi_detected() ? g_palette.status_ok : g_palette.status_err);
    printf("  ");
    print_status("Colors", console_colors_enabled() ? "ENABLED" : "DISABLED",
                  console_colors_enabled() ? g_palette.status_ok : g_palette.status_warn);
    
    goto_xy(3, 14);
    set_color(g_palette.info, g_palette.normal_bg);
    printf("Screen: ");
    set_color(g_palette.data, g_palette.normal_bg);
    printf("%dx%d", CONSOLE_WIDTH(), CONSOLE_HEIGHT());
    
    reset_colors();
}

/**
 * Demo: Hardware detection sequence
 */
static void demo_detection_sequence(void) {
    clear_screen();
    display_driver_banner("1.0 Demo");
    
    // Show detection progress
    display_detection_progress();
    delay_ms(1500);
    
    // Simulate finding NICs
    for (int i = 0; i < 2; i++) {
        display_detected_nic(&demo_nics[i], i, true);
        delay_ms(800);
    }
    
    delay_ms(1000);
    
    // Show configuration
    display_configuration_progress();
    delay_ms(2000);
    
    // Show final status
    display_nic_status_summary(demo_nics, 2);
    
    delay_ms(1000);
    display_tsr_loaded(0xC800, 0x60, 20);
}

/**
 * Demo: Network monitor interface
 */
static void demo_network_monitor(void) {
    int updates = 0;
    
    while (updates < 10 && !kbhit()) {
        // Simulate changing network activity
        for (int i = 0; i < 2; i++) {
            demo_nics[i].tx_packets += rand() % 100;
            demo_nics[i].rx_packets += rand() % 150;
            demo_nics[i].tx_bytes += demo_nics[i].tx_packets * 64;
            demo_nics[i].rx_bytes += demo_nics[i].rx_packets * 64;
        }
        
        display_network_monitor(demo_nics, 2);
        delay_ms(1000);
        updates++;
    }
    
    if (kbhit()) getch();  // Clear keyboard buffer
}

/**
 * Demo: Color palette test
 */
static void demo_color_palette(void) {
    clear_screen();
    draw_quarterdeck_header("Color Palette Test", "1.0");
    
    goto_xy(1, 6);
    draw_box(1, 6, CONSOLE_WIDTH(), 16, "ANSI Color Palette", false);
    
    // Standard colors
    goto_xy(3, 8);
    set_color(g_palette.info, g_palette.normal_bg);
    printf("Standard Colors:\n");
    
    const char *color_names[] = {
        "Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown", "White",
        "Gray", "Bright Blue", "Bright Green", "Bright Cyan", 
        "Bright Red", "Bright Magenta", "Yellow", "Bright White"
    };
    
    for (int i = 0; i < 16; i++) {
        goto_xy(3 + (i % 4) * 18, 10 + (i / 4));
        set_color(i, g_palette.normal_bg);
        printf("%-15s", color_names[i]);
    }
    
    reset_colors();
    
    // Quarterdeck palette
    goto_xy(3, 15);
    set_color(g_palette.info, g_palette.normal_bg);
    printf("Quarterdeck Palette:\n");
    
    goto_xy(3, 17);
    set_color(g_palette.header_fg, g_palette.header_bg);
    printf(" Header ");
    reset_colors();
    printf("  ");
    
    display_status_indicator("OK", g_palette.status_ok);
    printf("  ");
    display_status_indicator("WARNING", g_palette.status_warn);
    printf("  ");
    display_status_indicator("ERROR", g_palette.status_err);
    
    goto_xy(3, 19);
    set_color(g_palette.info, g_palette.normal_bg);
    printf("Info Text");
    printf("  ");
    set_color(g_palette.data, g_palette.normal_bg);
    printf("Data Values");
    printf("  ");
    set_color(g_palette.accent, g_palette.normal_bg);
    printf("Accent");
    printf("  ");
    set_color(g_palette.frame, g_palette.normal_bg);
    printf("Frame");
    
    reset_colors();
}

/**
 * Demo: Box drawing characters
 */
static void demo_box_drawing(void) {
    clear_screen();
    draw_quarterdeck_header("Box Drawing Test", "1.0");
    
    // Single line box
    goto_xy(5, 8);
    draw_box(5, 8, 30, 6, "Single Line Box", false);
    
    goto_xy(7, 10);
    printf("Single line borders");
    goto_xy(7, 11);
    printf("Standard box drawing");
    
    // Double line box
    goto_xy(40, 8);
    draw_box(40, 8, 30, 6, "Double Line Box", true);
    
    goto_xy(42, 10);
    printf("Double line borders");
    goto_xy(42, 11);
    printf("Header-style box");
    
    // Nested boxes
    goto_xy(10, 16);
    draw_box(10, 16, 60, 8, "Nested Box Example", false);
    
    goto_xy(15, 18);
    draw_box(15, 18, 20, 4, "Inner", false);
    
    goto_xy(40, 18);
    draw_box(40, 18, 20, 4, "Inner", false);
    
    // Character demonstration
    goto_xy(5, 26);
    set_color(g_palette.info, g_palette.normal_bg);
    printf("Characters: ");
    set_color(g_palette.data, g_palette.normal_bg);
    printf("%s %s %s %s %s %s %s", 
           g_box_chars.horizontal, g_box_chars.vertical,
           g_box_chars.top_left, g_box_chars.top_right,
           g_box_chars.bottom_left, g_box_chars.bottom_right,
           g_box_chars.cross);
    
    reset_colors();
}

/**
 * Demo: Diagnostic messages
 */
static void demo_diagnostic_messages(void) {
    clear_screen();
    draw_quarterdeck_header("Diagnostic Messages", "1.0");
    
    goto_xy(1, 6);
    draw_box(1, 6, CONSOLE_WIDTH(), 15, "System Messages", false);
    
    const char *messages[] = {
        "Packet driver initialization started",
        "3C509B: EEPROM read successful",
        "3C509B: Link beat detected, carrier established", 
        "3C515: Bus master DMA initialized successfully",
        "Network interface configuration complete",
        "RX buffer overflow, 3 packets dropped",
        "Adapter failure detected, attempting recovery...",
        "Adapter recovered, operation resumed",
        "Network traffic: 1,245 pkt/s (15% utilization)",
        "Driver loaded successfully in TSR mode"
    };
    
    const char *levels[] = {
        "INFO", "INFO", "INFO", "INFO", "SUCCESS",
        "WARNING", "ERROR", "SUCCESS", "INFO", "SUCCESS"
    };
    
    for (int i = 0; i < 10; i++) {
        goto_xy(3, 8 + i);
        display_diagnostic_message(levels[i], messages[i]);
        delay_ms(500);
    }
}

/**
 * Wait for key press with prompt
 */
static void wait_for_key(void) {
    goto_xy(1, CONSOLE_HEIGHT() - 1);
    set_color(g_palette.frame, g_palette.normal_bg);
    printf("Press any key to continue...");
    reset_colors();
    
    getch();
}