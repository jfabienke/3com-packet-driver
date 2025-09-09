/**
 * @file debug_logging.c
 * @brief Debug logging framework with configurable levels and output targets
 * 
 * 3Com Packet Driver - Diagnostics Agent - Week 1
 * Implements comprehensive debug logging with /LOG=ON support
 */

#include "../../include/diagnostics.h"
#include "../../include/common.h"
#include "../../include/logging.h"
#include "../../docs/agents/shared/error-codes.h"
#include "../loader/timer_services.h"
#include "../loader/tsr_file_io.h"
#include "../loader/network_logging.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Debug logging configuration */
typedef struct debug_logger {
    bool initialized;
    diag_level_t current_level;
    uint32_t category_mask;
    
    /* Output targets */
    bool console_output;
    bool file_output;
    bool network_output;
    bool buffer_output;
    
    /* File logging */
    char log_file_path[128];
    tsr_file_handle_t file_handle; /* TSR-safe file handle */
    bool file_open;
    uint32_t max_file_size;
    uint32_t current_file_size;
    
    /* Ring buffer logging for real-time monitoring */
    log_entry_t *ring_buffer;
    uint16_t ring_size;
    uint16_t ring_write_pos;
    uint16_t ring_read_pos;
    uint16_t ring_count;
    bool ring_wrapped;
    
    /* Performance counters */
    uint32_t log_entries_total;
    uint32_t log_entries_dropped;
    uint32_t log_buffer_overflows;
    uint32_t log_file_errors;
    
    /* Rate limiting */
    bool rate_limiting_enabled;
    uint32_t rate_limit_per_sec;
    uint32_t rate_limit_window_start;
    uint32_t rate_limit_count;
    
} debug_logger_t;

static debug_logger_t g_debug_logger = {0};

/* Log level string mappings */
static const char* log_level_strings[] = {
    "NONE",
    "ERROR",
    "WARN",
    "INFO",
    "DEBUG",
    "TRACE"
};

/* Log category string mappings */
static const char* log_category_strings[] = {
    "HW",      /* Hardware */
    "NET",     /* Network */
    "MEM",     /* Memory */
    "IRQ",     /* Interrupt */
    "PKT",     /* Packet */
    "CFG",     /* Configuration */
    "PERF",    /* Performance */
    "DRV",     /* Driver */
};

/* Internal helper functions */
static const char* get_log_level_string(diag_level_t level) {
    if (level >= sizeof(log_level_strings) / sizeof(log_level_strings[0])) {
        return "UNKNOWN";
    }
    return log_level_strings[level];
}

static const char* get_log_category_string(uint32_t category) {
    /* Find first set bit to determine primary category */
    for (int i = 0; i < 8; i++) {
        if (category & (1 << i)) {
            return log_category_strings[i];
        }
    }
    return "ALL";
}

static uint32_t get_current_timestamp_ms(void) {
    /* DOS timer-based timestamp implementation */
    return get_millisecond_timestamp();
}

static bool should_log_message(diag_level_t level, uint32_t category) {
    if (!g_debug_logger.initialized) {
        return false;
    }
    
    /* Check log level */
    if (level > g_debug_logger.current_level) {
        return false;
    }
    
    /* Check category filter */
    if (g_debug_logger.category_mask != DIAG_CAT_ALL && 
        !(category & g_debug_logger.category_mask)) {
        return false;
    }
    
    /* Check rate limiting */
    if (g_debug_logger.rate_limiting_enabled) {
        uint32_t current_time = get_current_timestamp_ms();
        uint32_t window_elapsed = current_time - g_debug_logger.rate_limit_window_start;
        
        if (window_elapsed >= 1000) { /* 1 second window */
            g_debug_logger.rate_limit_window_start = current_time;
            g_debug_logger.rate_limit_count = 0;
        }
        
        if (g_debug_logger.rate_limit_count >= g_debug_logger.rate_limit_per_sec) {
            g_debug_logger.log_entries_dropped++;
            return false;
        }
        
        g_debug_logger.rate_limit_count++;
    }
    
    return true;
}

/* Initialize debug logging framework */
int debug_logging_init(void) {
    if (g_debug_logger.initialized) {
        return SUCCESS;
    }
    
    /* Set default configuration */
    g_debug_logger.current_level = DIAG_LEVEL_INFO;
    g_debug_logger.category_mask = DIAG_CAT_ALL;
    
    /* Default output targets */
    g_debug_logger.console_output = true;
    g_debug_logger.file_output = false;
    g_debug_logger.network_output = false;
    g_debug_logger.buffer_output = true;
    
    /* Default file settings */
    strcpy(g_debug_logger.log_file_path, "PACKET.LOG");
    g_debug_logger.max_file_size = 1024 * 1024; /* 1MB */
    g_debug_logger.current_file_size = 0;
    g_debug_logger.file_open = false;
    
    /* Initialize TSR file I/O system */
    int tsr_result = tsr_file_io_init();
    if (tsr_result != TSRFILE_SUCCESS) {
        return ERROR_INITIALIZATION_FAILED;
    }
    
    /* Initialize network logging (disabled by default) */
    netlog_init(0, 0);
    
    /* Initialize ring buffer */
    g_debug_logger.ring_size = 512; /* Default ring buffer size */
    g_debug_logger.ring_buffer = (log_entry_t*)malloc(g_debug_logger.ring_size * sizeof(log_entry_t));
    if (!g_debug_logger.ring_buffer) {
        return ERROR_OUT_OF_MEMORY;
    }
    
    memset(g_debug_logger.ring_buffer, 0, g_debug_logger.ring_size * sizeof(log_entry_t));
    g_debug_logger.ring_write_pos = 0;
    g_debug_logger.ring_read_pos = 0;
    g_debug_logger.ring_count = 0;
    g_debug_logger.ring_wrapped = false;
    
    /* Initialize counters */
    g_debug_logger.log_entries_total = 0;
    g_debug_logger.log_entries_dropped = 0;
    g_debug_logger.log_buffer_overflows = 0;
    g_debug_logger.log_file_errors = 0;
    
    /* Rate limiting (disabled by default) */
    g_debug_logger.rate_limiting_enabled = false;
    g_debug_logger.rate_limit_per_sec = 100;
    g_debug_logger.rate_limit_window_start = get_current_timestamp_ms();
    g_debug_logger.rate_limit_count = 0;
    
    g_debug_logger.initialized = true;
    
    debug_log_info("Debug logging framework initialized");
    return SUCCESS;
}

/* Configure debug logging from /LOG=ON parameter */
int debug_logging_configure_from_param(const char *log_param) {
    if (!log_param) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Parse /LOG=ON parameter */
    if (strcmp(log_param, "ON") == 0) {
        g_debug_logger.current_level = DIAG_LEVEL_INFO;
        g_debug_logger.file_output = true;
    } else if (strcmp(log_param, "DEBUG") == 0) {
        g_debug_logger.current_level = DIAG_LEVEL_DEBUG;
        g_debug_logger.file_output = true;
    } else if (strcmp(log_param, "TRACE") == 0) {
        g_debug_logger.current_level = DIAG_LEVEL_TRACE;
        g_debug_logger.file_output = true;
    } else if (strcmp(log_param, "OFF") == 0) {
        g_debug_logger.current_level = DIAG_LEVEL_NONE;
        g_debug_logger.file_output = false;
    } else {
        return ERROR_INVALID_PARAM;
    }
    
    debug_log_info("Debug logging configured from parameter: %s", log_param);
    return SUCCESS;
}

/* Set debug log level */
int debug_logging_set_level(diag_level_t level) {
    if (!g_debug_logger.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    if (level > DIAG_LEVEL_TRACE) {
        return ERROR_INVALID_PARAM;
    }
    
    g_debug_logger.current_level = level;
    debug_log_info("Debug log level set to: %s", get_log_level_string(level));
    return SUCCESS;
}

/* Set category filter mask */
int debug_logging_set_category_filter(uint32_t category_mask) {
    if (!g_debug_logger.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    g_debug_logger.category_mask = category_mask;
    debug_log_debug("Debug log category filter set to: 0x%08lX", category_mask);
    return SUCCESS;
}

/* Enable/disable output targets */
int debug_logging_set_output_targets(bool console, bool file, bool network, bool buffer) {
    if (!g_debug_logger.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    g_debug_logger.console_output = console;
    g_debug_logger.file_output = file;
    g_debug_logger.network_output = network;
    g_debug_logger.buffer_output = buffer;
    
    debug_log_info("Debug output targets: console=%d, file=%d, network=%d, buffer=%d",
                   console, file, network, buffer);
    return SUCCESS;
}

/* Set log file path */
int debug_logging_set_file_path(const char *file_path) {
    if (!g_debug_logger.initialized || !file_path) {
        return ERROR_INVALID_PARAM;
    }
    
    if (strlen(file_path) >= sizeof(g_debug_logger.log_file_path)) {
        return ERROR_BUFFER_TOO_SMALL;
    }
    
    strcpy(g_debug_logger.log_file_path, file_path);
    debug_log_info("Debug log file path set to: %s", file_path);
    return SUCCESS;
}

/* Enable/disable rate limiting */
int debug_logging_set_rate_limiting(bool enabled, uint32_t messages_per_sec) {
    if (!g_debug_logger.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    g_debug_logger.rate_limiting_enabled = enabled;
    g_debug_logger.rate_limit_per_sec = messages_per_sec;
    
    debug_log_info("Rate limiting %s: %lu messages/sec", 
                   enabled ? "enabled" : "disabled", messages_per_sec);
    return SUCCESS;
}

/* Write log entry to ring buffer */
static int write_to_ring_buffer(const log_entry_t *entry) {
    if (!g_debug_logger.buffer_output || !entry) {
        return SUCCESS;
    }
    
    /* Check if buffer is full */
    if (g_debug_logger.ring_count >= g_debug_logger.ring_size) {
        g_debug_logger.log_buffer_overflows++;
        if (!g_debug_logger.ring_wrapped) {
            g_debug_logger.ring_wrapped = true;
            g_debug_logger.ring_read_pos = (g_debug_logger.ring_write_pos + 1) % g_debug_logger.ring_size;
        } else {
            g_debug_logger.ring_read_pos = (g_debug_logger.ring_read_pos + 1) % g_debug_logger.ring_size;
        }
    } else {
        g_debug_logger.ring_count++;
    }
    
    /* Copy entry to ring buffer */
    memcpy(&g_debug_logger.ring_buffer[g_debug_logger.ring_write_pos], entry, sizeof(log_entry_t));
    g_debug_logger.ring_write_pos = (g_debug_logger.ring_write_pos + 1) % g_debug_logger.ring_size;
    
    return SUCCESS;
}

/* Write log entry to console */
static int write_to_console(const log_entry_t *entry) {
    if (!g_debug_logger.console_output || !entry) {
        return SUCCESS;
    }
    
    printf("[%s] %s: %s\n", 
           get_log_level_string(entry->level),
           get_log_category_string(entry->category),
           entry->message);
    
    return SUCCESS;
}

/* Write log entry to file */
static int write_to_file(const log_entry_t *entry) {
    if (!g_debug_logger.file_output || !entry) {
        return SUCCESS;
    }
    
    /* Actual file I/O implementation using TSR-safe system */
    char formatted_entry[512];
    int written = snprintf(formatted_entry, sizeof(formatted_entry),
                          "[%lu] [%s] %s:%s:%lu %s\n",
                          entry->timestamp,
                          get_log_level_string(entry->level),
                          entry->file ? entry->file : "unknown",
                          entry->function ? entry->function : "unknown",
                          entry->line,
                          entry->message);
    
    if (written > 0) {
        /* Ensure file handle is opened */
        if (!g_debug_logger.file_open) {
            int result = tsr_file_open(&g_debug_logger.file_handle, 
                                      g_debug_logger.log_file_path,
                                      g_debug_logger.max_file_size);
            if (result == TSRFILE_SUCCESS) {
                g_debug_logger.file_open = true;
            }
        }
        
        if (g_debug_logger.file_open) {
            /* Write to file using TSR-safe I/O */
            int result = tsr_file_write(&g_debug_logger.file_handle, 
                                       formatted_entry, written);
            if (result == TSRFILE_SUCCESS) {
                g_debug_logger.current_file_size = g_debug_logger.file_handle.current_size;
            }
        }
    }
    
    return SUCCESS;
}

/* Core logging function */
int debug_log_message(diag_level_t level, uint32_t category, const char *function,
                      const char *file, uint32_t line, const char *format, ...) {
    if (!should_log_message(level, category)) {
        return SUCCESS;
    }
    
    /* Format message */
    log_entry_t entry;
    memset(&entry, 0, sizeof(log_entry_t));
    
    va_list args;
    va_start(args, format);
    vsnprintf(entry.message, sizeof(entry.message), format, args);
    va_end(args);
    
    /* Fill entry details */
    entry.timestamp = get_current_timestamp_ms();
    entry.level = level;
    entry.category = category;
    entry.function = function;
    entry.file = file;
    entry.line = line;
    entry.next = NULL;
    
    /* Write to all enabled output targets */
    write_to_ring_buffer(&entry);
    write_to_console(&entry);
    write_to_file(&entry);
    
    /* Network logging implementation */
    if (g_debug_logger.network_output) {
        netlog_send_message((uint8_t)entry->level, (uint8_t)entry->category, entry->message);
    }
    
    g_debug_logger.log_entries_total++;
    return SUCCESS;
}

/* Convenience logging functions */
int debug_log_error(const char *format, ...) {
    if (!should_log_message(DIAG_LEVEL_ERROR, DIAG_CAT_DRIVER)) {
        return SUCCESS;
    }
    
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    return debug_log_message(DIAG_LEVEL_ERROR, DIAG_CAT_DRIVER, 
                            __FUNCTION__, __FILE__, __LINE__, "%s", buffer);
}

int debug_log_warning(const char *format, ...) {
    if (!should_log_message(DIAG_LEVEL_WARNING, DIAG_CAT_DRIVER)) {
        return SUCCESS;
    }
    
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    return debug_log_message(DIAG_LEVEL_WARNING, DIAG_CAT_DRIVER, 
                            __FUNCTION__, __FILE__, __LINE__, "%s", buffer);
}

int debug_log_info(const char *format, ...) {
    if (!should_log_message(DIAG_LEVEL_INFO, DIAG_CAT_DRIVER)) {
        return SUCCESS;
    }
    
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    return debug_log_message(DIAG_LEVEL_INFO, DIAG_CAT_DRIVER, 
                            __FUNCTION__, __FILE__, __LINE__, "%s", buffer);
}

int debug_log_debug(const char *format, ...) {
    if (!should_log_message(DIAG_LEVEL_DEBUG, DIAG_CAT_DRIVER)) {
        return SUCCESS;
    }
    
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    return debug_log_message(DIAG_LEVEL_DEBUG, DIAG_CAT_DRIVER, 
                            __FUNCTION__, __FILE__, __LINE__, "%s", buffer);
}

int debug_log_trace(const char *format, ...) {
    if (!should_log_message(DIAG_LEVEL_TRACE, DIAG_CAT_DRIVER)) {
        return SUCCESS;
    }
    
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    return debug_log_message(DIAG_LEVEL_TRACE, DIAG_CAT_DRIVER, 
                            __FUNCTION__, __FILE__, __LINE__, "%s", buffer);
}

/* Read entries from ring buffer */
int debug_logging_read_buffer(log_entry_t *entries, uint16_t max_entries, uint16_t *count_read) {
    if (!g_debug_logger.initialized || !entries || !count_read) {
        return ERROR_INVALID_PARAM;
    }
    
    *count_read = 0;
    
    while (*count_read < max_entries && g_debug_logger.ring_count > 0) {
        memcpy(&entries[*count_read], &g_debug_logger.ring_buffer[g_debug_logger.ring_read_pos], 
               sizeof(log_entry_t));
        
        g_debug_logger.ring_read_pos = (g_debug_logger.ring_read_pos + 1) % g_debug_logger.ring_size;
        g_debug_logger.ring_count--;
        (*count_read)++;
    }
    
    return SUCCESS;
}

/* Get logging statistics */
int debug_logging_get_statistics(uint32_t *total_entries, uint32_t *dropped_entries, 
                                 uint32_t *buffer_overflows, uint32_t *file_errors) {
    if (!g_debug_logger.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    if (total_entries) *total_entries = g_debug_logger.log_entries_total;
    if (dropped_entries) *dropped_entries = g_debug_logger.log_entries_dropped;
    if (buffer_overflows) *buffer_overflows = g_debug_logger.log_buffer_overflows;
    if (file_errors) *file_errors = g_debug_logger.log_file_errors;
    
    return SUCCESS;
}

/* Print logging dashboard */
int debug_logging_print_dashboard(void) {
    if (!g_debug_logger.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    printf("\n=== DEBUG LOGGING DASHBOARD ===\n");
    printf("Status: %s\n", g_debug_logger.initialized ? "Active" : "Inactive");
    printf("Level: %s\n", get_log_level_string(g_debug_logger.current_level));
    printf("Category Filter: 0x%08lX\n", g_debug_logger.category_mask);
    printf("Output Targets: Console=%s, File=%s, Network=%s, Buffer=%s\n",
           g_debug_logger.console_output ? "ON" : "OFF",
           g_debug_logger.file_output ? "ON" : "OFF",
           g_debug_logger.network_output ? "ON" : "OFF",
           g_debug_logger.buffer_output ? "ON" : "OFF");
    
    printf("\nStatistics:\n");
    printf("  Total Entries: %lu\n", g_debug_logger.log_entries_total);
    printf("  Dropped Entries: %lu\n", g_debug_logger.log_entries_dropped);
    printf("  Buffer Overflows: %lu\n", g_debug_logger.log_buffer_overflows);
    printf("  File Errors: %lu\n", g_debug_logger.log_file_errors);
    
    printf("\nRing Buffer:\n");
    printf("  Size: %d\n", g_debug_logger.ring_size);
    printf("  Count: %d\n", g_debug_logger.ring_count);
    printf("  Wrapped: %s\n", g_debug_logger.ring_wrapped ? "YES" : "NO");
    
    if (g_debug_logger.file_output) {
        printf("\nFile Logging:\n");
        printf("  Path: %s\n", g_debug_logger.log_file_path);
        printf("  Current Size: %lu bytes\n", g_debug_logger.current_file_size);
        printf("  Max Size: %lu bytes\n", g_debug_logger.max_file_size);
    }
    
    if (g_debug_logger.rate_limiting_enabled) {
        printf("\nRate Limiting:\n");
        printf("  Limit: %lu messages/sec\n", g_debug_logger.rate_limit_per_sec);
        printf("  Current Count: %lu\n", g_debug_logger.rate_limit_count);
    }
    
    return SUCCESS;
}

/* Week 1 specific: NE2000 emulation debug logging */
int debug_logging_ne2000_emulation(diag_level_t level, const char *operation, 
                                   uint16_t reg, uint16_t value, const char *description) {
    if (!g_debug_logger.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    return debug_log_message(level, DIAG_CAT_HARDWARE, __FUNCTION__, __FILE__, __LINE__,
                            "NE2000: %s reg=0x%04X val=0x%04X - %s", 
                            operation, reg, value, description);
}

/* Configure network logging */
int debug_logging_configure_network(const char *network_config) {
    if (!g_debug_logger.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    if (!network_config) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Configure network logging */
    int result = netlog_configure(network_config);
    if (result == NETLOG_SUCCESS) {
        g_debug_logger.network_output = netlog_is_available();
        debug_log_info("Network logging configured: %s", network_config);
        return SUCCESS;
    }
    
    return ERROR_INVALID_PARAM;
}

/* Check if debug logging system is ready */
int debug_logging_system_ready(void)
{
    return g_debug_logger.initialized;
}

/* Cleanup debug logging framework */
void debug_logging_cleanup(void) {
    if (!g_debug_logger.initialized) {
        return;
    }
    
    debug_log_info("Shutting down debug logging framework");
    
    /* Free ring buffer */
    if (g_debug_logger.ring_buffer) {
        free(g_debug_logger.ring_buffer);
        g_debug_logger.ring_buffer = NULL;
    }
    
    /* Close log file if open */
    if (g_debug_logger.file_open) {
        tsr_file_close(&g_debug_logger.file_handle);
        g_debug_logger.file_open = false;
    }
    
    /* Cleanup network logging */
    if (g_debug_logger.network_output) {
        netlog_cleanup();
    }
    
    /* Cleanup TSR file I/O system */
    tsr_file_io_cleanup();
    
    memset(&g_debug_logger, 0, sizeof(debug_logger_t));
}