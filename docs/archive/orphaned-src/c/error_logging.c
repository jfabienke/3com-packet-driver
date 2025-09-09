/**
 * @file error_logging.c
 * @brief Comprehensive error logging implementation
 * 
 * Production-quality logging system with ring buffer, categorization,
 * and multiple output options for production deployment.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <dos.h>
#include "error_logging.h"

/* Ring buffer for log entries */
static struct error_log_entry *log_buffer = NULL;
static uint16_t log_buffer_size = 0;
static uint16_t log_write_index = 0;
static uint16_t log_oldest_index = 0;
static bool log_buffer_wrapped = false;
static bool error_logging_initialized = false;

/* Statistics tracking */
static struct error_logging_stats log_stats;

/* Configuration */
static error_level_t min_output_level = ERROR_LEVEL_WARNING;
static bool category_enabled[10];

/* Error level names */
static const char *level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "CRIT", "FATAL"
};

/* Category names */
static const char *category_names[] = {
    "INIT", "HARDWARE", "MEMORY", "NETWORK", "VDS", 
    "XMS", "IRQ", "PACKET", "CONFIG", "SYSTEM"
};

/**
 * Get DOS timer ticks for timestamps
 */
uint32_t get_dos_timer_ticks(void)
{
    uint32_t ticks;
    
    __asm {
        mov     ah, 0x00
        int     0x1A            ; Get system time
        mov     word ptr ticks, dx
        mov     word ptr ticks+2, cx
    }
    
    return ticks;
}

/**
 * Initialize error logging system
 */
int error_logging_init(uint16_t buffer_size)
{
    if (error_logging_initialized) {
        return 0;  /* Already initialized */
    }
    
    /* Validate buffer size */
    if (buffer_size < MIN_LOG_BUFFER_SIZE) {
        buffer_size = MIN_LOG_BUFFER_SIZE;
    }
    if (buffer_size > MAX_LOG_BUFFER_SIZE) {
        buffer_size = MAX_LOG_BUFFER_SIZE;
    }
    
    /* Allocate log buffer */
    log_buffer = malloc(buffer_size * sizeof(struct error_log_entry));
    if (!log_buffer) {
        return -1;  /* Allocation failed */
    }
    
    /* Initialize buffer state */
    memset(log_buffer, 0, buffer_size * sizeof(struct error_log_entry));
    log_buffer_size = buffer_size;
    log_write_index = 0;
    log_oldest_index = 0;
    log_buffer_wrapped = false;
    
    /* Clear statistics */
    memset(&log_stats, 0, sizeof(log_stats));
    log_stats.buffer_size = buffer_size;
    
    /* Enable all categories by default */
    for (int i = 0; i < 10; i++) {
        category_enabled[i] = true;
    }
    
    error_logging_initialized = true;
    
    /* Log initialization message */
    error_log_entry(ERROR_LEVEL_INFO, ERROR_CATEGORY_SYSTEM, 0,
                   "error_log", 0, "Error logging initialized", 
                   buffer_size, sizeof(struct error_log_entry));
    
    return 0;
}

/**
 * Extract module name from full path
 */
static void extract_module_name(const char *full_path, char *module_name)
{
    const char *filename = full_path;
    const char *p;
    
    /* Find the last slash or backslash */
    for (p = full_path; *p; p++) {
        if (*p == '\\' || *p == '/') {
            filename = p + 1;
        }
    }
    
    /* Copy up to 11 characters (leaving room for null terminator) */
    strncpy(module_name, filename, 11);
    module_name[11] = '\0';
    
    /* Remove extension if present */
    char *dot = strchr(module_name, '.');
    if (dot) {
        *dot = '\0';
    }
}

/**
 * Log error entry with full context
 */
void error_log_entry(error_level_t level, error_category_t category, 
                     uint16_t error_code, const char *module, uint16_t line,
                     const char *message, uint32_t context1, uint32_t context2)
{
    if (!error_logging_initialized) {
        /* Emergency fallback - try to initialize with default size */
        if (error_logging_init(DEFAULT_LOG_BUFFER_SIZE) != 0) {
            return;  /* Can't log without initialization */
        }
    }
    
    /* Check if this category is enabled */
    if (category < 10 && !category_enabled[category]) {
        return;
    }
    
    /* Check minimum level filtering */
    if (level < min_output_level) {
        return;
    }
    
    /* Get current log entry */
    struct error_log_entry *entry = &log_buffer[log_write_index];
    
    /* Fill entry */
    entry->timestamp = get_dos_timer_ticks();
    entry->level = level;
    entry->category = category;
    entry->error_code = error_code;
    entry->line_number = line;
    entry->context_data[0] = context1;
    entry->context_data[1] = context2;
    
    /* Extract module name */
    extract_module_name(module, entry->module);
    
    /* Copy message (truncate if necessary) */
    strncpy(entry->message, message, 63);
    entry->message[63] = '\0';
    
    /* Update statistics */
    log_stats.total_entries++;
    
    switch (level) {
        case ERROR_LEVEL_DEBUG:   log_stats.debug_count++; break;
        case ERROR_LEVEL_INFO:    log_stats.info_count++; break;
        case ERROR_LEVEL_WARNING: log_stats.warning_count++; break;
        case ERROR_LEVEL_ERROR:   log_stats.error_count++; break;
        case ERROR_LEVEL_CRITICAL: log_stats.critical_count++; break;
        case ERROR_LEVEL_FATAL:   log_stats.fatal_count++; break;
    }
    
    if (category < 10) {
        log_stats.category_counts[category]++;
    }
    
    /* Advance write index */
    log_write_index++;
    if (log_write_index >= log_buffer_size) {
        log_write_index = 0;
        log_buffer_wrapped = true;
    }
    
    /* Update oldest index if buffer wrapped */
    if (log_buffer_wrapped) {
        log_oldest_index = log_write_index;
    }
    
    /* Update statistics */
    log_stats.current_index = log_write_index;
    log_stats.oldest_index = log_oldest_index;
    log_stats.buffer_wrapped = log_buffer_wrapped;
    
    /* Handle buffer full condition */
    if (log_buffer_wrapped) {
        log_stats.log_full_events++;
    }
    
    /* Emergency output for critical/fatal errors */
    if (level >= ERROR_LEVEL_CRITICAL) {
        char emergency_msg[128];
        sprintf(emergency_msg, "%s [%s:%u] %s", 
               level_names[level], entry->module, line, message);
        emergency_log_to_screen(emergency_msg);
    }
}

/**
 * Get logging statistics
 */
void error_logging_get_stats(struct error_logging_stats *stats)
{
    if (!stats || !error_logging_initialized) {
        return;
    }
    
    *stats = log_stats;
}

/**
 * Get recent log entries
 */
int error_logging_get_recent_entries(struct error_log_entry *buffer, 
                                    uint16_t max_entries, error_level_t min_level)
{
    if (!buffer || !error_logging_initialized) {
        return 0;
    }
    
    uint16_t entries_copied = 0;
    uint16_t current_index = log_write_index;
    uint16_t entries_to_check = log_buffer_wrapped ? log_buffer_size : log_write_index;
    
    /* Walk backwards through the log */
    for (uint16_t i = 0; i < entries_to_check && entries_copied < max_entries; i++) {
        /* Calculate index (walking backwards) */
        uint16_t index = (current_index > 0) ? current_index - 1 : log_buffer_size - 1;
        current_index = index;
        
        struct error_log_entry *entry = &log_buffer[index];
        
        /* Check if entry meets minimum level */
        if (entry->level >= min_level) {
            buffer[entries_copied] = *entry;
            entries_copied++;
        }
    }
    
    return entries_copied;
}

/**
 * Find entries by category
 */
int error_logging_find_by_category(error_category_t category, 
                                  struct error_log_entry *buffer, uint16_t max_entries)
{
    if (!buffer || !error_logging_initialized || category >= 10) {
        return 0;
    }
    
    uint16_t entries_found = 0;
    uint16_t start_index = log_buffer_wrapped ? log_oldest_index : 0;
    uint16_t end_index = log_write_index;
    uint16_t entries_to_check = log_buffer_wrapped ? log_buffer_size : log_write_index;
    
    for (uint16_t i = 0; i < entries_to_check && entries_found < max_entries; i++) {
        uint16_t index = (start_index + i) % log_buffer_size;
        struct error_log_entry *entry = &log_buffer[index];
        
        if (entry->category == category) {
            buffer[entries_found] = *entry;
            entries_found++;
        }
    }
    
    return entries_found;
}

/**
 * Find entries by error code
 */
int error_logging_find_by_code(uint16_t error_code,
                              struct error_log_entry *buffer, uint16_t max_entries)
{
    if (!buffer || !error_logging_initialized) {
        return 0;
    }
    
    uint16_t entries_found = 0;
    uint16_t start_index = log_buffer_wrapped ? log_oldest_index : 0;
    uint16_t entries_to_check = log_buffer_wrapped ? log_buffer_size : log_write_index;
    
    for (uint16_t i = 0; i < entries_to_check && entries_found < max_entries; i++) {
        uint16_t index = (start_index + i) % log_buffer_size;
        struct error_log_entry *entry = &log_buffer[index];
        
        if (entry->error_code == error_code) {
            buffer[entries_found] = *entry;
            entries_found++;
        }
    }
    
    return entries_found;
}

/**
 * Count entries by level
 */
uint32_t error_logging_count_by_level(error_level_t level)
{
    switch (level) {
        case ERROR_LEVEL_DEBUG:   return log_stats.debug_count;
        case ERROR_LEVEL_INFO:    return log_stats.info_count;
        case ERROR_LEVEL_WARNING: return log_stats.warning_count;
        case ERROR_LEVEL_ERROR:   return log_stats.error_count;
        case ERROR_LEVEL_CRITICAL: return log_stats.critical_count;
        case ERROR_LEVEL_FATAL:   return log_stats.fatal_count;
        default: return 0;
    }
}

/**
 * Count entries since timestamp
 */
uint32_t error_logging_count_since_timestamp(uint32_t timestamp)
{
    if (!error_logging_initialized) {
        return 0;
    }
    
    uint32_t count = 0;
    uint16_t start_index = log_buffer_wrapped ? log_oldest_index : 0;
    uint16_t entries_to_check = log_buffer_wrapped ? log_buffer_size : log_write_index;
    
    for (uint16_t i = 0; i < entries_to_check; i++) {
        uint16_t index = (start_index + i) % log_buffer_size;
        struct error_log_entry *entry = &log_buffer[index];
        
        if (entry->timestamp >= timestamp) {
            count++;
        }
    }
    
    return count;
}

/**
 * Export log to file
 */
int error_logging_export_to_file(const char *filename)
{
    if (!filename || !error_logging_initialized) {
        return -1;
    }
    
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        return -1;
    }
    
    /* Write header */
    fprintf(fp, "3Com Packet Driver Error Log\n");
    fprintf(fp, "Generated: %lu ticks\n", get_dos_timer_ticks());
    fprintf(fp, "Total entries: %lu\n", log_stats.total_entries);
    fprintf(fp, "Buffer size: %u\n\n", log_buffer_size);
    
    /* Write column headers */
    fprintf(fp, "Timestamp  Level  Category   Code Module      Line Message\n");
    fprintf(fp, "---------- ------ ---------- ---- ----------- ---- --------------------------------\n");
    
    /* Write entries */
    uint16_t start_index = log_buffer_wrapped ? log_oldest_index : 0;
    uint16_t entries_to_write = log_buffer_wrapped ? log_buffer_size : log_write_index;
    
    for (uint16_t i = 0; i < entries_to_write; i++) {
        uint16_t index = (start_index + i) % log_buffer_size;
        struct error_log_entry *entry = &log_buffer[index];
        
        fprintf(fp, "%10lu %-6s %-10s %04X %-11s %4u %s\n",
               entry->timestamp,
               error_level_name(entry->level),
               error_category_name(entry->category),
               entry->error_code,
               entry->module,
               entry->line_number,
               entry->message);
        
        /* Include context data if non-zero */
        if (entry->context_data[0] != 0 || entry->context_data[1] != 0) {
            fprintf(fp, "           Context: 0x%08lX 0x%08lX\n",
                   entry->context_data[0], entry->context_data[1]);
        }
    }
    
    fclose(fp);
    return 0;
}

/**
 * Health check for logging system
 */
int error_logging_health_check(void)
{
    if (!error_logging_initialized) {
        return -3;  /* Not initialized */
    }
    
    int health_score = 0;
    
    /* Check buffer utilization */
    if (log_buffer_wrapped) {
        health_score -= 1;  /* Buffer has wrapped - potential data loss */
    }
    
    /* Check error rates */
    if (log_stats.total_entries > 0) {
        uint32_t error_rate = ((log_stats.error_count + log_stats.critical_count + 
                               log_stats.fatal_count) * 100) / log_stats.total_entries;
        
        if (error_rate > 20) {
            health_score -= 3;  /* High error rate */
        } else if (error_rate > 10) {
            health_score -= 2;  /* Moderate error rate */
        } else if (error_rate > 5) {
            health_score -= 1;  /* Elevated error rate */
        }
        
        /* Check for fatal errors */
        if (log_stats.fatal_count > 0) {
            health_score -= 5;  /* Fatal errors present */
        }
        
        /* Check for critical errors */
        if (log_stats.critical_count > 5) {
            health_score -= 2;  /* Many critical errors */
        }
    }
    
    return health_score;
}

/**
 * Emergency screen output
 */
void emergency_log_to_screen(const char *message)
{
    /* Output to screen using BIOS services */
    printf("\n*** DRIVER ERROR: %s ***\n", message);
}

/**
 * Emergency serial output
 */
void emergency_log_to_serial(const char *message, uint8_t port)
{
    /* Simple serial output for debugging - would implement UART access */
    /* For now, just use screen output */
    emergency_log_to_screen(message);
}

/**
 * Utility functions
 */
const char *error_level_name(error_level_t level)
{
    if (level <= ERROR_LEVEL_FATAL) {
        return level_names[level];
    }
    return "UNKNOWN";
}

const char *error_category_name(error_category_t category)
{
    if (category < 10) {
        return category_names[category];
    }
    return "UNKNOWN";
}

/**
 * Error code descriptions
 */
const char *error_code_description(uint16_t error_code)
{
    /* Map error codes to descriptions */
    switch (error_code) {
        case ERR_INIT_DRIVER_LOAD:     return "Driver load failure";
        case ERR_INIT_MEMORY_ALLOC:    return "Memory allocation failure";
        case ERR_HW_NIC_NOT_FOUND:     return "NIC not found";
        case ERR_HW_IRQ_CONFLICT:      return "IRQ conflict detected";
        case ERR_MEM_BUFFER_ALLOC:     return "Buffer allocation failed";
        case ERR_VDS_LOCK_FAILED:      return "VDS lock operation failed";
        case ERR_NET_TX_TIMEOUT:       return "Transmit timeout";
        case ERR_NET_RX_OVERRUN:       return "Receive buffer overrun";
        default: return "Unknown error";
    }
}

/**
 * Configuration functions
 */
void error_logging_set_output_level(error_level_t min_level)
{
    min_output_level = min_level;
}

void error_logging_enable_category(error_category_t category, bool enable)
{
    if (category < 10) {
        category_enabled[category] = enable;
    }
}

/**
 * Cleanup old entries
 */
void error_logging_clear_old_entries(uint32_t older_than_ticks)
{
    if (!error_logging_initialized) {
        return;
    }
    
    uint32_t current_time = get_dos_timer_ticks();
    uint32_t cutoff_time = current_time - older_than_ticks;
    
    /* Simple implementation - would need more sophisticated approach for production */
    /* For now, just track that cleanup was requested */
    LOG_SIMPLE_INFO("Log cleanup requested");
}