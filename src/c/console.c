/**
 * @file console.c
 * @brief ANSI Color Console Implementation for Quarterdeck-Style Output
 *
 * 3Com Packet Driver - Enhanced Console Interface
 *
 * This file implements the ANSI color console system with automatic
 * detection and graceful fallback for DOS environments. Provides that
 * classic Quarterdeck-style professional interface that DOS power users
 * remember and love.
 */

#include "../include/console.h"
#include "../include/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <conio.h>

// Global Console State
console_state_t g_console = {
    .ansi_detected = false,
    .color_enabled = false,
    .unicode_supported = false,
    .screen_width = 80,
    .screen_height = 25,
    .current_fg = COLOR_WHITE,
    .current_bg = COLOR_BLACK,
    .cursor_x = 0,
    .cursor_y = 0
};

// Quarterdeck-Style Color Palettes
const quarterdeck_palette_t PALETTE_QUARTERDECK = {
    .header_fg = COLOR_BRIGHT_WHITE,
    .header_bg = COLOR_BLUE,
    .status_ok = COLOR_BRIGHT_GREEN,
    .status_warn = COLOR_YELLOW,
    .status_err = COLOR_BRIGHT_RED,
    .info = COLOR_BRIGHT_CYAN,
    .data = COLOR_WHITE,
    .accent = COLOR_BRIGHT_MAGENTA,
    .frame = COLOR_GRAY,
    .normal_fg = COLOR_WHITE,
    .normal_bg = COLOR_BLACK
};

const quarterdeck_palette_t PALETTE_MONOCHROME = {
    .header_fg = COLOR_BRIGHT_WHITE,
    .header_bg = COLOR_BLACK,
    .status_ok = COLOR_WHITE,
    .status_warn = COLOR_WHITE,
    .status_err = COLOR_BRIGHT_WHITE,
    .info = COLOR_WHITE,
    .data = COLOR_WHITE,
    .accent = COLOR_WHITE,
    .frame = COLOR_WHITE,
    .normal_fg = COLOR_WHITE,
    .normal_bg = COLOR_BLACK
};

const quarterdeck_palette_t PALETTE_GREEN_SCREEN = {
    .header_fg = COLOR_BRIGHT_GREEN,
    .header_bg = COLOR_BLACK,
    .status_ok = COLOR_GREEN,
    .status_warn = COLOR_BRIGHT_GREEN,
    .status_err = COLOR_BRIGHT_GREEN,
    .info = COLOR_GREEN,
    .data = COLOR_GREEN,
    .accent = COLOR_BRIGHT_GREEN,
    .frame = COLOR_GREEN,
    .normal_fg = COLOR_GREEN,
    .normal_bg = COLOR_BLACK
};

// Current active palette
quarterdeck_palette_t g_palette;

// Box Drawing Characters
box_chars_t g_box_chars;

// Unicode box characters (when supported)
static const box_chars_t UNICODE_BOX_CHARS = {
    .horizontal = "─",
    .vertical = "│",
    .top_left = "┌",
    .top_right = "┐",
    .bottom_left = "└",
    .bottom_right = "┘",
    .cross = "┼",
    .tee_down = "┬",
    .tee_up = "┴",
    .tee_right = "├",
    .tee_left = "┤",
    .double_horizontal = "═",
    .double_vertical = "║",
    .double_top_left = "╔",
    .double_top_right = "╗",
    .double_bottom_left = "╚",
    .double_bottom_right = "╝"
};

// ASCII fallback box characters
static const box_chars_t ASCII_BOX_CHARS = {
    .horizontal = "-",
    .vertical = "|",
    .top_left = "+",
    .top_right = "+",
    .bottom_left = "+",
    .bottom_right = "+",
    .cross = "+",
    .tee_down = "+",
    .tee_up = "+",
    .tee_right = "+",
    .tee_left = "+",
    .double_horizontal = "=",
    .double_vertical = "|",
    .double_top_left = "+",
    .double_top_right = "+",
    .double_bottom_left = "+",
    .double_bottom_right = "+"
};

// Graph Block Characters
graph_chars_t g_graph_chars;

static const graph_chars_t UNICODE_GRAPH_CHARS = {
    .block_empty = "_",
    .block_1_8 = "▁",
    .block_1_4 = "▂",
    .block_3_8 = "▃",
    .block_1_2 = "▄",
    .block_5_8 = "▅",
    .block_3_4 = "▆",
    .block_7_8 = "▇",
    .block_full = "█"
};

static const graph_chars_t ASCII_GRAPH_CHARS = {
    .block_empty = ".",
    .block_1_8 = ".",
    .block_1_4 = ":",
    .block_3_8 = ":",
    .block_1_2 = "i",
    .block_5_8 = "i",
    .block_3_4 = "I",
    .block_7_8 = "I",
    .block_full = "#"
};

// Internal helper functions
static void write_ansi_color(ansi_color_t fg, ansi_color_t bg);
static void detect_screen_size(void);
static bool test_unicode_support(void);
static void init_character_sets(void);

// === Core Console Functions ===

/**
 * Initialize the console system with ANSI detection
 */
int console_init(void) {
    // Detect ANSI support
    g_console.ansi_detected = detect_ansi_support();
    g_console.color_enabled = g_console.ansi_detected;
    
    // Detect screen size
    detect_screen_size();
    
    // Test Unicode support (for box drawing)
    g_console.unicode_supported = test_unicode_support();
    
    // Initialize character sets based on capabilities
    init_character_sets();
    
    // Set default palette
    g_palette = PALETTE_QUARTERDECK;
    
    // Reset console state
    console_reset();
    
    return SUCCESS;
}

/**
 * Cleanup console and restore normal colors
 */
void console_cleanup(void) {
    if (g_console.ansi_detected) {
        printf(ANSI_RESET);
        printf(ANSI_CURSOR_ON);  // Ensure cursor is visible
    }
    g_console.color_enabled = false;
}

/**
 * Detect ANSI.SYS or compatible driver using multiple methods
 */
int detect_ansi_support(void) {
    // Method 1: Check for ANSI.SYS driver via multiplex interrupt
    union REGS regs;
    regs.x.ax = 0x1A00;  // ANSI.SYS installation check
    int86(0x2F, &regs, &regs);
    
    if (regs.h.al == 0xFF) {
        return true;  // ANSI.SYS detected
    }
    
    // Method 2: Environment variable check
    char *ansi_env = getenv("ANSI");
    if (ansi_env) {
        if (strcmp(ansi_env, "ON") == 0 || strcmp(ansi_env, "1") == 0) {
            return true;
        }
    }
    
    // Method 3: Check for NANSI or other ANSI drivers
    char *term_env = getenv("TERM");
    if (term_env && strstr(term_env, "ansi")) {
        return true;
    }
    
    // Method 4: Simple capability probe (cursor position request)
    // This is risky as it may hang on some terminals, so we do it last
    // and with a timeout mechanism
    printf(ANSI_ESC "6n");  // Request cursor position
    
    // Check if we get a response within reasonable time
    // In a real implementation, we'd use a timer here
    // For now, assume no ANSI if we got this far
    
    return false;
}

/**
 * Reset console to default state
 */
void console_reset(void) {
    if (g_console.ansi_detected) {
        printf(ANSI_RESET);
        set_color(g_palette.normal_fg, g_palette.normal_bg);
    }
    g_console.current_fg = g_palette.normal_fg;
    g_console.current_bg = g_palette.normal_bg;
}

// === Color Management ===

/**
 * Set both foreground and background colors
 */
void set_color(ansi_color_t fg, ansi_color_t bg) {
    if (!g_console.color_enabled) {
        return;
    }
    
    g_console.current_fg = fg;
    g_console.current_bg = bg;
    write_ansi_color(fg, bg);
}

/**
 * Set only foreground color
 */
void set_foreground(ansi_color_t color) {
    set_color(color, g_console.current_bg);
}

/**
 * Set only background color
 */
void set_background(ansi_color_t color) {
    set_color(g_console.current_fg, color);
}

/**
 * Reset to normal colors
 */
void reset_colors(void) {
    set_color(g_palette.normal_fg, g_palette.normal_bg);
}

ansi_color_t get_foreground(void) {
    return g_console.current_fg;
}

ansi_color_t get_background(void) {
    return g_console.current_bg;
}

// === Cursor Control ===

/**
 * Move cursor to specific position (1-based coordinates)
 */
void goto_xy(uint8_t x, uint8_t y) {
    if (g_console.ansi_detected) {
        printf(ANSI_ESC "%d;%dH", y, x);
    } else {
        gotoxy(x, y);  // Use Turbo C library function
    }
    g_console.cursor_x = x;
    g_console.cursor_y = y;
}

/**
 * Get current cursor position
 */
void get_cursor_pos(uint8_t *x, uint8_t *y) {
    *x = g_console.cursor_x;
    *y = g_console.cursor_y;
}

uint8_t get_cursor_x(void) {
    return g_console.cursor_x;
}

uint8_t get_cursor_y(void) {
    return g_console.cursor_y;
}

void cursor_up(uint8_t lines) {
    if (g_console.ansi_detected) {
        printf(ANSI_ESC "%dA", lines);
    }
    if (g_console.cursor_y > lines) {
        g_console.cursor_y -= lines;
    } else {
        g_console.cursor_y = 1;
    }
}

void cursor_down(uint8_t lines) {
    if (g_console.ansi_detected) {
        printf(ANSI_ESC "%dB", lines);
    }
    if (g_console.cursor_y + lines <= g_console.screen_height) {
        g_console.cursor_y += lines;
    } else {
        g_console.cursor_y = g_console.screen_height;
    }
}

void cursor_left(uint8_t cols) {
    if (g_console.ansi_detected) {
        printf(ANSI_ESC "%dD", cols);
    }
    if (g_console.cursor_x > cols) {
        g_console.cursor_x -= cols;
    } else {
        g_console.cursor_x = 1;
    }
}

void cursor_right(uint8_t cols) {
    if (g_console.ansi_detected) {
        printf(ANSI_ESC "%dC", cols);
    }
    if (g_console.cursor_x + cols <= g_console.screen_width) {
        g_console.cursor_x += cols;
    } else {
        g_console.cursor_x = g_console.screen_width;
    }
}

// === Screen Control ===

/**
 * Clear entire screen and home cursor
 */
void clear_screen(void) {
    if (g_console.ansi_detected) {
        printf(ANSI_CLEAR_SCREEN ANSI_HOME);
    } else {
        clrscr();  // Use Turbo C library function
    }
    g_console.cursor_x = 1;
    g_console.cursor_y = 1;
}

/**
 * Clear current line
 */
void clear_line(void) {
    if (g_console.ansi_detected) {
        printf(ANSI_ESC "2K");
    } else {
        clreol();  // Use Turbo C library function
    }
}

/**
 * Clear from cursor to end of line
 */
void clear_to_end_of_line(void) {
    if (g_console.ansi_detected) {
        printf(ANSI_ESC "K");
    } else {
        clreol();
    }
}

void save_cursor(void) {
    if (g_console.ansi_detected) {
        printf(ANSI_SAVE_CURSOR);
    }
}

void restore_cursor(void) {
    if (g_console.ansi_detected) {
        printf(ANSI_RESTORE_CURSOR);
    }
}

// === Quarterdeck-Style Interface Functions ===

/**
 * Draw classic Quarterdeck-style header with title
 */
void draw_quarterdeck_header(const char *title, const char *version) {
    if (!g_console.color_enabled) {
        printf("%s v%s\n", title, version);
        printf("================================================================================\n");
        return;
    }
    
    // Clear screen and position cursor
    clear_screen();
    
    // Draw header with classic blue background
    set_color(g_palette.header_fg, g_palette.header_bg);
    
    // Top border with double lines
    printf("%s", g_box_chars.double_top_left);
    for (int i = 0; i < g_console.screen_width - 2; i++) {
        printf("%s", g_box_chars.double_horizontal);
    }
    printf("%s\n", g_box_chars.double_top_right);
    
    // Title line centered
    char full_title[256];
    sprintf(full_title, "%s v%s", title, version);
    const char *centered = center_text(full_title, g_console.screen_width - 4);
    printf("%s %s %s\n", g_box_chars.double_vertical, centered, g_box_chars.double_vertical);
    
    // Bottom border
    printf("%s", g_box_chars.double_bottom_left);
    for (int i = 0; i < g_console.screen_width - 2; i++) {
        printf("%s", g_box_chars.double_horizontal);
    }
    printf("%s\n", g_box_chars.double_bottom_right);
    
    // Reset to normal colors
    reset_colors();
}

/**
 * Draw help/command footer
 */
void draw_quarterdeck_footer(const char *help_text) {
    save_cursor();
    goto_xy(1, g_console.screen_height);
    
    set_color(g_palette.normal_bg, g_palette.header_bg);  // Reverse colors
    printf(" %-*s", g_console.screen_width - 1, help_text);
    
    reset_colors();
    restore_cursor();
}

/**
 * Draw a box with optional title
 */
void draw_box(uint8_t x, uint8_t y, uint8_t width, uint8_t height, 
              const char *title, bool double_border) {
    
    const char *h_char = double_border ? g_box_chars.double_horizontal : g_box_chars.horizontal;
    const char *v_char = double_border ? g_box_chars.double_vertical : g_box_chars.vertical;
    const char *tl_char = double_border ? g_box_chars.double_top_left : g_box_chars.top_left;
    const char *tr_char = double_border ? g_box_chars.double_top_right : g_box_chars.top_right;
    const char *bl_char = double_border ? g_box_chars.double_bottom_left : g_box_chars.bottom_left;
    const char *br_char = double_border ? g_box_chars.double_bottom_right : g_box_chars.bottom_right;
    
    set_color(g_palette.frame, g_palette.normal_bg);
    
    // Top line
    goto_xy(x, y);
    printf("%s", tl_char);
    
    if (title && strlen(title) > 0) {
        // Title in top border
        int title_len = strlen(title);
        int padding = (width - title_len - 4) / 2;  // "─ title ─"
        
        for (int i = 0; i < padding; i++) {
            printf("%s", h_char);
        }
        printf(" %s ", title);
        for (int i = padding + title_len + 2; i < width - 1; i++) {
            printf("%s", h_char);
        }
    } else {
        for (int i = 0; i < width - 2; i++) {
            printf("%s", h_char);
        }
    }
    printf("%s", tr_char);
    
    // Side lines
    for (int i = 1; i < height - 1; i++) {
        goto_xy(x, y + i);
        printf("%s", v_char);
        goto_xy(x + width - 1, y + i);
        printf("%s", v_char);
    }
    
    // Bottom line
    goto_xy(x, y + height - 1);
    printf("%s", bl_char);
    for (int i = 0; i < width - 2; i++) {
        printf("%s", h_char);
    }
    printf("%s", br_char);
    
    reset_colors();
}

// === Status Display Functions ===

/**
 * Display a colored status indicator like [OK], [ERROR], etc.
 */
void display_status_indicator(const char *status, ansi_color_t color) {
    set_color(color, g_palette.normal_bg);
    printf("[%s]", status);
    reset_colors();
}

/**
 * Display a progress bar
 */
void display_progress_bar(uint8_t percent, uint8_t width) {
    if (percent > 100) percent = 100;
    
    int filled = (percent * width) / 100;
    
    printf("[");
    set_color(g_palette.status_ok, g_palette.normal_bg);
    for (int i = 0; i < filled; i++) {
        printf("%s", g_graph_chars.block_full);
    }
    
    set_color(g_palette.frame, g_palette.normal_bg);
    for (int i = filled; i < width; i++) {
        printf("%s", g_graph_chars.block_empty);
    }
    
    reset_colors();
    printf("] %d%%", percent);
}

/**
 * Display network activity graph using block characters
 */
void display_network_activity_graph(uint32_t *history, int history_size, uint8_t width) {
    // Find maximum value for scaling
    uint32_t max_val = 1;
    for (int i = 0; i < history_size; i++) {
        if (history[i] > max_val) {
            max_val = history[i];
        }
    }
    
    // Draw graph
    for (int i = 0; i < width && i < history_size; i++) {
        int height = (history[i] * 8) / max_val;
        
        // Color coding based on activity level
        if (height == 0) {
            set_color(g_palette.frame, g_palette.normal_bg);
            printf("%s", g_graph_chars.block_empty);
        } else if (height <= 2) {
            set_color(g_palette.status_ok, g_palette.normal_bg);
            printf("%s", height == 1 ? g_graph_chars.block_1_8 : g_graph_chars.block_1_4);
        } else if (height <= 4) {
            set_color(g_palette.status_warn, g_palette.normal_bg);
            printf("%s", height == 3 ? g_graph_chars.block_3_8 : g_graph_chars.block_1_2);
        } else if (height <= 6) {
            set_color(g_palette.status_warn, g_palette.normal_bg);
            printf("%s", height == 5 ? g_graph_chars.block_5_8 : g_graph_chars.block_3_4);
        } else {
            set_color(g_palette.status_err, g_palette.normal_bg);
            printf("%s", height == 7 ? g_graph_chars.block_7_8 : g_graph_chars.block_full);
        }
    }
    
    reset_colors();
}

// === Text Formatting ===

/**
 * Center text within given width
 */
const char *center_text(const char *text, uint8_t width) {
    static char buffer[256];
    int text_len = strlen(text);
    
    if (text_len >= width) {
        strncpy(buffer, text, width);
        buffer[width] = '\0';
        return buffer;
    }
    
    int padding = (width - text_len) / 2;
    sprintf(buffer, "%*s%s%*s", padding, "", text, width - text_len - padding, "");
    return buffer;
}

/**
 * Print text with specific colors
 */
void print_colored(const char *text, ansi_color_t fg, ansi_color_t bg) {
    ansi_color_t old_fg = g_console.current_fg;
    ansi_color_t old_bg = g_console.current_bg;
    
    set_color(fg, bg);
    printf("%s", text);
    set_color(old_fg, old_bg);
}

/**
 * Print label: value with status color
 */
void print_status(const char *label, const char *value, ansi_color_t status_color) {
    set_color(g_palette.info, g_palette.normal_bg);
    printf("%s: ", label);
    set_color(status_color, g_palette.normal_bg);
    printf("%s", value);
    reset_colors();
}

// === Configuration ===

void console_enable_colors(bool enable) {
    g_console.color_enabled = enable && g_console.ansi_detected;
}

void console_set_palette(const quarterdeck_palette_t *palette) {
    if (palette) {
        g_palette = *palette;
    }
}

const quarterdeck_palette_t *console_get_palette(void) {
    return &g_palette;
}

bool console_colors_enabled(void) {
    return g_console.color_enabled;
}

bool console_ansi_detected(void) {
    return g_console.ansi_detected;
}

// === Utility Functions ===

void delay_ms(uint16_t milliseconds) {
    delay(milliseconds);  // Use Turbo C library function
}

const char *format_mac_address(const uint8_t *mac) {
    static char buffer[18];
    sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buffer;
}

const char *format_bytes(uint32_t bytes) {
    static char buffer[32];
    
    if (bytes < 1024) {
        sprintf(buffer, "%lu B", bytes);
    } else if (bytes < 1024 * 1024) {
        sprintf(buffer, "%.1f KB", bytes / 1024.0);
    } else {
        sprintf(buffer, "%.1f MB", bytes / (1024.0 * 1024.0));
    }
    
    return buffer;
}

const char *format_packets_per_sec(uint32_t pps) {
    static char buffer[16];
    
    if (pps < 1000) {
        sprintf(buffer, "%lu pkt/s", pps);
    } else {
        sprintf(buffer, "%.1fK pkt/s", pps / 1000.0);
    }
    
    return buffer;
}

// === Internal Helper Functions ===

/**
 * Write ANSI color escape sequence
 */
static void write_ansi_color(ansi_color_t fg, ansi_color_t bg) {
    if (!g_console.ansi_detected) {
        return;
    }
    
    // Handle bright colors (8-15) by using the appropriate ANSI sequence
    int ansi_fg = 30 + (fg & 0x07);
    int ansi_bg = 40 + (bg & 0x07);
    
    if (fg & 0x08) {  // Bright foreground
        printf(ANSI_ESC "1;%dm", ansi_fg);
    } else {
        printf(ANSI_ESC "0;%dm", ansi_fg);
    }
    
    if (bg & 0x08) {  // Bright background (if supported)
        printf(ANSI_ESC "%dm", ansi_bg + 60);
    } else {
        printf(ANSI_ESC "%dm", ansi_bg);
    }
}

/**
 * Detect current screen size
 */
static void detect_screen_size(void) {
    union REGS regs;
    
    // Try to get screen info via BIOS
    regs.h.ah = 0x0F;  // Get video mode
    int86(0x10, &regs, &regs);
    
    if (regs.h.ah > 0) {
        g_console.screen_width = regs.h.ah;
    } else {
        g_console.screen_width = 80;  // Default
    }
    
    // Try to get screen height
    char far *screen_rows = (char far *)0x00400084L;
    if (*screen_rows > 0) {
        g_console.screen_height = *screen_rows + 1;
    } else {
        g_console.screen_height = 25;  // Default
    }
}

/**
 * Test if Unicode box characters are supported
 */
static bool test_unicode_support(void) {
    // For DOS, Unicode is generally not supported
    // We'll use ASCII box characters as fallback
    return false;
}

/**
 * Initialize character sets based on detected capabilities
 */
static void init_character_sets(void) {
    if (g_console.unicode_supported) {
        g_box_chars = UNICODE_BOX_CHARS;
        g_graph_chars = UNICODE_GRAPH_CHARS;
    } else {
        g_box_chars = ASCII_BOX_CHARS;
        g_graph_chars = ASCII_GRAPH_CHARS;
    }
}