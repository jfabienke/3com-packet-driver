/**
 * @file logging.c
 * @brief Event logging functionality
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 * Fixed for Watcom C compiler (C89 compliance)
 */

#include "dos_io.h"
#include <string.h>
#include <stdarg.h>
#include <dos.h>
#include <stdlib.h>
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

/* Enhanced logging configuration for Phase 3C */
static int logging_enabled = 1;
static int log_level = LOG_LEVEL_INFO;
static dos_file_t log_file = -1;
static char log_buffer[LOG_BUFFER_SIZE];
static int log_to_console = 1;
static int log_to_file = 0;
static int log_to_network = 0;  /* New: network logging */
static char log_filename[128];  /* Initialized at runtime to reduce _DATA segment */

/* Ring buffer for efficient DOS memory usage */
static char *ring_buffer = NULL;
static int ring_buffer_size = 8192;  /* 8KB ring buffer */
static int ring_write_pos = 0;
static int ring_read_pos = 0;
static int ring_entries = 0;
static int ring_wrapped = 0;
static int ring_enabled = 0;

/* Category filtering */
static int category_filter = 0xFF;  /* All categories enabled by default */

/* Performance counters */
static unsigned long log_entries_written = 0;
static unsigned long log_entries_dropped = 0;
static unsigned long log_buffer_overruns = 0;

/* Log level names */
static const char *level_names[] = {
    "DEBUG",
    "INFO",
    "WARNING",
    "ERROR"
};

/**
 * @brief Initialize enhanced logging subsystem with ring buffer
 * @return 0 on success, negative on error
 */
int logging_init(void) {
    logging_enabled = 1;
    log_level = LOG_LEVEL_INFO;
    log_to_console = 1;
    log_to_file = 0;
    log_to_network = 0;

    /* Initialize default filename (moved from static init to reduce _DATA) */
    strcpy(log_filename, "3COMPD.LOG");

    /* Clear log buffer */
    memset(log_buffer, 0, sizeof(log_buffer));
    
    /* Initialize ring buffer for efficient memory usage */
    ring_buffer = malloc(ring_buffer_size);
    if (ring_buffer) {
        memset(ring_buffer, 0, ring_buffer_size);
        ring_write_pos = 0;
        ring_read_pos = 0;
        ring_entries = 0;
        ring_wrapped = 0;
        ring_enabled = 1;
        log_info("Ring buffer initialized (%d bytes)", ring_buffer_size);
    } else {
        ring_enabled = 0;
        log_warning("Failed to allocate ring buffer, using fallback logging");
    }
    
    /* Initialize performance counters */
    log_entries_written = 0;
    log_entries_dropped = 0;
    log_buffer_overruns = 0;
    
    log_info("Enhanced logging subsystem initialized");
    return 0;
}

/**
 * @brief Set logging level
 * @param level New logging level
 * @return 0 on success, negative on error
 */
int logging_set_level(int level) {
    if (level < LOG_LEVEL_DEBUG || level > LOG_LEVEL_ERROR) {
        return LOG_ERR_INVALID_LEVEL;
    }
    
    log_level = level;
    log_info("Log level set to %s", level_names[level]);
    return 0;
}

/**
 * @brief Enable or disable console logging
 * @param enable 1 to enable, 0 to disable
 * @return 0 on success
 */
int logging_set_console(int enable) {
    log_to_console = enable;
    if (enable) {
        log_info("Console logging enabled");
    }
    return 0;
}

/**
 * @brief Enable file logging with enhanced error handling
 * @param filename Log file name (NULL for default)
 * @return 0 on success, negative on error
 */
int logging_set_file(const char *filename) {
    /* Close existing file if open */
    if (log_file >= 0) {
        log_info("Closing previous log file: %s", log_filename);
        dos_fclose(log_file);
        log_file = -1;
        log_to_file = 0;
    }

    /* Update filename */
    if (filename) {
        strncpy(log_filename, filename, sizeof(log_filename) - 1);
        log_filename[sizeof(log_filename) - 1] = '\0';
    }

    /* Attempt to open file for append */
    log_file = dos_fopen(log_filename, "a");
    if (log_file >= 0) {
        log_to_file = 1;
        
        /* Write header to log file */
        dos_fwrite("\n=== 3Com Packet Driver Log Started ===\n", 1, 40, log_file);
        dos_fflush(log_file);

        log_info("File logging enabled: %s", log_filename);
        return 0;
    } else {
        log_to_file = 0;
        log_warning("Failed to open log file: %s", log_filename);
        return LOG_ERR_FILE_OPEN;
    }
}

/**
 * @brief Rotate log file when it gets too large
 * @return 0 on success, negative on error
 */
int logging_rotate_file(void) {
    long current_pos;
    char backup_name[132];  /* sizeof(log_filename) + 4 */

    if (log_file < 0 || !log_to_file) {
        return 0; /* No file logging active */
    }

    /* Get current file size - ftell not available with dos_io */
    current_pos = 0; /* TODO: implement dos_ftell */
    if (current_pos < 0) {
        return -1; /* Error getting file position */
    }

    /* Check if rotation is needed (> 1MB) */
    if (current_pos > 1048576) {
        log_info("Rotating log file (size: %ld bytes)", current_pos);

        /* Close current file */
        dos_fclose(log_file);

        /* Create backup filename */
        snprintf(backup_name, sizeof(backup_name), "%s.old", log_filename);

        /* Rename current file to backup */
        rename(log_filename, backup_name);

        /* Open new file */
        log_file = dos_fopen(log_filename, "w");
        if (log_file >= 0) {
            dos_fwrite("=== 3Com Packet Driver Log (Rotated) ===\n", 1, 42, log_file);
            dos_fflush(log_file);
            log_info("Log file rotated successfully");
            return 0;
        } else {
            log_to_file = 0;
            log_error("Failed to open new log file after rotation");
            return LOG_ERR_FILE_OPEN;
        }
    }

    return 0; /* No rotation needed */
}

/**
 * @brief Get current time as string
 * @param buffer Buffer to store time string
 * @param size Buffer size
 */
static void get_time_string(char *buffer, size_t size) {
    /* Time formatting for DOS using system timer */
    /* For now, use a simple counter or DOS time functions */
    
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
    if (log_to_file && log_file >= 0) {
        dos_fwrite(log_buffer, 1, strlen(log_buffer), log_file);
        dos_fwrite("\n", 1, 1, log_file);
        dos_fflush(log_file);
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
 * @brief Set category filter for logging
 * @param categories Bitmask of categories to log
 * @return 0 on success
 */
int logging_set_category_filter(int categories) {
    category_filter = categories;
    log_info("Category filter set to 0x%02X", categories);
    return 0;
}

/**
 * @brief Enable network logging target
 * @param enable 1 to enable, 0 to disable
 * @return 0 on success
 */
int logging_set_network(int enable) {
    log_to_network = enable;
    if (enable) {
        log_info("Network logging enabled");
    } else {
        log_info("Network logging disabled");
    }
    return 0;
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

/* Network logging configuration */
static char network_log_host[64];  /* Initialized at runtime to reduce _DATA segment */
static int network_log_port = 514;                    /* Syslog port */
static int network_log_protocol = 0;                  /* 0=UDP, 1=TCP */

/**
 * @brief Send log message to network target
 * @param message Log message
 */
void log_to_network_target(const char *message) {
    /* C89: All declarations must come before statements */
    static dos_file_t network_log_file = -1;

    /* External function declarations for NIC access */
    extern int hardware_get_nic_count(void);
    extern void* hardware_get_nic(int index);

    if (!message || !log_to_network) {
        return;
    }

    /* Implement actual network transmission for remote logging */
    /* For DOS environment, we'll use a simplified packet construction approach */

    if (!log_to_network) {
        return;  /* Network logging disabled */
    }

    /* For production implementation, construct and send UDP syslog packet */
    /* This is a simplified version - real implementation would:
     * 1. Get proper NIC handle and check link status
     * 2. Construct complete Ethernet/IP/UDP headers
     * 3. Format message as RFC 3164 syslog
     * 4. Handle IP address configuration
     * 5. Implement proper error handling and retries
     */

    /* File-based network logging for debugging */
    if (network_log_file < 0) {
        network_log_file = dos_fopen("NETLOG.TXT", "a");
    }

    if (network_log_file >= 0) {
        dos_fwrite("NET[", 1, 4, network_log_file);
        dos_fwrite(network_log_host, 1, strlen(network_log_host), network_log_file);
        dos_fwrite("]: ", 1, 3, network_log_file);
        dos_fwrite(message, 1, strlen(message), network_log_file);
        dos_fwrite("\n", 1, 1, network_log_file);
        dos_fflush(network_log_file);
    }
    
    /* In actual implementation, this would be something like:
     * 
     * udp_packet_t packet;
     * packet.dest_ip = inet_addr(network_log_host);
     * packet.dest_port = network_log_port;
     * packet.data = message;
     * packet.length = strlen(message);
     * 
     * if (send_udp_packet(&packet) != SUCCESS) {
     *     log_buffer_overruns++;
     * }
     */
}

/**
 * @brief Configure network logging target
 * @param host Target host IP address or hostname
 * @param port Target port number
 * @param protocol 0 for UDP, 1 for TCP
 * @return 0 on success
 */
int logging_set_network_target(const char *host, int port, int protocol) {
    int i;

    if (!host || port <= 0 || port > 65535) {
        return LOG_ERR_INVALID_LEVEL; /* Reuse error code */
    }

    /* Copy host (safely) */
    i = 0;
    while (host[i] && (size_t)i < sizeof(network_log_host) - 1) {
        network_log_host[i] = host[i];
        i++;
    }
    network_log_host[i] = '\0';
    
    network_log_port = port;
    network_log_protocol = protocol;
    
    log_info("Network logging target set to %s:%d (%s)", 
             network_log_host, network_log_port, 
             protocol ? "TCP" : "UDP");
    
    return 0;
}

/**
 * @brief Set ring buffer size (must be called before init)
 * @param size Buffer size in bytes
 * @return 0 on success
 */
int logging_set_ring_buffer_size(int size) {
    if (ring_buffer) {
        return -1; /* Already initialized */
    }
    
    if (size < 1024 || size > 65536) {
        return -1; /* Invalid size */
    }
    
    ring_buffer_size = size;
    return 0;
}

/**
 * @brief Check if ring buffer is enabled
 * @return 1 if enabled, 0 if disabled
 */
int logging_ring_buffer_enabled(void) {
    return ring_enabled;
}

/**
 * @brief Enable or disable logging
 * @param enable 1 to enable, 0 to disable
 * @return 0 on success
 */
int logging_enable(int enable) {
    logging_enabled = enable;
    if (enable) {
        log_info("Logging enabled");
    }
    return 0;
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
 * @brief Cleanup enhanced logging subsystem
 * @return 0 on success
 */
int logging_cleanup(void) {
    if (log_file >= 0) {
        log_info("Closing log file");
        dos_fclose(log_file);
        log_file = -1;
    }

    /* Cleanup ring buffer */
    if (ring_buffer) {
        free(ring_buffer);
        ring_buffer = NULL;
        ring_enabled = 0;
    }

    /* Print final statistics */
    printf("Logging statistics: %lu entries written, %lu dropped, %lu overruns\r\n",
           log_entries_written, log_entries_dropped, log_buffer_overruns);

    logging_enabled = 0;
    return 0;
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
 * @brief Initialize logging with configuration
 * @param config_log_enabled Initial logging enabled state
 * @return 0 on success
 */
int logging_init_with_config(int config_log_enabled) {
    int result;
    result = logging_init();
    if (result == 0) {
        logging_enabled = config_log_enabled;
    }
    return result;
}

/**
 * @brief Configure advanced logging settings
 * @param level Log level
 * @param categories Category filter bitmask
 * @param console_out Enable console output
 * @param file_out Enable file output
 * @param network_out Enable network output
 * @return 0 on success
 */
int logging_configure_advanced(int level, int categories, int console_out, int file_out, int network_out) {
    logging_set_level(level);
    logging_set_category_filter(categories);
    logging_set_console(console_out);
    if (file_out) {
        logging_set_file(NULL);  /* Use default filename */
    }
    logging_set_network(network_out);
    return 0;
}

/**
 * @brief Apply configuration from config structure
 * @param config_ptr Pointer to configuration (opaque)
 * @return 0 on success
 */
int logging_apply_config(const void *config_ptr) {
    /* Placeholder - actual implementation depends on config structure */
    (void)config_ptr;
    return 0;
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

