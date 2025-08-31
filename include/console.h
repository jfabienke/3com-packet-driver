/**
 * @file console.h
 * @brief ANSI Color Console Interface for Quarterdeck-Style Output
 *
 * Provides ANSI color support with automatic detection and graceful fallback
 * for DOS packet driver. Implements classic Quarterdeck-style interface
 * with professional box drawing and color-coded status indicators.
 *
 * Features:
 * - ANSI.SYS detection using multiple methods
 * - Quarterdeck-style color palette
 * - Box drawing with Unicode/ASCII fallback
 * - Real-time status displays
 * - Configuration-based color control
 *
 * This brings that authentic 90s system utility aesthetic while
 * maintaining compatibility with basic terminals.
 */

#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include <stdint.h>
#include <stdbool.h>

// ANSI Color Definitions (Standard 16-color palette)
typedef enum {
    COLOR_BLACK = 0,
    COLOR_BLUE = 1,
    COLOR_GREEN = 2, 
    COLOR_CYAN = 3,
    COLOR_RED = 4,
    COLOR_MAGENTA = 5,
    COLOR_BROWN = 6,
    COLOR_WHITE = 7,
    COLOR_GRAY = 8,
    COLOR_BRIGHT_BLUE = 9,
    COLOR_BRIGHT_GREEN = 10,
    COLOR_BRIGHT_CYAN = 11,
    COLOR_BRIGHT_RED = 12,
    COLOR_BRIGHT_MAGENTA = 13,
    COLOR_YELLOW = 14,
    COLOR_BRIGHT_WHITE = 15
} ansi_color_t;

// Console State and Capabilities
typedef struct {
    bool ansi_detected;        // ANSI.SYS or compatible detected
    bool color_enabled;        // Color output enabled
    bool unicode_supported;    // Unicode box drawing available
    uint8_t screen_width;      // Terminal width (typically 80)
    uint8_t screen_height;     // Terminal height (typically 25)
    ansi_color_t current_fg;   // Current foreground color
    ansi_color_t current_bg;   // Current background color
    uint8_t cursor_x;          // Current cursor X position
    uint8_t cursor_y;          // Current cursor Y position
} console_state_t;

// Quarterdeck-Style Color Palette
typedef struct {
    ansi_color_t header_fg;    // Header text color (bright white)
    ansi_color_t header_bg;    // Header background (blue)
    ansi_color_t status_ok;    // Success status (bright green)
    ansi_color_t status_warn;  // Warning status (yellow)  
    ansi_color_t status_err;   // Error status (bright red)
    ansi_color_t info;         // Information text (bright cyan)
    ansi_color_t data;         // Data values (white)
    ansi_color_t accent;       // Accent elements (bright magenta)
    ansi_color_t frame;        // Box frames (gray)
    ansi_color_t normal_fg;    // Normal text (white)
    ansi_color_t normal_bg;    // Normal background (black)
} quarterdeck_palette_t;

// Box Drawing Characters (Unicode and ASCII fallback)
typedef struct {
    const char *horizontal;         // ─ or -
    const char *vertical;           // │ or |
    const char *top_left;           // ┌ or +
    const char *top_right;          // ┐ or +
    const char *bottom_left;        // └ or +
    const char *bottom_right;       // ┘ or +
    const char *cross;              // ┼ or +
    const char *tee_down;           // ┬ or +
    const char *tee_up;             // ┴ or +
    const char *tee_right;          // ├ or +
    const char *tee_left;           // ┤ or +
    const char *double_horizontal;  // ═ or =
    const char *double_vertical;    // ║ or |
    const char *double_top_left;    // ╔ or +
    const char *double_top_right;   // ╗ or +
    const char *double_bottom_left; // ╚ or +
    const char *double_bottom_right;// ╝ or +
} box_chars_t;

// Traffic Graph Block Characters
typedef struct {
    const char *block_empty;    // _ or .
    const char *block_1_8;      // ▁ or .
    const char *block_1_4;      // ▂ or :
    const char *block_3_8;      // ▃ or :
    const char *block_1_2;      // ▄ or i
    const char *block_5_8;      // ▅ or i
    const char *block_3_4;      // ▆ or I
    const char *block_7_8;      // ▇ or I
    const char *block_full;     // █ or #
} graph_chars_t;

// Function Prototypes

// === Core Console Functions ===
int console_init(void);
void console_cleanup(void);
int detect_ansi_support(void);
void console_reset(void);

// === Color Management ===
void set_color(ansi_color_t fg, ansi_color_t bg);
void set_foreground(ansi_color_t color);
void set_background(ansi_color_t color);
void reset_colors(void);
ansi_color_t get_foreground(void);
ansi_color_t get_background(void);

// === Cursor Control ===
void goto_xy(uint8_t x, uint8_t y);
void get_cursor_pos(uint8_t *x, uint8_t *y);
uint8_t get_cursor_x(void);
uint8_t get_cursor_y(void);
void cursor_up(uint8_t lines);
void cursor_down(uint8_t lines);
void cursor_left(uint8_t cols);
void cursor_right(uint8_t cols);

// === Screen Control ===
void clear_screen(void);
void clear_line(void);
void clear_to_end_of_line(void);
void save_cursor(void);
void restore_cursor(void);

// === Quarterdeck-Style Interface Functions ===
void draw_quarterdeck_header(const char *title, const char *version);
void draw_quarterdeck_footer(const char *help_text);
void draw_box(uint8_t x, uint8_t y, uint8_t width, uint8_t height, 
              const char *title, bool double_border);
void draw_horizontal_line(uint8_t x, uint8_t y, uint8_t width, bool double_line);
void draw_vertical_line(uint8_t x, uint8_t y, uint8_t height, bool double_line);

// === Status Display Functions ===
void display_status_indicator(const char *status, ansi_color_t color);
void display_progress_bar(uint8_t percent, uint8_t width);
void display_network_activity_graph(uint32_t *history, int history_size, uint8_t width);

// === Text Formatting ===
const char *center_text(const char *text, uint8_t width);
void print_colored(const char *text, ansi_color_t fg, ansi_color_t bg);
void print_status(const char *label, const char *value, ansi_color_t status_color);

// === Configuration ===
void console_enable_colors(bool enable);
void console_set_palette(const quarterdeck_palette_t *palette);
const quarterdeck_palette_t *console_get_palette(void);
bool console_colors_enabled(void);
bool console_ansi_detected(void);

// === Utility Functions ===
void delay_ms(uint16_t milliseconds);
const char *format_mac_address(const uint8_t *mac);
const char *format_bytes(uint32_t bytes);
const char *format_packets_per_sec(uint32_t pps);

// === Global State Access ===
extern console_state_t g_console;
extern quarterdeck_palette_t g_palette;
extern box_chars_t g_box_chars;
extern graph_chars_t g_graph_chars;

// === Predefined Palettes ===
extern const quarterdeck_palette_t PALETTE_QUARTERDECK;
extern const quarterdeck_palette_t PALETTE_MONOCHROME;
extern const quarterdeck_palette_t PALETTE_GREEN_SCREEN;

// === ANSI Escape Sequences (Internal) ===
#define ANSI_ESC "\x1B["
#define ANSI_RESET ANSI_ESC "0m"
#define ANSI_CLEAR_SCREEN ANSI_ESC "2J"
#define ANSI_HOME ANSI_ESC "H"
#define ANSI_SAVE_CURSOR ANSI_ESC "s"
#define ANSI_RESTORE_CURSOR ANSI_ESC "u"
#define ANSI_CURSOR_OFF ANSI_ESC "?25l"
#define ANSI_CURSOR_ON ANSI_ESC "?25h"

// === Color Macros ===
#define MAKE_COLOR_CODE(fg, bg) ((bg << 4) | fg)
#define ANSI_FG(color) (30 + (color & 0x07) + ((color & 0x08) ? 60 : 0))
#define ANSI_BG(color) (40 + (color & 0x07) + ((color & 0x08) ? 60 : 0))

// === Convenience Macros ===
#define CONSOLE_WIDTH() (g_console.screen_width)
#define CONSOLE_HEIGHT() (g_console.screen_height)
#define COLORS_ENABLED() (g_console.color_enabled)
#define ANSI_DETECTED() (g_console.ansi_detected)

// Status indicator shortcuts
#define STATUS_OK()     display_status_indicator("OK", g_palette.status_ok)
#define STATUS_ERROR()  display_status_indicator("ERROR", g_palette.status_err)
#define STATUS_WARN()   display_status_indicator("WARNING", g_palette.status_warn)
#define STATUS_ACTIVE() display_status_indicator("ACTIVE", g_palette.status_ok)
#define STATUS_READY()  display_status_indicator("READY", g_palette.status_warn)

#endif /* _CONSOLE_H_ */