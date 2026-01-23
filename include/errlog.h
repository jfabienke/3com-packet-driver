/**
 * @file error_logging.h
 * @brief Comprehensive error logging and diagnostic system
 * 
 * Production-quality logging system addressing GPT-5's recommendation
 * for comprehensive error tracking and diagnostic capabilities.
 */

#ifndef ERROR_LOGGING_H
#define ERROR_LOGGING_H

#include <stdint.h>
#include <stdbool.h>

/* Error severity levels */
typedef enum {
    ERROR_LEVEL_DEBUG = 0,      /* Debug information */
    ERROR_LEVEL_INFO = 1,       /* Informational messages */
    ERROR_LEVEL_WARNING = 2,    /* Warning conditions */
    ERROR_LEVEL_ERROR = 3,      /* Error conditions */
    ERROR_LEVEL_CRITICAL = 4,   /* Critical errors */
    ERROR_LEVEL_FATAL = 5       /* Fatal errors - system unstable */
} error_level_t;

/* Error categories for classification */
typedef enum {
    ERROR_CATEGORY_INIT = 0,        /* Initialization errors */
    ERROR_CATEGORY_HARDWARE = 1,    /* Hardware-related errors */
    ERROR_CATEGORY_MEMORY = 2,      /* Memory management errors */
    ERROR_CATEGORY_NETWORK = 3,     /* Network operation errors */
    ERROR_CATEGORY_VDS = 4,         /* VDS/DMA errors */
    ERROR_CATEGORY_XMS = 5,         /* XMS memory errors */
    ERROR_CATEGORY_IRQ = 6,         /* Interrupt handling errors */
    ERROR_CATEGORY_PACKET = 7,      /* Packet processing errors */
    ERROR_CATEGORY_CONFIG = 8,      /* Configuration errors */
    ERROR_CATEGORY_SYSTEM = 9       /* System-level errors */
} error_category_t;

/* Error log entry */
struct error_log_entry {
    uint32_t timestamp;         /* DOS timer ticks */
    error_level_t level;        /* Severity level */
    error_category_t category;  /* Error category */
    uint16_t error_code;        /* Specific error code */
    uint16_t line_number;       /* Source line number */
    char module[12];            /* Source module name */
    char message[64];           /* Error message */
    uint32_t context_data[2];   /* Additional context data */
};

/* Error logging statistics */
struct error_logging_stats {
    uint32_t total_entries;     /* Total log entries */
    uint32_t debug_count;       /* Debug messages */
    uint32_t info_count;        /* Info messages */
    uint32_t warning_count;     /* Warning messages */
    uint32_t error_count;       /* Error messages */
    uint32_t critical_count;    /* Critical errors */
    uint32_t fatal_count;       /* Fatal errors */
    uint16_t log_full_events;   /* Times log buffer was full */
    uint16_t entries_dropped;   /* Entries dropped due to full buffer */
    
    /* Category breakdown */
    uint16_t category_counts[10]; /* Count per category */
    
    /* Ring buffer status */
    uint16_t buffer_size;       /* Log buffer size */
    uint16_t current_index;     /* Current write position */
    uint16_t oldest_index;      /* Oldest valid entry */
    bool buffer_wrapped;        /* Buffer has wrapped around */
};

/* Error code definitions by category */

/* Initialization error codes (0x0000-0x00FF) */
#define ERR_INIT_DRIVER_LOAD        0x0001
#define ERR_INIT_MEMORY_ALLOC       0x0002
#define ERR_INIT_XMS_UNAVAILABLE    0x0003
#define ERR_INIT_VDS_UNAVAILABLE    0x0004
#define ERR_INIT_CONFIG_INVALID     0x0005
#define ERR_INIT_TSR_INSTALL        0x0006

/* Hardware error codes (0x0100-0x01FF) */
#define ERR_HW_NIC_NOT_FOUND        0x0101
#define ERR_HW_PNP_DETECTION        0x0102
#define ERR_HW_IO_BASE_CONFLICT     0x0103
#define ERR_HW_IRQ_CONFLICT         0x0104
#define ERR_HW_RESET_TIMEOUT        0x0105
#define ERR_HW_EEPROM_READ          0x0106
#define ERR_HW_DMA_SETUP            0x0107
#define ERR_HW_BUS_MASTER_FAIL      0x0108

/* Memory error codes (0x0200-0x02FF) */
#define ERR_MEM_BUFFER_ALLOC        0x0201
#define ERR_MEM_POOL_EXHAUSTED      0x0202
#define ERR_MEM_ALIGNMENT_ERROR     0x0203
#define ERR_MEM_CORRUPTION          0x0204
#define ERR_MEM_LEAK_DETECTED       0x0205
#define ERR_MEM_UMB_ACCESS          0x0206

/* Network error codes (0x0300-0x03FF) */
#define ERR_NET_TX_TIMEOUT          0x0301
#define ERR_NET_RX_OVERRUN          0x0302
#define ERR_NET_PACKET_TOO_LARGE    0x0303
#define ERR_NET_CHECKSUM_ERROR      0x0304
#define ERR_NET_LINK_DOWN           0x0305
#define ERR_NET_COLLISION_LIMIT     0x0306

/* VDS error codes (0x0400-0x04FF) */
#define ERR_VDS_LOCK_FAILED         0x0401
#define ERR_VDS_UNLOCK_FAILED       0x0402
#define ERR_VDS_BOUNDARY_CROSS      0x0403
#define ERR_VDS_HANDLE_LEAK         0x0404
#define ERR_VDS_REGISTRY_FULL       0x0405

/* Function prototypes */

/**
 * Initialize error logging system
 */
int error_logging_init(uint16_t buffer_size);

/**
 * Log error with full context
 */
void error_log_entry(error_level_t level, error_category_t category, 
                     uint16_t error_code, const char *module, uint16_t line,
                     const char *message, uint32_t context1, uint32_t context2);

/**
 * Simplified logging macros for common use
 */
#define LOG_FATAL(code, msg, ctx1, ctx2)    \
    error_log_entry(ERROR_LEVEL_FATAL, ERROR_CATEGORY_SYSTEM, code, __FILE__, __LINE__, msg, ctx1, ctx2)

#define LOG_CRITICAL(category, code, msg, ctx1, ctx2) \
    error_log_entry(ERROR_LEVEL_CRITICAL, category, code, __FILE__, __LINE__, msg, ctx1, ctx2)

#define LOG_ERROR_CTX(category, code, msg, ctx1, ctx2) \
    error_log_entry(ERROR_LEVEL_ERROR, category, code, __FILE__, __LINE__, msg, ctx1, ctx2)

#define LOG_WARNING_CTX(category, code, msg, ctx1, ctx2) \
    error_log_entry(ERROR_LEVEL_WARNING, category, code, __FILE__, __LINE__, msg, ctx1, ctx2)

#define LOG_INFO_CTX(category, msg, ctx1, ctx2) \
    error_log_entry(ERROR_LEVEL_INFO, category, 0, __FILE__, __LINE__, msg, ctx1, ctx2)

#define LOG_DEBUG_CTX(category, msg, ctx1, ctx2) \
    error_log_entry(ERROR_LEVEL_DEBUG, category, 0, __FILE__, __LINE__, msg, ctx1, ctx2)

/**
 * Simplified versions without context data
 */
#define LOG_SIMPLE_ERROR(category, code, msg) \
    LOG_ERROR_CTX(category, code, msg, 0, 0)

#define LOG_SIMPLE_WARNING(category, msg) \
    LOG_WARNING_CTX(category, 0, msg, 0, 0)

#define LOG_SIMPLE_INFO(msg) \
    LOG_INFO_CTX(ERROR_CATEGORY_SYSTEM, msg, 0, 0)

/**
 * Query and export functions
 */
void error_logging_get_stats(struct error_logging_stats *stats);
int error_logging_export_to_file(const char *filename);
int error_logging_get_recent_entries(struct error_log_entry *buffer, 
                                    uint16_t max_entries, error_level_t min_level);

/**
 * Filter and search functions
 */
int error_logging_find_by_category(error_category_t category, 
                                  struct error_log_entry *buffer, uint16_t max_entries);
int error_logging_find_by_code(uint16_t error_code,
                              struct error_log_entry *buffer, uint16_t max_entries);
uint32_t error_logging_count_by_level(error_level_t level);
uint32_t error_logging_count_since_timestamp(uint32_t timestamp);

/**
 * Maintenance and cleanup
 */
void error_logging_clear_old_entries(uint32_t older_than_ticks);
void error_logging_compress_log(void);
int error_logging_health_check(void);

/**
 * Utility functions
 */
const char *error_level_name(error_level_t level);
const char *error_category_name(error_category_t category);
const char *error_code_description(uint16_t error_code);
uint32_t get_dos_timer_ticks(void);

/**
 * Emergency logging for critical situations
 */
void emergency_log_to_screen(const char *message);
void emergency_log_to_serial(const char *message, uint8_t port);

/**
 * Integration with existing logging system
 */
void error_logging_set_output_level(error_level_t min_level);
void error_logging_enable_category(error_category_t category, bool enable);

/* Ring buffer configuration */
#define DEFAULT_LOG_BUFFER_SIZE     256     /* Default number of log entries */
#define MIN_LOG_BUFFER_SIZE         32      /* Minimum buffer size */
#define MAX_LOG_BUFFER_SIZE         1024    /* Maximum buffer size */

/* Severity level filtering */
#define PRODUCTION_LOG_LEVEL        ERROR_LEVEL_WARNING  /* Production minimum level */
#define DEBUG_LOG_LEVEL             ERROR_LEVEL_DEBUG    /* Debug build level */
#define EMERGENCY_LOG_LEVEL         ERROR_LEVEL_CRITICAL /* Emergency-only level */

/**
 * Diagnostic helpers for specific subsystems
 */

/* Hardware diagnostic logging */
#define LOG_HW_ERROR(code, msg) \
    LOG_ERROR_CTX(ERROR_CATEGORY_HARDWARE, code, msg, 0, 0)

#define LOG_HW_INFO(msg, iobase, irq) \
    LOG_INFO_CTX(ERROR_CATEGORY_HARDWARE, msg, iobase, irq)

/* Memory diagnostic logging */
#define LOG_MEM_ERROR(code, msg, addr) \
    LOG_ERROR_CTX(ERROR_CATEGORY_MEMORY, code, msg, (uint32_t)(addr), 0)

#define LOG_MEM_ALLOC(size, addr) \
    LOG_DEBUG_CTX(ERROR_CATEGORY_MEMORY, "Memory allocated", size, (uint32_t)(addr))

/* VDS diagnostic logging */
#define LOG_VDS_ERROR(code, msg, handle) \
    LOG_ERROR_CTX(ERROR_CATEGORY_VDS, code, msg, handle, 0)

#define LOG_VDS_LOCK(handle, phys_addr) \
    LOG_DEBUG_CTX(ERROR_CATEGORY_VDS, "VDS lock", handle, phys_addr)

/* Network diagnostic logging */
#define LOG_NET_ERROR(code, msg, device_id) \
    LOG_ERROR_CTX(ERROR_CATEGORY_NETWORK, code, msg, device_id, 0)

#define LOG_NET_PACKET(msg, size, device_id) \
    LOG_DEBUG_CTX(ERROR_CATEGORY_PACKET, msg, size, device_id)

#endif /* ERROR_LOGGING_H */