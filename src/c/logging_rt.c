/**
 * @file logging_rt.c
 * @brief Event logging - runtime functions (ROOT segment)
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file contains MINIMAL runtime logging functions that remain resident
 * in memory during packet operations. All printf/fprintf/fopen calls have
 * been REMOVED to avoid pulling in the C library (~40KB savings).
 *
 * Runtime logging only stores to ring buffer. Console/file output is
 * handled during init or by a separate diagnostic utility.
 *
 * Init-only code is in logging_init.c (OVERLAY segment).
 *
 * Refactored: 2026-01-28 13:45:00 CET - Removed C library dependencies
 * Split from logging.c: 2026-01-28 (ROOT/OVERLAY segmentation)
 */

#include <string.h>
#include <stdarg.h>
#include <dos.h>
#include "dos_io.h"
#include "logging.h"

/*
 * State variables - shared between logging_rt.c and logging_init.c
 * Defined here (ROOT segment), declared extern in logging_init.c
 */
int logging_enabled = 1;
int log_level = LOG_LEVEL_INFO;
char log_buffer[LOG_BUFFER_SIZE];

/* Ring buffer state */
char *ring_buffer = NULL;
int ring_buffer_size = 8192;
int ring_write_pos = 0;
int ring_read_pos = 0;
int ring_entries = 0;
int ring_wrapped = 0;
int ring_enabled = 0;

/* Category filtering */
int category_filter = 0xFF;

/* Performance counters */
unsigned long log_entries_written = 0;
unsigned long log_entries_dropped = 0;
unsigned long log_buffer_overruns = 0;

/* Log level names */
static const char *level_names[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERR"
};

/* ============================================================================
 * Minimal string formatting (no C library)
 * ============================================================================ */

/**
 * @brief Simple integer to string (no C library)
 */
static char* int_to_str(int val, char *buf, int base) {
    char *p = buf;
    char *start;
    int neg = 0;

    if (val < 0 && base == 10) {
        neg = 1;
        val = -val;
    }

    /* Build string in reverse */
    do {
        int digit = val % base;
        *p++ = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        val /= base;
    } while (val > 0);

    if (neg) *p++ = '-';
    *p = '\0';

    /* Reverse the string */
    start = buf;
    p--;
    while (start < p) {
        char tmp = *start;
        *start++ = *p;
        *p-- = tmp;
    }

    return buf;
}

/**
 * @brief Minimal format - only %s, %d, %x supported
 */
static int minimal_vsprintf(char *buf, int bufsize, const char *fmt, va_list args) {
    int pos = 0;
    const char *p = fmt;

    while (*p && pos < bufsize - 1) {
        if (*p == '%' && *(p+1)) {
            p++;
            switch (*p) {
                case 's': {
                    const char *s = va_arg(args, const char*);
                    if (!s) s = "(null)";
                    while (*s && pos < bufsize - 1) {
                        buf[pos++] = *s++;
                    }
                    break;
                }
                case 'd': {
                    char numbuf[16];
                    int_to_str(va_arg(args, int), numbuf, 10);
                    {
                        char *n = numbuf;
                        while (*n && pos < bufsize - 1) {
                            buf[pos++] = *n++;
                        }
                    }
                    break;
                }
                case 'x':
                case 'X': {
                    char numbuf[16];
                    unsigned int val = va_arg(args, unsigned int);
                    int_to_str((int)val, numbuf, 16);
                    {
                        char *n = numbuf;
                        while (*n && pos < bufsize - 1) {
                            buf[pos++] = *n++;
                        }
                    }
                    break;
                }
                case 'l': {
                    /* Skip 'l' modifier, handle next char */
                    p++;
                    if (*p == 'u' || *p == 'd') {
                        char numbuf[16];
                        long val = va_arg(args, long);
                        int_to_str((int)val, numbuf, 10);
                        {
                            char *n = numbuf;
                            while (*n && pos < bufsize - 1) {
                                buf[pos++] = *n++;
                            }
                        }
                    } else if (*p == 'x' || *p == 'X') {
                        char numbuf[16];
                        unsigned long val = va_arg(args, unsigned long);
                        int_to_str((int)val, numbuf, 16);
                        {
                            char *n = numbuf;
                            while (*n && pos < bufsize - 1) {
                                buf[pos++] = *n++;
                            }
                        }
                    }
                    break;
                }
                case '%':
                    buf[pos++] = '%';
                    break;
                default:
                    /* Unknown format, copy as-is */
                    buf[pos++] = '%';
                    if (pos < bufsize - 1) buf[pos++] = *p;
                    break;
            }
            p++;
        } else {
            buf[pos++] = *p++;
        }
    }
    buf[pos] = '\0';
    return pos;
}

/* ============================================================================
 * Minimal DOS output (no C library)
 * ============================================================================ */

/**
 * @brief Output string to DOS console using INT 21h (no C library)
 * Only used for critical errors - NOT for normal logging
 */
static void dos_puts(const char *s) {
    union REGS regs;

    while (*s) {
        regs.h.ah = 0x02;  /* DOS: Display character */
        regs.h.dl = (unsigned char)*s++;
        int86(0x21, &regs, &regs);
    }
}

/* ============================================================================
 * Runtime logging functions
 * ============================================================================ */

/**
 * @brief Store log entry in ring buffer
 * @param message Log message to store
 */
void log_to_ring_buffer(const char *message) {
    int msg_len;
    int total_len;

    if (!ring_buffer || !message || !ring_enabled) {
        return;
    }

    msg_len = strlen(message);
    if (msg_len == 0) {
        return;
    }

    /* Add newline and null terminator space */
    total_len = msg_len + 2;

    /* Check if we have enough space */
    if (total_len > ring_buffer_size) {
        log_buffer_overruns++;
        return; /* Message too large for buffer */
    }

    /* Handle ring buffer wraparound */
    if (ring_write_pos + total_len > ring_buffer_size) {
        ring_write_pos = 0;
        ring_wrapped = 1;
    }

    /* Copy message to ring buffer */
    memcpy(ring_buffer + ring_write_pos, message, msg_len);
    ring_buffer[ring_write_pos + msg_len] = '\n';
    ring_buffer[ring_write_pos + msg_len + 1] = '\0';

    /* Update write position */
    ring_write_pos = (ring_write_pos + total_len) % ring_buffer_size;
    ring_entries++;
}

/**
 * @brief Core logging function - ring buffer only
 * @param level Log level
 * @param format Format string
 * @param args Variable arguments
 */
static void log_internal(int level, const char *format, va_list args) {
    union REGS regs;
    int pos = 0;

    if (!logging_enabled || level < log_level) {
        log_entries_dropped++;
        return;
    }

    /* Get DOS time for timestamp */
    regs.h.ah = 0x2C;
    int86(0x21, &regs, &regs);

    /* Build timestamp: [HH:MM:SS] LEVEL: */
    log_buffer[pos++] = '[';
    if (regs.h.ch < 10) log_buffer[pos++] = '0';
    {
        char numbuf[8];
        int_to_str(regs.h.ch, numbuf, 10);
        {
            char *n = numbuf;
            while (*n && pos < LOG_BUFFER_SIZE - 1) log_buffer[pos++] = *n++;
        }
    }
    log_buffer[pos++] = ':';
    if (regs.h.cl < 10) log_buffer[pos++] = '0';
    {
        char numbuf[8];
        int_to_str(regs.h.cl, numbuf, 10);
        {
            char *n = numbuf;
            while (*n && pos < LOG_BUFFER_SIZE - 1) log_buffer[pos++] = *n++;
        }
    }
    log_buffer[pos++] = ':';
    if (regs.h.dh < 10) log_buffer[pos++] = '0';
    {
        char numbuf[8];
        int_to_str(regs.h.dh, numbuf, 10);
        {
            char *n = numbuf;
            while (*n && pos < LOG_BUFFER_SIZE - 1) log_buffer[pos++] = *n++;
        }
    }
    log_buffer[pos++] = ']';
    log_buffer[pos++] = ' ';

    /* Add level name */
    {
        const char *lvl = level_names[level];
        while (*lvl && pos < LOG_BUFFER_SIZE - 3) log_buffer[pos++] = *lvl++;
    }
    log_buffer[pos++] = ':';
    log_buffer[pos++] = ' ';
    log_buffer[pos] = '\0';

    /* Format the message */
    pos += minimal_vsprintf(log_buffer + pos, LOG_BUFFER_SIZE - pos, format, args);

    /* Store in ring buffer */
    if (ring_enabled) {
        log_to_ring_buffer(log_buffer);
    }

    log_entries_written++;
}

/**
 * @brief Log debug message
 */
void log_debug(const char *format, ...) {
    va_list args;

    if (!logging_enabled || LOG_LEVEL_DEBUG < log_level) {
        return;
    }

    va_start(args, format);
    log_internal(LOG_LEVEL_DEBUG, format, args);
    va_end(args);
}

/**
 * @brief Log info message
 */
void log_info(const char *format, ...) {
    va_list args;

    if (!logging_enabled || LOG_LEVEL_INFO < log_level) {
        return;
    }

    va_start(args, format);
    log_internal(LOG_LEVEL_INFO, format, args);
    va_end(args);
}

/**
 * @brief Log warning message
 */
void log_warning(const char *format, ...) {
    va_list args;

    if (!logging_enabled || LOG_LEVEL_WARNING < log_level) {
        return;
    }

    va_start(args, format);
    log_internal(LOG_LEVEL_WARNING, format, args);
    va_end(args);
}

/**
 * @brief Log error message
 */
void log_error(const char *format, ...) {
    va_list args;

    if (!logging_enabled || LOG_LEVEL_ERROR < log_level) {
        return;
    }

    va_start(args, format);
    log_internal(LOG_LEVEL_ERROR, format, args);
    va_end(args);
}

/**
 * @brief Log error message to DOS console (critical errors only)
 * This bypasses the ring buffer and outputs directly to console.
 * Use sparingly - only for fatal errors during startup.
 */
void log_critical(const char *format, ...) {
    va_list args;

    va_start(args, format);
    minimal_vsprintf(log_buffer, LOG_BUFFER_SIZE, format, args);
    va_end(args);

    dos_puts("ERR: ");
    dos_puts(log_buffer);
    dos_puts("\r\n");
}

/**
 * @brief Read entries from ring buffer
 */
int log_read_ring_buffer(char *buffer, int buffer_size) {
    int bytes_read;
    int entry_start;
    int entry_end;
    int entry_len;

    if (!ring_buffer || !buffer || buffer_size <= 0 || !ring_enabled) {
        return 0;
    }

    bytes_read = 0;

    while (ring_read_pos != ring_write_pos && bytes_read < buffer_size - 1) {
        entry_start = ring_read_pos;
        entry_end = entry_start;

        while (entry_end < ring_buffer_size &&
               ring_buffer[entry_end] != '\n' &&
               ring_buffer[entry_end] != '\0') {
            entry_end++;
        }

        entry_len = entry_end - entry_start;

        if (bytes_read + entry_len + 1 >= buffer_size) {
            break;
        }

        memcpy(buffer + bytes_read, ring_buffer + entry_start, entry_len);
        buffer[bytes_read + entry_len] = '\n';
        bytes_read += entry_len + 1;

        ring_read_pos = (entry_end + 2) % ring_buffer_size;

        if (ring_read_pos >= ring_buffer_size) {
            ring_read_pos = 0;
        }
    }

    buffer[bytes_read] = '\0';
    return bytes_read;
}

/**
 * @brief Get logging statistics
 */
void logging_get_stats(unsigned long *written, unsigned long *dropped, unsigned long *overruns) {
    if (written) *written = log_entries_written;
    if (dropped) *dropped = log_entries_dropped;
    if (overruns) *overruns = log_buffer_overruns;
}

/**
 * @brief Check if ring buffer is enabled
 */
int logging_ring_buffer_enabled(void) {
    return ring_enabled;
}

/**
 * @brief Check if logging is enabled
 */
int logging_is_enabled(void) {
    return logging_enabled;
}

/**
 * @brief Get current logging level
 */
int logging_get_level(void) {
    return log_level;
}

/**
 * @brief Get category name string
 */
const char* get_category_name(int category) {
    switch (category) {
        case LOG_CAT_HARDWARE:    return "HW";
        case LOG_CAT_NETWORK:     return "NET";
        case LOG_CAT_MEMORY:      return "MEM";
        case LOG_CAT_INTERRUPT:   return "IRQ";
        case LOG_CAT_PACKET:      return "PKT";
        case LOG_CAT_CONFIG:      return "CFG";
        case LOG_CAT_PERFORMANCE: return "PERF";
        case LOG_CAT_DRIVER:      return "DRV";
        default:                  return "?";
    }
}

/**
 * @brief Get current logging configuration
 */
void logging_get_config(int *level, int *categories, int *outputs) {
    if (level) {
        *level = log_level;
    }
    if (categories) {
        *categories = category_filter;
    }
    if (outputs) {
        /* Ring buffer is the only runtime output */
        *outputs = ring_enabled ? 0x01 : 0x00;
    }
}

/* ============================================================================
 * Legacy symbol stubs - referenced by other modules but not used at runtime.
 * These provide linker-visible definitions without pulling in C library code.
 * ============================================================================ */

/* Legacy output destination flags */
int log_to_console = 0;
int log_to_file = 0;
int log_to_network = 0;

/* Legacy file logging state */
char log_filename[1] = {0};
dos_file_t log_file = -1;

/* Legacy network logging state */
char network_log_host[1] = {0};
int network_log_port = 0;
int network_log_protocol = 0;

/* Legacy logging function stubs */
void log_at_level(int level, const char *format, ...) {
    (void)level;
    (void)format;
}

void log_warning_category(int cat, const char *fmt, ...) {
    (void)cat;
    (void)fmt;
}

void log_error_category(int cat, const char *fmt, ...) {
    (void)cat;
    (void)fmt;
}

void log_debug_category(int cat, const char *fmt, ...) {
    (void)cat;
    (void)fmt;
}

/* End of logging_rt.c */
