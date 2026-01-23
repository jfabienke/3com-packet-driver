/**
 * @file entry_validation.h
 * @brief Entry point validation and environment checks
 * 
 * GPT-5 A+ Enhancement: Phase 0 boot sequence validation
 * Ensures safe environment before any driver initialization
 */

#ifndef _ENTRY_VALIDATION_H_
#define _ENTRY_VALIDATION_H_

#include <stdint.h>
#include <stdbool.h>

/* Default packet driver interrupt vector */
#define DEFAULT_PKT_VECTOR      0x60

/* Valid vector ranges for packet drivers */
#define MIN_USER_VECTOR         0x60
#define MAX_USER_VECTOR         0x7F
#define MIN_ALT_VECTOR          0xC0
#define MAX_ALT_VECTOR          0xCF

/* Error codes */
typedef enum {
    ENTRY_SUCCESS = 0,
    ENTRY_ERR_ALREADY_INSTALLED = -1,
    ENTRY_ERR_VECTOR_IN_USE = -2,
    ENTRY_ERR_INVALID_VECTOR = -3,
    ENTRY_ERR_DOS_VERSION = -4,
    ENTRY_ERR_MEMORY_INSUFFICIENT = -5,
    ENTRY_ERR_CONFLICT = -6
} entry_error_t;

/* Entry validation results */
typedef struct {
    bool driver_already_installed;     /* Packet driver already present */
    bool vector_available;             /* Target vector is free */
    bool dos_compatible;               /* DOS version compatible */
    bool memory_sufficient;            /* Enough memory for TSR */
    uint8_t target_vector;             /* Interrupt vector to use */
    uint8_t existing_vector;           /* Vector of existing driver (if any) */
    uint16_t dos_version;              /* DOS version detected */
    uint32_t free_memory;              /* Free conventional memory */
    char conflict_desc[128];           /* Description of any conflicts */
} entry_validation_t;

/* Command line parsing results */
typedef struct {
    uint8_t vector;                    /* Requested interrupt vector */
    bool uninstall;                    /* Uninstall request */
    bool force;                        /* Force installation */
    bool quiet;                        /* Quiet mode */
    bool verbose;                      /* Verbose output */
    char config_file[128];             /* Configuration file path */
} cmdline_args_t;

/* Function prototypes */

/**
 * @brief Perform comprehensive entry validation
 * 
 * This is the FIRST function called, before any initialization
 * 
 * @param argc Argument count from main()
 * @param argv Argument array from main()
 * @param result Output validation results
 * @return ENTRY_SUCCESS or error code
 */
int entry_validate(int argc, char *argv[], entry_validation_t *result);

/**
 * @brief Parse command line arguments
 * 
 * @param argc Argument count
 * @param argv Argument array
 * @param args Output parsed arguments
 * @return ENTRY_SUCCESS or error code
 */
int parse_command_line(int argc, char *argv[], cmdline_args_t *args);

/**
 * @brief Check if packet driver already installed
 * 
 * @param vector Interrupt vector to check
 * @return true if driver present, false otherwise
 */
bool check_packet_driver_installed(uint8_t vector);

/**
 * @brief Validate interrupt vector is safe to use
 * 
 * @param vector Vector to validate
 * @return true if vector is safe, false otherwise
 */
bool validate_interrupt_vector(uint8_t vector);

/**
 * @brief Check DOS version compatibility
 * 
 * @param min_version Minimum required DOS version (BCD)
 * @return true if compatible, false otherwise
 */
bool check_dos_compatibility(uint16_t min_version);

/**
 * @brief Check if sufficient memory for TSR
 * 
 * @param required_kb Required memory in KB
 * @return true if sufficient, false otherwise
 */
bool check_memory_available(uint16_t required_kb);

/**
 * @brief Get description of entry validation error
 * 
 * @param error Error code
 * @return Human-readable error string
 */
const char* entry_error_string(entry_error_t error);

/**
 * @brief Print entry validation results
 * 
 * @param result Validation results to print
 */
void print_entry_validation(const entry_validation_t *result);

/**
 * @brief Uninstall existing packet driver
 * 
 * @param vector Vector of driver to uninstall
 * @return ENTRY_SUCCESS or error code
 */
int uninstall_packet_driver(uint8_t vector);

#endif /* _ENTRY_VALIDATION_H_ */