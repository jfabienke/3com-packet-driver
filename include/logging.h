/**
 * @file logging.h
 * @brief Event logging functionality
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _LOGGING_H_
#define _LOGGING_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */

/* Log levels */
#define LOG_LEVEL_DEBUG     0
#define LOG_LEVEL_INFO      1
#define LOG_LEVEL_WARNING   2
#define LOG_LEVEL_ERROR     3

/* Error codes */
#define LOG_SUCCESS             0
#define LOG_ERR_INVALID_LEVEL  -1
#define LOG_ERR_FILE_OPEN      -2
#define LOG_ERR_NETWORK_CONFIG -3
#define LOG_ERR_NETWORK_SEND   -4

/* Buffer sizes for enhanced logging */
#define LOG_BUFFER_SIZE     256
#define LOG_RING_BUFFER_DEFAULT_SIZE  8192

/* Category definitions for filtering */
#define LOG_CAT_HARDWARE       0x01
#define LOG_CAT_NETWORK        0x02
#define LOG_CAT_MEMORY         0x04
#define LOG_CAT_INTERRUPT      0x08
#define LOG_CAT_PACKET         0x10
#define LOG_CAT_CONFIG         0x20
#define LOG_CAT_PERFORMANCE    0x40
#define LOG_CAT_DRIVER         0x80
#define LOG_CAT_ALL            0xFF

/* Function prototypes */
int logging_init(void);
int logging_set_level(int level);
int logging_set_console(int enable);
int logging_set_file(const char *filename);
int logging_rotate_file(void);
int logging_set_network(int enable);
int logging_set_network_target(const char *host, int port, int protocol);
int logging_set_category_filter(int categories);
void log_at_level(int level, const char *format, ...);
void log_debug(const char *format, ...);
void log_info(const char *format, ...);
void log_warning(const char *format, ...);
void log_error(const char *format, ...);
int logging_enable(int enable);
int logging_is_enabled(void);
int logging_get_level(void);
int logging_cleanup(void);

/* Enhanced logging functions with category filtering */
void log_at_level_with_category(int level, int category, const char *format, ...);
void log_debug_category(int category, const char *format, ...);
void log_info_category(int category, const char *format, ...);
void log_warning_category(int category, const char *format, ...);
void log_error_category(int category, const char *format, ...);
const char* get_category_name(int category);

/* Configuration integration functions */
int logging_init_with_config(int config_log_enabled);
int logging_configure_advanced(int level, int categories, int console_out, int file_out, int network_out);
int logging_apply_config(const void *config_ptr);
void logging_get_config(int *level, int *categories, int *outputs);

/* Enhanced ring buffer functions */
void log_to_ring_buffer(const char *message);
int log_read_ring_buffer(char *buffer, int buffer_size);
int logging_set_ring_buffer_size(int size);
int logging_ring_buffer_enabled(void);
void logging_get_stats(unsigned long *written, unsigned long *dropped, unsigned long *overruns);
void log_to_network_target(const char *message);

/* Convenience macros for category-specific logging */
#define LOG_HW_DEBUG(fmt, ...)    log_debug_category(LOG_CAT_HARDWARE, fmt, ##__VA_ARGS__)
#define LOG_HW_INFO(fmt, ...)     log_info_category(LOG_CAT_HARDWARE, fmt, ##__VA_ARGS__)
#define LOG_HW_WARNING(fmt, ...)  log_warning_category(LOG_CAT_HARDWARE, fmt, ##__VA_ARGS__)
#define LOG_HW_ERROR(fmt, ...)    log_error_category(LOG_CAT_HARDWARE, fmt, ##__VA_ARGS__)

#define LOG_NET_DEBUG(fmt, ...)   log_debug_category(LOG_CAT_NETWORK, fmt, ##__VA_ARGS__)
#define LOG_NET_INFO(fmt, ...)    log_info_category(LOG_CAT_NETWORK, fmt, ##__VA_ARGS__)
#define LOG_NET_WARNING(fmt, ...) log_warning_category(LOG_CAT_NETWORK, fmt, ##__VA_ARGS__)
#define LOG_NET_ERROR(fmt, ...)   log_error_category(LOG_CAT_NETWORK, fmt, ##__VA_ARGS__)

#define LOG_MEM_DEBUG(fmt, ...)   log_debug_category(LOG_CAT_MEMORY, fmt, ##__VA_ARGS__)
#define LOG_MEM_INFO(fmt, ...)    log_info_category(LOG_CAT_MEMORY, fmt, ##__VA_ARGS__)
#define LOG_MEM_WARNING(fmt, ...) log_warning_category(LOG_CAT_MEMORY, fmt, ##__VA_ARGS__)
#define LOG_MEM_ERROR(fmt, ...)   log_error_category(LOG_CAT_MEMORY, fmt, ##__VA_ARGS__)

#define LOG_PKT_DEBUG(fmt, ...)   log_debug_category(LOG_CAT_PACKET, fmt, ##__VA_ARGS__)
#define LOG_PKT_INFO(fmt, ...)    log_info_category(LOG_CAT_PACKET, fmt, ##__VA_ARGS__)
#define LOG_PKT_WARNING(fmt, ...) log_warning_category(LOG_CAT_PACKET, fmt, ##__VA_ARGS__)
#define LOG_PKT_ERROR(fmt, ...)   log_error_category(LOG_CAT_PACKET, fmt, ##__VA_ARGS__)

#define LOG_PERF_DEBUG(fmt, ...)  log_debug_category(LOG_CAT_PERFORMANCE, fmt, ##__VA_ARGS__)
#define LOG_PERF_INFO(fmt, ...)   log_info_category(LOG_CAT_PERFORMANCE, fmt, ##__VA_ARGS__)
#define LOG_PERF_WARNING(fmt, ...) log_warning_category(LOG_CAT_PERFORMANCE, fmt, ##__VA_ARGS__)
#define LOG_PERF_ERROR(fmt, ...)  log_error_category(LOG_CAT_PERFORMANCE, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* _LOGGING_H_ */
