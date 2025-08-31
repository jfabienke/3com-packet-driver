/**
 * @file tsr_file_io.h  
 * @brief TSR-Safe File I/O System Interface
 * 
 * Provides safe file operations for TSR context using INT 28h
 * and InDOS checking to avoid DOS reentrancy issues.
 */

#ifndef TSR_FILE_IO_H
#define TSR_FILE_IO_H

#include <stdint.h>

/* File I/O result codes */
#define TSRFILE_SUCCESS           0
#define TSRFILE_ERROR_INVALID    -1
#define TSRFILE_ERROR_ACCESS     -2
#define TSRFILE_ERROR_BUSY       -3
#define TSRFILE_ERROR_DISK       -4
#define TSRFILE_ERROR_BUFFER     -5

/* Ring buffer for IRQ-safe logging */
#define TSRFILE_RING_SIZE        256
#define TSRFILE_MAX_ENTRY_SIZE   256
#define TSRFILE_MAX_PATH_SIZE    128

typedef struct {
    char message[TSRFILE_MAX_ENTRY_SIZE];
    uint16_t length;
    uint32_t timestamp;
    uint8_t level;
    uint8_t flags;
} tsr_log_entry_t;

typedef struct {
    tsr_log_entry_t entries[TSRFILE_RING_SIZE];
    uint16_t write_pos;
    uint16_t read_pos;
    uint16_t count;
    uint8_t wrapped;
    uint8_t initialized;
} tsr_ring_buffer_t;

/* File handle structure */
typedef struct {
    int dos_handle;           /* DOS file handle */
    char path[TSRFILE_MAX_PATH_SIZE];
    uint32_t current_size;
    uint32_t max_size;
    uint8_t open;
    uint8_t rotation_enabled;
    uint16_t rotation_count;  /* Current .001, .002, etc */
} tsr_file_handle_t;

/**
 * @brief Initialize TSR file I/O system
 * 
 * @return TSRFILE_SUCCESS or error code
 */
int tsr_file_io_init(void);

/**
 * @brief Open file for TSR-safe writing
 * 
 * @param handle File handle structure to initialize
 * @param path File path (max 127 chars)
 * @param max_size Maximum file size (0 = no limit)
 * @return TSRFILE_SUCCESS or error code
 */
int tsr_file_open(tsr_file_handle_t *handle, const char *path, uint32_t max_size);

/**
 * @brief Write string to file (TSR-safe)
 * 
 * Uses INT 28h mechanism to defer write until DOS is idle.
 * 
 * @param handle Open file handle
 * @param data Data to write
 * @param length Length of data
 * @return TSRFILE_SUCCESS or error code
 */
int tsr_file_write(tsr_file_handle_t *handle, const char *data, uint16_t length);

/**
 * @brief Write formatted string to file
 * 
 * @param handle Open file handle
 * @param format Printf-style format string
 * @param ... Format arguments
 * @return TSRFILE_SUCCESS or error code
 */
int tsr_file_printf(tsr_file_handle_t *handle, const char *format, ...);

/**
 * @brief Add entry to ring buffer (IRQ-safe)
 * 
 * @param entry Log entry to add
 * @return TSRFILE_SUCCESS or error code
 */
int tsr_ring_buffer_add(const tsr_log_entry_t *entry);

/**
 * @brief Flush ring buffer to files (call from INT 28h)
 * 
 * @return Number of entries flushed
 */
int tsr_ring_buffer_flush(void);

/**
 * @brief Rotate file when size limit reached
 * 
 * @param handle File handle
 * @return TSRFILE_SUCCESS or error code  
 */
int tsr_file_rotate(tsr_file_handle_t *handle);

/**
 * @brief Close file handle
 * 
 * @param handle File handle to close
 */
void tsr_file_close(tsr_file_handle_t *handle);

/**
 * @brief Check if DOS is idle (safe for file I/O)
 * 
 * @return 1 if safe, 0 if busy
 */
int tsr_file_dos_idle(void);

/**
 * @brief Get ring buffer statistics
 * 
 * @param entries_buffered Number of entries in buffer
 * @param buffer_overflows Number of overflows
 */
void tsr_file_get_stats(uint16_t *entries_buffered, uint16_t *buffer_overflows);

/**
 * @brief Cleanup file I/O system
 */
void tsr_file_io_cleanup(void);

/* Configuration */
#define TSRFILE_DEFAULT_MAX_SIZE   (100 * 1024)  /* 100KB default */
#define TSRFILE_ROTATION_EXT_MAX   99            /* .001 to .099 */

#endif /* TSR_FILE_IO_H */