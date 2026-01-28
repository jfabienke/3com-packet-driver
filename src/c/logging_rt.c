/**
 * @file logging_rt.c
 * @brief Event logging - runtime functions (ROOT segment)
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file contains runtime logging functions that remain resident
 * in memory during packet operations. Init-only code is in logging_init.c.
 *
 * This file is part of the 3Com Packet Driver project.
 *
 * Split from logging.c: 2026-01-28 (ROOT/OVERLAY segmentation)
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <dos.h>
#include "logging.h"

/* Watcom C compatibility - provide snprintf/vsnprintf wrappers */
#ifdef __WATCOMC__
#define snprintf watcom_snprintf
#define vsnprintf watcom_vsnprintf

static int watcom_snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    int ret;
    va_start(args, fmt);
    ret = vsprintf(buf, fmt, args);
    va_end(args);
    if (size > 0) {
        buf[size - 1] = '\0';
    }
    return ret;
}

static int watcom_vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    int ret;
    ret = vsprintf(buf, fmt, args);
    if (size > 0) {
        buf[size - 1] = '\0';
    }
    return ret;
}
#endif

/*
 * State variables - shared between logging_rt.c and logging_init.c
 * Defined here (ROOT segment), declared extern in logging_init.c
 */
int logging_enabled = 1;
int log_level = LOG_LEVEL_INFO;
FILE *log_file = NULL;
char log_buffer[LOG_BUFFER_SIZE];
int log_to_console = 1;
int log_to_file = 0;
int log_to_network = 0;
char log_filename[128];

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
    "WARNING",
    "ERROR"
};

/* Network logging configuration */
char network_log_host[64];
int network_log_port = 514;
int network_log_protocol = 0;

/**
 * @brief Get current time as string
 * @param buffer Buffer to store time string
 * @param size Buffer size
 */
static void get_time_string(char *buffer, size_t size) {
    union REGS regs;

    /* Get DOS time */
    regs.h.ah = 0x2C;
    int86(0x21, &regs, &regs);

    snprintf(buffer, size, "%02d:%02d:%02d",
             regs.h.ch, regs.h.cl, regs.h.dh);
}

/**
 * @brief Enhanced log message function with ring buffer and filtering
 * @param level Log level
 * @param format Printf-style format string
 * @param ... Arguments
 */
void log_at_level(int level, const char *format, ...) {
    va_list args;
    char time_str[16];
    int len;

    if (!logging_enabled || level < log_level) {
        log_entries_dropped++;
        return;
    }

    if (level < LOG_LEVEL_DEBUG || level > LOG_LEVEL_ERROR) {
        log_entries_dropped++;
        return;
    }

    /* Get current time */
    get_time_string(time_str, sizeof(time_str));

    /* Format the message */
    va_start(args, format);

    /* Build log message with timestamp and level */
    len = snprintf(log_buffer, sizeof(log_buffer),
                   "[%s] %s: ", time_str, level_names[level]);

    if (len > 0 && (size_t)len < sizeof(log_buffer)) {
        vsnprintf(log_buffer + len, sizeof(log_buffer) - (size_t)len, format, args);
    }

    va_end(args);

    /* Store in ring buffer first (most efficient) */
    if (ring_enabled) {
        log_to_ring_buffer(log_buffer);
    }

    /* Output to console */
    if (log_to_console) {
        printf("%s\r\n", log_buffer);
    }

    /* Output to file */
    if (log_to_file && log_file) {
        fprintf(log_file, "%s\n", log_buffer);
        fflush(log_file);
    }

    /* Output to network (placeholder) */
    if (log_to_network) {
        log_to_network_target(log_buffer);
    }

    log_entries_written++;

    /* Check if file rotation is needed (every 100 entries) */
    if (log_to_file && (log_entries_written % 100) == 0) {
        logging_rotate_file();
    }
}

/**
 * @brief Log debug message
 * @param format Printf-style format string
 * @param ... Arguments
 */
void log_debug(const char *format, ...) {
    va_list args;

    if (!logging_enabled || LOG_LEVEL_DEBUG < log_level) {
        return;
    }

    va_start(args, format);
    vsnprintf(log_buffer, sizeof(log_buffer), format, args);
    va_end(args);

    log_at_level(LOG_LEVEL_DEBUG, "%s", log_buffer);
}

/**
 * @brief Log info message
 * @param format Printf-style format string
 * @param ... Arguments
 */
void log_info(const char *format, ...) {
    va_list args;

    if (!logging_enabled || LOG_LEVEL_INFO < log_level) {
        return;
    }

    va_start(args, format);
    vsnprintf(log_buffer, sizeof(log_buffer), format, args);
    va_end(args);

    log_at_level(LOG_LEVEL_INFO, "%s", log_buffer);
}

/**
 * @brief Log warning message
 * @param format Printf-style format string
 * @param ... Arguments
 */
void log_warning(const char *format, ...) {
    va_list args;

    if (!logging_enabled || LOG_LEVEL_WARNING < log_level) {
        return;
    }

    va_start(args, format);
    vsnprintf(log_buffer, sizeof(log_buffer), format, args);
    va_end(args);

    log_at_level(LOG_LEVEL_WARNING, "%s", log_buffer);
}

/**
 * @brief Log error message
 * @param format Printf-style format string
 * @param ... Arguments
 */
void log_error(const char *format, ...) {
    va_list args;

    if (!logging_enabled || LOG_LEVEL_ERROR < log_level) {
        return;
    }

    va_start(args, format);
    vsnprintf(log_buffer, sizeof(log_buffer), format, args);
    va_end(args);

    log_at_level(LOG_LEVEL_ERROR, "%s", log_buffer);
}

/**
 * @brief Store log entry in ring buffer
 * @param message Log message to store
 */
void log_to_ring_buffer(const char *message) {
    int msg_len;
    int total_len;

    if (!ring_buffer || !message) {
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
        /* Wrap around to beginning */
        ring_write_pos = 0;
        ring_wrapped = 1;

        /* Adjust read position if we're overwriting unread data */
        if (ring_wrapped && ring_read_pos < total_len) {
            ring_read_pos = total_len;
        }
    }

    /* Check if we're overwriting unread data */
    if (ring_wrapped &&
        ((ring_read_pos > ring_write_pos && ring_read_pos < ring_write_pos + total_len) ||
         (ring_read_pos <= ring_write_pos && ring_read_pos < ring_write_pos + total_len))) {
        /* Move read position to avoid reading corrupted data */
        ring_read_pos = (ring_write_pos + total_len) % ring_buffer_size;
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
 * @brief Read entries from ring buffer
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of bytes read
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
        /* Find end of current log entry */
        entry_start = ring_read_pos;
        entry_end = entry_start;

        /* Find the newline or null terminator */
        while (entry_end < ring_buffer_size &&
               ring_buffer[entry_end] != '\n' &&
               ring_buffer[entry_end] != '\0') {
            entry_end++;
        }

        entry_len = entry_end - entry_start;

        /* Check if we have space in output buffer */
        if (bytes_read + entry_len + 1 >= buffer_size) {
            break; /* Not enough space */
        }

        /* Copy entry to output buffer */
        memcpy(buffer + bytes_read, ring_buffer + entry_start, entry_len);
        buffer[bytes_read + entry_len] = '\n';
        bytes_read += entry_len + 1;

        /* Move read position */
        ring_read_pos = (entry_end + 2) % ring_buffer_size; /* Skip \n and \0 */

        /* Handle wraparound */
        if (ring_read_pos >= ring_buffer_size) {
            ring_read_pos = 0;
        }
    }

    buffer[bytes_read] = '\0';
    return bytes_read;
}

/**
 * @brief Send log message to network target
 * @param message Log message
 */
void log_to_network_target(const char *message) {
    /* C89: All declarations must come before statements */
    static FILE *network_log_file = NULL;

    if (!message || !log_to_network) {
        return;
    }

    /* File-based network logging for debugging */
    if (!network_log_file) {
        network_log_file = fopen("NETLOG.TXT", "a");
    }

    if (network_log_file) {
        fprintf(network_log_file, "NET[%s:%d]: %s\n", network_log_host, network_log_port, message);
        fflush(network_log_file);
    }
}

/**
 * @brief Get logging statistics
 * @param written Pointer to store entries written count
 * @param dropped Pointer to store entries dropped count
 * @param overruns Pointer to store buffer overruns count
 */
void logging_get_stats(unsigned long *written, unsigned long *dropped, unsigned long *overruns) {
    if (written) *written = log_entries_written;
    if (dropped) *dropped = log_entries_dropped;
    if (overruns) *overruns = log_buffer_overruns;
}

/**
 * @brief Check if ring buffer is enabled
 * @return 1 if enabled, 0 if disabled
 */
int logging_ring_buffer_enabled(void) {
    return ring_enabled;
}

/**
 * @brief Check if logging is enabled
 * @return 1 if enabled, 0 if disabled
 */
int logging_is_enabled(void) {
    return logging_enabled;
}

/**
 * @brief Get current logging level
 * @return Current log level
 */
int logging_get_level(void) {
    return log_level;
}

/**
 * @brief Get category name string
 * @param category Category bitmask
 * @return Category name string
 */
const char* get_category_name(int category) {
    switch (category) {
        case LOG_CAT_HARDWARE:    return "HARDWARE";
        case LOG_CAT_NETWORK:     return "NETWORK";
        case LOG_CAT_MEMORY:      return "MEMORY";
        case LOG_CAT_INTERRUPT:   return "INTERRUPT";
        case LOG_CAT_PACKET:      return "PACKET";
        case LOG_CAT_CONFIG:      return "CONFIG";
        case LOG_CAT_PERFORMANCE: return "PERF";
        case LOG_CAT_DRIVER:      return "DRIVER";
        default:                  return "UNKNOWN";
    }
}

/**
 * @brief Log message with level and category
 * @param level Log level
 * @param category Log category
 * @param format Printf-style format string
 * @param ... Arguments
 */
void log_at_level_with_category(int level, int category, const char *format, ...) {
    va_list args;

    if (!logging_enabled || level < log_level) {
        log_entries_dropped++;
        return;
    }

    /* Check category filter */
    if (!(category_filter & category)) {
        log_entries_dropped++;
        return;
    }

    va_start(args, format);
    vsnprintf(log_buffer, sizeof(log_buffer), format, args);
    va_end(args);

    log_at_level(level, "[%s] %s", get_category_name(category), log_buffer);
}

/**
 * @brief Log debug message with category
 * @param category Log category
 * @param format Printf-style format string
 * @param ... Arguments
 */
void log_debug_category(int category, const char *format, ...) {
    va_list args;

    if (!logging_enabled || LOG_LEVEL_DEBUG < log_level) {
        return;
    }

    if (!(category_filter & category)) {
        return;
    }

    va_start(args, format);
    vsnprintf(log_buffer, sizeof(log_buffer), format, args);
    va_end(args);

    log_at_level(LOG_LEVEL_DEBUG, "[%s] %s", get_category_name(category), log_buffer);
}

/**
 * @brief Log info message with category
 * @param category Log category
 * @param format Printf-style format string
 * @param ... Arguments
 */
void log_info_category(int category, const char *format, ...) {
    va_list args;

    if (!logging_enabled || LOG_LEVEL_INFO < log_level) {
        return;
    }

    if (!(category_filter & category)) {
        return;
    }

    va_start(args, format);
    vsnprintf(log_buffer, sizeof(log_buffer), format, args);
    va_end(args);

    log_at_level(LOG_LEVEL_INFO, "[%s] %s", get_category_name(category), log_buffer);
}

/**
 * @brief Log warning message with category
 * @param category Log category
 * @param format Printf-style format string
 * @param ... Arguments
 */
void log_warning_category(int category, const char *format, ...) {
    va_list args;

    if (!logging_enabled || LOG_LEVEL_WARNING < log_level) {
        return;
    }

    if (!(category_filter & category)) {
        return;
    }

    va_start(args, format);
    vsnprintf(log_buffer, sizeof(log_buffer), format, args);
    va_end(args);

    log_at_level(LOG_LEVEL_WARNING, "[%s] %s", get_category_name(category), log_buffer);
}

/**
 * @brief Log error message with category
 * @param category Log category
 * @param format Printf-style format string
 * @param ... Arguments
 */
void log_error_category(int category, const char *format, ...) {
    va_list args;

    if (!logging_enabled || LOG_LEVEL_ERROR < log_level) {
        return;
    }

    if (!(category_filter & category)) {
        return;
    }

    va_start(args, format);
    vsnprintf(log_buffer, sizeof(log_buffer), format, args);
    va_end(args);

    log_at_level(LOG_LEVEL_ERROR, "[%s] %s", get_category_name(category), log_buffer);
}

/**
 * @brief Get current logging configuration
 * @param level Pointer to store current level
 * @param categories Pointer to store category filter
 * @param outputs Pointer to store output flags
 */
void logging_get_config(int *level, int *categories, int *outputs) {
    int output_flags;

    if (level) {
        *level = log_level;
    }
    if (categories) {
        *categories = category_filter;
    }
    if (outputs) {
        output_flags = 0;
        if (log_to_console) output_flags |= 0x01;
        if (log_to_file) output_flags |= 0x02;
        if (log_to_network) output_flags |= 0x04;
        *outputs = output_flags;
    }
}
