/**
 * @file logging_init.c
 * @brief Event logging - initialization functions (OVERLAY segment)
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file contains logging initialization, configuration, and cleanup
 * functions that can be discarded after driver initialization.
 * Runtime logging code is in logging_rt.c.
 *
 * This file is part of the 3Com Packet Driver project.
 *
 * Split from logging.c: 2026-01-28 (ROOT/OVERLAY segmentation)
 */

#include "dos_io.h"
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include "logging.h"

/* Watcom C compatibility - provide snprintf wrapper for this file */
#ifdef __WATCOMC__
#define snprintf watcom_snprintf_init

static int watcom_snprintf_init(char *buf, size_t size, const char *fmt, ...) {
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
#endif

/*
 * Extern declarations for state variables defined in logging_rt.c
 */
extern int logging_enabled;
extern int log_level;
extern dos_file_t log_file;
extern char log_buffer[];
extern int log_to_console;
extern int log_to_file;
extern int log_to_network;
extern char log_filename[];

/* Ring buffer state (defined in logging_rt.c) */
extern char *ring_buffer;
extern int ring_buffer_size;
extern int ring_write_pos;
extern int ring_read_pos;
extern int ring_entries;
extern int ring_wrapped;
extern int ring_enabled;

/* Category filtering (defined in logging_rt.c) */
extern int category_filter;

/* Performance counters (defined in logging_rt.c) */
extern unsigned long log_entries_written;
extern unsigned long log_entries_dropped;
extern unsigned long log_buffer_overruns;

/* Network logging configuration (defined in logging_rt.c) */
extern char network_log_host[];
extern int network_log_port;
extern int network_log_protocol;

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
    memset(log_buffer, 0, LOG_BUFFER_SIZE);

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
    log_info("Log level set to %d", level);
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
        strncpy(log_filename, filename, 127);
        log_filename[127] = '\0';
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
    char backup_name[132];

    if (log_file < 0 || !log_to_file) {
        return 0; /* No file logging active */
    }

    /* Get current file size - ftell not available with dos_io, skip rotation check */
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
    while (host[i] && (size_t)i < 63) {
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
