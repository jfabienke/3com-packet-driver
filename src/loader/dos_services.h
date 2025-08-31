/**
 * @file dos_services.h
 * @brief DOS System Services Interface
 * 
 * Provides DOS-specific services like InDOS checking and
 * configuration file handling for TSR context.
 */

#ifndef DOS_SERVICES_H
#define DOS_SERVICES_H

#include <stdio.h>

/* Error codes */
#define ERROR_DOS_BUSY          -1
#define ERROR_FILE_NOT_FOUND    -2
#define ERROR_FILE_WRITE_FAILED -3
#define SUCCESS                 0

/**
 * @brief Configuration line handler callback
 * 
 * @param key Configuration key
 * @param value Configuration value  
 * @param line_number Line number in file
 * @param user_data User-provided data
 * @return SUCCESS if handled, negative error code otherwise
 */
typedef int (*config_line_handler_t)(const char *key, const char *value, 
                                    int line_number, void *user_data);

/**
 * @brief Check if DOS is busy (unsafe to call INT 21h)
 * 
 * @return 1 if DOS is busy, 0 if safe to make DOS calls
 */
int dos_busy(void);

/**
 * @brief Parse key=value line from configuration file
 * 
 * @param line Input line to parse
 * @param key Output buffer for key (must be at least 32 bytes)
 * @param val Output buffer for value (must be at least 96 bytes)
 * @return 1 if parsing successful, 0 if invalid line
 */
int parse_config_line(const char *line, char *key, char *val);

/**
 * @brief Convert string value to boolean
 * 
 * @param str String to convert ("yes", "no", "true", "false", "1", "0")
 * @return 1 for true values, 0 for false values
 */
int string_to_bool(const char *str);

/**
 * @brief Load configuration from DOS-style config file
 * 
 * @param filename Configuration file name
 * @param config_handler Callback function to process each key=value pair
 * @param user_data User data passed to callback
 * @return Number of parameters loaded, negative on error
 */
int load_dos_config_file(const char *filename, 
                        config_line_handler_t config_handler,
                        void *user_data);

#endif /* DOS_SERVICES_H */