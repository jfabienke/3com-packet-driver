/**
 * @file eeprom.h
 * @brief EEPROM reading and configuration management for 3Com NICs
 *
 * This header provides comprehensive EEPROM reading functionality with robust
 * timeout protection, error handling, and configuration parsing for both
 * 3C515-TX and 3C509B NICs. This implementation addresses the critical
 * production blocker identified in Sprint 0B.1.
 *
 * Key Features:
 * - Robust timeout protection (10ms maximum wait)
 * - Comprehensive error handling and recovery
 * - MAC address extraction and validation
 * - Hardware configuration parsing
 * - Support for both 3C515 and 3C509B EEPROM formats
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 */

#ifndef _EEPROM_H_
#define _EEPROM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "3c515.h"
#include "3c509b.h"

/* EEPROM Constants */
#define EEPROM_MAX_SIZE             0x40    /* Maximum EEPROM size (64 words) */
#define EEPROM_TIMEOUT_MS           10      /* Maximum timeout for EEPROM operations */
#define EEPROM_RETRY_COUNT          3       /* Number of retries for failed reads */
#define EEPROM_VERIFY_RETRIES       2       /* Verification read attempts */

/* EEPROM Status Codes */
#define EEPROM_SUCCESS              0       /* Operation successful */
#define EEPROM_ERROR_TIMEOUT        -1      /* Operation timed out */
#define EEPROM_ERROR_VERIFY         -2      /* Verification failed */
#define EEPROM_ERROR_INVALID_ADDR   -3      /* Invalid EEPROM address */
#define EEPROM_ERROR_INVALID_DATA   -4      /* Invalid data read */
#define EEPROM_ERROR_HARDWARE       -5      /* Hardware error */
#define EEPROM_ERROR_CHECKSUM       -6      /* Checksum mismatch */
#define EEPROM_ERROR_NOT_PRESENT    -7      /* EEPROM not present/responding */

/* EEPROM Address Definitions */

/* 3C515-TX EEPROM Layout */
#define EEPROM_3C515_MAC_ADDR_0     0x00    /* MAC address bytes 0-1 */
#define EEPROM_3C515_MAC_ADDR_1     0x01    /* MAC address bytes 2-3 */
#define EEPROM_3C515_MAC_ADDR_2     0x02    /* MAC address bytes 4-5 */
#define EEPROM_3C515_DEVICE_ID      0x03    /* Device/Product ID */
#define EEPROM_3C515_MFG_DATE       0x04    /* Manufacturing date */
#define EEPROM_3C515_MFG_DATA       0x05    /* Manufacturing data */
#define EEPROM_3C515_CONFIG_WORD    0x06    /* Configuration word */
#define EEPROM_3C515_VENDOR_ID      0x07    /* 3Com vendor ID (0x6d50) */
#define EEPROM_3C515_SW_CONFIG      0x08    /* Software configuration */
#define EEPROM_3C515_CAPS_WORD      0x09    /* Capabilities word */
#define EEPROM_3C515_CHECKSUM       0x3F    /* Checksum (last word) */

/* 3C509B EEPROM Layout */
#define EEPROM_3C509B_MAC_ADDR_0    0x00    /* MAC address bytes 0-1 */
#define EEPROM_3C509B_MAC_ADDR_1    0x01    /* MAC address bytes 2-3 */
#define EEPROM_3C509B_MAC_ADDR_2    0x02    /* MAC address bytes 4-5 */
#define EEPROM_3C509B_DEVICE_ID     0x03    /* Device/Product ID */
#define EEPROM_3C509B_MFG_DATE      0x04    /* Manufacturing date */
#define EEPROM_3C509B_MFG_DATA      0x05    /* Manufacturing data */
#define EEPROM_3C509B_CONFIG_WORD   0x06    /* Configuration word */
#define EEPROM_3C509B_VENDOR_ID     0x07    /* 3Com vendor ID (0x6d50) */
#define EEPROM_3C509B_IO_CONFIG     0x08    /* I/O base configuration */
#define EEPROM_3C509B_IRQ_CONFIG    0x09    /* IRQ configuration */
#define EEPROM_3C509B_MEDIA_CONFIG  0x0D    /* Media configuration */
#define EEPROM_3C509B_SW_CONFIG     0x14    /* Software configuration */
#define EEPROM_3C509B_CHECKSUM      0x1F    /* Checksum (word 31) */

/* Configuration Word Bit Definitions */
#define EEPROM_CONFIG_MEDIA_MASK    0x0070  /* Media type mask */
#define EEPROM_CONFIG_MEDIA_SHIFT   4       /* Media type bit shift */
#define EEPROM_CONFIG_DUPLEX_BIT    0x0020  /* Full duplex capability */
#define EEPROM_CONFIG_AUTO_SELECT   0x0100  /* Auto-select media */
#define EEPROM_CONFIG_100MBPS_CAP   0x0080  /* 100Mbps capability */

/* Media Type Codes in EEPROM */
#define EEPROM_MEDIA_10BASE_T       0x0     /* 10BaseT */
#define EEPROM_MEDIA_AUI            0x1     /* AUI */
#define EEPROM_MEDIA_BNC            0x3     /* BNC/Coax */
#define EEPROM_MEDIA_100BASE_TX     0x4     /* 100BaseTX */
#define EEPROM_MEDIA_100BASE_FX     0x5     /* 100BaseFX */
#define EEPROM_MEDIA_MII            0x6     /* MII */

/* EEPROM Data Structures */

/**
 * @brief EEPROM configuration data for both NIC types
 */
typedef struct {
    /* Common fields */
    uint8_t  mac_address[6];        /* Ethernet MAC address */
    uint16_t device_id;             /* Device/Product ID */
    uint16_t vendor_id;             /* Vendor ID (should be 0x6d50 for 3Com) */
    uint16_t revision;              /* Hardware revision */
    
    /* Configuration */
    uint16_t config_word;           /* Configuration word from EEPROM */
    uint8_t  media_type;            /* Default media type */
    uint8_t  connector_type;        /* Physical connector type */
    bool     auto_select;           /* Auto-select media capability */
    bool     full_duplex_cap;       /* Full duplex capability */
    bool     speed_100mbps_cap;     /* 100Mbps capability */
    
    /* Hardware-specific */
    uint16_t io_base_config;        /* I/O base address configuration */
    uint8_t  irq_config;            /* IRQ configuration */
    uint16_t capabilities;          /* Hardware capabilities mask */
    
    /* Manufacturing data */
    uint16_t mfg_date;              /* Manufacturing date */
    uint16_t mfg_data;              /* Manufacturing data */
    
    /* Validation */
    uint16_t checksum_calculated;   /* Calculated checksum */
    uint16_t checksum_stored;       /* Stored checksum from EEPROM */
    bool     checksum_valid;        /* Checksum validation result */
    
    /* Status */
    bool     data_valid;            /* Overall data validity */
    int      last_error;            /* Last error code */
    uint32_t read_attempts;         /* Number of read attempts */
} eeprom_config_t;

/**
 * @brief EEPROM read status information
 */
typedef struct {
    uint32_t total_reads;           /* Total read operations */
    uint32_t successful_reads;      /* Successful reads */
    uint32_t timeout_errors;        /* Timeout errors */
    uint32_t verify_errors;         /* Verification errors */
    uint32_t retry_count;           /* Number of retries performed */
    uint32_t max_read_time_us;      /* Maximum read time observed */
    uint32_t avg_read_time_us;      /* Average read time */
} eeprom_stats_t;

/* Core EEPROM Reading Functions */

/**
 * @brief Read complete EEPROM configuration for 3C515-TX with timeout protection
 * @param iobase I/O base address of the NIC
 * @param config Pointer to configuration structure to fill
 * @return EEPROM_SUCCESS on success, negative error code on failure
 */
int read_3c515_eeprom(uint16_t iobase, eeprom_config_t *config);

/**
 * @brief Read complete EEPROM configuration for 3C509B with timeout protection
 * @param iobase I/O base address of the NIC
 * @param config Pointer to configuration structure to fill
 * @return EEPROM_SUCCESS on success, negative error code on failure
 */
int read_3c509b_eeprom(uint16_t iobase, eeprom_config_t *config);

/* Low-level EEPROM Access Functions */

/**
 * @brief Read a single word from 3C515-TX EEPROM with timeout protection
 * @param iobase I/O base address
 * @param address EEPROM word address (0-63)
 * @param data Pointer to store read data
 * @return EEPROM_SUCCESS on success, negative error code on failure
 */
int eeprom_read_word_3c515(uint16_t iobase, uint8_t address, uint16_t *data);

/**
 * @brief Read a single word from 3C509B EEPROM with timeout protection
 * @param iobase I/O base address
 * @param address EEPROM word address (0-31)
 * @param data Pointer to store read data
 * @return EEPROM_SUCCESS on success, negative error code on failure
 */
int eeprom_read_word_3c509b(uint16_t iobase, uint8_t address, uint16_t *data);

/* EEPROM Validation and Parsing Functions */

/**
 * @brief Validate EEPROM checksum
 * @param eeprom_data Array of EEPROM words
 * @param size Number of words in array
 * @param is_3c515 True for 3C515, false for 3C509B
 * @return true if checksum is valid, false otherwise
 */
bool eeprom_validate_checksum(const uint16_t *eeprom_data, int size, bool is_3c515);

/**
 * @brief Parse EEPROM data into configuration structure
 * @param eeprom_data Raw EEPROM data
 * @param size Size of EEPROM data array
 * @param config Configuration structure to fill
 * @param is_3c515 True for 3C515, false for 3C509B
 * @return EEPROM_SUCCESS on success, negative error code on failure
 */
int eeprom_parse_config(const uint16_t *eeprom_data, int size, 
                       eeprom_config_t *config, bool is_3c515);

/**
 * @brief Extract MAC address from EEPROM data
 * @param eeprom_data Raw EEPROM data
 * @param mac_address Buffer to store MAC address (6 bytes)
 * @param is_3c515 True for 3C515, false for 3C509B
 * @return EEPROM_SUCCESS on success, negative error code on failure
 */
int eeprom_extract_mac_address(const uint16_t *eeprom_data, uint8_t *mac_address, bool is_3c515);

/* Hardware Validation Functions */

/**
 * @brief Validate that NIC hardware matches EEPROM configuration
 * @param iobase I/O base address
 * @param config EEPROM configuration
 * @param is_3c515 True for 3C515, false for 3C509B
 * @return EEPROM_SUCCESS if valid, negative error code if invalid
 */
int eeprom_validate_hardware(uint16_t iobase, const eeprom_config_t *config, bool is_3c515);

/**
 * @brief Test EEPROM accessibility and basic functionality
 * @param iobase I/O base address
 * @param is_3c515 True for 3C515, false for 3C509B
 * @return EEPROM_SUCCESS if accessible, negative error code if not
 */
int eeprom_test_accessibility(uint16_t iobase, bool is_3c515);

/* Utility Functions */

/**
 * @brief Get string representation of media type from EEPROM
 * @param media_code Media type code from EEPROM
 * @return String representation of media type
 */
const char* eeprom_media_type_to_string(uint8_t media_code);

/**
 * @brief Get string representation of EEPROM error code
 * @param error_code Error code from EEPROM operations
 * @return String representation of error
 */
const char* eeprom_error_to_string(int error_code);

/**
 * @brief Print detailed EEPROM configuration information
 * @param config EEPROM configuration structure
 * @param label Optional label for output
 */
void eeprom_print_config(const eeprom_config_t *config, const char *label);

/* Diagnostic Functions */

/**
 * @brief Read single EEPROM word for diagnostic purposes (3C509B)
 * @param iobase I/O base address
 * @param address EEPROM address
 * @return EEPROM word value, 0xFFFF on error
 */
uint16_t nic_read_eeprom_3c509b(uint16_t iobase, uint8_t address);

/**
 * @brief Read single EEPROM word for diagnostic purposes (3C515)
 * @param iobase I/O base address
 * @param address EEPROM address
 * @return EEPROM word value, 0xFFFF on error
 */
uint16_t nic_read_eeprom_3c515(uint16_t iobase, uint8_t address);

/**
 * @brief Dump entire EEPROM contents for debugging
 * @param iobase I/O base address
 * @param is_3c515 True for 3C515, false for 3C509B
 * @param output_buffer Buffer to store formatted output
 * @param buffer_size Size of output buffer
 * @return Number of bytes written to buffer
 */
int eeprom_dump_contents(uint16_t iobase, bool is_3c515, char *output_buffer, size_t buffer_size);

/* Statistics and Monitoring */

/**
 * @brief Get EEPROM operation statistics
 * @param stats Pointer to statistics structure to fill
 */
void eeprom_get_stats(eeprom_stats_t *stats);

/**
 * @brief Clear EEPROM operation statistics
 */
void eeprom_clear_stats(void);

/**
 * @brief Initialize EEPROM subsystem
 * @return EEPROM_SUCCESS on success, negative error code on failure
 */
int eeprom_init(void);

/**
 * @brief Cleanup EEPROM subsystem
 */
void eeprom_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* _EEPROM_H_ */